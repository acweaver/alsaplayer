/*  playlist.c
 *
 *  Copyright (C) 1999-2002 Andy Lo A Foe <andy@alsaplayer.org>
 *  Rewritten for glib-2.0 by Evgeny Chukreev <codedj@echo.ru> 
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
 *
 *  $Id$
 *
*/ 

#include <glib-object.h>

#include "playitem.h"
#include "playlist.h"
#include "object.h"

/* TODO: Make i18n oneday. */
#define _(s) (s)

/* Class properties. */
enum {
    PROP_0,
    PROP_PAUSE,
    PROP_LOOPING_SONG,
    PROP_LOOPING_PLAYLIST
};

/* --- prototypes --- */
static void	ap_playlist_class_init	    (ApPlaylistClass	*class);
static void	ap_playlist_init	    (ApPlaylist		*playlist);
static void	ap_playlist_set_property    (GObject		*object,
					     guint		prop_id,
					     const GValue	*value,
					     GParamSpec		*pspec);
static void	ap_playlist_get_property    (GObject		*object,
					     guint		prop_id,
					     GValue		*value,
					     GParamSpec		*pspec);
static void	ap_playlist_finalize	    (GObject		*object);
static void	ap_playlist_dispose	    (GObject		*object);
static gpointer ap_playlist_info_thread	    (gpointer		data);
static gpointer ap_playlist_insert_thread   (gpointer		data);

/* --- variables --- */
static gpointer		parent_class = NULL;

/* --- functions --- */

static void
ap_playlist_class_init (ApPlaylistClass *class)
{
    /* Like aliases */
    GObjectClass *gobject_class = G_OBJECT_CLASS (class);
    
    /* Peek parent class */
    parent_class = g_type_class_peek_parent (class);
    
    /* Init GObject Class */
    gobject_class->set_property = ap_playlist_set_property;
    gobject_class->get_property = ap_playlist_get_property;
    gobject_class->finalize = ap_playlist_finalize;
    gobject_class->dispose = ap_playlist_dispose;

    /* Install properties */
    g_object_class_install_property (gobject_class,
				     PROP_PAUSE,
				     g_param_spec_boolean ("pause",
							  _("Pause"),
							  _("Playlist pause state."),
							  FALSE,
							  G_PARAM_READWRITE)
				     );

    g_object_class_install_property (gobject_class,
				     PROP_LOOPING_SONG,
				     g_param_spec_boolean ("looping_song",
							  _("Looping song"),
							  _("Looping song state."),
							  FALSE,
							  G_PARAM_READWRITE)
				     );

    g_object_class_install_property (gobject_class,
				     PROP_LOOPING_PLAYLIST,
				     g_param_spec_boolean ("looping_playlist",
							  _("Looping playlist"),
							  _("Looping playlist state."),
							  FALSE,
							  G_PARAM_READWRITE)
				     );
    
    /* Register signals */
    g_signal_new ("pause-toggled",
		  G_OBJECT_CLASS_TYPE (class),
		  G_SIGNAL_RUN_FIRST,
		  G_STRUCT_OFFSET (ApPlaylistClass, pause_toggled_signal),
		  NULL,
		  NULL,
		  g_cclosure_marshal_VOID__BOOLEAN,
		  G_TYPE_NONE,
		  1, G_TYPE_BOOLEAN);

    g_signal_new ("looping-song-toggled",
		  G_OBJECT_CLASS_TYPE (class),
		  G_SIGNAL_RUN_FIRST,
		  G_STRUCT_OFFSET (ApPlaylistClass, looping_song_toggled_signal),
		  NULL,
		  NULL,
		  g_cclosure_marshal_VOID__BOOLEAN,
		  G_TYPE_NONE,
		  1, G_TYPE_BOOLEAN);

    g_signal_new ("looping-playlist-toggled",
		  G_OBJECT_CLASS_TYPE (class),
		  G_SIGNAL_RUN_FIRST,
		  G_STRUCT_OFFSET (ApPlaylistClass, looping_playlist_toggled_signal),
		  NULL,
		  NULL,
		  g_cclosure_marshal_VOID__BOOLEAN,
		  G_TYPE_NONE,
		  1, G_TYPE_BOOLEAN);
    
    g_signal_new ("updated",
		  G_OBJECT_CLASS_TYPE (class),
		  G_SIGNAL_RUN_FIRST,
		  G_STRUCT_OFFSET (ApPlaylistClass, updated_signal),
		  NULL,
		  NULL,
		  g_cclosure_marshal_VOID__OBJECT,
		  G_TYPE_NONE,
		  1, G_TYPE_OBJECT);
} /* ap_playlist_class_init */

static void
ap_playlist_init (ApPlaylist *playlist)
{
    playlist->active = TRUE;
    
    playlist->paused = FALSE;
    playlist->looping_song = FALSE;

    /* Create asynchronous queue for delivering playitems into info thread */
    playlist->info_queue = g_async_queue_new ();
    
    /* Start info thread for this playlist. */
    playlist->info_thread = g_thread_create (ap_playlist_info_thread,
					     playlist,	/* data*/
					     1,		/* joinable */
					     NULL);	/* error var */

    /* Create asynchronous queue for delivering filenames into insert thread 
     * Filenames passed through the pointer arrays.
     * If pointer array has zero length, thread should stop its process.
     * Also, thread should free pointer array.
     */
    playlist->insert_queue = g_async_queue_new ();
    
    /* Start insert thread for this playlist. */
    playlist->insert_thread = g_thread_create (ap_playlist_insert_thread,
					       playlist,    /* data*/
					       1,	    /* joinable */
					       NULL);	    /* error var */
} /* ap_playlist_init */

static void
ap_playlist_dispose (GObject *object)
{
    ApPlaylist	    *playlist = AP_PLAYLIST (object);
    ApPlayItem	    *playitem;
    GPtrArray	    *p_array;

    /* Let other threads know that we are going to die.
     */
    playlist->active = FALSE;
 
    /* Stop insert thread.
     * We create empty pointer array for this.
     */
    p_array = g_ptr_array_new ();
    g_async_queue_push (playlist->insert_queue, p_array);
  
    /* Stop info thread.
     * We create fiction playitem (with NULL filename),
     * which is the signal to exit from info thread.
     * This item will be destroyed inside info thread.
     */
    playitem = ap_playitem_new (NULL);
    g_async_queue_push (playlist->info_queue, playitem);
 
    /* Join stopped threads. */
    g_thread_join (playlist->insert_thread);
    g_thread_join (playlist->info_thread);
   
    G_OBJECT_CLASS (parent_class)->dispose (object);
} /* ap_playlist_dispose */


static void
ap_playlist_finalize (GObject *object)
{
    ApPlaylist *playlist = AP_PLAYLIST (object); 
 
    /* Unref queues */
    g_async_queue_unref (playlist->info_queue);
    g_async_queue_unref (playlist->insert_queue);
    
    G_OBJECT_CLASS (parent_class)->finalize (object);
} /* ap_playlist_finalize */

static void
ap_playlist_set_property (GObject		*object,
			  guint			prop_id,
			  const GValue		*value,
			  GParamSpec		*pspec)
{
    ApPlaylist *playlist = AP_PLAYLIST (object);

    switch (prop_id) {
	case PROP_PAUSE:
	    ap_playlist_set_pause (playlist, g_value_get_boolean (value));
	    break;
	case PROP_LOOPING_SONG:
	    ap_playlist_set_loop_song (playlist, g_value_get_boolean (value));
	    break;
	case PROP_LOOPING_PLAYLIST:
	    ap_playlist_set_loop_playlist (playlist, g_value_get_boolean (value));
	    break;
    }
} /* ap_playlist_set_property */

static void
ap_playlist_get_property (GObject	*object,
			  guint		prop_id,
		          GValue	*value,
		          GParamSpec	*pspec)
{
    ApPlaylist *playlist = AP_PLAYLIST (object);

    switch (prop_id) {
	case PROP_PAUSE:
	    g_value_set_boolean (value, playlist->paused);
	    break;
	case PROP_LOOPING_SONG:
	    g_value_set_boolean (value, playlist->looping_song);
	    break;
	case PROP_LOOPING_PLAYLIST:
	    g_value_set_boolean (value, playlist->looping_playlist);
	    break;
    }
} /* ap_playlist_set_property */

gpointer
ap_playlist_info_thread (gpointer data)
{
    /* We don't need to ref playlist, since this thread exists
     * while playlist life only. */
    ApPlaylist	*playlist = AP_PLAYLIST (data);

    /* Ref queue. Just for fun. */
    g_async_queue_ref (playlist->info_queue);

    /* infty loop */
    while (1) {
	ApPlayItem *playitem = g_async_queue_pop (playlist->info_queue);

	/* We may not lock playitem during this, since
	 * we don't do access by this pointer and
	 * suppose that the NULL filenamed playitem
	 * could not change it filename.
	 */
	if (ap_playitem_get_filename (playitem) == NULL) {
	    /* This is the last item.
	     * It signals us to exit thread.
	     */
	    ap_object_unref (AP_OBJECT (playitem));
	    break;
	}

	if (!playlist->active) {
	    /* Thread is going to shutdown.
	     * So, lets skip real updating.
	     */
	    ap_object_unref (AP_OBJECT (playitem));
	    continue;
	}
	
	/* TODO: Make the real info filling.
	 * But now, we just emulate it ;)
	 */
	g_print ("info thread fills %p\n", playitem);

	ap_object_lock (AP_OBJECT (playitem));
	ap_playitem_set_title (playitem, "Updated");
	ap_object_unlock (AP_OBJECT (playitem));
	
	/* Emit "updated" signal */
	g_signal_emit_by_name (playlist, "updated", playitem);

	/* it was refed in ap_playlist_update_playitem */
	ap_object_unref (AP_OBJECT (playitem));
    }

    /* Unref queue before exit. */
    g_async_queue_unref (playlist->info_queue);
    
    return NULL;
} /* ap_playlist_info_thread */

gpointer
ap_playlist_insert_thread (gpointer data)
{
    /* We don't need to ref playlist, since this thread exists
     * while playlist life only. */
    ApPlaylist	*playlist = AP_PLAYLIST (data);

    /* Ref queue. Just for fun. */
    g_async_queue_ref (playlist->insert_queue);

    /* infty loop */
    while (1) {
	guint	    pos, i;
	GPtrArray   *p_array = g_async_queue_pop (playlist->insert_queue);

	if (p_array->len == 0) {
	    /* This is the last item.
	     * It signals us to exit thread.
	     */
	    g_ptr_array_free (p_array, TRUE);
	    break;
	}

	if (!playlist->active) {
	    /* Thread is going to shutdown.
	     * So, lets skip real inserting.
	     */
	    for (i=1; i<p_array->len; i++)
		g_free (g_ptr_array_index (p_array, i));
	    g_ptr_array_free (p_array, TRUE);
	    continue;
	}

	/* Get position value */
	pos = GPOINTER_TO_UINT (g_ptr_array_index (p_array, 0));
	
	g_print ("Insert %u items in %u position.\n", p_array->len-1, pos);

	/* Free array, and each pointer in array. */
	for (i=1; i<p_array->len; i++)
	    g_free (g_ptr_array_index (p_array, i));
	g_ptr_array_free (p_array, TRUE);
    }

    /* Unref queue before exit. */
    g_async_queue_unref (playlist->insert_queue);
    
    return NULL;
} /* ap_playlist_insert_thread */


/* *****************************************************************/
/* Public functions.						   */

/**
 * @brief	Register the #ApPlaylistClass if necessary,
 *		and returns the type ID associated to it.
 *
 * @returns	The type ID of the &ApPlaylistClass.
 **/
GType
ap_playlist_get_type (void)
{
    static GType type = 0;
    static const GTypeInfo playlist_info = {
	sizeof (ApPlaylistClass),
	NULL,
        NULL,
        (GClassInitFunc) ap_playlist_class_init,
        NULL,
        NULL,
        sizeof (ApPlaylist),
        0,
        (GInstanceInitFunc) ap_playlist_init,
        NULL
    };

    if (!type) {
	/* First time create */
	type = g_type_register_static (AP_TYPE_OBJECT,	/* Parent Type */
				       "ApPlaylist",	/* Name */
				       &playlist_info,	/* Type Info */
				       0);		/* Flags */
    }

    return type;
}; /* ap_playlist_object_get_type */

/**
 * @param playlist	An ApPlaylist.
 * @param pause		TRUE if the playlist should be paused.
 *
 * @brief		Sets whether to pause the playlist or not.
 *
 * @see			ap_playlist_is_paused()
 */
void
ap_playlist_set_pause (ApPlaylist *playlist, gboolean pause)
{
    g_return_if_fail (AP_IS_PLAYLIST (playlist));

    if (playlist->paused != pause) {
	playlist->paused = pause;
    
	/* Signal */
	g_signal_emit_by_name (playlist, "pause-toggled", pause);
    }
} /* ap_playlist_set_pause */

/**
 * @param playlist	An ApPlaylist.
 *
 * @brief		Returns whether to pause the playlist or not.
 *
 * @return		TRUE if the playlist is paused.
 *
 * @see			ap_playlist_set_pause()
 */
gboolean
ap_playlist_is_paused (ApPlaylist *playlist)
{
    g_return_val_if_fail (AP_IS_PLAYLIST (playlist), FALSE);

    return playlist->paused;
} /* ap_playitem_is_paused */

/**
 * @param playlist	An ApPlaylist.
 * @param loop_song	TRUE if the song should be looped.
 *
 * @brief		Sets whether to loop the song or not.
 *
 * @see			ap_playlist_is_looping_song()
 */
void
ap_playlist_set_loop_song (ApPlaylist *playlist, gboolean loop_song)
{
    g_return_if_fail (AP_IS_PLAYLIST (playlist));

    if (playlist->looping_song != loop_song) {
	playlist->looping_song = loop_song;

	/* Signal */
	g_signal_emit_by_name (playlist, "looping-song-toggled", loop_song);
    }
} /* ap_playitem_set_playtime */

/**
 * @param playlist	An ApPlaylist.
 *
 * @brief		Returns whether to loop the song or not.
 *
 * @return		TRUE if the song is looping.
 *
 * @see			ap_playlist_set_loop_song()
 */
gboolean
ap_playlist_is_looping_song (ApPlaylist *playlist)
{
    g_return_val_if_fail (AP_IS_PLAYLIST (playlist), FALSE);

    return playlist->looping_song;
} /* ap_playitem_set_playtime */

/**
 * @param playlist	    An ApPlaylist.
 * @param loop_playlist	    TRUE if the playlist should be looped.
 *
 * @brief		    Sets whether to loop the playlist or not.
 *
 * @see			    ap_playlist_is_looping_playlist()
 */
void
ap_playlist_set_loop_playlist (ApPlaylist *playlist, gboolean loop_playlist)
{
    g_return_if_fail (AP_IS_PLAYLIST (playlist));

    if (playlist->looping_playlist != loop_playlist) {
	playlist->looping_playlist = loop_playlist;

	/* Signal */
	g_signal_emit_by_name (playlist, "looping-playlist-toggled", loop_playlist);
    }
} /* ap_playitem_set_playtime */

/**
 * @param playlist	An ApPlaylist.
 *
 * @brief		Returns whether to loop the playlist or not.
 *
 * @return		TRUE if the playlist is looping.
 *
 * @see			ap_playlist_set_loop_playlist()
 */
gboolean
ap_playlist_is_looping_playlist (ApPlaylist *playlist)
{
    g_return_val_if_fail (AP_IS_PLAYLIST (playlist), FALSE);

    return playlist->looping_playlist;
} /* ap_playitem_set_playtime */

/**
 * @param playlist	    An #ApPlaylist.
 * @param playitem	    Playitem to update.
 *
 * @brief		    Queue playitem for update.
 *
 * You can track end of the updating process
 * via the "updated" signal.
 **/
void
ap_playlist_update (ApPlaylist	    *playlist,
		    ApPlayItem	    *playitem)
{
    g_return_if_fail (AP_IS_PLAYLIST (playlist));
    g_return_if_fail (AP_IS_PLAYITEM (playitem));

    ap_object_ref (AP_OBJECT (playitem));
    g_async_queue_push (playlist->info_queue, playitem);
}

/**
 * @param playlist	    An #ApPlaylist.
 * @param array		    An array of pointers to filename strings.
 * @param pos		    The position to place the items at.
 *
 * @brief		    Queue filenames to insert at the given position.
 *
 * You can track end of the inserting process
 * via the "inserted" signal.
 **/
void
ap_playlist_insert (ApPlaylist	    *playlist,
		    GPtrArray	    *array,
		    guint	    pos)
{
    GPtrArray *queued;
    guint i;
    
    g_return_if_fail (AP_IS_PLAYLIST (playlist));
    g_return_if_fail (array != NULL);
    g_return_if_fail (array->len != 0);

    /* Create array to queue it. */
    queued = g_ptr_array_sized_new (array->len + 1);

    /* Remember insert position as the first pointer in the array. */
    g_ptr_array_add (queued, GUINT_TO_POINTER (pos));

    /* Duplicate all strings for queued array. */
    for (i=0; i<array->len; i++) {
	gchar *new_str = g_strdup (g_ptr_array_index (array, i));

	g_ptr_array_add (queued, new_str);
    }

    g_async_queue_push (playlist->insert_queue, queued);
}