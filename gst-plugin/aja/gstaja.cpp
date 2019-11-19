/* GStreamer
 * Copyright (C) 2015 PSM <philm@aja.com>
 * Copyright (C) 2017 Sebastian Dröge <sebastian@centricular.com>
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
//#include "gstajahevcsrc.h"
#include "gstajavideosink.h"
#include "gstajaaudiosrc.h"
#include "gstajaaudiosink.h"
#include "gstajadeviceprovider.h"

GST_DEBUG_CATEGORY_STATIC (gst_aja_debug);
#define GST_CAT_DEFAULT gst_aja_debug

typedef struct _Device Device;
struct _Device
{
  GstAjaOutput output[NTV2_MAX_NUM_CHANNELS];
  GstAjaInput input[NTV2_MAX_NUM_CHANNELS];
};

G_LOCK_DEFINE_STATIC (devices);
static GHashTable *devices;

GstAjaInput *
gst_aja_acquire_input (const gchar * inDeviceSpecifier, gint channel,
    GstElement * src, gboolean is_audio, gboolean is_hevc)
{
  GstAjaInput *input;

  g_return_val_if_fail (channel >= 0 && channel < NTV2_MAX_NUM_CHANNELS, NULL);

  G_LOCK (devices);
  if (!devices)
    devices = g_hash_table_new (g_str_hash, g_str_equal);
  Device *device = (Device *) g_hash_table_lookup (devices, inDeviceSpecifier);
  if (!device) {
    device = g_new0 (Device, 1);
    g_hash_table_insert (devices, g_strdup (inDeviceSpecifier),
        (gpointer) device);
  }
  input = &device->input[channel];

  g_mutex_lock (&input->lock);

  if (input->ntv2AVHevc == NULL) {
    // FIXME: Make this configurable
    input->ntv2AVHevc =
        new NTV2GstAV (std::string (inDeviceSpecifier), (NTV2Channel) channel);
  }

  if (is_audio && !input->audiosrc) {
    input->audiosrc = GST_ELEMENT_CAST (gst_object_ref (src));
    g_mutex_unlock (&input->lock);
    G_UNLOCK (devices);
    return input;
  } else if (!input->videosrc) {
    input->videosrc = GST_ELEMENT_CAST (gst_object_ref (src));
    g_mutex_unlock (&input->lock);
    G_UNLOCK (devices);
    return input;
  }
  g_mutex_unlock (&input->lock);
  G_UNLOCK (devices);

  GST_ERROR ("Input device %s (audio: %d) in use already", inDeviceSpecifier,
      is_audio);
  return NULL;
}

// *INDENT-OFF*
#define NTSC    10, 11, false,  "bt601"
#define PAL     12, 11, true,   "bt601"
#define HD      1,  1,  true,   "bt709"
#define UHD     1,  1,  true,   "bt2020"

static const GstAjaMode modesRaw[GST_AJA_MODE_RAW_END] =
{
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_525_2398,          720,    486,    8,  24000,    1001, true,   true,    NTSC},  // GST_AJA_MODE_RAW_NTSC_8_2398i
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_525_2400,          720,    486,    8,  24,       1,    true,   true,    NTSC},  // GST_AJA_MODE_RAW_NTSC_8_24i
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_525_5994,          720,    486,    8,  30000,    1001, true,   true,    NTSC},  // GST_AJA_MODE_RAW_NTSC_8_5994i
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_525_2398,          720,    486,    10, 24000,    1001, true,   true,    NTSC},  // GST_AJA_MODE_RAW_NTSC_10_2398i
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_525_2400,          720,    486,    10, 24,       1,    true,   true,    NTSC},  // GST_AJA_MODE_RAW_NTSC_10_24i
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_525_5994,          720,    486,    10, 30000,    1001, true,   true,    NTSC},  // GST_AJA_MODE_RAW_NTSC_10_5994i
    
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_625_5000,          720,    486,    8,  25,       1,    true,   true,    PAL},   // GST_AJA_MODE_RAW_PAL_8_50i
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_625_5000,          720,    486,    10, 25,       1,    true,   true,    PAL},   // GST_AJA_MODE_RAW_PAL_10_50i
    
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_720p_2398,         1280,   720,    8,  24000,    1001, false,  true,    HD},    // GST_AJA_MODE_RAW_720_8_2398p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_720p_2500,         1280,   720,    8,  25,       1,    false,  true,    HD},    // GST_AJA_MODE_RAW_720_8_25p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_720p_5000,         1280,   720,    8,  50,       1,    false,  true,    HD},    // GST_AJA_MODE_RAW_720_8_50p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_720p_5994,         1280,   720,    8,  60000,    1001, false,  true,    HD},    // GST_AJA_MODE_RAW_720_8_5994p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_720p_6000,         1280,   720,    8,  60,       1,    false,  true,    HD},    // GST_AJA_MODE_RAW_720_8_60p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_720p_2398,         1280,   720,    10, 2400,     1001, false,  true,    HD},    // GST_AJA_MODE_RAW_720_10_2398p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_720p_2500,         1280,   720,    10, 25,       1,    false,  true,    HD},    // GST_AJA_MODE_RAW_720_10_25p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_720p_5000,         1280,   720,    10, 50,       1,    false,  true,    HD},    // GST_AJA_MODE_RAW_720_10_50p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_720p_5994,         1280,   720,    10, 60000,    1001, false,  true,    HD},    // GST_AJA_MODE_RAW_720_10_5994p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_720p_6000,         1280,   720,    10, 60,       1,    false,  true,    HD},    // GST_AJA_MODE_RAW_720_10_60p
    
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080p_2398,        1920,   1080,   8,  24000,    1001, false,  true,    HD},    // GST_AJA_MODE_RAW_1080_8_2398p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080p_2400,        1920,   1080,   8,  24,       1,    false,  true,    HD},    // GST_AJA_MODE_RAW_1080_8_24p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080p_2500,        1920,   1080,   8,  25,       1,    false,  true,    HD},    // GST_AJA_MODE_RAW_1080_8_25p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080p_2997,        1920,   1080,   8,  30000,    1001, false,  true,    HD},    // GST_AJA_MODE_RAW_1080_8_2997p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080p_3000,        1920,   1080,   8,  30,       1,    false,  true,    HD},    // GST_AJA_MODE_RAW_1080_8_30p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080i_5000,        1920,   1080,   8,  25,       1,    true,   true,    HD},    // GST_AJA_MODE_RAW_1080_8_50i
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080p_5000_A,      1920,   1080,   8,  50,       1,    false,  true,    HD},    // GST_AJA_MODE_RAW_1080_8_50p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080i_5994,        1920,   1080,   8,  30000,    1001, true,   true,    HD},    // GST_AJA_MODE_RAW_1080_8_5994i
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080p_5994_A,      1920,   1080,   8,  60000,    1001, false,  true,    HD},    // GST_AJA_MODE_RAW_1080_8_5994p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080i_6000,        1920,   1080,   8,  30,       1,    true,   true,    HD},    // GST_AJA_MODE_RAW_1080_8_60i
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080p_6000_A,      1920,   1080,   8,  60,       1,    false,  true,    HD},    // GST_AJA_MODE_RAW_1080_8_60p
    
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080p_2398,        1920,   1080,   10, 24000,    1001, false,  true,    HD},    // GST_AJA_MODE_RAW_1080_10_2398p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080p_2400,        1920,   1080,   10, 24,       1,    false,  true,    HD},    // GST_AJA_MODE_RAW_1080_10_24p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080p_2500,        1920,   1080,   10, 25,       1,    false,  true,    HD},    // GST_AJA_MODE_RAW_1080_10_25p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080p_2997,        1920,   1080,   10, 30000,    1001, false,  true,    HD},    // GST_AJA_MODE_RAW_1080_10_2997p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080p_3000,        1920,   1080,   10, 30,       1,    false,  true,    HD},    // GST_AJA_MODE_RAW_1080_10_30p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080i_5000,        1920,   1080,   10, 25,       1,    true,   true,    HD},    // GST_AJA_MODE_RAW_1080_10_50i
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080p_5000_A,      1920,   1080,   10, 50,       1,    false,  true,    HD},    // GST_AJA_MODE_RAW_1080_10_50p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080i_5994,        1920,   1080,   10, 30000,    1001, true,   true,    HD},    // GST_AJA_MODE_RAW_1080_10_5994i
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080p_5994_A,      1920,   1080,   10, 60000,    1001, false,  true,    HD},    // GST_AJA_MODE_RAW_1080_10_5994p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080i_6000,        1920,   1080,   10, 30,       1,    true,   true,    HD},    // GST_AJA_MODE_RAW_1080_10_60i
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_1080p_6000_A,      1920,   1080,   10, 60,       1,    false,  true,    HD},    // GST_AJA_MODE_RAW_1080_10_60p
    
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_3840x2160p_2398, 3840,   2160,   8,  24000,    1001, false,  true,    UHD},   // GST_AJA_MODE_RAW_UHD_8_2398p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_3840x2160p_2400, 3840,   2160,   8,  24,       1,    false,  true,    UHD},   // GST_AJA_MODE_RAW_UHD_8_24p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_3840x2160p_2500, 3840,   2160,   8,  25,       1,    false,  true,    UHD},   // GST_AJA_MODE_RAW_UHD_8_25p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_3840x2160p_2997, 3840,   2160,   8,  30000,    1001, false,  true,    UHD},   // GST_AJA_MODE_RAW_UHD_8_2997p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_3840x2160p_3000, 3840,   2160,   8,  30,       1,    false,  true,    UHD},   // GST_AJA_MODE_RAW_UHD_8_30p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_3840x2160p_5000, 3840,   2160,   8,  50,       1,    false,  true,    UHD},   // GST_AJA_MODE_RAW_UHD_8_50p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_3840x2160p_5994, 3840,   2160,   8,  60000,    1001, false,  true,    UHD},   // GST_AJA_MODE_RAW_UHD_8_5994p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_3840x2160p_6000, 3840,   2160,   8,  60,       1,    false,  true,    UHD},   // GST_AJA_MODE_RAW_UHD_8_60p
    
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_3840x2160p_2398, 3840,   2160,   10, 24000,    1001, false,  true,    UHD},   // GST_AJA_MODE_RAW_UHD_10_2398p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_3840x2160p_2400, 3840,   2160,   10, 24,       1,    false,  true,    UHD},   // GST_AJA_MODE_RAW_UHD_10_24p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_3840x2160p_2500, 3840,   2160,   10, 25,       1,    false,  true,    UHD},   // GST_AJA_MODE_RAW_UHD_10_25p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_3840x2160p_2997, 3840,   2160,   10, 30000,    1001, false,  true,    UHD},   // GST_AJA_MODE_RAW_UHD_10_2997p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_3840x2160p_3000, 3840,   2160,   10, 30,       1,    false,  true,    UHD},   // GST_AJA_MODE_RAW_UHD_10_30p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_3840x2160p_5000, 3840,   2160,   10, 50,       1,    false,  true,    UHD},   // GST_AJA_MODE_RAW_UHD_10_50p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_3840x2160p_5994, 3840,   2160,   10, 60000,    1001, false,  true,    UHD},   // GST_AJA_MODE_RAW_UHD_10_5994p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_3840x2160p_6000, 3840,   2160,   10, 60,       1,    false,  true,    UHD},   // GST_AJA_MODE_RAW_UHD_10_60p
    
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4096x2160p_2398, 4096,   2160,   8,  24000,    1001, false,  true,    UHD},   // GST_AJA_MODE_RAW_4K_8_2398p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4096x2160p_2400, 4096,   2160,   8,  24,       1,    false,  true,    UHD},   // GST_AJA_MODE_RAW_4K_8_24p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4096x2160p_2500, 4096,   2160,   8,  25,       1,    false,  true,    UHD},   // GST_AJA_MODE_RAW_4K_8_25p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4096x2160p_2997, 4096,   2160,   8,  30000,    1001, false,  true,    UHD},   // GST_AJA_MODE_RAW_4K_8_2997p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4096x2160p_3000, 4096,   2160,   8,  30,       1,    false,  true,    UHD},   // GST_AJA_MODE_RAW_4K_8_30p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4096x2160p_4795, 4096,   2160,   8,  48000,    1001, false,  true,    UHD},   // GST_AJA_MODE_RAW_4K_8_4795p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4096x2160p_4800, 4096,   2160,   8,  48,       1,    false,  true,    UHD},   // GST_AJA_MODE_RAW_4K_8_48p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4096x2160p_5000, 4096,   2160,   8,  50,       1,    false,  true,    UHD},   // GST_AJA_MODE_RAW_4K_8_50p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4096x2160p_5994, 4096,   2160,   8,  60000,    1001, false,  true,    UHD},   // GST_AJA_MODE_RAW_4K_8_5994p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4096x2160p_6000, 4096,   2160,   8,  60,       1,    false,  true,    UHD},   // GST_AJA_MODE_RAW_4K_8_60p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4096x2160p_11988, 4096,   2160,  8,  120000,   1001, false,  true,    UHD},   // GST_AJA_MODE_RAW_4K_8_11988p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4096x2160p_12000, 4096,   2160,  8,  120,       1,    false,  true,    UHD},   // GST_AJA_MODE_RAW_4K_8_120p
    
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4096x2160p_2398, 4096,   2160,   10, 24000,    1001, false,  true,    UHD},   // GST_AJA_MODE_RAW_4K_10_2398p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4096x2160p_2400, 4096,   2160,   10, 24,       1,    false,  true,    UHD},   // GST_AJA_MODE_RAW_4K_10_24p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4096x2160p_2500, 4096,   2160,   10, 25,       1,    false,  true,    UHD},   // GST_AJA_MODE_RAW_4K_10_25p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4096x2160p_2997, 4096,   2160,   10, 30000,    1001, false,  true,    UHD},   // GST_AJA_MODE_RAW_4K_10_2997p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4096x2160p_3000, 4096,   2160,   10, 30,       1,    false,  true,    UHD},   // GST_AJA_MODE_RAW_4K_10_30p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4096x2160p_4795, 4096,   2160,   10, 48000,    1001, false,  true,    UHD},   // GST_AJA_MODE_RAW_4K_10_4795p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4096x2160p_4800, 4096,   2160,   10, 48,       1,    false,  true,    UHD},   // GST_AJA_MODE_RAW_4K_10_48p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4096x2160p_5000, 4096,   2160,   10, 50,       1,    false,  true,    UHD},   // GST_AJA_MODE_RAW_4K_10_50p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4096x2160p_5994, 4096,   2160,   10, 60000,    1001, false,  true,    UHD},   // GST_AJA_MODE_RAW_4K_10_5994p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4096x2160p_6000, 4096,   2160,   10, 60,       1,    false,  true,    UHD},   // GST_AJA_MODE_RAW_4K_10_60p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4096x2160p_11988, 4096,  2160,   10, 120000,   1001, false,  true,    UHD},   // GST_AJA_MODE_RAW_4K_10_11988p
    {M31_NUMVIDEOPRESETS,       NTV2_FORMAT_4096x2160p_12000, 4096,  2160,   10, 120,      1,    false,  true,    UHD},   // GST_AJA_MODE_RAW_4K_10_120p
};

GType
gst_aja_mode_get_type_raw (void)
{
    static gsize id = 0;
    static const GEnumValue modes[GST_AJA_MODE_RAW_END + 1] =
    {
        {GST_AJA_MODE_RAW_NTSC_8_2398i,     "ntsc-2398",             "NTSC 8Bit 23.98i     "},
        {GST_AJA_MODE_RAW_NTSC_8_24i,     "ntsc-24",             "NTSC 8Bit 24i     "},
        {GST_AJA_MODE_RAW_NTSC_8_5994i,     "ntsc",             "NTSC 8Bit 59.94i     "},
        {GST_AJA_MODE_RAW_NTSC_10_2398i,    "ntsc-2398-10",          "NTSC 10Bit 23.98i    "},
        {GST_AJA_MODE_RAW_NTSC_10_24i,    "ntsc-24-10",          "NTSC 10Bit 24i    "},
        {GST_AJA_MODE_RAW_NTSC_10_5994i,    "ntsc-10",          "NTSC 10Bit 59.94i    "},
        
        {GST_AJA_MODE_RAW_PAL_8_50i,        "pal",              "PAL 8Bit 50i         "},
        {GST_AJA_MODE_RAW_PAL_10_50i,       "pal-10",           "PAL 10Bit 50i        "},
        
        {GST_AJA_MODE_RAW_720_8_2398p,        "720p2398",           "HD720 8Bit 23.98p       "},
        {GST_AJA_MODE_RAW_720_8_25p,        "720p25",           "HD720 8Bit 25p       "},
        {GST_AJA_MODE_RAW_720_8_50p,        "720p50",           "HD720 8Bit 50p       "},
        {GST_AJA_MODE_RAW_720_8_5994p,      "720p5994",        "HD720 8Bit 59.94p    "},
        {GST_AJA_MODE_RAW_720_8_60p,        "720p60",           "HD720 8Bit 60p       "},
        {GST_AJA_MODE_RAW_720_10_2398p,       "720p2398-10",        "HD720 10Bit 23.98p      "},
        {GST_AJA_MODE_RAW_720_10_25p,       "720p25-10",        "HD720 10Bit 25p      "},
        {GST_AJA_MODE_RAW_720_10_50p,       "720p50-10",        "HD720 10Bit 50p      "},
        {GST_AJA_MODE_RAW_720_10_5994p,     "720p5994-10",     "HD720 10Bit 59.94p   "},
        {GST_AJA_MODE_RAW_720_10_60p,       "720p60-10",        "HD720 10Bit 60p      "},
        
        {GST_AJA_MODE_RAW_1080_8_2398p,       "1080p2398",          "HD1080 8Bit 23.98p     "},
        {GST_AJA_MODE_RAW_1080_8_24p,       "1080p24",          "HD1080 8Bit 24p     "},
        {GST_AJA_MODE_RAW_1080_8_25p,       "1080p25",          "HD1080 8Bit 25p     "},
        {GST_AJA_MODE_RAW_1080_8_2997p,       "1080p2997",          "HD1080 8Bit 29.97p     "},
        {GST_AJA_MODE_RAW_1080_8_30p,       "1080p30",          "HD1080 8Bit 30p     "},
        {GST_AJA_MODE_RAW_1080_8_50i,       "1080i50",          "HD1080 8Bit 50i      "},
        {GST_AJA_MODE_RAW_1080_8_50p,       "1080p50",          "HD1080 8Bit 50p      "},
        {GST_AJA_MODE_RAW_1080_8_5994i,     "1080i5994",       "HD1080 8Bit 59.94i   "},
        {GST_AJA_MODE_RAW_1080_8_5994p,     "1080p5994",       "HD1080 8Bit 59.94p   "},
        {GST_AJA_MODE_RAW_1080_8_60i,       "1080i60",          "HD1080 8Bit 60i      "},
        {GST_AJA_MODE_RAW_1080_8_60p,       "1080p60",          "HD1080 8Bit 60p      "},
        
        {GST_AJA_MODE_RAW_1080_10_2398p,      "1080p2398-10",       "HD1080 10Bit 23.98p     "},
        {GST_AJA_MODE_RAW_1080_10_24p,      "1080p24-10",       "HD1080 10Bit 24p     "},
        {GST_AJA_MODE_RAW_1080_10_25p,      "1080p25-10",       "HD1080 10Bit 25p     "},
        {GST_AJA_MODE_RAW_1080_10_2997p,      "1080p2997-10",       "HD1080 10Bit 29.97p     "},
        {GST_AJA_MODE_RAW_1080_10_30p,      "1080p30-10",       "HD1080 10Bit 30p     "},
        {GST_AJA_MODE_RAW_1080_10_50i,      "1080i50-10",       "HD1080 10Bit 50i     "},
        {GST_AJA_MODE_RAW_1080_10_50p,      "1080p50-10",       "HD1080 10Bit 50p     "},
        {GST_AJA_MODE_RAW_1080_10_5994i,    "1080i5994-10",    "HD1080 10Bit 59.94i  "},
        {GST_AJA_MODE_RAW_1080_10_5994p,    "1080p5994-10",    "HD1080 10Bit 59.94p  "},
        {GST_AJA_MODE_RAW_1080_10_60i,      "1080i60-10",       "HD1080 10Bit 60i     "},
        {GST_AJA_MODE_RAW_1080_10_60p,      "1080p60-10",       "HD1080 10Bit 60p     "},
        
        {GST_AJA_MODE_RAW_UHD_8_2398p,        "UHDp2398",           "UHD3140 8Bit 23.98p     "},
        {GST_AJA_MODE_RAW_UHD_8_24p,        "UHDp24",           "UHD3140 8Bit 24p     "},
        {GST_AJA_MODE_RAW_UHD_8_25p,        "UHDp25",           "UHD3140 8Bit 25p     "},
        {GST_AJA_MODE_RAW_UHD_8_2997p,        "UHDp2997",           "UHD3140 8Bit 29.97p     "},
        {GST_AJA_MODE_RAW_UHD_8_30p,        "UHDp30",           "UHD3140 8Bit 30p     "},
        {GST_AJA_MODE_RAW_UHD_8_50p,        "UHDp50",           "UHD3140 8Bit 50p     "},
        {GST_AJA_MODE_RAW_UHD_8_5994p,      "UHDp5994",        "UHD3140 8Bit 59.94p  "},
        {GST_AJA_MODE_RAW_UHD_8_60p,        "UHDp60",           "UHD3140 8Bit 60p     "},
        
        {GST_AJA_MODE_RAW_UHD_10_2398p,       "UHDp2398-10",        "UHD3140 10Bit 23.98p    "},
        {GST_AJA_MODE_RAW_UHD_10_24p,       "UHDp24-10",        "UHD3140 10Bit 24p    "},
        {GST_AJA_MODE_RAW_UHD_10_25p,       "UHDp25-10",        "UHD3140 10Bit 25p    "},
        {GST_AJA_MODE_RAW_UHD_10_2997p,       "UHDp2997-10",        "UHD3140 10Bit 29.97p    "},
        {GST_AJA_MODE_RAW_UHD_10_30p,       "UHDp30-10",        "UHD3140 10Bit 30p    "},
        {GST_AJA_MODE_RAW_UHD_10_50p,       "UHDp50-10",        "UHD3140 10Bit 50p    "},
        {GST_AJA_MODE_RAW_UHD_10_5994p,     "UHDp5994-10",     "UHD3140 10Bit 59.94p "},
        {GST_AJA_MODE_RAW_UHD_10_60p,       "UHDp60-10",        "UHD3140 10Bit 60p    "},
        
        {GST_AJA_MODE_RAW_4K_8_2398p,         "4Kp2398",            "4K4096 8Bit 23.98p      "},
        {GST_AJA_MODE_RAW_4K_8_24p,         "4Kp24",            "4K4096 8Bit 24p      "},
        {GST_AJA_MODE_RAW_4K_8_25p,         "4Kp25",            "4K4096 8Bit 25p      "},
        {GST_AJA_MODE_RAW_4K_8_2997p,         "4Kp2997",            "4K4096 8Bit 29.97p      "},
        {GST_AJA_MODE_RAW_4K_8_30p,         "4Kp30",            "4K4096 8Bit 30p      "},
        {GST_AJA_MODE_RAW_4K_8_4795p,         "4Kp4795",            "4K4096 8Bit 47.95p      "},
        {GST_AJA_MODE_RAW_4K_8_48p,         "4Kp48",            "4K4096 8Bit 48p      "},
        {GST_AJA_MODE_RAW_4K_8_50p,         "4Kp50",            "4K4096 8Bit 50p      "},
        {GST_AJA_MODE_RAW_4K_8_5994p,       "4Kp5994",         "4K4096 8Bit 59.94p   "},
        {GST_AJA_MODE_RAW_4K_8_60p,         "4Kp60",            "4K4096 8Bit 60p      "},
        {GST_AJA_MODE_RAW_4K_8_11988p,         "4Kp11988",            "4K4096 8Bit 119.88p      "},
        {GST_AJA_MODE_RAW_4K_8_120p,         "4Kp120",            "4K4096 8Bit 120p      "},
        
        {GST_AJA_MODE_RAW_4K_10_2398p,        "4Kp2398-10",         "4K4096 10Bit 23.98p     "},
        {GST_AJA_MODE_RAW_4K_10_24p,        "4Kp24-10",         "4K4096 10Bit 24p     "},
        {GST_AJA_MODE_RAW_4K_10_25p,        "4Kp25-10",         "4K4096 10Bit 25p     "},
        {GST_AJA_MODE_RAW_4K_10_2997p,        "4Kp2997-10",         "4K4096 10Bit 29.97p     "},
        {GST_AJA_MODE_RAW_4K_10_30p,        "4Kp30-10",         "4K4096 10Bit 30p     "},
        {GST_AJA_MODE_RAW_4K_10_4795p,        "4Kp4795-10",         "4K4096 10Bit 47.95p     "},
        {GST_AJA_MODE_RAW_4K_10_48p,        "4Kp48-10",         "4K4096 10Bit 48p     "},
        {GST_AJA_MODE_RAW_4K_10_50p,        "4Kp50-10",         "4K4096 10Bit 50p     "},
        {GST_AJA_MODE_RAW_4K_10_5994p,      "4Kp5994-10",      "4K4096 10Bit 59.94p  "},
        {GST_AJA_MODE_RAW_4K_10_60p,        "4Kp60-10",         "4K4096 10Bit 60p     "},
        {GST_AJA_MODE_RAW_4K_10_11988p,        "4Kp11988-10",         "4K4096 10Bit 119.88p     "},
        {GST_AJA_MODE_RAW_4K_10_120p,        "4Kp1200-10",         "4K4096 10Bit 120p     "},
        {0,                                 NULL,               NULL}
    };
    
    if (g_once_init_enter (&id))
    {
        GType tmp = g_enum_register_static ("GstAjaRawModes", modes);
        g_once_init_leave (&id, tmp);
    }
    
    return (GType) id;
}

// *INDENT-ON*

const GstAjaMode *
gst_aja_get_mode_raw (GstAjaModeRawEnum e)
{
  if (e < GST_AJA_MODE_RAW_NTSC_8_2398i || e >= GST_AJA_MODE_RAW_END)
    return NULL;
  return &modesRaw[e];
}

static GstStructure *
gst_aja_mode_get_structure_raw (GstAjaModeRawEnum e)
{
  const GstAjaMode *mode = gst_aja_get_mode_raw (e);
  GstStructure *s;

  s = gst_structure_new ("video/x-raw",
      "format", G_TYPE_STRING, mode->bitDepth == 8 ? "UYVY" : "v210",
      "width", G_TYPE_INT, mode->width,
      "height", G_TYPE_INT, mode->height,
      "framerate", GST_TYPE_FRACTION, mode->fps_n, mode->fps_d,
      "interlace-mode", G_TYPE_STRING,
      mode->isInterlaced ? "interleaved" : "progressive", "pixel-aspect-ratio",
      GST_TYPE_FRACTION, mode->par_n, mode->par_d, "colorimetry", G_TYPE_STRING,
      mode->colorimetry, "chroma-site", G_TYPE_STRING, "mpeg2", NULL);

  if (mode->isInterlaced) {
    if (mode->isTff)
      gst_structure_set (s, "field-order", G_TYPE_STRING, "top-field-first",
          NULL);
    else
      gst_structure_set (s, "field-order", G_TYPE_STRING, "bottom-field-first",
          NULL);
  }

  return s;
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
  for (i = 1; i < (int) G_N_ELEMENTS (modesRaw); i++) {
    s = gst_aja_mode_get_structure_raw ((GstAjaModeRawEnum) i);
    gst_structure_remove_field (s, "colorimetry");
    gst_caps_append_structure (caps, s);
  }

  return caps;
}

// *INDENT-OFF*
static const GstAjaMode modesHevc[GST_AJA_MODE_HEVC_END] =
{
    {M31_FILE_720X480_420_8_5994i,       NTV2_FORMAT_525_5994,          720,    486,    8,  30000,    1001, true,   false,   NTSC},  // GST_AJA_MODE_HEVC_NTSC_420_8_5994i
    {M31_FILE_720X480_422_10_5994i,      NTV2_FORMAT_525_5994,          720,    486,    10, 30000,    1001, true,   true,    NTSC},  // GST_AJA_MODE_HEVC_NTSC_422_10_5994i

    {M31_FILE_720X576_420_8_50i,         NTV2_FORMAT_625_5000,          720,    486,    8,  25,       1,    true,   false,   PAL},   // GST_AJA_MODE_HEVC_PAL_420_8_50i
    {M31_FILE_720X576_422_10_50i,        NTV2_FORMAT_625_5000,          720,    486,    10, 25,       1,    true,   true,    PAL},   // GST_AJA_MODE_HEVC_PAL_422_10_50i

    {M31_FILE_1280X720_420_8_50p,        NTV2_FORMAT_720p_5000,         1280,   720,    8,  50,       1,    false,  false,   HD},    // GST_AJA_MODE_HEVC_720_420_8_50p
    {M31_FILE_1280X720_420_8_5994p,      NTV2_FORMAT_720p_5994,         1280,   720,    8,  60000,    1001, false,  false,   HD},    // GST_AJA_MODE_HEVC_720_420_8_5994p
    {M31_FILE_1280X720_420_8_60p,        NTV2_FORMAT_720p_6000,         1280,   720,    8,  60,       1,    false,  false,   HD},    // GST_AJA_MODE_HEVC_720_420_8_60p
    {M31_FILE_1280X720_422_10_50p,       NTV2_FORMAT_720p_5000,         1280,   720,    10, 50,       1,    false,  true,    HD},    // GST_AJA_MODE_HEVC_720_422_10_50p
    {M31_FILE_1280X720_422_10_5994p,     NTV2_FORMAT_720p_5994,         1280,   720,    10, 60000,    1001, false,  true,    HD},    // GST_AJA_MODE_HEVC_720_422_10_5994p
    {M31_FILE_1280X720_422_10_60p,       NTV2_FORMAT_720p_6000,         1280,   720,    10, 60,       1,    false,  true,    HD},    // GST_AJA_MODE_HEVC_720_422_10_60p

    {M31_FILE_1920X1080_420_8_50i,       NTV2_FORMAT_1080i_5000,        1920,   1080,   8,  25,       1,    true,   false,   HD},    // GST_AJA_MODE_HEVC_1080_420_8_50i
    {M31_FILE_1920X1080_420_8_50p,       NTV2_FORMAT_1080p_5000_A,      1920,   1080,   8,  50,       1,    false,  false,   HD},    // GST_AJA_MODE_HEVC_1080_420_8_50p
    {M31_FILE_1920X1080_420_8_5994i,     NTV2_FORMAT_1080i_5994,        1920,   1080,   8,  30000,    1001, true,   false,   HD},    // GST_AJA_MODE_HEVC_1080_420_8_5994i
    {M31_FILE_1920X1080_420_8_5994p,     NTV2_FORMAT_1080p_5994_A,      1920,   1080,   8,  60,       1,    false,  false,   HD},    // GST_AJA_MODE_HEVC_1080_420_8_5994p
    {M31_FILE_1920X1080_420_8_60i,       NTV2_FORMAT_1080i_6000,        1920,   1080,   8,  30,       1,    true,   false,   HD},    // GST_AJA_MODE_HEVC_1080_420_8_60i
    {M31_FILE_1920X1080_420_8_60p,       NTV2_FORMAT_1080p_6000_A,      1920,   1080,   8,  60,       1,    false,  false,   HD},    // GST_AJA_MODE_HEVC_1080_420_8_60p

    {M31_FILE_1920X1080_422_10_50i,      NTV2_FORMAT_1080i_5000,        1920,   1080,   10, 25,       1,    true,   true,    HD},    // GST_AJA_MODE_HEVC_1080_422_10_50i
    {M31_FILE_1920X1080_422_10_50p,      NTV2_FORMAT_1080p_5000_A,      1920,   1080,   10, 50,       1,    false,  true,    HD},    // GST_AJA_MODE_HEVC_1080_422_10_50p
    {M31_FILE_1920X1080_422_10_5994i,    NTV2_FORMAT_1080i_5994,        1920,   1080,   10, 30000,    1001, true,   true,    HD},    // GST_AJA_MODE_HEVC_1080_422_10_5994i
    {M31_FILE_1920X1080_422_10_5994p,    NTV2_FORMAT_1080p_5994_A,      1920,   1080,   10, 60,       1,    false,  true,    HD},    // GST_AJA_MODE_HEVC_1080_422_10_5994p
    {M31_FILE_1920X1080_422_10_60i,      NTV2_FORMAT_1080i_6000,        1920,   1080,   10, 30,       1,    true,   true,    HD},    // GST_AJA_MODE_HEVC_1080_422_10_60i
    {M31_FILE_1920X1080_422_10_60p,      NTV2_FORMAT_1080p_6000_A,      1920,   1080,   10, 60,       1,    false,  true,    HD},    // GST_AJA_MODE_HEVC_1080_422_10_60p

    {M31_FILE_3840X2160_420_8_50p,       NTV2_FORMAT_3840x2160p_5000, 3840,   2160,   8,  50,       1,    false,  false,   UHD},   // GST_AJA_MODE_HEVC_UHD_420_8_50p
    {M31_FILE_3840X2160_420_8_5994p,     NTV2_FORMAT_3840x2160p_5994, 3840,   2160,   8,  60000,    1001, false,  false,   UHD},   // GST_AJA_MODE_HEVC_UHD_420_8_5994p
    {M31_FILE_3840X2160_420_8_60p,       NTV2_FORMAT_3840x2160p_6000, 3840,   2160,   8,  60,       1,    false,  false,   UHD},   // GST_AJA_MODE_HEVC_UHD_420_8_60p

    {M31_FILE_3840X2160_420_10_50p,      NTV2_FORMAT_3840x2160p_5000, 3840,   2160,   10, 50,       1,    false,  false,   UHD},   // GST_AJA_MODE_HEVC_UHD_420_10_50p
    {M31_FILE_3840X2160_420_10_5994p,    NTV2_FORMAT_3840x2160p_5994, 3840,   2160,   10, 60000,    1001, false,  false,   UHD},   // GST_AJA_MODE_HEVC_UHD_420_10_5994p
    {M31_FILE_3840X2160_420_10_60p,      NTV2_FORMAT_3840x2160p_6000, 3840,   2160,   10, 60,       1,    false,  false,   UHD},   // GST_AJA_MODE_HEVC_UHD_420_10_60p

    {M31_FILE_3840X2160_422_10_50p,      NTV2_FORMAT_3840x2160p_5000, 3840,   2160,   10, 50,       1,    false,  true,    UHD},   // GST_AJA_MODE_HEVC_UHD_422_10_50p
    {M31_FILE_3840X2160_422_10_5994p,    NTV2_FORMAT_3840x2160p_5994, 3840,   2160,   10, 60000,    1001, false,  true,    UHD},   // GST_AJA_MODE_HEVC_UHD_422_10_5994p
    {M31_FILE_3840X2160_422_10_60p,      NTV2_FORMAT_3840x2160p_6000, 3840,   2160,   10, 60,       1,    false,  true,    UHD},   // GST_AJA_MODE_HEVC_UHD_422_10_60p
};

GType
gst_aja_mode_get_type_hevc (void)
{
    static gsize id = 0;
    static const GEnumValue modes[GST_AJA_MODE_HEVC_END + 1] =
    {
        {GST_AJA_MODE_HEVC_NTSC_420_8_5994i,     "ntsc",             "NTSC 8Bit 420 59.94i      "},
        {GST_AJA_MODE_HEVC_NTSC_422_10_5994i,    "ntsc-10+",         "NTSC 10Bit 422 59.94i     "},
        
        {GST_AJA_MODE_HEVC_PAL_420_8_50i,        "pal",              "PAL 8Bit 420 50i          "},
        {GST_AJA_MODE_HEVC_PAL_422_10_50i,       "pal-10+",          "PAL 10Bit 422 50i         "},
        
        {GST_AJA_MODE_HEVC_720_420_8_50p,        "720p50",           "HD720 8Bit 420 50p        "},
        {GST_AJA_MODE_HEVC_720_420_8_5994p,      "720p5994",        "HD720 8Bit 420 59.94p     "},
        {GST_AJA_MODE_HEVC_720_420_8_60p,        "720p60",           "HD720 8Bit 420 60p        "},
        {GST_AJA_MODE_HEVC_720_422_10_50p,       "720p50-10+",       "HD720 10Bit 422 50p       "},
        {GST_AJA_MODE_HEVC_720_422_10_5994p,     "720p5994-10+",    "HD720 10Bit 422 59.94p    "},
        {GST_AJA_MODE_HEVC_720_422_10_60p,       "720p60-10+",       "HD720 10Bit 422 60p       "},
        
        {GST_AJA_MODE_HEVC_1080_420_8_50i,       "1080i50",          "HD1080 8Bit 420 50i       "},
        {GST_AJA_MODE_HEVC_1080_420_8_50p,       "1080p50",          "HD1080 8Bit 420 50p       "},
        {GST_AJA_MODE_HEVC_1080_420_8_5994i,     "1080i5994",       "HD1080 8Bit 420 59.94i    "},
        {GST_AJA_MODE_HEVC_1080_420_8_5994p,     "1080p5994",       "HD1080 8Bit 420 59.94p    "},
        {GST_AJA_MODE_HEVC_1080_420_8_60i,       "1080i60",          "HD1080 8Bit 420 60i       "},
        {GST_AJA_MODE_HEVC_1080_420_8_60p,       "1080p60",          "HD1080 8Bit 420 60p       "},
        
        {GST_AJA_MODE_HEVC_1080_422_10_50i,      "1080i50-10+",      "HD1080 10Bit 422 50i      "},
        {GST_AJA_MODE_HEVC_1080_422_10_50p,      "1080p50-10+",      "HD1080 10Bit 422 50p      "},
        {GST_AJA_MODE_HEVC_1080_422_10_5994i,    "1080i5994-10+",   "HD1080 10Bit 422 59.94i   "},
        {GST_AJA_MODE_HEVC_1080_422_10_5994p,    "1080p5994-10+",   "HD1080 10Bit 422 59.94p   "},
        {GST_AJA_MODE_HEVC_1080_422_10_60i,      "1080i60-10+",      "HD1080 10Bit 422 60i      "},
        {GST_AJA_MODE_HEVC_1080_422_10_60p,      "1080p60-10+",      "HD1080 10Bit 422 60p      "},
        
        {GST_AJA_MODE_HEVC_UHD_420_8_50p,        "UHDp50",           "UHD3140 8Bit 420 50p      "},
        {GST_AJA_MODE_HEVC_UHD_420_8_5994p,      "UHDp5994",        "UHD3140 8Bit 420 59.94p   "},
        {GST_AJA_MODE_HEVC_UHD_420_8_60p,        "UHDp60",           "UHD3140 8Bit 420 60p      "},
        
        {GST_AJA_MODE_HEVC_UHD_420_10_50p,       "UHDp50-10",        "UHD3140 10Bit 420 50p     "},
        {GST_AJA_MODE_HEVC_UHD_420_10_5994p,     "UHDp5994-10",     "UHD3140 10Bit 420 59.94p  "},
        {GST_AJA_MODE_HEVC_UHD_420_10_60p,       "UHDp60-10",        "UHD3140 10Bit 420 60p     "},
        
        {GST_AJA_MODE_HEVC_UHD_422_10_50p,       "UHDp50-10+",       "UHD3140 10Bit 422 50p     "},
        {GST_AJA_MODE_HEVC_UHD_422_10_5994p,     "UHDp5994-10+",    "UHD3140 10Bit 422 59.94p  "},
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

// *INDENT-ON*

const GstAjaMode *
gst_aja_get_mode_hevc (GstAjaModeHevcEnum e)
{
  if (e < GST_AJA_MODE_HEVC_NTSC_420_8_5994i || e >= GST_AJA_MODE_HEVC_END)
    return NULL;
  return &modesHevc[e];
}

GType
gst_aja_video_input_mode_get_type (void)
{
    static gsize id = 0;
    static const GEnumValue modes[] =
    {
        {GST_AJA_VIDEO_INPUT_MODE_SDI,     "sdi",              "SDI"},
        {GST_AJA_VIDEO_INPUT_MODE_HDMI,    "hdmi",             "HDMI"},
        {GST_AJA_VIDEO_INPUT_MODE_ANALOG,  "analog",           "Analog"},
        {0,                                 NULL,               NULL}
    };
    
    if (g_once_init_enter (&id))
    {
        GType tmp = g_enum_register_static ("GstAjaVideoInputMode", modes);
        g_once_init_leave (&id, tmp);
    }
    
    return (GType) id;
}

GType
gst_aja_sdi_input_mode_get_type (void)
{
    static gsize id = 0;
    static const GEnumValue modes[] =
    {
        {SDI_INPUT_MODE_SINGLE_LINK,    "single-link",   "Single Link"},
        {SDI_INPUT_MODE_QUAD_LINK_SQD,  "quad-link-sqd", "Quad Link SQD"},
        {SDI_INPUT_MODE_QUAD_LINK_TSI,  "quad-link-tsi", "Quad Link TSI"},
        {0,                             NULL,            NULL}
    };
    
    if (g_once_init_enter (&id))
    {
        GType tmp = g_enum_register_static ("GstAjaSDIInputMode", modes);
        g_once_init_leave (&id, tmp);
    }
    
    return (GType) id;
}

GType
gst_aja_timecode_mode_get_type (void)
{
    static gsize id = 0;
    static const GEnumValue modes[] =
    {
        {GST_AJA_TIMECODE_MODE_VITC1,      "vitc1",              "RP188 VITC1"},
        {GST_AJA_TIMECODE_MODE_VITC2,      "vitc2",              "RP188 VITC2"},
        {GST_AJA_TIMECODE_MODE_ANALOG_LTC1,"analog-ltc1",        "Analog LTC1"},
        {GST_AJA_TIMECODE_MODE_ANALOG_LTC2,"analog-ltc2",        "Analog LTC2"},
        {GST_AJA_TIMECODE_MODE_ATC_LTC,    "atc-ltc",            "ATC LTC"},
        {0,                                 NULL,                 NULL}
    };
    
    if (g_once_init_enter (&id))
    {
        GType tmp = g_enum_register_static ("GstAjaTimecodeMode", modes);
        g_once_init_leave (&id, tmp);
    }
    
    return (GType) id;
}

GType
gst_aja_audio_input_mode_get_type (void)
{
    static gsize id = 0;
    static const GEnumValue modes[] =
    {
        {GST_AJA_AUDIO_INPUT_MODE_EMBEDDED,"embedded",         "Embedded"},
        {GST_AJA_AUDIO_INPUT_MODE_HDMI,    "hdmi",             "HDMI"},
        {GST_AJA_AUDIO_INPUT_MODE_AES,     "aes",              "AES"},
        {GST_AJA_AUDIO_INPUT_MODE_ANALOG,  "analog",           "Analog"},
        {0,                                 NULL,               NULL}
    };
    
    if (g_once_init_enter (&id))
    {
        GType tmp = g_enum_register_static ("GstAjaAudioInputMode", modes);
        g_once_init_leave (&id, tmp);
    }
    
    return (GType) id;
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

GstClock *
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

  if (self->output != NULL) {
    g_mutex_lock (&self->output->lock);
    start_time = self->output->clock_start_time;
    offset = self->output->clock_offset;
    last_time = self->output->clock_last_time;
    time = -1;
    if (!self->output->started) {
      result = last_time;
    } else {
      status = self->output->ntv2AVHevc->GetHardwareClock (GST_SECOND, &time);
      if (status) {
        result = time;

        if (start_time == GST_CLOCK_TIME_NONE)
          start_time = self->output->clock_start_time = result;

        if (result > start_time)
          result -= start_time;
        else
          result = 0;

        if (self->output->clock_restart) {
          self->output->clock_offset = result - last_time;
          offset = self->output->clock_offset;
          self->output->clock_restart = FALSE;
        }
        result = MAX (last_time, result);
        result -= offset;
        result = MAX (last_time, result);
      } else {
        result = last_time;
      }

      self->output->clock_last_time = result;
    }
    result += self->output->clock_epoch;
    g_mutex_unlock (&self->output->lock);
  } else {
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


G_DEFINE_TYPE (GstAjaBufferPool, gst_aja_buffer_pool, GST_TYPE_BUFFER_POOL);

static GQuark video_buffer_quark, audio_buffer_quark;

static gboolean
gst_aja_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  GstAjaBufferPool *aja_pool = GST_AJA_BUFFER_POOL (pool);

  if (!GST_BUFFER_POOL_CLASS (gst_aja_buffer_pool_parent_class)->set_config
      (pool, config))
    return FALSE;

  if (!gst_structure_get_boolean (config, "is-video", &aja_pool->is_video))
    return FALSE;
  if (!gst_structure_get_boolean (config, "is-hevc", &aja_pool->is_hevc))
    return FALSE;
  if (!gst_buffer_pool_config_get_params (config, NULL, &aja_pool->size, NULL,
          NULL))
    return FALSE;

  return TRUE;
}

static void
aja_audio_buff_free (AjaAudioBuff * audioBuff)
{
  delete audioBuff;
}

static void
aja_video_buff_free (AjaVideoBuff * videoBuff)
{
  if (videoBuff->pInfoBuffer)
    delete[]videoBuff->pInfoBuffer;

  delete videoBuff;
}

static GstFlowReturn
gst_aja_buffer_pool_alloc_buffer (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstAjaBufferPool *aja_pool = GST_AJA_BUFFER_POOL (pool);
  GstFlowReturn ret;

  ret =
      GST_BUFFER_POOL_CLASS (gst_aja_buffer_pool_parent_class)->alloc_buffer
      (pool, buffer, params);
  if (ret != GST_FLOW_OK)
    return ret;

  if (!aja_pool->is_video) {
    AjaAudioBuff *audioBuff = new AjaAudioBuff;
    audioBuff->buffer = *buffer;
    audioBuff->pAudioBuffer = NULL;
    audioBuff->audioBufferSize = 0;
    audioBuff->audioDataSize = 0;

    gst_mini_object_set_qdata (GST_MINI_OBJECT_CAST (*buffer),
        audio_buffer_quark, audioBuff, (GDestroyNotify) aja_audio_buff_free);
  } else {
    AjaVideoBuff *videoBuff = new AjaVideoBuff;

    videoBuff->buffer = *buffer;
    if (aja_pool->is_hevc) {
      videoBuff->pInfoBuffer = new uint32_t[(sizeof (HevcEncodedInfo) * 2) / 4];
      videoBuff->infoBufferSize = sizeof (HevcEncodedInfo) * 2;
    } else {
      videoBuff->pInfoBuffer = new uint32_t[(sizeof (HevcPictureInfo) * 2) / 4];
      videoBuff->infoBufferSize = sizeof (HevcPictureInfo) * 2;
    }
    videoBuff->pVideoBuffer = NULL;
    videoBuff->videoBufferSize = 0;
    videoBuff->videoDataSize = 0;

    gst_mini_object_set_qdata (GST_MINI_OBJECT_CAST (*buffer),
        video_buffer_quark, videoBuff, (GDestroyNotify) aja_video_buff_free);
  }

  return ret;
}

static void
gst_aja_buffer_pool_reset_buffer (GstBufferPool * pool, GstBuffer * buffer)
{
  GstAjaBufferPool *aja_pool = GST_AJA_BUFFER_POOL (pool);
  gsize maxsize, size, offset;

  // Update size again to the maximum, we might've made the buffer
  // smaller because we got less data than the maximum
  size = gst_buffer_get_sizes (buffer, &offset, &maxsize);
  if (size != aja_pool->size && aja_pool->size < maxsize) {
    gst_buffer_resize (buffer, -offset, aja_pool->size);
    size = aja_pool->size;
  }
  if (size == aja_pool->size && gst_buffer_n_memory (buffer) == 1)
    GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_TAG_MEMORY);

  GST_BUFFER_POOL_CLASS (gst_aja_buffer_pool_parent_class)->reset_buffer (pool,
      buffer);
}

static void
gst_aja_buffer_pool_release_buffer (GstBufferPool * pool, GstBuffer * buffer)
{
  // Free if something removed our qdata
  if (!gst_mini_object_get_qdata (GST_MINI_OBJECT_CAST (buffer),
          audio_buffer_quark)
      && !gst_mini_object_get_qdata (GST_MINI_OBJECT_CAST (buffer),
          video_buffer_quark))
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_TAG_MEMORY);

  GST_BUFFER_POOL_CLASS (gst_aja_buffer_pool_parent_class)->release_buffer
      (pool, buffer);
}

static void
gst_aja_buffer_pool_class_init (GstAjaBufferPoolClass * klass)
{
  GstBufferPoolClass *buffer_pool_class = (GstBufferPoolClass *) klass;

  buffer_pool_class->set_config = gst_aja_buffer_pool_set_config;
  buffer_pool_class->alloc_buffer = gst_aja_buffer_pool_alloc_buffer;
  buffer_pool_class->reset_buffer = gst_aja_buffer_pool_reset_buffer;
  buffer_pool_class->release_buffer = gst_aja_buffer_pool_release_buffer;

  video_buffer_quark = g_quark_from_static_string ("AjaVideoBuff");
  audio_buffer_quark = g_quark_from_static_string ("AjaAudioBuff");
}

static void
gst_aja_buffer_pool_init (GstAjaBufferPool * buffer_pool)
{
}

GstBufferPool *
gst_aja_buffer_pool_new (void)
{
  GstAjaBufferPool *self =
      GST_AJA_BUFFER_POOL (g_object_new (GST_TYPE_AJA_BUFFER_POOL, NULL));

  return GST_BUFFER_POOL_CAST (self);
}

AjaVideoBuff *
gst_aja_buffer_get_video_buff (GstBuffer * buffer)
{
  return (AjaVideoBuff *)
      gst_mini_object_get_qdata (GST_MINI_OBJECT_CAST (buffer),
      video_buffer_quark);
}

AjaAudioBuff *
gst_aja_buffer_get_audio_buff (GstBuffer * buffer)
{
  return (AjaAudioBuff *)
      gst_mini_object_get_qdata (GST_MINI_OBJECT_CAST (buffer),
      audio_buffer_quark);
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
  GST_DEBUG_CATEGORY_INIT (gst_aja_debug, "aja", 0,
      "debug category for aja plugin");

  gst_element_register (plugin, "ajavideosrc", GST_RANK_NONE,
      GST_TYPE_AJA_VIDEO_SRC);
  //  gst_element_register (plugin, "ajahevcsrc", GST_RANK_NONE, GST_TYPE_AJA_HEVC_SRC);
  gst_element_register (plugin, "ajaaudiosrc", GST_RANK_NONE,
      GST_TYPE_AJA_AUDIO_SRC);

  gst_device_provider_register (plugin, "ajadeviceprovider",
        GST_RANK_PRIMARY, GST_TYPE_AJA_DEVICE_PROVIDER);

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
