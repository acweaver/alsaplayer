/*
 * drv_alsaplayer.c
 * Copyright (C) 1999 Paul N. Fisher <rao@gnu.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include "mikmod.h"

#define MIKMOD_FRAME_SIZE	4608
SBYTE *audio_buffer;

static BOOL 
alsaplayer_Init ()
{
  if (!(audio_buffer = (SBYTE *) malloc (MIKMOD_FRAME_SIZE)))
    return 1;

  return VC_Init ();
}

static void 
alsaplayer_Exit ()
{
  VC_Exit();
  
  if (audio_buffer)
    free (audio_buffer);
}

static void
alsaplayer_Update ()
{
  /* 
     No need to use this extra level of indirection;
     we handle all audio_buffer updating in mikmod_play_frame ()
  */
}

static BOOL 
alsaplayer_IsThere ()
{
  return 1;
}

MDRIVER drv_alsaplayer =
{
  NULL,
  "AlsaPlayer",
  "AlsaPlayer Output Driver v1.0",
  0, 255,
  "alsaplayer",

  NULL,
  alsaplayer_IsThere,
  VC_SampleLoad,
  VC_SampleUnload,
  VC_SampleSpace,
  VC_SampleLength,
  alsaplayer_Init,
  alsaplayer_Exit,
  NULL,
  VC_SetNumVoices,
  VC_PlayStart,
  VC_PlayStop,
  alsaplayer_Update,
  NULL,
  VC_VoiceSetVolume,
  VC_VoiceGetVolume,
  VC_VoiceSetFrequency,
  VC_VoiceGetFrequency,
  VC_VoiceSetPanning,
  VC_VoiceGetPanning,
  VC_VoicePlay,
  VC_VoiceStop,
  VC_VoiceStopped,
  VC_VoiceGetPosition,
  VC_VoiceRealVolume
};