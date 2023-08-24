/**
    @file        ntv2encode.cpp
    @brief        Implementation of NTV2Encode class.
    @copyright    Copyright (C) 2015 AJA Video Systems, Inc.  All rights reserved.
                  Copyright (C) 2017 Sebastian Dröge <sebastian@centricular.com>
                  Copyright (C) 2021 NVIDIA Corporation.  All rights reserved.
**/

#include <stdio.h>
#include <semaphore.h>
#include <fcntl.h>
#include <string>
#include <sched.h>
#include <unistd.h>
#include <pthread.h>

#include "gstntv2.h"
#include "gstaja.h"
#include "ntv2utils.h"
#include "ntv2devicefeatures.h"
#include "ajabase/system/process.h"
#include "ajabase/system/systemtime.h"

#if ENABLE_NVMM
#include "nvbufsurface.h"
#endif

GST_DEBUG_CATEGORY_STATIC (gst_ntv2_debug);
#define GST_CAT_DEFAULT gst_ntv2_debug

#define NTV2_AUDIOSIZE_MAX        (401 * 1024)

static void
_init_ntv2_debug (void)
{
#ifndef GST_DISABLE_GST_DEBUG
  static gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (gst_ntv2_debug, "ajantv2", 0, "AJA ntv2");
    g_once_init_leave (&_init, 1);
  }
#endif
}

NTV2GstAV::NTV2GstAV (const std::string inDeviceSpecifier,
    const NTV2Channel inChannel)

:



mACInputThread (NULL),
mLock (new AJALock),
mDeviceID (DEVICE_ID_NOTFOUND),
mDeviceSpecifier (inDeviceSpecifier),
mInputChannel (inChannel),
mInputSource (NTV2_INPUTSOURCE_SDI1),
mVideoFormat (NTV2_MAX_NUM_VIDEO_FORMATS),
mMultiStream (false),
mCaps (NULL),
mAudioSystem (NTV2_AUDIOSYSTEM_1),
mNumAudioChannels (0),
mLastFrame (false),
mLastFrameInput (false),
mLastFrameVideoOut (false),
mLastFrameAudioOut (false),
mGlobalQuit (false),
mStarted (false),
mVideoCallback (0),
mVideoCallbackRefcon (0),
mAudioCallback (0),
mAudioCallbackRefcon (0),
mAudioBufferPool (NULL),
mVideoBufferPool (NULL)
{
  _init_ntv2_debug ();
}                               //    constructor


NTV2GstAV::~NTV2GstAV ()
{
  //    Stop my capture and consumer threads, then destroy them...
  Quit ();

  // unsubscribe from input vertical event...
  mDevice.UnsubscribeInputVerticalEvent (mInputChannel);

  FreeHostBuffers ();

  delete mLock;
  mLock = NULL;


}                               // destructor


AJAStatus NTV2GstAV::Open (void)
{
  if (mDeviceID != DEVICE_ID_NOTFOUND)
    return AJA_STATUS_SUCCESS;

  //    Open the device...
  if (!CNTV2DeviceScanner::GetFirstDeviceFromArgument (mDeviceSpecifier,
          mDevice)) {
    GST_ERROR ("ERROR: Device not found");
    return AJA_STATUS_OPEN;
  }

  mDevice.SetEveryFrameServices (NTV2_OEM_TASKS);       //    Since this is an OEM app, use the OEM service level
  mDeviceID = mDevice.GetDeviceID ();   //    Keep the device ID handy, as it's used frequently

  std::string serialNumber;

  if (!mDevice.GetSerialNumberString (serialNumber))
    serialNumber = "(none)";

  GST_DEBUG ("Opened device with ID %d (%s, version %s, serial number %s)",
      mDeviceID, mDevice.GetDisplayName ().c_str (),
      mDevice.GetDeviceVersionString ().c_str (), serialNumber.c_str ());
  GST_DEBUG ("Using SDK version %d.%d.%d.%d (%s) and driver version %s",
      AJA_NTV2_SDK_VERSION_MAJOR, AJA_NTV2_SDK_VERSION_MINOR,
      AJA_NTV2_SDK_VERSION_POINT, AJA_NTV2_SDK_BUILD_NUMBER,
      AJA_NTV2_SDK_BUILD_DATETIME, mDevice.GetDriverVersionString ().c_str ());

  // So we can configure each channel separately
  mDevice.SetMultiFormatMode(true);

  return AJA_STATUS_SUCCESS;
}


AJAStatus NTV2GstAV::Close (void)
{
  AJAStatus
  status (AJA_STATUS_SUCCESS);

  mDeviceID = DEVICE_ID_NOTFOUND;

  return status;
}

static gpointer
init_setup_mutex (gpointer data) {
  sem_t *s = SEM_FAILED;
  s = sem_open ("/gstreamer-ajavideosrc-sem", O_CREAT, S_IRUSR|S_IWUSR, 1);
  if (s == SEM_FAILED) {
    g_critical ("Failed to create SHM semaphore for GStreamer AJA video source: %s", g_strerror (errno));
  }
  return s;
}

static sem_t *
get_setup_mutex (void) {
  static GOnce once = G_ONCE_INIT;

  g_once (&once, init_setup_mutex, NULL);

  return (sem_t *) once.retval;
}

class ShmMutexLocker {
  public:
    ShmMutexLocker() {
      sem_t *s = get_setup_mutex ();
      if (s != SEM_FAILED)
        sem_wait (s);
    }

    ~ShmMutexLocker() {
      sem_t *s = get_setup_mutex ();
      if (s != SEM_FAILED)
        sem_post (s);
    }
};

AJAStatus
    NTV2GstAV::Init (const NTV2VideoFormat inVideoFormat,
    const NTV2InputSource inInputSource,
    const uint32_t inBitDepth,
    const bool inIsRGBA,
    const bool inIs422,
    const bool inIsAuto,
    const SDIInputMode inSDIInputMode,
    const NTV2TCIndex inTimeCode,
    const bool inCaptureTall,
    const bool inPassthrough,
    const uint32_t inCaptureCPUCore,
    GstCaps *inCaps,
    const bool inUseNvmm)
{
  AJAStatus status (AJA_STATUS_SUCCESS);

  ShmMutexLocker locker;

  mVideoSource = inInputSource;
  mBitDepth = inBitDepth;
  mIsRGBA = inIsRGBA;
  mIs422 = inIs422;
  mIsAuto = inIsAuto;
  mTimecodeMode = inTimeCode;
  mCaptureTall = inCaptureTall;
  mPassthrough = inPassthrough;
  mSDIInputMode = inSDIInputMode;
  mCaptureCPUCore = inCaptureCPUCore;
  mCaps = inCaps;

#if ENABLE_NVMM
  mUseNvmm = inUseNvmm;
#else
  if (inUseNvmm) {
    GST_ERROR ("NVMM RDMA Not Supported");
    return AJA_STATUS_FAIL;
  }
#endif

  // Map input video modes. For quad-link and UHD/4k HDMI we need to map
  // to 4x modes, otherwise keep mode as is
  mQuad = true;
  if (mSDIInputMode == SDI_INPUT_MODE_QUAD_LINK_SQD ||
      mSDIInputMode == SDI_INPUT_MODE_QUAD_LINK_TSI ||
      mVideoSource == NTV2_INPUTSOURCE_HDMI1) {
    switch (inVideoFormat) {
      case NTV2_FORMAT_3840x2160p_2398:
        mVideoFormat = NTV2_FORMAT_4x1920x1080p_2398;
        break;
      case NTV2_FORMAT_3840x2160p_2400:
        mVideoFormat = NTV2_FORMAT_4x1920x1080p_2400;
        break;
      case NTV2_FORMAT_3840x2160p_2500:
        mVideoFormat = NTV2_FORMAT_4x1920x1080p_2500;
        break;
      case NTV2_FORMAT_3840x2160p_2997:
        mVideoFormat = NTV2_FORMAT_4x1920x1080p_2997;
        break;
      case NTV2_FORMAT_3840x2160p_3000:
        mVideoFormat = NTV2_FORMAT_4x1920x1080p_3000;
        break;
      case NTV2_FORMAT_3840x2160p_5000:
        mVideoFormat = NTV2_FORMAT_4x1920x1080p_5000;
        break;
      case NTV2_FORMAT_3840x2160p_5994:
        mVideoFormat = NTV2_FORMAT_4x1920x1080p_5994;
        break;
      case NTV2_FORMAT_3840x2160p_6000:
        mVideoFormat = NTV2_FORMAT_4x1920x1080p_6000;
        break;
      case NTV2_FORMAT_4096x2160p_2398:
        mVideoFormat = NTV2_FORMAT_4x2048x1080p_2398;
        break;
      case NTV2_FORMAT_4096x2160p_2400:
        mVideoFormat = NTV2_FORMAT_4x2048x1080p_2400;
        break;
      case NTV2_FORMAT_4096x2160p_2500:
        mVideoFormat = NTV2_FORMAT_4x2048x1080p_2500;
        break;
      case NTV2_FORMAT_4096x2160p_2997:
        mVideoFormat = NTV2_FORMAT_4x2048x1080p_2997;
        break;
      case NTV2_FORMAT_4096x2160p_3000:
        mVideoFormat = NTV2_FORMAT_4x2048x1080p_3000;
        break;
      case NTV2_FORMAT_4096x2160p_4795:
        mVideoFormat = NTV2_FORMAT_4x2048x1080p_4795;
        break;
      case NTV2_FORMAT_4096x2160p_4800:
        mVideoFormat = NTV2_FORMAT_4x2048x1080p_4800;
        break;
      case NTV2_FORMAT_4096x2160p_5000:
        mVideoFormat = NTV2_FORMAT_4x2048x1080p_5000;
        break;
      case NTV2_FORMAT_4096x2160p_5994:
        mVideoFormat = NTV2_FORMAT_4x2048x1080p_5994;
        break;
      case NTV2_FORMAT_4096x2160p_6000:
        mVideoFormat = NTV2_FORMAT_4x2048x1080p_6000;
        break;
      case NTV2_FORMAT_4096x2160p_11988:
        mVideoFormat = NTV2_FORMAT_4x2048x1080p_11988;
        break;
      case NTV2_FORMAT_4096x2160p_12000:
        mVideoFormat = NTV2_FORMAT_4x2048x1080p_12000;
        break;
      default:
        if (inVideoFormat >= NTV2_FORMAT_FIRST_UHD2_DEF_FORMAT &&
            inVideoFormat <= NTV2_FORMAT_END_UHD2_FULL_DEF_FORMATS) {
          // For UHD2/8k there are only the 4x formats available currently
          mVideoFormat = inVideoFormat;
          mQuad = true;
        } else {
          if (mSDIInputMode == SDI_INPUT_MODE_QUAD_LINK_SQD ||
              mSDIInputMode == SDI_INPUT_MODE_QUAD_LINK_TSI) {
            GST_ERROR ("Quad mode requires UHD/UHD2/4k/8k resolution");
            return AJA_STATUS_FAIL;
          }

          // Ok for HDMI
          mVideoFormat = inVideoFormat;
          mQuad = false;
        }
        break;
    }
  } else {
    mVideoFormat = inVideoFormat;
    mQuad = false;
  }

  if (mSDIInputMode == SDI_INPUT_MODE_QUAD_LINK_SQD || mSDIInputMode == SDI_INPUT_MODE_QUAD_LINK_TSI) {
    if (mInputChannel != NTV2_CHANNEL1 && mInputChannel != NTV2_CHANNEL5) {
      GST_ERROR ("Quad mode requires channel 1 or 5");
      return AJA_STATUS_FAIL;
    }
  }

  //  If we are in auto mode then do nothing if we are already running, otherwise force raw, 422, 8 bit.
  //  This flag shoud only be driven by the audiosrc to either start a non running channel without having
  //  to know anything about the video or to latch onto an alreay running channel in the event it has been
  //  started by the videosrc.
  if (mIsAuto) {
    if (mStarted)
      return AJA_STATUS_SUCCESS;

    //  Get SDI input format
    status = DetermineInputFormat (mInputChannel,
        mSDIInputMode == SDI_INPUT_MODE_QUAD_LINK_SQD || mSDIInputMode == SDI_INPUT_MODE_QUAD_LINK_TSI,
        mVideoFormat);
    if (AJA_FAILURE (status))
      return status;

    mBitDepth = 8;
  }

  // Ensure that mCaptureTall is only set for formats that can
  // actually contain VANC and that we handle
  switch (mVideoFormat) {
    case NTV2_FORMAT_720p_2398:
    case NTV2_FORMAT_720p_2500:
    case NTV2_FORMAT_720p_5000:
    case NTV2_FORMAT_720p_5994:
    case NTV2_FORMAT_720p_6000:
    case NTV2_FORMAT_1080i_5000:
    case NTV2_FORMAT_1080i_5994:
    case NTV2_FORMAT_1080i_6000:
    case NTV2_FORMAT_1080p_2398:
    case NTV2_FORMAT_1080p_2400:
    case NTV2_FORMAT_1080p_2500:
    case NTV2_FORMAT_1080p_2997:
    case NTV2_FORMAT_1080p_3000:
    case NTV2_FORMAT_1080p_5000_A:
    case NTV2_FORMAT_1080p_5994_A:
    case NTV2_FORMAT_1080p_6000_A:
      break;
    default:
      mCaptureTall = false;
      break;
  }

  // Don't do full frame capture for HDMI
  if (mVideoSource == NTV2_INPUTSOURCE_HDMI1) {
    mCaptureTall = false;
  }

  //    Setup frame buffer
  status = SetupVideo ();
  if (AJA_FAILURE (status)) {
    GST_ERROR ("Video setup failure");
    return status;
  }

  return AJA_STATUS_SUCCESS;
}

AJAStatus
NTV2GstAV::InitAudio (const NTV2AudioSource inAudioSource, uint32_t * numAudioChannels)
{
  AJAStatus
  status (AJA_STATUS_SUCCESS);

  mAudioSource = inAudioSource;
  mNumAudioChannels = *numAudioChannels;

  //    Setup audio buffer
  status = SetupAudio ();
  if (AJA_FAILURE (status)) {
    GST_ERROR ("Audio setup failure");
    return status;
  }

  ULWord
      nchannels = -1;
  mDevice.GetNumberAudioChannels (nchannels, mAudioSystem);
  *numAudioChannels = nchannels;

  return status;
}

void
NTV2GstAV::Quit (void)
{
  if (!mLastFrame && !mGlobalQuit) {
    //    Set the last frame flag to start the quit process
    mLastFrame = true;

    //    Wait for the last frame to be written to disk
    int i;
    int timeout = 300;
    for (i = 0; i < timeout; i++) {
      if (mLastFrameVideoOut && mLastFrameAudioOut)
        break;
      AJATime::Sleep (10);
    }

    if (i == timeout)
      GST_ERROR ("ERROR: Wait for last frame timeout");
  }

  //    Stop the worker threads
  mGlobalQuit = true;
  mStarted = false;

  StopACThread ();
  FreeHostBuffers ();

  //  Stop video capture
  mDevice.SetMode (mInputChannel, NTV2_MODE_DISPLAY, false);
  if (mQuad) {
    mDevice.SetMode ((NTV2Channel) (mInputChannel + 1), NTV2_MODE_DISPLAY, false);
    mDevice.SetMode ((NTV2Channel) (mInputChannel + 2), NTV2_MODE_DISPLAY, false);
    mDevice.SetMode ((NTV2Channel) (mInputChannel + 3), NTV2_MODE_DISPLAY, false);
  }
}

AJAStatus NTV2GstAV::SetupVideo (void)
{
  // Figure out frame buffer format
  if (mIsRGBA)
    mPixelFormat = NTV2_FBF_ABGR;
  else if (mBitDepth == 8)
    mPixelFormat = NTV2_FBF_8BIT_YCBCR;
  else
    mPixelFormat = NTV2_FBF_10BIT_YCBCR;

  // Enable and subscribe to the interrupts for the channel to be used...
  mDevice.EnableOutputInterrupt ();
  mDevice.EnableInputInterrupt (mInputChannel);
  mDevice.SubscribeInputVerticalEvent (mInputChannel);

  // Enable input channel
  mDevice.SetMode (mInputChannel, NTV2_MODE_CAPTURE, false);
  mDevice.SetFrameBufferFormat (mInputChannel, mPixelFormat);

  if (mQuad && mVideoSource == NTV2_INPUTSOURCE_HDMI1) {
    mDevice.Set4kSquaresEnable(true, (NTV2Channel) mInputChannel);
    mDevice.SetTsiFrameEnable(true, (NTV2Channel) mInputChannel);
  } else if (mQuad) {
    if (mSDIInputMode == SDI_INPUT_MODE_QUAD_LINK_SQD) {
      if (mVideoFormat >= NTV2_FORMAT_FIRST_UHD2_DEF_FORMAT &&
          mVideoFormat <= NTV2_FORMAT_END_UHD2_FULL_DEF_FORMATS) {
        mDevice.SetQuadQuadFrameEnable(true, (NTV2Channel) mInputChannel);
        mDevice.SetQuadQuadSquaresEnable(true, (NTV2Channel) mInputChannel);
      } else {
        mDevice.Set4kSquaresEnable(true, (NTV2Channel) mInputChannel);
        mDevice.SetTsiFrameEnable(false, (NTV2Channel) mInputChannel);
      }
    } else if (mSDIInputMode == SDI_INPUT_MODE_QUAD_LINK_TSI) {
      if (mVideoFormat >= NTV2_FORMAT_FIRST_UHD2_DEF_FORMAT &&
          mVideoFormat <= NTV2_FORMAT_END_UHD2_FULL_DEF_FORMATS) {
        mDevice.SetQuadQuadFrameEnable(true, (NTV2Channel) mInputChannel);
        mDevice.SetQuadQuadSquaresEnable(false, (NTV2Channel) mInputChannel);
      } else {
        mDevice.Set4kSquaresEnable(false, (NTV2Channel) mInputChannel);
        mDevice.SetTsiFrameEnable(true, (NTV2Channel) mInputChannel);
      }
    } else {
      g_assert_not_reached ();
    }
  } else {
    mDevice.Set4kSquaresEnable(false, (NTV2Channel) mInputChannel);
    mDevice.SetTsiFrameEnable(false, (NTV2Channel) mInputChannel);
  }

  mDevice.EnableChannel (mInputChannel);

  //    Setup frame buffer
  if (mQuad) {
    //    Set capture mode
    mDevice.SetMode ((NTV2Channel) (mInputChannel + 1), NTV2_MODE_CAPTURE, false);
    mDevice.SetMode ((NTV2Channel) (mInputChannel + 2), NTV2_MODE_CAPTURE, false);
    mDevice.SetMode ((NTV2Channel) (mInputChannel + 3), NTV2_MODE_CAPTURE, false);

    //    Set frame buffer format
    mDevice.SetFrameBufferFormat ((NTV2Channel) (mInputChannel + 1), mPixelFormat);
    mDevice.SetFrameBufferFormat ((NTV2Channel) (mInputChannel + 2), mPixelFormat);
    mDevice.SetFrameBufferFormat ((NTV2Channel) (mInputChannel + 3), mPixelFormat);

    //    Enable frame buffers
    mDevice.EnableChannel ((NTV2Channel) (mInputChannel + 1));
    mDevice.EnableChannel ((NTV2Channel) (mInputChannel + 2));
    mDevice.EnableChannel ((NTV2Channel) (mInputChannel + 3));
  }

  mDevice.SetVideoFormat(mVideoFormat, true, false, mInputChannel);

  mTimeBase.SetAJAFrameRate (GetAJAFrameRate (GetNTV2FrameRateFromVideoFormat
          (mVideoFormat)));

  // Set up routing

  // Select input channel based on mode
  NTV2CrosspointID inputIdentifier = NTV2_XptSDIIn1;
  NTV2InputCrosspointID cscInput = NTV2_XptCSC1VidInput;
  NTV2CrosspointID cscOutput = NTV2_XptCSC1VidRGB;
  bool inputRGB = false;
  switch (mVideoSource) {
    case NTV2_INPUTSOURCE_SDI1:
      // Select correct values based on channel
      switch (mInputChannel) {
         default:
         case NTV2_CHANNEL1:
            inputIdentifier = NTV2_XptSDIIn1;
            mInputSource    = NTV2_INPUTSOURCE_SDI1;
            cscInput = NTV2_XptCSC1VidInput;
            cscOutput = mIsRGBA? NTV2_XptCSC1VidRGB : NTV2_XptCSC1VidYUV;
            break;
         case NTV2_CHANNEL2:
            inputIdentifier = NTV2_XptSDIIn2;
            mInputSource    = NTV2_INPUTSOURCE_SDI2;
            cscInput = NTV2_XptCSC2VidInput;
            cscOutput = mIsRGBA? NTV2_XptCSC2VidRGB : NTV2_XptCSC2VidYUV;
            break;
         case NTV2_CHANNEL3:
            inputIdentifier = NTV2_XptSDIIn3;
            mInputSource    = NTV2_INPUTSOURCE_SDI3;
            cscInput = NTV2_XptCSC3VidInput;
            cscOutput = mIsRGBA? NTV2_XptCSC3VidRGB : NTV2_XptCSC3VidYUV;
            break;
         case NTV2_CHANNEL4:
            inputIdentifier = NTV2_XptSDIIn4;
            mInputSource    = NTV2_INPUTSOURCE_SDI4;
            cscInput = NTV2_XptCSC4VidInput;
            cscOutput = mIsRGBA? NTV2_XptCSC4VidRGB : NTV2_XptCSC4VidYUV;
            break;
         case NTV2_CHANNEL5:
            inputIdentifier = NTV2_XptSDIIn5;
            mInputSource    = NTV2_INPUTSOURCE_SDI5;
            cscInput = NTV2_XptCSC5VidInput;
            cscOutput = mIsRGBA? NTV2_XptCSC5VidRGB : NTV2_XptCSC5VidYUV;
            break;
         case NTV2_CHANNEL6:
            inputIdentifier = NTV2_XptSDIIn6;
            mInputSource    = NTV2_INPUTSOURCE_SDI6;
            cscInput = NTV2_XptCSC6VidInput;
            cscOutput = mIsRGBA? NTV2_XptCSC6VidRGB : NTV2_XptCSC6VidYUV;
            break;
         case NTV2_CHANNEL7:
            inputIdentifier = NTV2_XptSDIIn7;
            mInputSource    = NTV2_INPUTSOURCE_SDI7;
            cscInput = NTV2_XptCSC7VidInput;
            cscOutput = mIsRGBA? NTV2_XptCSC7VidRGB : NTV2_XptCSC7VidYUV;
            break;
         case NTV2_CHANNEL8:
            inputIdentifier = NTV2_XptSDIIn8;
            mInputSource    = NTV2_INPUTSOURCE_SDI8;
            cscInput = NTV2_XptCSC8VidInput;
            cscOutput = mIsRGBA? NTV2_XptCSC8VidRGB : NTV2_XptCSC8VidYUV;
            break;
      }

      if(!::NTV2DeviceCanDoInputSource (mDeviceID, mInputSource))
        mInputSource = NTV2_INPUTSOURCE_SDI1;

      break;
    case NTV2_INPUTSOURCE_HDMI1:
      // Select correct values based on channel
      NTV2LHIHDMIColorSpace hdmiColor;
      mDevice.GetHDMIInputColor (hdmiColor, mInputChannel);
      inputRGB = hdmiColor == NTV2_LHIHDMIColorSpaceRGB;
      switch (mInputChannel) {
         default:
         case NTV2_CHANNEL1:
            inputIdentifier = inputRGB? NTV2_XptHDMIIn1RGB : NTV2_XptHDMIIn1;
            mInputSource    = NTV2_INPUTSOURCE_HDMI1;
            cscInput        = NTV2_XptCSC1VidInput;
            cscOutput       = mIsRGBA? NTV2_XptCSC1VidRGB : NTV2_XptCSC1VidYUV;
            break;
         case NTV2_CHANNEL2:
            inputIdentifier = inputRGB? NTV2_XptHDMIIn2RGB : NTV2_XptHDMIIn2;
            mInputSource    = NTV2_INPUTSOURCE_HDMI2;
            cscInput        = NTV2_XptCSC2VidInput;
            cscOutput       = mIsRGBA? NTV2_XptCSC2VidRGB : NTV2_XptCSC2VidYUV;
            break;
         case NTV2_CHANNEL3:
            inputIdentifier = inputRGB? NTV2_XptHDMIIn3RGB : NTV2_XptHDMIIn3;
            mInputSource    = NTV2_INPUTSOURCE_HDMI3;
            cscInput        = NTV2_XptCSC3VidInput;
            cscOutput       = mIsRGBA? NTV2_XptCSC3VidRGB : NTV2_XptCSC3VidYUV;
            break;
         case NTV2_CHANNEL4:
            inputIdentifier = inputRGB? NTV2_XptHDMIIn4RGB : NTV2_XptHDMIIn4;
            mInputSource    = NTV2_INPUTSOURCE_HDMI4;
            cscInput        = NTV2_XptCSC4VidInput;
            cscOutput       = mIsRGBA? NTV2_XptCSC4VidRGB : NTV2_XptCSC4VidYUV;
            break;
      }
      break;
    case NTV2_INPUTSOURCE_ANALOG1:
      inputIdentifier = NTV2_XptAnalogIn;
      mInputSource = NTV2_INPUTSOURCE_ANALOG1;
      cscInput = NTV2_XptCSC1VidInput;
      cscOutput = mIsRGBA? NTV2_XptCSC1VidRGB : NTV2_XptCSC1VidYUV;
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  NTV2InputCrosspointID fbfInputSelect;
  CNTV2SignalRouter router;

  // Get old router
  mDevice.GetRouting(router);

  // Get corresponding input select entries for the channel
  switch (mInputChannel) {
    default:
    case NTV2_CHANNEL1:
      fbfInputSelect = NTV2_XptFrameBuffer1Input;
      break;
    case NTV2_CHANNEL2:
      fbfInputSelect = NTV2_XptFrameBuffer2Input;
      break;
    case NTV2_CHANNEL3:
      fbfInputSelect = NTV2_XptFrameBuffer3Input;
      break;
    case NTV2_CHANNEL4:
      fbfInputSelect = NTV2_XptFrameBuffer4Input;
      break;
    case NTV2_CHANNEL5:
      fbfInputSelect = NTV2_XptFrameBuffer5Input;
      break;
    case NTV2_CHANNEL6:
      fbfInputSelect = NTV2_XptFrameBuffer6Input;
      break;
    case NTV2_CHANNEL7:
      fbfInputSelect = NTV2_XptFrameBuffer7Input;
      break;
    case NTV2_CHANNEL8:
      fbfInputSelect = NTV2_XptFrameBuffer8Input;
      break;
  }

  // Disconnect anything related to the input channel(s) and other connectors
  // we would be using for any other mode for those input channel(s).
  if (mQuad && mVideoSource == NTV2_INPUTSOURCE_HDMI1) {
    // Need to disconnect the 4 inputs corresponding to this channel from
    // their framebuffers/muxers, and muxers from their framebuffers
    NTV2ActualConnections connections = router.GetConnections();

    for (NTV2ActualConnectionsConstIter iter = connections.begin(); iter != connections.end(); iter++) {
      if (iter->first == NTV2_XptFrameBuffer1Input ||
          iter->first == NTV2_XptFrameBuffer1BInput ||
          iter->first == NTV2_XptFrameBuffer2Input ||
          iter->first == NTV2_XptFrameBuffer2BInput ||
          iter->second == NTV2_Xpt425Mux1AYUV ||
          iter->second == NTV2_Xpt425Mux1BYUV ||
          iter->second == NTV2_Xpt425Mux2AYUV ||
          iter->second == NTV2_Xpt425Mux2BYUV ||
          iter->first == NTV2_Xpt425Mux1AInput ||
          iter->first == NTV2_Xpt425Mux1BInput ||
          iter->first == NTV2_Xpt425Mux2AInput ||
          iter->first == NTV2_Xpt425Mux2BInput ||
          iter->second == NTV2_XptHDMIIn1 ||
          iter->second == NTV2_XptHDMIIn1Q2 ||
          iter->second == NTV2_XptHDMIIn1Q3 ||
          iter->second == NTV2_XptHDMIIn1Q4)
        router.RemoveConnection(iter->first, iter->second);
    }
  } else if (mQuad && mInputChannel == NTV2_CHANNEL1) {
    // Need to disconnect the 4 inputs corresponding to this channel from
    // their framebuffers/muxers, and muxers from their framebuffers
    NTV2ActualConnections connections = router.GetConnections();

    for (NTV2ActualConnectionsConstIter iter = connections.begin(); iter != connections.end(); iter++) {
      if (iter->first == NTV2_XptFrameBuffer1Input ||
          iter->first == NTV2_XptFrameBuffer1BInput ||
          iter->first == NTV2_XptFrameBuffer1DS2Input ||
          iter->first == NTV2_XptFrameBuffer2Input ||
          iter->first == NTV2_XptFrameBuffer2BInput ||
          iter->first == NTV2_XptFrameBuffer2DS2Input ||
          iter->second == NTV2_Xpt425Mux1AYUV ||
          iter->second == NTV2_Xpt425Mux1BYUV ||
          iter->second == NTV2_Xpt425Mux2AYUV ||
          iter->second == NTV2_Xpt425Mux2BYUV ||
          iter->first == NTV2_Xpt425Mux1AInput ||
          iter->first == NTV2_Xpt425Mux1BInput ||
          iter->first == NTV2_Xpt425Mux2AInput ||
          iter->first == NTV2_Xpt425Mux2BInput ||
          iter->second == NTV2_XptSDIIn1 ||
          iter->second == NTV2_XptSDIIn2 ||
          iter->second == NTV2_XptSDIIn3 ||
          iter->second == NTV2_XptSDIIn4 ||
          iter->second == NTV2_XptSDIIn1DS2 ||
          iter->second == NTV2_XptSDIIn2DS2 ||
          iter->first == NTV2_XptFrameBuffer1Input ||
          iter->first == NTV2_XptFrameBuffer2Input ||
          iter->first == NTV2_XptFrameBuffer3Input ||
          iter->first == NTV2_XptFrameBuffer4Input
          )
        router.RemoveConnection(iter->first, iter->second);
    }
  } else if (mQuad) {
    // Need to disconnect the 4 inputs corresponding to this channel from
    // their framebuffers/muxers, and muxers from their framebuffers
    NTV2ActualConnections connections = router.GetConnections();

    for (NTV2ActualConnectionsConstIter iter = connections.begin(); iter != connections.end(); iter++) {
      if (iter->first == NTV2_XptFrameBuffer5Input ||
          iter->first == NTV2_XptFrameBuffer5BInput ||
          iter->first == NTV2_XptFrameBuffer5DS2Input ||
          iter->first == NTV2_XptFrameBuffer6Input ||
          iter->first == NTV2_XptFrameBuffer6BInput ||
          iter->first == NTV2_XptFrameBuffer6DS2Input ||
          iter->second == NTV2_Xpt425Mux3AYUV ||
          iter->second == NTV2_Xpt425Mux3BYUV ||
          iter->second == NTV2_Xpt425Mux4AYUV ||
          iter->second == NTV2_Xpt425Mux4BYUV ||
          iter->first == NTV2_Xpt425Mux3AInput ||
          iter->first == NTV2_Xpt425Mux3BInput ||
          iter->first == NTV2_Xpt425Mux4AInput ||
          iter->first == NTV2_Xpt425Mux4BInput ||
          iter->second == NTV2_XptSDIIn5 ||
          iter->second == NTV2_XptSDIIn6 ||
          iter->second == NTV2_XptSDIIn7 ||
          iter->second == NTV2_XptSDIIn8 ||
          iter->second == NTV2_XptSDIIn5DS2 ||
          iter->second == NTV2_XptSDIIn6DS2 ||
          iter->first == NTV2_XptFrameBuffer5Input ||
          iter->first == NTV2_XptFrameBuffer6Input ||
          iter->first == NTV2_XptFrameBuffer7Input ||
          iter->first == NTV2_XptFrameBuffer8Input)
        router.RemoveConnection(iter->first, iter->second);
    }
  } else {
    NTV2ActualConnections connections = router.GetConnections();

    // Disconnect input and its framebuffer
    for (NTV2ActualConnectionsConstIter iter = connections.begin(); iter != connections.end(); iter++) {
      if (iter->first == fbfInputSelect ||
          iter->second == inputIdentifier)
        router.RemoveConnection(iter->first, iter->second);

    // Disconnect input and its csc
      if (iter->first == cscInput ||
          iter->second == inputIdentifier)
        router.RemoveConnection(iter->first, iter->second);

      if (((inputIdentifier == NTV2_XptSDIIn6 || inputIdentifier == NTV2_XptSDIIn8) &&
          iter->first == NTV2_XptFrameBuffer6BInput) ||
          ((inputIdentifier == NTV2_XptSDIIn5 || inputIdentifier == NTV2_XptSDIIn6) &&
          iter->first == NTV2_XptFrameBuffer5BInput) ||
          ((inputIdentifier == NTV2_XptSDIIn4 || inputIdentifier == NTV2_XptSDIIn2) &&
          iter->first == NTV2_XptFrameBuffer2BInput) ||
          ((inputIdentifier == NTV2_XptSDIIn1 || inputIdentifier == NTV2_XptSDIIn2) &&
          iter->first == NTV2_XptFrameBuffer1BInput))
        router.RemoveConnection(iter->first, iter->second);
    }
  }

  // Special-case for UHD HDMI and SDI TSI
  if (mQuad && mVideoSource == NTV2_INPUTSOURCE_HDMI1) {
    inputIdentifier = NTV2_Xpt425Mux1AYUV;
  } else if (mQuad && mSDIInputMode == SDI_INPUT_MODE_QUAD_LINK_TSI && mVideoFormat < NTV2_FORMAT_FIRST_UHD2_DEF_FORMAT) {
    if (mInputChannel == NTV2_CHANNEL1)
      inputIdentifier = NTV2_Xpt425Mux1AYUV;
    else
      inputIdentifier = NTV2_Xpt425Mux3AYUV;
  }

  if ((mIsRGBA && inputRGB) || (!mIsRGBA && !inputRGB)) {
    // Add to the mapping to the router for this channel
    router.AddConnection (fbfInputSelect, inputIdentifier);
  } else {
    // Route the channel into the CSC for color conversion
    router.AddConnection (cscInput, inputIdentifier);
    router.AddConnection (fbfInputSelect, cscOutput);
  }

  // Disable SDI output from the SDI input being used,
  // but only if the device supports bi-directional SDI,
  // and only if the input being used is an SDI input
  if (::NTV2DeviceHasBiDirectionalSDI (mDeviceID)) {
    mDevice.SetSDITransmitEnable(mInputChannel, false);
    if (mQuad) {
      mDevice.SetSDITransmitEnable ((NTV2Channel) (mInputChannel + 1), false);
      mDevice.SetSDITransmitEnable ((NTV2Channel) (mInputChannel + 2), false);
      mDevice.SetSDITransmitEnable ((NTV2Channel) (mInputChannel + 3), false);
      mDevice.WaitForOutputVerticalInterrupt ();
      mDevice.WaitForOutputVerticalInterrupt ();
      mDevice.WaitForOutputVerticalInterrupt ();
    }
    mDevice.WaitForOutputVerticalInterrupt ();
  } else {
    if (mVideoSource == NTV2_INPUTSOURCE_HDMI1) {
      // Enable HDMI passthrough
      if (mInputChannel == NTV2_CHANNEL1) {
        router.AddConnection(NTV2_XptHDMIOutQ1Input, NTV2_XptHDMIIn1);
      } else if (mInputChannel == NTV2_CHANNEL2) {
        router.AddConnection(NTV2_XptHDMIOutQ2Input, NTV2_XptHDMIIn2);
      } else if (mInputChannel == NTV2_CHANNEL3) {
        router.AddConnection(NTV2_XptHDMIOutQ3Input, NTV2_XptHDMIIn3);
      } else if (mInputChannel == NTV2_CHANNEL4) {
        router.AddConnection(NTV2_XptHDMIOutQ4Input, NTV2_XptHDMIIn4);
      }
    } else {
      // enable SDI End to End mode for all AJA cards that don't support bidirectional SDI

      if (mInputChannel == NTV2_CHANNEL1) {
        router.AddConnection (NTV2_XptSDIOut1Input, NTV2_XptSDIIn1);
      } else if (mInputChannel == NTV2_CHANNEL2) {
        router.AddConnection (NTV2_XptSDIOut2Input, NTV2_XptSDIIn2);
      } else if (mInputChannel == NTV2_CHANNEL3) {
        router.AddConnection (NTV2_XptSDIOut3Input, NTV2_XptSDIIn3);
      } else if (mInputChannel == NTV2_CHANNEL4) {
        router.AddConnection (NTV2_XptSDIOut4Input, NTV2_XptSDIIn4);
      }
    }
  }

  // Enable UHD/4k quad mode
  if (mQuad) {
    if (mVideoSource == NTV2_INPUTSOURCE_HDMI1) {
        router.AddConnection(NTV2_XptFrameBuffer1BInput, NTV2_Xpt425Mux1BYUV);
        router.AddConnection(NTV2_XptFrameBuffer2Input, NTV2_Xpt425Mux2AYUV);
        router.AddConnection(NTV2_XptFrameBuffer2BInput, NTV2_Xpt425Mux2BYUV);

        router.AddConnection(NTV2_Xpt425Mux1AInput, NTV2_XptHDMIIn1);
        router.AddConnection(NTV2_Xpt425Mux1BInput, NTV2_XptHDMIIn1Q2);
        router.AddConnection(NTV2_Xpt425Mux2AInput, NTV2_XptHDMIIn1Q3);
        router.AddConnection(NTV2_Xpt425Mux2BInput, NTV2_XptHDMIIn1Q4);
    } else if (mSDIInputMode == SDI_INPUT_MODE_QUAD_LINK_TSI && NTV2_IS_QUAD_QUAD_HFR_VIDEO_FORMAT(mVideoFormat)) {
      if (mInputChannel == NTV2_CHANNEL1) {
        router.AddConnection(NTV2_XptFrameBuffer1DS2Input, NTV2_XptSDIIn2);
        router.AddConnection(NTV2_XptFrameBuffer2Input, NTV2_XptSDIIn3);
        router.AddConnection(NTV2_XptFrameBuffer2DS2Input, NTV2_XptSDIIn4);
      } else {
        router.AddConnection(NTV2_XptFrameBuffer5DS2Input, NTV2_XptSDIIn6);
        router.AddConnection(NTV2_XptFrameBuffer5Input, NTV2_XptSDIIn7);
        router.AddConnection(NTV2_XptFrameBuffer6DS2Input, NTV2_XptSDIIn8);
      }
      mOutputChannel = NTV2_CHANNEL5;
    } else if (mSDIInputMode == SDI_INPUT_MODE_QUAD_LINK_TSI && NTV2_IS_QUAD_QUAD_FORMAT(mVideoFormat)) {
      if (mInputChannel == NTV2_CHANNEL1) {
        router.AddConnection(NTV2_XptFrameBuffer1DS2Input, NTV2_XptSDIIn1DS2);
        router.AddConnection(NTV2_XptFrameBuffer2Input, NTV2_XptSDIIn2);
        router.AddConnection(NTV2_XptFrameBuffer2DS2Input, NTV2_XptSDIIn2DS2);
      } else {
        router.AddConnection(NTV2_XptFrameBuffer5DS2Input, NTV2_XptSDIIn5DS2);
        router.AddConnection(NTV2_XptFrameBuffer5Input, NTV2_XptSDIIn6);
        router.AddConnection(NTV2_XptFrameBuffer6DS2Input, NTV2_XptSDIIn6DS2);
      }
      mOutputChannel = NTV2_CHANNEL5;

    } else if (mSDIInputMode == SDI_INPUT_MODE_QUAD_LINK_TSI) {
      if (mInputChannel == NTV2_CHANNEL1) {
        router.AddConnection(NTV2_XptFrameBuffer1BInput, NTV2_Xpt425Mux1BYUV);
        router.AddConnection(NTV2_XptFrameBuffer2Input, NTV2_Xpt425Mux2AYUV);
        router.AddConnection(NTV2_XptFrameBuffer2BInput, NTV2_Xpt425Mux2BYUV);

        router.AddConnection(NTV2_Xpt425Mux1AInput, NTV2_XptSDIIn1);
        router.AddConnection(NTV2_Xpt425Mux1BInput, NTV2_XptSDIIn2);
        router.AddConnection(NTV2_Xpt425Mux2AInput, NTV2_XptSDIIn3);
        router.AddConnection(NTV2_Xpt425Mux2BInput, NTV2_XptSDIIn4);
      } else {
        router.AddConnection(NTV2_XptFrameBuffer5BInput, NTV2_Xpt425Mux3BYUV);
        router.AddConnection(NTV2_XptFrameBuffer6Input, NTV2_Xpt425Mux4AYUV);
        router.AddConnection(NTV2_XptFrameBuffer6BInput, NTV2_Xpt425Mux4BYUV);

        router.AddConnection(NTV2_Xpt425Mux3AInput, NTV2_XptSDIIn5);
        router.AddConnection(NTV2_Xpt425Mux3BInput, NTV2_XptSDIIn6);
        router.AddConnection(NTV2_Xpt425Mux4AInput, NTV2_XptSDIIn7);
        router.AddConnection(NTV2_Xpt425Mux4BInput, NTV2_XptSDIIn8);
      }
      mOutputChannel = NTV2_CHANNEL5;
    } else {
      if (mInputChannel == NTV2_CHANNEL1) {
        router.AddConnection(NTV2_XptFrameBuffer2Input, NTV2_XptSDIIn2);
        router.AddConnection(NTV2_XptFrameBuffer3Input, NTV2_XptSDIIn3);
        router.AddConnection(NTV2_XptFrameBuffer4Input, NTV2_XptSDIIn4);
      } else {
        router.AddConnection(NTV2_XptFrameBuffer6Input, NTV2_XptSDIIn6);
        router.AddConnection(NTV2_XptFrameBuffer7Input, NTV2_XptSDIIn7);
        router.AddConnection(NTV2_XptFrameBuffer8Input, NTV2_XptSDIIn8);
      }
      mOutputChannel = NTV2_CHANNEL5;
    }
  }

  // Enable passthrough on bidirectional devices
  if (mPassthrough && ::NTV2DeviceHasBiDirectionalSDI(mDeviceID)) {
    int numVideoInputs = NTV2DeviceGetNumVideoOutputs (mDeviceID);

    mDevice.SetMode((NTV2Channel)(mInputChannel + numVideoInputs / 2), NTV2_MODE_DISPLAY);
    // Enable End to End mode for all AJA cards that don't support bidirectional SDI
    if (mInputChannel == NTV2_CHANNEL1) {
      router.AddConnection ((numVideoInputs == 8) ? NTV2_XptSDIOut5Input : NTV2_XptSDIOut3Input, NTV2_XptSDIIn1);
      mDevice.SetSDITransmitEnable ((numVideoInputs == 8) ? NTV2_CHANNEL5 : NTV2_CHANNEL3, true);
    } else if (mInputChannel == NTV2_CHANNEL2) {
      router.AddConnection ((numVideoInputs == 8) ? NTV2_XptSDIOut6Input : NTV2_XptSDIOut4Input, NTV2_XptSDIIn2);
      mDevice.SetSDITransmitEnable ((numVideoInputs == 8) ? NTV2_CHANNEL6 : NTV2_CHANNEL4 , true);
    } else if (mInputChannel == NTV2_CHANNEL3) {
      router.AddConnection ((numVideoInputs == 8) ? NTV2_XptSDIOut7Input : NTV2_XptSDIOut5Input, NTV2_XptSDIIn3);
      mDevice.SetSDITransmitEnable ((numVideoInputs == 8) ? NTV2_CHANNEL7 : NTV2_CHANNEL5 , true);
    } else if (mInputChannel == NTV2_CHANNEL4) {
      router.AddConnection ((numVideoInputs == 8) ? NTV2_XptSDIOut8Input : NTV2_XptSDIOut6Input, NTV2_XptSDIIn4);
      mDevice.SetSDITransmitEnable ((numVideoInputs == 8) ? NTV2_CHANNEL8 : NTV2_CHANNEL6 , true);
    }
  }

  // VANC handling
  if (mCaptureTall) {
    GST_DEBUG ("Asking to enable VANC Data");
    mDevice.SetEnableVANCData (true, false, mInputChannel);
    if (mPixelFormat == NTV2_FBF_8BIT_YCBCR) {
      GST_DEBUG ("8bit, asking to shift VANC");
      if (!mDevice.SetVANCShiftMode (mInputChannel,
              NTV2_VANCDATA_8BITSHIFT_ENABLE))
        GST_WARNING ("Failed to request 8bit VANC shift");
    }
  }

  // Enable routes
  {
    std::stringstream os;
    CNTV2SignalRouter oldRouter;
    mDevice.GetRouting(oldRouter);
    oldRouter.Print(os);
    GST_DEBUG ("Previous routing:\n%s", os.str().c_str());
  }
  mDevice.ApplySignalRoute (router, true);
  {
    std::stringstream os;
    CNTV2SignalRouter currentRouter;
    mDevice.GetRouting(currentRouter);
    currentRouter.Print(os);
    GST_DEBUG ("New routing:\n%s", os.str().c_str());
  }

  //    Set the device reference to the input...
  //    FIXME
//  if (mMultiStream) {
//    mDevice.SetReference (NTV2_REFERENCE_FREERUN);
//  } else {
    mDevice.SetReference (::NTV2InputSourceToReferenceSource (mInputSource));
//  }

#if 0
  //    When input is 3Gb convert to 3Ga for capture (no RGB support?)
  bool is3Gb = false;
  mDevice.GetSDIInput3GbPresent (is3Gb, mInputChannel);

  if (mQuad) {
    mDevice.SetSDIInLevelBtoLevelAConversion (NTV2_CHANNEL1, is3Gb);
    mDevice.SetSDIInLevelBtoLevelAConversion (NTV2_CHANNEL2, is3Gb);
    mDevice.SetSDIInLevelBtoLevelAConversion (NTV2_CHANNEL3, is3Gb);
    mDevice.SetSDIInLevelBtoLevelAConversion (NTV2_CHANNEL4, is3Gb);
    mDevice.SetSDIOutLevelAtoLevelBConversion (NTV2_CHANNEL5, false);
    mDevice.SetSDIOutLevelAtoLevelBConversion (NTV2_CHANNEL6, false);
    mDevice.SetSDIOutLevelAtoLevelBConversion (NTV2_CHANNEL7, false);
    mDevice.SetSDIOutLevelAtoLevelBConversion (NTV2_CHANNEL8, false);
  } else {
    mDevice.SetSDIInLevelBtoLevelAConversion (mInputChannel, is3Gb);
    mDevice.SetSDIOutLevelAtoLevelBConversion (mOutputChannel, false);
  }

  if (!mMultiStream)            //    If not doing multistream...
    mDevice.ClearRouting ();    //    ...replace existing routing

  //    Connect SDI output spigots to FB outputs...
  mDevice.Connect (NTV2_XptSDIOut5Input, NTV2_XptFrameBuffer5YUV);
  mDevice.Connect (NTV2_XptSDIOut6Input, NTV2_XptFrameBuffer6YUV);
  mDevice.Connect (NTV2_XptSDIOut7Input, NTV2_XptFrameBuffer7YUV);
  mDevice.Connect (NTV2_XptSDIOut8Input, NTV2_XptFrameBuffer8YUV);
#endif

  //    Give the device some time to lock to the input signal...
  mDevice.WaitForOutputVerticalInterrupt (mInputChannel, 8);


  return AJA_STATUS_SUCCESS;
}                               //    SetupAudio


AJAStatus NTV2GstAV::SetupAudio (void)
{
  // Select audio system to use based on the channel
  switch (mInputChannel) {
    default:
    case NTV2_CHANNEL1:
      mAudioSystem = NTV2_AUDIOSYSTEM_1;
      break;
    case NTV2_CHANNEL2:
      mAudioSystem = NTV2_AUDIOSYSTEM_2;
      break;
    case NTV2_CHANNEL3:
      mAudioSystem = NTV2_AUDIOSYSTEM_3;
      break;
    case NTV2_CHANNEL4:
      mAudioSystem = NTV2_AUDIOSYSTEM_4;
      break;
    case NTV2_CHANNEL5:
      mAudioSystem = NTV2_AUDIOSYSTEM_5;
      break;
    case NTV2_CHANNEL6:
      mAudioSystem = NTV2_AUDIOSYSTEM_6;
      break;
    case NTV2_CHANNEL7:
      mAudioSystem = NTV2_AUDIOSYSTEM_7;
      break;
    case NTV2_CHANNEL8:
      mAudioSystem = NTV2_AUDIOSYSTEM_8;
      break;
  }

  // Then based on channel and/or mode, select the audio input
  switch (mAudioSource) {
    case NTV2_AUDIO_EMBEDDED:
      switch (mInputChannel) {
        default:
        case NTV2_CHANNEL1:
          mDevice.SetAudioSystemInputSource (mAudioSystem, NTV2_AUDIO_EMBEDDED, NTV2_EMBEDDED_AUDIO_INPUT_VIDEO_1);
          mDevice.SetEmbeddedAudioInput(NTV2_EMBEDDED_AUDIO_INPUT_VIDEO_1, mAudioSystem);
          break;
        case NTV2_CHANNEL2:
          mDevice.SetAudioSystemInputSource (mAudioSystem, NTV2_AUDIO_EMBEDDED, NTV2_EMBEDDED_AUDIO_INPUT_VIDEO_2);
          mDevice.SetEmbeddedAudioInput(NTV2_EMBEDDED_AUDIO_INPUT_VIDEO_2, mAudioSystem);
          break;
        case NTV2_CHANNEL3:
          mDevice.SetAudioSystemInputSource (mAudioSystem, NTV2_AUDIO_EMBEDDED, NTV2_EMBEDDED_AUDIO_INPUT_VIDEO_3);
          mDevice.SetEmbeddedAudioInput(NTV2_EMBEDDED_AUDIO_INPUT_VIDEO_3, mAudioSystem);
          break;
        case NTV2_CHANNEL4:
          mDevice.SetAudioSystemInputSource (mAudioSystem, NTV2_AUDIO_EMBEDDED, NTV2_EMBEDDED_AUDIO_INPUT_VIDEO_4);
          mDevice.SetEmbeddedAudioInput(NTV2_EMBEDDED_AUDIO_INPUT_VIDEO_4, mAudioSystem);
          break;
        case NTV2_CHANNEL5:
          mDevice.SetAudioSystemInputSource (mAudioSystem, NTV2_AUDIO_EMBEDDED, NTV2_EMBEDDED_AUDIO_INPUT_VIDEO_5);
          mDevice.SetEmbeddedAudioInput(NTV2_EMBEDDED_AUDIO_INPUT_VIDEO_5, mAudioSystem);
          break;
        case NTV2_CHANNEL6:
          mDevice.SetAudioSystemInputSource (mAudioSystem, NTV2_AUDIO_EMBEDDED, NTV2_EMBEDDED_AUDIO_INPUT_VIDEO_6);
          mDevice.SetEmbeddedAudioInput(NTV2_EMBEDDED_AUDIO_INPUT_VIDEO_6, mAudioSystem);
          break;
        case NTV2_CHANNEL7:
          mDevice.SetAudioSystemInputSource (mAudioSystem, NTV2_AUDIO_EMBEDDED, NTV2_EMBEDDED_AUDIO_INPUT_VIDEO_7);
          mDevice.SetEmbeddedAudioInput(NTV2_EMBEDDED_AUDIO_INPUT_VIDEO_7, mAudioSystem);
          break;
        case NTV2_CHANNEL8:
          mDevice.SetAudioSystemInputSource (mAudioSystem, NTV2_AUDIO_EMBEDDED, NTV2_EMBEDDED_AUDIO_INPUT_VIDEO_8);
          mDevice.SetEmbeddedAudioInput(NTV2_EMBEDDED_AUDIO_INPUT_VIDEO_8, mAudioSystem);
          break;
      }

      break;
    case NTV2_AUDIO_HDMI:
      switch (mInputChannel) {
        default:
        case NTV2_CHANNEL1:
          mDevice.SetAudioSystemInputSource (mAudioSystem, NTV2_AUDIO_HDMI, NTV2_EMBEDDED_AUDIO_INPUT_VIDEO_1);
          mDevice.SetEmbeddedAudioInput(NTV2_EMBEDDED_AUDIO_INPUT_VIDEO_1, mAudioSystem);
          break;
        case NTV2_CHANNEL2:
          mDevice.SetAudioSystemInputSource (mAudioSystem, NTV2_AUDIO_HDMI, NTV2_EMBEDDED_AUDIO_INPUT_VIDEO_2);
          mDevice.SetEmbeddedAudioInput(NTV2_EMBEDDED_AUDIO_INPUT_VIDEO_2, mAudioSystem);
          break;
        case NTV2_CHANNEL3:
          mDevice.SetAudioSystemInputSource (mAudioSystem, NTV2_AUDIO_HDMI, NTV2_EMBEDDED_AUDIO_INPUT_VIDEO_3);
          mDevice.SetEmbeddedAudioInput(NTV2_EMBEDDED_AUDIO_INPUT_VIDEO_3, mAudioSystem);
          break;
        case NTV2_CHANNEL4:
          mDevice.SetAudioSystemInputSource (mAudioSystem, NTV2_AUDIO_HDMI, NTV2_EMBEDDED_AUDIO_INPUT_VIDEO_4);
          mDevice.SetEmbeddedAudioInput(NTV2_EMBEDDED_AUDIO_INPUT_VIDEO_4, mAudioSystem);
          break;
      }
      break;
    case NTV2_AUDIO_AES:
      mDevice.SetAudioSystemInputSource (mAudioSystem, NTV2_AUDIO_AES, NTV2_EMBEDDED_AUDIO_INPUT_VIDEO_1);
      break;
    case NTV2_AUDIO_ANALOG:
      mDevice.SetAudioSystemInputSource (mAudioSystem, NTV2_AUDIO_ANALOG, NTV2_EMBEDDED_AUDIO_INPUT_VIDEO_1);
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  if (mNumAudioChannels == 0)
    mNumAudioChannels =::NTV2DeviceGetMaxAudioChannels (mDeviceID);
  if (mNumAudioChannels >::NTV2DeviceGetMaxAudioChannels (mDeviceID))
    return AJA_STATUS_FAIL;

  // Setting channels or getting the maximum number of channels generally fails and we always
  // get all channels available to the card
  mDevice.SetNumberAudioChannels (mNumAudioChannels, mAudioSystem);
  mDevice.SetAudioRate (NTV2_AUDIO_48K, mAudioSystem);
  mDevice.SetEmbeddedAudioClock (NTV2_EMBEDDED_AUDIO_CLOCK_VIDEO_INPUT,
      mAudioSystem);

  //    The on-device audio buffer should be 4MB to work best across all devices & platforms...
  mDevice.SetAudioBufferSize (NTV2_AUDIO_BUFFER_BIG, mAudioSystem);

  mDevice.SetAudioLoopBack(NTV2_AUDIO_LOOPBACK_OFF, mAudioSystem);

  return AJA_STATUS_SUCCESS;

}                               //    SetupAudio


void
NTV2GstAV::SetupHostBuffers (void)
{
  mVideoBufferSize =
      GetVideoActiveSize (mVideoFormat, mPixelFormat,
      mCaptureTall ? NTV2_VANCMODE_TALL : NTV2_VANCMODE_OFF);
  mAudioBufferSize = NTV2_AUDIOSIZE_MAX;

  mDevice.DMABufferAutoLock(false, true, 0);

  // These video buffers are actually passed out of this class so we need to assign them unique numbers
  // so they can be tracked and also they have a state
  GstStructure *config;
#if ENABLE_NVMM
  if (mUseNvmm) {
    mVideoBufferPool = gst_aja_nvmm_buffer_pool_new ();
    config = gst_buffer_pool_get_config (mVideoBufferPool);
    gst_buffer_pool_config_set_params (config, mCaps, mVideoBufferSize,
        VIDEO_ARRAY_SIZE, 0);

    gst_buffer_pool_set_config (mVideoBufferPool, config);
    gst_buffer_pool_set_active (mVideoBufferPool, TRUE);
  } else
#endif
  {
    GstAllocator *video_alloc = gst_aja_allocator_new(&mDevice, mVideoBufferSize, VIDEO_ARRAY_SIZE);

    mVideoBufferPool = gst_aja_buffer_pool_new ();
    config = gst_buffer_pool_get_config (mVideoBufferPool);
    gst_buffer_pool_config_set_params (config, NULL, mVideoBufferSize,
        VIDEO_ARRAY_SIZE, 0);
    gst_buffer_pool_config_set_allocator (config, video_alloc, NULL);
    gst_structure_set (config, "is-video", G_TYPE_BOOLEAN, TRUE, NULL);

    gst_buffer_pool_set_config (mVideoBufferPool, config);
    gst_buffer_pool_set_active (mVideoBufferPool, TRUE);
    gst_object_unref (video_alloc);
  }

  GstAllocator *audio_alloc = gst_aja_allocator_new(&mDevice, mAudioBufferSize, AUDIO_ARRAY_SIZE);
  mAudioBufferPool = gst_aja_buffer_pool_new ();
  config = gst_buffer_pool_get_config (mAudioBufferPool);
  gst_buffer_pool_config_set_params (config, NULL, mAudioBufferSize,
      AUDIO_ARRAY_SIZE, 0);
  gst_buffer_pool_config_set_allocator (config, audio_alloc, NULL);
  gst_structure_set (config, "is-video", G_TYPE_BOOLEAN, FALSE, NULL);
  gst_buffer_pool_set_config (mAudioBufferPool, config);
  gst_buffer_pool_set_active (mAudioBufferPool, TRUE);
  gst_object_unref (audio_alloc);
}                               //    SetupHostBuffers


void
NTV2GstAV::FreeHostBuffers (void)
{
  if (mVideoBufferPool) {
    gst_buffer_pool_set_active (mVideoBufferPool, FALSE);
    gst_object_unref (mVideoBufferPool);
    mVideoBufferPool = NULL;
  }

  if (mAudioBufferPool) {
    gst_buffer_pool_set_active (mAudioBufferPool, FALSE);
    gst_object_unref (mAudioBufferPool);
    mAudioBufferPool = NULL;
  }
}

void
NTV2GstAV::SetupAutoCirculate (void)
{
  //    Tell capture AutoCirculate to use 8 frame buffers on the device...
  mInputTransferStruct.Clear ();
  mInputTransferStruct.acFrameBufferFormat = mPixelFormat;

  int frameStart, frameEnd;

  // FIXME: This works around a bug in the SDK. In theory by
  // setting the number of frames to circulate only it should
  // figure out correct start/end frame indices, but this causes
  // corrupted output.
  //
  // Let's assume at least 6 frames (15 for UHD) per channel here and
  // calculate our own indices

  if (NTV2_IS_12G_FORMAT (mVideoFormat) ||
      NTV2_IS_QUAD_FRAME_FORMAT (mVideoFormat) || NTV2_IS_QUAD_QUAD_FORMAT (mVideoFormat)) {
    frameStart = mInputChannel * 15;
    frameEnd = frameStart + 14;
  } else {
    frameStart = mInputChannel * 7;
    frameEnd = frameStart + 6;
  }

  mDevice.AutoCirculateStop (mInputChannel);
  mDevice.AutoCirculateInitForInput (mInputChannel, 0,  //    Frames to circulate
      mAudioSystem,             //    Which audio system
      AUTOCIRCULATE_WITH_RP188, //    With RP188?
      1,                        //    1 channel
      frameStart,
      frameEnd);
}


AJAStatus NTV2GstAV::Run ()
{
  mLastFrame = false;
  mLastFrameInput = false;
  mLastFrameVideoOut = false;
  mLastFrameAudioOut = false;
  mGlobalQuit = false;


  //    Setup to capture video/audio/anc input
  SetupAutoCirculate ();

  //    Setup the circular buffers
  SetupHostBuffers ();

  if (mDevice.GetInputVideoFormat (mInputSource) == NTV2_FORMAT_UNKNOWN)
    GST_WARNING ("No video signal present on the input connector");

  // always start the AC thread
  StartACThread ();

  mStarted = true;
  return AJA_STATUS_SUCCESS;
}


// This is where we will start the AC thread
void
NTV2GstAV::StartACThread (void)
{
  mACInputThread = new AJAThread ();
  mACInputThread->Attach (ACInputThreadStatic, this);
  mACInputThread->SetPriority (AJA_ThreadPriority_High);
  mACInputThread->Start ();
}


// This is where we will stop the AC thread
void
NTV2GstAV::StopACThread (void)
{
  if (mACInputThread) {
    while (mACInputThread->Active ())
      AJATime::Sleep (10);

    delete mACInputThread;
    mACInputThread = NULL;
  }
}


// The video input thread static callback
void
NTV2GstAV::ACInputThreadStatic (AJAThread * pThread, void *pContext)
{
  (void) pThread;

  NTV2GstAV *pApp (reinterpret_cast < NTV2GstAV * >(pContext));

  if (pApp->mCaptureCPUCore != (uint32_t) -1) {
    cpu_set_t mask;
    pthread_t current_thread = pthread_self();

    CPU_ZERO(&mask);
    CPU_SET(pApp->mCaptureCPUCore, &mask);

    if (pthread_setaffinity_np(current_thread, sizeof (mask), &mask) != 0) {
      GST_ERROR ("Failed to set affinity for current thread to core %u", pApp->mCaptureCPUCore);
    }
  }

  pApp->ACInputWorker ();
}


void
NTV2GstAV::ACInputWorker (void)
{
  // Choose timecode source
  NTV2TCIndex tcIndex, configuredTcIndex = (NTV2TCIndex)-1;
  ULWord vpidA, vpidB;

  // start AutoCirculate running...
  mDevice.AutoCirculateStart (mInputChannel);

  bool haveSignal = true;
  unsigned int iterations_without_frame = 0;

  uint64_t processed_frames = 0;
  uint64_t dropped_frames = 0;
  uint32_t last_dropped_frames = 0;
  bool dropped_frames_now = false;

  while (!mGlobalQuit) {
    AUTOCIRCULATE_STATUS acStatus;
    mDevice.AutoCirculateGetStatus (mInputChannel, acStatus);

    // Update timecode index if it changed since the last frame
    if (configuredTcIndex == (NTV2TCIndex)-1 || configuredTcIndex != mTimecodeMode) {
      configuredTcIndex = mTimecodeMode;
      if (mTimecodeMode == NTV2_TCINDEX_LTC1 || mTimecodeMode == NTV2_TCINDEX_LTC2) {
        tcIndex = mTimecodeMode;
        mDevice.SetLTCInputEnable (true);
      } else {
        switch (mInputChannel) {
          default:
          case NTV2_CHANNEL1:
            if (mTimecodeMode == NTV2_TCINDEX_SDI1)
              tcIndex = NTV2_TCINDEX_SDI1_LTC;
            else if (mTimecodeMode == NTV2_TCINDEX_SDI1_LTC)
              tcIndex = NTV2_TCINDEX_SDI1_LTC;
            else
              tcIndex = NTV2_TCINDEX_SDI1_2;
            break;
          case NTV2_CHANNEL2:
            if (mTimecodeMode == NTV2_TCINDEX_SDI1)
              tcIndex = NTV2_TCINDEX_SDI2_LTC;
            else if (mTimecodeMode == NTV2_TCINDEX_SDI1_LTC)
              tcIndex = NTV2_TCINDEX_SDI2_LTC;
            else
              tcIndex = NTV2_TCINDEX_SDI2_2;
            break;
          case NTV2_CHANNEL3:
            if (mTimecodeMode == NTV2_TCINDEX_SDI1)
              tcIndex = NTV2_TCINDEX_SDI3_LTC;
            else if (mTimecodeMode == NTV2_TCINDEX_SDI1_LTC)
              tcIndex = NTV2_TCINDEX_SDI3_LTC;
            else
              tcIndex = NTV2_TCINDEX_SDI3_2;
            break;
          case NTV2_CHANNEL4:
            if (mTimecodeMode == NTV2_TCINDEX_SDI1)
              tcIndex = NTV2_TCINDEX_SDI4_LTC;
            else if (mTimecodeMode == NTV2_TCINDEX_SDI1_LTC)
              tcIndex = NTV2_TCINDEX_SDI4_LTC;
            else
              tcIndex = NTV2_TCINDEX_SDI4_2;
            break;
          case NTV2_CHANNEL5:
            if (mTimecodeMode == NTV2_TCINDEX_SDI1)
              tcIndex = NTV2_TCINDEX_SDI5_LTC;
            else if (mTimecodeMode == NTV2_TCINDEX_SDI1_LTC)
              tcIndex = NTV2_TCINDEX_SDI5_LTC;
            else
              tcIndex = NTV2_TCINDEX_SDI5_2;
            break;
          case NTV2_CHANNEL6:
            if (mTimecodeMode == NTV2_TCINDEX_SDI1)
              tcIndex = NTV2_TCINDEX_SDI6_LTC;
            else if (mTimecodeMode == NTV2_TCINDEX_SDI1_LTC)
              tcIndex = NTV2_TCINDEX_SDI6_LTC;
            else
              tcIndex = NTV2_TCINDEX_SDI6_2;
            break;
          case NTV2_CHANNEL7:
            if (mTimecodeMode == NTV2_TCINDEX_SDI1)
              tcIndex = NTV2_TCINDEX_SDI7_LTC;
            else if (mTimecodeMode == NTV2_TCINDEX_SDI1_LTC)
              tcIndex = NTV2_TCINDEX_SDI7_LTC;
            else
              tcIndex = NTV2_TCINDEX_SDI7_2;
            break;
          case NTV2_CHANNEL8:
            if (mTimecodeMode == NTV2_TCINDEX_SDI1)
              tcIndex = NTV2_TCINDEX_SDI8_LTC;
            else if (mTimecodeMode == NTV2_TCINDEX_SDI1_LTC)
              tcIndex = NTV2_TCINDEX_SDI8_LTC;
            else
              tcIndex = NTV2_TCINDEX_SDI8_2;
            break;
        }
      }
    }

    NTV2VideoFormat inputVideoFormat = mDevice.GetInputVideoFormat(mInputSource);
    vpidA = 0;
    vpidB = 0;
    mDevice.ReadSDIInVPID(mInputChannel, vpidA, vpidB);

    GST_DEBUG ("Got input video format %08x and VPIDs %08x / %08x", (int) inputVideoFormat, vpidA, vpidB);

    // For quad mode, we will get the format of a single input
    NTV2VideoFormat effectiveVideoFormat = mVideoFormat;
    if (mQuad && mVideoSource == NTV2_INPUTSOURCE_SDI1) {
      switch (mVideoFormat) {
        case NTV2_FORMAT_4x1920x1080p_2398:
          effectiveVideoFormat = NTV2_FORMAT_1080p_2398;
          break;
        case NTV2_FORMAT_4x1920x1080p_2400:
          effectiveVideoFormat = NTV2_FORMAT_1080p_2400;
          break;
        case NTV2_FORMAT_4x1920x1080p_2500:
          effectiveVideoFormat = NTV2_FORMAT_1080p_2500;
          break;
        case NTV2_FORMAT_4x1920x1080p_2997:
          effectiveVideoFormat = NTV2_FORMAT_1080p_2997;
          break;
        case NTV2_FORMAT_4x1920x1080p_3000:
          effectiveVideoFormat = NTV2_FORMAT_1080p_3000;
          break;
        case NTV2_FORMAT_4x1920x1080p_5000:
          effectiveVideoFormat = NTV2_FORMAT_1080p_5000_A;
          break;
        case NTV2_FORMAT_4x1920x1080p_5994:
          effectiveVideoFormat = NTV2_FORMAT_1080p_5994_A;
          break;
        case NTV2_FORMAT_4x1920x1080p_6000:
          effectiveVideoFormat = NTV2_FORMAT_1080p_6000_A;
          break;
        case NTV2_FORMAT_4x2048x1080p_2398:
          effectiveVideoFormat = NTV2_FORMAT_1080p_2K_2398;
          break;
        case NTV2_FORMAT_4x2048x1080p_2400:
          effectiveVideoFormat = NTV2_FORMAT_1080p_2K_2400;
          break;
        case NTV2_FORMAT_4x2048x1080p_2500:
          effectiveVideoFormat = NTV2_FORMAT_1080p_2K_2500;
          break;
        case NTV2_FORMAT_4x2048x1080p_2997:
          effectiveVideoFormat = NTV2_FORMAT_1080p_2K_2997;
          break;
        case NTV2_FORMAT_4x2048x1080p_3000:
          effectiveVideoFormat = NTV2_FORMAT_1080p_2K_3000;
          break;
        case NTV2_FORMAT_4x2048x1080p_4795:
          effectiveVideoFormat = NTV2_FORMAT_1080p_2K_4795_A;
          break;
        case NTV2_FORMAT_4x2048x1080p_4800:
          effectiveVideoFormat = NTV2_FORMAT_1080p_2K_4800_A;
          break;
        case NTV2_FORMAT_4x2048x1080p_5000:
          effectiveVideoFormat = NTV2_FORMAT_1080p_2K_5000_A;
          break;
        case NTV2_FORMAT_4x2048x1080p_5994:
          effectiveVideoFormat = NTV2_FORMAT_1080p_2K_5994_A;
          break;
        case NTV2_FORMAT_4x2048x1080p_6000:
          effectiveVideoFormat = NTV2_FORMAT_1080p_2K_6000_A;
          break;
        case NTV2_FORMAT_4x3840x2160p_2398:
          effectiveVideoFormat = NTV2_FORMAT_3840x2160p_2398;
          break;
        case NTV2_FORMAT_4x3840x2160p_2400:
          effectiveVideoFormat = NTV2_FORMAT_3840x2160p_2400;
          break;
        case NTV2_FORMAT_4x3840x2160p_2500:
          effectiveVideoFormat = NTV2_FORMAT_3840x2160p_2500;
          break;
        case NTV2_FORMAT_4x3840x2160p_2997:
          effectiveVideoFormat = NTV2_FORMAT_3840x2160p_2997;
          break;
        case NTV2_FORMAT_4x3840x2160p_3000:
          effectiveVideoFormat = NTV2_FORMAT_3840x2160p_3000;
          break;
        case NTV2_FORMAT_4x3840x2160p_5000:
          effectiveVideoFormat = NTV2_FORMAT_3840x2160p_5000;
          break;
        case NTV2_FORMAT_4x3840x2160p_5994:
          effectiveVideoFormat = NTV2_FORMAT_3840x2160p_5994;
          break;
        case NTV2_FORMAT_4x3840x2160p_6000:
          effectiveVideoFormat = NTV2_FORMAT_3840x2160p_6000;
          break;
        case NTV2_FORMAT_4x4096x2160p_2398:
          effectiveVideoFormat = NTV2_FORMAT_4096x2160p_2398;
          break;
        case NTV2_FORMAT_4x4096x2160p_2400:
          effectiveVideoFormat = NTV2_FORMAT_4096x2160p_2400;
          break;
        case NTV2_FORMAT_4x4096x2160p_2500:
          effectiveVideoFormat = NTV2_FORMAT_4096x2160p_2500;
          break;
        case NTV2_FORMAT_4x4096x2160p_2997:
          effectiveVideoFormat = NTV2_FORMAT_4096x2160p_2997;
          break;
        case NTV2_FORMAT_4x4096x2160p_3000:
          effectiveVideoFormat = NTV2_FORMAT_4096x2160p_3000;
          break;
        case NTV2_FORMAT_4x4096x2160p_4795:
          effectiveVideoFormat = NTV2_FORMAT_4096x2160p_4795;
          break;
        case NTV2_FORMAT_4x4096x2160p_4800:
          effectiveVideoFormat = NTV2_FORMAT_4096x2160p_4800;
          break;
        case NTV2_FORMAT_4x4096x2160p_5000:
          effectiveVideoFormat = NTV2_FORMAT_4096x2160p_5000;
          break;
        case NTV2_FORMAT_4x4096x2160p_5994:
          effectiveVideoFormat = NTV2_FORMAT_4096x2160p_5994;
          break;
        case NTV2_FORMAT_4x4096x2160p_6000:
          effectiveVideoFormat = NTV2_FORMAT_4096x2160p_6000;
          break;
        default:
          break;
      }
    }

    GST_DEBUG ("Expected video format %08x (effective %08x)", (int) mVideoFormat, (int) effectiveVideoFormat);

    haveSignal = (effectiveVideoFormat == inputVideoFormat) || (mVideoFormat == inputVideoFormat);

    GST_DEBUG ("Autocirculate state: %d, buffer level %u, frames processed %u, frames dropped %u",
               acStatus.acState, acStatus.acBufferLevel,
               acStatus.acFramesProcessed, acStatus.acFramesDropped);

    if (last_dropped_frames != acStatus.acFramesDropped) {
      if (last_dropped_frames > acStatus.acFramesDropped) {
        dropped_frames += (G_MAXUINT32 - last_dropped_frames) + acStatus.acFramesDropped;
      } else {
        dropped_frames += acStatus.acFramesDropped - last_dropped_frames;
      }

      last_dropped_frames = acStatus.acFramesDropped;
      dropped_frames_now = true;
      GST_ERROR ("Dropped frames! Captured %" G_GUINT64_FORMAT " dropped %" G_GUINT64_FORMAT, processed_frames + 1, dropped_frames);
    }

    GST_DEBUG ("Overall frames captured %" G_GUINT64_FORMAT " dropped %" G_GUINT64_FORMAT, processed_frames + 1, dropped_frames);

    // wait for captured frame
    if (acStatus.acState == NTV2_AUTOCIRCULATE_RUNNING
        && acStatus.acBufferLevel > 1) {
      // At this point, there's at least one fully-formed frame available in the device's
      // frame buffer to transfer to the host. Reserve an AvaDataBuffer to "produce", and
      // use it in the next transfer from the device...
      AjaVideoBuff *pVideoData (AcquireVideoBuffer ());
      GstMapInfo video_map, audio_map;

      iterations_without_frame = 0;
      pVideoData->haveSignal = haveSignal;

      if (pVideoData->buffer) {
        gst_buffer_map (pVideoData->buffer, &video_map, GST_MAP_READWRITE);
#if ENABLE_NVMM
        if (pVideoData->isNvmm) {
          NvBufSurface *surf = (NvBufSurface*)video_map.data;
          pVideoData->pVideoBuffer = (uint32_t *) surf->surfaceList[0].dataPtr;
          pVideoData->videoBufferSize = surf->surfaceList[0].dataSize;
          // Lock the buffer for RDMA
          mDevice.DMABufferLock(pVideoData->pVideoBuffer, pVideoData->videoBufferSize, false, true);
        } else
#endif
        {
          pVideoData->pVideoBuffer = (uint32_t *) video_map.data;
          pVideoData->videoBufferSize = video_map.size;
        }
      }
      mInputTransferStruct.SetVideoBuffer (pVideoData->pVideoBuffer,
          pVideoData->videoBufferSize);

      AjaAudioBuff *pAudioData = AcquireAudioBuffer ();
      pAudioData->haveSignal = haveSignal;
      if (pAudioData->buffer) {
        gst_buffer_map (pAudioData->buffer, &audio_map, GST_MAP_READWRITE);
        pAudioData->pAudioBuffer = (uint32_t *) audio_map.data;
        pAudioData->audioBufferSize = audio_map.size;
      }
      mInputTransferStruct.SetAudioBuffer (pAudioData->pAudioBuffer,
          pAudioData->audioBufferSize);

      // do the transfer from the device into our host AvaDataBuffer...
      mDevice.AutoCirculateTransfer (mInputChannel, mInputTransferStruct);

#if ENABLE_NVMM
      if (pVideoData->isNvmm) {
        // Unlock from RDMA
        mDevice.DMABufferUnlock(pVideoData->pVideoBuffer, pVideoData->videoBufferSize);
        NvBufSurface *surf = (NvBufSurface*)video_map.data;
        surf->numFilled = 1;
      }
#endif

      // get the video data size
      pVideoData->videoDataSize = pVideoData->videoBufferSize;
      if (pVideoData->buffer) {
        bool validVanc = false;
        NTV2FrameGeometry currentGeometry;
        gsize offset = 0;       // Offset in number of lines

        mDevice.GetFrameGeometry (currentGeometry);
        switch (currentGeometry) {
          case NTV2_FG_1920x1112:
            // 32 line offset
            // FIXME : Remove hardcording once gstntv2 is gstvideoformat aware
            if (mBitDepth == 8)
              offset = 32 * 1920 * 2;
            else
              offset = 32 * 1920 * 16 / 6;
            validVanc = true;
            break;
          case NTV2_FG_1280x740:
            // 20 line offset
            // FIXME : Remove hardcording once gstntv2 is gstvideoformat aware
            if (mBitDepth == 8)
              offset = 20 * 1280 * 2;
            else
              offset = 20 * 1296 * 16 / 6;
            validVanc = true;
            break;
          default:
            if (mCaptureTall)
              GST_ERROR ("UNKNOWN GEOMETRY %u!", currentGeometry);
            break;
        }
        GST_DEBUG ("offset %" G_GSIZE_FORMAT, offset);
        GST_DEBUG ("videoDataSize %u", pVideoData->videoDataSize);
        gst_buffer_unmap (pVideoData->buffer, &video_map);
        if (!pVideoData->isNvmm) {
          gst_buffer_resize (pVideoData->buffer, offset,
              pVideoData->videoDataSize - offset);
        }
        pVideoData->pAncillaryData = validVanc ? pVideoData->pVideoBuffer : NULL;
        pVideoData->pVideoBuffer = NULL;
      }
      pVideoData->lastFrame = mLastFrame;

      // get the audio data size
      pAudioData->audioDataSize =
          mInputTransferStruct.acTransferStatus.acAudioTransferSize;
      if (pAudioData->buffer) {
        gst_buffer_unmap (pAudioData->buffer, &audio_map);
        gst_buffer_resize (pAudioData->buffer, 0, pAudioData->audioDataSize);
        pAudioData->pAudioBuffer = NULL;
      }
      pAudioData->lastFrame = mLastFrame;

      // FIXME: this should actually use acAudioClockTimeStamp but
      // it does not actually seem to be based on a 48kHz clock
      pVideoData->timeStamp =
          mInputTransferStruct.acTransferStatus.acFrameStamp.acFrameTime;
      pAudioData->timeStamp =
          mInputTransferStruct.acTransferStatus.acFrameStamp.acFrameTime;

      pVideoData->fieldCount =
          mInputTransferStruct.acTransferStatus.
          acFrameStamp.acCurrentFieldCount;

      pVideoData->frameNumber = processed_frames + dropped_frames;
      pAudioData->frameNumber = processed_frames + dropped_frames;

      pVideoData->framesProcessed = processed_frames + 1;
      pAudioData->framesProcessed = processed_frames + 1;
      pVideoData->framesDropped = dropped_frames;
      pAudioData->framesDropped = dropped_frames;
      pVideoData->droppedChanged = dropped_frames_now;
      pAudioData->droppedChanged = dropped_frames_now;
      if (dropped_frames_now) {
        dropped_frames_now = false;
      }

      pVideoData->timeCodeValid = false;
      NTV2_RP188 timeCode;
      if (mInputTransferStruct.acTransferStatus.
          acFrameStamp.GetInputTimeCode (timeCode, tcIndex) &&
          (timeCode.fDBB != 0xffffffff || timeCode.fLo != 0xffffffff || timeCode.fHi != 0xffffffff)) {
        // get the sdi input anc data
        pVideoData->timeCodeDBB = timeCode.fDBB;
        pVideoData->timeCodeLow = timeCode.fLo;
        pVideoData->timeCodeHigh = timeCode.fHi;
        pVideoData->timeCodeValid = true;
      }

      pVideoData->transferCharacteristics = 3;
      pVideoData->colorimetry = 3;
      pVideoData->fullRange = FALSE;
      if (mVideoSource == NTV2_INPUTSOURCE_SDI1 && vpidA != 0) {
        if (vpidA & 0x80000000) {
          switch (vpidA >> 24) {
            case 0x81: // ST 352
            case 0x82: // ST 352
            case 0x83: // ST 352
            case 0x86: // ST 352
            case 0x84: // ST 292-1
            case 0x8B: // ST 425-1
            case 0x8D: // ST 425-1
            case 0xB1: // ST 292-2
            case 0x8E: // ST 425-2
            case 0x8F: // ST 425-2
            case 0x90: // ST 435-1
            case 0x91: // ST 425-4
            case 0x92: // ST 425-4
            case 0x93: // ST 425-4
            case 0xA0: // ST 435-1
            case 0xB0: // ST 2047-2
            case 0xB2: // ST 2047-4
            case 0xB3: // ST 2048-3
            case 0xB4: // RDD 22
            case 0xB5: // RDD 22
              // No colorimetry/transfer/range information
              break;
            case 0x85: // ST 292-1
            case 0x8C: // ST 425-1
            {
              guint colorimetry = (vpidA >> 12) & 0x09;

              pVideoData->transferCharacteristics = (vpidA >> 20) & 0x03;
              if (colorimetry == 0x00)
                pVideoData->colorimetry = 0;
              else if (colorimetry == 0x01)
                pVideoData->colorimetry = 1;
              else if (colorimetry == 0x08)
                pVideoData->colorimetry = 2;
              else if (colorimetry == 0x09)
                pVideoData->colorimetry = 3;

              pVideoData->fullRange = (vpidA & 0x02) == 0x02;
              break;
            }
              break;
            case 0x87: // ST 372
            case 0x8A: // ST 425-1
            case 0x95: // ST 425-3
            case 0x96: // ST 425-3
            case 0x98: // ST 425-5
            case 0x9A: // ST 425-6
            case 0x9B: // ST 425-6
            {
              guint colorimetry = (vpidA >> 12) & 0x09;

              pVideoData->transferCharacteristics = (vpidA >> 20) & 0x03;
              if (colorimetry == 0x00)
                pVideoData->colorimetry = 0;
              else if (colorimetry == 0x01)
                pVideoData->colorimetry = 1;
              else if (colorimetry == 0x08)
                pVideoData->colorimetry = 2;
              else if (colorimetry == 0x09)
                pVideoData->colorimetry = 3;

              pVideoData->fullRange = (vpidA & 0x03) == 0x00 || (vpidA & 0x03) == 0x03;
              break;
            }
            case 0x88: // ST 425-1
            case 0x89: // ST 425-1
            case 0x94: // ST 425-3
            case 0x97: // ST 425-5
            case 0x99: // ST 425-6
            case 0xC0: // ST 2081-10
            case 0xC2: // ST 2081-11
            case 0xF3: // ST 2081-11
            case 0xC4: // ST 2081-12
            case 0xC5: // ST 2081-12
            case 0xCE: // ST 2082-10
            case 0xCF: // ST 2082-10
            case 0xD0: // ST 2082-11
            case 0xD1: // ST 2082-11
            case 0xD2: // ST 2082-12
            case 0xD3: // ST 2082-12
              pVideoData->transferCharacteristics = (vpidA >> 20) & 0x03;
              pVideoData->colorimetry = (vpidA >> 12) & 0x03;
              pVideoData->fullRange = (vpidA & 0x03) == 0x00 || (vpidA & 0x03) == 0x03;
              break;
            case 0xA1: // ST 2036-3
            case 0xA2: // ST 2036-3
            case 0xA5: // ST 2036-4
            case 0xA6: // ST 2036-4
            case 0xC1: // ST 2081-10
              pVideoData->transferCharacteristics = (vpidA >> 20) & 0x03;
              pVideoData->colorimetry = 3;
              pVideoData->fullRange = (vpidA & 0x03) == 0x00 || (vpidA & 0x03) == 0x03;
              break;
            default:
              break;
          }
        } else {
          // Historical payload identifiers
          pVideoData->transferCharacteristics = 3;
          pVideoData->colorimetry = 3;
          pVideoData->fullRange = FALSE;
        }
      }

      if (pVideoData->lastFrame && !mLastFrameInput) {
        GST_INFO ("Capture last frame number %" G_GUINT64_FORMAT, processed_frames);
        mLastFrameInput = true;
      }

      processed_frames++;

      // Possible callbacks are not setup yet so make sure we release the buffer if
      // no one is there to catch them
      if (!DoCallback (VIDEO_CALLBACK, pVideoData))
        ReleaseVideoBuffer (pVideoData);

      if (pVideoData->lastFrame) {
        GST_INFO ("Video out last frame number %" G_GUINT64_FORMAT, processed_frames);
        mLastFrameVideoOut = true;
      }

      // Possible callbacks are not setup yet so make sure we release the buffer if
      // no one is there to catch them
      if (!DoCallback (AUDIO_CALLBACK, pAudioData))
        ReleaseAudioBuffer (pAudioData);

      if (pAudioData->lastFrame) {
        GST_INFO ("Audio out last frame number %" G_GUINT64_FORMAT, processed_frames);
        mLastFrameAudioOut = true;
      }
    } else {
      // Either AutoCirculate is not running, or there were no frames available on the device to transfer.
      // Rather than waste CPU cycles spinning, waiting until a frame becomes available, it's far more
      // efficient to wait for the next input vertical interrupt event to get signaled...
      if (mLastFrame) {
          mLastFrameVideoOut = true;
          mLastFrameAudioOut = true;
          break;
      } else {
        // If we don't have a frame for 32 iterations (512ms) then consider
        // this as signal loss too even if the driver still reports the
        // expected mode above
        if (haveSignal && iterations_without_frame < 32) {
          mDevice.WaitForInputVerticalInterrupt (mInputChannel);
          iterations_without_frame++;
        } else {
          DoCallback (VIDEO_CALLBACK, NULL);
          DoCallback (AUDIO_CALLBACK, NULL);
          // Short enough to not miss any frames at 60fps / 16.667ms per frame
          g_usleep (16000);
        }
      }
    }
  }                             // loop til quit signaled

  // Stop AutoCirculate...
  mDevice.AutoCirculateStop (mInputChannel);
}

void
NTV2GstAV::SetCallback (CallBackType cbType, NTV2Callback callback,
    void *callbackRefcon)
{
  if (cbType == VIDEO_CALLBACK) {
    mVideoCallback = callback;
    mVideoCallbackRefcon = callbackRefcon;
  } else if (cbType == AUDIO_CALLBACK) {
    mAudioCallback = callback;
    mAudioCallbackRefcon = callbackRefcon;
  }
}

AjaVideoBuff *
NTV2GstAV::AcquireVideoBuffer ()
{
  GstBuffer *buffer;
  AjaVideoBuff *videoBuff;

  if (gst_buffer_pool_acquire_buffer (mVideoBufferPool, &buffer,
          NULL) != GST_FLOW_OK)
    return NULL;

  videoBuff = gst_aja_buffer_get_video_buff (buffer);
  return videoBuff;
}

AjaAudioBuff *
NTV2GstAV::AcquireAudioBuffer ()
{
  GstBuffer *buffer;
  AjaAudioBuff *audioBuff;

  if (gst_buffer_pool_acquire_buffer (mAudioBufferPool, &buffer,
          NULL) != GST_FLOW_OK)
    return NULL;

  audioBuff = gst_aja_buffer_get_audio_buff (buffer);
  return audioBuff;
}


void
NTV2GstAV::ReleaseVideoBuffer (AjaVideoBuff * videoBuffer)
{
  if (videoBuffer->buffer)
    gst_buffer_unref (videoBuffer->buffer);
}


void
NTV2GstAV::ReleaseAudioBuffer (AjaAudioBuff * audioBuffer)
{
  if (audioBuffer->buffer)
    gst_buffer_unref (audioBuffer->buffer);
}


void
NTV2GstAV::AddRefVideoBuffer (AjaVideoBuff * videoBuffer)
{
  if (videoBuffer->buffer)
    gst_buffer_ref (videoBuffer->buffer);
}


void
NTV2GstAV::AddRefAudioBuffer (AjaAudioBuff * audioBuffer)
{
  if (audioBuffer->buffer)
    gst_buffer_ref (audioBuffer->buffer);
}


bool NTV2GstAV::GetHardwareClock (uint64_t desiredTimeScale, uint64_t * time)
{
  uint32_t
  audioCounter (0);

  bool status = mDevice.ReadRegister (kRegAud1Counter, audioCounter);
  *time = (audioCounter * desiredTimeScale) / 48000;
  return status;
}

void
NTV2GstAV::UpdateTimecodeIndex(const NTV2TCIndex inTimeCode)
{
  mTimecodeMode = inTimeCode;
}

AJAStatus
    NTV2GstAV::DetermineInputFormat (NTV2Channel inputChannel, bool quad,
    NTV2VideoFormat & videoFormat)
{
  NTV2VideoFormat sdiFormat = mDevice.GetSDIInputVideoFormat (inputChannel);
  if (sdiFormat == NTV2_FORMAT_UNKNOWN)
    return AJA_STATUS_FAIL;

  switch (sdiFormat) {
    case NTV2_FORMAT_1080p_5000_A:
    case NTV2_FORMAT_1080p_5000_B:
      videoFormat = NTV2_FORMAT_1080p_5000_A;
      if (quad)
        videoFormat = NTV2_FORMAT_4x1920x1080p_5000;
      break;
    case NTV2_FORMAT_1080p_5994_A:
    case NTV2_FORMAT_1080p_5994_B:
      videoFormat = NTV2_FORMAT_1080p_5994_A;
      if (quad)
        videoFormat = NTV2_FORMAT_4x1920x1080p_5994;
      break;
    case NTV2_FORMAT_1080p_6000_A:
    case NTV2_FORMAT_1080p_6000_B:
      videoFormat = NTV2_FORMAT_1080p_6000_A;
      if (quad)
        videoFormat = NTV2_FORMAT_4x1920x1080p_6000;
      break;
    default:
      videoFormat = sdiFormat;
      break;
  }

  return AJA_STATUS_SUCCESS;
}


AJA_FrameRate NTV2GstAV::GetAJAFrameRate (NTV2FrameRate frameRate)
{
  switch (frameRate) {
    case NTV2_FRAMERATE_2398:
      return AJA_FrameRate_2398;
    case NTV2_FRAMERATE_2400:
      return AJA_FrameRate_2400;
    case NTV2_FRAMERATE_2500:
      return AJA_FrameRate_2500;
    case NTV2_FRAMERATE_2997:
      return AJA_FrameRate_2997;
    case NTV2_FRAMERATE_3000:
      return AJA_FrameRate_3000;
    case NTV2_FRAMERATE_4795:
      return AJA_FrameRate_4795;
    case NTV2_FRAMERATE_4800:
      return AJA_FrameRate_4800;
    case NTV2_FRAMERATE_5000:
      return AJA_FrameRate_5000;
    case NTV2_FRAMERATE_5994:
      return AJA_FrameRate_5994;
    case NTV2_FRAMERATE_6000:
      return AJA_FrameRate_6000;
    default:
      break;
  }

  return AJA_FrameRate_Unknown;
}


bool NTV2GstAV::DoCallback (CallBackType type, void *msg)
{
  if (type == VIDEO_CALLBACK) {
    if (mVideoCallback) {
      return mVideoCallback (mVideoCallbackRefcon, msg);
    }
  } else if (type == AUDIO_CALLBACK) {
    if (mAudioCallback) {
      return mAudioCallback (mAudioCallbackRefcon, msg);
    }
  }
  return false;
}
