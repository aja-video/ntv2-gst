/* GStreamer
 * Copyright (C) 2015 PSM <philm@aja.com>
 * Copyright (C) 2017 Sebastian Dröge <sebastian@centricular.com>
 * Copyright (C) 2021 NVIDIA Corporation.  All rights reserved.
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

typedef enum {
  SIGNAL_STATE_UNKNOWN,
  SIGNAL_STATE_LOST,
  SIGNAL_STATE_AVAILABLE,
} GstAjaSignalState;

struct _GstAjaVideoSrc
{
    GstPushSrc                  parent;

    GstAjaModeRawEnum           modeEnum;
    uint8_t                     transferCharacteristics; /// SDR-TV (0), HLG (1), PQ (2), unspecified (3)
    uint8_t                     colorimetry;             /// Rec 709 (0), VANC (1), UHDTV (2), unspecified (3)
    bool                        fullRange;               /// 0-255 if true, 16-235 otherwise

    GstVideoInfo                info;
    GstAjaInput                 *input;

    GCond                       cond;
    GMutex                      lock;
    gboolean                    flushing;
    GstQueueArray               *current_frames;

    guint                       queue_size;
    gchar *                     device_identifier;
    GstAjaVideoInputMode        input_mode;
    SDIInputMode                sdi_input_mode;
    guint                       input_channel;
    gboolean                    passthrough;
    gboolean                    output_stream_time;
    GstClockTime                skip_first_time;
    GstAjaTimecodeMode          timecode_mode;
    gboolean                    output_cc;
    gint			last_cc_vbi_line;
    guint                       capture_cpu_core;
    gboolean                    use_nvmm;

    guint skipped_last;
    guint64 skipped_overall;
    GstClockTime skip_from_timestamp;
    GstClockTime skip_to_timestamp;

    // All only accessed from the capture thread
    GstAjaSignalState signal_state;
    GstClockTime first_time;
    GstClockTime discont_time;
    guint64 discont_frame_number;
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
