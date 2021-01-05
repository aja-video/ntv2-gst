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

#include "gstajavideosrc.h"
#include "gstajavideosrc.h"

#if GST_CHECK_VERSION(1, 15, 0)
#include <gst/video/video-anc.h>
#endif

GST_DEBUG_CATEGORY_STATIC (gst_aja_video_src_debug);
#define GST_CAT_DEFAULT gst_aja_video_src_debug

#define DEFAULT_MODE               (GST_AJA_MODE_RAW_720_8_5994p)
#define DEFAULT_DEVICE_IDENTIFIER  ("0")
#define DEFAULT_INPUT_MODE         (GST_AJA_VIDEO_INPUT_MODE_SDI)
#define DEFAULT_SDI_INPUT_MODE     (SDI_INPUT_MODE_SINGLE_LINK)
#define DEFAULT_INPUT_CHANNEL      (0)
#define DEFAULT_PASSTHROUGH        (FALSE)
#define DEFAULT_QUEUE_SIZE         (5)
#define DEFAULT_OUTPUT_STREAM_TIME (FALSE)
#define DEFAULT_SKIP_FIRST_TIME    (0)
#define DEFAULT_TIMECODE_MODE	   (GST_AJA_TIMECODE_MODE_VITC1)
#define DEFAULT_OUTPUT_CC	   (FALSE)
#define DEFAULT_CAPTURE_CPU_CORE   ((guint)-1)

enum
{
  PROP_0,
  PROP_MODE,
  PROP_DEVICE_IDENTIFIER,
  PROP_INPUT_MODE,
  PROP_SDI_INPUT_MODE,
  PROP_INPUT_CHANNEL,
  PROP_PASSTHROUGH,
  PROP_QUEUE_SIZE,
  PROP_OUTPUT_STREAM_TIME,
  PROP_SKIP_FIRST_TIME,
  PROP_TIMECODE_MODE,
  PROP_OUTPUT_CC,
  PROP_SIGNAL,
  PROP_CAPTURE_CPU_CORE,
};

typedef enum {
  NO_CHANGE,
  GOT_SIGNAL,
  RECOVERED_SIGNAL,
  LOST_SIGNAL,
} SignalChange;

typedef struct
{
  GstAjaVideoSrc *video_src;
  SignalChange signal_change;
  AjaVideoBuff *video_buff;
  GstClockTime capture_time;
  GstClockTime stream_time;
  GstAjaModeRawEnum mode;
  gboolean first_buffer;
} AjaCaptureVideoFrame;

static void
aja_capture_video_frame_clear (void *data)
{
  AjaCaptureVideoFrame *frame = (AjaCaptureVideoFrame *) data;

  if ((frame->video_buff) && (frame->video_src->input) && (frame->video_src->input->ntv2AV))
    frame->video_src->input->ntv2AV->ReleaseVideoBuffer (frame->video_buff);
  memset(frame, 0, sizeof (*frame));
}

static void gst_aja_video_src_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec);
static void gst_aja_video_src_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec);

static void gst_aja_video_src_finalize (GObject * object);

static gboolean gst_aja_video_src_query (GstBaseSrc * src, GstQuery * query);
static GstCaps * gst_aja_video_src_get_caps (GstBaseSrc * src, GstCaps * filter);
static gboolean gst_aja_video_src_unlock (GstBaseSrc * src);
static gboolean gst_aja_video_src_unlock_stop (GstBaseSrc * src);

static GstFlowReturn gst_aja_video_src_create (GstPushSrc * psrc,
    GstBuffer ** buffer);

static gboolean gst_aja_video_src_open (GstAjaVideoSrc * video_src);
static gboolean gst_aja_video_src_close (GstAjaVideoSrc * video_src);

static gboolean gst_aja_video_src_stop (GstAjaVideoSrc * src);

static void gst_aja_video_src_start_streams (GstElement * element);

static GstStateChangeReturn gst_aja_video_src_change_state (GstElement *
    element, GstStateChange transition);

static bool gst_aja_video_src_video_callback (void *refcon, void *msg);

#define parent_class gst_aja_video_src_parent_class
G_DEFINE_TYPE (GstAjaVideoSrc, gst_aja_video_src, GST_TYPE_PUSH_SRC);

static void
gst_aja_video_src_class_init (GstAjaVideoSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *basesrc_class = GST_BASE_SRC_CLASS (klass);
  GstPushSrcClass *pushsrc_class = GST_PUSH_SRC_CLASS (klass);
  GstCaps *templ_caps;

  gobject_class->set_property = gst_aja_video_src_set_property;
  gobject_class->get_property = gst_aja_video_src_get_property;
  gobject_class->finalize = gst_aja_video_src_finalize;

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_aja_video_src_change_state);

  basesrc_class->query = GST_DEBUG_FUNCPTR (gst_aja_video_src_query);
  basesrc_class->negotiate = NULL;
  basesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_aja_video_src_get_caps);
  basesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_aja_video_src_unlock);
  basesrc_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_aja_video_src_unlock_stop);

  pushsrc_class->create = GST_DEBUG_FUNCPTR (gst_aja_video_src_create);

  g_object_class_install_property (gobject_class, PROP_MODE,
      g_param_spec_enum ("mode", "Playback Mode",
          "Video Mode to use for playback",
          GST_TYPE_AJA_MODE_RAW, DEFAULT_MODE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              G_PARAM_CONSTRUCT)));

  g_object_class_install_property (gobject_class, PROP_DEVICE_IDENTIFIER,
      g_param_spec_string ("device-identifier",
          "Device identifier",
          "Input device instance to use",
          DEFAULT_DEVICE_IDENTIFIER,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              G_PARAM_CONSTRUCT)));

  g_object_class_install_property (gobject_class, PROP_INPUT_MODE,
      g_param_spec_enum ("input-mode", "Input Mode",
          "Video Input Mode to use for playback",
          GST_TYPE_AJA_VIDEO_INPUT_MODE, DEFAULT_INPUT_MODE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              G_PARAM_CONSTRUCT)));

  g_object_class_install_property (gobject_class, PROP_SDI_INPUT_MODE,
      g_param_spec_enum ("sdi-input-mode", "SDI Input Mode",
          "SDI Input Mode to use for playback",
          GST_TYPE_AJA_SDI_INPUT_MODE, DEFAULT_SDI_INPUT_MODE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              G_PARAM_CONSTRUCT)));

  g_object_class_install_property (gobject_class, PROP_INPUT_CHANNEL,
      g_param_spec_uint ("input-channel",
          "Input channel",
          "Input channel to use",
          0, NTV2_MAX_NUM_CHANNELS - 1, DEFAULT_INPUT_CHANNEL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              G_PARAM_CONSTRUCT)));

  g_object_class_install_property (gobject_class, PROP_PASSTHROUGH,
      g_param_spec_boolean ("passthrough",
          "Passthrough",
          "Passthrough on bidirectional devices by halfing the number of input channels",
          DEFAULT_PASSTHROUGH,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              G_PARAM_CONSTRUCT)));

  g_object_class_install_property (gobject_class, PROP_QUEUE_SIZE,
      g_param_spec_uint ("queue-size",
          "Queue Size",
          "Size of internal queue in number of video frames",
          1, G_MAXINT, DEFAULT_QUEUE_SIZE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_OUTPUT_STREAM_TIME,
      g_param_spec_boolean ("output-stream-time", "Output Stream Time",
          "Output stream time directly instead of translating to pipeline clock",
          DEFAULT_OUTPUT_STREAM_TIME,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_SKIP_FIRST_TIME,
      g_param_spec_uint64 ("skip-first-time", "Skip First Time",
          "Skip that much time of initial frames after starting", 0,
          G_MAXUINT64, DEFAULT_SKIP_FIRST_TIME,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_TIMECODE_MODE,
      g_param_spec_enum ("timecode-mode", "Timecode Mode",
          "Timecode Mode to use for extraction",
          GST_TYPE_AJA_TIMECODE_MODE, DEFAULT_TIMECODE_MODE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              G_PARAM_CONSTRUCT)));

  g_object_class_install_property (gobject_class, PROP_OUTPUT_CC,
      g_param_spec_boolean ("output-cc", "Output Closed Caption",
          "Extract and output CC as GstMeta (if present)",
          DEFAULT_OUTPUT_CC,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_SIGNAL,
      g_param_spec_boolean ("signal", "Input signal available",
          "True if there is a valid input signal available",
          FALSE, (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_CAPTURE_CPU_CORE,
      g_param_spec_uint ("capture-cpu-core",
          "Capture CPU Core",
          "Sets the affinity of the capture thread to this CPU core (-1=disabled)",
          0, G_MAXUINT, DEFAULT_CAPTURE_CPU_CORE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
              G_PARAM_CONSTRUCT)));

  templ_caps = gst_aja_mode_get_template_caps_raw ();
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, templ_caps));
  gst_caps_unref (templ_caps);

  gst_element_class_set_static_metadata (element_class, "Aja Raw Source",
      "Video/Src", "Aja Raw RT Video Source", "PSM <philm@aja.com>");

  GST_DEBUG_CATEGORY_INIT (gst_aja_video_src_debug, "ajavideosrc", 0,
      "debug category for ajavideosrc element");
}

static void
gst_aja_video_src_init (GstAjaVideoSrc * src)
{
  GST_DEBUG_OBJECT (src, "init");

  src->modeEnum = DEFAULT_MODE;
  src->input_channel = DEFAULT_INPUT_CHANNEL;
  src->input_mode = DEFAULT_INPUT_MODE;
  src->sdi_input_mode = DEFAULT_SDI_INPUT_MODE;
  src->passthrough = DEFAULT_PASSTHROUGH;
  src->device_identifier = g_strdup (DEFAULT_DEVICE_IDENTIFIER);
  src->queue_size = DEFAULT_QUEUE_SIZE;
  src->output_stream_time = DEFAULT_OUTPUT_STREAM_TIME;
  src->skip_first_time = DEFAULT_SKIP_FIRST_TIME;
  src->timecode_mode = DEFAULT_TIMECODE_MODE;
  src->capture_cpu_core = DEFAULT_CAPTURE_CPU_CORE;

  src->window_size = 64;
  src->times = g_new (GstClockTime, 4 * src->window_size);
  src->times_temp = src->times + 2 * src->window_size;
  src->window_fill = 0;
  src->window_skip = 1;
  src->window_skip_count = 0;

  src->signal_state = SIGNAL_STATE_UNKNOWN;

  gst_base_src_set_live (GST_BASE_SRC (src), TRUE);
  gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);

  gst_pad_use_fixed_caps (GST_BASE_SRC_PAD (src));

  g_mutex_init (&src->lock);
  g_cond_init (&src->cond);

  src->current_frames = gst_queue_array_new_for_struct (sizeof (AjaCaptureVideoFrame), DEFAULT_QUEUE_SIZE);

  src->skipped_last = 0;
  src->skipped_overall = 0;
  src->skip_from_timestamp = GST_CLOCK_TIME_NONE;
  src->skip_to_timestamp = GST_CLOCK_TIME_NONE;
}

void
gst_aja_video_src_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAjaVideoSrc *src = GST_AJA_VIDEO_SRC (object);
  GST_DEBUG_OBJECT (src, "set_property");

  switch (property_id) {
    case PROP_MODE:
      src->modeEnum = (GstAjaModeRawEnum) g_value_get_enum (value);
      break;

    case PROP_DEVICE_IDENTIFIER:
      g_free (src->device_identifier);
      src->device_identifier = g_value_dup_string (value);
      break;

    case PROP_INPUT_MODE:
      src->input_mode = (GstAjaVideoInputMode) g_value_get_enum (value);
      break;

    case PROP_SDI_INPUT_MODE:
      src->sdi_input_mode = (SDIInputMode) g_value_get_enum (value);
      break;

    case PROP_INPUT_CHANNEL:
      src->input_channel = g_value_get_uint (value);
      break;

    case PROP_PASSTHROUGH:
      src->passthrough = g_value_get_boolean (value);
      break;

    case PROP_QUEUE_SIZE:
      src->queue_size = g_value_get_uint (value);
      break;

    case PROP_OUTPUT_STREAM_TIME:
      src->output_stream_time = g_value_get_boolean (value);
      break;

    case PROP_SKIP_FIRST_TIME:
      src->skip_first_time = g_value_get_uint64 (value);
      break;

    case PROP_TIMECODE_MODE:
      src->timecode_mode = (GstAjaTimecodeMode) g_value_get_enum (value);
      if (src->input && src->input->ntv2AV) {
        NTV2TCIndex timecode_mode;

        switch (src->timecode_mode) {
          case GST_AJA_TIMECODE_MODE_VITC1:
            timecode_mode = NTV2_TCINDEX_SDI1;
            break;

          case GST_AJA_TIMECODE_MODE_VITC2:
            timecode_mode = NTV2_TCINDEX_SDI1_2;
            break;

          case GST_AJA_TIMECODE_MODE_ANALOG_LTC1:
            timecode_mode = NTV2_TCINDEX_LTC1;
            break;

          case GST_AJA_TIMECODE_MODE_ANALOG_LTC2:
            timecode_mode = NTV2_TCINDEX_LTC2;
            break;

          case GST_AJA_TIMECODE_MODE_ATC_LTC:
            timecode_mode = NTV2_TCINDEX_SDI1_LTC;
            break;

          default:
            g_assert_not_reached ();
            break;
        }
        src->input->ntv2AV->UpdateTimecodeIndex(timecode_mode);
      }

      break;

    case PROP_OUTPUT_CC:
#if GST_CHECK_VERSION(1, 15, 0)
      src->output_cc = g_value_get_boolean (value);
#else
      src->output_cc = FALSE;
#endif
      break;

    case PROP_CAPTURE_CPU_CORE:
      src->capture_cpu_core = g_value_get_uint (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_aja_video_src_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstAjaVideoSrc *src = GST_AJA_VIDEO_SRC (object);
  GST_DEBUG_OBJECT (src, "get_property");

  switch (property_id) {
    case PROP_MODE:
      g_value_set_enum (value, src->modeEnum);
      break;

    case PROP_DEVICE_IDENTIFIER:
      g_value_set_string (value, src->device_identifier);
      break;

    case PROP_INPUT_MODE:
      g_value_set_enum (value, src->input_mode);
      break;

    case PROP_SDI_INPUT_MODE:
      g_value_set_enum (value, src->sdi_input_mode);
      break;

    case PROP_INPUT_CHANNEL:
      g_value_set_uint (value, src->input_channel);
      break;

    case PROP_PASSTHROUGH:
      g_value_set_boolean (value, src->passthrough);
      break;

    case PROP_QUEUE_SIZE:
      g_value_set_uint (value, src->queue_size);
      break;

    case PROP_OUTPUT_STREAM_TIME:
      g_value_set_boolean (value, src->output_stream_time);
      break;

    case PROP_SKIP_FIRST_TIME:
      g_value_set_uint64 (value, src->skip_first_time);
      break;

    case PROP_TIMECODE_MODE:
      g_value_set_enum (value, src->timecode_mode);
      break;

    case PROP_OUTPUT_CC:
      g_value_set_boolean (value, src->output_cc);
      break;

    case PROP_SIGNAL:
      g_value_set_boolean (value, src->signal_state == SIGNAL_STATE_AVAILABLE);
      break;

    case PROP_CAPTURE_CPU_CORE:
      g_value_set_uint (value, src->capture_cpu_core);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_aja_video_src_finalize (GObject * object)
{
  GstAjaVideoSrc *src = GST_AJA_VIDEO_SRC (object);

  GST_DEBUG_OBJECT (src, "finalize");

  AjaCaptureVideoFrame *frame;
  while ((frame = (AjaCaptureVideoFrame *) gst_queue_array_pop_head_struct (src->current_frames))) {
    aja_capture_video_frame_clear (frame);
  }
  gst_queue_array_free (src->current_frames);
  src->current_frames = NULL;

  g_free (src->device_identifier);
  src->device_identifier = NULL;

  g_free (src->times);
  src->times = NULL;
  g_mutex_clear (&src->lock);
  g_cond_clear (&src->cond);

  // Call parent class
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* notify the subclass of new caps */
static gboolean
gst_aja_video_src_start (GstAjaVideoSrc *src)
{
  GST_DEBUG_OBJECT (src, "start");

  const GstAjaMode *mode;

  g_mutex_lock (&src->input->lock);
  if (src->input->video_enabled) {
    g_mutex_unlock (&src->input->lock);
    return TRUE;
  }

  mode = gst_aja_get_mode_raw (src->modeEnum);
  g_assert (mode != NULL);

  src->input->mode = mode;
  src->input->video_enabled = TRUE;
  if (src->input->start_streams)
    src->input->start_streams (src->input->videosrc);
  g_mutex_unlock (&src->input->lock);

  src->skipped_last = 0;
  src->skipped_overall = 0;
  src->skip_from_timestamp = GST_CLOCK_TIME_NONE;
  src->skip_to_timestamp = GST_CLOCK_TIME_NONE;

  return TRUE;
}

static GstCaps *
gst_aja_video_src_get_caps (GstBaseSrc * bsrc, GstCaps * filter)
{
  GstAjaVideoSrc *src = GST_AJA_VIDEO_SRC (bsrc);
  GstCaps *caps;

  caps = gst_aja_mode_get_caps_raw (src->modeEnum);
  if (filter) {
    GstCaps *tmp = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = tmp;
  }

  return caps;
}

static gboolean
gst_aja_video_src_query (GstBaseSrc * bsrc, GstQuery * query)
{
  GstAjaVideoSrc *src = GST_AJA_VIDEO_SRC (bsrc);
  GST_DEBUG_OBJECT (src, "query");

  gboolean ret = TRUE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      if (src->input) {
        g_mutex_lock (&src->input->lock);
        if (src->input->mode) {
          GstClockTime min, max;

          min =
              gst_util_uint64_scale_ceil (GST_SECOND, src->input->mode->fps_d,
              src->input->mode->fps_n);
          max = src->queue_size * min;

          gst_query_set_latency (query, TRUE, min, max);
          ret = TRUE;
        } else {
          ret = FALSE;
        }
        g_mutex_unlock (&src->input->lock);
      } else {
        ret = FALSE;
      }

      break;
    }

    default:
      ret = GST_BASE_SRC_CLASS (parent_class)->query (bsrc, query);
      break;
  }

  return ret;
}

// unlock any pending access to the resource. subclasses should unlock any function ASAP
static gboolean
gst_aja_video_src_unlock (GstBaseSrc * bsrc)
{
  GstAjaVideoSrc *src = GST_AJA_VIDEO_SRC (bsrc);
  GST_DEBUG_OBJECT (src, "unlock");

  g_mutex_lock (&src->lock);
  src->flushing = TRUE;
  g_cond_signal (&src->cond);
  g_mutex_unlock (&src->lock);

  return TRUE;
}

// Clear any pending unlock request, as we succeeded in unlocking
static gboolean
gst_aja_video_src_unlock_stop (GstBaseSrc * bsrc)
{
  GstAjaVideoSrc *src = GST_AJA_VIDEO_SRC (bsrc);
  GST_DEBUG_OBJECT (src, "unlock_stop");

  g_mutex_lock (&src->lock);
  src->flushing = FALSE;

  AjaCaptureVideoFrame *frame;
  while ((frame = (AjaCaptureVideoFrame *) gst_queue_array_pop_head_struct (src->current_frames))) {
    aja_capture_video_frame_clear (frame);
  }
  g_mutex_unlock (&src->lock);

  return TRUE;
}

static gboolean
gst_aja_video_src_open (GstAjaVideoSrc * src)
{
  AJAStatus status;
  const GstAjaMode *mode;
  NTV2InputSource input_source;
  NTV2TCIndex timecode_mode;

  GST_DEBUG_OBJECT (src, "open");

  src->input =
      gst_aja_acquire_input (src->device_identifier, src->input_channel,
      GST_ELEMENT_CAST (src), FALSE);
  if (!src->input) {
    GST_ERROR_OBJECT (src, "Failed to acquire input");
    return FALSE;
  }

  mode = gst_aja_get_mode_raw (src->modeEnum);
  g_assert (mode != NULL);

  g_mutex_lock (&src->input->lock);
  src->input->mode = mode;
  src->input->start_streams = gst_aja_video_src_start_streams;

  status = src->input->ntv2AV->Open ();
  if (!AJA_SUCCESS (status)) {
    GST_ERROR_OBJECT (src, "Failed to open input");
    g_mutex_unlock (&src->input->lock);
    return FALSE;
  }

  switch (src->input_mode) {
    case GST_AJA_VIDEO_INPUT_MODE_SDI:
      input_source = NTV2_INPUTSOURCE_SDI1;
      break;

    case GST_AJA_VIDEO_INPUT_MODE_HDMI:
      input_source = NTV2_INPUTSOURCE_HDMI1;
      break;

    case GST_AJA_VIDEO_INPUT_MODE_ANALOG:
      input_source = NTV2_INPUTSOURCE_ANALOG1;
      break;

    default:
      g_assert_not_reached ();
      break;
  }

  switch (src->timecode_mode) {
    case GST_AJA_TIMECODE_MODE_VITC1:
      timecode_mode = NTV2_TCINDEX_SDI1;
      break;

    case GST_AJA_TIMECODE_MODE_VITC2:
      timecode_mode = NTV2_TCINDEX_SDI1_2;
      break;

    case GST_AJA_TIMECODE_MODE_ANALOG_LTC1:
      timecode_mode = NTV2_TCINDEX_LTC1;
      break;

    case GST_AJA_TIMECODE_MODE_ANALOG_LTC2:
      timecode_mode = NTV2_TCINDEX_LTC2;
      break;

    case GST_AJA_TIMECODE_MODE_ATC_LTC:
      timecode_mode = NTV2_TCINDEX_SDI1_LTC;
      break;

    default:
      g_assert_not_reached ();
      break;
  }

  status = src->input->ntv2AV->Init (src->input->mode->videoFormat,
      input_source,
      src->input->mode->bitDepth,
      src->input->mode->is422,
      false,
      src->sdi_input_mode, timecode_mode, false, src->output_cc ? true : false,
      src->passthrough ? true : false, src->capture_cpu_core);
  if (!AJA_SUCCESS (status)) {
    GST_ERROR_OBJECT (src, "Failed to initialize input");
    g_mutex_unlock (&src->input->lock);
    return FALSE;
  }

  g_mutex_unlock (&src->input->lock);

  return TRUE;
}

static gboolean
gst_aja_video_src_close (GstAjaVideoSrc * src)
{
  GST_DEBUG_OBJECT (src, "close");

  if (src->input) {
    g_mutex_lock (&src->input->lock);

    if (src->input->ntv2AV) {
      src->input->ntv2AV->Quit ();
      src->input->ntv2AV->Close ();
      delete src->input->ntv2AV;
      src->input->ntv2AV = NULL;
      GST_DEBUG_OBJECT (src, "shut down ntv2HEVC");
    }

    src->input->mode = NULL;
    src->input->video_enabled = FALSE;
    gst_object_unref (src->input->videosrc);
    src->input->videosrc = NULL;
    src->input->start_streams = NULL;

    g_mutex_unlock (&src->input->lock);
    src->input = NULL;
  }

  return TRUE;
}

static gboolean
gst_aja_video_src_stop (GstAjaVideoSrc * src)
{
  GST_DEBUG_OBJECT (src, "stop");

  if (src->input && src->input->video_enabled) {
    g_mutex_lock (&src->input->lock);
    src->input->ntv2AV->Quit ();
    src->input->video_enabled = FALSE;
    src->input->ntv2AV->SetCallback (VIDEO_CALLBACK, 0, 0);
    g_mutex_unlock (&src->input->lock);
  }

  AjaCaptureVideoFrame *frame;
  while ((frame = (AjaCaptureVideoFrame *) gst_queue_array_pop_head_struct (src->current_frames))) {
    aja_capture_video_frame_clear (frame);
  }

  src->signal_state = SIGNAL_STATE_UNKNOWN;

  return TRUE;
}

static void
gst_aja_video_src_start_streams (GstElement * element)
{
  GstAjaVideoSrc *src = GST_AJA_VIDEO_SRC (element);
  GST_DEBUG_OBJECT (src, "start_streams");

  if (src->input->video_enabled && (!src->input->audiosrc
          || src->input->audio_enabled)
      && (GST_STATE (src) == GST_STATE_PLAYING
          || GST_STATE_PENDING (src) == GST_STATE_PLAYING)) {
    GST_DEBUG_OBJECT (src, "Starting streams");

    g_mutex_lock (&src->lock);
    src->signal_state = SIGNAL_STATE_UNKNOWN;
    src->discont_time = GST_CLOCK_TIME_NONE;
    src->discont_frame_number = 0;
    src->first_time = GST_CLOCK_TIME_NONE;
    src->window_fill = 0;
    src->window_filled = FALSE;
    src->window_skip = 1;
    src->window_skip_count = 0;
    src->current_time_mapping.xbase = 0;
    src->current_time_mapping.b = 0;
    src->current_time_mapping.num = 1;
    src->current_time_mapping.den = 1;
    src->next_time_mapping.xbase = 0;
    src->next_time_mapping.b = 0;
    src->next_time_mapping.num = 1;
    src->next_time_mapping.den = 1;
    g_mutex_unlock (&src->lock);

    if (src->input->ntv2AV) {
      src->input->started = TRUE;
      src->input->ntv2AV->Run ();
    }
  } else {
    GST_DEBUG_OBJECT (src, "Not starting streams yet");
  }
}

static GstStateChangeReturn
gst_aja_video_src_change_state (GstElement * element, GstStateChange transition)
{
  GstAjaVideoSrc *src = GST_AJA_VIDEO_SRC (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_aja_video_src_open (src)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto out;
      }
      break;

    case GST_STATE_CHANGE_READY_TO_PAUSED:
      g_mutex_lock (&src->input->lock);
      src->input->ntv2AV->SetCallback (VIDEO_CALLBACK,
          &gst_aja_video_src_video_callback, src);
      g_mutex_unlock (&src->input->lock);
      src->flushing = FALSE;
      break;

    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    {
      break;
    }

    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_aja_video_src_stop (src);
      break;

    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    {
      //HRESULT res;

      GST_DEBUG_OBJECT (src, "Stopping streams");
      g_mutex_lock (&src->input->lock);
      src->input->started = FALSE;
      g_mutex_unlock (&src->input->lock);

      break;
    }

    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    {
      g_mutex_lock (&src->input->lock);
      if (src->input->start_streams)
        src->input->start_streams (src->input->videosrc);
      g_mutex_unlock (&src->input->lock);

      break;
    }

    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_aja_video_src_close (src);
      break;

    default:
      break;
  }
out:

  return ret;
}

static void
gst_aja_video_src_update_time_mapping (GstAjaVideoSrc * src,
    GstClockTime capture_time, GstClockTime stream_time, gboolean discont)
{
  if (src->window_skip_count == 0) {
    GstClockTime num, den, b, xbase;
    gdouble r_squared;

    src->times[2 * src->window_fill] = stream_time;
    src->times[2 * src->window_fill + 1] = capture_time;

    src->window_fill++;
    src->window_skip_count++;
    if (src->window_skip_count >= src->window_skip)
      src->window_skip_count = 0;

    if (src->window_fill >= src->window_size) {
      guint fps =
          ((gdouble) src->info.fps_n + src->info.fps_d -
          1) / ((gdouble) src->info.fps_d);

      /* Start by updating first every frame, once full every second frame,
       * etc. until we update once every 4 seconds */
      if (src->window_skip < 4 * fps)
        src->window_skip *= 2;
      if (src->window_skip >= 4 * fps)
        src->window_skip = 4 * fps;

      src->window_fill = 0;
      src->window_filled = TRUE;
    }

    /* First sample ever, create some basic mapping to start */
    if (!src->window_filled && src->window_fill == 1) {
      src->current_time_mapping.xbase = stream_time;
      src->current_time_mapping.b = capture_time;
      src->current_time_mapping.num = 1;
      src->current_time_mapping.den = 1;
      src->next_time_mapping_pending = FALSE;
    }

    /* Only bother calculating anything here once we had enough measurements,
     * i.e. let's take the window size as a start */
    if (src->window_filled &&
        gst_calculate_linear_regression (src->times, src->times_temp,
            src->window_size, &num, &den, &b, &xbase, &r_squared)) {

      GST_DEBUG_OBJECT (src,
          "Calculated new time mapping: pipeline time = %lf * (stream time - %"
          G_GUINT64_FORMAT ") + %" G_GUINT64_FORMAT " (%lf)",
          ((gdouble) num) / ((gdouble) den), xbase, b, r_squared);

      src->next_time_mapping.xbase = xbase;
      src->next_time_mapping.b = b;
      src->next_time_mapping.num = num;
      src->next_time_mapping.den = den;
      src->next_time_mapping_pending = TRUE;
    }
  } else {
    src->window_skip_count++;
    if (src->window_skip_count >= src->window_skip)
      src->window_skip_count = 0;
  }

  if (src->next_time_mapping_pending) {
    GstClockTime expected, new_calculated, diff, max_diff;

    expected =
        gst_clock_adjust_with_calibration (NULL, stream_time,
        src->current_time_mapping.xbase, src->current_time_mapping.b,
        src->current_time_mapping.num, src->current_time_mapping.den);
    new_calculated =
        gst_clock_adjust_with_calibration (NULL, stream_time,
        src->next_time_mapping.xbase, src->next_time_mapping.b,
        src->next_time_mapping.num, src->next_time_mapping.den);

    if (new_calculated > expected)
      diff = new_calculated - expected;
    else
      diff = expected - new_calculated;

    /* At most 5% frame duration change per update */
    max_diff =
        gst_util_uint64_scale (GST_SECOND / 20, src->info.fps_d,
        src->info.fps_n);

    GST_DEBUG_OBJECT (src,
        "New time mapping causes difference of %" GST_TIME_FORMAT "(discont: %d)",
        GST_TIME_ARGS (diff), discont);
    GST_DEBUG_OBJECT (src, "Maximum allowed per frame %" GST_TIME_FORMAT,
        GST_TIME_ARGS (max_diff));

    if (!discont && diff > max_diff && diff < 50 * GST_MSECOND) {
      /* adjust so that we move that much closer */
      if (new_calculated > expected) {
        src->current_time_mapping.b = expected + max_diff;
        src->current_time_mapping.xbase = stream_time;
      } else {
        src->current_time_mapping.b = expected - max_diff;
        src->current_time_mapping.xbase = stream_time;
      }
    } else {
      src->current_time_mapping.xbase = src->next_time_mapping.xbase;
      src->current_time_mapping.b = src->next_time_mapping.b;
      src->current_time_mapping.num = src->next_time_mapping.num;
      src->current_time_mapping.den = src->next_time_mapping.den;
      src->next_time_mapping_pending = FALSE;
    }
  }
}

static void
gst_aja_video_src_got_frame (GstAjaVideoSrc * src, AjaVideoBuff * videoBuff)
{
  GstClock *clock;
  GstClockTime capture_sys;
  GstClockTime now_sys, now_pipeline, capture_pipeline;
  GstClockTime capture_delay = 0, base_time;
  GstClockTime stream_time, timestamp;
  gboolean had_signal = TRUE;

  clock = gst_element_get_clock (GST_ELEMENT_CAST (src));
  base_time = gst_element_get_base_time (GST_ELEMENT_CAST (src));

  // AJA seems to use the real time clock, not the monotonic clock
  now_sys = g_get_real_time ();
  now_pipeline = gst_clock_get_time (clock);
  capture_sys = videoBuff ? videoBuff->timeStamp / 10 : now_sys;
  // We can actually calculate how far in the past the frame was captured
  if (now_sys >= capture_sys && now_sys - capture_sys < 1000000 /* 1s */ ) {
    capture_delay = now_sys - capture_sys;
  }
  gst_object_unref (clock);

  if (now_pipeline > capture_delay)
    capture_pipeline = now_pipeline - capture_delay;
  else
    capture_pipeline = 0;

  if (capture_pipeline > base_time)
    capture_pipeline -= base_time;
  else
    capture_pipeline = 0;

  // Check if we have no signal
  if (videoBuff && !videoBuff->haveSignal) {
    src->input->ntv2AV->ReleaseVideoBuffer (videoBuff);
    videoBuff = NULL;
  }

  // Check if we switched from having no signal to having signal,
  // or the other way around
  if (!videoBuff) {
    if (src->signal_state != SIGNAL_STATE_LOST) {
      src->signal_state = SIGNAL_STATE_LOST;

      // Signal to the streaming thread that we lost signal so it can handle
      // this after all remaining queued up frames are handled
      if (!src->flushing) {
        AjaCaptureVideoFrame f;

        memset(&f, 0, sizeof (f));
        f.signal_change = LOST_SIGNAL;

        gst_queue_array_push_tail_struct (src->current_frames, &f);
        g_cond_signal (&src->cond);
      }
    }

    return;
  } else if (src->signal_state != SIGNAL_STATE_AVAILABLE || src->discont_time == GST_CLOCK_TIME_NONE) {
    GstAjaSignalState previous_signal_state = src->signal_state;
    if (src->signal_state != SIGNAL_STATE_AVAILABLE) {
      src->signal_state = SIGNAL_STATE_AVAILABLE;

      // Signal to the streaming thread that we got signal again so it can handle
      // this after all remaining queued up frames are handled
      if (!src->flushing) {
        AjaCaptureVideoFrame f;

        memset(&f, 0, sizeof (f));
        f.signal_change = previous_signal_state == SIGNAL_STATE_LOST ? RECOVERED_SIGNAL : GOT_SIGNAL;

        gst_queue_array_push_tail_struct (src->current_frames, &f);
        g_cond_signal (&src->cond);
      }
      had_signal = FALSE;
    }

    src->discont_time = capture_pipeline;
    src->discont_frame_number = videoBuff->frameNumber;
  }

  stream_time = src->discont_time +
      gst_util_uint64_scale (videoBuff->frameNumber - src->discont_frame_number,
      src->input->mode->fps_d * GST_SECOND, src->input->mode->fps_n);

  //GST_ERROR_OBJECT (src, "Got video frame at %" GST_TIME_FORMAT, GST_TIME_ARGS (capture_time));
  //GST_ERROR_OBJECT (src, "Got video duration %" GST_TIME_FORMAT, GST_TIME_ARGS (capture_duration));

  if (src->first_time == GST_CLOCK_TIME_NONE)
    src->first_time = stream_time;

  if (src->skip_first_time > 0
      && stream_time - src->first_time < src->skip_first_time) {
    GST_DEBUG_OBJECT (src,
        "Skipping frame as requested: %" GST_TIME_FORMAT " < %" GST_TIME_FORMAT,
        GST_TIME_ARGS (stream_time),
        GST_TIME_ARGS (src->skip_first_time + src->first_time));
    src->input->ntv2AV->ReleaseVideoBuffer (videoBuff);
    return;
  }

  gst_aja_video_src_update_time_mapping (src, capture_pipeline, stream_time, videoBuff->droppedChanged);

  if (src->output_stream_time) {
    timestamp = stream_time;
  } else {
    timestamp =
        gst_clock_adjust_with_calibration (NULL, stream_time,
        src->current_time_mapping.xbase, src->current_time_mapping.b,
        src->current_time_mapping.num, src->current_time_mapping.den);
  }

  //GST_ERROR_OBJECT (src, "Actual timestamp %" GST_TIME_FORMAT, GST_TIME_ARGS (capture_time));

  g_mutex_lock (&src->lock);
  if (!src->flushing) {
    SignalChange signal_change = NO_CHANGE;
    guint skipped_frames = 0;
    gboolean skipped_before = FALSE;

    while (gst_queue_array_get_length (src->current_frames) >= src->queue_size) {
      AjaCaptureVideoFrame *f = (AjaCaptureVideoFrame *) gst_queue_array_pop_head_struct (src->current_frames);

      // We need to remember if we got signal back here at some point
      if ((f->signal_change == GOT_SIGNAL && signal_change != RECOVERED_SIGNAL)
          || f->signal_change == RECOVERED_SIGNAL)
        signal_change = f->signal_change;
      if (f->video_buff) {
        if (skipped_frames == 0 && src->skipped_last == 0)
          src->skip_from_timestamp = f->capture_time;
        skipped_frames++;
        src->skip_to_timestamp = f->capture_time;
      }
      aja_capture_video_frame_clear (f);
    }

    if (src->skipped_last == 0 && skipped_frames > 0) {
      GST_WARNING_OBJECT (src, "Starting to drop frames");
    }

    src->skipped_last += skipped_frames;

    if (skipped_frames == 0 && src->skipped_last > 0) {
      GST_ELEMENT_WARNING_WITH_DETAILS (src,
          STREAM, FAILED,
          ("Dropped %u old frames from %" GST_TIME_FORMAT " to %"
          GST_TIME_FORMAT, src->skipped_last,
          GST_TIME_ARGS (src->skip_from_timestamp),
          GST_TIME_ARGS (src->skip_to_timestamp)),
          (NULL),
          ("dropped", G_TYPE_UINT, src->skipped_last,
           "from", G_TYPE_UINT64, src->skip_from_timestamp,
           "to", G_TYPE_UINT64, src->skip_to_timestamp, NULL));
      src->skipped_overall += src->skipped_last;
      src->skipped_last = 0;
      skipped_before = TRUE;
    }

    // Write the GOT_SIGNAL change into the oldest frame again
    if (signal_change != NO_CHANGE) {
      AjaCaptureVideoFrame *f = (AjaCaptureVideoFrame *) gst_queue_array_peek_head_struct (src->current_frames);
      if (f)
        f->signal_change = signal_change;
    }

    AjaCaptureVideoFrame f;
    memset(&f, 0, sizeof (f));
    f.video_src = src;
    f.video_buff = videoBuff;
    f.capture_time = timestamp;
    f.stream_time = stream_time;
    f.mode = src->modeEnum;
    f.signal_change = NO_CHANGE;
    f.first_buffer = !had_signal;

    if (skipped_before)
      videoBuff->droppedChanged = true;

    gst_queue_array_push_tail_struct (src->current_frames, &f);
    g_cond_signal (&src->cond);
    g_mutex_unlock (&src->lock);
  } else {
    g_mutex_unlock (&src->lock);
    src->input->ntv2AV->ReleaseVideoBuffer (videoBuff);
  }
}

#if GST_CHECK_VERSION(1, 15, 0)
static void
extract_cc_from_vbi (GstAjaVideoSrc * src, GstBuffer ** buffer,
    guint8 * ancillary_data)
{
  gint i;
  gboolean found = FALSE;
  gsize linewidth;
  GstVideoVBIParser *parser = NULL;
  GstClockTime before, after;

  before = gst_util_get_timestamp ();

  switch (src->input->mode->height) {
    case 720:
      if (src->input->mode->bitDepth == 8) {
        linewidth = 1280 * 2;
        parser = gst_video_vbi_parser_new (GST_VIDEO_FORMAT_UYVY, 1280);
      } else {
        linewidth = 1296 * 16 / 6;
        parser = gst_video_vbi_parser_new (GST_VIDEO_FORMAT_v210, 1280);
      }
      break;
    case 1080:
      if (src->input->mode->bitDepth == 8) {
        linewidth = 1920 * 2;
        parser = gst_video_vbi_parser_new (GST_VIDEO_FORMAT_UYVY, 1920);
      } else {
        linewidth = 1920 * 16 / 6;
        parser = gst_video_vbi_parser_new (GST_VIDEO_FORMAT_v210, 1920);
      }
      break;
    default:
      GST_ERROR ("Unsupported format for ancillary data !");
      linewidth = 255;
      break;
  }

  if (!parser)
    return;

  i = src->last_cc_vbi_line;
  if (i == -1)
    i = 0;
  ancillary_data += i * linewidth;

  while (i < 15 && !found) {
    GstVideoAncillary gstanc;
    GST_DEBUG ("Analyzing data on line %d", i);
    GST_MEMDUMP ("line data", ancillary_data, 255);

    gst_video_vbi_parser_add_line (parser, ancillary_data);
    while (gst_video_vbi_parser_get_ancillary (parser,
            &gstanc) == GST_VIDEO_VBI_PARSER_RESULT_OK) {
      if (GST_VIDEO_ANCILLARY_DID16 (&gstanc) ==
          GST_VIDEO_ANCILLARY_DID16_S334_EIA_708) {
        GST_DEBUG_OBJECT (src,
            "Adding CEA-708 CDP meta to buffer from line %d", i);
        GST_MEMDUMP_OBJECT (src, "CDP", gstanc.data, gstanc.data_count);
        gst_buffer_add_video_caption_meta (*buffer,
            GST_VIDEO_CAPTION_TYPE_CEA708_CDP, gstanc.data, gstanc.data_count);
        found = TRUE;
        src->last_cc_vbi_line = i;
        break;
      }
    }

    ancillary_data += linewidth;
    i++;
  }

  // If we didn't find any CC, restart from the first VBI line next time
  if (!found) {
    GST_DEBUG_OBJECT (src, "Didn't find any CC in this frame");
    src->last_cc_vbi_line = -1;
  }

  after = gst_util_get_timestamp ();
  GST_LOG ("Getting CC took %" GST_TIME_FORMAT,
      GST_TIME_ARGS (after - before));
  gst_video_vbi_parser_free (parser);
}
#endif

/* ask the subclass to create a buffer with offset and size, the default
 * implementation will call alloc and fill. */
static GstFlowReturn
gst_aja_video_src_create (GstPushSrc * bsrc, GstBuffer ** buffer)
{
  static GstStaticCaps stream_reference =
      GST_STATIC_CAPS ("timestamp/x-aja-stream");
  GstAjaVideoSrc *src = GST_AJA_VIDEO_SRC (bsrc);
  //GST_DEBUG_OBJECT (src, "create");

  GstFlowReturn flow_ret = GST_FLOW_OK;

  AjaCaptureVideoFrame f;
  GstCaps *caps;
  GstClockTime capture_time, stream_time;
  gboolean timecode_valid;
  guint32 timecode_high, timecode_low;
  guint8 aja_field_count;
  guint8 *ancillary_data;
  gboolean discont = false;

  if (!gst_aja_video_src_start (src)) {
    return GST_FLOW_NOT_NEGOTIATED;
  }

  g_mutex_lock (&src->lock);
retry:
  while (gst_queue_array_is_empty (src->current_frames) && !src->flushing) {
    g_cond_wait (&src->cond, &src->lock);
  }

  if (src->flushing) {
    g_mutex_unlock (&src->lock);
    GST_DEBUG_OBJECT (src, "Flushing");
    return GST_FLOW_FLUSHING;
  }

  f = *(AjaCaptureVideoFrame *) gst_queue_array_pop_head_struct (src->current_frames);

  // First of all, notify about signal change
  if (f.signal_change == GOT_SIGNAL || f.signal_change == RECOVERED_SIGNAL) {
    g_object_notify (G_OBJECT (src), "signal");
    if (f.signal_change == RECOVERED_SIGNAL)
      GST_ELEMENT_INFO (GST_ELEMENT (src), RESOURCE, READ, ("Signal recovered"),
        ("Input source detected"));
  } else if (f.signal_change == LOST_SIGNAL) {
    g_object_notify (G_OBJECT (src), "signal");
    GST_ELEMENT_WARNING (GST_ELEMENT (src), RESOURCE, READ, ("Signal lost"),
        ("No input source was detected - video frames invalid"));
  }

  // Retry if we got no video buffer
  if (!f.video_buff) {
    aja_capture_video_frame_clear (&f);
    goto retry;
  }

  if (src->modeEnum != f.mode ||
      src->transferCharacteristics != f.video_buff->transferCharacteristics ||
      src->colorimetry != f.video_buff->colorimetry ||
      src->fullRange != f.video_buff->fullRange ||
      !gst_pad_has_current_caps (GST_BASE_SRC_PAD (src))) {
    GST_DEBUG_OBJECT (src, "Mode changed from %d to %d (transfer "
        "characteristics: %d -> %d, colorimetry: %d -> %d, full "
        "range: %d -> %d)", src->modeEnum, f.mode,
        src->transferCharacteristics, f.video_buff->transferCharacteristics,
        src->colorimetry, f.video_buff->colorimetry,
        src->fullRange, f.video_buff->fullRange);

    src->modeEnum = f.mode;
    src->transferCharacteristics = f.video_buff->transferCharacteristics;
    src->colorimetry = f.video_buff->colorimetry;
    src->fullRange = f.video_buff->fullRange;
    g_mutex_unlock (&src->lock);
    caps = gst_aja_mode_get_caps_raw (f.mode);
    gst_video_info_from_caps (&src->info, caps);
    // TODO: Work with videoinfo instead of caps
    gst_caps_unref (caps);
#if GST_CHECK_VERSION(1,17,0)
    if (src->transferCharacteristics == 0) {
      // SDR-TV is the default
    } else if (src->transferCharacteristics == 1) {
      src->info.colorimetry.transfer = GST_VIDEO_TRANSFER_ARIB_STD_B67;
    } else if (src->transferCharacteristics == 2) {
      src->info.colorimetry.transfer = GST_VIDEO_TRANSFER_SMPTE2084;
    }
#endif
    if (src->colorimetry == 0) {
      if (src->info.height < 720) {
        src->info.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT601;
        src->info.colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_SMPTE170M;
      } else if (src->info.height < 2160) {
        src->info.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT709;
        src->info.colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;
      } else {
        src->info.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT2020;
        src->info.colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
      }
    } else if (src->colorimetry == 2) {
      src->info.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT2020;
      src->info.colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
    } else {
      if (src->transferCharacteristics == 1 || src->transferCharacteristics == 2) {
        src->info.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT2020;
        src->info.colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
      }
    }
    src->info.colorimetry.range = src->fullRange ? GST_VIDEO_COLOR_RANGE_0_255 : GST_VIDEO_COLOR_RANGE_16_235;
    caps = gst_video_info_to_caps (&src->info);
    gst_base_src_set_caps (GST_BASE_SRC_CAST (bsrc), caps);
    gst_element_post_message (GST_ELEMENT_CAST (src),
        gst_message_new_latency (GST_OBJECT_CAST (src)));
    gst_caps_unref (caps);
    src->last_cc_vbi_line = -1;
  } else {
    g_mutex_unlock (&src->lock);
  }

  //printf("data_size = %ld\n", data_size);

  *buffer = gst_buffer_ref (f.video_buff->buffer);
  capture_time = f.capture_time;
  stream_time = f.stream_time;
  timecode_valid = f.video_buff->timeCodeValid;
  aja_field_count = f.video_buff->fieldCount;
  timecode_high = f.video_buff->timeCodeHigh;
  timecode_low = f.video_buff->timeCodeLow;
  ancillary_data = (guint8 *) f.video_buff->pAncillaryData;
  discont = f.first_buffer || f.video_buff->droppedChanged;

  if (f.video_buff->droppedChanged) {
    GstMessage *msg;
    GstClockTime running_time;

    running_time = gst_segment_to_running_time (&GST_BASE_SRC (src)->segment,
        GST_FORMAT_TIME, capture_time);

    msg = gst_message_new_qos (GST_OBJECT (src), TRUE, running_time, f.stream_time,
        f.capture_time, gst_util_uint64_scale_int (GST_SECOND,
      src->input->mode->fps_d, src->input->mode->fps_n));
    gst_message_set_qos_stats (msg, GST_FORMAT_BUFFERS,
                               f.video_buff->framesProcessed - src->skipped_overall,
                               f.video_buff->framesDropped + src->skipped_overall);
    gst_element_post_message (GST_ELEMENT (src), msg);
  }

  aja_capture_video_frame_clear (&f);

  GST_BUFFER_TIMESTAMP (*buffer) = capture_time;
  GST_BUFFER_DURATION (*buffer) = gst_util_uint64_scale_int (GST_SECOND,
      src->input->mode->fps_d, src->input->mode->fps_n);

  if (src->input->mode->isInterlaced) {
    GST_BUFFER_FLAG_SET (*buffer, GST_VIDEO_BUFFER_FLAG_INTERLACED);
    if (src->input->mode->isTff)
      GST_BUFFER_FLAG_SET (*buffer, GST_VIDEO_BUFFER_FLAG_TFF);
  }

  if (discont)
    GST_BUFFER_FLAG_SET (*buffer, GST_BUFFER_FLAG_DISCONT);

  if (timecode_valid) {
    uint8_t hours, minutes, seconds, frames;
    GstVideoTimeCodeFlags flags = GST_VIDEO_TIME_CODE_FLAGS_NONE;
    guint field_count = 0;
    GstVideoTimeCode tc;

    if (src->input->mode->isInterlaced) {
      flags =
          (GstVideoTimeCodeFlags) (flags |
          GST_VIDEO_TIME_CODE_FLAGS_INTERLACED);
      field_count = aja_field_count == 0 ? 2 : aja_field_count;
    }
    // Any better way to detect this?
    if (src->input->mode->fps_d == 1001) {
      if (src->input->mode->fps_n == 30000 || src->input->mode->fps_n == 60000)
        flags =
            (GstVideoTimeCodeFlags) (flags |
            GST_VIDEO_TIME_CODE_FLAGS_DROP_FRAME);
      else
        flags =
                (GstVideoTimeCodeFlags) (flags &
                ~GST_VIDEO_TIME_CODE_FLAGS_DROP_FRAME);
    }

    hours = (((timecode_high & RP188_HOURTENS_MASK) >> 24) * 10) +
        ((timecode_high & RP188_HOURUNITS_MASK) >> 16);
    minutes = (((timecode_high & RP188_MINUTESTENS_MASK) >> 8) * 10) +
        (timecode_high & RP188_MINUTESUNITS_MASK);
    seconds = (((timecode_low & RP188_SECONDTENS_MASK) >> 24) * 10) +
        ((timecode_low & RP188_SECONDUNITS_MASK) >> 16);
    frames = (((timecode_low & RP188_FRAMETENS_MASK) >> 8) * 10) +
        (timecode_low & RP188_FRAMEUNITS_MASK);

    gst_video_time_code_init (&tc, src->input->mode->fps_n,
        src->input->mode->fps_d, NULL, flags, hours, minutes, seconds, frames,
        field_count);
    if (gst_video_time_code_is_valid (&tc)) {
      GST_DEBUG_OBJECT (src, "Adding timecode %02u:%02u:%02u.%02u", hours, minutes, seconds, frames);
      gst_buffer_add_video_time_code_meta (*buffer, &tc);
    }
    gst_video_time_code_clear (&tc);
  }
#if GST_CHECK_VERSION (1, 13, 0)
  gst_buffer_add_reference_timestamp_meta (*buffer,
      gst_static_caps_get (&stream_reference), stream_time,
      GST_CLOCK_TIME_NONE);
#endif

#if GST_CHECK_VERSION(1, 15, 0)
  if (ancillary_data && src->output_cc)
    extract_cc_from_vbi (src, buffer, ancillary_data);
#endif

#if 1
  GST_DEBUG_OBJECT (src,
      "Outputting buffer %p with timestamp %" GST_TIME_FORMAT " and duration %"
      GST_TIME_FORMAT, *buffer, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (*buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (*buffer)));
#endif

  return flow_ret;
}

static bool
gst_aja_video_src_video_callback (void *refcon, void *msg)
{
  GstAjaVideoSrc *src = (GstAjaVideoSrc *) refcon;
  AjaVideoBuff *videoBuffer = (AjaVideoBuff *) msg;

  if (src->input->video_enabled == FALSE)
    return false;

  gst_aja_video_src_got_frame (src, videoBuffer);
  return true;
}
