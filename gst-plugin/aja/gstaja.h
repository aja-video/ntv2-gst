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

#ifndef _GST_AJA_H_
#define _GST_AJA_H_

#include <stdio.h>

#include <gst/gst.h>
#include <gst/base/base.h>
#include "gstntv2.h"

#include "ntv2enums.h"
#include "ntv2m31enums.h"

#if ENABLE_NVMM
#include "gstnvdsbufferpool.h"
#endif

typedef enum
{
    GST_AJA_MODE_RAW_NTSC_8_2398i,
    GST_AJA_MODE_RAW_NTSC_8_24i,
    GST_AJA_MODE_RAW_NTSC_8_5994i,
    GST_AJA_MODE_RAW_NTSC_8_RGBA_2398i,
    GST_AJA_MODE_RAW_NTSC_8_RGBA_24i,
    GST_AJA_MODE_RAW_NTSC_8_RGBA_5994i,
    GST_AJA_MODE_RAW_NTSC_10_2398i,
    GST_AJA_MODE_RAW_NTSC_10_24i,
    GST_AJA_MODE_RAW_NTSC_10_5994i,
    
    GST_AJA_MODE_RAW_PAL_8_50i,
    GST_AJA_MODE_RAW_PAL_8_RGBA_50i,
    GST_AJA_MODE_RAW_PAL_10_50i,
    
    GST_AJA_MODE_RAW_720_8_2398p,
    GST_AJA_MODE_RAW_720_8_25p,
    GST_AJA_MODE_RAW_720_8_50p,
    GST_AJA_MODE_RAW_720_8_5994p,
    GST_AJA_MODE_RAW_720_8_60p,
    GST_AJA_MODE_RAW_720_8_RGBA_2398p,
    GST_AJA_MODE_RAW_720_8_RGBA_25p,
    GST_AJA_MODE_RAW_720_8_RGBA_50p,
    GST_AJA_MODE_RAW_720_8_RGBA_5994p,
    GST_AJA_MODE_RAW_720_8_RGBA_60p,
    GST_AJA_MODE_RAW_720_10_2398p,
    GST_AJA_MODE_RAW_720_10_25p,
    GST_AJA_MODE_RAW_720_10_50p,
    GST_AJA_MODE_RAW_720_10_5994p,
    GST_AJA_MODE_RAW_720_10_60p,
    
    GST_AJA_MODE_RAW_1080_8_2398p,
    GST_AJA_MODE_RAW_1080_8_24p,
    GST_AJA_MODE_RAW_1080_8_25p,
    GST_AJA_MODE_RAW_1080_8_2997p,
    GST_AJA_MODE_RAW_1080_8_30p,
    GST_AJA_MODE_RAW_1080_8_50i,
    GST_AJA_MODE_RAW_1080_8_50p,
    GST_AJA_MODE_RAW_1080_8_5994i,
    GST_AJA_MODE_RAW_1080_8_5994p,
    GST_AJA_MODE_RAW_1080_8_60i,
    GST_AJA_MODE_RAW_1080_8_60p,

    GST_AJA_MODE_RAW_1080_8_RGBA_2398p,
    GST_AJA_MODE_RAW_1080_8_RGBA_24p,
    GST_AJA_MODE_RAW_1080_8_RGBA_25p,
    GST_AJA_MODE_RAW_1080_8_RGBA_2997p,
    GST_AJA_MODE_RAW_1080_8_RGBA_30p,
    GST_AJA_MODE_RAW_1080_8_RGBA_50i,
    GST_AJA_MODE_RAW_1080_8_RGBA_50p,
    GST_AJA_MODE_RAW_1080_8_RGBA_5994i,
    GST_AJA_MODE_RAW_1080_8_RGBA_5994p,
    GST_AJA_MODE_RAW_1080_8_RGBA_60i,
    GST_AJA_MODE_RAW_1080_8_RGBA_60p,
    
    GST_AJA_MODE_RAW_1080_10_2398p,
    GST_AJA_MODE_RAW_1080_10_24p,
    GST_AJA_MODE_RAW_1080_10_25p,
    GST_AJA_MODE_RAW_1080_10_2997p,
    GST_AJA_MODE_RAW_1080_10_30p,
    GST_AJA_MODE_RAW_1080_10_50i,
    GST_AJA_MODE_RAW_1080_10_50p,
    GST_AJA_MODE_RAW_1080_10_5994i,
    GST_AJA_MODE_RAW_1080_10_5994p,
    GST_AJA_MODE_RAW_1080_10_60i,
    GST_AJA_MODE_RAW_1080_10_60p,
    
    GST_AJA_MODE_RAW_UHD_8_2398p,
    GST_AJA_MODE_RAW_UHD_8_24p,
    GST_AJA_MODE_RAW_UHD_8_25p,
    GST_AJA_MODE_RAW_UHD_8_2997p,
    GST_AJA_MODE_RAW_UHD_8_30p,
    GST_AJA_MODE_RAW_UHD_8_50p,
    GST_AJA_MODE_RAW_UHD_8_5994p,
    GST_AJA_MODE_RAW_UHD_8_60p,

    GST_AJA_MODE_RAW_UHD_8_RGBA_2398p,
    GST_AJA_MODE_RAW_UHD_8_RGBA_24p,
    GST_AJA_MODE_RAW_UHD_8_RGBA_25p,
    GST_AJA_MODE_RAW_UHD_8_RGBA_2997p,
    GST_AJA_MODE_RAW_UHD_8_RGBA_30p,
    GST_AJA_MODE_RAW_UHD_8_RGBA_50p,
    GST_AJA_MODE_RAW_UHD_8_RGBA_5994p,
    GST_AJA_MODE_RAW_UHD_8_RGBA_60p,
    
    GST_AJA_MODE_RAW_UHD_10_2398p,
    GST_AJA_MODE_RAW_UHD_10_24p,
    GST_AJA_MODE_RAW_UHD_10_25p,
    GST_AJA_MODE_RAW_UHD_10_2997p,
    GST_AJA_MODE_RAW_UHD_10_30p,
    GST_AJA_MODE_RAW_UHD_10_50p,
    GST_AJA_MODE_RAW_UHD_10_5994p,
    GST_AJA_MODE_RAW_UHD_10_60p,
    
    GST_AJA_MODE_RAW_4K_8_2398p,
    GST_AJA_MODE_RAW_4K_8_24p,
    GST_AJA_MODE_RAW_4K_8_25p,
    GST_AJA_MODE_RAW_4K_8_2997p,
    GST_AJA_MODE_RAW_4K_8_30p,
    GST_AJA_MODE_RAW_4K_8_4795p,
    GST_AJA_MODE_RAW_4K_8_48p,
    GST_AJA_MODE_RAW_4K_8_50p,
    GST_AJA_MODE_RAW_4K_8_5994p,
    GST_AJA_MODE_RAW_4K_8_60p,
    GST_AJA_MODE_RAW_4K_8_11988p,
    GST_AJA_MODE_RAW_4K_8_120p,
    
    GST_AJA_MODE_RAW_4K_8_RGBA_2398p,
    GST_AJA_MODE_RAW_4K_8_RGBA_24p,
    GST_AJA_MODE_RAW_4K_8_RGBA_25p,
    GST_AJA_MODE_RAW_4K_8_RGBA_2997p,
    GST_AJA_MODE_RAW_4K_8_RGBA_30p,
    GST_AJA_MODE_RAW_4K_8_RGBA_4795p,
    GST_AJA_MODE_RAW_4K_8_RGBA_48p,
    GST_AJA_MODE_RAW_4K_8_RGBA_50p,
    GST_AJA_MODE_RAW_4K_8_RGBA_5994p,
    GST_AJA_MODE_RAW_4K_8_RGBA_60p,
    GST_AJA_MODE_RAW_4K_8_RGBA_11988p,
    GST_AJA_MODE_RAW_4K_8_RGBA_120p,
    
    GST_AJA_MODE_RAW_4K_10_2398p,
    GST_AJA_MODE_RAW_4K_10_24p,
    GST_AJA_MODE_RAW_4K_10_25p,
    GST_AJA_MODE_RAW_4K_10_2997p,
    GST_AJA_MODE_RAW_4K_10_30p,
    GST_AJA_MODE_RAW_4K_10_4795p,
    GST_AJA_MODE_RAW_4K_10_48p,
    GST_AJA_MODE_RAW_4K_10_50p,
    GST_AJA_MODE_RAW_4K_10_5994p,
    GST_AJA_MODE_RAW_4K_10_60p,
    GST_AJA_MODE_RAW_4K_10_11988p,
    GST_AJA_MODE_RAW_4K_10_120p,

    GST_AJA_MODE_RAW_UHD2_8_2398p,
    GST_AJA_MODE_RAW_UHD2_8_24p,
    GST_AJA_MODE_RAW_UHD2_8_25p,
    GST_AJA_MODE_RAW_UHD2_8_2997p,
    GST_AJA_MODE_RAW_UHD2_8_30p,
    GST_AJA_MODE_RAW_UHD2_8_50p,
    GST_AJA_MODE_RAW_UHD2_8_5994p,
    GST_AJA_MODE_RAW_UHD2_8_60p,
    
    GST_AJA_MODE_RAW_UHD2_10_2398p,
    GST_AJA_MODE_RAW_UHD2_10_24p,
    GST_AJA_MODE_RAW_UHD2_10_25p,
    GST_AJA_MODE_RAW_UHD2_10_2997p,
    GST_AJA_MODE_RAW_UHD2_10_30p,
    GST_AJA_MODE_RAW_UHD2_10_50p,
    GST_AJA_MODE_RAW_UHD2_10_5994p,
    GST_AJA_MODE_RAW_UHD2_10_60p,
    
    GST_AJA_MODE_RAW_8K_8_2398p,
    GST_AJA_MODE_RAW_8K_8_24p,
    GST_AJA_MODE_RAW_8K_8_25p,
    GST_AJA_MODE_RAW_8K_8_2997p,
    GST_AJA_MODE_RAW_8K_8_30p,
    GST_AJA_MODE_RAW_8K_8_4795p,
    GST_AJA_MODE_RAW_8K_8_48p,
    GST_AJA_MODE_RAW_8K_8_50p,
    GST_AJA_MODE_RAW_8K_8_5994p,
    GST_AJA_MODE_RAW_8K_8_60p,
    
    GST_AJA_MODE_RAW_8K_10_2398p,
    GST_AJA_MODE_RAW_8K_10_24p,
    GST_AJA_MODE_RAW_8K_10_25p,
    GST_AJA_MODE_RAW_8K_10_2997p,
    GST_AJA_MODE_RAW_8K_10_30p,
    GST_AJA_MODE_RAW_8K_10_4795p,
    GST_AJA_MODE_RAW_8K_10_48p,
    GST_AJA_MODE_RAW_8K_10_50p,
    GST_AJA_MODE_RAW_8K_10_5994p,
    GST_AJA_MODE_RAW_8K_10_60p,
    
    GST_AJA_MODE_RAW_END
    
} GstAjaModeRawEnum;

#define GST_TYPE_AJA_MODE_RAW (gst_aja_mode_get_type_raw ())
GType gst_aja_mode_get_type_raw (void);

typedef enum {
  GST_AJA_VIDEO_INPUT_MODE_SDI,
  GST_AJA_VIDEO_INPUT_MODE_HDMI,
  GST_AJA_VIDEO_INPUT_MODE_ANALOG,
} GstAjaVideoInputMode;

#define GST_TYPE_AJA_VIDEO_INPUT_MODE (gst_aja_video_input_mode_get_type ())
GType gst_aja_video_input_mode_get_type (void);

#define GST_TYPE_AJA_SDI_INPUT_MODE (gst_aja_sdi_input_mode_get_type ())
GType gst_aja_sdi_input_mode_get_type (void);

typedef enum {
  GST_AJA_TIMECODE_MODE_VITC1,
  GST_AJA_TIMECODE_MODE_VITC2,
  GST_AJA_TIMECODE_MODE_ANALOG_LTC1,
  GST_AJA_TIMECODE_MODE_ANALOG_LTC2,
  GST_AJA_TIMECODE_MODE_ATC_LTC,
} GstAjaTimecodeMode;

#define GST_TYPE_AJA_TIMECODE_MODE (gst_aja_timecode_mode_get_type ())
GType gst_aja_timecode_mode_get_type (void);

typedef enum {
  GST_AJA_AUDIO_INPUT_MODE_EMBEDDED,
  GST_AJA_AUDIO_INPUT_MODE_HDMI,
  GST_AJA_AUDIO_INPUT_MODE_AES,
  GST_AJA_AUDIO_INPUT_MODE_ANALOG,
} GstAjaAudioInputMode;

#define GST_TYPE_AJA_AUDIO_INPUT_MODE (gst_aja_audio_input_mode_get_type ())
GType gst_aja_audio_input_mode_get_type (void);

// Used to keep track of engine when shared between audio and video
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
    NTV2VideoFormat         videoFormat;
    int                     width;
    int                     height;
    int                     bitDepth;
    gboolean                isRGBA;
    int                     fps_n;
    int                     fps_d;
    gboolean                isInterlaced;
    gboolean                is422;
    int                     par_n;
    int                     par_d;
    gboolean                isTff;
    const gchar             *colorimetry;
};


const GstAjaMode * gst_aja_get_mode_raw (GstAjaModeRawEnum e);

GstCaps * gst_aja_mode_get_caps_raw (GstAjaModeRawEnum e, gboolean isNvmm);
GstCaps * gst_aja_mode_get_template_caps_raw (void);

typedef struct _GstAjaOutput GstAjaOutput;
struct _GstAjaOutput
{
    NTV2GstAV           *ntv2AV;
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
    NTV2GstAV           *ntv2AV;
    NTV2EngineState     ntv2EngineState;
    const GstAjaMode    *mode;

    gboolean            started;
    
    GMutex              lock;
    
    GstElement          *audiosrc;
    gboolean            audio_enabled;
    GstElement          *videosrc;
    gboolean            video_enabled;
    void (*start_streams) (GstElement *videosrc);
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

    GstAjaOutput *output;
};

struct _GstAjaClockClass
{
    GstSystemClockClass parent_class;
};

GType gst_aja_clock_get_type (void);

GstAjaInput *  gst_aja_acquire_input (const gchar * deviceIdentifier, gint channel, GstElement * src, gboolean is_audio);

#define GST_TYPE_AJA_BUFFER_POOL \
(gst_aja_buffer_pool_get_type())
#define GST_AJA_BUFFER_POOL(obj) \
(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AJA_BUFFER_POOL,GstAjaBufferPool))
#define GST_AJA_BUFFER_POOL_CLASS(klass) \
(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AJA_BUFFER_POOL,GstAjaBufferPoolClass))
#define GST_IS_Aja_BUFFER_POOL(obj) \
(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AJA_BUFFER_POOL))
#define GST_IS_Aja_BUFFER_POOL_CLASS(klass) \
(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AJA_BUFFER_POOL))
#define GST_AJA_BUFFER_POOL_CAST(obj) \
((GstAjaBufferPool*)(obj))

typedef struct _GstAjaBufferPool GstAjaBufferPool;
typedef struct _GstAjaBufferPoolClass GstAjaBufferPoolClass;

struct _GstAjaBufferPool
{
    GstBufferPool buffer_pool;

    gboolean is_video;
    guint size;
};

struct _GstAjaBufferPoolClass
{
    GstBufferPoolClass parent_class;
};

GType gst_aja_buffer_pool_get_type (void);
GstBufferPool * gst_aja_buffer_pool_new (void);
AjaVideoBuff * gst_aja_buffer_get_video_buff (GstBuffer * buffer);
AjaAudioBuff * gst_aja_buffer_get_audio_buff (GstBuffer * buffer);

#define GST_TYPE_AJA_ALLOCATOR \
(gst_aja_allocator_get_type())
#define GST_AJA_ALLOCATOR(obj) \
(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AJA_ALLOCATOR,GstAjaAllocator))
#define GST_AJA_ALLOCATOR_CLASS(klass) \
(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AJA_ALLOCATOR,GstAjaAllocatorClass))
#define GST_IS_Aja_ALLOCATOR(obj) \
(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AJA_ALLOCATOR))
#define GST_IS_Aja_ALLOCATOR_CLASS(klass) \
(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AJA_ALLOCATOR))
#define GST_AJA_ALLOCATOR_CAST(obj) \
((GstAjaAllocator*)(obj))

typedef struct _GstAjaAllocator GstAjaAllocator;
typedef struct _GstAjaAllocatorClass GstAjaAllocatorClass;

struct _GstAjaAllocator
{
    GstAllocator allocator;

    CNTV2Card *device;
    gsize alloc_size;
    guint num_prealloc, num_allocated;
    GstQueueArray *free_list;
};

struct _GstAjaAllocatorClass
{
    GstAllocatorClass parent_class;
};

GType gst_aja_allocator_get_type (void);
GstAllocator * gst_aja_allocator_new (CNTV2Card *device, gsize alloc_size, guint num_prealloc);


#if ENABLE_NVMM

#define GST_TYPE_AJA_NVMM_BUFFER_POOL \
    (gst_aja_nvmm_buffer_pool_get_type())
#define GST_IS_AJA_NVMM_BUFFER_POOL(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_AJA_NVMM_BUFFER_POOL))
#define GST_AJA_NVMM_BUFFER_POOL(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_AJA_NVMM_BUFFER_POOL, GstAjaNvmmBufferPool))
#define GST_AJA_NVMM_BUFFER_POOL_CAST(obj) \
    ((GstAjaNvmmBufferPool*)(obj))

typedef struct _GstAjaNvmmBufferPool GstAjaNvmmBufferPool;
typedef struct _GstAjaNvmmBufferPoolClass GstAjaNvmmBufferPoolClass;

struct _GstAjaNvmmBufferPool
{
    // The NvmmBufferPool extends the NvDsBufferPool in order to allocate NVMM buffers.
    GstNvDsBufferPool nvds_pool;
};

struct _GstAjaNvmmBufferPoolClass
{
    GstBufferPoolClass parent_class;
};

GType gst_aja_nvmm_buffer_pool_get_type (void);
GstBufferPool * gst_aja_nvmm_buffer_pool_new (void);

#endif // ENABLE_NVMM

#endif
