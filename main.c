#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utils.h"
#include "zim.h"

static void
usage (const char *progname)
{
  printf (
    "%s [-h|--help] [-m] [-a [-t <whitelisted mime-types>]] <zimfile> [url]\n" 
    "\n" 
    "Parse a zimfile and print articles' urls and names on STDOUT.\n" 
    "\n"
    "If `-a` is provided, also print the content of those articles.\n" 
    "By default, only the mime-types starting with `text/plain` and\n"
    "`text/html` are shown. You can provide a comma separated list of\n"
    "whitelisted mime-types with the `-t` option. If the mime-type of the\n"
    "article is not in the list, it will only print `NOT-WHITELISTED-MIME-TYPE`.\n"
    "\n"
    "If `-m` is provided, print instead the list of mime-types in the archive,\n" 
    "ignoring other options.\n"
    "\n"
    "If `url` is provided, print instead the content of the article corresponding to the\n"
    "provided url. Those urls are the ones provided while listing all articles.\n"
    "In that case, options are ignored.\n",
  progname);
}

enum {
  MODE_ALL,
  MODE_SINGLE,
  MODE_MIME,
};

#define MAX_ARG_LENGTH 1000
int MODE = MODE_ALL;
bool SHOW_ARTICLES_CONTENT = false;
const char *FILENAME = NULL;
const char *URL = NULL;
const char *MIME_WHITELIST = "text/html,text/plain";

/*
 * Handle the various options documented in usage().
 */
static void
parse_params (int argc, char **argv)
{
  int opt = 0;

  while ((opt = getopt (argc, argv, "amht:")) != -1)
    {
      switch (opt)
        {
          case 'a':
            SHOW_ARTICLES_CONTENT = true;
            break;

          case 'm':
            MODE = MODE_MIME;
            break;

          case 'h':
            usage (argv[0]);
            exit (0);

          case 't':
            MIME_WHITELIST = optarg;
            break;

          default:
            fprintf (stderr, "Unrecognized option: -%c\n\n", opt);
            usage (argv[0]);
            exit (1);
        }
    }

  if (optind >= argc)
    {
      fprintf (stderr, "You must provide a zimfile.\n\n");
      usage (argv[0]);
      exit (1);
    }

  if (strncmp (argv[optind], "--help", 10) == 0)
    {
      usage (argv[0]);
      exit (0);
    }

  FILENAME = argv[optind];

  if (optind + 1 < argc)
    {
      MODE = MODE_SINGLE;
      URL = argv[optind + 1];
    }
}

int
main (int argc, char **argv)
{
  int err = 0;

  for (int i = 0; i < argc; i++)
    if (strlen (argv[i]) > MAX_ARG_LENGTH)
      argv[i][MAX_ARG_LENGTH-1] = 0;

  parse_params (argc, argv);

  switch (MODE)
    {
      case MODE_ALL:
        err = dump_all_articles (FILENAME, SHOW_ARTICLES_CONTENT, MIME_WHITELIST);
        break;

      case MODE_MIME:
        err = dump_mime_types (FILENAME);
        break;

      default:
        err = show_article (FILENAME, URL);
    }

  return err;
}
