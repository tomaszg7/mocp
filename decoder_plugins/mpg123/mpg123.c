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


static void get_comment_tags (mpg123_handle *of, struct file_tags *info)
{
/*	int i;
	const OpusTags *comments;

	comments = op_tags (of, -1);
	for (i = 0; i < comments->comments; i++) {
		if (!strncasecmp(comments->user_comments[i], "title=", strlen ("title=")))
			info->title = xstrdup(comments->user_comments[i] + strlen ("title="));
		else if (!strncasecmp(comments->user_comments[i], "artist=", strlen ("artist=")))
			info->artist = xstrdup (comments->user_comments[i] + strlen ("artist="));
		else if (!strncasecmp(comments->user_comments[i], "album=", strlen ("album=")))
			info->album = xstrdup (comments->user_comments[i] + strlen ("album="));
		else if (!strncasecmp(comments->user_comments[i], "tracknumber=", strlen ("tracknumber=")))
			info->track = atoi (comments->user_comments[i] + strlen ("tracknumber="));
		else if (!strncasecmp(comments->user_comments[i], "track=", strlen ("track=")))
			info->track = atoi (comments->user_comments[i] + strlen ("track="));
	}*/
}

/* Return a description of an op_*() error. */
static const char *mpg123_str_error (const int code)
{
/*	const char *result;

	switch (code) {
		case OP_FALSE:
			result = "Request was not successful";
			break;
		case OP_EOF:
			result = "End of File";
			break;
		case OP_HOLE:
			result = "Hole in stream";
			break;
		case OP_EREAD:
			result = "An underlying read, seek, or tell operation failed.";
			break;
		case OP_EFAULT:
			result = "Internal (Opus) logic fault";
			break;
		case OP_EIMPL:
			result = "Unimplemented feature";
			break;
		case OP_EINVAL:
			result = "Invalid argument";
			break;
		case OP_ENOTFORMAT:
			result = "Not an Opus file";
			break;
		case OP_EBADHEADER:
			result = "Invalid or corrupt header";
			break;
		case OP_EVERSION:
			result = "Opus header version mismatch";
			break;
		case OP_EBADPACKET:
			result = "An audio packet failed to decode properly";
			break;
		case OP_ENOSEEK:
			result = "Requested seeking in unseekable stream";
			break;
		case OP_EBADTIMESTAMP:
			result = "File timestamps fail sanity tests";
			break;
		default:
			result = "Unknown error";
       }
*/
       return "Unknown error";
}


/* Fill info structure with data from ogg comments */
static void mpg123_tags (const char *file_name, struct file_tags *info, const int tags_sel)
{
/*	OggOpusFile *of;
	int err_code;

	// op_test() is faster than op_open(), but we can't read file time with it.
	if (tags_sel & TAGS_TIME) {
		of = op_open_file(file_name,&err_code);
		if (err_code < 0) {
			logit ("Can't open %s: %s", file_name, opus_str_error (err_code));
			op_free(of);
			return;
		}
	}
	else {
		of = op_open_file(file_name,&err_code);
		if (err_code < 0) {
			logit ("Can't open %s: %s", file_name, opus_str_error (err_code));
			op_free (of);
			return;
		}
	}

	if (tags_sel & TAGS_COMMENTS)
		get_comment_tags (of, info);

	if (tags_sel & TAGS_TIME) {
		ogg_int64_t opus_time;

		opus_time = op_pcm_total (of, -1);
		if (opus_time >= 0)
			info->time = opus_time / 48000;
			debug("Duration tags: %d, samples %lld",info->time,(long long)opus_time);
	}

	op_free (of);*/
}

/*static int read_cb (void *datasource, unsigned char *ptr, int bytes)
{
	ssize_t res;

	res = io_read (datasource, ptr, bytes);
	if (res < 0) {
		logit ("Read error");
		res = -1;
	}

	return res;
}

static int seek_cb (void *datasource, opus_int64 offset, int whence)
{
	debug ("Seek request to %"PRId64" (%s)", (int64_t)offset,
		whence == SEEK_SET ? "SEEK_SET" : (whence == SEEK_CUR ? "SEEK_CUR" : "SEEK_END"));
	return io_seek (datasource, offset, whence)<0 ? -1 : 0;
}

static int close_cb (void *datasource ATTR_UNUSED)
{
	return 0;
}

static opus_int64 tell_cb (void *datasource)
{
	return (opus_int64)io_tell (datasource);
}
*/

static void mpg123_open_stream_internal (struct mpg123_data *data)
{
	int res;

/*	OpusFileCallbacks callbacks = {
		read_cb,
		seek_cb,
		tell_cb,
		close_cb
	};*/

	data->tags = tags_new ();

	mpg123_init();
	data->mf = mpg123_new (NULL, &res);
	mpg123_open_fd (data->mf,data->stream->fd);

	int ch, enc;
	long rate;

	res = mpg123_getformat (data->mf,&rate,&ch,&enc);
	debug ("Encoding: %i, sample rate: %li, channels: %i",enc,rate,ch);
	data->sample_rate = rate;
	data->channels = ch;
	data->encoding = enc;

	if (data->mf == NULL || res != MPG123_OK) {
		const char *mpg123_err = mpg123_plain_strerror (res);

		decoder_error (&data->error, ERROR_FATAL, 0, "%s", mpg123_err);
		debug ("mpg123_new error: %s", mpg123_err);
		mpg123_delete (data->mf);
		mpg123_exit();
		data->mf = NULL;
		io_close (data->stream);
	}
       else {
// ////////////  != MPG123_OK ?
//		ogg_int64_t samples;
//		data->last_section = -1;
//		data->avg_bitrate = op_bitrate (data->of, -1) / 1000;
//		data->bitrate = data->avg_bitrate;
//		samples = op_pcm_total (data->of, -1);
//		if (samples == OP_EINVAL)
//			data->duration = -1;
//		else
//			data->duration =samples/48000;
//		debug("Duration: %d, samples %lld",data->duration,(long long)samples);
		/* Ensure that this output format will not change
		(it might, when we allow it). */
		mpg123_format_none(data->mf);
		mpg123_format(data->mf, data->sample_rate, data->channels, data->encoding);
		data->ok = 1;
//		get_comment_tags (data->of, data->tags);
	}
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

static int mpg123_can_decode (struct io_stream *stream)
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

/*static int opus_seek (void *prv_data, int sec)
{
	struct opus_data *data = (struct opus_data *)prv_data;

	assert (sec >= 0);

	return op_pcm_seek (data->of, sec * (ogg_int64_t)48000)<0 ? -1 : sec;
} */

static int mpg123_decodeX (void *prv_data, char *buf, int buf_len, struct sound_params *sound_params)
{
	struct mpg123_data *data = (struct mpg123_data *)prv_data;
	int ret;
//	int current_section;
	int bitrate;
	size_t decoded_bytes;

	decoder_error_clear (&data->error);

	while (1) {
		ret = mpg123_read(data->mf, buf, buf_len, &decoded_bytes);
		if (ret != MPG123_OK) {
			decoder_error (&data->error, ERROR_STREAM, 0, "Error in the stream!"); // ???
			continue;
		}
		if (decoded_bytes == 0)
			return 0;

		sound_params->channels = data->channels;
		sound_params->rate = data->sample_rate;
		sound_params->fmt = SFMT_S16 | SFMT_NE; // ????
//		ret *= sound_params->channels * sizeof(opus_int16);
		/* Update the bitrate information */
//		bitrate = op_bitrate_instant (data->of);
//		if (bitrate > 0)
//			data->bitrate = bitrate / 1000;

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
	NULL,//mpg123_seek,
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
