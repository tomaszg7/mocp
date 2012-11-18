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
//#include <ogg/ogg.h>
//#include <opus/opus.h>

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
	int bitrate;
	int avg_bitrate;
	int duration;
	struct decoder_error error;
	int ok; /* was this stream successfully opened? */

//	int tags_change; /* the tags were changed from the last call of
//			    ogg_current_tags */
//	struct file_tags *tags;
};


/*static void get_comment_tags (OggVorbis_File *vf, struct file_tags *info)
{
	int i;
	vorbis_comment *comments;

	comments = ov_comment (vf, -1);
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
*/
/* Return a malloc()ed description of an ov_*() error. */
/*static char *vorbis_strerror (const int code)
{
	char *err;

	switch (code) {
		case OV_EREAD:
			err = "read error";
			break;
		case OV_ENOTVORBIS:
			err = "not a vorbis file";
			break;
		case OV_EVERSION:
			err = "vorbis version mismatch";
			break;
		case OV_EBADHEADER:
			err = "invalid Vorbis bitstream header";
			break;
		case OV_EFAULT:
			err = "internal (vorbis) logic fault";
			break;
		default:
			err = "unknown error";
	}

	return xstrdup (err);
}
*/


/* Fill info structure with data from ogg comments */
static void opus_tags (const char *file_name, struct file_tags *info,
		const int tags_sel)
{
  debug ("start");

	OggOpusFile *vf;
	FILE *file;
	int err_code;

return;  // Not yet implemented

	if (!(file = fopen (file_name, "r"))) {
		logit ("Can't open an OGG file: %s", strerror(errno));
		return;
	}

	// ov_test() is faster than ov_open(), but we can't read file time with it.
	if (tags_sel & TAGS_TIME) {
		vf=op_open_file(file, err_code);
		if (err_code  < 0) {
			logit ("Can't open %s: %d", file_name, err_code);
			fclose (file);

			return;
		}
	}
	else {
		vf=op_test_file(file,err_code);
		if (err_code < 0) {
			logit ("Can't open %s: %d", file_name, err_code);
			fclose (file);

			return;
		}
	}

	if (tags_sel & TAGS_COMMENTS)
		info=op_tags(vf,-1);

	if (tags_sel & TAGS_TIME) {
		int vorbis_time;

	    vorbis_time = op_pcm_total (vf, -1) / 48000;
	    if (vorbis_time >= 0)
			info->time = vorbis_time;
	}

	op_free (vf);
}

static size_t read_callback (void *ptr, size_t size, size_t nmemb,
		void *datasource)
{
    debug ("start");
	ssize_t res;

	res = io_read (datasource, ptr, size * nmemb);

	/* libvorbisfile expects the read callback to return >= 0 with errno
	 * set to non zero on error. */
	if (res < 0) {
		logit ("Read error");
		if (errno == 0)
			errno = 0xffff;
		res = 0;
	}
	else
		res /= size;

	return res;
}

// static int seek_callback (void *datasource, opus_int64 offset, int whence)
// {
//   debug ("start");
// 	debug ("Seek request to %ld (%s)", (long)offset,
// 			whence == SEEK_SET ? "SEEK_SET"
// 			: (whence == SEEK_CUR ? "SEEK_CUR" : "SEEK_END"));
// 	return io_seek (datasource, offset, whence);
// }

// static int close_callback (void *datasource ATTR_UNUSED)
// {
//   debug ("start");
// 	return 0;
// }
// 
// static opus_int64 tell_callback (void *datasource)
// {
//   debug ("start");
// 	return io_tell (datasource);
// }

static void opus_open_stream_internal (struct opus_data *data)
{
debug ("opus_open_int_start");
	int res=0;
	OpusFileCallbacks callbacks = {
		read_callback,
		NULL,//seek_callback,
		NULL,//close_callback,
		NULL//tell_callback
	};

	data->vf = op_open_callbacks(data->stream, &callbacks, NULL, 0, &res);
debug ("opus_open_int_callbacks");
	if (res < 0) {
//		char *vorbis_err = vorbis_strerror (res);

//		decoder_error (&data->error, ERROR_FATAL, 0, "%s",
//				res);
		debug ("op_open error: %d", res);
//		free (vorbis_err);

		io_close (data->stream);
	}
	else {
		data->last_section = -1;
		data->avg_bitrate = op_bitrate (data->vf, -1) / 1000;
		data->bitrate = data->avg_bitrate;
		data->duration = op_pcm_total (data->vf, -1) / 48000;
		if (data->duration == OP_EINVAL)
			data->duration = -1;
		data->ok = 1;
//		get_comment_tags (&data->vf, data->tags);
	}
debug ("opus_open_int_end"); 
}

static void *opus_open (const char *file)
{
debug ("opus_open_start");
	struct opus_data *data;
	data = (struct opus_data *)xmalloc (sizeof(struct opus_data));
	data->ok = 0;

	decoder_error_init (&data->error);
//	data->tags_change = 0;
//	data->tags = NULL;

	data->stream = io_open (file, 1);
	if (!io_ok(data->stream)) {
		decoder_error (&data->error, ERROR_FATAL, 0,
				"Can't load OGG: %s",
				io_strerror(data->stream));
		io_close (data->stream);
	}
	else
		opus_open_stream_internal (data);
debug ("opus_open_stop");

	return data;
}

static int opus_can_decode (struct io_stream *stream ATTR_UNUSED)
{
//	char buf[34];
debug ("opus_can_decode_open_start");

//	if (io_peek (stream, buf, 34) == 34 && !memcmp (buf, "OggS", 4)
//			&& !memcmp (buf + 28, "\01vorbis", 7))
		return 1;

//	return 0;
}

static void *opus_open_stream (struct io_stream *stream)
{
  debug ("start");
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
  debug ("start");
	struct opus_data *data = (struct opus_data *)prv_data;

	if (data->ok) {
		op_free (data->vf);
		io_close (data->stream);
	}

	decoder_error_clear (&data->error);
//	if (data->tags)
//		tags_free (data->tags);
	free (data);
}

static int opus_seek (void *prv_data, int sec)
{
  debug ("start");
	struct opus_data *data = (struct opus_data *)prv_data;

	assert (sec >= 0);

return -1; // Not yet implemented

	return op_pcm_seek_page (data->vf, sec * 48000) ? -1 : sec;
}

static int opus_decodeX (void *prv_data, char *buf, int buf_len,
		struct sound_params *sound_params)
{
  debug ("start");
	struct opus_data *data = (struct opus_data *)prv_data;
	int ret;
	int current_section;
	int bitrate;
//	opus_info *info;

	decoder_error_clear (&data->error);

	while (1) {
		ret = op_read(data->vf, buf, buf_len, &current_section);
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
//			data->tags_change = 1;
//			tags_free (data->tags);
//			data->tags = tags_new ();
//			get_comment_tags (&data->vf, data->tags);
		}

//		info = op_info (&data->vf, -1);
//		assert (info != NULL);
		sound_params->channels = op_channel_count(data->vf, -1);
		sound_params->rate = 48000;
		sound_params->fmt = SFMT_S16 | SFMT_NE;

		/* Update the bitrate information */
		bitrate = op_bitrate_instant (data->vf);
		if (bitrate > 0)
			data->bitrate = bitrate / 1000;

		break;
	}

	return ret;
}

/*static int vorbis_current_tags (void *prv_data, struct file_tags *tags)
{
	struct vorbis_data *data = (struct vorbis_data *)prv_data;

	tags_copy (tags, data->tags);

	if (data->tags_change) {
		data->tags_change = 0;
		return 1;
	}

	return 0;
}
*/

static int opus_get_bitrate (void *prv_data)
{
  debug ("start");
	struct opus_data *data = (struct opus_data *)prv_data;

	return data->bitrate;
}

static int opus_get_avg_bitrate (void *prv_data)
{
  debug ("start");
	struct opus_data *data = (struct opus_data *)prv_data;

	return data->avg_bitrate;
}

static int opus_get_duration (void *prv_data)
{
  debug ("start");

	struct opus_data *data = (struct opus_data *)prv_data;

	return data->duration;
}

static struct io_stream *opus_get_stream (void *prv_data)
{
    debug ("start");
	struct opus_data *data = (struct opus_data *)prv_data;

	return data->stream;
}

static void opus_get_name (const char *file ATTR_UNUSED, char buf[4])
{
  debug ("opus_get_name_start");

	strcpy (buf, "OPS");
}

static int opus_our_format_ext (const char *ext)
{
  debug ("opus_our_format_start");

	return !strcasecmp (ext, "opus");
	//return 1;
}

static void opus_get_error (void *prv_data, struct decoder_error *error)
{
  debug ("start");
	struct opus_data *data = (struct opus_data *)prv_data;

	decoder_error_copy (error, &data->error);
}

static int opus_our_mime (const char *mime)
{
  debug ("start");
	return !strcasecmp (mime, "audio/opus");
}

static struct decoder opus_decoder = {
	DECODER_API_VERSION,
	NULL,
	NULL,
	opus_open,
	opus_open_stream,
	opus_can_decode,
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
	NULL,			// opus_current_tags,
	opus_get_stream,
	opus_get_avg_bitrate
};

struct decoder *plugin_init ()
{
	return &opus_decoder;
}
