#include <errno.h>
#include <lzma.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zstd.h>

#include "utils.h"
#include "zim.h"

#define MAX_MIME_TYPES_LEN 10000
#define COMPRESSION_XZ 4
#define COMPRESSION_ZSTD 5
#define MAX_ARTICLE_SIZE 10000000
#define MIME_TYPE_REDIRECT 0xffff
#define MIME_TYPE_REDLINK 0xfffe
#define MIME_TYPE_DELETED 0xfffd

typedef struct {
  unsigned int magic_number;
  unsigned short int major_version;
  unsigned short int minor_version;
  unsigned long long int uuid;
  unsigned int article_count;
  unsigned int cluster_count;
  unsigned long int url_ptr_pos;
  unsigned long int title_ptr_pos;
  unsigned long int dir_entries_pos;
  unsigned long int cluster_ptr_pos;
  unsigned long int mime_list_pos;
  unsigned int main_page;
  unsigned int layout_page;
  unsigned long int checksum_pos;
} zim_header_t;

typedef struct {
  char **items;
  size_t len;
} zim_mime_type_list_t;

typedef struct {
  char *path;
  zim_header_t *header;
  zim_mime_type_list_t *mime_type_list;
} zim_archive_t;

typedef struct {
  unsigned short int mime_type;
  char namespace;
  unsigned int revision;
  unsigned int redirect_index;
  unsigned int cluster_number;
  unsigned int blob_number;
  char *url;
  char *title;
} zim_directory_entry_t;

/*
 * Helper to decode a single integer, of `len` capacity, from the given
 * zimfile.
 *
 * Accepted values for `len` are 2, 4, 8 and 16 bytes. The result will be
 * placed in `dest`, which should be respectively a pointer to an unsigned
 * short int, an unsigned int, an unsigned long int and an unsigned long long
 * int.
 *
 * Return non-zero in case of error.
 * 
 */
static int
read_int (FILE *file, size_t len, void *dest)
{
  int err = 0;
  void *buf = xalloc (len);
  size_t r = fread (buf, 1, len, file);
  if (r != len)
    {
      err = 1;
      fprintf (stderr, "zim.c : read_int() : could not read value from zimfile.\n");
      goto cleanup;
    }

  switch (len)
    {
      case 2:
        *(unsigned short int *) dest = *(unsigned short int *) buf;
        break;

      case 4:
        *(unsigned int *) dest = *(unsigned int *) buf;
        break;

      case 8:
        *(unsigned long int *) dest = *(unsigned long int *) buf;
        break;

      case 16:
        *(unsigned long long int *) dest = *(unsigned long long int *) buf;
        break;

      default:
        err = 1;
        fprintf (stderr, "zim.c : read_int() : unrecognized length for int : %ld.\n", len);
        goto cleanup;
    }

  cleanup:
  if (buf) free (buf);
  return err;
}

/*
 * Same as read_int(), but reading from a buffer rather than a file.
 * Used to read compressed data.
 *
 * Return non-zero in case of error.
 * 
 */
static int
read_int_from_buf (const char *buf, size_t len, void *dest)
{
  int err = 0;
  char *tmp = xalloc (len);
  for (size_t i = 0; i < len; i++)
    tmp[i] = buf[i];

  switch (len)
    {
      case 2:
        *(unsigned short int *) dest = *(unsigned short int *) tmp;
        break;

      case 4:
        *(unsigned int *) dest = *(unsigned int *) tmp;
        break;

      case 8:
        *(unsigned long int *) dest = *(unsigned long int *) tmp;
        break;

      case 16:
        *(unsigned long long int *) dest = *(unsigned long long int *) tmp;
        break;

      default:
        err = 1;
        fprintf (stderr, "zim.c : read_int_from_buf() : unrecognized length for int : %ld.\n", len);
        goto cleanup;
    }

  cleanup:
  if (tmp) free (tmp);
  return err;
}

/*
 * Parse the header of the zimfile, containing metadata and position
 * of important blocks.
 *
 * Return non-zero on error.
 */
static int
parse_headers (zim_header_t *header, FILE *file)
{
  int err = read_int (file, 2, &(header->major_version));
  if (err)
    {
      fprintf (stderr, "zim.c : parse_headers() : malformed headers : can't read major version.\n");
      return 1;
    }

  err = read_int (file, 2, &(header->minor_version));
  if (err)
    {
      fprintf (stderr, "zim.c : parse_headers() : malformed headers : can't read minor version.\n");
      return 1;
    }

  err = read_int (file, 16, &(header->uuid));
  if (err)
    {
      fprintf (stderr, "zim.c : parse_headers() : malformed headers : can't read uuid.\n");
      return 1;
    }

  err = read_int (file, 4, &(header->article_count));
  if (err)
    {
      fprintf (stderr, "zim.c : parse_headers() : malformed headers : can't read article count.\n");
      return 1;
    }

  err = read_int (file, 4, &(header->cluster_count));
  if (err)
    {
      fprintf (stderr, "zim.c : parse_headers() : malformed headers : can't read cluster count.\n");
      return 1;
    }

  err = read_int (file, 8, &(header->url_ptr_pos));
  if (err)
    {
      fprintf (stderr, "zim.c : parse_headers() : malformed headers : can't read url pointer position.\n");
      return 1;
    }

  err = read_int (file, 8, &(header->title_ptr_pos));
  if (err)
    {
      fprintf (stderr, "zim.c : parse_headers() : malformed headers : can't read title pointer position.\n");
      return 1;
    }

  err = read_int (file, 8, &(header->cluster_ptr_pos));
  if (err)
    {
      fprintf (stderr, "zim.c : parse_headers() : malformed headers : can't read cluster pointer position.\n");
      return 1;
    }

  err = read_int (file, 8, &(header->mime_list_pos));
  if (err)
    {
      fprintf (stderr, "zim.c : parse_headers() : malformed headers : can't read mime list position.\n");
      return 1;
    }

  err = read_int (file, 4, &(header->main_page));
  if (err)
    {
      fprintf (stderr, "zim.c : parse_headers() : malformed headers : can't read main page position.\n");
      return 1;
    }

  err = read_int (file, 4, &(header->layout_page));
  if (err)
    {
      fprintf (stderr, "zim.c : parse_headers() : malformed headers : can't read layout page position.\n");
      return 1;
    }

  err = read_int (file, 4, &(header->checksum_pos));
  if (err)
    {
      fprintf (stderr, "zim.c : parse_headers() : malformed headers : can't read checksum position.\n");
      return 1;
    }

  return 0;
}

/*
 * Find the list of mime-types in the archive, and populate
 * archive->mime_type_list.
 */
static void
parse_mime_type_list (const zim_archive_t *archive, FILE *file)
{
  int continue_parsing = 1;
  zim_mime_type_list_t *list = archive->mime_type_list;
  fseek (file, archive->header->mime_list_pos, SEEK_SET);

  while (continue_parsing)
    {
      char *buf = xalloc (101);
      size_t pos = 0;
      while (pos < 100)
        {
          int r = fread (buf + pos, 1, 1, file);
          if (r != 1)
            {
              fprintf (stderr, "parse_mime_type_list() : can't read file.\n");
              exit (1);
            }

          if (buf[pos] == 0)
            break;

          pos++;
        }

      if (strncmp (buf, "", 100) == 0)
        {
          continue_parsing = 0;
          free (buf);
        }
      else
        {
          list->len++;

          if (list->len > MAX_MIME_TYPES_LEN)
            {
              fprintf (stderr, "zim.c : parse_mime_type_list() : maximum number of header exceeded, ignoring the rest.\n");
              return;
            }

          list->items = xrealloc (list->items, list->len * sizeof (char *));
          list->items[list->len - 1] = buf;
        }
    }
}

static zim_archive_t *
new_zim_archive ()
{
  zim_archive_t *archive = xalloc (sizeof (*archive));
  archive->header = xalloc (sizeof (*archive->header));
  archive->mime_type_list = xalloc (sizeof (*archive->mime_type_list));
  archive->path = NULL;

  return archive;
}

static void
free_zim_mime_type_list (zim_mime_type_list_t *list)
{
  if (!list) return;

  if (list->items)
    {
      for (size_t i = 0; i < list->len; i++)
        free (list->items[i]);
      free (list->items);
    }

  free (list);
}

static void
free_zim_archive (zim_archive_t *archive)
{
  if (!archive) return;

  if (archive->header) free (archive->header);
  if (archive->mime_type_list) free_zim_mime_type_list (archive->mime_type_list);
  if (archive->path) free (archive->path);

  free (archive);
}

static void
free_zim_directory_entry (zim_directory_entry_t *entry)
{
  if (!entry) return;

  if (entry->url) free (entry->url);
  if (entry->title) free (entry->title);

  free (entry);
}

/*
 * Read an entry in the index table. This is where url and title resides, plus
 * address of full content.
 *
 * You must allocate memory for `entry`.
 *
 * Return non-zero in case of error.
 */
static int
parse_directory_entry (FILE *file, zim_directory_entry_t *entry)
{
  int err = 0;
  int r = 0;

  err = read_int (file, 2, &entry->mime_type);
  if (err)
    {
      fprintf (stderr, "zim.c : parse_directory_entry() : malformed zimfile : can't read entry mime type.\n");
      goto cleanup;
    }

  if (fseek (file, 1, SEEK_CUR) == -1)
    {
      err = 1;
      fprintf (stderr, "zim.c : parse_directory_entry() : can't access file anymore.\n");
      goto cleanup;
    }

  r = fread (&entry->namespace, 1, 1, file);
  if (r != 1)
    {
      err = 1;
      fprintf (stderr, "zim.c : parse_directory_entry() : can't read file anymore.\n");
      goto cleanup;
    }

  err = read_int (file, 4, &entry->revision);
  if (err)
    {
      fprintf (stderr, "zim.c : parse_directory_entry() : malformed zimfile : can't read revision.\n");
      goto cleanup;
    }

  if (entry->mime_type == MIME_TYPE_REDIRECT)
    {
      err = read_int (file, 4, &entry->redirect_index);
      if (err)
        {
          fprintf (stderr, "zim.c : parse_directory_entry() : malformed zimfile : can't read redirect index.\n");
          goto cleanup;
        }
    }
  else
    {
      err = read_int (file, 4, &entry->cluster_number);
      if (err)
        {
          fprintf (stderr, "zim.c : parse_directory_entry() : malformed zimfile : can't read cluster number.\n");
          goto cleanup;
        }

      err = read_int (file, 4, &entry->blob_number);
      if (err)
        {
          fprintf (stderr, "zim.c : parse_directory_entry() : malformed zimfile : can't read blob number.\n");
          goto cleanup;
        }
    }

  entry->url = xalloc (1001);
  entry->title = xalloc (1001);

  for (size_t i = 0; i < 1000; i++)
    {
      char c;
      size_t read = fread (&c, 1, 1, file);
      if (read < 1)
        {
          err = 1;
          fprintf (stderr, "zim.c : parse_directory_entry() : can't read url from file\n");
          goto cleanup;
        }

      entry->url[i] = c;
      if (c == 0) break;
    }
  entry->url[1000] = 0;

  for (size_t i = 0; i < 1000; i++)
    {
      char c;
      size_t read = fread (&c, 1, 1, file);
      if (read < 1)
        {
          err = 1;
          fprintf (stderr, "zim.c : parse_directory_entry() : can't read title from file\n");
          goto cleanup;
        }
      entry->title[i] = c;
      if (c == 0) break;
    }
  entry->title[1000] = 0;

  cleanup:
  return err;
}

/*
 * Parse the zimfile at `path` into `archive`.
 *
 * This will fill the header information so the rest of the content can be
 * reached.
 *
 * You must allocate memory for `archive`.
 *
 * Return non-zero in case of error.
 */
static int
zim_parse (const char *path, zim_archive_t *archive)
{
  int err = 0;
  void *buf = NULL;
  unsigned int magic_number = 0;
  FILE *file = NULL;


  file = fopen (path, "r");
  if (!file)
    {
      err = 1;
      fprintf (stderr, "zim.c : zim_parse() : can't open file : %s\n", path);
      goto cleanup;
    }

  buf = xalloc (4);
  size_t r = fread (buf, 1, 4, file);
  if (r != 4)
    {
      err = 1;
      fprintf (stderr, "zim.c : zim_parse() : error while reading file : %s\n", path);
      goto cleanup;
    }

  magic_number = *(unsigned int *) buf;
  if (magic_number != 72173914)
    {
      err = 1;
      fprintf (stderr, "zim.c : zim_parse() : the magic number for this file does not match the one expected. This means it's either not a zimfile, or it's an incompatible version of one.\n");
      goto cleanup;
    }

  archive->header->magic_number = magic_number;
  archive->path = strdup (path);

  err = parse_headers (archive->header, file);
  if (err)
    {
      fprintf (stderr, "zim.c : zim_parse() : error while reading headers.\n");
      goto cleanup;
    }

  parse_mime_type_list (archive, file);

  if (fseek (file, archive->header->url_ptr_pos, SEEK_SET) == -1)
    {
      err = 1;
      fprintf (stderr, "zim.c : zim_parse() :corrupted  zimfile : can't jump to url pointer position.\n");
      goto cleanup;
    }

  err = read_int (file, 8, &archive->header->dir_entries_pos);
  if (err)
    {
      fprintf (stderr, "zim.c : zim_parse() :corrupted  zimfile : can't read dir entries position.\n");
      goto cleanup;
    }

  cleanup:
  if (file) fclose (file);
  if (buf) free (buf);
  return err;
}

/*
 * LZMA setup.
 *
 * Return non-zero in case of error.
 */
static int
init_lzma_decoder (lzma_stream *strm)
{
  int err = 0;
	lzma_ret ret = lzma_stream_decoder (strm, UINT64_MAX, 0);

	if (ret != LZMA_OK)
    {
      err = 1;
      const char *msg;

      switch (ret)
      {
        case LZMA_MEM_ERROR:
          msg = "Memory allocation failed";
          break;

        case LZMA_OPTIONS_ERROR:
          msg = "Unsupported decompressor flags";
          break;

        default:
          msg = "Unknown error, possibly a bug";
          break;
      }

      fprintf(stderr, "Error initializing the lzma decoder: %s (error code %u)\n", msg, ret);
    }

	return err;
}

/*
 * Read data or `len` length from a XZ compressed cluster.
 *
 * `file`'s read pointer should be positioned at the start of
 * the data.
 *
 * Return non-zero in case of error.
 */
static int
read_from_xz_compressed_cluster (FILE *file, size_t start, size_t len, void *buf)
{
  int err = 0;
	lzma_stream strm = LZMA_STREAM_INIT;
	lzma_action action = LZMA_RUN;
	uint8_t inbuf[BUFSIZ];
	uint8_t outbuf[BUFSIZ];
  size_t read_count = 0;
  size_t buf_pos = 0;

  err = init_lzma_decoder (&strm);
  if (err)
    {
      fprintf (stderr, "zim.c : read_from_xz_compressed_cluster() : can't initialize lzma.\n");
      goto cleanup;
    }

	strm.next_in = NULL;
	strm.avail_in = 0;
	strm.next_out = outbuf;
	strm.avail_out = sizeof(outbuf);

	while (true)
    {
      if (strm.avail_in == 0 && !feof (file))
        {
          strm.next_in = inbuf;
          strm.avail_in = fread (inbuf, 1, sizeof (inbuf), file);

          if (ferror (file))
            {
              err = 1;
              fprintf (stderr, "zim.c : read_from_xz_compressed_cluster() : Read error: %s\n", strerror (errno));
              goto cleanup;
            }

          if (feof (file))
            action = LZMA_FINISH;
        }

      lzma_ret ret = lzma_code (&strm, action);

      if (strm.avail_out == 0 || ret == LZMA_STREAM_END) {
        size_t write_size = sizeof (outbuf) - strm.avail_out;
        uint8_t *outbuf_reader = outbuf;

        if (read_count < start)
          {
            if (read_count + write_size >= start)
              {
                size_t offset = start - read_count;
                outbuf_reader += offset;
                write_size -= offset;
              }
            else
              goto writing_done;
          }

        char *tmp_buf = buf;
        tmp_buf += buf_pos;

        size_t should_write = write_size;
        if (buf_pos + write_size >= len)
          should_write = len - buf_pos;
        memcpy ((void *) tmp_buf, outbuf_reader, should_write);
        buf_pos += should_write;
        if (buf_pos >= len) return true; // all is read

        writing_done:
        read_count += BUFSIZ;
        strm.next_out = outbuf;
        strm.avail_out = sizeof (outbuf);
      }

      if (ret != LZMA_OK)
        {
          if (ret == LZMA_STREAM_END)
            {
              fprintf(stderr, "End of archive reached before expected length\n");
              return false;
            }

          const char *msg;
          switch (ret)
            {
              case LZMA_MEM_ERROR:
                msg = "Memory allocation failed";
                break;

              case LZMA_FORMAT_ERROR:
                msg = "The input is not in the .xz format";
                break;

              case LZMA_OPTIONS_ERROR:
                msg = "Unsupported compression options";
                break;

              case LZMA_DATA_ERROR:
                msg = "Compressed file is corrupt";
                break;

              case LZMA_BUF_ERROR:
                msg = "Compressed file is truncated or "
                    "otherwise corrupt";
                break;

              default:
                msg = "Unknown error, possibly a bug";
                break;
            }

          fprintf (stderr, "zim.c : read_from_xz_compressed_cluster() : Decoder error: " "%s (error code %u)\n", msg, ret);
          return false;
        }
    }

  cleanup:
  return err;
}

/*
 * Read a whole article from a XZ compressed cluster.
 *
 * The position of the cluster is retrieved from the provided `entry`.
 *
 * Return NULL in case of error.
 */
static char *
read_article_from_xz_compressed_cluster (const zim_directory_entry_t *entry, FILE *file, size_t offset_size)
{
  char *content = NULL;
  long initial_pos = ftell (file);

  unsigned long int blob_index_pos = offset_size * entry->blob_number;
  unsigned long int blob_index = 0;
  int err = read_from_xz_compressed_cluster (file, blob_index_pos, offset_size, &blob_index);
  if (err)
    {
      fprintf (stderr, "zim.c : read_article_from_compressed_cluster() : can't find blob start position\n");
      return strdup ("Error reading article.");
    }
  fseek (file, initial_pos, SEEK_SET);

  unsigned long int blob_index_end = 0;
  err = read_from_xz_compressed_cluster (file, blob_index_pos + offset_size, offset_size, &blob_index_end);
  if (err)
    {
      fprintf (stderr, "zim.c : read_article_from_compressed_cluster() : can't find blob end position\n");
      return strdup ("Error reading article.");
    }
  fseek (file, initial_pos, SEEK_SET);

  size_t len = blob_index_end - blob_index;
  content = xalloc (MAX_ARTICLE_SIZE);
  read_from_xz_compressed_cluster (file, blob_index, len, content);

  return content;
}

/*
 * Read a whole article from a ZSTD compressed cluster.
 *
 * The position of the cluster is retrieved from the provided `entry`.
 *
 * Return NULL in case of error.
 */
static char *
read_article_from_zstd_compressed_cluster (const zim_directory_entry_t *entry, FILE *file, size_t offset_size, size_t cluster_len)
{
  char compressed[cluster_len];
  char *buf = xalloc (MAX_ARTICLE_SIZE);
  char *content = NULL;
  size_t read = fread (compressed, 1, cluster_len, file);
  if (read != cluster_len)
    {
      fprintf (stderr, "zim.c : read_article_from_zstd_compressed_cluster(): can't read cluster.\n.");
      goto cleanup;
    }

  int err = ZSTD_decompress (buf, MAX_ARTICLE_SIZE, compressed, cluster_len);
  if (ZSTD_isError (err))
    {
      fprintf (stderr, "zim.c : read_article_from_zstd_compressed_cluster(): can't decompress cluster: %s\n.", ZSTD_getErrorName(err));
      goto cleanup;
    }

  char *pos = buf;
  pos += offset_size * entry->blob_number;

  unsigned long int blob_index = 0;
  err = read_int_from_buf (pos, offset_size, &blob_index);
  if (err)
    {
      fprintf (stderr, "zim.c : read_article_from_zstd_compressed_cluster(): corrupted zimfile : can't read blob index\n.");
      goto cleanup;
    }

  pos += offset_size;
  unsigned long int blob_end_index = 0;

  err = read_int_from_buf (pos, offset_size, &blob_end_index);
  if (err)
    {
      fprintf (stderr, "zim.c : read_article_from_zstd_compressed_cluster(): corrupted zimfile : can't read blob end index\n.");
      goto cleanup;
    }

  pos = buf + blob_index;
  content = strndup (pos, blob_end_index - blob_index);

  cleanup:
  free (buf);
  return content;
}

/*
 * Read a whole article from an uncompressed cluster.
 *
 * The position of the cluster is retrieved from the provided `entry`.
 *
 * Return NULL in case of error.
 */
static char *
read_article_from_uncompressed_cluster (const zim_directory_entry_t *entry, FILE *file, size_t offset_size)
{
  char *content = NULL;

  fseek (file, offset_size * entry->blob_number, SEEK_CUR);
  unsigned long int blob_index = 0;
  int err = read_int (file, offset_size, &blob_index);
  if (err)
    {
      fprintf (stderr, "zim.c : read_article_from_uncompressed_cluster(): corrupted zimfile : can't read blob index\n.");
      goto cleanup;
    }

  unsigned long int blob_end_index = 0;
  err = read_int (file, offset_size, &blob_end_index);
  if (err)
    {
      fprintf (stderr, "zim.c : read_article_from_uncompressed_cluster(): corrupted zimfile : can't read blob end index\n.");
      goto cleanup;
    }

  if (fseek (file, blob_index - (offset_size * (entry->blob_number+2)), SEEK_CUR) == -1)
    {
      fprintf (stderr, "zim.c : read_article_from_uncompressed_cluster() : can't use zimfile anymore.\n");
      goto cleanup;
    }

  content = xalloc (MAX_ARTICLE_SIZE);
  if (!fgets (content, blob_end_index - blob_index, file))
    {
      fprintf (stderr, "zim.c : read_article_from_uncompressed_cluster() : can't read file.\n");
      goto cleanup;
    }

  cleanup:
  return content;
}

/*
 * Retrieve an article content given its directory entry.
 *
 * Return NULL in case of error.
 */
static char *
retrieve_directory_entry_content (const zim_archive_t *archive, const zim_directory_entry_t *entry)
{
  char *content = NULL;
  FILE *file = NULL;

  file = fopen (archive->path, "r");
  if (!file)
    {
      fprintf (stderr, "zim.c : retrieve_directory_entry_content() : can't open zimfile.\n");
      goto cleanup;
    }
  if (fseek (file, archive->header->cluster_ptr_pos + (entry->cluster_number * 8), SEEK_SET) == -1)
    {
      fprintf (stderr, "zim.c : retrieve_directory_entry_content() : can't use zimfile anymore.\n");
      goto cleanup;
    }

  unsigned long cluster_start = 0;
  int err = read_int (file, 8, &cluster_start);
  if (err)
    {
      fprintf (stderr, "zim.c : retrieve_directory_entry_content() : corrupted zimfile : can't read cluster start position.\n");
      goto cleanup;
    }

  unsigned long cluster_end = 0;
  if (entry->cluster_number < archive->header->cluster_count - 1)
    {
      err = read_int (file, 8, &cluster_end);
      if (err)
        {
          fprintf (stderr, "zim.c : retrieve_directory_entry_content() : corrupted zimfile : can't read cluster end position.\n");
          goto cleanup;
        }
    }

  if (fseek (file, cluster_start, SEEK_SET) == -1)
    {
      fprintf (stderr, "zim.c : retrieve_directory_entry_content() : can't use zimfile anymore.\n");
      goto cleanup;
    }

  int cluster_information;
  int r = fread (&cluster_information, 1, 1, file);
  if (r != 1)
    {
      fprintf (stderr, "zim.c : retrieve_directory_entry_content() : can't read cluster information.\n");
      goto cleanup;
    }
  int compressed = cluster_information & 0x0F;
  int extended = cluster_information & 0x10;
  size_t offset_size = extended ? 8 : 4;
  size_t cluster_len = cluster_end - cluster_start - 1; // cluster start with an uncompressed byte for cluster info

  if (compressed == COMPRESSION_XZ)
    content = read_article_from_xz_compressed_cluster (entry, file, offset_size);
  else if (compressed == COMPRESSION_ZSTD)
    content = read_article_from_zstd_compressed_cluster (entry, file, offset_size, cluster_len);
  else
    content = read_article_from_uncompressed_cluster (entry, file, offset_size);

  cleanup:
  if (file) fclose (file);
  return content;
}

/*
 * Read document content in a given position is the list of articles.
 *
 * This is mostly useful when iterating on all articles, otherwise it's
 * easier to find an article by its url.
 *
 * Return NULL in case of error.
 */
static char *
read_article_at_index (zim_archive_t *archive, size_t i)
{
  FILE *file = NULL;
  unsigned long int dir_entry = 0;
  zim_directory_entry_t *entry = NULL;
  char *content = NULL;

  file = fopen (archive->path, "r");
  if (!file)
    {
      fprintf (stderr, "zim.c : read_article_at_index() : can't open archive file.\n");
      goto cleanup;
    }

  if (fseek (file, archive->header->url_ptr_pos + (i * 8), SEEK_SET) == -1)
    {
      fprintf (stderr, "zim.c : read_article_at_index() : zimefile corrupted : can't reach header position.\n");
      goto cleanup;
    }
  if (!read_int (file, 8, &dir_entry))
    {
      fprintf (stderr, "zim.c : read_article_at_index() : zimefile corrupted : can't read entry index.\n");
      goto cleanup;
    }

  if (fseek (file, dir_entry, SEEK_SET) == -1)
    {
      fprintf (stderr, "zim.c : read_article_at_index() : zimefile corrupted : can't reach article position.\n");
      goto cleanup;
    }

  entry = xalloc (sizeof (*entry));
  int err = parse_directory_entry (file, entry);
  if (err)
    {
      fprintf (stderr, "zim.c : read_article_at_index() : corrupted zimfile : can't parse entry.\n");
      goto cleanup;
    }

  if (entry->mime_type == MIME_TYPE_REDIRECT)
    {
      content = read_article_at_index (archive, entry->redirect_index);
      goto cleanup;
    }

  if (entry->mime_type == MIME_TYPE_REDLINK || entry->mime_type == MIME_TYPE_DELETED)
    {
      fprintf (stderr, "zim.c : read_article_at_index() : non-existing or deleted page.\n");
      goto cleanup;
    }

  content = retrieve_directory_entry_content (archive, entry);

  cleanup:
  if (entry) free_zim_directory_entry (entry);
  if (file) fclose (file);
  return content;
}

/*
 * Read document content in a given url.
 *
 * There is no http request performed, the url is the name given
 * to the record in the zimfile, coresponding to the path in the
 * url of the article where is was fetched from the web.
 *
 * Return NULL in case of error.
 */
static char *
read_article_at_url (zim_archive_t *archive, const char *url)
{
  char *content = NULL;
  FILE *file = NULL;
  unsigned long int dir_entry = 0;
  zim_directory_entry_t *entry = NULL;

  file = fopen (archive->path, "r");
  if (!file)
    {
      fprintf (stderr, "zim.c : read_article_at_url() : can't open archive file.\n");
      goto cleanup;
    }

  unsigned long int floor = 0;
  unsigned long int ceil = archive->header->article_count;

  while (true)
    {
      unsigned long int cut = floor + (ceil - floor) / 2;
      if (floor == cut) break;
      if (fseek (file, archive->header->url_ptr_pos + cut * 8, SEEK_SET) == -1)
        {
          fprintf (stderr, "zim.c : read_article_at_url() : can't use file anymore.\n");
          goto cleanup;
        }

      int err = read_int (file, 8, &dir_entry);
      if (err)
        {
          fprintf (stderr, "zim.c : read_article_at_url() : can't read anymore.\n");
          goto cleanup;
        }

      if (fseek (file, dir_entry, SEEK_SET) == -1)
        {
          fprintf (stderr, "zim.c : read_article_at_url() : can't use file anymore.\n");
          goto cleanup;
        }

      entry = xalloc (sizeof (*entry));
      err = parse_directory_entry (file, entry);
      if (err)
        {
          fprintf (stderr, "zim.c : read_article_at_url() : corrupted zimfile : can't parse entry.\n");
          goto cleanup;
        }

      int diff = strncmp (url, entry->url, strlen (url));

      if (diff == 0)
        break;
      else
        {
          if (diff < 0) ceil = cut;
          else floor = cut;

          free_zim_directory_entry (entry);
          entry = NULL;
        }
    }

  if (!entry)
    {
      fprintf (stderr, "zim.c : read_article_at_url() : can't find provided url : %s\n", url);
      goto cleanup;
    }

  if (entry->mime_type == MIME_TYPE_REDIRECT) // redirect
    {
      content = read_article_at_index (archive, entry->redirect_index);
      goto cleanup;
    }

  if (entry->mime_type == MIME_TYPE_REDLINK || entry->mime_type == MIME_TYPE_DELETED) // redlink or deleted page
    {
      fprintf (stderr, "zim.c : read_article_at_url() : non-existing or deleted page.\n");
      goto cleanup;
    }

  content = retrieve_directory_entry_content (archive, entry);


  cleanup:
  if (entry) free_zim_directory_entry (entry);
  if (file) fclose (file);
  return content;
}

/*
 * Utility to find if a given mime-type is accepted by the comma seperated
 * whitelist provided as option or by default.
 */
static bool
is_accepted_mimetype (const char *mime_type, const char *mime_type_whitelist)
{
  bool accepted = false;
  char *list = strdup (mime_type_whitelist);

  char *accepted_mime_type = strtok (list, ",");
  while (accepted_mime_type)
    {
      if (strncmp (mime_type, accepted_mime_type, strlen (accepted_mime_type)) == 0)
        {
          accepted = true;
          break;
        }
      accepted_mime_type = strtok (NULL, ",");
    }

  free (list);
  return accepted;
}

/*
 * Print all article from the zim archive in the following format:
 *
 *   <START_OF_ZIM_ARTICLE>
 *   url: /foo/bar.html
 *   title: Foo Bar
 *   mime-type: text/html
 *   content:
 *   <html>
 *   <body>
 *   <p>Foo.</p>
 *   <p>Bar.</p>
 *   </body>
 *   </html>
 *   <END_OF_ZIM_ARTICLE>
 * 
 * `content` is only displayed if `show_article_content` is true.
 *
 * Even then, content will only be shown if the mime-type of the article
 * starts with one of the whitelisted mime-type in the comma seperated list
 * `mime_type_whitelist`. This is a start of the string match and not an
 * exact match because zimfile often contains mime-types like this:
 *
 *   text/plain;charset=UTF-8
 *
 * We obviously want to accept those is we accept "text/plain" (especially
 * since I've never seen a "text/plain" document in a zimfile not being
 * encoded in UTF-8 anyway).
 *
 * Return non-zero in case of error.
 *
 */
int
dump_all_articles (const char *zimfile_path, bool show_article_content, const char *mime_type_whitelist)
{
  int err = 0;
  FILE *file = NULL;
  zim_archive_t *archive = NULL;

  archive = new_zim_archive ();
  err = zim_parse (zimfile_path, archive);
  if (err)
    {
      fprintf (stderr, "zim.c : dump_all_articles() : can't parse %s. Is it a zim file?\n", zimfile_path);
      goto cleanup;
    }

  file = fopen (archive->path, "r");
  if (!file)
    {
      err = 1;
      fprintf (stderr, "zim.c : dump_all_articles() : can't open file %s\n", archive->path);
      goto cleanup;
    }

  for (size_t i = 0; i < archive->header->article_count; i++)
    {
      unsigned long int dir_entry = 0;

      if (fseek (file, archive->header->url_ptr_pos + i * 8, SEEK_SET) == -1)
        {
          err = 1;
          fprintf (stderr, "zim.c : dump_all_articles() : can't seek file to url pointer\n");
          goto cleanup;
        }

      err = read_int (file, 8, &dir_entry);
      if (err)
        {
          fprintf (stderr, "zim.c : dump_all_articles() : can't read url pointer\n");
          goto cleanup;
        }

      if (fseek (file, dir_entry, SEEK_SET) == -1)
        {
          err = 1;
          fprintf (stderr, "zim.c : dump_all_articles() : can't seek file to dir entry\n");
          goto cleanup;
        }

      zim_directory_entry_t *entry = xalloc (sizeof (*entry));
      err = parse_directory_entry (file, entry);
      if (err)
        {
          fprintf (stderr, "zim.c : dump_all_articles() : bogus entry found. Ignoring.\n");
          free_zim_directory_entry (entry);
          continue;
        }

      puts ("<START_OF_ZIM_ARTICLE>");
      printf ("url: %s\n", entry->url);
      printf ("title: %s\n", entry->title);

      if (entry->mime_type < archive->mime_type_list->len)
        {
          const char *mime_type = archive->mime_type_list->items[entry->mime_type];
          printf ("mime-type: %s\n", mime_type);

          if (show_article_content)
            {
              if (is_accepted_mimetype (mime_type, mime_type_whitelist))
                {
                  puts ("content:");
                  char *content = read_article_at_url (archive, entry->url);
                  if (content)
                    {
                      puts (content);
                      free (content);
                    }
                  else
                    fprintf (stderr, "zim.c : dump_all_articles() : can't find content for this article.");
                }
              else
                puts ("content:\nNOT-WHITELISTED-MIME-TYPE");
            }
        }
      else
        {
          switch (entry->mime_type)
            {
              case MIME_TYPE_REDIRECT:
                puts ("mime-type: none (redirect)");
                break;

              case MIME_TYPE_REDLINK:
              case MIME_TYPE_DELETED:
                puts ("mime-type: none (deleted page)");
                break;

              default:
                puts ("mime-type: unknown");
            }
        }

      puts ("<END_OF_ZIM_ARTICLE>");
      free_zim_directory_entry (entry);
    }

  cleanup:
  if (file) fclose (file);
  if (archive) free_zim_archive (archive);
  return err;
}

/*
 * Dump the list of mime-type included in the zim archive.
 *
 * This is especially useful to decide on a whitelist to provide to
 * dump_all_articles().
 *
 * Return non-zero in case of error.
 */
int
dump_mime_types (const char *zimfile_path)
{
  int err = 0;
  zim_archive_t *archive = NULL;

  archive = new_zim_archive ();
  err = zim_parse (zimfile_path, archive);
  if (err)
    {
      fprintf (stderr, "zim.c : dump_all_articles() : can't parse %s. Is it a zim file?\n", zimfile_path);
      goto cleanup;
    }

  for (size_t i = 0; i < archive->mime_type_list->len; i++)
    printf ("%s\n", archive->mime_type_list->items[i]);

  cleanup:
  if (archive) free_zim_archive (archive);
  return err;
}

/*
 * Print the content of a given article at `url`.
 *
 * `url` is the name of the document, which can be retrieve from
 * dump_all_articles().
 *
 * Return non-zero in case of error.
 */
int
show_article (const char *zimfile_path, const char *url)
{
  int err = 0;
  zim_archive_t *archive = NULL;
  char *article = NULL;

  archive = new_zim_archive ();
  err = zim_parse (zimfile_path, archive);
  if (err)
    {
      fprintf (stderr, "zim.c : show_article() : can't parse %s. Is it a zim file?\n", zimfile_path);
      goto cleanup;
    }

  article = read_article_at_url (archive, url);
  if (!article)
    {
      fprintf (stderr, "zim.c : show_article() : can't read article.\n");
      goto cleanup;
    }

  puts (article);

  cleanup:
  if (article) free (article);
  return err;
}
