#ifndef ZIM_H
#define ZIM_H

#include <stdbool.h>

int dump_all_articles (const char *zimfile_path, bool show_article_content, const char *mime_type_whitelist);
int dump_mime_types (const char *zimfile_path);
int show_article (const char *zimfile_path, const char *url);

#endif
