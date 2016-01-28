/*
 * MOC - music on console
 * Copyright (C) 2002 - 2005 Damian Pietras <daper@daper.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <limits.h>
#include <inttypes.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <mpg123.h>


#define DEBUG

#include "common.h"
#include "log.h"
#include "decoder.h"
#include "io.h"
#include "audio.h"


struct mpg123_data
{
	struct io_stream *stream;
	mpg123_handle *mf;
//	int last_section;
	int bitrate;
	int avg_bitrate;
	int duration;
	int sample_rate;
	int channels;
	int encoding; // ??
	struct decoder_error error;
	int ok; /* was this stream successfully opened? */
	int tags_change; /* the tags were changed from the last call of opus_current_tags */
	struct file_tags *tags;
};


static void get_tags (mpg123_handle *mf, struct file_tags *info)
{
	mpg123_id3v1 *v1;
	mpg123_id3v2 *v2;
	mpg123_id3 (mf, &v1, &v2);

	if (v2) {
		if (v2->title && v2->title->p) info->title = xstrdup(v2->title->p);
		if (v2->artist && v2->artist->p) info->artist = xstrdup(v2->artist->p);
		if (v2->album && v2->album->p) info->album  = xstrdup(v2->album->p);
// 		info->track = v2???
	} else if (v1) {
		if (v1->title) info->title  = xstrdup(v1->title);
		if (v1->artist) info->artist = xstrdup(v1->artist);
		if (v1->album) info->album = xstrdup(v1->album);
	}
}


static void mpg123_tags (const char *file_name, struct file_tags *info, const int tags_sel)
{
	mpg123_handle *mf;
	int res;
	int ch, enc;
	long rate;
	off_t samples;
	mpg123_init();
	mf = mpg123_new (NULL, &res);
	if (mf == NULL || mpg123_open (mf, file_name) != MPG123_OK || mpg123_getformat (mf,&rate,&ch,&enc) != MPG123_OK) {
			logit ("Can't open file %s:", file_name);
			mpg123_delete (mf);
			mpg123_exit();
			return;
	}

	if (tags_sel & TAGS_COMMENTS)
		get_tags (mf, info);

	if (tags_sel & TAGS_TIME) {
	    samples = mpg123_length(mf);
	    if (samples > 0)
			info->time=samples/rate;
	    debug("Duration tags: %d, samples %lld",info->time,(long long)samples);
	}

	mpg123_delete (mf);
	mpg123_exit();
}

static ssize_t read_cb (void *datasource, void *ptr, size_t bytes)
{
	ssize_t res;

	res = io_read (datasource, ptr, bytes);
	if (res < 0) {
		logit ("Read error");
		res = -1;
	}

	return res;
}

static off_t seek_cb (void *datasource, off_t offset, int whence)
{
	debug ("Seek request to %"PRId64" (%s)", (int64_t)offset,
		whence == SEEK_SET ? "SEEK_SET" : (whence == SEEK_CUR ? "SEEK_CUR" : "SEEK_END"));
	return io_seek (datasource, offset, whence)<0 ? -1 : 0;
}


static void mpg123_open_stream_internal (struct mpg123_data *data)
{
	int res;
	char *mpg123_err;
	int ch, enc;
	long rate;
	struct mpg123_frameinfo info;
	off_t file_size, samples;

	data->tags = tags_new ();

	mpg123_init();

	data->mf = mpg123_new (NULL, &res);
	mpg123_replace_reader_handle(data->mf,read_cb,seek_cb,NULL);

	if (data->mf == NULL)
	{
	  mpg123_err = (char*) mpg123_plain_strerror (res);
	}
	else
	{
	  if (mpg123_open_handle (data->mf,data->stream) == MPG123_OK && mpg123_getformat (data->mf,&rate,&ch,&enc) == MPG123_OK)
	  {
	    debug ("Encoding: %i, sample rate: %li, channels: %i",enc,rate,ch);
	    data->sample_rate = rate;
	    data->channels = ch;
	    data->encoding = enc;
		/* Ensure that this output format will not change
		(it might, when we allow it). */
	    mpg123_format_none(data->mf);
	    mpg123_format(data->mf, data->sample_rate, data->channels, data->encoding);

	    mpg123_info(data->mf,&info);
	    debug ("Bitrate %i",info.bitrate);
	    data->bitrate = info.bitrate;

	    mpg123_scan(data->mf);
	    samples = mpg123_length(data->mf);
	    if (samples == MPG123_ERR)
			data->duration = -1;
	    else
			data->duration =samples/rate;
	    debug("Duration: %d, samples %lld",data->duration,(long long)samples);
	    file_size = io_file_size (data->stream);
	    if (data->duration > 0 && file_size != -1)
			data->avg_bitrate = file_size / data->duration * 8;
	    get_tags (data->mf, data->tags);

	    data->ok = 1;
	    return;
	  }
	  else
	  {
	    mpg123_err = "Error opening handle or getting format";
	  }
	}

	decoder_error (&data->error, ERROR_FATAL, 0, "%s", mpg123_err);
	debug ("mpg123_new error: %s", mpg123_err);
	mpg123_delete (data->mf);
	mpg123_exit();
	data->mf = NULL;
	io_close (data->stream);

// 	}
}

static void *mpg123_openX (const char *file)
{
	struct mpg123_data *data;
	data = (struct mpg123_data *)xmalloc (sizeof(struct mpg123_data));
	data->ok = 0;

	decoder_error_init (&data->error);
	data->tags_change = 0;
	data->tags = NULL;

	data->stream = io_open (file, 1);
	if (!io_ok(data->stream)) {
		decoder_error (&data->error, ERROR_FATAL, 0, "Can't open mpg123 file: %s", io_strerror(data->stream));
		io_close (data->stream);
	}
	else
		mpg123_open_stream_internal (data);
	return data;
}

static int mpg123_can_decode (__attribute__ ((unused)) struct io_stream *stream)
{
/*	char buf[36];

	if (io_peek (stream, buf, 36) == 36 && !memcmp (buf, "OggS", 4)
			&& !memcmp (buf + 28, "OpusHead", 8))*/
		return 1;
//	return 0;
}

static void *mpg123_open_stream (struct io_stream *stream)
{
	struct mpg123_data *data;

	data = (struct mpg123_data *)xmalloc (sizeof(struct mpg123_data));
	data->ok = 0;

	decoder_error_init (&data->error);
	data->stream = stream;
	mpg123_open_stream_internal (data);
	return data;
}

static void mpg123_closeX (void *prv_data)
{
	struct mpg123_data *data = (struct mpg123_data *)prv_data;

	if (data->ok) {
		mpg123_delete (data->mf);
		mpg123_exit();
		io_close (data->stream);
	}

	decoder_error_clear (&data->error);
	if (data->tags)
		tags_free (data->tags);
	free (data);
}

static int mpg123_seekX (void *prv_data, int sec)
{
	struct mpg123_data *data = (struct mpg123_data *)prv_data;

	assert (sec >= 0);

	return mpg123_seek (data->mf, sec * data->sample_rate,SEEK_SET)<0 ? -1 : sec;
}

static int mpg123_decodeX (void *prv_data, char *buf, int buf_len, struct sound_params *sound_params)
{
	struct mpg123_data *data = (struct mpg123_data *)prv_data;
	int ret;
	size_t decoded_bytes;
	struct mpg123_frameinfo info;

	decoder_error_clear (&data->error);

	while (1) {
		ret = mpg123_read(data->mf, (unsigned char *) buf, buf_len, &decoded_bytes);
		if (ret != MPG123_OK) {
			decoder_error (&data->error, ERROR_STREAM, 0, "Error in the stream: %s",mpg123_plain_strerror (ret)); // ???
//				debug ("mpg123 decoder error: %s", mpg123_plain_strerror (ret));
// 			continue;
			return 0;
		}
		if (decoded_bytes == 0)
			return 0;

		sound_params->channels = data->channels;
		sound_params->rate = data->sample_rate;
		sound_params->fmt = SFMT_S16 | SFMT_NE; // ????

		/* Update the bitrate information */
		mpg123_info(data->mf,&info);
		if (info.bitrate > 0)
			data->bitrate = info.bitrate;

		break;
	}
	return (int)decoded_bytes;
}

static int mpg123_current_tags (void *prv_data, struct file_tags *tags)
{
	struct mpg123_data *data = (struct mpg123_data *)prv_data;

	tags_copy (tags, data->tags);

	if (data->tags_change) {
		data->tags_change = 0;
		return 1;
	}

	return 0;
}


static int mpg123_get_bitrate (void *prv_data)
{
	struct mpg123_data *data = (struct mpg123_data *)prv_data;

	return data->bitrate;
}

static int mpg123_get_avg_bitrate (void *prv_data)
{
	struct mpg123_data *data = (struct mpg123_data *)prv_data;

	return data->avg_bitrate;
}

static int mpg123_get_duration (void *prv_data)
{

	struct mpg123_data *data = (struct mpg123_data *)prv_data;

	return data->duration;
}

static struct io_stream *mpg123_get_stream (void *prv_data)
{
	struct mpg123_data *data = (struct mpg123_data *)prv_data;

	return data->stream;
}

static void mpg123_get_name (const char *file ATTR_UNUSED, char buf[4])
{
	strcpy (buf, "123");
}

static int mpg123_our_format_ext (const char *ext)
{
	return !strcasecmp (ext, "mp3");
}

static void mpg123_get_error (void *prv_data, struct decoder_error *error)
{
	struct mpg123_data *data = (struct mpg123_data *)prv_data;

	decoder_error_copy (error, &data->error);
}

static int mpg123_our_mime (const char *mime)
{
	return !strcasecmp (mime, "audio/mpeg") || !strncasecmp (mime, "audio/mpeg;", 11);
}

static struct decoder mpg123_decoderX = {
	DECODER_API_VERSION,
	NULL,
	NULL,
	mpg123_openX,
	mpg123_open_stream,
	mpg123_can_decode,
	mpg123_closeX,
	mpg123_decodeX,
	mpg123_seekX,
	mpg123_tags,
	mpg123_get_bitrate,
	mpg123_get_duration,
	mpg123_get_error,
	mpg123_our_format_ext,
	mpg123_our_mime,
	mpg123_get_name,
	mpg123_current_tags,
	mpg123_get_stream,
	mpg123_get_avg_bitrate
};

struct decoder *plugin_init ()
{
	return &mpg123_decoderX;
}
