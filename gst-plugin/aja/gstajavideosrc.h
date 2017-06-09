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

#ifndef _GST_AJA_VIDEO_SRC_H_
#define _GST_AJA_VIDEO_SRC_H_

#include <gst/gst.h>
#include <gst/base/base.h>
#include <gst/video/video.h>
#include "gstaja.h"

#include <gst/base/gstbasesrc.h>

G_BEGIN_DECLS

#define GST_TYPE_AJA_VIDEO_SRC          (gst_aja_video_src_get_type())
#define GST_AJA_VIDEO_SRC(obj)          (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AJA_VIDEO_SRC,GstAjaVideoSrc))
#define GST_AJA_VIDEO_SRC_CAST(obj)     ((GstAjaVideoSrc*)obj)
#define GST_AJA_VIDEO_SRC_CLASS(klass)  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AJA_VIDEO_SRC,GstAjaVideoSrcClass))
#define GST_IS_AJA_VIDEO_SRC(obj)       (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AJA_VIDEO_SRC))
#define GST_IS_AJA_VIDEO_SRC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AJA_VIDEO_SRC))

typedef struct _GstAjaVideoSrc GstAjaVideoSrc;
typedef struct _GstAjaVideoSrcClass GstAjaVideoSrcClass;

struct _GstAjaVideoSrc
{
    GstPushSrc                  parent;

    GstAjaModeRawEnum           modeEnum;

    GstVideoInfo                info;
    GstAjaInput                 *input;

    GCond                       cond;
    GMutex                      lock;
    gboolean                    flushing;
    GQueue                      current_frames;

    guint                       queue_size;
    gchar *                     device_identifier;
    guint                       input_channel;
    gboolean                    output_stream_time;
    GstClockTime                skip_first_time;

    GstClockTime first_time;
    GstClockTime *times;
    GstClockTime *times_temp;
    guint window_size, window_fill;
    gboolean window_filled;
    guint window_skip, window_skip_count;
    struct {
      GstClockTime xbase, b;
      GstClockTime num, den;
    } current_time_mapping;
    struct {
      GstClockTime xbase, b;
      GstClockTime num, den;
    } next_time_mapping;
    gboolean next_time_mapping_pending;
};

struct _GstAjaVideoSrcClass
{
    GstPushSrcClass parent_class;
};

GType gst_aja_video_src_get_type (void);

G_END_DECLS

#endif /* _GST_AJA_VIDEO_SRC_H_ */
