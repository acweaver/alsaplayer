/*
 *  gtk.cpp - GTK interface plugin main file
 *  Copyright (C) 2001 Andy Lo A Foe <andy@alsaplayer.org>
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

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <math.h>
#include <glib.h>

#include "config.h"

#include "SampleBuffer.h"
#include "CorePlayer.h"
#include "Playlist.h"
#include "ScopesWindow.h"
#include "gtk_interface.h"
#include "utilities.h"
#include "interface_plugin.h"

static char addon_dir[1024];


void unload_scope_addons()
{
	apUnregiserScopePlugins();
	// No unloading of shared libs is done yet
}

void load_scope_addons()
{
	char path[1024];
	struct stat buf;

	scope_plugin_info_type scope_plugin_info;

	sprintf(path, "%s/scopes", addon_dir);

	DIR *dir = opendir(path);
	dirent *entry;

	if (dir) {
		while ((entry = readdir(dir)) != NULL) { // For each file in scopes
			if (strcmp(entry->d_name, ".") == 0 ||
				strcmp(entry->d_name, "..") == 0) {
				continue;
			}
			sprintf(path, "%s/scopes/%s", addon_dir, entry->d_name);
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
					scope_plugin_info = (scope_plugin_info_type) dlsym(handle, "scope_plugin_info");
					if (scope_plugin_info) { 
#ifdef DEBUG					
						fprintf(stderr, "Loading scope addon: %s\n", path);
#endif
						apRegisterScopePlugin(scope_plugin_info());
					} else {
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

int interface_gtk_init()
{
	strcpy(addon_dir, ADDON_DIR);
	return 1;
}


int interface_gtk_running()
{
	return 1;
}


int interface_gtk_stop()
{
	return 1;
}

void interface_gtk_close()
{
	return;
}


int interface_gtk_start(CorePlayer *coreplayer, Playlist *playlist, int argc, char **argv)
{
	char path[256];
	char *home;

	g_thread_init(NULL);
	if (!g_thread_supported()) {
		fprintf(stderr, "Sorry - this interface requires working threads.\n");
		return 1;
	}
	
	// Scope functions
	AlsaSubscriber *scopes = new AlsaSubscriber();
	scopes->Subscribe(coreplayer->GetNode());
	scopes->EnterStream(scope_feeder_func, coreplayer);
	
	gtk_set_locale();
	gtk_init(&argc, &argv);
	gdk_rgb_init();

	home = getenv("HOME");
	if (home && strlen(home) < 128) {
		sprintf(path, "%s/.aprc", home);
		gtk_rc_parse(path);
	} else {
		sprintf(path, "%s/.gtkrc", home);
		gtk_rc_parse(path);
	}

	playlist->UnPause(); // Make sure playlist is active
	init_main_window(coreplayer, playlist);
	
	// Do something whacky here

	//CorePlayer *third = new CorePlayer(coreplayer->GetNode());
	//third->PlayFile("/mp3/Andy/the crystal method - bad stone.mp3");

	// Scope addons
	load_scope_addons();
	GDK_THREADS_ENTER();
	gtk_main();
	GDK_THREADS_LEAVE();
	unload_scope_addons();

	return 0;
}


interface_plugin default_plugin =
{
	INTERFACE_PLUGIN_VERSION,
	{ "GTK+ interface v1.0" },
	{ "Andy Lo A Foe" },
	interface_gtk_init,
	interface_gtk_start,
	interface_gtk_running,
	interface_gtk_stop,
	interface_gtk_close
};

extern "C" {

interface_plugin *interface_plugin_info()
{
	return &default_plugin;
}

}