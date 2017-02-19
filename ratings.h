#ifndef RATINGS_H
#define RATINGS_H

#include "playlist.h"

#ifdef __cplusplus
extern "C" {
#endif

/* store ratings for a file */
bool ratings_write_file (const char *fn, int rating);

/* read ratings for a file */
void ratings_read_file (const char *fn, struct file_tags *tags);

#ifdef __cplusplus
}
#endif

#endif
