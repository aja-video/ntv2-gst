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
    guint                       input_channel;
    guint                       device_number;

    GstClockTime                internal_base_time;
    GstClockTime                external_base_time;
};

struct _GstAjaVideoSrcClass
{
    GstPushSrcClass parent_class;
};

GType gst_aja_video_src_get_type (void);
void gst_aja_video_src_convert_to_external_clock (GstAjaVideoSrc * self, GstClockTime * timestamp, GstClockTime * duration);

G_END_DECLS

#endif /* _GST_AJA_VIDEO_SRC_H_ */
