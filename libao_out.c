/*
 * MOC - music on console
 *
 * libao sound driver for MOC by Edward Lee.
 * Copyright (C) 2012 Edward Lee <e45lee@plg.uwaterloo.ca>
 * ( Based off of SNDIO driver )
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

#ifdef HAVE_LIBAO
# include <ao/ao.h>
#endif

#include <assert.h>

#include "common.h"
#include "audio.h"
#include "options.h"
#include "log.h"


static struct ao_device* output_device = NULL;
static void libao_close ();
static int output_id = -1;
static struct sound_params params;

static int libao_init (struct output_driver_caps *caps)
{
	ao_info *device_info;
	char *device;

	assert (caps != NULL);

	/** Call ao_initalize */
	ao_initialize( );

	device = options_get_str("AODevice");
	/** Check if there is option selecting a device */
	if (device) {
		if( ( output_id = ao_driver_id(device) ) == -1 ) {
			error( "Failed to open device %s for libao output.", device);
			      ao_shutdown( );
			return 0;
		}
	}
	else
	/** Check if default device is available */
	if( ( output_id = ao_default_driver_id( ) ) == -1 ) {
		error( "Failed to find default device for libao output." );
		ao_shutdown( );
		return 0;
	}

	device_info = ao_driver_info (output_id);
	logit( "Found libao device = %d: %s.", output_id, device_info->name);

	if (device_info->type == AO_TYPE_FILE) {
		error( "Output to file with libao not implemented." );
		ao_shutdown( );
		return 0;
	}

	caps->min_channels = 1;
	caps->max_channels = 100;
	caps->formats = SFMT_S8 | SFMT_S16 | SFMT_S32;

	switch (device_info->preferred_byte_format) {
		case AO_FMT_LITTLE:
			caps->formats |= SFMT_LE;
			break;
		case AO_FMT_BIG:
			caps->formats |= SFMT_BE;
			break;
		default:
			caps->formats |= SFMT_NE;
			break;
	}

	return 1;
}

static void libao_shutdown ()
{
	/** Shutdown libao */
	ao_shutdown( );
}

/* Return 0 on failure. */
static int libao_open (struct sound_params *sound_params)
{
	ao_sample_format format;
	assert( output_device == NULL );

	/** Open */
	format.byte_format = ao_driver_info (output_id) -> preferred_byte_format;
	format.matrix = NULL;
	format.rate = sound_params->rate;
	format.channels = sound_params->channels;
	params = *sound_params;

	if( sound_params->fmt & SFMT_S8 )
			format.bits = 8;
	else if( sound_params->fmt & SFMT_S16 )
			format.bits = 16;
	else if( sound_params->fmt & SFMT_S32 )
			format.bits = 32;

	if( ( output_device = ao_open_live( output_id, &format, NULL ) ) == NULL )
	{
		const char *result;

		switch (errno) {
			case AO_ENODRIVER:
				result = "No driver corresponds to driver_id.";
				break;
			case AO_ENOTLIVE:
				result = "This driver is not a live output device.";
				break;
			case AO_EBADOPTION:
				result = "A valid option key has an invalid value.";
				break;
			case AO_EOPENDEVICE:
				result = "Cannot open the device (for example, if /dev/dsp cannot be opened for writing).";
				break;
			case AO_EFAIL:
			default:
				result = "Unknown error.";
		}
		logit( "Failed to open libao output device: %s", result );
		return 0;
	}

	return 1;
}

/* Return the number of bytes played, or zero on error. */
static int libao_play (const char *buff, const size_t size)
{
	int ret;
	assert (output_device != NULL);

	logit("Playing buffer, %d bytes.", (int)size);

	ret = ao_play (output_device, (void*)buff, size);
	logit("Result = %d.", ret);
	if( ret == 0 )
	{
		return -1;
	}

	return size;
}

static void libao_close ()
{
	assert (output_device != NULL);

	ao_close(output_device);
	output_device = NULL;
}

static int libao_read_mixer ()
{
	return 100;
}

static void libao_set_mixer (int vol ATTR_UNUSED)
{
}

static int libao_get_buff_fill ()
{
	assert (output_device != NULL);

	/* Since we cannot stop libao playing the samples already in
	 * its buffer, there will never be anything left unheard. */

	return 0;
}

static int libao_reset ()
{
	assert (output_device != NULL);

	/* libao will continue to play the samples already in its buffer
	 * regardless of what we do, so there's nothing we can do. */

	return 1;
}

static int libao_get_rate ()
{
	assert (output_device != NULL);

	return params.rate;
}

static void libao_toggle_mixer_channel ()
{
}

static char *libao_get_mixer_channel_name ()
{
	return xstrdup ("Dummy mixer");
}

void libao_funcs (struct hw_funcs *funcs)
{
	funcs->init = libao_init;
	funcs->shutdown = libao_shutdown;
	funcs->open = libao_open;
	funcs->close = libao_close;
	funcs->play = libao_play;
	funcs->read_mixer = libao_read_mixer;
	funcs->set_mixer = libao_set_mixer;
	funcs->get_buff_fill = libao_get_buff_fill;
	funcs->reset = libao_reset;
	funcs->get_rate = libao_get_rate;
	funcs->toggle_mixer_channel = libao_toggle_mixer_channel;
	funcs->get_mixer_channel_name = libao_get_mixer_channel_name;
}
