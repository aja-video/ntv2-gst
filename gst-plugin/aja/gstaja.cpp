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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstaja.h"
#include "gstajavideosrc.h"
#include "gstajahevcsrc.h"
#include "gstajavideosink.h"
#include "gstajaaudiosrc.h"
#include "gstajaaudiosink.h"

GST_DEBUG_CATEGORY_STATIC (gst_aja_debug);
#define GST_CAT_DEFAULT gst_aja_debug

typedef struct _Device Device;
struct _Device
{
    GstAjaOutput    output[NTV2_CHANNEL8];
    GstAjaInput     input[NTV2_CHANNEL8];
};

//static GOnce devices_once = G_ONCE_INIT;
//static int n_devices;
static Device devices[4];

GstAjaInput *
gst_aja_acquire_input (gint deviceNum, gint channel, GstElement * src, gboolean is_audio, gboolean is_hevc)
{
    GstAjaInput *input;
    
    input = &devices[deviceNum].input[channel];
    
    g_mutex_lock (&input->lock);

    input->ntv2AVHevc = new NTV2GstAVHevc("0", (NTV2Channel) channel);
    if (input->ntv2AVHevc == NULL)
    {
        GST_ERROR_OBJECT (src, "Failed to acquire input");
        g_mutex_unlock (&input->lock);
        return NULL;
    }
    
    input->clock = gst_aja_clock_new ("GstAjaInputClock");
    GST_AJA_CLOCK_CAST (input->clock)->input = input;

    if (is_audio && !input->audiosrc)
    {
        input->audiosrc = GST_ELEMENT_CAST (gst_object_ref (src));
        g_mutex_unlock (&input->lock);
        return input;
        
    } else if (!input->videosrc)
    {
        input->videosrc = GST_ELEMENT_CAST (gst_object_ref (src));
        g_mutex_unlock (&input->lock);
        return input;
    }
    g_mutex_unlock (&input->lock);
    
    GST_ERROR ("Input device %d (audio: %d) in use already", deviceNum, is_audio);
    return NULL;
}



#define NTSC    10, 11, false,  "bt601"
#define PAL     12, 11, true,   "bt601"
#define HD      1,  1,  false,  "bt709"
#define UHD     1,  1,  false,  "bt2020"


static const GstAjaMode modesRaw[] =
{
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_525_5994,          720,    486,    8,  30000,    1001, true,   true,    false,  NTSC},  // GST_AJA_MODE_RAW_NTSC_8_5994i
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_525_5994,          720,    486,    10, 30000,    1001, true,   true,    false,  NTSC},  // GST_AJA_MODE_RAW_NTSC_10_5994i
    
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_625_5000,          720,    486,    8,  25,       1,    true,   true,    false,  PAL},   // GST_AJA_MODE_RAW_PAL_8_50i
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_625_5000,          720,    486,    10, 25,       1,    true,   true,    false,  PAL},   // GST_AJA_MODE_RAW_PAL_10_50i
    
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_720p_5000,         1280,   720,    8,  50,       1,    false,  true,    false,  HD},    // GST_AJA_MODE_RAW_720_8_50p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_720p_5994,         1280,   720,    8,  60000,    1001, false,  true,    false,  HD},    // GST_AJA_MODE_RAW_720_8_5994p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_720p_6000,         1280,   720,    8,  60,       1,    false,  true,    false,  HD},    // GST_AJA_MODE_RAW_720_8_60p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_720p_5000,         1280,   720,    10, 50,       1,    false,  true,    false,  HD},    // GST_AJA_MODE_RAW_720_10_50p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_720p_5994,         1280,   720,    10, 60000,    1001, false,  true,    false,  HD},    // GST_AJA_MODE_RAW_720_10_5994p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_720p_6000,         1280,   720,    10, 60,       1,    false,  true,    false,  HD},    // GST_AJA_MODE_RAW_720_10_60p
    
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080i_5000,        1920,   1080,   8,  25,       1,    true,   true,    false,  HD},    // GST_AJA_MODE_RAW_1080_8_50i
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080p_5000_A,      1920,   1080,   8,  50,       1,    false,  true,    false,  HD},    // GST_AJA_MODE_RAW_1080_8_50p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080i_5994,        1920,   1080,   8,  30000,    1001, true,   true,    false,  HD},    // GST_AJA_MODE_RAW_1080_8_5994i
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080p_5994_A,      1920,   1080,   8,  60,       1,    false,  true,    false,  HD},    // GST_AJA_MODE_RAW_1080_8_5994p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080i_6000,        1920,   1080,   8,  30,       1,    true,   true,    false,  HD},    // GST_AJA_MODE_RAW_1080_8_60i
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080p_6000_A,      1920,   1080,   8,  60,       1,    false,  true,    false,  HD},    // GST_AJA_MODE_RAW_1080_8_60p
    
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080i_5000,        1920,   1080,   10, 25,       1,    true,   true,    false,  HD},    // GST_AJA_MODE_RAW_1080_10_50i
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080p_5000_A,      1920,   1080,   10, 50,       1,    false,  true,    false,  HD},    // GST_AJA_MODE_RAW_1080_10_50p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080i_5994,        1920,   1080,   10, 30000,    1001, true,   true,    false,  HD},    // GST_AJA_MODE_RAW_1080_10_5994i
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080p_5994_A,      1920,   1080,   10, 60,       1,    false,  true,    false,  HD},    // GST_AJA_MODE_RAW_1080_10_5994p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080i_6000,        1920,   1080,   10, 30,       1,    true,   true,    false,  HD},    // GST_AJA_MODE_RAW_1080_10_60i
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080p_6000_A,      1920,   1080,   10, 60,       1,    false,  true,    false,  HD},    // GST_AJA_MODE_RAW_1080_10_60p
    
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4x1920x1080p_5000, 3840,   2160,   8,  50,       1,    false,  true,    true,   UHD},   // GST_AJA_MODE_RAW_UHD_8_50p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4x1920x1080p_5994, 3840,   2160,   8,  60000,    1001, false,  true,    true,   UHD},   // GST_AJA_MODE_RAW_UHD_8_5994p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4x1920x1080p_6000, 3840,   2160,   8,  60,       1,    false,  true,    true,   UHD},   // GST_AJA_MODE_RAW_UHD_8_60p
    
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4x1920x1080p_5000, 3840,   2160,   10, 50,       1,    false,  true,    true,   UHD},   // GST_AJA_MODE_RAW_UHD_10_50p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4x1920x1080p_5994, 3840,   2160,   10, 60000,    1001, false,  true,    true,   UHD},   // GST_AJA_MODE_RAW_UHD_10_5994p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4x1920x1080p_6000, 3840,   2160,   10, 60,       1,    false,  true,    true,   UHD},   // GST_AJA_MODE_RAW_UHD_10_60p
};

GType
gst_aja_mode_get_type_raw (void)
{
    static gsize id = 0;
    static const GEnumValue modes[] =
    {
        {GST_AJA_MODE_RAW_NTSC_8_5994i,     "ntsc",             "NTSC 8Bit 59.94i     "},
        {GST_AJA_MODE_RAW_NTSC_10_5994i,    "ntsc-10",          "NTSC 10Bit 59.94i    "},
        
        {GST_AJA_MODE_RAW_PAL_8_50i,        "pal",              "PAL 8Bit 50i         "},
        {GST_AJA_MODE_RAW_PAL_10_50i,       "pal-10",           "PAL 10Bit 50i        "},
        
        {GST_AJA_MODE_RAW_720_8_50p,        "720p50",           "HD720 8Bit 50p       "},
        {GST_AJA_MODE_RAW_720_8_5994p,      "720p59.94",        "HD720 8Bit 59.94p    "},
        {GST_AJA_MODE_RAW_720_8_60p,        "720p60",           "HD720 8Bit 60p       "},
        {GST_AJA_MODE_RAW_720_10_50p,       "720p50-10",        "HD720 10Bit 50p      "},
        {GST_AJA_MODE_RAW_720_10_5994p,     "720p59.94-10",     "HD720 10Bit 59.94p   "},
        {GST_AJA_MODE_RAW_720_10_60p,       "720p60-10",        "HD720 10Bit 60p      "},
        
        {GST_AJA_MODE_RAW_1080_8_50i,       "1080i50",          "HD1080 8Bit 50i      "},
        {GST_AJA_MODE_RAW_1080_8_50p,       "1080p50",          "HD1080 8Bit 50p      "},
        {GST_AJA_MODE_RAW_1080_8_5994i,     "1080i59.94",       "HD1080 8Bit 59.94i   "},
        {GST_AJA_MODE_RAW_1080_8_5994p,     "1080p59.94",       "HD1080 8Bit 59.94p   "},
        {GST_AJA_MODE_RAW_1080_8_60i,       "1080i60",          "HD1080 8Bit 60i      "},
        {GST_AJA_MODE_RAW_1080_8_60p,       "1080p60",          "HD1080 8Bit 60p      "},
        
        {GST_AJA_MODE_RAW_1080_10_50i,      "1080i50-10",       "HD1080 10Bit 50i     "},
        {GST_AJA_MODE_RAW_1080_10_50p,      "1080p50-10",       "HD1080 10Bit 50p     "},
        {GST_AJA_MODE_RAW_1080_10_5994i,    "1080i59.94-10",    "HD1080 10Bit 59.94i  "},
        {GST_AJA_MODE_RAW_1080_10_5994p,    "1080p59.94-10",    "HD1080 10Bit 59.94p  "},
        {GST_AJA_MODE_RAW_1080_10_60i,      "1080i60-10",       "HD1080 10Bit 60i     "},
        {GST_AJA_MODE_RAW_1080_10_60p,      "1080p60-10",       "HD1080 10Bit 60p     "},
        
        {GST_AJA_MODE_RAW_UHD_8_50p,        "UHDp50",           "UHD3140 8Bit 50p     "},
        {GST_AJA_MODE_RAW_UHD_8_5994p,      "UHDp59.94",        "UHD3140 8Bit 59.94p  "},
        {GST_AJA_MODE_RAW_UHD_8_60p,        "UHDp60",           "UHD3140 8Bit 60p     "},
        
        {GST_AJA_MODE_RAW_UHD_10_50p,       "UHDp50-10",        "UHD3140 10Bit 50p    "},
        {GST_AJA_MODE_RAW_UHD_10_5994p,     "UHDp59.94-10",     "UHD3140 10Bit 59.94p "},
        {GST_AJA_MODE_RAW_UHD_10_60p,       "UHDp60-10",        "UHD3140 10Bit 60p    "},
        {0,                                 NULL,               NULL}
    };
    
    if (g_once_init_enter (&id))
    {
        GType tmp = g_enum_register_static ("GstAjaRawModes", modes);
        g_once_init_leave (&id, tmp);
    }
    
    return (GType) id;
}

const GstAjaMode *
gst_aja_get_mode_raw (GstAjaModeRawEnum e)
{
    if (e < GST_AJA_MODE_RAW_NTSC_8_5994i || e >= GST_AJA_MODE_RAW_END)
        return NULL;
    return &modesRaw[e];
}

static GstStructure *
gst_aja_mode_get_structure_raw (GstAjaModeRawEnum e)
{
    const GstAjaMode *mode = gst_aja_get_mode_raw (e);
    
    return gst_structure_new ("video/x-raw",
                              "format", G_TYPE_STRING, mode->bitDepth == 8 ? "UYVY" : "v210",
                              "width", G_TYPE_INT, mode->width,
                              "height", G_TYPE_INT, mode->height,
                              "framerate", GST_TYPE_FRACTION, mode->fps_n, mode->fps_d,
                              "interlace-mode", G_TYPE_STRING, mode->isInterlaced ? "interleaved" : "progressive",
                              "pixel-aspect-ratio", GST_TYPE_FRACTION, mode->par_n, mode->par_d,
                              "colorimetry", G_TYPE_STRING, mode->colorimetry,
                              "chroma-site", G_TYPE_STRING, "mpeg2",
                              NULL);
}

GstCaps *
gst_aja_mode_get_caps_raw (GstAjaModeRawEnum e)
{
    GstCaps *caps;
    
    caps = gst_caps_new_empty ();
    gst_caps_append_structure (caps, gst_aja_mode_get_structure_raw (e));
    
    return caps;
}

GstCaps *
gst_aja_mode_get_template_caps_raw (void)
{
    int i;
    GstCaps *caps;
    GstStructure *s;
    
    caps = gst_caps_new_empty ();
    for (i = 1; i < (int) G_N_ELEMENTS (modesRaw); i++)
    {
        s = gst_aja_mode_get_structure_raw ((GstAjaModeRawEnum) i);
        gst_caps_append_structure (caps, s);
    }
    
    return caps;
}

static const GstAjaMode modesHevc[] =
{
    {M31_FILE_720X480_420_8_5994i,       NTV2_FORMAT_525_5994,          720,    486,    8,  30000,    1001, true,   false,  false,  NTSC},  // GST_AJA_MODE_HEVC_NTSC_420_8_5994i
    {M31_FILE_720X480_422_10_5994i,      NTV2_FORMAT_525_5994,          720,    486,    10, 30000,    1001, true,   true,   false,  NTSC},  // GST_AJA_MODE_HEVC_NTSC_422_10_5994i

    {M31_FILE_720X576_420_8_50i,         NTV2_FORMAT_625_5000,          720,    486,    8,  25,       1,    true,   false,  false,  PAL},   // GST_AJA_MODE_HEVC_PAL_420_8_50i
    {M31_FILE_720X576_422_10_50i,        NTV2_FORMAT_625_5000,          720,    486,    10, 25,       1,    true,   true,   false,  PAL},   // GST_AJA_MODE_HEVC_PAL_422_10_50i

    {M31_FILE_1280X720_420_8_50p,        NTV2_FORMAT_720p_5000,         1280,   720,    8,  50,       1,    false,  false,  false,  HD},    // GST_AJA_MODE_HEVC_720_420_8_50p
    {M31_FILE_1280X720_420_8_5994p,      NTV2_FORMAT_720p_5994,         1280,   720,    8,  60000,    1001, false,  false,  false,  HD},    // GST_AJA_MODE_HEVC_720_420_8_5994p
    {M31_FILE_1280X720_420_8_60p,        NTV2_FORMAT_720p_6000,         1280,   720,    8,  60,       1,    false,  false,  false,  HD},    // GST_AJA_MODE_HEVC_720_420_8_60p
    {M31_FILE_1280X720_422_10_50p,       NTV2_FORMAT_720p_5000,         1280,   720,    10, 50,       1,    false,  true,   false,  HD},    // GST_AJA_MODE_HEVC_720_422_10_50p
    {M31_FILE_1280X720_422_10_5994p,     NTV2_FORMAT_720p_5994,         1280,   720,    10, 60000,    1001, false,  true,   false,  HD},    // GST_AJA_MODE_HEVC_720_422_10_5994p
    {M31_FILE_1280X720_422_10_60p,       NTV2_FORMAT_720p_6000,         1280,   720,    10, 60,       1,    false,  true,   false,  HD},    // GST_AJA_MODE_HEVC_720_422_10_60p

    {M31_FILE_1920X1080_420_8_50i,       NTV2_FORMAT_1080i_5000,        1920,   1080,   8,  25,       1,    true,   false,  false,  HD},    // GST_AJA_MODE_HEVC_1080_420_8_50i
    {M31_FILE_1920X1080_420_8_50p,       NTV2_FORMAT_1080p_5000_A,      1920,   1080,   8,  50,       1,    false,  false,  false,  HD},    // GST_AJA_MODE_HEVC_1080_420_8_50p
    {M31_FILE_1920X1080_420_8_5994i,     NTV2_FORMAT_1080i_5994,        1920,   1080,   8,  30000,    1001, true,   false,  false,  HD},    // GST_AJA_MODE_HEVC_1080_420_8_5994i
    {M31_FILE_1920X1080_420_8_5994p,     NTV2_FORMAT_1080p_5994_A,      1920,   1080,   8,  60,       1,    false,  false,  false,  HD},    // GST_AJA_MODE_HEVC_1080_420_8_5994p
    {M31_FILE_1920X1080_420_8_60i,       NTV2_FORMAT_1080i_6000,        1920,   1080,   8,  30,       1,    true,   false,  false,  HD},    // GST_AJA_MODE_HEVC_1080_420_8_60i
    {M31_FILE_1920X1080_420_8_60p,       NTV2_FORMAT_1080p_6000_A,      1920,   1080,   8,  60,       1,    false,  false,  false,  HD},    // GST_AJA_MODE_HEVC_1080_420_8_60p

    {M31_FILE_1920X1080_422_10_50i,      NTV2_FORMAT_1080i_5000,        1920,   1080,   10, 25,       1,    true,   true,   false,  HD},    // GST_AJA_MODE_HEVC_1080_422_10_50i
    {M31_FILE_1920X1080_422_10_50p,      NTV2_FORMAT_1080p_5000_A,      1920,   1080,   10, 50,       1,    false,  true,   false,  HD},    // GST_AJA_MODE_HEVC_1080_422_10_50p
    {M31_FILE_1920X1080_422_10_5994i,    NTV2_FORMAT_1080i_5994,        1920,   1080,   10, 30000,    1001, true,   true,   false,  HD},    // GST_AJA_MODE_HEVC_1080_422_10_5994i
    {M31_FILE_1920X1080_422_10_5994p,    NTV2_FORMAT_1080p_5994_A,      1920,   1080,   10, 60,       1,    false,  true,   false,  HD},    // GST_AJA_MODE_HEVC_1080_422_10_5994p
    {M31_FILE_1920X1080_422_10_60i,      NTV2_FORMAT_1080i_6000,        1920,   1080,   10, 30,       1,    true,   true,   false,  HD},    // GST_AJA_MODE_HEVC_1080_422_10_60i
    {M31_FILE_1920X1080_422_10_60p,      NTV2_FORMAT_1080p_6000_A,      1920,   1080,   10, 60,       1,    false,  true,   false,  HD},    // GST_AJA_MODE_HEVC_1080_422_10_60p

    {M31_FILE_3840X2160_420_8_50p,       NTV2_FORMAT_4x1920x1080p_5000, 3840,   2160,   8,  50,       1,    false,  false,  true,   UHD},   // GST_AJA_MODE_HEVC_UHD_420_8_50p
    {M31_FILE_3840X2160_420_8_5994p,     NTV2_FORMAT_4x1920x1080p_5994, 3840,   2160,   8,  60000,    1001, false,  false,  true,   UHD},   // GST_AJA_MODE_HEVC_UHD_420_8_5994p
    {M31_FILE_3840X2160_420_8_60p,       NTV2_FORMAT_4x1920x1080p_6000, 3840,   2160,   8,  60,       1,    false,  false,  true,   UHD},   // GST_AJA_MODE_HEVC_UHD_420_8_60p

    {M31_FILE_3840X2160_420_10_50p,      NTV2_FORMAT_4x1920x1080p_5000, 3840,   2160,   10, 50,       1,    false,  false,  true,   UHD},   // GST_AJA_MODE_HEVC_UHD_420_10_50p
    {M31_FILE_3840X2160_420_10_5994p,    NTV2_FORMAT_4x1920x1080p_5994, 3840,   2160,   10, 60000,    1001, false,  false,  true,   UHD},   // GST_AJA_MODE_HEVC_UHD_420_10_5994p
    {M31_FILE_3840X2160_420_10_60p,      NTV2_FORMAT_4x1920x1080p_6000, 3840,   2160,   10, 60,       1,    false,  false,  true,   UHD},   // GST_AJA_MODE_HEVC_UHD_420_10_60p

    {M31_FILE_3840X2160_422_10_50p,      NTV2_FORMAT_4x1920x1080p_5000, 3840,   2160,   10, 50,       1,    false,  true,   true,   UHD},   // GST_AJA_MODE_HEVC_UHD_422_10_50p
    {M31_FILE_3840X2160_422_10_5994p,    NTV2_FORMAT_4x1920x1080p_5994, 3840,   2160,   10, 60000,    1001, false,  true,   true,   UHD},   // GST_AJA_MODE_HEVC_UHD_422_10_5994p
    {M31_FILE_3840X2160_422_10_60p,      NTV2_FORMAT_4x1920x1080p_6000, 3840,   2160,   10, 60,       1,    false,  true,   true,   UHD},   // GST_AJA_MODE_HEVC_UHD_422_10_60p
};

GType
gst_aja_mode_get_type_hevc (void)
{
    static gsize id = 0;
    static const GEnumValue modes[] =
    {
        {GST_AJA_MODE_HEVC_NTSC_420_8_5994i,     "ntsc",             "NTSC 8Bit 420 59.94i      "},
        {GST_AJA_MODE_HEVC_NTSC_422_10_5994i,    "ntsc-10+",         "NTSC 10Bit 422 59.94i     "},
        
        {GST_AJA_MODE_HEVC_PAL_420_8_50i,        "pal",              "PAL 8Bit 420 50i          "},
        {GST_AJA_MODE_HEVC_PAL_422_10_50i,       "pal-10+",          "PAL 10Bit 422 50i         "},
        
        {GST_AJA_MODE_HEVC_720_420_8_50p,        "720p50",           "HD720 8Bit 420 50p        "},
        {GST_AJA_MODE_HEVC_720_420_8_5994p,      "720p59.94",        "HD720 8Bit 420 59.94p     "},
        {GST_AJA_MODE_HEVC_720_420_8_60p,        "720p60",           "HD720 8Bit 420 60p        "},
        {GST_AJA_MODE_HEVC_720_422_10_50p,       "720p50-10+",       "HD720 10Bit 422 50p       "},
        {GST_AJA_MODE_HEVC_720_422_10_5994p,     "720p59.94-10+",    "HD720 10Bit 422 59.94p    "},
        {GST_AJA_MODE_HEVC_720_422_10_60p,       "720p60-10+",       "HD720 10Bit 422 60p       "},
        
        {GST_AJA_MODE_HEVC_1080_420_8_50i,       "1080i50",          "HD1080 8Bit 420 50i       "},
        {GST_AJA_MODE_HEVC_1080_420_8_50p,       "1080p50",          "HD1080 8Bit 420 50p       "},
        {GST_AJA_MODE_HEVC_1080_420_8_5994i,     "1080i59.94",       "HD1080 8Bit 420 59.94i    "},
        {GST_AJA_MODE_HEVC_1080_420_8_5994p,     "1080p59.94",       "HD1080 8Bit 420 59.94p    "},
        {GST_AJA_MODE_HEVC_1080_420_8_60i,       "1080i60",          "HD1080 8Bit 420 60i       "},
        {GST_AJA_MODE_HEVC_1080_420_8_60p,       "1080p60",          "HD1080 8Bit 420 60p       "},
        
        {GST_AJA_MODE_HEVC_1080_422_10_50i,      "1080i50-10+",      "HD1080 10Bit 422 50i      "},
        {GST_AJA_MODE_HEVC_1080_422_10_50p,      "1080p50-10+",      "HD1080 10Bit 422 50p      "},
        {GST_AJA_MODE_HEVC_1080_422_10_5994i,    "1080i59.94-10+",   "HD1080 10Bit 422 59.94i   "},
        {GST_AJA_MODE_HEVC_1080_422_10_5994p,    "1080p59.94-10+",   "HD1080 10Bit 422 59.94p   "},
        {GST_AJA_MODE_HEVC_1080_422_10_60i,      "1080i60-10+",      "HD1080 10Bit 422 60i      "},
        {GST_AJA_MODE_HEVC_1080_422_10_60p,      "1080p60-10+",      "HD1080 10Bit 422 60p      "},
        
        {GST_AJA_MODE_HEVC_UHD_420_8_50p,        "UHDp50",           "UHD3140 8Bit 420 50p      "},
        {GST_AJA_MODE_HEVC_UHD_420_8_5994p,      "UHDp59.94",        "UHD3140 8Bit 420 59.94p   "},
        {GST_AJA_MODE_HEVC_UHD_420_8_60p,        "UHDp60",           "UHD3140 8Bit 420 60p      "},
        
        {GST_AJA_MODE_HEVC_UHD_420_10_50p,       "UHDp50-10",        "UHD3140 10Bit 420 50p     "},
        {GST_AJA_MODE_HEVC_UHD_420_10_5994p,     "UHDp59.94-10",     "UHD3140 10Bit 420 59.94p  "},
        {GST_AJA_MODE_HEVC_UHD_420_10_60p,       "UHDp60-10",        "UHD3140 10Bit 420 60p     "},
        
        {GST_AJA_MODE_HEVC_UHD_422_10_50p,       "UHDp50-10+",       "UHD3140 10Bit 422 50p     "},
        {GST_AJA_MODE_HEVC_UHD_422_10_5994p,     "UHDp59.94-10+",    "UHD3140 10Bit 422 59.94p  "},
        {GST_AJA_MODE_HEVC_UHD_422_10_60p,       "UHDp60-10+",       "UHD3140 10Bit 422 60p     "},
        {0,                                 NULL,               NULL}
    };
    
    if (g_once_init_enter (&id))
    {
        GType tmp = g_enum_register_static ("GstAjaHevcModes", modes);
        g_once_init_leave (&id, tmp);
    }
    
    return (GType) id;
}

const GstAjaMode *
gst_aja_get_mode_hevc (GstAjaModeHevcEnum e)
{
    if (e < GST_AJA_MODE_HEVC_NTSC_420_8_5994i || e >= GST_AJA_MODE_HEVC_END)
        return NULL;
    return &modesHevc[e];
}


G_DEFINE_TYPE (GstAjaClock, gst_aja_clock, GST_TYPE_SYSTEM_CLOCK);

static GstClockTime gst_aja_clock_get_internal_time (GstClock * clock);

static void
gst_aja_clock_class_init (GstAjaClockClass * klass)
{
    GstClockClass *clock_class = (GstClockClass *) klass;
    
    clock_class->get_internal_time = gst_aja_clock_get_internal_time;
}

static void
gst_aja_clock_init (GstAjaClock * clock)
{
    GST_OBJECT_FLAG_SET (clock, GST_CLOCK_FLAG_CAN_SET_MASTER);
}

static GstClock *
gst_aja_clock_new (const gchar * name)
{
    GstAjaClock *self =
    GST_AJA_CLOCK (g_object_new (GST_TYPE_AJA_CLOCK, "name", name,
                                      "clock-type", GST_CLOCK_TYPE_OTHER, NULL));
    
    return GST_CLOCK_CAST (self);
}

static GstClockTime
gst_aja_clock_get_internal_time (GstClock * clock)
{
    GstAjaClock *self = GST_AJA_CLOCK (clock);
    GstClockTime result, start_time, last_time;
    GstClockTimeDiff offset;
    uint64_t time;
    bool status;
    
    if (self->input != NULL)
    {
        g_mutex_lock (&self->input->lock);
        start_time = self->input->clock_start_time;
        offset = self->input->clock_offset;
        last_time = self->input->clock_last_time;
        time = -1;
        if (!self->input->started)
        {
            result = last_time;
        }
        else
        {
            status = self->input->ntv2AVHevc->GetHardwareClock(GST_SECOND, &time);
            if (status)
            {
                result = time;
                if (start_time == GST_CLOCK_TIME_NONE)
                    start_time = self->input->clock_start_time = result;
                
                if (result > start_time)
                    result -= start_time;
                else
                    result = 0;
                
                if (self->input->clock_restart)
                {
                    self->input->clock_offset = result - last_time;
                    offset = self->input->clock_offset;
                    self->input->clock_restart = FALSE;
                }
                result = MAX (last_time, result);
                result -= offset;
                result = MAX (last_time, result);
            }
            else
            {
                result = last_time;
            }
            //printf("time diff %ld\n", (result-self->input->clock_last_time)/1000);
            self->input->clock_last_time = result;
        }
        result += self->input->clock_epoch;
        g_mutex_unlock (&self->input->lock);
    }
    else if (self->output != NULL)
    {
        g_mutex_lock (&self->output->lock);
        start_time = self->output->clock_start_time;
        offset = self->output->clock_offset;
        last_time = self->output->clock_last_time;
        time = -1;
        if (!self->output->started)
        {
            result = last_time;
        }
        else
        {
            status = self->input->ntv2AVHevc->GetHardwareClock(GST_SECOND, &time);
            if (status)
            {
                result = time;
                
                if (start_time == GST_CLOCK_TIME_NONE)
                    start_time = self->output->clock_start_time = result;
                
                if (result > start_time)
                    result -= start_time;
                else
                    result = 0;
                
                if (self->output->clock_restart)
                {
                    self->output->clock_offset = result - last_time;
                    offset = self->output->clock_offset;
                    self->output->clock_restart = FALSE;
                }
                result = MAX (last_time, result);
                result -= offset;
                result = MAX (last_time, result);
            }
            else
            {
                result = last_time;
            }
            
            self->output->clock_last_time = result;
        }
        result += self->output->clock_epoch;
        g_mutex_unlock (&self->output->lock);
    }
    else
    {
        g_assert_not_reached ();
    }

    #if 1
    GST_LOG_OBJECT (clock,
                    "result %" GST_TIME_FORMAT " time %" GST_TIME_FORMAT " last time %"
                    GST_TIME_FORMAT " offset %" GST_TIME_FORMAT " start time %"
                    GST_TIME_FORMAT, GST_TIME_ARGS (result),
                    GST_TIME_ARGS (time), GST_TIME_ARGS (last_time), GST_TIME_ARGS (offset),
                    GST_TIME_ARGS (start_time));
   #endif
    
    return result;
}

#ifndef VERSION
#define VERSION "0.0.1"
#endif
#ifndef PACKAGE
#define PACKAGE "aja_plugin_package"
#endif
#ifndef PACKAGE_NAME
#define PACKAGE_NAME "aja_plugin_package"
#endif
#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "Unknown package origin"
#endif

static gboolean
aja_init (GstPlugin * plugin)
{
 	GST_DEBUG_CATEGORY_INIT (gst_aja_debug, "aja", 0, "debug category for aja plugin");

    memset (devices, 0x0, sizeof (devices));

	gst_element_register (plugin, "ajavideosrc", GST_RANK_NONE, GST_TYPE_AJA_VIDEO_SRC);
    gst_element_register (plugin, "ajahevcsrc", GST_RANK_NONE, GST_TYPE_AJA_HEVC_SRC);
  	gst_element_register (plugin, "ajaaudiosrc", GST_RANK_NONE, GST_TYPE_AJA_AUDIO_SRC);

    // These don't work yet so lets not register them
  	//gst_element_register (plugin, "ajavideosink", GST_RANK_NONE, GST_TYPE_AJA_VIDEO_SINK);
  	//gst_element_register (plugin, "ajaaudiosink", GST_RANK_NONE, GST_TYPE_AJA_AUDIO_SINK);

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    aja,
    "AJA Video plugin",
    aja_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)
