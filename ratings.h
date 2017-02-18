#ifndef RATINGS_H
#define RATINGS_H

#include "playlist.h"

#ifdef __cplusplus
extern "C" {
#endif

/* write ratings */
bool ratings_write_file (const char *fn, int rating);
bool ratings_write (const struct plist_item *item);

/* read ratings */
void ratings_read_file (const char *fn, struct file_tags *tags);
void ratings_read (struct plist_item *item);
void ratings_read_all (const struct plist *plist);

#ifdef __cplusplus
}
#endif

#endif
