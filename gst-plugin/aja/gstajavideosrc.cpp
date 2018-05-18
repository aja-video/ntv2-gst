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

#include "gstajavideosrc.h"
#include "gstajavideosrc.h"


GST_DEBUG_CATEGORY_STATIC (gst_aja_video_src_debug);
#define GST_CAT_DEFAULT gst_aja_video_src_debug

#define DEFAULT_MODE            (GST_AJA_MODE_RAW_720_8_5994p)
#define DEFAULT_DEVICE_NUMBER   (0)
#define DEFAULT_INPUT_CHANNEL   (0)
#define DEFAULT_QUEUE_SIZE      (5)

enum
{
    PROP_0,
    PROP_MODE,
    PROP_DEVICE_NUMBER,
    PROP_INPUT_CHANNEL,
    PROP_QUEUE_SIZE
};

typedef struct
{
    GstAjaVideoSrc              *video_src;
    AjaVideoBuff                *video_buff;
    GstClockTime                capture_time;
    GstClockTime                capture_duration;
    GstAjaModeRawEnum           mode;
} AjaCaptureVideoFrame;

static void
aja_capture_video_frame_free (void *data)
{
    AjaCaptureVideoFrame *frame = (AjaCaptureVideoFrame *) data;

    if ((frame->video_src->input) && (frame->video_src->input->ntv2AVHevc))
        frame->video_src->input->ntv2AVHevc->ReleaseVideoBuffer(frame->video_buff);
    g_free (frame);
}

typedef struct
{
    GstAjaVideoSrc              *video_src;
    AjaVideoBuff                *video_buff;
} AjaVideoFrame;

static void
aja_video_frame_free (void *data)
{
    AjaVideoFrame *frame = (AjaVideoFrame *) data;

    if ((frame->video_src->input) && (frame->video_src->input->ntv2AVHevc))
        frame->video_src->input->ntv2AVHevc->ReleaseVideoBuffer(frame->video_buff);
    g_free (frame);
}

static void gst_aja_video_src_set_property (GObject * object, guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_aja_video_src_get_property (GObject * object, guint property_id, GValue * value, GParamSpec * pspec);

static void gst_aja_video_src_finalize (GObject * object);

static gboolean gst_aja_video_src_set_caps (GstBaseSrc * src, GstCaps * caps);
static GstCaps *gst_aja_video_src_get_caps (GstBaseSrc * src, GstCaps * filter);
static gboolean gst_aja_video_src_query (GstBaseSrc * src, GstQuery * query);
static gboolean gst_aja_video_src_unlock (GstBaseSrc * src);
static gboolean gst_aja_video_src_unlock_stop (GstBaseSrc * src);

static GstFlowReturn gst_aja_video_src_create (GstPushSrc * psrc, GstBuffer ** buffer);

static gboolean gst_aja_video_src_open (GstAjaVideoSrc * video_src);
static gboolean gst_aja_video_src_close (GstAjaVideoSrc * video_src);

static gboolean gst_aja_video_src_stop (GstAjaVideoSrc * src);

static void gst_aja_video_src_start_streams (GstElement * element);

static GstStateChangeReturn gst_aja_video_src_change_state (GstElement * element, GstStateChange transition);
static GstClock *gst_aja_video_src_provide_clock (GstElement * element);

static gboolean gst_aja_video_src_video_callback (int64_t refcon, int64_t msg);

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

    element_class->change_state = GST_DEBUG_FUNCPTR (gst_aja_video_src_change_state);
    element_class->provide_clock = GST_DEBUG_FUNCPTR (gst_aja_video_src_provide_clock);

    basesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_aja_video_src_get_caps);
    basesrc_class->set_caps = GST_DEBUG_FUNCPTR (gst_aja_video_src_set_caps);
    basesrc_class->query = GST_DEBUG_FUNCPTR (gst_aja_video_src_query);
    basesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_aja_video_src_unlock);
    basesrc_class->unlock_stop = GST_DEBUG_FUNCPTR (gst_aja_video_src_unlock_stop);
    
    pushsrc_class->create = GST_DEBUG_FUNCPTR (gst_aja_video_src_create);

   g_object_class_install_property (gobject_class, PROP_MODE,
                                     g_param_spec_enum ("mode", "Playback Mode",
                                                        "Video Mode to use for playback",
                                                        GST_TYPE_AJA_MODE_RAW, DEFAULT_MODE,
                                                        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT)));
    
    g_object_class_install_property (gobject_class, PROP_DEVICE_NUMBER,
                                     g_param_spec_uint ("device-number",
                                                        "Device number",
                                                        "Input device instance to use",
                                                        0, G_MAXINT, DEFAULT_DEVICE_NUMBER,
                                                        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT)));

    g_object_class_install_property (gobject_class, PROP_INPUT_CHANNEL,
                                     g_param_spec_uint ("input-channel",
                                                        "Input channel",
                                                        "Input channel to use",
                                                        0, G_MAXINT, DEFAULT_INPUT_CHANNEL,
                                                        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT)));
    
    g_object_class_install_property (gobject_class, PROP_QUEUE_SIZE,
                                     g_param_spec_uint ("queue-size",
                                                        "Queue Size",
                                                        "Size of internal queue in number of video frames",
                                                        1, G_MAXINT, DEFAULT_QUEUE_SIZE,
                                                        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    templ_caps = gst_aja_mode_get_template_caps_raw ();
    gst_element_class_add_pad_template (element_class, gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, templ_caps));
    gst_caps_unref (templ_caps);
    
    gst_element_class_set_static_metadata (element_class, "Aja Raw Source", "Video/Src", "Aja Raw RT Video Source", "PSM <philm@aja.com>");

    GST_DEBUG_CATEGORY_INIT (gst_aja_video_src_debug, "ajavideosrc", 0, "debug category for ajavideosrc element");
}

static void
gst_aja_video_src_init (GstAjaVideoSrc *src)
{
    GST_DEBUG_OBJECT (src, "init");

    src->modeEnum = DEFAULT_MODE;
    src->input_channel = DEFAULT_INPUT_CHANNEL;
    src->device_number = DEFAULT_DEVICE_NUMBER;
    src->queue_size = DEFAULT_QUEUE_SIZE;
    
    gst_base_src_set_live (GST_BASE_SRC (src), TRUE);
    gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);
    
    g_mutex_init (&src->lock);
    g_cond_init (&src->cond);
    
    g_queue_init (&src->current_frames);
}

void
gst_aja_video_src_set_property (GObject * object, guint property_id, const GValue * value, GParamSpec * pspec)
{
    GstAjaVideoSrc *src = GST_AJA_VIDEO_SRC (object);
    GST_DEBUG_OBJECT (src, "set_property");

    switch (property_id)
    {
        case PROP_MODE:
            src->modeEnum = (GstAjaModeRawEnum) g_value_get_enum (value);
            break;
            
        case PROP_DEVICE_NUMBER:
            src->device_number = g_value_get_uint (value);
            break;
            
        case PROP_INPUT_CHANNEL:
            src->input_channel = g_value_get_uint (value);
            break;
            
        case PROP_QUEUE_SIZE:
            src->queue_size = g_value_get_uint (value);
            break;
            
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

void
gst_aja_video_src_get_property (GObject * object, guint property_id, GValue * value, GParamSpec * pspec)
{
    GstAjaVideoSrc *src = GST_AJA_VIDEO_SRC (object);
    GST_DEBUG_OBJECT (src, "get_property");

    switch (property_id)
    {
        case PROP_MODE:
            g_value_set_enum (value, src->modeEnum);
            break;
            
        case PROP_DEVICE_NUMBER:
            g_value_set_uint (value, src->device_number);
            break;
            
        case PROP_INPUT_CHANNEL:
            g_value_set_uint (value, src->input_channel);
            break;

        case PROP_QUEUE_SIZE:
            g_value_set_uint (value, src->queue_size);
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

    g_mutex_clear (&src->lock);
    g_cond_clear (&src->cond);
    
    // Call parent class
    G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* notify the subclass of new caps */
static gboolean
gst_aja_video_src_set_caps (GstBaseSrc * bsrc, GstCaps * caps)
{
    GstAjaVideoSrc *src = GST_AJA_VIDEO_SRC (bsrc);
    GST_DEBUG_OBJECT (src, "set_caps");

    GstCaps                 *current_caps;
    const GstAjaMode        *mode;
    
    if ((current_caps = gst_pad_get_current_caps (GST_BASE_SRC_PAD (bsrc))))
    {
        GST_DEBUG_OBJECT (src, "Pad already has caps %" GST_PTR_FORMAT, caps);
        
        if (!gst_caps_is_equal (caps, current_caps))
        {
            GST_DEBUG_OBJECT (src, "New caps, reconfiguring");
            gst_caps_unref (current_caps);
            return FALSE;
        }
        else
        {
            gst_caps_unref (current_caps);
            return TRUE;
        }
    }
    
    if (!gst_video_info_from_caps (&src->info, caps))
        return FALSE;
            
    mode = gst_aja_get_mode_raw (src->modeEnum);
    g_assert (mode != NULL);
        
    g_mutex_lock (&src->input->lock);
    src->input->mode = mode;
    src->input->video_enabled = TRUE;
    gst_aja_video_src_start_streams(src->input->videosrc);
    g_mutex_unlock (&src->input->lock);
    
    return TRUE;
}

static GstCaps *
gst_aja_video_src_get_caps (GstBaseSrc * bsrc, GstCaps * filter)
{
    GstAjaVideoSrc *src = GST_AJA_VIDEO_SRC (bsrc);
    GST_DEBUG_OBJECT (src, "get_caps");

    GstCaps     *mode_caps, *caps;
    
    g_mutex_lock (&src->lock);
    mode_caps = gst_aja_mode_get_caps_raw (src->modeEnum);
    g_mutex_unlock (&src->lock);
    
    if (filter)
    {
        caps = gst_caps_intersect_full (filter, mode_caps, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref (mode_caps);
    }
    else
    {
        caps = mode_caps;
    }
    
    return caps;
}

void
gst_aja_video_src_convert_to_external_clock (GstAjaVideoSrc * src, GstClockTime * timestamp, GstClockTime * duration)
{
    GstClock *clock;
    
    g_assert (timestamp != NULL);
    
    if (*timestamp == GST_CLOCK_TIME_NONE)
        return;
    
    clock = gst_element_get_clock (GST_ELEMENT_CAST (src));
    if (clock && clock != src->input->clock)
    {
        GstClockTime internal, external, rate_n, rate_d;
        GstClockTimeDiff external_start_time_diff;
        
        gst_clock_get_calibration (src->input->clock, &internal, &external, &rate_n, &rate_d);
        
        if (rate_n != rate_d && src->internal_base_time != GST_CLOCK_TIME_NONE)
        {
            GstClockTime internal_timestamp = *timestamp;
            
            // Convert to the running time corresponding to both clock times
            internal -= src->internal_base_time;
            external -= src->external_base_time;
            
            // Get the difference in the internal time, note
            // that the capture time is internal time.
            // Then scale this difference and offset it to
            // our external time. Now we have the running time
            // according to our external clock.
            //
            // For the duration we just scale
            if (internal > internal_timestamp)
            {
                guint64 diff = internal - internal_timestamp;
                diff = gst_util_uint64_scale (diff, rate_n, rate_d);
                *timestamp = external - diff;
            }
            else
            {
                guint64 diff = internal_timestamp - internal;
                diff = gst_util_uint64_scale (diff, rate_n, rate_d);
                *timestamp = external + diff;
            }
            
            GST_LOG_OBJECT (src,
                            "Converted %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT " (external: %"
                            GST_TIME_FORMAT " internal %" GST_TIME_FORMAT " rate: %lf)",
                            GST_TIME_ARGS (internal_timestamp), GST_TIME_ARGS (*timestamp),
                            GST_TIME_ARGS (external), GST_TIME_ARGS (internal),
                            ((gdouble) rate_n) / ((gdouble) rate_d));
            
            if (duration)
            {
                GstClockTime internal_duration = *duration;
                
                *duration = gst_util_uint64_scale (internal_duration, rate_d, rate_n);
                
                GST_LOG_OBJECT (src,
                                "Converted duration %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT
                                " (external: %" GST_TIME_FORMAT " internal %" GST_TIME_FORMAT
                                " rate: %lf)", GST_TIME_ARGS (internal_duration),
                                GST_TIME_ARGS (*duration), GST_TIME_ARGS (external),
                                GST_TIME_ARGS (internal), ((gdouble) rate_n) / ((gdouble) rate_d));
            }
        }
        else
        {
            GST_LOG_OBJECT (src, "No clock conversion needed, relative rate is 1.0");
        }
        
        // Add the diff between the external time when we
        // went to playing and the external time when the
        // pipeline went to playing. Otherwise we will
        // always start outputting from 0 instead of the
        // current running time.
        external_start_time_diff = gst_element_get_base_time (GST_ELEMENT_CAST (src));
        external_start_time_diff = src->external_base_time - external_start_time_diff;
        *timestamp += external_start_time_diff;
    }
    else
    {
        GST_LOG_OBJECT (src, "No clock conversion needed, same clocks");
    }
}

static gboolean
gst_aja_video_src_query (GstBaseSrc * bsrc, GstQuery * query)
{
    GstAjaVideoSrc *src = GST_AJA_VIDEO_SRC (bsrc);
    GST_DEBUG_OBJECT (src, "query");

    gboolean ret = TRUE;

    switch (GST_QUERY_TYPE (query))
    {
        case GST_QUERY_LATENCY:
        {
            if (src->input)
            {
                GstClockTime min, max;
                const GstAjaMode *mode;

                g_mutex_lock (&src->lock);
                mode = gst_aja_get_mode_raw (src->modeEnum);
                g_mutex_unlock (&src->lock);

                min = gst_util_uint64_scale_ceil (GST_SECOND, mode->fps_d, mode->fps_n);
                max = src->queue_size * min;

                gst_query_set_latency (query, TRUE, min, max);
                ret = TRUE;
            }
            else
            {
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
    g_queue_foreach (&src->current_frames, (GFunc) aja_capture_video_frame_free, NULL);
    g_queue_clear (&src->current_frames);
    g_mutex_unlock (&src->lock);
    
    return TRUE;
}

static gboolean
gst_aja_video_src_open (GstAjaVideoSrc * src)
{
    AJAStatus           status;
    const GstAjaMode *  mode;
    GST_DEBUG_OBJECT (src, "open");
    
    src->input = gst_aja_acquire_input (src->device_number, src->input_channel, GST_ELEMENT_CAST (src), FALSE, FALSE);
    if (!src->input)
    {
        GST_ERROR_OBJECT (src, "Failed to acquire input");
        return FALSE;
    }

    mode = gst_aja_get_mode_raw (src->modeEnum);
    g_assert (mode != NULL);

    g_mutex_lock (&src->input->lock);
    src->input->mode = mode;
    src->input->clock_start_time = GST_CLOCK_TIME_NONE;
    src->input->clock_epoch += src->input->clock_last_time;
    src->input->clock_last_time = 0;
    src->input->clock_offset = 0;

    g_mutex_unlock (&src->input->lock);

    status = src->input->ntv2AVHevc->Open ();
    if (!AJA_SUCCESS (status))
    {
        GST_ERROR_OBJECT (src, "Failed to open input");
        return FALSE;
    }

    status = src->input->ntv2AVHevc->Init (src->input->mode->videoPreset,
                                           src->input->mode->videoFormat,
                                           src->input->mode->bitDepth,
                                           src->input->mode->is422,
                                           false,
                                           false,
                                           src->input->mode->isQuad,
                                           false, false);
    if (!AJA_SUCCESS (status))
    {
        GST_ERROR_OBJECT (src, "Failed to initialize input");
        return FALSE;
    }

    src->input->ntv2AVHevc->SetCallback(VIDEO_CALLBACK, (int64_t)&gst_aja_video_src_video_callback, (int64_t)src);

    return TRUE;
}

static gboolean
gst_aja_video_src_close (GstAjaVideoSrc * src)
{
    GST_DEBUG_OBJECT (src, "close");
    
    if (src->input)
    {
        g_mutex_lock (&src->input->lock);

        if (src->input->ntv2AVHevc)
        {
            src->input->ntv2AVHevc->Quit ();
            delete src->input->ntv2AVHevc;
            src->input->ntv2AVHevc = NULL;
            GST_DEBUG_OBJECT (src, "shut down ntv2HEVC");
        }

        src->input->mode = NULL;
        src->input->video_enabled = FALSE;
        src->input->videosrc = NULL;

        g_mutex_unlock (&src->input->lock);
    }

    return TRUE;
}

static gboolean
gst_aja_video_src_stop (GstAjaVideoSrc * src)
{
    GST_DEBUG_OBJECT (src, "stop");
    
    g_queue_foreach (&src->current_frames, (GFunc) aja_capture_video_frame_free, NULL);
    g_queue_clear (&src->current_frames);
    
    if (src->input && src->input->video_enabled)
    {
        g_mutex_lock (&src->input->lock);
        src->input->ntv2AVHevc->Quit ();
        src->input->video_enabled = FALSE;
        g_mutex_unlock (&src->input->lock);
    }

    return TRUE;
}

static void
gst_aja_video_src_start_streams (GstElement * element)
{
    GstAjaVideoSrc *src = GST_AJA_VIDEO_SRC (element);
    GST_DEBUG_OBJECT (src, "start_streams");
    
    if (src->input->video_enabled && (!src->input->audiosrc || src->input->audio_enabled)
        && (GST_STATE (src) == GST_STATE_PLAYING
        || GST_STATE_PENDING (src) == GST_STATE_PLAYING))
    {
            GST_DEBUG_OBJECT (src, "Starting streams");

            if (src->input->ntv2AVHevc)
            {
                src->input->started = TRUE;
                src->input->clock_restart = TRUE;
                src->input->ntv2AVHevc->Run ();

                // Need to unlock to get the clock time
                g_mutex_unlock (&src->input->lock);

                // Current times of internal and external clock when we go to
                // playing. We need this to convert the pipeline running time
                // to the running time of the hardware
                //
                // We can't use the normal base time for the external clock
                // because we might go to PLAYING later than the pipeline
                src->internal_base_time = gst_clock_get_internal_time (src->input->clock);
                src->external_base_time = gst_clock_get_internal_time (GST_ELEMENT_CLOCK (src));

                g_mutex_lock (&src->input->lock);
            }
        }
        else
        {
            GST_DEBUG_OBJECT (src, "Not starting streams yet");
        }
}

static GstStateChangeReturn
gst_aja_video_src_change_state (GstElement * element, GstStateChange transition)
{
    GstAjaVideoSrc *src = GST_AJA_VIDEO_SRC (element);
    GstStateChangeReturn ret;
    
    switch (transition)
    {
        case GST_STATE_CHANGE_NULL_TO_READY:
            if (!gst_aja_video_src_open (src))
            {
                ret = GST_STATE_CHANGE_FAILURE;
                goto out;
            }
            break;
            
        case GST_STATE_CHANGE_READY_TO_PAUSED:
            g_mutex_lock (&src->input->lock);
            src->input->clock_start_time = GST_CLOCK_TIME_NONE;
            src->input->clock_epoch += src->input->clock_last_time;
            src->input->clock_last_time = 0;
            src->input->clock_offset = 0;
            g_mutex_unlock (&src->input->lock);
            gst_element_post_message (element, gst_message_new_clock_provide (GST_OBJECT_CAST (element), src->input->clock, TRUE));
            src->flushing = FALSE;
            break;
            
        case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
        {
            GstClock *clock;
            
            clock = gst_element_get_clock (GST_ELEMENT_CAST (src));
            if (clock && clock != src->input->clock)
            {
                gst_clock_set_master (src->input->clock, clock);
            }
            if (clock)
                gst_object_unref (clock);
            
            break;
        }
            
        default:
            break;
    }
    
    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
    if (ret == GST_STATE_CHANGE_FAILURE)
        return ret;
    
    switch (transition)
    {
        case GST_STATE_CHANGE_PAUSED_TO_READY:
            gst_element_post_message (element, gst_message_new_clock_lost (GST_OBJECT_CAST (element), src->input->clock));
            gst_clock_set_master (src->input->clock, NULL);
            // Reset calibration to make the clock reusable next time we use it
            gst_clock_set_calibration (src->input->clock, 0, 0, 1, 1);
            g_mutex_lock (&src->input->lock);
            src->input->clock_start_time = GST_CLOCK_TIME_NONE;
            src->input->clock_epoch += src->input->clock_last_time;
            src->input->clock_last_time = 0;
            src->input->clock_offset = 0;
            g_mutex_unlock (&src->input->lock);
            
            gst_aja_video_src_stop (src);
            break;
            
        case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
        {
            //HRESULT res;
            
            GST_DEBUG_OBJECT (src, "Stopping streams");
            g_mutex_lock (&src->input->lock);
            src->input->started = FALSE;
            g_mutex_unlock (&src->input->lock);
            
            //res = src->input->input->StopStreams ();
            //if (res != S_OK)
            //{
            //    GST_ELEMENT_ERROR (src, STREAM, FAILED, (NULL), ("Failed to stop streams: 0x%08x", res));
            //    ret = GST_STATE_CHANGE_FAILURE;
            //}
            src->internal_base_time = GST_CLOCK_TIME_NONE;
            src->external_base_time = GST_CLOCK_TIME_NONE;
            break;
        }
            
        case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
        {
            g_mutex_lock (&src->input->lock);
            gst_aja_video_src_start_streams(src->input->videosrc);
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
gst_aja_video_src_got_frame (GstAjaVideoSrc * src, AjaVideoBuff * videoBuff, GstAjaModeRawEnum mode, GstClockTime capture_time, GstClockTime capture_duration)
{
    //GST_ERROR_OBJECT (src, "Got video frame at %" GST_TIME_FORMAT, GST_TIME_ARGS (capture_time));
    //GST_ERROR_OBJECT (src, "Got video duration %" GST_TIME_FORMAT, GST_TIME_ARGS (capture_duration));
    
    gst_aja_video_src_convert_to_external_clock (src, &capture_time, &capture_duration);
    //GST_ERROR_OBJECT (src, "Actual timestamp %" GST_TIME_FORMAT, GST_TIME_ARGS (capture_time));
    
    g_mutex_lock (&src->lock);
    if (!src->flushing)
    {
        AjaCaptureVideoFrame *f;
        
        while (g_queue_get_length (&src->current_frames) >= src->queue_size)
        {
            f = (AjaCaptureVideoFrame *) g_queue_pop_head (&src->current_frames);
            GST_WARNING_OBJECT (src, "Dropping old frame at %" GST_TIME_FORMAT,
                                GST_TIME_ARGS (f->capture_time));
            aja_capture_video_frame_free (f);
        }
        
        f = (AjaCaptureVideoFrame *) g_malloc0 (sizeof (AjaCaptureVideoFrame));
        f->video_src = src;
        f->video_buff = videoBuff;
        f->capture_time = capture_time;
        f->capture_duration = capture_duration;
        f->mode = mode;
        
        g_queue_push_tail (&src->current_frames, f);
        g_cond_signal (&src->cond);
    }
    g_mutex_unlock (&src->lock);
}

/* ask the subclass to create a buffer with offset and size, the default
 * implementation will call alloc and fill. */
static GstFlowReturn
gst_aja_video_src_create (GstPushSrc * bsrc, GstBuffer ** buffer)
{
    GstAjaVideoSrc *src = GST_AJA_VIDEO_SRC (bsrc);
    //GST_DEBUG_OBJECT (src, "create");
    
    GstFlowReturn flow_ret = GST_FLOW_OK;
    
    const guint8 *data;
    gsize data_size;
    AjaVideoFrame *vf;
    AjaCaptureVideoFrame *f;
    GstCaps *caps;
    
    g_mutex_lock (&src->lock);
    while (g_queue_is_empty (&src->current_frames) && !src->flushing)
    {
        g_cond_wait (&src->cond, &src->lock);
    }
    
    f = (AjaCaptureVideoFrame *) g_queue_pop_head (&src->current_frames);
    g_mutex_unlock (&src->lock);
    
    if (src->flushing)
    {
        if (f)
            aja_capture_video_frame_free (f);
        GST_DEBUG_OBJECT (src, "Flushing");
        return GST_FLOW_FLUSHING;
    }
    
    g_mutex_lock (&src->lock);
    if (src->modeEnum != f->mode)
    {
        GST_DEBUG_OBJECT (src, "Mode changed from %d to %d", src->modeEnum, f->mode);
        src->modeEnum = f->mode;
        g_mutex_unlock (&src->lock);
        caps = gst_aja_mode_get_caps_raw (f->mode);
        gst_video_info_from_caps (&src->info, caps);
        gst_base_src_set_caps (GST_BASE_SRC_CAST (bsrc), caps);
        gst_element_post_message (GST_ELEMENT_CAST (src), gst_message_new_latency (GST_OBJECT_CAST (src)));
        gst_caps_unref (caps);
    }
    else
    {
        g_mutex_unlock (&src->lock);
    }
    
    data = (const guint8 *)f->video_buff->pVideoBuffer;
    data_size = (gsize)f->video_buff->videoDataSize;

    //printf("data_size = %ld\n", data_size);
    
    vf = (AjaVideoFrame *) g_malloc0 (sizeof (AjaVideoFrame));
    
    *buffer = gst_buffer_new_wrapped_full ((GstMemoryFlags) GST_MEMORY_FLAG_READONLY,
                                           (gpointer) data, data_size, 0, data_size, vf,
                                           (GDestroyNotify) aja_video_frame_free);
    
    vf->video_src = f->video_src;
    vf->video_buff = f->video_buff;
    if (vf->video_src->input)
        vf->video_src->input->ntv2AVHevc->AddRefVideoBuffer(vf->video_buff);
    
    GST_BUFFER_TIMESTAMP (*buffer) = f->capture_time;
    GST_BUFFER_DURATION (*buffer) = f->capture_duration;
    
#if 1
    GST_DEBUG_OBJECT (src,
                      "Outputting buffer %p with timestamp %" GST_TIME_FORMAT " and duration %"
                      GST_TIME_FORMAT, *buffer, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (*buffer)),
                      GST_TIME_ARGS (GST_BUFFER_DURATION (*buffer)));
#endif
    aja_capture_video_frame_free (f);
    return flow_ret;
}

static GstClock *
gst_aja_video_src_provide_clock (GstElement * element)
{
    GstAjaVideoSrc *src = GST_AJA_VIDEO_SRC (element);
    
    if (!src->input->ntv2AVHevc)
        return NULL;
    
    return GST_CLOCK_CAST (gst_object_ref (src->input->clock));
}

static gboolean
gst_aja_video_src_video_callback(int64_t refcon, int64_t msg)
{
    GstAjaVideoSrc *src = (GstAjaVideoSrc *) refcon;
    AjaVideoBuff *videoBuffer = (AjaVideoBuff *) msg;

    // Adjust time stamp based on start time
    if (src->input->clock_start_time != GST_CLOCK_TIME_NONE)
    {
        videoBuffer->timeStamp = videoBuffer->timeStamp-src->input->clock_start_time;
    }

    if (src->input->video_enabled == FALSE)
        return FALSE;
    
    gst_aja_video_src_got_frame(src, videoBuffer, src->modeEnum, videoBuffer->timeStamp, videoBuffer->timeDuration);
    return TRUE;
}

