// YTS
/*  CorePlayer.cpp - Core player object, most of the hacking is done here!  
 *	Copyright (C) 1998 Andy Lo A Foe <andy@alsa-project.org>
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


/*
	Issues:
		- None at the moment

*/

#include "CorePlayer.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/errno.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <sched.h>
#include <pthread.h>
#include <signal.h>
#include <math.h>
#include "Effects.h"
#include "utilities.h"

#define MAX_FRAGS	16
#define LOW_FRAGS	4	

extern void exit_sighandler(int);
static char addon_dir[1024];

// ----------------------------- AMP specific

//int CorePlayer::plugin_count;
//input_plugin CorePlayer::plugins[32];

//bool streamer_func(void *arg, void *data, int size);

bool surround_func(void *arg, void *data, int size)
{
	int amount = 50;
	int16_t *d = (int16_t *)data;
	for (int i=0; i < size; i+=2) {
		int16_t l =  d[i]; // Left
		int16_t r =  d[i+1]; // Right
		d[i] =  ((l*(256-amount))-(r*amount))/256;
		d[i+1] = ((r*(256-amount))-(l*amount))/256;
	}
	return true;
}

void CorePlayer::load_input_addons()
{
	char path[1024];
	DIR *dir;
	struct stat buf;
	dirent *entry;
	
	input_plugin_info_type input_plugin_info;

	sprintf(path, "%s/input", addon_dir);

	dir = opendir(path);

	if (dir) {
		while ((entry = readdir(dir)) != NULL) {
			if (strcmp(entry->d_name, ".") == 0 ||
			    strcmp(entry->d_name, "..") == 0) {
					continue;
			}
			sprintf(path, "%s/input/%s", addon_dir, entry->d_name);
			if (stat(path, &buf)) continue;
			if (S_ISREG(buf.st_mode)) {
				void *handle;

				char *ext = strrchr(path, '.');

				if (!ext)
					continue;
				
				ext++;

				if (strcasecmp(ext, "so"))
					continue;

				if ((handle = dlopen(path, RTLD_NOW |RTLD_GLOBAL))) {
					input_plugin_info = (input_plugin_info_type)
						dlsym(handle, "input_plugin_info");

					if (input_plugin_info) {
#ifdef DEBUG
						fprintf(stderr, "Loading input plugin: %s\n", path);
#endif
						input_plugin *the_plugin = input_plugin_info();
						if (the_plugin) {
							the_plugin->handle = handle;
						}
						if (!RegisterPlugin(the_plugin)) {
							fprintf(stderr, "Error loading %s\n", path);
							dlclose(handle);
						}
					} else {
#ifdef DEBUG
						fprintf(stderr, "Could not find symbol in shared object `%s'\n", path);
#endif
						dlclose(handle);
					}	
				} else {
					printf("%s\n", dlerror());
				}
			}	
		}
		closedir(dir);
	}
}


CorePlayer::CorePlayer(AlsaNode *the_node)
{
	int i;
	file_path[0] = 0;
	producer_thread = 0; 
	total_frames = 0;
	streaming = false;
	producing = false;
	plugin_count = 0;
	plugin = NULL;
	jumped = jump_point = repitched = write_buf_changed = 0; 
	new_frame_number = 0;
	read_direction = DIR_FORWARD;
	node = the_node;
	sub = NULL;
	last_read = -1;
	SetSpeedMulti(1.0);
	SetSpeed(1.0);
	SetVolume(100);
	SetPan(0);
	buffer = NULL;
	the_object = NULL;

	if ((buffer = new sample_buf[NR_BUF]) == NULL) {
		printf("Out of memory in CorePlayer::CorePlayer\n");
		exit(1);
	}

	for (i=0; i < NR_BUF; i++) {
		buffer[i].buf = new SampleBuffer(SAMPLE_STEREO, BUF_SIZE);
		// Set up next/prev pointers
		if (i > 0) {
			buffer[i].prev = &buffer[i-1];
			buffer[i-1].next = &buffer[i];
		}
		buffer[i].start = -1;
	}
	// Connect head and tail
	buffer[0].prev = &buffer[NR_BUF-1];
	buffer[NR_BUF-1].next = &buffer[0];

	memset(plugins, 0, sizeof(plugins));

	read_buf = write_buf = buffer;
	pthread_mutex_init(&counter_mutex, NULL); 
	pthread_mutex_init(&player_mutex, NULL);

	the_object = new input_object;
#ifndef NEW_PLAY	
	the_object->write_buffer = NULL;
#endif

	pthread_mutex_init(&the_object->object_mutex, NULL);
	test_sub = new AlsaSubscriber();
	test_sub->Subscribe(node);
	strcpy(addon_dir, ADDON_DIR);

	// Load the input addons
	load_input_addons();
}



void CorePlayer::ResetBuffer()
{
	for (int i=0; i < NR_BUF; i++) {
		buffer[i].start = -1;
		buffer[i].buf->Clear();
	}
}


CorePlayer::~CorePlayer()
{
	input_plugin *tmp;
	int i;

	UnregisterPlugins();

	for (i=0; i < NR_BUF; i++) {
		delete buffer[i].buf;
	}
	pthread_mutex_destroy(&counter_mutex);
	pthread_mutex_destroy(&player_mutex);
	pthread_mutex_destroy(&the_object->object_mutex);
	delete test_sub;
	delete []buffer;
	delete the_object;
}


void CorePlayer::UnregisterPlugins()
{
	input_plugin *tmp;
	
	Stop();

	pthread_mutex_lock(&player_mutex);
	for (int i = 0; i < plugin_count; i++) {
		tmp = &plugins[i];
		printf("Unregistering %s\n", tmp->name); 
		if (tmp->handle)
			dlclose(tmp->handle);
	}		
	pthread_mutex_unlock(&player_mutex);
}

int CorePlayer::RegisterPlugin(input_plugin *the_plugin)
{
	int version;
	input_plugin *tmp;
	
	pthread_mutex_lock(&player_mutex);	
	tmp = &plugins[plugin_count];
	tmp->version = the_plugin->version;
	if (tmp->version) {
		if ((version = tmp->version) != INPUT_PLUGIN_VERSION) {
			fprintf(stderr, "Wrong version number on plugin v%d, wanted v%d\n",
					version - INPUT_PLUGIN_BASE_VERSION,
					INPUT_PLUGIN_VERSION - INPUT_PLUGIN_BASE_VERSION);      
			pthread_mutex_unlock(&player_mutex);
			return 0;
		}
	}
	strncpy(tmp->name, the_plugin->name, 256);
	strncpy(tmp->author, the_plugin->author, 256);
	tmp->init = the_plugin->init;
	tmp->can_handle = the_plugin->can_handle;
	tmp->open = the_plugin->open;
	tmp->close = the_plugin->close;
	tmp->play_frame = the_plugin->play_frame;
	tmp->frame_seek = the_plugin->frame_seek;
	tmp->frame_size = the_plugin->frame_size;
	tmp->nr_frames = the_plugin->nr_frames;
	tmp->frame_to_sec = the_plugin->frame_to_sec;
	tmp->sample_rate = the_plugin->sample_rate;
	tmp->channels = the_plugin->channels;
	tmp->stream_info = the_plugin->stream_info;
	tmp->cleanup = the_plugin->cleanup;
	tmp->nr_tracks = the_plugin->nr_tracks;
	tmp->track_seek = the_plugin->track_seek;
	plugin_count++;
	if (plugin_count == 1) { // First so assign plugin
		plugin = tmp;
	}
	fprintf(stdout, "Input plugin: %s\n", tmp->name);
	pthread_mutex_unlock(&player_mutex);
	return 1;
}


unsigned long CorePlayer::GetCurrentTime(int frame)
{
	unsigned long result = 0;

	pthread_mutex_lock(&player_mutex);
	if (plugin && the_object) {
		result = plugin->frame_to_sec(the_object, frame < 0 ? GetPosition()
			: frame);
	}
	pthread_mutex_unlock(&player_mutex);

	return result;
}


int CorePlayer::GetPosition()
{
	if (jumped)	// HACK!
		return jump_point;
	if (read_buf && plugin && the_object) {
		if (read_buf->start < 0)
			return 0;		

		int frame_size = plugin->frame_size(the_object);
		if (frame_size) 
			return (read_buf->start + (read_buf->buf->GetReadIndex() * 4) / frame_size);
		else
			return 0;
	} else {
		return 0;
	}
}


int CorePlayer::GetFrames()
{
	int result = 0;

	pthread_mutex_lock(&player_mutex);
	if (plugin && the_object)
		result = plugin->nr_frames(the_object);
	pthread_mutex_unlock(&player_mutex);

	return result;
}


int CorePlayer::GetSampleRate()
{
	int result = 0;

	pthread_mutex_lock(&player_mutex);
	if (plugin && the_object)
		result = plugin->sample_rate(the_object);
	pthread_mutex_unlock(&player_mutex);

	return result;
}	


int CorePlayer::GetStreamInfo(stream_info *info)
{
	int result = 0;

	pthread_mutex_lock(&player_mutex);
	if (plugin && plugin->stream_info && info && the_object) {
		result = plugin->stream_info(the_object, info);
	}
	pthread_mutex_unlock(&player_mutex);

	return result;		
}


// This is the first function to get documented. Believe it or not
// it was the most difficult to get right! We absolutely NEED to have
// a known state of all the threads of our object! 

bool CorePlayer::Start(int reset)
{
	float result;
	int output_rate;
	int tries;

	pthread_mutex_lock(&player_mutex);
	// First check if we have a filename to play
	if (!Open()) {
		printf("\nOpen() has failed......\n");
		pthread_mutex_unlock(&player_mutex);
		return false;
	}
	producing = true; // So Read32() doesn't generate an error
	streaming = true;

	ResetBuffer();
	
	last_read = -1;
	output_rate = node->SamplingRate();
	
	result = plugin->sample_rate(the_object);
	if (result == output_rate)
		SetSpeedMulti(1.0);
	else
		SetSpeedMulti((float) result / (float) output_rate);
	update_pitch();
	write_buf_changed = 0;
	read_buf = write_buf;

	result = GetSpeed();

	if (result < 0.0) {
		int start_frame = plugin->nr_frames(the_object) - 16;
		write_buf->start = start_frame > 0 ? start_frame : 0;
		SetDirection(DIR_BACK);
	} else {
		write_buf->start = 0;
		SetDirection(DIR_FORWARD);
	}

	pthread_create(&producer_thread, NULL,
		(void * (*)(void *))producer_func, this);
	//pthread_detach(producer_thread);

	// Wait for up to 5 seconds
	tries = 500;
	while (--tries && !AvailableBuffers()) { 
		//printf("Waiting for buffers...\n");
		dosleep(10000);
	}

	sub = new AlsaSubscriber();
	if (!sub) {
		printf("Subscriber creation failed :-(\n");
		return false;
	}	
	sub->Subscribe(node);
	//printf("Starting streamer thread...\n");
	sub->EnterStream(streamer_func, this);
	pthread_mutex_unlock(&player_mutex);
	return true;
}  


void CorePlayer::Stop(int streamer)
{
	pthread_mutex_lock(&player_mutex);
	if (sub) {
        	delete sub;
        	sub = NULL;
	}
	producing = false;
	streaming = false;
	pthread_mutex_unlock(&counter_mutex); // Unblock if needed
#ifdef DEBUG	
	printf("Waiting for producer_thread to finish...\n");
#endif
	pthread_cancel(producer_thread);
	pthread_join(producer_thread, NULL); 
	pthread_mutex_destroy(&counter_mutex);
	pthread_mutex_init(&counter_mutex, NULL);
	producer_thread = 0;
#ifdef DEBUG	
	printf("producer_func is gone now...\n");
#endif
	ResetBuffer();
	Close();
	pthread_mutex_unlock(&player_mutex);
}


int CorePlayer::SetSpeed(float val)
{
	pitch_point = val;
	repitched = 1;

	return 0;
}

float CorePlayer::GetSpeed()
{
	return (read_direction == DIR_FORWARD ? pitch : -pitch);
}


int CorePlayer::Seek(int index)
{
	jump_point = index;
	jumped = 1;
	return 0;
}


int CorePlayer::FrameSeek(int index)
{
	if (plugin && the_object)
		index = plugin->frame_seek(the_object, index);
	else 
		index = -1;
	return index;
}


bool CorePlayer::PlayFile(const char *path)
{
	Stop();
	SetFile(path);
	return Start();
}

void CorePlayer::Close()
{
	if (plugin && the_object)
		plugin->close(the_object);
}


void CorePlayer::SetFile(const char *path)
{
	pthread_mutex_lock(&player_mutex);
	strncpy(file_path, path, 1024);
	pthread_mutex_unlock(&player_mutex);
}

// Find best plugin to play a file
input_plugin *
CorePlayer::GetPlayer(const char *path)
{
	// Check we've got a path
	if (!strlen(path)) {
		return NULL;
	}

	float best_support = 0.0;
	input_plugin *best_plugin = NULL;

	// Go through plugins, asking them
	for (int i = 0; i < plugin_count; i++) {
		input_plugin *p = plugins + i;
		float sl = p->can_handle(path);
		if (sl > best_support) {
			best_support = sl;
			best_plugin = p;
			if (sl == 1.0) break;
		}
	}

	// Return best plugin, if there is one
	if(best_support <= 0.0) return NULL;
	return best_plugin;
}

bool CorePlayer::Open()
{
	bool result = false;
	input_plugin *best_plugin = GetPlayer(file_path);

	if (best_plugin == NULL) {
		printf("No suitable plugin found for %s\n", file_path);
		return false;
	}

	plugin = best_plugin;
	
	if (plugin->open(the_object, file_path)) {
		result = true;
		frames_in_buffer = read_buf->buf->GetBufferSizeBytes(plugin->frame_size(the_object)) / plugin->frame_size(the_object);
	} else {
		result = false;
		frames_in_buffer = 0;
	}	

	return result;	
}


void print_buf(sample_buf *start)
{
	sample_buf *c = start;
	for (int i = 0; i < NR_BUF; i++, c = c->next) {
		printf("%d ", c->start);
	}
	printf("\n");	
}

int CorePlayer::SetDirection(int direction)
{
	sample_buf *tmp_buf;
	
	int buffers = 0;
	if (read_direction != direction) {
		tmp_buf = read_buf;
		switch(direction) {
		 case DIR_FORWARD:
			while (buffers < NR_BUF-1 && tmp_buf->next->start ==
			       (tmp_buf->start + frames_in_buffer)) {
				buffers++;
				tmp_buf = tmp_buf->next;
			}
			break;
	 	  case DIR_BACK:
			while (buffers < NR_BUF-1 && tmp_buf->prev->start ==
			       (tmp_buf->start - frames_in_buffer)) {
				buffers++;
				tmp_buf = tmp_buf->prev;
			}
		}
		new_write_buf = tmp_buf;
		new_frame_number = new_write_buf->start;
		write_buf_changed = 1;
		read_direction = direction;
		pthread_mutex_unlock(&counter_mutex);		
	} else {
	}
	return 0;	
	
}


int CorePlayer::AvailableBuffers()
{
	int result = 0;
	sample_buf *tmp = read_buf;

	if (read_buf == write_buf) {
		return 0;	
	}
	switch (read_direction) {
	 case DIR_FORWARD:
		while (tmp->next != write_buf) {
			tmp = tmp->next;
			result++;
		}
		break;
	 case DIR_BACK:
		while (tmp->prev != write_buf) {
			tmp = tmp->prev;
			result++;
		}
		break;
	}
	return result; // We can't use a buffer that is potentially being written to
			 // so a value of 1 should still return no buffers
}


void CorePlayer::update_pitch()
{
	if (pitch_point < 0) {
		SetDirection(DIR_BACK);
		pitch = -pitch_point;
	} else {
		SetDirection(DIR_FORWARD);
		pitch = pitch_point;
	}
	pitch *= pitch_multi;
	
	repitched = 0;
}	


int CorePlayer::Read32(void *data, int size)
{
	if (repitched) {
		update_pitch();
	}
	
	if (pitch == 0.0 || (read_buf == write_buf)) {
		if (write_buf->next->start == -2 || !producing) {
			return -2;
		}	
		memset(data, 0, size * 4);
		return size;
	}
	int use_read_direction = read_direction;	
	int *out_buf = (int *)data;
	int *in_buf = (int *)read_buf->buf->GetSamples();
	in_buf += read_buf->buf->GetReadIndex();
	int buf_index = 0;
	int tmp_index = 0;
	int check_index = read_buf->buf->GetReadIndex();
	int base_corr = 0;
	float use_pitch = pitch;
	
	if (jumped) {
		int i;
		jumped = 0;
		new_write_buf = read_buf;
		// ---- Testing area ----
		sample_buf *sb;
		for (i=0, sb = buffer; i < NR_BUF; i++, sb = sb->next) {
			sb->start = -1;
			sb->buf->Clear();
		}
		// ---- Testing area ----
		new_write_buf->start = jump_point;
		new_frame_number = jump_point;
		write_buf_changed = 1;
		pthread_mutex_unlock(&counter_mutex);
	}
	
	if (use_read_direction == DIR_FORWARD) {
		while (buf_index < size) {
			tmp_index = (int) ((float)use_pitch*(float)(buf_index-base_corr));
			if ((check_index + tmp_index) > (read_buf->buf->GetSamplesInBuffer())-1) {
				if (read_buf->next->start < 0 ||
					read_buf->next == write_buf) {
					if (read_buf->next->start == -2) {
						//printf("Next in queue (2)\n");
						return -2;
					}	
					memset(data, 0, size * 4);
					if (!IsPlaying()) {
						//printf("blah 1\n");
						return -1;
					}
					return size;
				} else if (read_buf->next->start !=
					read_buf->start + frames_in_buffer) {
					printf("------ WTF!!? %d - %d\n",
					read_buf->next->start,
					read_buf->start);
				}
				read_buf = read_buf->next;
				pthread_mutex_unlock(&counter_mutex);
				read_buf->buf->SetReadDirection(DIR_FORWARD);
				read_buf->buf->ResetRead();
				in_buf = (int *)read_buf->buf->GetSamples();
				base_corr = buf_index;
				check_index = 0;
				tmp_index = (int)((float)use_pitch*(float)(buf_index-base_corr)); // Recalc
			}
			out_buf[buf_index++] =  *(in_buf + tmp_index);
		}
	} else { // DIR_BACK
		while (buf_index < size) { // Read (size) amount of samples
			tmp_index = (int)((float)use_pitch*(float)(buf_index-base_corr));
			if ((check_index - tmp_index) < 0) { 
				if (read_buf->prev->start < 0 ||
					read_buf->prev == write_buf) {
				//	printf("Back (%d %d) ", AvailableBuffers(), read_buf->prev->start);
				//	print_buf(read_buf->prev);
					if (!IsPlaying()) {
						//printf("blah 2\n");
						return -1;
					}
					memset(data, 0, size * 4);
					return size;
				}
				read_buf = read_buf->prev;
				pthread_mutex_unlock(&counter_mutex);
				read_buf->buf->SetReadDirection(DIR_BACK);
				read_buf->buf->ResetRead();
				int buf_size = read_buf->buf->GetSamplesInBuffer();
				in_buf = (int *)read_buf->buf->GetSamples();
				in_buf += (buf_size + (check_index - tmp_index));
				base_corr = buf_index;
				check_index = buf_size-1;
				tmp_index = (int)((float)use_pitch*(float)(buf_index-base_corr)); // Recalc
			}
			out_buf[buf_index++] = *(in_buf - tmp_index);
		}
	}
	read_buf->buf->Seek(check_index + ((use_read_direction == DIR_FORWARD) ?
		 tmp_index+1 : -((tmp_index+1) > check_index ? check_index : tmp_index+1)));
	if (!size) printf("Humm, I copied nothing?\n");
	return size;
}

extern int thack;

int CorePlayer::pcm_worker(sample_buf *dest, int start, int len)
{
	int frames_read = 0;
	int bytes_written = 0;
	char *sample_buffer;

	if (dest && the_object) {
		int count = dest->buf->GetBufferSizeBytes(plugin->frame_size(the_object));
		dest->buf->Clear();
		if (last_read == start) {
		} else {
			FrameSeek(start);
		}
		if (start < 0) {
			return -1;
		}
#ifndef NEW_PLAY		
		the_object->write_buffer = dest->buf;
#endif		
		sample_buffer = (char *)dest->buf->GetSamples();
		while (count > 0) {
			if (!plugin->play_frame(the_object, sample_buffer + bytes_written)) {
				dest->start = start;
#ifdef DEBUG
				printf("frames read = %d\n", frames_read);
#endif
				return frames_read;
			} else {
				bytes_written += plugin->frame_size(the_object); // OPTIMIZE!
				frames_read++;
			}
			count -= plugin->frame_size(the_object);
		}
		dest->start = start;
		last_read = dest->start + frames_read;
		dest->buf->SetSamples(bytes_written >> 2);
		return frames_read;
	} else {
		return -1;
	}		
}


void CorePlayer::producer_func(void *data)
{
	CorePlayer *obj = (CorePlayer *)data;
	int frames_read;

	signal(SIGINT, exit_sighandler);
#ifndef NEW_PLAY
	obj->the_object->write_buffer = NULL;
#endif	
#ifdef DEBUG
	printf("Starting new producer_func...\n");
#endif	
	while (obj->producing) {
		if (obj->write_buf_changed) {
			obj->write_buf_changed = 0;
			obj->write_buf = obj->new_write_buf;
			obj->write_buf->start = obj->new_frame_number;
		}
		int val = obj->AvailableBuffers();	
		if (val < (NR_CBUF-1)) {
			switch (obj->read_direction) {
			 case DIR_FORWARD:
				frames_read = obj->pcm_worker(obj->write_buf, obj->write_buf->start);
				if (frames_read != obj->frames_in_buffer) {
					obj->producing = false;
					obj->write_buf->next->start = -2;
				}
				obj->write_buf = obj->write_buf->next;
				obj->write_buf->start = obj->write_buf->prev->start + obj->frames_in_buffer;
				break;
			 case DIR_BACK:
				frames_read = obj->pcm_worker(obj->write_buf, obj->write_buf->start);
				if (frames_read != obj->frames_in_buffer) {
					if (obj->write_buf->start >= 0)
						printf("an ERROR occured or EOF (%d)\n",
							obj->write_buf->start);
					obj->write_buf->prev->start = -2;
					obj->producing = false;
				}
				obj->write_buf = obj->write_buf->prev;
				obj->write_buf->start = obj->write_buf->next->start - 
					obj->frames_in_buffer;
				break;
			}
		} else {
#if 0
			printf("producer: going to wait for a free buffer\n");
#endif
			pthread_mutex_lock(&obj->counter_mutex);
#if 0
			printf("producer: unblocked\n");
#endif
		}	
	}
#ifdef DEBUG	
	printf("Exited producer_func (producing = %d)\n", obj->producing);
#endif
	return;
}
extern int global_bal;
extern int global_vol_left;
extern int global_vol_right;
extern int global_vol;
extern int global_pitch;
extern int global_reverb_on;
extern int global_reverb_delay;
extern int global_reverb_feedback;

bool CorePlayer::streamer_func(void *arg, void *data, int size)
{
	CorePlayer *obj = (CorePlayer *)arg;
	int count;
	char input_buffer[16384]; // Should be big enough
	//static int tracer = 0;
	//printf("Entering streamer_func...%6d\r", tracer++);
	//fflush(stdout);
	if ((count = obj->Read32(input_buffer, size >> 2)) >= 0) {
		//if (global_reverb_feedback) {
    //                    echo_effect32(data , size,
    //                            global_reverb_delay, global_reverb_feedback);
		//}
		//if (global_vol != 100 || global_bal != 100) {
		//		int left, right;
		//		left = global_vol_left * global_vol;
		//		left /= 100;
		//		right = global_vol_right * global_vol;
		//		right /= 100;
		//		volume_effect32(data, count, left, right);
		//}
		int v, p, left, right;
		p = obj->GetPan();
		v = obj->GetVolume();
		if (v != 100 || p != 0) {
			if (p == 0) {
				left = right = 100;
			} else 	if (p > 0) {
				right = 100;
				left = 100 - p;
			} else {
				left = 100;
				right = 100 + p;
			}
			if (v != 100) {
				left *= v;
				left /= 100;
				right *= v;
				right /= 100;
			}
			//printf("v = %d, p = %d, left = %d, right = %d\n",
			//	p, v, left, right);
			volume_effect32(input_buffer, count, left, right);
		}	
		// Now add the data to the current stream
		int16_t *in_buffer;
		int16_t *out_buffer;
		int32_t level;
		in_buffer = (int16_t *)input_buffer;
		out_buffer = (int16_t *)data;
		for (int i=0; i < size >> 1; i++) {
				level = *(in_buffer++) + *out_buffer;
				if (level > 32767) level = 32767;
				else if (level < -32768) level = -32768;
				*(out_buffer++) = (int16_t) level;
		}		
		return true;
	} else {
		//printf("Exiting from streamer_func...\n");
		obj->streaming = false;
		return false;
	}	
}