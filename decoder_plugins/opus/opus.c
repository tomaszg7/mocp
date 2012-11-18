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

#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <opus/opusfile.h>

#define DEBUG

#include "common.h"
#include "log.h"
#include "decoder.h"
#include "io.h"
#include "audio.h"


struct opus_data
{
	struct io_stream *stream;
	OggOpusFile *vf;
	int last_section;
	opus_int32 bitrate;
	opus_int32 avg_bitrate;
	int duration;
	struct decoder_error error;
	int ok; /* was this stream successfully opened? */
	int tags_change; /* the tags were changed from the last call of
			    ogg_current_tags */
	struct file_tags *tags;
};


static void get_comment_tags (OggOpusFile *vf, struct file_tags *info)
{
	int i;
	const OpusTags *comments = op_tags (vf, -1);
	
	for (i = 0; i < comments->comments; i++) {
		if (!strncasecmp(comments->user_comments[i], "title=",
				 strlen ("title=")))
			info->title = xstrdup(comments->user_comments[i]
					+ strlen ("title="));
		else if (!strncasecmp(comments->user_comments[i],
					"artist=", strlen ("artist=")))
			info->artist = xstrdup (
					comments->user_comments[i]
					+ strlen ("artist="));
		else if (!strncasecmp(comments->user_comments[i],
					"album=", strlen ("album=")))
			info->album = xstrdup (
					comments->user_comments[i]
					+ strlen ("album="));
		else if (!strncasecmp(comments->user_comments[i],
					"tracknumber=",
					strlen ("tracknumber=")))
			info->track = atoi (comments->user_comments[i]
					+ strlen ("tracknumber="));
		else if (!strncasecmp(comments->user_comments[i],
					"track=", strlen ("track=")))
			info->track = atoi (comments->user_comments[i]
					+ strlen ("track="));
	}
}

/* Return a malloc()ed description of an op_*() error. */
static char *opus_str_error (const int code)
{
	char *err;

	switch (code) {
		case OP_EREAD:
			err = "read, seek, or tell operation failed";
			break;
		case OP_ENOTFORMAT:
			err = "nor an Opus file";
			break;
		case OP_EVERSION:
			err = "Opus version mismatch";
			break;
		case OP_EBADHEADER:
			err = "invalid Opus bitstream header";
			break;
		case OP_EFAULT:
			err = "NULL pointer, or internal memory allocation failed, or an internal library error was encountered";
			break;
		case OP_EINVAL:
			err = "invalid parameters";
			break;
		case OP_EIMPL:
			err = "unimplemented feature";
			break;
		case OP_EBADPACKET:
			err = "bad packet";
			break;
		case OP_ENOSEEK:
			err = "unseekable streem";
			break;
		default:
			err = "unknown error";
	}

	return xstrdup (err);
}


/* Fill info structure with data from ogg comments */
static void opus_tags (const char *file_name, struct file_tags *info,
		const int tags_sel)
{
	OggOpusFile *vf;
	int err_code;

	// op_test() is faster than op_open(), but we can't read file time with it.
	if (tags_sel & TAGS_TIME) {
		vf=op_open_file(file_name, &err_code);
		char *opus_err = opus_str_error (err_code);
 		if (err_code  < 0) {
			logit ("Can't open %s: %s", file_name, opus_err);
			free(opus_err);
			return;
		}
	}
	else {
		vf=op_test_file(file_name,&err_code);
		char *opus_err = opus_str_error (err_code);
		if (err_code < 0) {
			logit ("Can't open %s: %s", file_name, opus_err);
			free(opus_err);
			return;
		}
	}

	if (tags_sel & TAGS_COMMENTS)
		get_comment_tags (vf, info);

	if (tags_sel & TAGS_TIME) {
		ogg_int64_t opus_time;

	    opus_time = op_pcm_total (vf, -1);
	    if (opus_time >= 0)
			info->time = opus_time / 48000;
			debug("Duration tags: %d, samples %lld",info->time,opus_time);
	}

	op_free (vf);
}

static int read_callback (void *datasource, unsigned char *ptr, int bytes)
{
	ssize_t res;

	res = io_read (datasource, ptr, bytes);

	/* libvorbisfile expects the read callback to return >= 0 with errno
	 * set to non zero on error. */
	if (res < 0) {
		logit ("Read error");
		if (errno == 0)
			errno = 0xffff;
		res = 0;
	}

	return res;
}

static int seek_callback (void *datasource, opus_int64 offset, int whence)
{
 	debug ("Seek request to %ld (%s)", (long)offset,
 			whence == SEEK_SET ? "SEEK_SET"
 			: (whence == SEEK_CUR ? "SEEK_CUR" : "SEEK_END"));
 	return io_seek (datasource, offset, whence)<0 ? -1 : 0;
}

static int close_callback (void *datasource ATTR_UNUSED)
{
	return 0;
}

static opus_int64 tell_callback (void *datasource)
{
	return io_tell (datasource);
}

static void opus_open_stream_internal (struct opus_data *data)
{
	int res=0;
	OpusFileCallbacks callbacks = {
		read_callback,
		seek_callback,
		tell_callback,
		close_callback
	};
	data->tags = tags_new ();
	data->vf = op_open_callbacks(data->stream, &callbacks, NULL, 0, &res);
	if (res < 0) {
		char *opus_err = opus_str_error (res);

		decoder_error (&data->error, ERROR_FATAL, 0, "%d",
				res);
		debug ("op_open error: %s", opus_err);
		free (opus_err);

		io_close (data->stream);
	}
	else {
		ogg_int64_t samples;
		data->last_section = -1;
		data->avg_bitrate = op_bitrate (data->vf, -1)/1000;
		data->bitrate = data->avg_bitrate;
		samples = op_pcm_total (data->vf, -1);
		if (samples == OP_EINVAL)
			data->duration = -1;
		else
			data->duration =samples/48000;
		debug("Duration: %d, samples %lld",data->duration,(long long)samples);
		data->ok = 1;
		get_comment_tags (data->vf, data->tags);
	}
}

static void *opus_open (const char *file)
{
	struct opus_data *data;
	data = (struct opus_data *)xmalloc (sizeof(struct opus_data));
	data->ok = 0;

	decoder_error_init (&data->error);
	data->tags_change = 0;
	data->tags = NULL;

	data->stream = io_open (file, 1);
	if (!io_ok(data->stream)) {
		decoder_error (&data->error, ERROR_FATAL, 0,
				"Can't load OGG: %s",
				io_strerror(data->stream));
		io_close (data->stream);
	}
	else
		opus_open_stream_internal (data);

	return data;
}

static void *opus_open_stream (struct io_stream *stream)
{
	struct opus_data *data;

	data = (struct opus_data *)xmalloc (sizeof(struct opus_data));
	data->ok = 0;

	decoder_error_init (&data->error);
	data->stream = stream;
	opus_open_stream_internal (data);

	return data;
}

static void opus_close (void *prv_data)
{
	struct opus_data *data = (struct opus_data *)prv_data;

	if (data->ok) {
		op_free (data->vf);
		io_close (data->stream);
	}

	decoder_error_clear (&data->error);
	if (data->tags)
		tags_free (data->tags);
	free (data);
}

static int opus_seek (void *prv_data, int sec)
{
   debug ("start");
 	struct opus_data *data = (struct opus_data *)prv_data;

 	assert (sec >= 0);
	return op_pcm_seek (data->vf, sec * 48000) ? -1 : sec;
}

static int opus_decodeX (void *prv_data, char *buf, int buf_len,
		struct sound_params *sound_params)
{
	struct opus_data *data = (struct opus_data *)prv_data;
	int ret;
	int current_section;
	int bitrate;

	decoder_error_clear (&data->error);

	while (1) {
		#ifdef INTERNAL_FLOAT
		    ret = op_read_float(data->vf, (float *)buf, buf_len/sizeof(float), &current_section);
		#else
		    ret = op_read(data->vf, (opus_int16 *)buf, buf_len/sizeof(opus_int16), &current_section);
		#endif
		if (ret == 0)
			return 0;
		if (ret < 0) {
			decoder_error (&data->error, ERROR_STREAM, 0,
					"Error in the stream!");
			continue;
		}

		if (current_section != data->last_section) {
			logit ("section change or first section");

			data->last_section = current_section;
			data->tags_change = 1;
			tags_free (data->tags);
			data->tags = tags_new ();
			get_comment_tags (data->vf, data->tags);
		  
		}

		sound_params->channels = op_channel_count(data->vf, -1);
		sound_params->rate = 48000;
		#ifdef INTERNAL_FLOAT
		  sound_params->fmt = SFMT_FLOAT;
		#else
		  sound_params->fmt = SFMT_S16 | SFMT_NE;
		#endif		

		  /* Update the bitrate information */
		bitrate = op_bitrate_instant (data->vf)/1000;
		if (bitrate > 0)
			data->bitrate = bitrate;
		break;
	}
	debug("decoded: %d samples, %u bytes, buffer: %d",ret,(unsigned int)sizeof(float)*ret,buf_len);
	#ifdef INTERNAL_FLOAT
	return ret*sizeof(float)*sound_params->channels;
	#else
	return ret*sizeof(opus_int16)*sound_params->channels;
	#endif
}

static int opus_current_tags (void *prv_data, struct file_tags *tags)
{
	struct opus_data *data = (struct opus_data *)prv_data;

	tags_copy (tags, data->tags);

	if (data->tags_change) {
		data->tags_change = 0;
		return 1;
	}

	return 0;
}


static int opus_get_bitrate (void *prv_data)
{
	struct opus_data *data = (struct opus_data *)prv_data;

	return data->bitrate;
}

static int opus_get_avg_bitrate (void *prv_data)
{
	struct opus_data *data = (struct opus_data *)prv_data;

	return data->avg_bitrate;
}

static int opus_get_duration (void *prv_data)
{

	struct opus_data *data = (struct opus_data *)prv_data;

	return data->duration;
}

static struct io_stream *opus_get_stream (void *prv_data)
{
	struct opus_data *data = (struct opus_data *)prv_data;

	return data->stream;
}

static void opus_get_name (const char *file ATTR_UNUSED, char buf[4])
{
	strcpy (buf, "OPS");
}

static int opus_our_format_ext (const char *ext)
{
	return !strcasecmp (ext, "opus");
}

static void opus_get_error (void *prv_data, struct decoder_error *error)
{
	struct opus_data *data = (struct opus_data *)prv_data;

	decoder_error_copy (error, &data->error);
}

static int opus_our_mime (const char *mime)
{
	return !strcasecmp (mime, "audio/opus");
}

static struct decoder opus_decoder = {
	DECODER_API_VERSION,
	NULL,
	NULL,
	opus_open,
	opus_open_stream,
	NULL,//opus_can_decode,
	opus_close,
	opus_decodeX,
	opus_seek,
	opus_tags,
	opus_get_bitrate,
	opus_get_duration,
	opus_get_error,
	opus_our_format_ext,
	opus_our_mime,
	opus_get_name,
	opus_current_tags,
	opus_get_stream,
	opus_get_avg_bitrate
};

struct decoder *plugin_init ()
{
	return &opus_decoder;
}
