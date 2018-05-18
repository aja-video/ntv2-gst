/* GStreamer
 * Copyright (C) 2015 PSM <philm@aja.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _GST_AJA_AUDIO_SRC_H_
#define _GST_AJA_AUDIO_SRC_H_

#include <gst/audio/gstaudiosrc.h>
#include "gstaja.h"

G_BEGIN_DECLS

#define GST_TYPE_AJA_AUDIO_SRC   (gst_aja_audio_src_get_type())
#define GST_AJA_AUDIO_SRC(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AJA_AUDIO_SRC,GstAjaAudioSrc))
#define GST_AJA_AUDIO_SRC_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AJA_AUDIO_SRC,GstAjaAudioSrcClass))
#define GST_IS_AJA_AUDIO_SRC(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AJA_AUDIO_SRC))
#define GST_IS_AJA_AUDIO_SRC_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AJA_AUDIO_SRC))

typedef struct _GstAjaAudioSrc GstAjaAudioSrc;
typedef struct _GstAjaAudioSrcClass GstAjaAudioSrcClass;

struct _GstAjaAudioSrc
{
    GstPushSrc                  parent;
    
    GstAudioInfo                info;
    GstAjaInput                 *input;

    GCond                       cond;
    GMutex                      lock;
    gboolean                    flushing;
    GQueue                      current_packets;

    GstClockTime                alignment_threshold;
    GstClockTime                discont_wait;
    GstClockTime                discont_time;

    guint                       device_number;
    guint                       input_channel;
    guint                       queue_size;
    guint64                     next_offset;

};

struct _GstAjaAudioSrcClass
{
    GstPushSrcClass parent_class;
};

GType gst_aja_audio_src_get_type (void);

G_END_DECLS

#endif
