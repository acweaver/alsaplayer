/*  PlayList.cpp - playlist window 'n stuff
 *  Copyright (C) 1998 Andy Lo A Foe <andy@alsa-project.org>
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

#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <fstream>
#include <cstdlib>

#include "Playlist.h"
#include "CorePlayer.h"
#include "utilities.h"
/* This is needed for the kludge fix for the Gtk+ deadlock */
#include "config.h"
//#ifdef HAVE_GTK
//#include <gtk/gtk.h>
//#endif

#define READBUFSIZE 1024
#define MAXLOADFAILURES 100
#define MAXRECURSEDEPTH 10

static void additems(std::vector<std::string> *items, std::string path, int depth);

extern void playlist_looper(void *data)
{
#ifdef DEBUG
	printf("THREAD-%d=playlist thread\n", getpid());
#endif /* DEBUG */
	Playlist *pl = (Playlist *)data;
	if(!pl) return;
	CorePlayer *coreplayer = (CorePlayer *)(pl->coreplayer);
	if(!coreplayer) return;

	while(pl->active) {
		if (!coreplayer->IsActive()) {
			if (!pl->Paused() && pl->Length())
				pl->Next();
		}
		dosleep(50000);
	}
}

class PlInsertItems {
	public:
		Playlist *playlist;
		std::vector<std::string> items;
		unsigned position;

		PlInsertItems(Playlist *pl) {
			playlist = pl;
		}
};

// Thread which performs an insert to playlist
void insert_thread(void *data) {
#ifdef DEBUG
	printf("THREAD-%d=insert thread\n", getpid());
#endif /* DEBUG */
	PlInsertItems * items = (PlInsertItems *)data;
	Playlist *playlist = items->playlist;

    // First vetting of the list, and recurse through directories
	std::vector<std::string> vetted_items;
	std::vector<std::string>::const_iterator i = items->items.begin();
	while(i != items->items.end()) {
		additems(&(vetted_items), *i++, MAXRECURSEDEPTH);
	}

	std::vector<PlayItem> newitems;
	if(vetted_items.size() > 0) {
		std::vector<std::string>::const_iterator path;
		// Check items for adding to list
		for(path = vetted_items.begin(); path != vetted_items.end(); path++) {
			// Check that item is valid
			if(!playlist->CanPlay(*path)) {
#ifdef DEBUG
				printf("Can't find a player for `%s'\n", path->c_str());
#endif /* DEBUG */
			} else {
				newitems.push_back(PlayItem(*path));
			}
		}
	}

	// Stop the list being changed while we add these items
	pthread_mutex_lock(&(playlist->playlist_mutex));

	// Check position is valid
	if(playlist->queue.size() < items->position) {
		items->position = playlist->queue.size();
	}
	// Add to list
	playlist->queue.insert(playlist->queue.begin() + items->position,
						   newitems.begin(),
						   newitems.end());

	if(playlist->curritem > items->position)
		playlist->curritem += newitems.size();

#ifdef DEBUG
	printf("Curritem = %d, Size = %d\n", playlist->curritem, playlist->queue.size());
#endif /* DEBUG */

	// Tell the subscribing interfaces about the changes
	if(playlist->interfaces.size() > 0) {
		std::set<PlaylistInterface *>::const_iterator i;
		for(i = playlist->interfaces.begin();
			i != playlist->interfaces.end(); i++) {
			(*i)->CbInsert(newitems, items->position);
			(*i)->CbSetCurrent(playlist->curritem);
		}
	}

	// Free the list again
	pthread_mutex_unlock(&(playlist->playlist_mutex));
	delete items;
}


// Playlist class

Playlist::Playlist(CorePlayer *p_new) {
	coreplayer = p_new;
	curritem = 0;
	
	active = true;
	pthread_mutex_init(&playlist_mutex, NULL);
	pthread_create(&playlist_thread, NULL,
				   (void * (*)(void *))playlist_looper, this);
}

Playlist::~Playlist() {
	// Nasty hack to stop other thread (should use pthread_cancel)
	// but we're going to replace this anyway
	active = false;
	pthread_join(playlist_thread, NULL);
	pthread_mutex_destroy(&playlist_mutex);

	// FIXME - need to do something to kill off an insert thread, if one is
	// running - otherwise it might have its playlist torn from underneath it
}

// Return number of items in playlist
unsigned Playlist::Length() {
	return queue.size();
}

// Request to move to specified item
void Playlist::Play(unsigned item) {
	pthread_mutex_lock(&playlist_mutex);
	if(item < 1) item = 1;
	if(item <= queue.size()) {
		curritem = item;
		PlayFile(queue[curritem - 1]);
	} else {
		curritem = queue.size();
		Stop();
	}
#ifdef DEBUG
	printf("Curritem = %d, Size = %d\n", curritem, queue.size());
#endif /* DEBUG */

	// Tell the subscribing interfaces about the change
	if(interfaces.size() > 0) {
		std::set<PlaylistInterface *>::const_iterator i;
		for(i = interfaces.begin(); i != interfaces.end(); i++) {
			(*i)->CbSetCurrent(curritem);
		}
	}
	pthread_mutex_unlock(&playlist_mutex);
}

// Request to move to next item
void Playlist::Next() {
	pthread_mutex_lock(&playlist_mutex);
	unsigned olditem = curritem;
	if(queue.size() > 0) {
		if(curritem < queue.size()) {
			curritem++;
			PlayFile(queue[curritem - 1]);
		}
	}

	// Tell the subscribing interfaces about the change
	if(curritem != olditem) {
#ifdef DEBUG
		printf("Curritem = %d, Size = %d\n", curritem, queue.size());
#endif /* DEBUG */
		if(interfaces.size() > 0) {
			std::set<PlaylistInterface *>::const_iterator i;
			for(i = interfaces.begin(); i != interfaces.end(); i++) {
				(*i)->CbSetCurrent(curritem);
			}
		}
	}
	pthread_mutex_unlock(&playlist_mutex);
}

// Request to move to previous item
void Playlist::Prev() {
	pthread_mutex_lock(&playlist_mutex);
	unsigned olditem = curritem;
	if(curritem > queue.size()) {
		curritem = queue.size();
	}
	if(curritem > 1) {
		curritem--;
	}
	if(curritem != 0) {
		PlayFile(queue[curritem - 1]);
#ifdef DEBUG
		printf("Curritem = %d, Size = %d\n", curritem, queue.size());
#endif /* DEBUG */
	}

	// Tell the subscribing interfaces about the change
	if(curritem != olditem) {
		if(interfaces.size() > 0) {
			std::set<PlaylistInterface *>::const_iterator i;
			for(i = interfaces.begin(); i != interfaces.end(); i++) {
				(*i)->CbSetCurrent(curritem);
			}
		}
	}
	pthread_mutex_unlock(&playlist_mutex);
}

// Request to put a new item at end of playlist
void Playlist::Insert(std::vector<std::string> const & paths, unsigned position) {
	// Prepare to do insert
	PlInsertItems * items = new PlInsertItems(this);
	items->position = position;

	// Copy list
	std::vector<std::string>::const_iterator i = paths.begin();
	while(i != paths.end()) {
		items->items.push_back(*i++);
	}

#ifdef DEBUG
	printf("Insert([%d items], %d)\n", items->items.size(), items->position);
#endif

    // Perform request in a sub-thread, so that we don't:
	// a) block the user interface
	// b) risk getting caught in a deadlock when we call the interface to
	//    inform it of the change
	//pthread_t adder;
	//pthread_create(&adder, NULL,
	//			   (void * (*)(void *))insert_thread, (void *)items);

	insert_thread(items);
}

// Add some items start them playing
void Playlist::AddAndPlay(std::vector<std::string> const &paths) {
	// There is a possible concurrency problem here, if we're trying
	// to insert items into the playlist at the same time as this is
	// called - the other new items could get inserted after the Play()
	// call, but before our items, causing the wrong ones to be played
	// However, this is sufficiently unlikely in practice, and fiddly
	// to fix, (and relatively harmless) that we can ignore it.

	// Move current play point to off end of list (stops play)
	Play(Length() + 1);

	// Now add the new items
	Insert(paths, Length());
}

// Remove tracks from position start to end inclusive
void Playlist::Remove(unsigned start, unsigned end) {
	if(start > end) {
		unsigned tmp = end;
		end = start;
		start = tmp;
	}
	if(start < 1) start = 1;
	if(start > queue.size()) start = queue.size();
	if(end < 1) end = 1;
	if(end > queue.size()) end = queue.size();

	pthread_mutex_lock(&playlist_mutex);

	queue.erase(queue.begin() + start - 1, queue.begin() + end);

	if(curritem > start) {
		if(curritem > end) {
			curritem -= (end + 1 - start);
		} else {
			curritem = start;
		}
	}

#ifdef DEBUG
	printf("Curritem = %d, Size = %d\n", curritem, queue.size());
#endif /* DEBUG */

	// Tell the subscribing interfaces about the change
	if(interfaces.size() > 0) {
		std::set<PlaylistInterface *>::const_iterator i;
		for(i = interfaces.begin(); i != interfaces.end(); i++) {
			(*i)->CbRemove(start, end);
			(*i)->CbSetCurrent(curritem);
		}
	}
	pthread_mutex_unlock(&playlist_mutex);
}


// Randomize playlist
void Playlist::Shuffle() {
	pthread_mutex_lock(&playlist_mutex);

	std::vector<PlayItem> tmp;

	pthread_mutex_unlock(&playlist_mutex);
}	

// Empty playlist
void Playlist::Clear() {
	pthread_mutex_lock(&playlist_mutex);
	queue.clear();
	curritem = 0;
#ifdef DEBUG
	printf("Curritem = %d, Size = %d\n", curritem, queue.size());
#endif /* DEBUG */

	// Tell the subscribing interfaces about the change
	if(interfaces.size() > 0) {
		std::set<PlaylistInterface *>::const_iterator i;
		for(i = interfaces.begin(); i != interfaces.end(); i++) {
			(*i)->CbClear();
		}
	}
	pthread_mutex_unlock(&playlist_mutex);
}

enum plist_result
Playlist::Save(std::string file, enum plist_format format) const
{
	switch(format) {
		case PL_FORMAT_M3U:
			if(file.length() < 4 ||
			   cmp_nocase(file.substr(file.length() - 4), ".m3u")) {
				file += ".m3u";
			}
			//cout << "Saving to " << file << endl;

			std::ofstream out_list(file.c_str());
			if(!out_list) return E_PL_BAD;

			//out_list << MAGIC_ID << endl;
			std::vector<PlayItem>::const_iterator p = queue.begin();
			while(p != queue.end()) {
				out_list << (*p).filename << std::endl;
				p++;
			}
	}
	return E_PL_SUCCESS;
}

// Returns:
// E_PL_SUCCESS on success,
// E_PL_DUBIOUS if file doesn't appear to be in a known format,
// E_PL_BAD if file definitely isn't in a known format.
// If "force" is true, will try to load anyway instead of returning E_PL_DUBIOUS
enum plist_result
Playlist::Load(std::string const &file, unsigned position, bool force)
{
	// Check extension
	if(!force) {
		if(file.length() < 4 ||
		   cmp_nocase(file.substr(file.length() - 4), ".m3u")) {
			return E_PL_DUBIOUS;
		}
	}

	// lstat file - allow regular files, pipes, unix sockets.
	// Don't allow anything else
	// FIXME - implement

	// Try opening file
#ifdef DEBUG
	fprintf(stderr, "Loading from: %s\n", file.c_str());
#endif
	FILE *in_list = fopen(file.c_str(), "r");
	if(!in_list) return E_PL_BAD;
	
	// Directory of m3u file, might need it later
	std::string dir = file;
	std::string::size_type i = dir.rfind('/');
	if(i != std::string::npos) dir.erase(i);
	dir += '/';
	
	// Read the file
	char path[READBUFSIZE + 1];
	std::vector<std::string> newfiles;
	

	// Give up if too many failures (so we don't wait for almost ever if
	// someone tries clicking on an mp3 file...)
	// However, if its just that some of the files don't exist anymore,
	// don't give up.
	unsigned successes = 0;
	unsigned failures = 0;
	while(failures < MAXLOADFAILURES || failures < successes) {

		if(fscanf(in_list,
				  "%" STRINGISE(READBUFSIZE) "[^\n]\n",
				  path) != 1) break;

		if (strchr(path, '\\')) { // DOS style paths, convert
			for (int c=0; path[c] != '\0'; c++) {
				if (path[c] == '\\') path[c] = '/';
			}
			// And make sure there is no '\r' at the end
			char *p;
			if ((p = strrchr(path, '\r')) != NULL)
				*p = '\0';
		}

		std::string newfile;
		if (path[0] == '/') {
			// Absolute path
			newfile = std::string(path);
		} else if(path[0] == '\0') {
			// No path
			failures++;
			continue;
		} else {
			// Relative path
			newfile = dir + std::string(path);
		}

		// See if the file exists, and isn't a directory
		struct stat buf;
		if(lstat(newfile.c_str(), &buf)) {
			failures++;
			continue;
		}
		if (S_ISDIR(buf.st_mode)) {
			failures++;
			continue;
		}

		// Don't allow directories
		newfiles.push_back(newfile);
		successes++;
	}

	// Check if we read whole file OK
	if(ferror(in_list) || !feof(in_list)) {
		fclose(in_list);
		return E_PL_BAD;
	}
	fclose(in_list);

	// Do the insert
	Insert(newfiles, position);
	return E_PL_SUCCESS;
}

void Playlist::Register(PlaylistInterface * pl_if) {
	pthread_mutex_lock(&playlist_mutex);

	interfaces.insert(pl_if);

	// Tell the interfaces about the current state
	pl_if->CbClear();
	if(queue.size()) {
		pl_if->CbInsert(queue, 0);
	}
	pl_if->CbSetCurrent(curritem);

	pthread_mutex_unlock(&playlist_mutex);
#ifdef DEBUG
	printf("Registered new interface\n");
#endif /* DEBUG */
}

void Playlist::UnRegister(PlaylistInterface * pl_if) {
	pthread_mutex_lock(&playlist_mutex);
	interfaces.erase(interfaces.find(pl_if));
	pthread_mutex_unlock(&playlist_mutex);
#ifdef DEBUG
	printf("Unregistered playlist interface\n");
#endif /* DEBUG */
}

void Playlist::Stop() {
#ifdef DEBUG
	printf("Playlist::Stop()\n");
#endif /* DEBUG */
	coreplayer->Stop();
}

void Playlist::PlayFile(PlayItem const & item) {
#ifdef DEBUG
	printf("Playlist::PlayFile(\"%s\")\n", item.filename.c_str());
#endif /* DEBUG */

	coreplayer->Stop();
	coreplayer->SetFile(item.filename.c_str());
	coreplayer->Start();
}

// Check if we are able to play a given file
bool Playlist::CanPlay(std::string const & path) {
	struct stat buf;

	// Does file exist?
	if (stat(path.c_str(), &buf)) {
#ifdef DEBUG
		printf("File does not exist\n");
#endif /* DEBUG */
		return false;
	}

	// Is it a type we might have a chance of playing?
	// (Some plugins may cope with playing special devices, eg, a /dev/scd)
	bool can_play = false;
	if (S_ISREG(buf.st_mode) ||
		S_ISCHR(buf.st_mode) || S_ISBLK(buf.st_mode) ||
		S_ISFIFO(buf.st_mode) || S_ISSOCK(buf.st_mode))
	{
		input_plugin *plugin = coreplayer->GetPlayer(path.c_str());
		if (plugin) {
			can_play = true;
		}
	}

	return can_play;
}

// Add a path to list, recursing through (to a maximum of depth subdirectories)
static void additems(std::vector<std::string> *items, std::string path, int depth) {
	if(depth < 0) return;

	struct stat buf;

	// Stat file, and don't follow symlinks
	// FIXME - make follow symlinks, but without letting it get into infinite
	// loops
	if (lstat(path.c_str(), &buf)) {
		return;
	}

	if (S_ISDIR(buf.st_mode)) {
		dirent *entry;
		DIR *dir = opendir(path.c_str());
		if (dir) {
			while ((entry = readdir(dir)) != NULL) {
				//printf("`%s'/`%s'\n", path.c_str(), entry->d_name);
				if (strcmp(entry->d_name, ".") == 0 ||
					strcmp(entry->d_name, "..") == 0)
					continue;
				additems(items, path + "/" + entry->d_name, depth - 1);
			}
			closedir(dir);  
		}
	} else {
		items->push_back(path);
	}
}