/*
 * MOC - music on console
 * Copyright (C) 2004 Damian Pietras <daper@daper.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

/* Based on aplay copyright (c) by Jaroslav Kysela <perex@suse.cz> 
 * and alsamixer copyright (c) 2010 Clemens Ladisch <clemens@ladisch.de>
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include <inttypes.h>
#include <alsa/asoundlib.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>
#define exp10(x) (exp((x) * log(10)))

#define DEBUG

#include "common.h"
#include "server.h"
#include "audio.h"
#include "options.h"
#include "log.h"

#define BUFFER_MAX_USEC	300000
#define MAX_LINEAR_DB_SCALE	2400

static snd_pcm_t *handle = NULL;

static struct
{
	unsigned int channels;
	unsigned int rate;
	snd_pcm_format_t format;
} params = { 0, 0, SND_PCM_FORMAT_UNKNOWN };

static snd_pcm_uframes_t buffer_frames;
static snd_pcm_uframes_t chunk_frames;
static int chunk_bytes = -1;
static char alsa_buf[512 * 1024];
static int alsa_buf_fill = 0;
static int bytes_per_frame;
static int bytes_per_sample;

static snd_mixer_t *mixer_handle = NULL;
static snd_mixer_elem_t *mixer_elem1 = NULL;
static snd_mixer_elem_t *mixer_elem2 = NULL;
static snd_mixer_elem_t *mixer_elem_curr = NULL;
static long mixer1_min = -1, mixer1_max = -1;
static long mixer2_min = -1, mixer2_max = -1;

/* Percentage volume setting for first and second mixer. */
static int volume1 = -1;
static int volume2 = -1;

/* Real volume setting as we last read them. */
static long real_volume1 = -1;
static long real_volume2 = -1;

static bool use_linear_volume_scale1 = true;
static bool use_linear_volume_scale2 = true;
double min_norm1, min_norm2;

/* Scale the mixer value to 0-100 range for first and second channel */
//TODO: SND_CTL_TLV_DB_GAIN_MUTE check, vide: alsamixer code
static int scale_volume (long real_vol, long *mixer_min, long *mixer_max, double *min_norm, bool *use_linear_volume_scale)
{
	int res;

	if (*use_linear_volume_scale) {
		res = (real_vol - *mixer_min) * 100 / (*mixer_max - *mixer_min);
	}
	else {
		res = (exp10 ((real_vol - *mixer_max) / 6000.0) - *min_norm) * 100 / (1 - *min_norm);
        }
//        logit ("Scale: %ld -> %d", real_vol, res);

	return res;
}

static long scale_volume_inverse (int vol, long *mixer_min, long *mixer_max, double *min_norm, bool *use_linear_volume_scale)
{
	long res;

	if (*use_linear_volume_scale) {
		res = lrint (vol * (*mixer_max - *mixer_min) / 100 + *mixer_min);
	}
	else {
		res = lrint(6000.0 * log10 (vol * (1 - *min_norm) / 100 + *min_norm) + *mixer_max);
        }
//        logit ("Inverse scale: %d -> %ld", vol, res);

	return res;
}

/* Map ALSA's mask to MOC's format (and visa versa). */
static const struct {
	snd_pcm_format_t mask;
	long format;
} format_masks[] = {
	{SND_PCM_FORMAT_S8, SFMT_S8},
	{SND_PCM_FORMAT_U8, SFMT_U8},
	{SND_PCM_FORMAT_S16, SFMT_S16},
	{SND_PCM_FORMAT_U16, SFMT_U16},
	{SND_PCM_FORMAT_S24, SFMT_S24},
	{SND_PCM_FORMAT_U24, SFMT_U24},
	{SND_PCM_FORMAT_S32, SFMT_S32},
	{SND_PCM_FORMAT_U32, SFMT_U32},
#ifdef WORDS_BIGENDIAN
	{SND_PCM_FORMAT_S24_3BE, SFMT_S24_3},
	{SND_PCM_FORMAT_U24_3BE, SFMT_U24_3}
#else
	{SND_PCM_FORMAT_S24_3LE, SFMT_S24_3},
	{SND_PCM_FORMAT_U24_3LE, SFMT_U24_3}
#endif
};

/* Given an ALSA mask, return a MOC format or zero if unknown. */
static inline long mask_to_format (const snd_pcm_format_mask_t *mask)
{
	long result = 0;

	for (size_t ix = 0; ix < ARRAY_SIZE(format_masks); ix += 1) {
		if (snd_pcm_format_mask_test (mask, format_masks[ix].mask))
			result |= format_masks[ix].format;
	}

#if 0
	if (snd_pcm_format_mask_test (mask, SND_PCM_FORMAT_S24))
		result |= SFMT_S32; /* conversion needed */
#endif

	return result;
}

/* Given a MOC format, return an ALSA mask.
 * Return SND_PCM_FORMAT_UNKNOWN if unknown. */
static inline snd_pcm_format_t format_to_mask (long format)
{
	snd_pcm_format_t result = SND_PCM_FORMAT_UNKNOWN;

	for (size_t ix = 0; ix < ARRAY_SIZE(format_masks); ix += 1) {
		if (format_masks[ix].format == format) {
			result = format_masks[ix].mask;
			break;
		}
	}

	return result;
}

#ifndef NDEBUG
static void alsa_log_cb (const char *unused1 ATTR_UNUSED,
                         int unused2 ATTR_UNUSED,
                         const char *unused3 ATTR_UNUSED,
                         int unused4 ATTR_UNUSED, const char *fmt, ...)
{
	char *msg;
	va_list va;

	assert (fmt);

	va_start (va, fmt);
	msg = format_msg_va (fmt, va);
	va_end (va);

	logit ("ALSA said: %s", msg);
	free (msg);
}
#endif

static snd_pcm_hw_params_t *alsa_open_device (const char *device)
{
	int rc;
	snd_pcm_hw_params_t *result;

	assert (!handle);

	rc = snd_pcm_open (&handle, device, SND_PCM_STREAM_PLAYBACK,
	                                    SND_PCM_NONBLOCK);
	if (rc < 0) {
		error ("Can't open audio: %s", snd_strerror (rc));
		goto err1;
	}

	rc = snd_pcm_hw_params_malloc (&result);
	if (rc < 0) {
		error ("Can't allocate ALSA hardware parameters structure: %s",
		        snd_strerror (rc));
		goto err2;
	}

	rc = snd_pcm_hw_params_any (handle, result);
	if (rc < 0) {
		error ("Can't initialize hardware parameters structure: %s",
		        snd_strerror (rc));
		goto err3;
	}

	if (0) {
	err3:
		snd_pcm_hw_params_free (result);
	err2:
		snd_pcm_close (handle);
	err1:
		result = NULL;
		handle = NULL;
	}

	return result;
}

/* Fill caps with the device capabilities. Return 0 on error. */
static int fill_capabilities (struct output_driver_caps *caps)
{
	int result = 0;
	snd_pcm_hw_params_t *hw_params;

	assert (!handle);

	hw_params = alsa_open_device (options_get_str ("ALSADevice"));
	if (!hw_params)
		return 0;

	do {
		int rc;
		unsigned int val;
		snd_pcm_format_mask_t *format_mask;

		rc = snd_pcm_hw_params_get_channels_min (hw_params, &val);
		if (rc < 0) {
			error ("Can't get the minimum number of channels: %s",
			        snd_strerror (rc));
			break;
		}
		caps->min_channels = val;

		rc = snd_pcm_hw_params_get_channels_max (hw_params, &val);
		if (rc < 0) {
			error ("Can't get the maximum number of channels: %s",
			        snd_strerror (rc));
			break;
		}
		caps->max_channels = val;

		rc = snd_pcm_hw_params_set_rate_resample (handle, hw_params, 0);
		if (rc < 0) {
			error ("Can't restrict ALSA configuration space to contain only real hardware rates: %s",
			        snd_strerror (rc));
			break;
		}

		rc = snd_pcm_hw_params_get_rate_min (hw_params, &val, 0);
		if (rc < 0) {
			error ("Can't get the minimum sample rate: %s",
			        snd_strerror(rc));
			break;
		}
		caps->min_rate = val;

		rc = snd_pcm_hw_params_get_rate_max (hw_params, &val, 0);
		if (rc < 0) {
			error ("Can't get the maximum sample rate: %s",
			        snd_strerror(rc));
			break;
		}
		caps->max_rate = val;

		rc = snd_pcm_format_mask_malloc (&format_mask);
		if (rc < 0) {
			error ("Can't allocate format mask: %s", snd_strerror (rc));
			break;
		}
		snd_pcm_hw_params_get_format_mask (hw_params, format_mask);
		caps->formats = mask_to_format (format_mask) | SFMT_NE;
		snd_pcm_format_mask_free (format_mask);

		result = 1;
	} while (0);

	snd_pcm_hw_params_free (hw_params);
	snd_pcm_close (handle);
	handle = NULL;

	return result;
}

static void handle_mixer_events (snd_mixer_t *mixer_handle)
{
	struct pollfd *fds = NULL;

	assert (mixer_handle);

	do {
		int rc, count;

		count = snd_mixer_poll_descriptors_count (mixer_handle);
		if (count < 0) {
			logit ("snd_mixer_poll_descriptors_count() failed: %s",
			        snd_strerror (count));
			break;
		}

		fds = xcalloc (count, sizeof (struct pollfd));

		rc = snd_mixer_poll_descriptors (mixer_handle, fds, count);
		if (rc < 0) {
			logit ("snd_mixer_poll_descriptors() failed: %s",
			        snd_strerror (rc));
			break;
		}

		rc = poll (fds, count, 0);
		if (rc < 0) {
			error_errno ("poll() failed", errno);
			break;
		}

		if (rc == 0)
			break;

		debug ("Mixer event");

		rc = snd_mixer_handle_events (mixer_handle);
		if (rc < 0)
			logit ("snd_mixer_handle_events() failed: %s", snd_strerror (rc));
	} while (0);

	free (fds);
}

static int alsa_read_mixer_raw (snd_mixer_elem_t *elem, bool raw)
{
	if (mixer_handle) {
		long volume = 0;
		int nchannels = 0;
		bool joined;
		int i;
		int err;

		assert (elem != NULL);

		handle_mixer_events (mixer_handle);

		joined = snd_mixer_selem_has_playback_volume_joined (elem);

		for (i = 0; i < SND_MIXER_SCHN_LAST; i++) {
			if (snd_mixer_selem_has_playback_channel (elem, i)) {
				long vol;

				nchannels++;
				err = raw ? snd_mixer_selem_get_playback_volume (elem, i, &vol) :
					snd_mixer_selem_get_playback_dB (elem, i, &vol);
				if (err < 0) {
					error ("Can't read mixer: %s",
							snd_strerror(err));
					return -1;
				}
//				logit ("Vol %d: %ld", i, vol);
				volume += vol;
			}
			if (joined)
				break;
		}

		if (nchannels > 0)
			volume /= nchannels;
		else {
			logit ("Mixer has no channels");
			volume = -1;
		}

		/*logit ("Max: %ld, Min: %ld", mixer_max, mixer_min);*/
		return volume;

	}
	else
		return -1;
}

static snd_mixer_elem_t *alsa_init_mixer_channel (const char *name,
		long *vol_min, long *vol_max, double *min_norm, bool *use_linear_volume_scale)
{
	snd_mixer_selem_id_t *sid;
	snd_mixer_elem_t *elem = NULL;

	long db_min, db_max;

	snd_mixer_selem_id_malloc (&sid);
	snd_mixer_selem_id_set_index (sid, 0);
	snd_mixer_selem_id_set_name (sid, name);

	if (!(elem = snd_mixer_find_selem(mixer_handle, sid)))
		error ("Can't find mixer %s", name);
	else if (!snd_mixer_selem_has_playback_volume(elem)) {
		error ("Mixer device has no playback volume (%s).", name);
		elem = NULL;
	}
	else {
		snd_mixer_selem_get_playback_volume_range (elem, vol_min,
				vol_max);
		if ((snd_mixer_selem_get_playback_dB_range (elem, &db_min, &db_max) >= 0) && (db_min < db_max) && (db_max - db_min >= MAX_LINEAR_DB_SCALE)) {
			*use_linear_volume_scale = false;
			*min_norm = exp10((db_min - db_max) / 6000.0);
			*vol_min = db_min;
			*vol_max = db_max;
			logit ("Opened mixer (%s), perceptive volume range: %ld-%lddB, min_norm: %f", name,
				*vol_min, *vol_max,*min_norm);
		}
		else if (*vol_min >= *vol_max) {
			logit ("Mixer (%s) has no usable volume range", name);
			elem = NULL;
			*vol_min = -1;
			*vol_max = -1;
		}
		else {
			logit ("Opened mixer (%s), linear volume range: %ld-%ld", name,
				*vol_min, *vol_max);
		}
	}

	snd_mixer_selem_id_free (sid);

	return elem;
}

static void alsa_close_mixer ()
{
	if (mixer_handle) {
		int rc;

		rc = snd_mixer_close (mixer_handle);
		if (rc < 0)
			logit ("Can't close mixer: %s", snd_strerror (rc));

		mixer_handle = NULL;
	}
}

static void alsa_open_mixer (const char *device)
{
	int rc;

	assert (!mixer_handle);

	rc = snd_mixer_open (&mixer_handle, 0);
	if (rc < 0) {
		error ("Can't open ALSA mixer: %s", snd_strerror (rc));
		goto err;
	}

	rc = snd_mixer_attach (mixer_handle, device);
	if (rc < 0) {
		error ("Can't attach mixer: %s", snd_strerror (rc));
		goto err;
	}

	rc = snd_mixer_selem_register (mixer_handle, NULL, NULL);
	if (rc < 0) {
		error ("Can't register mixer: %s", snd_strerror (rc));
		goto err;
	}

	rc = snd_mixer_load (mixer_handle);
	if (rc < 0) {
		error ("Can't load mixer: %s", snd_strerror (rc));
		goto err;
	}

	if (0) {
	err:
		alsa_close_mixer ();
	}
}

static void alsa_set_current_mixer ()
{
	if (mixer_elem1 && (real_volume1 = alsa_read_mixer_raw (mixer_elem1,use_linear_volume_scale1)) != -1) {
		volume1 = scale_volume (real_volume1, &mixer1_min, &mixer1_max, &min_norm1, &use_linear_volume_scale1);
		assert (RANGE(0, volume1, 100));
	}
	else {
		mixer_elem1 = NULL;
		mixer_elem_curr = mixer_elem2;
	}

	if (mixer_elem2 && (real_volume2 = alsa_read_mixer_raw (mixer_elem2,use_linear_volume_scale2)) != -1) {
		volume2 = scale_volume (real_volume2, &mixer2_min, &mixer2_max, &min_norm2, &use_linear_volume_scale2);
		assert (RANGE(0, volume2, 100));
	}
	else {
		mixer_elem2 = NULL;
		mixer_elem_curr = mixer_elem1;
	}
}

static void alsa_shutdown ()
{
	alsa_close_mixer ();

#ifndef NDEBUG
	snd_lib_error_set_handler (NULL);
#endif
}

static int alsa_init (struct output_driver_caps *caps)
{
	int result = 0;
	const char *device;

	assert (!mixer_handle);

	device = options_get_str ("ALSADevice");
	logit ("Initialising ALSA device: %s", device);

#ifndef NDEBUG
	snd_lib_error_set_handler (alsa_log_cb);
#endif

	alsa_open_mixer (device);

	if (mixer_handle) {
		mixer_elem1 = alsa_init_mixer_channel (options_get_str ("ALSAMixer1"),
		              &mixer1_min, &mixer1_max, &min_norm1, &use_linear_volume_scale1);
		mixer_elem2 = alsa_init_mixer_channel (options_get_str ("ALSAMixer2"),
		              &mixer2_min, &mixer2_max, &min_norm2, &use_linear_volume_scale2);
	}

	mixer_elem_curr = mixer_elem1 ? mixer_elem1 : mixer_elem2;

	if (mixer_elem_curr)
		alsa_set_current_mixer ();

	if (!mixer_elem_curr)
		goto err;

	result = fill_capabilities (caps);
	if (result == 0)
		goto err;

	if (0) {
	err:
		alsa_shutdown ();
	}

	return result;
}

static int alsa_open (struct sound_params *sound_params)
{
	int rc, result = 0;
	unsigned int period_time, buffer_time;
	char fmt_name[128];
	const char *device;
	snd_pcm_hw_params_t *hw_params;

	assert (!handle);

	params.format = format_to_mask (sound_params->fmt & SFMT_MASK_FORMAT);
	if (params.format == SND_PCM_FORMAT_UNKNOWN) {
		error ("Unknown sample format: %s",
		        sfmt_str (sound_params->fmt, fmt_name, sizeof (fmt_name)));
		return 0;
	}

	device = options_get_str ("ALSADevice");
	logit ("Opening ALSA device: %s", device);

	hw_params = alsa_open_device (device);
	if (!hw_params)
		return 0;

	rc = snd_pcm_hw_params_set_access (handle, hw_params,
	                                   SND_PCM_ACCESS_RW_INTERLEAVED);
	if (rc < 0) {
		error ("Can't set ALSA access type: %s", snd_strerror (rc));
		goto err;
	}

	rc = snd_pcm_hw_params_set_format (handle, hw_params, params.format);
	if (rc < 0) {
		error ("Can't set sample format: %s", snd_strerror (rc));
		goto err;
	}

	bytes_per_sample = sfmt_Bps (sound_params->fmt);
	logit ("Set sample width: %d bytes", bytes_per_sample);

	params.rate = sound_params->rate;
	rc = snd_pcm_hw_params_set_rate_near (handle, hw_params, &params.rate, 0);
	if (rc < 0) {
		error ("Can't set sample rate: %s", snd_strerror (rc));
		goto err;
	}

	logit ("Set rate: %uHz", params.rate);

	rc = snd_pcm_hw_params_set_channels (handle, hw_params,
	                                     sound_params->channels);
	if (rc < 0) {
		error ("Can't set number of channels: %s", snd_strerror (rc));
		goto err;
	}

	logit ("Set channels: %d", sound_params->channels);

	rc = snd_pcm_hw_params_get_buffer_time_max (hw_params, &buffer_time, 0);
	if (rc < 0) {
		error ("Can't get maximum buffer time: %s", snd_strerror (rc));
		goto err;
	}

	buffer_time = MIN(buffer_time, BUFFER_MAX_USEC);
	period_time = buffer_time / 4;

	rc = snd_pcm_hw_params_set_period_time_near (handle, hw_params,
	                                             &period_time, 0);
	if (rc < 0) {
		error ("Can't set period time: %s", snd_strerror (rc));
		goto err;
	}

	rc = snd_pcm_hw_params_set_buffer_time_near (handle, hw_params,
	                                             &buffer_time, 0);
	if (rc < 0) {
		error ("Can't set buffer time: %s", snd_strerror (rc));
		goto err;
	}

	rc = snd_pcm_hw_params (handle, hw_params);
	if (rc < 0) {
		error ("Can't set audio parameters: %s", snd_strerror (rc));
		goto err;
	}

	snd_pcm_hw_params_get_period_size (hw_params, &chunk_frames, 0);
	debug ("Chunk size: %lu frames", chunk_frames);

	snd_pcm_hw_params_get_buffer_size (hw_params, &buffer_frames);
	debug ("Buffer size: %lu frames", buffer_frames);
	debug ("Buffer time: %"PRIu64"us",
	        (uint64_t) buffer_frames * UINT64_C(1000000) / params.rate);

	bytes_per_frame = sound_params->channels * bytes_per_sample;
	debug ("Frame size: %d bytes", bytes_per_frame);

	chunk_bytes = chunk_frames * bytes_per_frame;

	if (chunk_frames == buffer_frames) {
		error ("Can't use period equal to buffer size (%lu == %lu)",
				chunk_frames, buffer_frames);
		goto err;
	}

	rc = snd_pcm_prepare (handle);
	if (rc < 0) {
		error ("Can't prepare audio interface for use: %s",
		        snd_strerror (rc));
		goto err;
	}

	logit ("ALSA device opened");

	params.channels = sound_params->channels;
	alsa_buf_fill = 0;
	result = 1;

err:
	snd_pcm_hw_params_free (hw_params);

	return result;
}

/* Play from alsa_buf as many chunks as possible. Move the remaining data
 * to the beginning of the buffer. Return the number of bytes written
 * or -1 on error. */
static int play_buf_chunks ()
{
	int written = 0;
	bool zero_logged = false;

	while (alsa_buf_fill >= chunk_bytes) {
		int rc;

		rc = snd_pcm_writei (handle, alsa_buf + written, chunk_frames);

		if (rc == 0) {
			if (!zero_logged) {
				debug ("Played 0 bytes");
				zero_logged = true;
			}
			continue;
		}

		zero_logged = false;

		if (rc > 0) {
			int written_bytes = rc * bytes_per_frame;

			written += written_bytes;
			alsa_buf_fill -= written_bytes;

			debug ("Played %d bytes", written_bytes);
			continue;
		}

		rc = snd_pcm_recover (handle, rc, 0);

		switch (rc) {
		case 0:
			break;
		case -EAGAIN:
			if (snd_pcm_wait (handle, 500) < 0)
				logit ("snd_pcm_wait() failed");
			break;
		default:
			error ("Can't play: %s", snd_strerror (rc));
			return -1;
		}
	}

	debug ("%d bytes remain in alsa_buf", alsa_buf_fill);
	memmove (alsa_buf, alsa_buf + written, alsa_buf_fill);

	return written;
}

static void alsa_close ()
{
	snd_pcm_sframes_t delay;

	assert (handle != NULL);

	/* play what remained in the buffer */
	if (alsa_buf_fill > 0) {
		unsigned int samples_required;

		assert (alsa_buf_fill < chunk_bytes);

		samples_required = (chunk_bytes - alsa_buf_fill) / bytes_per_sample;
		snd_pcm_format_set_silence (params.format, alsa_buf + alsa_buf_fill,
									samples_required);

		alsa_buf_fill = chunk_bytes;
		play_buf_chunks ();
	}

	/* Wait for ALSA buffers to empty.
	 * Do not be tempted to use snd_pcm_nonblock() and snd_pcm_drain()
	 * here; there are two bugs in ALSA which make it a bad idea (see
	 * the SVN commit log for r2550).  Instead we sleep for the duration
	 * of the still unplayed samples. */
	if (snd_pcm_delay (handle, &delay) == 0)
		xsleep (delay, params.rate);
	snd_pcm_close (handle);
	logit ("ALSA device closed");

	params.format = 0;
	params.rate = 0;
	params.channels = 0;
	buffer_frames = 0;
	chunk_frames = 0;
	chunk_bytes = -1;
	handle = NULL;
}

static int alsa_play (const char *buff, const size_t size)
{
	int to_write = size;
	int buf_pos = 0;

	assert (chunk_bytes > 0);

	debug ("Got %zu bytes to play", size);

	while (to_write) {
		int to_copy;

		to_copy = MIN(to_write, ssizeof(alsa_buf) - alsa_buf_fill);
		memcpy (alsa_buf + alsa_buf_fill, buff + buf_pos, to_copy);
		to_write -= to_copy;
		buf_pos += to_copy;
		alsa_buf_fill += to_copy;

		debug ("Copied %d bytes to alsa_buf (now filled with %d bytes)",
				to_copy, alsa_buf_fill);

		if (play_buf_chunks() < 0)
			return -1;
	}

	debug ("Played everything");

	return size;
}

static int alsa_read_mixer ()
{
	long curr_real_vol;
	long *real_vol;
	int *vol;

	if (mixer_elem_curr == mixer_elem1) {
		real_vol = &real_volume1;
		vol = &volume1;
		curr_real_vol = alsa_read_mixer_raw (mixer_elem_curr,use_linear_volume_scale1);
	}
	else {
		real_vol = &real_volume2;
		vol = &volume2;
		curr_real_vol = alsa_read_mixer_raw (mixer_elem_curr,use_linear_volume_scale2);
	}

//	logit ("Current volume raw: %ld",curr_real_vol);

	if (*real_vol != curr_real_vol) {
		*real_vol = curr_real_vol;
		*vol = (vol == &volume1) ? scale_volume(*real_vol, &mixer1_min, &mixer1_max, &min_norm1, &use_linear_volume_scale1)
			: scale_volume(*real_vol, &mixer2_min, &mixer2_max, &min_norm2, &use_linear_volume_scale2);
		logit ("Mixer volume has changes since we last read it.");

		logit ("Current volume: %d, real_volume: %ld",*vol,curr_real_vol);

	}

	return *vol;
}

static void alsa_set_mixer (int vol)
{
	if (mixer_handle) {
		int err;
		long vol_alsa;
		long *real_vol;

		if (mixer_elem_curr == mixer_elem1) {
			volume1 = vol;
			real_vol = &real_volume1;
			vol_alsa = scale_volume_inverse (volume1, &mixer1_min, &mixer1_max, &min_norm1, &use_linear_volume_scale1);

			debug ("Setting vol to %ld, requested: %d", vol_alsa, vol);

			err = use_linear_volume_scale1 ? snd_mixer_selem_set_playback_volume_all (mixer_elem_curr, vol_alsa) :
				snd_mixer_selem_set_playback_dB_all (mixer_elem_curr, vol_alsa,0);

			if (err < 0)
				error ("Can't set mixer: %s", snd_strerror(err));
			else {
				*real_vol = alsa_read_mixer_raw (mixer_elem_curr,use_linear_volume_scale1);
			}
		}
		else {
			volume2 = vol;
			real_vol = &real_volume2;
			vol_alsa = scale_volume_inverse (volume2, &mixer2_min, &mixer2_max, &min_norm2, &use_linear_volume_scale2);

			debug ("Setting vol to %ld, requested: %d", vol_alsa, vol);

			err = use_linear_volume_scale2 ? snd_mixer_selem_set_playback_volume_all (mixer_elem_curr, vol_alsa) :
				snd_mixer_selem_set_playback_dB_all (mixer_elem_curr, vol_alsa,0);

			if (err < 0)
				error ("Can't set mixer: %s", snd_strerror(err));
			else {
				*real_vol = alsa_read_mixer_raw (mixer_elem_curr,use_linear_volume_scale2);
			}
		}
	logit("Intended ALSA volume %ld, got %ld", vol_alsa, *real_vol);
	}
}

static int alsa_get_buff_fill ()
{
	int result = 0;

	do {
		int rc;
		snd_pcm_sframes_t delay;

		if (!handle)
			break;

		rc = snd_pcm_delay (handle, &delay);
		if (rc < 0) {
			logit ("snd_pcm_delay() failed: %s", snd_strerror (rc));
			break;
		}

		/* delay can be negative if an underrun occurs */
		result = MAX(delay, 0) * bytes_per_frame;
	} while (0);

	return result;
}

static int alsa_reset ()
{
	int result = 0;

	do {
		int rc;

		if (!handle) {
			logit ("alsa_reset() when the device is not opened.");
			break;
		}

		rc = snd_pcm_drop (handle);
		if (rc < 0) {
			error ("Can't reset the device: %s", snd_strerror (rc));
			break;
		}

		rc = snd_pcm_prepare (handle);
		if (rc < 0) {
			error ("Can't prepare after reset: %s", snd_strerror (rc));
			break;
		}

		alsa_buf_fill = 0;
		result = 1;
	} while (0);

	return result;
}

static int alsa_get_rate ()
{
	return params.rate;
}

static void alsa_toggle_mixer_channel ()
{
	if (mixer_elem_curr == mixer_elem1 && mixer_elem2)
		mixer_elem_curr = mixer_elem2;
	else if (mixer_elem1)
		mixer_elem_curr = mixer_elem1;
}

static char *alsa_get_mixer_channel_name ()
{
	char *result;

	if (mixer_elem_curr == mixer_elem1)
		result = xstrdup (options_get_str ("ALSAMixer1"));
	else
		result = xstrdup (options_get_str ("ALSAMixer2"));

	return result;
}

void alsa_funcs (struct hw_funcs *funcs)
{
	funcs->init = alsa_init;
	funcs->shutdown = alsa_shutdown;
	funcs->open = alsa_open;
	funcs->close = alsa_close;
	funcs->play = alsa_play;
	funcs->read_mixer = alsa_read_mixer;
	funcs->set_mixer = alsa_set_mixer;
	funcs->get_buff_fill = alsa_get_buff_fill;
	funcs->reset = alsa_reset;
	funcs->get_rate = alsa_get_rate;
	funcs->toggle_mixer_channel = alsa_toggle_mixer_channel;
	funcs->get_mixer_channel_name = alsa_get_mixer_channel_name;
}
