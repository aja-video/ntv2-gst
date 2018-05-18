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
/**
 * SECTION:element-gstaja_audio_sink
 *
 * The aja_audio_sink element does FIXME stuff.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v fakesrc ! aja_audio_sink ! FIXME ! fakesink
 * ]|
 * FIXME Describe what the pipeline does.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/audio/gstaudiosink.h>
#include "gstajaaudiosink.h"

GST_DEBUG_CATEGORY_STATIC (gst_aja_audio_sink_debug);
#define GST_CAT_DEFAULT gst_aja_audio_sink_debug

/* prototypes */


static void gst_aja_audio_sink_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_aja_audio_sink_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_aja_audio_sink_dispose (GObject * object);
static void gst_aja_audio_sink_finalize (GObject * object);

static gboolean gst_aja_audio_sink_open (GstAudioSink * sink);
static gboolean gst_aja_audio_sink_prepare (GstAudioSink * sink,
    GstAudioRingBufferSpec * spec);
static gboolean gst_aja_audio_sink_unprepare (GstAudioSink * sink);
static gboolean gst_aja_audio_sink_close (GstAudioSink * sink);
static gint gst_aja_audio_sink_write (GstAudioSink * sink, gpointer data,
    guint length);
static guint gst_aja_audio_sink_delay (GstAudioSink * sink);
static void gst_aja_audio_sink_reset (GstAudioSink * sink);

enum
{
  PROP_0
};

/* pad templates */

/* FIXME add/remove the formats that you want to support */
static GstStaticPadTemplate gst_aja_audio_sink_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw,format=S16LE,rate=[1,max],"
      "channels=[1,max],layout=interleaved")
    );


/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstAjaaudiosink, gst_aja_audio_sink, GST_TYPE_AUDIO_SINK,
  GST_DEBUG_CATEGORY_INIT (gst_aja_audio_sink_debug, "aja_audio_sink", 0,
  "debug category for aja_audio_sink element"));

static void
gst_aja_audio_sink_class_init (GstAjaaudiosinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstAudioSinkClass *audio_sink_class = GST_AUDIO_SINK_CLASS (klass);

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_pad_template (GST_ELEMENT_CLASS(klass),
      gst_static_pad_template_get (&gst_aja_audio_sink_sink_template));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS(klass),
      "FIXME Long name", "Generic", "FIXME Description",
      "FIXME <fixme@example.com>");

  gobject_class->set_property = gst_aja_audio_sink_set_property;
  gobject_class->get_property = gst_aja_audio_sink_get_property;
  gobject_class->dispose = gst_aja_audio_sink_dispose;
  gobject_class->finalize = gst_aja_audio_sink_finalize;
  audio_sink_class->open = GST_DEBUG_FUNCPTR (gst_aja_audio_sink_open);
  audio_sink_class->prepare = GST_DEBUG_FUNCPTR (gst_aja_audio_sink_prepare);
  audio_sink_class->unprepare = GST_DEBUG_FUNCPTR (gst_aja_audio_sink_unprepare);
  audio_sink_class->close = GST_DEBUG_FUNCPTR (gst_aja_audio_sink_close);
  audio_sink_class->write = GST_DEBUG_FUNCPTR (gst_aja_audio_sink_write);
  audio_sink_class->delay = GST_DEBUG_FUNCPTR (gst_aja_audio_sink_delay);
  audio_sink_class->reset = GST_DEBUG_FUNCPTR (gst_aja_audio_sink_reset);

}

static void
gst_aja_audio_sink_init (GstAjaaudiosink *aja_audio_sink)
{
}

void
gst_aja_audio_sink_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAjaaudiosink *aja_audio_sink = GST_AJA_AUDIO_SINK (object);

  GST_DEBUG_OBJECT (aja_audio_sink, "set_property");

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_aja_audio_sink_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstAjaaudiosink *aja_audio_sink = GST_AJA_AUDIO_SINK (object);

  GST_DEBUG_OBJECT (aja_audio_sink, "get_property");

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_aja_audio_sink_dispose (GObject * object)
{
  GstAjaaudiosink *aja_audio_sink = GST_AJA_AUDIO_SINK (object);

  GST_DEBUG_OBJECT (aja_audio_sink, "dispose");

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (gst_aja_audio_sink_parent_class)->dispose (object);
}

void
gst_aja_audio_sink_finalize (GObject * object)
{
  GstAjaaudiosink *aja_audio_sink = GST_AJA_AUDIO_SINK (object);

  GST_DEBUG_OBJECT (aja_audio_sink, "finalize");

  /* clean up object here */

  G_OBJECT_CLASS (gst_aja_audio_sink_parent_class)->finalize (object);
}

/* open the device with given specs */
static gboolean
gst_aja_audio_sink_open (GstAudioSink * sink)
{
  GstAjaaudiosink *aja_audio_sink = GST_AJA_AUDIO_SINK (sink);

  GST_DEBUG_OBJECT (aja_audio_sink, "open");

  return TRUE;
}

/* prepare resources and state to operate with the given specs */
static gboolean
gst_aja_audio_sink_prepare (GstAudioSink * sink, GstAudioRingBufferSpec * spec)
{
  GstAjaaudiosink *aja_audio_sink = GST_AJA_AUDIO_SINK (sink);

  GST_DEBUG_OBJECT (aja_audio_sink, "prepare");

  return TRUE;
}

/* undo anything that was done in prepare() */
static gboolean
gst_aja_audio_sink_unprepare (GstAudioSink * sink)
{
  GstAjaaudiosink *aja_audio_sink = GST_AJA_AUDIO_SINK (sink);

  GST_DEBUG_OBJECT (aja_audio_sink, "unprepare");

  return TRUE;
}

/* close the device */
static gboolean
gst_aja_audio_sink_close (GstAudioSink * sink)
{
  GstAjaaudiosink *aja_audio_sink = GST_AJA_AUDIO_SINK (sink);

  GST_DEBUG_OBJECT (aja_audio_sink, "close");

  return TRUE;
}

/* write samples to the device */
static gint
gst_aja_audio_sink_write (GstAudioSink * sink, gpointer data, guint length)
{
  GstAjaaudiosink *aja_audio_sink = GST_AJA_AUDIO_SINK (sink);

  GST_DEBUG_OBJECT (aja_audio_sink, "write");

  return 0;
}

/* get number of samples queued in the device */
static guint
gst_aja_audio_sink_delay (GstAudioSink * sink)
{
  GstAjaaudiosink *aja_audio_sink = GST_AJA_AUDIO_SINK (sink);

  GST_DEBUG_OBJECT (aja_audio_sink, "delay");

  return 0;
}

/* reset the audio device, unblock from a write */
static void
gst_aja_audio_sink_reset (GstAudioSink * sink)
{
  GstAjaaudiosink *aja_audio_sink = GST_AJA_AUDIO_SINK (sink);

  GST_DEBUG_OBJECT (aja_audio_sink, "reset");

}

