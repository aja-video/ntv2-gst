/*
 * Copyright (C) 2019 Mathieu Duponchelle <mathieu@centricular.com>
 * Copyright (C) 2019 Sebastian Dröge <sebastian@centricular.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstajadeviceprovider.h"
#include "gstaja.h"

static GstDevice *gst_aja_device_new (NTV2DeviceInfo & device, gboolean video);

G_DEFINE_TYPE (GstAjaDeviceProvider, gst_aja_device_provider,
    GST_TYPE_DEVICE_PROVIDER);

static void
gst_aja_device_provider_init (GstAjaDeviceProvider * self)
{
}

static GList *
gst_aja_device_provider_probe (GstDeviceProvider * provider)
{
  GList *ret = NULL;

  CNTV2DeviceScanner scanner;

  NTV2DeviceInfoList devices = scanner.GetDeviceInfoList ();
  for (NTV2DeviceInfoList::iterator it = devices.begin (); it != devices.end ();
      it++) {
    // Skip non-input devices
    if (it->numVidInputs == 0)
      continue;

    ret = g_list_prepend (ret, gst_aja_device_new (*it, TRUE));
    ret = g_list_prepend (ret, gst_aja_device_new (*it, FALSE));
  }

  ret = g_list_reverse (ret);

  return ret;
}

static void
gst_aja_device_provider_class_init (GstAjaDeviceProviderClass * klass)
{
  GstDeviceProviderClass *dm_class = GST_DEVICE_PROVIDER_CLASS (klass);

  dm_class->probe = GST_DEBUG_FUNCPTR (gst_aja_device_provider_probe);

  gst_device_provider_class_set_static_metadata (dm_class,
      "AJA Device Provider", "Source/Audio/Video",
      "List and provides AJA capture devices",
      "Sebastian Dröge <sebastian@centricular.com>");
}

G_DEFINE_TYPE (GstAjaDevice, gst_aja_device, GST_TYPE_DEVICE);

static void
gst_aja_device_init (GstAjaDevice * self)
{
}

static GstElement *
gst_aja_device_create_element (GstDevice * device, const gchar * name)
{
  GstAjaDevice *self = GST_AJA_DEVICE (device);
  GstElement *ret = NULL;

  if (self->video) {
    ret = gst_element_factory_make ("ajavideosrc", name);
  } else {
    ret = gst_element_factory_make ("ajaaudiosrc", name);
  }

  if (ret) {
    gchar *device_identifier = g_strdup_printf ("%u", self->device_index);

    g_object_set (ret, "device-identifier", device_identifier, NULL);
    g_free (device_identifier);
  }

  return ret;
}

static void
gst_aja_device_class_init (GstAjaDeviceClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstDeviceClass *gst_device_class = GST_DEVICE_CLASS (klass);

  gst_device_class->create_element =
      GST_DEBUG_FUNCPTR (gst_aja_device_create_element);
}

static GstDevice *
gst_aja_device_new (NTV2DeviceInfo & device, gboolean video)
{
  GstDevice *ret;
  gchar *display_name;
  const gchar *device_class;
  GstCaps *caps = NULL;
  GstStructure *properties;

  device_class = video ? "Video/Source" : "Audio/Source";
  display_name =
      g_strdup_printf ("AJA %s (%s)", device.deviceIdentifier.c_str (),
      video ? "Video" : "Audio");

  if (video)
    caps = gst_aja_mode_get_template_caps_raw ();
  else
    caps =
        gst_caps_from_string
        ("audio/x-raw, format=S32LE, channels=[1,2], rate=48000, "
        "layout=interleaved;"
        "audio/x-raw, format=S32LE, channels=[3,16], rate=48000, "
        "channel-mask = (bitmask) 0, layout=interleaved;");
  properties = gst_structure_new_empty ("properties");

  gst_structure_set (properties,
      "device-id", G_TYPE_UINT, device.deviceID,
      "device-index", G_TYPE_UINT, device.deviceIndex,
      "pci-slot", G_TYPE_UINT, device.pciSlot,
      "serial-number", G_TYPE_UINT64, device.deviceSerialNumber,
      "device-identifier", G_TYPE_STRING, device.deviceIdentifier.c_str (),
      "num-vid-inputs", G_TYPE_UINT, device.numVidInputs,
      "num-anlg-vid-inputs", G_TYPE_UINT, device.numAnlgVidInputs,
      "num-hdmi-vid-inputs", G_TYPE_UINT, device.numHDMIVidInputs,
      "num-audio-streams", G_TYPE_UINT, device.numAudioStreams,
      "num-analog-audio-input-channels", G_TYPE_UINT,
      device.numAnalogAudioInputChannels, "num-aes-audio-input-channels",
      G_TYPE_UINT, device.numAESAudioInputChannels,
      "num-embedded-audio-input-channels", G_TYPE_UINT,
      device.numEmbeddedAudioInputChannels, "num-hdmi-audio-input-channels",
      G_TYPE_UINT, device.numHDMIAudioInputChannels, "dual-link-support",
      G_TYPE_BOOLEAN, device.dualLinkSupport, "sdi-3g-support", G_TYPE_BOOLEAN,
      device.sdi3GSupport, "sdi-12g-support", G_TYPE_BOOLEAN,
      device.sdi12GSupport, "ip-support", G_TYPE_BOOLEAN, device.ipSupport,
      "bi-directional-sdi", G_TYPE_BOOLEAN, device.biDirectionalSDI,
      "ltc-in-support", G_TYPE_BOOLEAN, device.ltcInSupport,
      "ltc-in-on-ref-port", G_TYPE_BOOLEAN, device.ltcInOnRefPort, "2k-support",
      G_TYPE_BOOLEAN, device.has2KSupport, "4k-support", G_TYPE_BOOLEAN,
      device.has4KSupport, NULL);

  ret = GST_DEVICE (g_object_new (GST_TYPE_AJA_DEVICE,
          "display-name", display_name,
          "device-class", device_class, "caps", caps, "properties", properties,
          NULL));

  g_free (display_name);
  gst_caps_unref (caps);
  gst_structure_free (properties);

  GST_AJA_DEVICE (ret)->video = video;
  GST_AJA_DEVICE (ret)->device_index = device.deviceIndex;

  return ret;
}
