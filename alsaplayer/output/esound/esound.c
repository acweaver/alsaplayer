/*  esound.c - ESD output plugin
 *  Copyright (C) 1999 Andy Lo A Foe <andy@alsa-project.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/ 

#include "config.h"
#include <stdio.h>
#include <esd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <dlfcn.h>
#include "output_plugin.h"

static int esound_socket = -1;

static int esound_init()
{
	char *host = NULL;
	char *name = NULL;

	esd_format_t format = ESD_BITS16 | ESD_STEREO | ESD_STREAM | ESD_PLAY;
	int rate = 44100;
#if 0            
	printf("ESD: loading libesd.so\n");
	void *handle = dlopen("libesd.so", RTLD_LAZY);
	if (handle == NULL) {
		printf("ESD: %s\n", dlerror());
		return 0;
	} else {
		int (*esd_play_stream_fallback)(esd_format_t,int,
                        const char *, const char *);
                        
                 esd_play_stream_fallback = dlsym(handle, "esd_play_stream_fallback");
                 eounsd_socket = (*esd_play_stream_fallback)(format, rate, host,
                                name);
                if (eounsd_socket < 0) {
					/* printf("ESD: could not open socket connection\n"); */
					dlclose(handle);
					return 0; 
                }
	}
#else
	esound_socket = esd_play_stream_fallback(format, rate, host, name);
	if (esound_socket < 0) {
		/* printf("ESD: could not open socket connection\n"); */
		return 0;
	}	
#endif
	return 1;
}

static int esound_open(int card, int device)
{
	if (esound_socket >= 0) 
		return 1;
	return 0;
}


static void esound_close()
{
	return;
}


static int esound_write(void *data, int count)
{
	write(esound_socket, data, count);
	return 1;
}


static int esound_set_buffer(int fragment_size, int fragment_count, int channels)
{
	printf("ESD: fragments fixed at 256/256, stereo\n");
	return 1;
}


static int esound_set_sample_rate(int rate)
{
	printf("ESD: rate fixed at 44100Hz\n");
	return 1;
}

static int esound_get_latency()
{
	return ((256*256));	// Hardcoded, but esd sucks ass
}

output_plugin esd_output = {
	OUTPUT_PLUGIN_VERSION,
	{ "ESD output v1.0 (broken for mono output)" },
	{ "Andy Lo A Foe" },
	esound_init,
	esound_open,
	esound_close,
	esound_write,
	esound_set_buffer,
	esound_set_sample_rate,
	NULL,
	esound_get_latency
};


output_plugin *output_plugin_info(void)
{
	return &esd_output;
}