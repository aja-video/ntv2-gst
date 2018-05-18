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

#ifndef _GST_AJA_H_
#define _GST_AJA_H_

#include <stdio.h>

#include <gst/gst.h>
#include "gstntv2.h"

#include "ntv2enums.h"
#include "ntv2m31enums.h"

typedef enum
{
    GST_AJA_MODE_RAW_NTSC_8_5994i,
    GST_AJA_MODE_RAW_NTSC_10_5994i,
    
    GST_AJA_MODE_RAW_PAL_8_50i,
    GST_AJA_MODE_RAW_PAL_10_50i,
    
    GST_AJA_MODE_RAW_720_8_50p,
    GST_AJA_MODE_RAW_720_8_5994p,
    GST_AJA_MODE_RAW_720_8_60p,
    GST_AJA_MODE_RAW_720_10_50p,
    GST_AJA_MODE_RAW_720_10_5994p,
    GST_AJA_MODE_RAW_720_10_60p,
    
    GST_AJA_MODE_RAW_1080_8_50i,
    GST_AJA_MODE_RAW_1080_8_50p,
    GST_AJA_MODE_RAW_1080_8_5994i,
    GST_AJA_MODE_RAW_1080_8_5994p,
    GST_AJA_MODE_RAW_1080_8_60i,
    GST_AJA_MODE_RAW_1080_8_60p,
    
    GST_AJA_MODE_RAW_1080_10_50i,
    GST_AJA_MODE_RAW_1080_10_50p,
    GST_AJA_MODE_RAW_1080_10_5994i,
    GST_AJA_MODE_RAW_1080_10_5994p,
    GST_AJA_MODE_RAW_1080_10_60i,
    GST_AJA_MODE_RAW_1080_10_60p,
    
    GST_AJA_MODE_RAW_UHD_8_50p,
    GST_AJA_MODE_RAW_UHD_8_5994p,
    GST_AJA_MODE_RAW_UHD_8_60p,
    
    GST_AJA_MODE_RAW_UHD_10_50p,
    GST_AJA_MODE_RAW_UHD_10_5994p,
    GST_AJA_MODE_RAW_UHD_10_60p,
    
    GST_AJA_MODE_RAW_END
    
} GstAjaModeRawEnum;

#define GST_TYPE_AJA_MODE_RAW (gst_aja_mode_get_type_raw ())
GType gst_aja_mode_get_type_raw (void);

typedef enum
{
    GST_AJA_MODE_HEVC_NTSC_420_8_5994i,
    GST_AJA_MODE_HEVC_NTSC_422_10_5994i,
    
    GST_AJA_MODE_HEVC_PAL_420_8_50i,
    GST_AJA_MODE_HEVC_PAL_422_10_50i,
    
    GST_AJA_MODE_HEVC_720_420_8_50p,
    GST_AJA_MODE_HEVC_720_420_8_5994p,
    GST_AJA_MODE_HEVC_720_420_8_60p,
    GST_AJA_MODE_HEVC_720_422_10_50p,
    GST_AJA_MODE_HEVC_720_422_10_5994p,
    GST_AJA_MODE_HEVC_720_422_10_60p,
    
    GST_AJA_MODE_HEVC_1080_420_8_50i,
    GST_AJA_MODE_HEVC_1080_420_8_50p,
    GST_AJA_MODE_HEVC_1080_420_8_5994i,
    GST_AJA_MODE_HEVC_1080_420_8_5994p,
    GST_AJA_MODE_HEVC_1080_420_8_60i,
    GST_AJA_MODE_HEVC_1080_420_8_60p,
    
    GST_AJA_MODE_HEVC_1080_422_10_50i,
    GST_AJA_MODE_HEVC_1080_422_10_50p,
    GST_AJA_MODE_HEVC_1080_422_10_5994i,
    GST_AJA_MODE_HEVC_1080_422_10_5994p,
    GST_AJA_MODE_HEVC_1080_422_10_60i,
    GST_AJA_MODE_HEVC_1080_422_10_60p,
    
    GST_AJA_MODE_HEVC_UHD_420_8_50p,
    GST_AJA_MODE_HEVC_UHD_420_8_5994p,
    GST_AJA_MODE_HEVC_UHD_420_8_60p,
    
    GST_AJA_MODE_HEVC_UHD_420_10_50p,
    GST_AJA_MODE_HEVC_UHD_420_10_5994p,
    GST_AJA_MODE_HEVC_UHD_420_10_60p,
    
    GST_AJA_MODE_HEVC_UHD_422_10_50p,
    GST_AJA_MODE_HEVC_UHD_422_10_5994p,
    GST_AJA_MODE_HEVC_UHD_422_10_60p,

    GST_AJA_MODE_HEVC_END

} GstAjaModeHevcEnum;

#define GST_TYPE_AJA_MODE_HEVC (gst_aja_mode_get_type_hevc ())
GType gst_aja_mode_get_type_hevc (void);

// Used to keep track of engine when shared between audio/hevc/and video
typedef enum
{
    NTV2_EngineStateUndefined,
    NTV2_EngineStateInitialized,
    NTV2_EngineStateRunning,
    NTV2_EngineStateQuit,
} NTV2EngineState;

typedef struct _GstAjaMode GstAjaMode;
struct _GstAjaMode
{    
    M31VideoPreset          videoPreset;
    NTV2VideoFormat         videoFormat;
    int                     width;
    int                     height;
    int                     bitDepth;
    int                     fps_n;
    int                     fps_d;
    gboolean                isInterlaced;
    gboolean                is422;
    gboolean                isQuad;
    int                     par_n;
    int                     par_d;
    gboolean                isTff;
    const gchar             *colorimetry;
};


const GstAjaMode * gst_aja_get_mode_raw (GstAjaModeRawEnum e);
const GstAjaMode * gst_aja_get_mode_hevc (GstAjaModeHevcEnum e);

GstCaps * gst_aja_mode_get_caps_raw (GstAjaModeRawEnum e);
GstCaps * gst_aja_mode_get_template_caps_raw (void);

typedef struct _GstAjaOutput GstAjaOutput;
struct _GstAjaOutput
{
    NTV2GstAVHevc       *ntv2AVHevc;
    NTV2EngineState     ntv2EngineState;
    const GstAjaMode    *mode;
    
    GstClock            *clock;
    GstClock            *audio_clock;
    GstClockTime        clock_start_time, clock_last_time, clock_epoch;
    GstClockTimeDiff    clock_offset;
    gboolean            started, clock_restart;
    
    GMutex              lock;
    
    GstElement          *audiosink;
    gboolean            audio_enabled;
    GstElement          *videosink; 
    gboolean            video_enabled;
    void (*start_scheduled_playback) (GstElement *videosink);
};

typedef struct _GstAjaInput GstAjaInput;
struct _GstAjaInput
{
    NTV2GstAVHevc       *ntv2AVHevc;
    NTV2EngineState     ntv2EngineState;
    const GstAjaMode    *mode;

    GstClock            *clock;
    GstClockTime        clock_start_time, clock_offset, clock_last_time, clock_epoch;
    gboolean            started, clock_restart;
    
    GMutex              lock;
    
    GstElement          *audiosrc;
    gboolean            audio_enabled;
    GstElement          *videosrc;
    gboolean            video_enabled;
    GstElement          *hevcsrc;
    gboolean            hevc_enabled;
};

#define GST_TYPE_AJA_CLOCK \
(gst_aja_clock_get_type())
#define GST_AJA_CLOCK(obj) \
(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AJA_CLOCK,GstAjaClock))
#define GST_AJA_CLOCK_CLASS(klass) \
(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AJA_CLOCK,GstAjaClockClass))
#define GST_IS_Aja_CLOCK(obj) \
(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AJA_CLOCK))
#define GST_IS_Aja_CLOCK_CLASS(klass) \
(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AJA_CLOCK))
#define GST_AJA_CLOCK_CAST(obj) \
((GstAjaClock*)(obj))

typedef struct _GstAjaClock GstAjaClock;
typedef struct _GstAjaClockClass GstAjaClockClass;

struct _GstAjaClock
{
    GstSystemClock clock;

    GstAjaInput *input;
    GstAjaOutput *output;
};

struct _GstAjaClockClass
{
    GstSystemClockClass parent_class;
};

GType gst_aja_clock_get_type (void);
static GstClock *gst_aja_clock_new (const gchar * name);


void    gst_set_aja_clock_and_element (GstElement * element, gboolean is_audio);
GstAjaInput *  gst_aja_acquire_input (gint deviceNum, gint channel, GstElement * src, gboolean is_audio, gboolean is_hevc);


#endif
