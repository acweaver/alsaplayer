/*  gtk_interface.cpp - gtk+ callbacks, etc
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

#include <unistd.h>
#include "config.h"
//#define NEW_SCALE
//#define TESTING
//#define SUBSECOND_DISPLAY 

#include <algorithm>
#include "utilities.h"

// Include things needed for gtk interface:
#include <gtk/gtk.h>

#include "support.h"
#include "gladesrc.h"
#include "pixmaps/f_play.xpm"
#include "pixmaps/r_play.xpm"
#include "pixmaps/pause.xpm"
#include "pixmaps/next.xpm"
#include "pixmaps/prev.xpm"
#include "pixmaps/stop.xpm"
#include "pixmaps/volume_icon.xpm"
#include "pixmaps/balance_icon.xpm"
#if 0
#include "pixmaps/eject.xpm"
#endif
#include "pixmaps/play.xpm"
#include "pixmaps/playlist.xpm"
#include "pixmaps/cd.xpm"

#include "PlaylistWindow.h"

// Include other things: (ultimate aim is to wrap this up into one nice file)
#include "CorePlayer.h"
#include "Playlist.h"
#include "EffectsWindow.h"
#include "Effects.h"
#include "ScopesWindow.h"
Playlist *playlist = NULL;

// Defines
#ifdef SUBSECOND_DISPLAY 
#define UPDATE_TIMEOUT  20000
#else
#define UPDATE_TIMEOUT  200000
#endif
#define BAL_CENTER  100
#define UPDATE_COUNT    5
#define MIN_BAL_TRESH   BAL_CENTER-10   // Center is a special case
#define MAX_BAL_TRESH   BAL_CENTER+10   // so we build in some slack
#define ZERO_PITCH_TRESH 2

// Global variables (get rid of these too... ;-) )
static int global_update = 1;
static int global_speed = 100;

static int global_draw_volume = 1;
static GtkWidget *play_pix;
static GdkPixmap *val_ind = NULL;
static gint global_rb = 1;

/* These are used to contain the size of the window manager borders around
   our windows, and are used to show/hide windows in the same positions. */
gint windows_x_offset = -1;
gint windows_y_offset = -1;

static gint main_window_x = 150;
static gint main_window_y = 175;

typedef struct  _update_struct {
	gpointer data;
	GtkWidget *drawing_area;
	GtkWidget *vol_scale;
	GtkWidget *bal_scale;
	GtkWidget *pos_scale;
	GtkWidget *speed_scale;
} update_struct;

update_struct global_ustr;

// Static variables  (to be moved into a class, at some point)
static GtkWidget *play_dialog;
static pthread_t indicator_thread;

gint global_effects_show = 0;
gint global_scopes_show = 0;

static int vol_scale[] = {
				0,1,2,4,7,12,18,26,35,45,56,69,83,100 };

#ifdef SUBSECOND_DISPLAY
#define INDICATOR_WIDTH 80
#else
#define INDICATOR_WIDTH 64
#endif

////////////////////////
// Callback functions //
////////////////////////


gint indicator_callback(gpointer data);



gboolean main_window_delete(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	CorePlayer *p = (CorePlayer *)data;
	global_update = -1;
	pthread_join(indicator_thread, NULL);
	p->Stop();
	gtk_main_quit();
	
	return FALSE;
}


void press_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	global_update = 0;
}

void draw_volume();

void volume_move_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	draw_volume();
}

void draw_title(char *title)
{
	update_struct *ustr = &global_ustr;
	GdkRectangle update_rect;
	static char old_title[128] = "";
	static int count = UPDATE_COUNT;

	if (count-- > 0 && strcmp(old_title, title) == 0)
		return;
	else {
		count = UPDATE_COUNT;
		if (strlen(title) > 127) {
			strncpy(old_title, title, 126);
			old_title[127] = 0;
		} else
		strcpy(old_title, title);
	}		
	update_rect.x = 82;
	update_rect.y = 0;
	update_rect.width = ustr->drawing_area->allocation.width - 82;
	update_rect.height = 18;

	if (val_ind) {	
			// Clear area
			gdk_draw_rectangle(val_ind,
							ustr->drawing_area->style->black_gc,
							true, update_rect.x, update_rect.y, update_rect.width,
							update_rect.height);
			// Draw string
			gdk_draw_string(val_ind, ustr->drawing_area->style->font,
							ustr->drawing_area->style->white_gc, update_rect.x+6,
							update_rect.y+14, title);
			// Do the drawing
			gtk_widget_draw (ustr->drawing_area, &update_rect);	
	}
}

void draw_format(char *format)
{
	update_struct *ustr = &global_ustr;
	GdkRectangle update_rect;
	static char old_format[128] = "";
	static int count = UPDATE_COUNT;

	if (count-- > 0 && strcmp(old_format, format) == 0) 
		return;
	else {
		count = UPDATE_COUNT;
		if (strlen(format) > 126) {
			strncpy(old_format, format, 126);
			old_format[127] = 0;
		} else
		strcpy(old_format, format);
	}

	update_rect.x = 82;
	update_rect.y = 16;
	update_rect.width = ustr->drawing_area->allocation.width - 82 - INDICATOR_WIDTH;  
	update_rect.height = 18;

	if (val_ind) {
			// Clear area
			gdk_draw_rectangle(val_ind,
							ustr->drawing_area->style->black_gc,
							true, update_rect.x, update_rect.y, update_rect.width,
							update_rect.height);
			// Draw string
			gdk_draw_string(val_ind, ustr->drawing_area->style->font,
							ustr->drawing_area->style->white_gc, update_rect.x+6,
							update_rect.y+12, format);
			// Do the drawing
			gtk_widget_draw (ustr->drawing_area, &update_rect);
	}
}


void draw_volume()
{
	update_struct *ustr = &global_ustr;
	GtkAdjustment *adj;
	CorePlayer *p = (CorePlayer *)ustr->data;
	GdkRectangle update_rect;
	char str[60];
	static int old_vol = -1;
	static int count = UPDATE_COUNT;

	if (!ustr->vol_scale)
		return;
#ifdef NEW_SCALE	
	adj = GTK_BSCALE(ustr->vol_scale)->adjustment;
#else	
	adj = GTK_RANGE(ustr->vol_scale)->adjustment;
#endif
	int val = (int)GTK_ADJUSTMENT(adj)->value;

	if (count-- > 0 && val == old_vol)
		return;
	else {
		count = UPDATE_COUNT;
		old_vol = val;
		//p->SetVolume(val);
	}	
	int idx = val;
  idx = (idx < 0) ? 0 : ((idx > 13) ? 13 : idx);
	val = vol_scale[idx];

	val ? sprintf(str, "Volume: %d%%  ", val) : sprintf(str, "Volume: mute");

	update_rect.x = 0;
	update_rect.y = 16;
	update_rect.width = 82;
	update_rect.height = 16;
	if (val_ind) {	
			gdk_draw_rectangle(val_ind,
							ustr->drawing_area->style->black_gc,
							true, update_rect.x, update_rect.y, update_rect.width, update_rect.height);
			gdk_draw_string(val_ind, ustr->drawing_area->style->font,
							ustr->drawing_area->style->white_gc, update_rect.x+6, update_rect.y+12, str);
			gtk_widget_draw (ustr->drawing_area, &update_rect);
	}		
}

void draw_balance();


void balance_move_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	global_draw_volume = 0;
	draw_balance();
}


void balance_release_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	global_draw_volume = 1;
}


void draw_balance()
{
	update_struct *ustr = &global_ustr;
	GdkRectangle update_rect;
	CorePlayer *p = (CorePlayer *)ustr->data;
	char str[60];
	int pan, left, right;

	pan = p->GetPan();
	/*
	if (pan > 0) {
		right = 100;
		left = 100 - pan;
	} else if (pan == 0) {
		left = right = 100;
	} else {
		left = 100;
		right = 100 + pan;
	}	
	*/
	if (pan < 0) {
		sprintf(str, "Pan: left %d%%", - pan);
	} else if (pan > 0) {
		sprintf(str, "Pan: right %d%%", pan);
	} else {
		sprintf(str, "Pan: center");
	} 
	update_rect.x = 0;
	update_rect.y = 16;
	update_rect.width = 82; 
	update_rect.height = 18;
	if (val_ind) {
			gdk_draw_rectangle(val_ind,
							ustr->drawing_area->style->black_gc,
							true, update_rect.x, update_rect.y, 
							update_rect.width, update_rect.height);
			gdk_draw_string(val_ind,
							ustr->drawing_area->style->font,
							ustr->drawing_area->style->white_gc,
							update_rect.x+6, update_rect.y+12,
							str);
			gtk_widget_draw (ustr->drawing_area, &update_rect);
	}		
}

void draw_speed();

void speed_move_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	draw_speed();
}

void draw_speed()
{
	update_struct *ustr = &global_ustr;
	GtkAdjustment *adj;
	GdkRectangle update_rect;
	char str[60];
	int speed_val;
	static int old_val = -20000;
	static int count = UPDATE_COUNT;
	static int pause_blink =  2000000;
	static int pause_active = 1;
#if 1
	adj = GTK_RANGE(ustr->speed_scale)->adjustment;
#else
	adj = GTK_BSCALE(ustr->speed_scale)->adjustment;
#endif	

	speed_val = (int)GTK_ADJUSTMENT(adj)->value;
	if (count-- > 0 && speed_val == old_val) 
		return;
	count = UPDATE_COUNT;	
	old_val = speed_val;	
	if (speed_val < ZERO_PITCH_TRESH && speed_val > -ZERO_PITCH_TRESH) {
#if 0
		if ((pause_blink -= (UPDATE_TIMEOUT * UPDATE_COUNT)) < 0) {
			pause_blink = 200000;
			pause_active = 1 - pause_active;
		}
		sprintf(str, "Speed: %s", pause_active ? "paused" : "");
#else
		sprintf(str, "Speed: pause");
#endif
	}
	else
		sprintf(str, "Speed: %d%%  ", (int)GTK_ADJUSTMENT(adj)->value);
	update_rect.x = 0; 
	update_rect.y = 0;
	update_rect.width = 82;
	update_rect.height = 16;
	if (val_ind) {
			gdk_draw_rectangle(val_ind,
							ustr->drawing_area->style->black_gc,
							true,
							update_rect.x, update_rect.y,
							update_rect.width,
							update_rect.height);
			gdk_draw_string(val_ind,
							ustr->drawing_area->style->font,
							ustr->drawing_area->style->white_gc,
							update_rect.x+6, update_rect.y+14,
							str);
			gtk_widget_draw (ustr->drawing_area, &update_rect);		
	}
}	


void val_release_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	update_struct *ustr = &global_ustr;
	GdkRectangle update_rect;

	update_rect.x = 0;
	update_rect.y = 0;
	update_rect.width = 106;
	update_rect.height = 20;
	if (val_ind) {
			gdk_draw_rectangle(val_ind,
							ustr->drawing_area->style->black_gc,
							true,
							0, 0,
							ustr->drawing_area->allocation.width-64,
							20);
			gtk_widget_draw (ustr->drawing_area, &update_rect);
	}
}


void release_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	GtkAdjustment *adj;
	update_struct *ustr = &global_ustr;
	CorePlayer *p = (CorePlayer *)ustr->data;

	adj = GTK_RANGE(widget)->adjustment;
	p->Seek((int)adj->value);
	global_update = 1;	
}


void move_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	//static int c=0;
	//printf("move event %d\n", c++);
	indicator_callback(data);
}

void speed_cb(GtkWidget *widget, gpointer data)
{
	CorePlayer *p = (CorePlayer *)data;
	float val =  GTK_ADJUSTMENT(widget)->value;
	global_speed = (gint) val;	
	if (val < ZERO_PITCH_TRESH && val > -ZERO_PITCH_TRESH)
		val = 0;
	p->SetSpeed(  (float) val / 100.0 );
	draw_speed();
}

void forward_play_cb(GtkWidget *widget, gpointer data)
{
	GtkAdjustment *adj;


	adj = GTK_RANGE(data)->adjustment;
	gtk_adjustment_set_value(adj, 100.0);
}


void reverse_play_cb(GtkWidget *widget, gpointer data)
{
	GtkAdjustment *adj;

	adj = GTK_RANGE(data)->adjustment;
	gtk_adjustment_set_value(adj, -100.0);
}


void pause_cb(GtkWidget *widget, gpointer data)
{
	GtkAdjustment *adj;
	float new_val;

	static float val = 100.0;

	adj = GTK_RANGE(data)->adjustment;

	if (adj->value == 0.0) { // paused
		new_val = val;
	} else {
		new_val = 0.0;
		val = adj->value;
	}

	gtk_adjustment_set_value(adj, new_val);
}


void stop_cb(GtkWidget *widget, gpointer data)
{
	CorePlayer *p = (CorePlayer *)data;

	if (p && p->IsPlaying()) {
		if (playlist)
			playlist->Pause();
		p->Stop();
		clear_buffer();
	}	
}

void eject_cb(GtkWidget *, gpointer);

void play_cb(GtkWidget *widget, gpointer data)
{
	CorePlayer *p = (CorePlayer *)data;
	if (p) {
		if (playlist)
			playlist->UnPause(); // Start playing stuff in the playlist
		eject_cb(widget, data);
	}	
}


void eject_cb(GtkWidget *wdiget, gpointer data)
{
	CorePlayer *p = (CorePlayer *)data;
	if (p) {
		gtk_widget_show(play_dialog);
		gdk_window_raise(play_dialog->window);
	}
}	


void volume_cb(GtkWidget *widget, gpointer data)
{
	GtkAdjustment *adj = (GtkAdjustment *)widget;
	CorePlayer *p = (CorePlayer *)data;


	if (p) {
		int idx = (int)adj->value;
		idx = (idx < 0) ? 0 : ((idx > 13) ? 13 : idx);
		p->SetVolume(vol_scale[idx]);
	}
}


void balance_cb(GtkWidget *widget, gpointer data)
{
	GtkAdjustment *adj = (GtkAdjustment *)widget;
	CorePlayer *p = (CorePlayer *)data;
	int val;

	if (p) {
		val = (int)adj->value;
		if (val > MIN_BAL_TRESH && val < MAX_BAL_TRESH) val = BAL_CENTER;
		p->SetPan(val - 100);
	}	
}


gint indicator_callback(gpointer data)
{
	update_struct *ustr;
	CorePlayer *p;
	GtkAdjustment *adj;
	GdkDrawable *drawable;
	GdkRectangle  update_rect;
	GdkColor color;
	stream_info info;
	char str[60];
	unsigned long slider_val, t_min, t_sec;
	unsigned long c_hsec, secs, c_min, c_sec;
	unsigned long sr;
	static char old_str[60] = "";

	ustr = &global_ustr;
	p = (CorePlayer *)ustr->data;
	drawable = ustr->drawing_area->window;

	adj = GTK_RANGE(ustr->pos_scale)->adjustment;
	adj->lower = 0;
	adj->upper = p->GetFrames() - 32; // HACK!!
	memset(&info, 0, sizeof(stream_info));

	color.red = color.blue = color.green = 0;
	gdk_color_alloc(gdk_colormap_get_system(), &color);

	sr = p->GetSampleRate();
	if (p->IsActive()) { 
		int pos;
		pos = global_update ? p->GetPosition() : (int) adj->value;
		slider_val = pos;
		secs = global_update ? 
		p->GetCurrentTime() : p->GetCurrentTime((int) adj->value);
		c_min = secs / 6000;
		c_sec = (secs % 6000) / 100;
#ifdef SUBSECOND_DISPLAY		
		c_hsec = secs % 100;
#endif		
		secs = p->GetCurrentTime(p->GetFrames());
		t_min = secs / 6000;
		t_sec = (secs % 6000) / 100;
		gtk_adjustment_set_value(adj, pos);
		p->GetStreamInfo(&info);
	} else {
		t_min = 0;
		t_sec = 0;
		c_sec = 0;
		c_min = 0;
		c_hsec = 0;
		sprintf(info.title, "No stream");
	}
	if (t_min == 0 && t_sec == 0) {
		sprintf(str, "No time data");
	} else {
#ifdef SUBSECOND_DISPLAY	
		sprintf(str, "%02ld:%02ld.%02d/%02d:%02d", c_min, c_sec, c_hsec, t_min, t_sec);
#else
		sprintf(str, "%02ld:%02ld/%02ld:%02ld", c_min, c_sec, t_min, t_sec);
#endif
	}
	if (val_ind && strcmp(old_str, str) != 0) {
		strcpy(old_str, str);
		// Painting in pixmap here
		update_rect.x = ustr->drawing_area->allocation.width-INDICATOR_WIDTH;
		update_rect.y = 16;
		update_rect.width = INDICATOR_WIDTH;
		update_rect.height = 18;	
		gdk_draw_rectangle(val_ind, 
						   ustr->drawing_area->style->black_gc,
						   true,
						   update_rect.x,
						   update_rect.y,
						   update_rect.width,
						   update_rect.height);

		gdk_draw_string(val_ind,
						ustr->drawing_area->style->font,
						ustr->drawing_area->style->white_gc,
						update_rect.x + 2, 
						update_rect.y + 12,
						str);	
		gtk_widget_draw (ustr->drawing_area, &update_rect);
	}
	draw_format(info.stream_type);
	draw_title(info.title);
	draw_speed();
	if (global_draw_volume)
		draw_volume();
	update_rect.x = 0;
	update_rect.y = 0;
	update_rect.width = ustr->drawing_area->allocation.width;
	update_rect.height = ustr->drawing_area->allocation.height;
	gdk_flush();
	return true;
}


void cd_cb(GtkWidget *widget, gpointer data)
{
	CorePlayer *p = (CorePlayer *)data;

	if (p) {
		playlist->Pause();
		p->PlayFile("CD.cdda");
		playlist->UnPause();
	}
}


void exit_cb(GtkWidget *widget, gpointer data)
{
	gtk_main_quit();	
}

void scopes_cb(GtkWidget *widget, gpointer data)
{
	GtkWidget *win = (GtkWidget *)data;
	int x, y;
	if (global_scopes_show) {
		gdk_window_get_origin(win->window, &x, &y);
		if (windows_x_offset >= 0) {
			x -= windows_x_offset;
			y -= windows_y_offset;
		}
		gtk_widget_hide(win);
		gtk_widget_set_uposition(win, x, y);
	} else {
		gtk_widget_show(win);
	}
	global_scopes_show = 1 - global_scopes_show;	
}


void effects_cb(GtkWidget *widget, gpointer data)
{
	GtkWidget *win = (GtkWidget *)data;
	int x, y;

	if (global_effects_show) {
		gdk_window_get_origin(win->window, &x, &y);
		if (windows_x_offset >= 0) {
			x -= windows_x_offset;
			y -= windows_y_offset;
		}
		gtk_widget_hide(win);
		gtk_widget_set_uposition(win, x, y);
	} else {
		gtk_widget_show(win);
	}
	global_effects_show = 1 - global_effects_show;
}


void play_file_ok(GtkWidget *widget, gpointer data)
{
	Playlist *playlist = (Playlist *)data;
	CorePlayer *p = playlist->GetCorePlayer();

	if (p) {
		char *selected;
		GtkCList *file_list =
		GTK_CLIST(GTK_FILE_SELECTION(play_dialog)->file_list);
		GList *next = file_list->selection;
		if (!next) { // Nothing was selected
			return;
		}
		gchar *current_dir =
		g_strdup(gtk_file_selection_get_filename(GTK_FILE_SELECTION(play_dialog)));
		char *path;
		int index;	
		int marker = strlen(current_dir)-1;
		while (marker > 0 && current_dir[marker] != '/')
			current_dir[marker--] = '\0';

		// Get the selections
		std::vector<std::string> paths;
		while (next) {
			index = GPOINTER_TO_INT(next->data);

			gtk_clist_get_text(file_list, index, 0, &path);
			if (path) {
				paths.push_back(std::string(current_dir) + "/" + path);
			}
			next = next->next;
		}

		// Sort them (they're sometimes returned in a slightly odd order)
		sort(paths.begin(), paths.end());

		// Add selections to the queue, and start playing them
		playlist->AddAndPlay(paths);
		playlist->UnPause();
		
		gtk_clist_unselect_all(file_list);
		g_free(current_dir);
	}
	gtk_widget_hide(GTK_WIDGET(play_dialog));
}

void play_file_cancel(GtkWidget *widget, gpointer data)
{
	gint x,y;

	gdk_window_get_root_origin(GTK_WIDGET(data)->window, &x, &y);
	gtk_widget_hide(GTK_WIDGET(data));
	gtk_widget_hide(GTK_WIDGET(data));
	gtk_widget_set_uposition(GTK_WIDGET(data), x, y);
}


gboolean play_file_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	gint x, y;

	gdk_window_get_root_origin(widget->window, &x, &y);
	gtk_widget_hide(widget);
	gtk_widget_set_uposition(widget, x, y);

	return TRUE;
}

void playlist_cb(GtkWidget *widget, gpointer data)
{
	PlaylistWindowGTK *pl = (PlaylistWindowGTK *)data;
	pl->ToggleVisible();
}

gint alsaplayer_button_press(GtkWidget *widget, GdkEvent *event)
{
	if (event->type == GDK_BUTTON_PRESS) {
		GdkEventButton *bevent = (GdkEventButton *) event;
		gtk_menu_popup (GTK_MENU (widget), NULL, NULL, NULL, NULL,
						bevent->button, bevent->time);
		return true;
	}
	return false;
}


void indicator_looper(void *data)
{
#ifdef DEBUG
	printf("THREAD-%d=indicator thread\n", getpid());
#endif
	while (global_update >= 0) {
		GDK_THREADS_ENTER();
		if (global_update == 1) {
			indicator_callback(data);
		}
		GDK_THREADS_LEAVE();
		dosleep(UPDATE_TIMEOUT);
	}	
}


GtkWidget *xpm_label_box(gchar * xpm_data[], GtkWidget *to_win)
{
	GtkWidget *box1;
	GtkWidget *pixmapwid;
	GdkPixmap *pixmap;
	GdkBitmap *mask;
	GtkStyle *style;

	box1 = gtk_hbox_new(false, 0);
	gtk_container_border_width(GTK_CONTAINER(box1), 0);

	style = gtk_widget_get_style(to_win);

	pixmap = gdk_pixmap_create_from_xpm_d(to_win->window, &mask, &style->bg[
		GTK_STATE_NORMAL], xpm_data);
	pixmapwid = gtk_pixmap_new(pixmap, mask);

	gtk_box_pack_start(GTK_BOX(box1), pixmapwid, true, false, 1);

	gtk_widget_show(pixmapwid);

        return (box1);
}


void on_expose_event (GtkWidget * widget, GdkEvent * event, gpointer data)
{
  gint x, y;

  if (windows_x_offset == -1)
    {
      gdk_window_get_origin (widget->window, &x, &y);
      windows_x_offset = x - main_window_x;
      /* Make sure offset seems reasonable. If not, set it to -2 so we don't
         try this again later. */
      if (windows_x_offset < 0 || windows_x_offset > 50)
        windows_x_offset = -2;
      else
        windows_y_offset = y - main_window_y;
    }
}

gint pixmap_expose(GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
	GdkPixmap *the_pixmap = val_ind;
	gdk_draw_pixmap(widget->window,
		widget->style->black_gc,
		the_pixmap,
		event->area.x, event->area.y,
		event->area.x, event->area.y,
		event->area.width, event->area.height);
	return false;
}


gint val_area_configure(GtkWidget *widget, GdkEventConfigure *event, gpointer data)
{
	if (val_ind) {
		global_update = 0;
		gdk_pixmap_unref(val_ind);
	}
	val_ind = gdk_pixmap_new(widget->window,
		widget->allocation.width,
		32, -1);
	gdk_draw_rectangle(val_ind,
						widget->style->black_gc,
                        true, 
                        0, 0,
                        widget->allocation.width,
                        32);
	// Set up expose event handler 
	gtk_signal_connect(GTK_OBJECT(widget), "expose_event",
                (GtkSignalFunc) pixmap_expose, val_ind);
	global_update = 1;	
	return true;
}

void init_main_window(CorePlayer *p, Playlist *pl)
{
	GtkWidget *root_menu;
	GtkWidget *menu_item;
	GtkWidget *main_window;
	GtkWidget *effects_window;
	GtkWidget *scopes_window;
	GtkWidget *working;
	GtkWidget *speed_scale;
	GtkWidget *pix;
	GtkWidget *val_area;
	GtkStyle *style;
	GdkFont *smallfont;
	GtkAdjustment *adj;

	// Dirty trick
	playlist = pl;

	main_window = create_main_window();
	gtk_window_set_policy(GTK_WINDOW(main_window), false, false, false);
	gtk_window_set_title(GTK_WINDOW(main_window), "AlsaPlayer "VERSION);
	gtk_widget_show(main_window); // Hmmm

	static PlaylistWindowGTK *playlist_window_gtk = new PlaylistWindowGTK(playlist);
	effects_window = init_effects_window();	
	scopes_window = init_scopes_window();
	play_dialog = gtk_file_selection_new("Play file");
	gtk_file_selection_hide_fileop_buttons (GTK_FILE_SELECTION (play_dialog));
	GtkCList *file_list = GTK_CLIST(GTK_FILE_SELECTION(play_dialog)->file_list);

	gtk_clist_set_selection_mode(file_list, GTK_SELECTION_EXTENDED);

	gtk_signal_connect(GTK_OBJECT(
								  GTK_FILE_SELECTION(play_dialog)->cancel_button), "clicked",
					   GTK_SIGNAL_FUNC(play_file_cancel), play_dialog);
	gtk_signal_connect(GTK_OBJECT(play_dialog), "delete_event",
					   GTK_SIGNAL_FUNC(play_file_delete_event), play_dialog);
	gtk_signal_connect(GTK_OBJECT(
								  GTK_FILE_SELECTION(play_dialog)->ok_button),
					   "clicked", GTK_SIGNAL_FUNC(play_file_ok), playlist);


	gtk_signal_connect (GTK_OBJECT (main_window), "expose_event",
						GTK_SIGNAL_FUNC (on_expose_event), NULL);


	speed_scale = get_widget(main_window, "pitch_scale"); 

	smallfont = gdk_font_load("-adobe-helvetica-medium-r-normal--10-*-*-*-*-*-*-*");


	style = gtk_style_new();
	style = gtk_style_copy(gtk_widget_get_style(main_window));
	gdk_font_unref(style->font);
	style->font = smallfont;
	gdk_font_ref(style->font); 	

#if 0
	working = get_widget(main_window, "eject_button");
	if (working) {
		pix = xpm_label_box(eject_xpm, main_window);
		gtk_widget_show(pix);
		gtk_container_add(GTK_CONTAINER(working), pix);
		gtk_signal_connect(GTK_OBJECT(working), "clicked",
						   GTK_SIGNAL_FUNC(eject_cb), p);
		gtk_button_set_relief(GTK_BUTTON(working),
							  global_rb ? GTK_RELIEF_NONE : GTK_RELIEF_NORMAL);
	}										
#endif

	working = get_widget(main_window, "stop_button");
	pix = xpm_label_box(stop_xpm, main_window);
	gtk_widget_show(pix);
	gtk_container_add(GTK_CONTAINER(working), pix);
	gtk_signal_connect(GTK_OBJECT(working), "clicked",
					   GTK_SIGNAL_FUNC(stop_cb), p);
	gtk_button_set_relief(GTK_BUTTON(working), global_rb ? GTK_RELIEF_NONE :
						  GTK_RELIEF_NORMAL);

	working = get_widget(main_window, "reverse_button");
	pix = xpm_label_box(r_play_xpm, main_window);
	gtk_widget_show(pix);
	gtk_container_add(GTK_CONTAINER(working), pix);
	gtk_signal_connect(GTK_OBJECT(working), "clicked",
					   GTK_SIGNAL_FUNC(reverse_play_cb), speed_scale);
	gtk_button_set_relief(GTK_BUTTON(working), global_rb ? GTK_RELIEF_NONE : GTK_RELIEF_NORMAL);

	working = get_widget(main_window, "forward_button");
	pix = xpm_label_box(f_play_xpm, main_window);
	gtk_widget_show(pix);
	gtk_container_add(GTK_CONTAINER(working), pix); 
	gtk_signal_connect(GTK_OBJECT(working), "clicked",
					   GTK_SIGNAL_FUNC(forward_play_cb), speed_scale);
	gtk_button_set_relief(GTK_BUTTON(working), 
						  global_rb ? GTK_RELIEF_NONE : GTK_RELIEF_NORMAL);

	working = get_widget(main_window, "pause_button");
	pix = xpm_label_box(pause_xpm, main_window);
	gtk_widget_show(pix);
	gtk_container_add(GTK_CONTAINER(working), pix);
	gtk_signal_connect(GTK_OBJECT(working), "clicked",
					   GTK_SIGNAL_FUNC(pause_cb), speed_scale);
	gtk_button_set_relief(GTK_BUTTON(working), 
						  global_rb ? GTK_RELIEF_NONE : GTK_RELIEF_NORMAL);

	working = get_widget(main_window, "prev_button");
	pix = xpm_label_box(prev_xpm, main_window);
	gtk_widget_show(pix);
	gtk_container_add(GTK_CONTAINER(working), pix);
	gtk_signal_connect(GTK_OBJECT(working), "clicked",
					   GTK_SIGNAL_FUNC(playlist_window_gtk_prev), playlist);
	gtk_button_set_relief(GTK_BUTTON(working), 
						  global_rb ? GTK_RELIEF_NONE : GTK_RELIEF_NORMAL);

	working = get_widget(main_window, "next_button");
	pix = xpm_label_box(next_xpm, main_window);
	gtk_widget_show(pix);
	gtk_container_add(GTK_CONTAINER(working), pix); 
	gtk_signal_connect(GTK_OBJECT(working), "clicked",
					   GTK_SIGNAL_FUNC(playlist_window_gtk_next), playlist);
	gtk_button_set_relief(GTK_BUTTON(working), 
						  global_rb ? GTK_RELIEF_NONE : GTK_RELIEF_NORMAL);

	working = get_widget(main_window, "play_button");
	play_pix = xpm_label_box(play_xpm, main_window);
	gtk_widget_show(play_pix);
	gtk_container_add(GTK_CONTAINER(working), play_pix);
	gtk_signal_connect(GTK_OBJECT(working), "clicked",
					   GTK_SIGNAL_FUNC(play_cb), p);
	gtk_button_set_relief(GTK_BUTTON(working), 
						  global_rb ? GTK_RELIEF_NONE : GTK_RELIEF_NORMAL);

	working = get_widget(main_window, "volume_pix_frame");
	if (working) {
		pix = xpm_label_box(volume_icon_xpm, main_window);
		gtk_widget_show(pix);
		gtk_container_add(GTK_CONTAINER(working), pix);
	}
	working = get_widget(main_window, "vol_scale");
	if (working) {
#ifdef NEW_SCALE	
		adj = GTK_BSCALE(working)->adjustment;
#else
		adj = GTK_RANGE(working)->adjustment;
#endif	
		gtk_adjustment_set_value(adj, (float)p->GetVolume());
		gtk_signal_connect (GTK_OBJECT (adj), "value_changed",
							GTK_SIGNAL_FUNC(volume_cb), p);
	}

	val_area = gtk_drawing_area_new();
	gtk_drawing_area_size(GTK_DRAWING_AREA(val_area), 204, 32);
	gtk_widget_show(val_area);

	global_ustr.vol_scale = working;
	global_ustr.drawing_area = val_area;
	global_ustr.data = p;
	if (working) {	
		gtk_signal_connect (GTK_OBJECT (working), "motion_notify_event",
							GTK_SIGNAL_FUNC(volume_move_event), &global_ustr);
		gtk_signal_connect (GTK_OBJECT (working), "button_press_event",
							GTK_SIGNAL_FUNC(volume_move_event), &global_ustr);
	}
	gtk_signal_connect(GTK_OBJECT(val_area), "configure_event",
					   (GtkSignalFunc) val_area_configure, NULL);
	gtk_signal_connect(GTK_OBJECT(val_area), "expose_event",
					   (GtkSignalFunc) pixmap_expose, NULL);


	working = get_widget(main_window, "balance_pic_frame");
	if (working) {
		pix = xpm_label_box(balance_icon_xpm, main_window);
		gtk_widget_show(pix);
		gtk_container_add(GTK_CONTAINER(working), pix);
	}
	working = get_widget(main_window,  "bal_scale");
	if (working) {		
		adj = GTK_RANGE(working)->adjustment;
		gtk_adjustment_set_value(adj, 100.0);
		gtk_signal_connect (GTK_OBJECT (adj), "value_changed",
							GTK_SIGNAL_FUNC(balance_cb), p);
		global_ustr.bal_scale = working;
		gtk_signal_connect (GTK_OBJECT (working), "motion_notify_event",
							GTK_SIGNAL_FUNC(balance_move_event), &global_ustr);
		gtk_signal_connect (GTK_OBJECT (working), "button_press_event",
							GTK_SIGNAL_FUNC(balance_move_event), &global_ustr);
		gtk_signal_connect (GTK_OBJECT(working), "button_release_event",
							GTK_SIGNAL_FUNC(balance_release_event), &global_ustr);
	}

	working = get_widget(main_window, "playlist_button");
	pix = xpm_label_box(playlist_xpm, main_window);
	gtk_widget_show(pix);
	gtk_container_add(GTK_CONTAINER(working), pix);
	gtk_signal_connect(GTK_OBJECT(working), "clicked",
					   GTK_SIGNAL_FUNC(playlist_cb), playlist_window_gtk); 
	gtk_button_set_relief(GTK_BUTTON(working), 
						  global_rb ? GTK_RELIEF_NONE : GTK_RELIEF_NORMAL);
#if 0
	working = get_widget(main_window, "scope_button");
	if (working) {
		GtkWidget *label = gtk_label_new("alsaplayer");
		style = gtk_style_copy(gtk_widget_get_style(label));
		gdk_font_unref(style->font);
		style->font = smallfont;
		gdk_font_ref(style->font);
		gtk_widget_set_style(GTK_WIDGET(label), style);
		gtk_container_add(GTK_CONTAINER(working), label);
		gtk_widget_show(label);
		gtk_button_set_relief(GTK_BUTTON(working),
							  global_rb ? GTK_RELIEF_NONE : GTK_RELIEF_HALF);
	}
#endif

	working = get_widget(main_window, "cd_button");
	pix = xpm_label_box(cd_xpm, main_window);
	gtk_widget_show(pix);
	gtk_container_add(GTK_CONTAINER(working), pix);
	//gtk_signal_connect(GTK_OBJECT(working), "clicked",
	//			GTK_SIGNAL_FUNC(cd_cb), p);
	gtk_button_set_relief(GTK_BUTTON(working),
						  global_rb ? GTK_RELIEF_NONE : GTK_RELIEF_NORMAL);

	working = get_widget(main_window, "info_box"); 

	gtk_widget_set_style(val_area, style);

	gtk_box_pack_start (GTK_BOX (working), val_area, true, true, 0);

	global_ustr.data = p;
	working = get_widget(main_window, "pos_scale");
	global_ustr.pos_scale = working;

	working = get_widget(main_window, "pos_scale");
	gtk_signal_connect(GTK_OBJECT(working), "button_release_event",
					   GTK_SIGNAL_FUNC(release_event), &global_ustr);
	gtk_signal_connect (GTK_OBJECT (working), "button_press_event",
						GTK_SIGNAL_FUNC(press_event), NULL);
	gtk_signal_connect (GTK_OBJECT (working), "motion_notify_event",
						GTK_SIGNAL_FUNC(move_event), &global_ustr);


	global_ustr.speed_scale = speed_scale;
#if 1
	gtk_signal_connect (GTK_OBJECT (speed_scale), "motion_notify_event",
						GTK_SIGNAL_FUNC(speed_move_event), &global_ustr);
	gtk_signal_connect (GTK_OBJECT (speed_scale), "button_press_event",
						GTK_SIGNAL_FUNC(speed_move_event), &global_ustr);

	adj = GTK_RANGE(speed_scale)->adjustment;
	gtk_signal_connect (GTK_OBJECT (adj), "value_changed",
						GTK_SIGNAL_FUNC(speed_cb), p);
	gtk_adjustment_set_value(adj, 100.0);
#endif
	gtk_signal_connect(GTK_OBJECT(main_window), "delete_event", GTK_SIGNAL_FUNC(main_window_delete), p);

	// Create root menu
	root_menu = gtk_menu_new();

	// Preferences
#if 0
	menu_item = gtk_menu_item_new_with_label("Preferences...");
	gtk_menu_append(GTK_MENU(root_menu), menu_item);
	gtk_widget_show(menu_item);
#endif
	// Scopes
	menu_item = gtk_menu_item_new_with_label("Scopes...");
	gtk_menu_append(GTK_MENU(root_menu), menu_item);
	gtk_signal_connect(GTK_OBJECT(menu_item), "activate",
					   GTK_SIGNAL_FUNC(scopes_cb), scopes_window);
	gtk_widget_show(menu_item);
	// Effects
	menu_item = gtk_menu_item_new_with_label("Effects...");
	gtk_menu_append(GTK_MENU(root_menu), menu_item);
	gtk_signal_connect(GTK_OBJECT(menu_item), "activate",
					   GTK_SIGNAL_FUNC(effects_cb), effects_window);
	gtk_widget_show(menu_item);
	gtk_widget_set_sensitive(menu_item, false);

	// About
	menu_item = gtk_menu_item_new_with_label("About...");
	gtk_menu_append(GTK_MENU(root_menu), menu_item);
	gtk_widget_show(menu_item);
	gtk_widget_set_sensitive(menu_item, false);

#if 1	
	// Separator
	menu_item = gtk_menu_item_new();
	gtk_menu_append(GTK_MENU(root_menu), menu_item);
	gtk_widget_show(menu_item);

	// CD playback
	menu_item = gtk_menu_item_new_with_label("CD Player (CDDA)");
	gtk_widget_show(menu_item);
	gtk_menu_append(GTK_MENU(root_menu), menu_item);
	gtk_signal_connect(GTK_OBJECT(menu_item), "activate",
					   GTK_SIGNAL_FUNC(cd_cb), p);
#endif


	// Separator
	menu_item = gtk_menu_item_new();
	gtk_menu_append(GTK_MENU(root_menu), menu_item);
	gtk_widget_show(menu_item);
	// Exit
	menu_item = gtk_menu_item_new_with_label("Exit");
	gtk_menu_append(GTK_MENU(root_menu), menu_item);
	gtk_signal_connect(GTK_OBJECT(menu_item), "activate",
					   GTK_SIGNAL_FUNC(exit_cb), NULL);
	gtk_widget_show(menu_item);
#if 0	
	// Connect popup menu
	working = get_widget(main_window, "scope_button");
	if (working) {
		gtk_signal_connect_object (GTK_OBJECT (working), "event",
								   GTK_SIGNAL_FUNC(alsaplayer_button_press), GTK_OBJECT(root_menu));
	}
	// Create CD button menu
	GtkWidget *cd_menu = gtk_menu_new();	
	menu_item = gtk_menu_item_new_with_label("Play CD (using CDDA)");
	gtk_widget_show(menu_item);
	gtk_menu_append(GTK_MENU(cd_menu), menu_item);
	gtk_signal_connect(GTK_OBJECT(menu_item), "activate",
					   GTK_SIGNAL_FUNC(cd_cb), p);
#ifdef HAVE_SOCKMON
	menu_item = gtk_menu_item_new_with_label("Monitor TCP socket (experimental)");
	gtk_widget_show(menu_item);
	gtk_menu_append(GTK_MENU(cd_menu), menu_item);
	gtk_signal_connect(GTK_OBJECT(menu_item), "activate",
					   GTK_SIGNAL_FUNC(sock_cb1), p);
	menu_item = gtk_menu_item_new_with_label("Monitor ESD (experimental)");
	gtk_widget_show(menu_item);
	gtk_menu_append(GTK_MENU(cd_menu), menu_item);
	gtk_signal_connect(GTK_OBJECT(menu_item), "activate",
					   GTK_SIGNAL_FUNC(sock_cb2), p);
#endif
#endif
	working = get_widget(main_window, "cd_button");
	gtk_signal_connect_object (GTK_OBJECT (working), "event",
							   GTK_SIGNAL_FUNC(alsaplayer_button_press), GTK_OBJECT(root_menu)); // cd

	gdk_flush();

	// start indicator thread
	pthread_create(&indicator_thread, NULL,
				   (void * (*)(void *))indicator_looper, p);
#ifdef DEBUG
	printf("THREAD-%d=main thread\n", getpid());
#endif
}