/*
 *  audiofile_engine.cpp (C) 1999 by Michael Pruett <michael@68k.org>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <stdio.h>
#include <alloca.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <audiofile.h>
#include "input_plugin.h"

/*
	Audio File Library frames have a different meaning than AlsaPlayer
	frames.  In AlsaPlayer a frame is an array of bytes that make of a
	number of samples where Audio File Library sees a frame as a single
	sample (or sample pair in case of stereo).  Standardizing on one
	definition of "frame" would be good of course.  Comments?
*/

static const int FRAME_COUNT = 8192;
	
struct af_local_data
{
	AFfilehandle	filehandle;
	int		frameSize;
	int		sampleRate;
	char		filename[NAME_MAX];
};

/*
	Take a path as input and return the last part of it as the file name.
*/
static char *getfilenamefrompath (char *path)
{
	char	*p = strrchr(path, '/');

	if (p != NULL)
		p++;
	else
		p = path;

	return p;
}

static void init_audiofile ()
{
	return;
}

static int audiofile_open (input_object *obj, char *name)
{
	static int				audiofile_init_done = 0;
	struct af_local_data	*data;

#ifdef DEBUG
	printf("audiofile_open: opening %s\n", name);
#endif

	if (!obj)
		return 0;

	obj->local_data = malloc(sizeof(struct af_local_data));

	if (!obj->local_data)
	{
		return 0;
	}

	data = (struct af_local_data *) obj->local_data;

	data->filehandle = afOpenFile(name, "r", NULL);
	strncpy(data->filename, getfilenamefrompath(name), NAME_MAX);

	if (data->filehandle == AF_NULL_FILEHANDLE)
	{
		free(obj->local_data);
		return 0;
	}

	if (audiofile_init_done == 0)
	{
		init_audiofile();
		audiofile_init_done = 1;
	}

	data->frameSize = (int) afGetFrameSize(data->filehandle, AF_DEFAULT_TRACK, 1);
	data->sampleRate = (int) afGetRate(data->filehandle, AF_DEFAULT_TRACK);
	obj->nr_frames =
		(afGetFrameCount(data->filehandle, AF_DEFAULT_TRACK) * data->frameSize +
		FRAME_COUNT - 1) / FRAME_COUNT;
	obj->nr_channels = afGetChannels(data->filehandle, AF_DEFAULT_TRACK);
	obj->frame_size = FRAME_COUNT;

#ifdef DEBUG
	printf("data.frameSize = %d\n", data->frameSize);
	printf("data.sampleRate = %d\n", data->sampleRate);
	printf("nr_frames = %d\n", obj->nr_frames);
	printf("nr_channels = %d\n", obj->nr_channels);
	printf("frame_size = %d\n", obj->frame_size);
#endif

	return 1;
}


void audiofile_close (input_object *obj)
{
#ifdef DEBUG
	printf("audiofile_close\n");
#endif

	if (obj == NULL)
		return;

	if (obj->local_data)
	{
		struct af_local_data *data = 
			(struct af_local_data *) obj->local_data;
		afCloseFile(data->filehandle);
		free(obj->local_data);
		obj->local_data = NULL;
	}
}


static int audiofile_play_frame (input_object *obj, char *buf)
{
	int	bytes_to_read;
	int	frames_read;
	void	*buffer;

	struct af_local_data	*data;

	if (!obj)
		return 0;

	data = (struct af_local_data *) obj->local_data;

	if (!data)
		return 0;

	buffer = alloca(FRAME_COUNT * data->frameSize);

	if (!buffer)
		return 0;

	bytes_to_read = FRAME_COUNT;

	frames_read = afReadFrames(data->filehandle, AF_DEFAULT_TRACK, buffer,
		 bytes_to_read / data->frameSize);
	if (buf)
		memcpy(buf, buffer,  FRAME_COUNT);

	if (frames_read * data->frameSize < FRAME_COUNT)
		return 0;

	return 1;
}


static int audiofile_frame_seek (input_object *obj, int frame)
{
	AFframecount	currentFrame;
	int				result = 0;

	struct af_local_data	*data;

#ifdef DEBUG
	printf("audiofile_frame_seek: %d\n", frame);
#endif

	if (!obj)
			return result;

	data = (struct af_local_data *) obj->local_data;

	if (data->filehandle == AF_NULL_FILEHANDLE)
	{
		return result;
	}

	currentFrame = afSeekFrame(data->filehandle, AF_DEFAULT_TRACK,
		frame * FRAME_COUNT / data->frameSize);

	result = (currentFrame >= 0);

#ifdef DEBUG
	printf("audiofile_frame_seek: result %d\n", result);
#endif

	return result;
}


static int audiofile_frame_size (input_object *obj)
{
	return FRAME_COUNT;
}


static int audiofile_nr_frames (input_object *obj)
{
	if (!obj)
		return 0;

	return obj->nr_frames;
}


static int audiofile_sample_rate (input_object *obj)
{
	struct af_local_data *data;

	if (!obj)
		return 0;

	data = (struct af_local_data *) obj->local_data;

	return data->sampleRate;
}


static int audiofile_channels (input_object *obj)
{
	if (!obj)
		return 0;

	return obj->nr_channels;
}


static unsigned long audiofile_frame_to_sec (input_object *obj, int frame)
{
	unsigned long	result = 0;

	struct af_local_data	*data;

#ifdef DEBUG
	printf("audiofile_frame_to_sec: %d\n", frame);
#endif

	if (!obj)
		return result;

	data = (struct af_local_data *) obj->local_data;

	if (!data)
		return result;
	
	result = (unsigned long) (frame * FRAME_COUNT /
		(data->frameSize * data->sampleRate / 100));

#ifdef DEBUG
	printf("audiofile_frame_to_sec: result %d\n", result);
#endif

	return result;
}

static float audiofile_test_support (const char *name)
{
	AFfilehandle	file;

	const char *fname = strrchr(name, '/');

	if (!fname)
		fname = name;
	if (strstr(fname, ".wav") ||
		strstr(fname, ".WAV") ||
		strstr(fname, ".au") ||
		strstr(fname, ".AU") ||
		strstr(fname, ".aiff") ||
		strstr(fname, ".AIFF")) {
			file = afOpenFile(name, "r", NULL);
			if (file == AF_NULL_FILEHANDLE)
				return 0.0;
			afCloseFile(file);
			return 1.0;
	}
	return 0.0;
}

static int audiofile_stream_info (input_object *obj, stream_info *info)
{
	int				sampleWidth;
	double			sampleRate;
	char			*fileType;
	struct af_local_data	*data;

	if (!obj || !info)
			return 0;

	data = (struct af_local_data *) obj->local_data;

	if (data->filehandle == AF_NULL_FILEHANDLE)
	{
			return 0;
	}

	afGetSampleFormat(data->filehandle, AF_DEFAULT_TRACK, NULL, &sampleWidth);
	sampleRate = afGetRate(data->filehandle, AF_DEFAULT_TRACK);
	fileType = (char *) afQueryPointer(AF_QUERYTYPE_FILEFMT, AF_QUERY_NAME,
		afGetFileFormat(data->filehandle, NULL), 0, 0);

	sprintf(info->stream_type, "%d-bit %dkHz %s %s",
		sampleWidth,
		(int) (sampleRate / 1000),
		obj->nr_channels == 2 ? "stereo" : "mono",
		fileType);

	strcpy(info->author, "");
	strcpy(info->title, data->filename);

	return 1;
}

static int audiofile_init ()
{
	return 1;
}

input_plugin audiofile_plugin =
{
	INPUT_PLUGIN_VERSION,
	0,
	{ "Audio File Library player v0.2.1" },	// Plugin name
	{ "Michael Pruett" },	// Author name
	audiofile_init,
	NULL,
	NULL,
	audiofile_test_support,
	audiofile_open,
	audiofile_close,
	audiofile_play_frame,
	audiofile_frame_seek,
	audiofile_frame_size,
	audiofile_nr_frames,
	audiofile_frame_to_sec,
	audiofile_sample_rate,
	audiofile_channels,
	audiofile_stream_info,
	NULL,
	NULL
};

input_plugin *input_plugin_info (void)
{
	return &audiofile_plugin;
}