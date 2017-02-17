/*
 * MOC - music on console
 * Copyright (C) 2004,2005 Damian Pietras <daper@daper.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#define DEBUG

#include "common.h"
#include "playlist.h"
#include "log.h"
#include "options.h"
#include "files.h"
#include "interface.h"

/*
 * The ratings file should contain lines in this format:
 * [1-5] <filename>\n
 * Lines starting with '#' or garbage are ignored
 *
 * There must only be a single space after the rating, so that
 * files starting with spaces can be tagged without some
 * quoting scheme (we want parsing the file to be as fast as
 * possible).
 *
 * Newlines in file names are not handled in all cases (things
 * like "<something>\n3 <some other filename>", but whatever).
 */


#define BUF_SIZE (8*1024)


// find rating for a file and returns that rating or
// -1 if not found. If found, filepos is the position
// of the rating character in rf
// rf is assumed to be freshly opened (i.e. ftell()==0)
static int find_rating (const char *fn, FILE *rf, long *filepos)
{
	assert(fn && rf && ftell(rf) == 0 && filepos);

	char buf[BUF_SIZE]; // storage for one chunk
	char *s = NULL; // current position in chunk
	int  n = 0;    // amount of characters left in chunk
	long fpos = 0; // position of end of chunk
	const int fnlen = strlen(fn);

	#define GETC(c) do{ \
	if (!n) { \
		n = fread(buf, 1, BUF_SIZE, rf); \
		s = buf; \
		fpos += n; \
		if (!n) (c) = -1; else { (c) = *(const unsigned char*)s++; --n; } \
	} \
	else { (c) = *(const unsigned char*)s++; --n; } \
	} while (0)
		
	while (true)
	{
		int c0; GETC(c0);
		if (c0 < 0) return -1;

		if (c0 == '\n') continue; // empty line
		if (c0 >= '0' && c0 <= '5')
		{
			char c; GETC(c);
			if (c < 0) return -1;
			if (c == '\n') continue;
			if (c == ' ')
			{
				// so far so good, now look for fn
				const char *t = fn;
				int nleft = fnlen;
				while (true)
				{
					int ncmp = (nleft < n ? nleft : n);
					if (memcmp(t, s, ncmp))
					{
						// not our file. skip rest of line
						break;
					}

					// next line is where things get weird
					// if fn contains newlines
					s += ncmp;
					t += ncmp;
					n -= ncmp;
					nleft -= ncmp;

					if (!nleft)
					{
						// might be our file. remember
						// position
						*filepos = fpos-n - fnlen - 2;
						
						// check for trailing garbage
						GETC(c);
						if (c >= 0 && c != '\n') break;

						return c0 - '0';
					}

					if (!n)
					{
						n = fread(buf, 1, BUF_SIZE, rf);
						if (!n) return -1;
						s = buf;
					}
				}
			}
		}

		// skip to next line
		while (true)
		{
			char *e = memchr (s, '\n', n);
			if (e)
			{
				n -= e-s + 1;
				s = e+1;
				break;
			}
			n = fread(buf, 1, BUF_SIZE, rf);
			if (!n) return -1;
			s = buf;
		}
	}
	#undef GETC
}

static FILE *open_ratings_file (const char *fn, const char *mode)
{
	assert(fn && mode && *mode);

	char buf[512];
	size_t  N = sizeof(buf);
	const char *rfn = options_get_str ("RatingFile");

	char *sep = strrchr (fn, '/');
	if (!sep)
	{
		return fopen (rfn, mode);
	}
	else if ((sep-fn) + 1 + strlen (rfn) + 1 <= N)
	{
		// we can use stack buffer to hold the file name
		memcpy (buf, fn, (sep-fn) + 1);
		strcpy (buf + (sep-fn) + 1, rfn);
		return fopen (buf, mode);
	}
	else
	{
		int N = (sep-fn) + 1 + strlen (rfn) + 1;
		char *gbuf = xmalloc (N);
		if (!gbuf) return NULL;

		memcpy (gbuf, fn, (sep-fn) + 1);
		strcpy (gbuf + (sep-fn) + 1, rfn);
		FILE *rf = fopen (gbuf, "rb");
		free (gbuf);
		return rf;
	}
}

void ratings_read (struct plist_item *item)
{
	assert(item && item->file);
	if (item->type != F_SOUND) return;

	int rating = 0;

	FILE *rf = open_ratings_file (item->file, "rb");
	if (rf)
	{
		// get filename
		const char *fn = item->file;
		const char *sep = strrchr (fn, '/');
		if (sep) fn = sep + 1;

		// get rating
		long filepos; // reading does not need this
		rating = find_rating (fn, rf, &filepos);
		if (rating < 0) rating = 0;

		fclose (rf);
	}

	// store the rating (or the default 0)
	if (!item->tags) item->tags = tags_new ();
	if (!item->tags) return;
	item->tags->rating = rating;
	item->tags->filled |= TAGS_RATING;
}

void ratings_read_file (const char *fn, struct file_tags *tags)
{
	assert(fn && tags);

	int rating = 0;

	FILE *rf = open_ratings_file (fn, "rb");
	if (rf)
	{
		// get filename
		const char *sep = strrchr (fn, '/');
		if (sep) fn = sep + 1;

		// get rating
		long filepos; // reading does not need this
		rating = find_rating (fn, rf, &filepos);
		if (rating < 0) rating = 0;

		fclose (rf);
	}

	// store the rating (or the default 0)
	tags->rating = rating;
	tags->filled |= TAGS_RATING;
}

void ratings_read_all (const struct plist *plist)
{
	assert (plist);

	for (int i = 0; i < plist->num && !user_wants_interrupt (); ++i)
	{
		if (plist_deleted (plist, i)) continue;
		struct plist_item *item = plist->items + i;
		if (!item || (item->tags && item->tags->filled & TAGS_RATING))
			continue;

		// open ratings file for item and read the
		// entire thing, hopefully hitting lots of
		// other items as well
		
		// TODO: be stupid for now
		ratings_read (item);
	}
}

void ratings_write_file (const char *fn, int rating)
{
	assert(fn && rating >= 0 && rating <= 5);

	// get filename
	const char *path = fn;
	const char *sep = strrchr (fn, '/');
	if (sep) fn = sep + 1;

	FILE *rf = open_ratings_file (path, "rb+");
	if (!rf)
	{
		if (rating <= 0) return; // 0 rating needs no writing

		// file did not exist or could not be opened for reading
		FILE *rf = open_ratings_file (path, "ab");
		if (!rf) return; // can't create it either

		// append new rating
		fprintf (rf, "%d %s\n", rating, fn);
		fclose (rf);
		return;
	}

	long filepos;
	int r0 = find_rating (fn, rf, &filepos);
	if (r0 < 0)
	{
		// not found - append
		if (rating > 0 && 0 == fseek (rf, 0, SEEK_END))
		{
			fprintf (rf, "%d %s\n", rating, fn);
		}
	}
	else if (r0 != rating)
	{
		assert (rating >= 0 && rating <= 5);
		if (0 == fseek (rf, filepos, SEEK_SET))
		{
			fputc ('0' + rating, rf);
		}
	}
	fclose (rf);
}

void ratings_write (const struct plist_item *item)
{
	assert(item && item->file);
	if (item->type != F_SOUND) return;
	if (item->type != F_SOUND || !item->tags) return;
	if (!(item->tags->filled & TAGS_RATING)) return;

	const int rating = item->tags->rating;
	const char *fn = item->file;

	ratings_write_file (fn, rating);
}

