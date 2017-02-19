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
#include <assert.h>

#include "playlist.h"
#include "options.h"
#include "interface.h" /* for user_wants_interrupt() */
#include "server.h" /* for server_error */

/* Ratings files should contain lines in this format:
 * [0-5] <filename>\n
 * Everything else is ignored.
 *
 * There must only be a single space after the rating, so that
 * files starting with spaces can be tagged without some
 * quoting scheme (we want parsing the file to be as fast as
 * possible).
 *
 * Newlines in file names are not handled in all cases (things
 * like "<something>\n3 <some other filename>", but whatever). */

/* We read files in chunks of BUF_SIZE bytes */
#define BUF_SIZE (8*1024)


/* find rating for a file and returns that rating or
 * -1 if not found. If found, filepos is the position
 * of the rating character in rf.
 * rf is assumed to be freshly opened (i.e. ftell()==0). */
static int find_rating (const char *fn, FILE *rf, long *filepos)
{
	assert(fn && rf && ftell(rf) == 0);

	char buf[BUF_SIZE]; /* storage for one chunk */
	char   *s = NULL;   /* current position in chunk */
	int     n = 0;      /* characters left in chunk */
	long fpos = 0;      /* ftell() of end of chunk */
	const int fnlen = strlen(fn);

	/* get next char, refill buffer if needed */
	#define GETC(c) do{ \
	if (!n) { \
		n = fread(buf, 1, BUF_SIZE, rf); \
		s = buf; \
		fpos += n; \
		if (!n) (c) = -1; else { (c) = *(const unsigned char*)s++; --n; } \
	} \
	else { (c) = *(const unsigned char*)s++; --n; } \
	} while (0)
	
	/* loop over all lines in the file */
	while (true)
	{
		int c0; GETC(c0);

		if (c0 < 0) return -1; /* EOF */

		if (c0 == '\n') continue; /* empty line */

		if (c0 >= '0' && c0 <= '5') /* possible rating line */
		{
			char c; GETC(c);

			if (c < 0) return -1; /* EOF again */
			
			/* go straight to next line if we already read the newline */
			if (c == '\n') continue; 
			
			if (c == ' ') /* still good */
			{
				/* find fn */
				const char *t = fn; /* remaining string to match */
				int nleft = fnlen; /* invariant: nleft == strlen(t) */
				while (true)
				{
					/* compare as much as possible in this chunk */
					int ncmp = (nleft < n ? nleft : n);
					if (memcmp(t, s, ncmp))
					{
						/* not our file. skip rest of line */
						break;
					}

					/* Note: next line is where things get weird
					 * if fn contains newlines */
					s += ncmp;
					t += ncmp;
					n -= ncmp;
					nleft -= ncmp;

					if (!nleft)
					{
						/* remember position of rating */
						if (filepos)
							*filepos = fpos-n - fnlen - 2;
						
						/* check for trailing garbage */
						GETC(c);
						if (c >= 0 && c != '\n')
							break; /* skip rest of line */

						/* success */
						return c0 - '0';
					}

					if (!n) /* read next chunk */
					{
						n = fread(buf, 1, BUF_SIZE, rf);
						if (!n) return -1;
						s = buf;
					}
				}
			}
		}

		/* skip to next line */
		while (true)
		{
			char *e = memchr (s, '\n', n);
			if (e)
			{
				/* found a newline, update position in buffer */
				n -= e-s + 1;
				s = e+1;
				break;
			}
			/* look for newline in next chunk */
			n = fread(buf, 1, BUF_SIZE, rf);
			if (!n) return -1;
			s = buf;
		}
	}
	#undef GETC
}

/* open ratings file in the same folder as fn */
static FILE *open_ratings_file (const char *fn, const char *mode)
{
	assert(fn && mode && *mode);

	char buf[512]; /* buffer for file path */
	size_t  N = sizeof(buf);
	const char *rfn = options_get_str ("RatingFile");

	char *sep = strrchr (fn, '/');
	if (!sep)
	{
		/* current directory */
		return fopen (rfn, mode);
	}
	else if ((sep-fn) + 1 + strlen (rfn) + 1 <= N)
	{
		/* buf can hold the file name */
		memcpy (buf, fn, (sep-fn) + 1);
		strcpy (buf + (sep-fn) + 1, rfn);
		return fopen (buf, mode);
	}
	else
	{
		/* path is too long, allocate buffer on heap */
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

/* read rating for a file into file_tags */
void ratings_read_file (const char *fn, struct file_tags *tags)
{
	assert(fn && tags);

	int rating = 0;

	FILE *rf = open_ratings_file (fn, "rb");
	if (rf)
	{
		/* get filename */
		const char *sep = strrchr (fn, '/');
		if (sep) fn = sep + 1;

		/* read rating from ratings file */
		rating = find_rating (fn, rf, NULL);

		/* if fn has no rating, treat as 0-rating */
		if (rating < 0) rating = 0;

		fclose (rf);
	}

	/* store the rating */
	tags->rating = rating;
	tags->filled |= TAGS_RATING;
}

/* update ratings file for given file path and rating */
bool ratings_write_file (const char *fn, int rating)
{
	assert(fn && rating >= 0 && rating <= 5);

	const char *failmsg = "Rating could not be written (check permissions).";
	#define FAIL do { \
		server_error (__FILE__, __LINE__, "ratings_write_file", failmsg); \
		return 0; } while (0)


	/* keep full path for open_ratings_file */
	const char *path = fn;

	/* get filename */
	const char *sep = strrchr (fn, '/');
	if (sep) fn = sep + 1;

	FILE *rf = open_ratings_file (path, "rb+");
	if (!rf)
	{
		if (rating <= 0) return 1; /* 0 rating needs no writing */

		/* ratings file did not exist or could not be opened
		 * for reading. Try creating it */
		FILE *rf = open_ratings_file (path, "ab");
		if (!rf) FAIL; /* can't create it either */

		/* append new rating */
		int ok = fprintf (rf, "%d %s\n", rating, fn);
		fclose (rf);
		if (!ok) FAIL;
		return 1;
	}

	/* ratings file exists, locate our file */
	long filepos;
	int  ok = 1;
	int r0 = find_rating (fn, rf, &filepos);
	if (r0 < 0)
	{
		/* not found - append */
		if (rating > 0 && 0 == fseek (rf, 0, SEEK_END))
		{
			ok = fprintf (rf, "%d %s\n", rating, fn);
		}
	}
	else if (r0 != rating)
	{
		/* update existing entry */
		assert (rating >= 0 && rating <= 5);
		if (0 == fseek (rf, filepos, SEEK_SET))
		{
			ok = (fputc ('0' + rating, rf) > 0);
		}
	}
	fclose (rf);

	if (!ok) FAIL;

	#undef FAIL

	return 1;
}

