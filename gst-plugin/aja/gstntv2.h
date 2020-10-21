/**
    @file        ntv2capture.h
    @brief        Declares the NTV2Capture class.
    @copyright    Copyright (C) 2012-2015 AJA Video Systems, Inc.  All rights reserved.
                  Copyright (C) 2017 Sebastian Dröge <sebastian@centricular.com>
**/


#ifndef _NTV2ENCODEHEVC_H
#define _NTV2ENCODEHEVC_H

#include <gst/gst.h>

#include "ntv2enums.h"
#include "ntv2m31enums.h"
#include "ntv2signalrouter.h"
#include "ntv2devicescanner.h"
#include "ntv2rp188.h"

#include "ajabase/common/videotypes.h"
#include "ajabase/common/circularbuffer.h"
#include "ajabase/common/timebase.h"
#include "ajabase/system/thread.h"

#include "ntv2m31.h"

#define VIDEO_RING_SIZE            16
#define VIDEO_ARRAY_SIZE        60
#define AUDIO_RING_SIZE            (3*VIDEO_RING_SIZE)
#define AUDIO_ARRAY_SIZE        (3*VIDEO_ARRAY_SIZE)

#define ASECOND                 (1000000000)

typedef bool (*NTV2Callback) (void * refcon, void * msg);

typedef enum
{
    VIDEO_CALLBACK,
    AUDIO_CALLBACK
} CallBackType;

typedef enum {
  SDI_INPUT_MODE_SINGLE_LINK,
  SDI_INPUT_MODE_QUAD_LINK_SQD,
  SDI_INPUT_MODE_QUAD_LINK_TSI,
} SDIInputMode;

typedef struct
{
    GstBuffer *     buffer;                 /// If buffer != NULL, it actually owns the AjaVideoBuff and the following 3 fields are NULL

    uint32_t *      pVideoBuffer;           /// Pointer to host video buffer
    uint32_t        videoBufferSize;        /// Size of host video buffer (bytes)
    uint32_t        videoDataSize;          /// Size of video data (bytes)
    uint32_t *      pAncillaryData;           /// Pointer to host ancillary data

    uint32_t *      pInfoBuffer;            /// Picture information (raw) or encode information (hevc)
    uint32_t        infoBufferSize;         /// Size of the host information buffer (bytes)
    uint32_t        infoDataSize;           /// Size of the information data (bytes)

    uint64_t        frameNumber;            /// Frame number
    uint8_t         fieldCount;             /// Number of fields
    bool            timeCodeValid;
    uint32_t        timeCodeDBB;            /// Time code data dbb
    uint32_t        timeCodeLow;            /// Time code data low
    uint32_t        timeCodeHigh;           /// Time code data high
    uint64_t        timeStamp;              /// Time stamp of video data
    bool            lastFrame;              /// Indicates last captured frame
    bool            haveSignal;             /// true if we actually have signal
    bool            discont;                /// true if frames were dropped before this one

    uint8_t         transferCharacteristics; /// SDR-TV (0), HLG (1), PQ (2), unspecified (3)
    uint8_t         colorimetry;             /// Rec 709 (0), VANC (1), UHDTV (2), unspecified (3)
    bool            fullRange;               /// 0-255 if true, 16-235 otherwise
} AjaVideoBuff;


typedef struct
{
    GstBuffer *     buffer;                 /// If buffer != NULL, it actually owns the AjaAudioBuff

    uint32_t *      pAudioBuffer;           ///    Pointer to host audio buffer
    uint32_t        audioBufferSize;        ///    Size of host audio buffer (bytes)
    uint32_t        audioDataSize;          ///    Size of audio data (bytes)

    uint32_t        frameNumber;            /// Frame number
    uint64_t        timeStamp;              /// Time stamp of video data
    bool            lastFrame;              /// Indicates last captured frame
    bool            haveSignal;             /// true if we actually have signal
    bool            discont;                /// true if frames were dropped before this one
} AjaAudioBuff;

        

/**
    @brief    Instances of me capture frames in real time from a video signal provided to an input of
            an AJA device.
**/

class NTV2GstAV
{
    //    Public Instance Methods
    public:
        /**
            @brief    Constructs me using the given settings.
            @note    I'm not completely initialized and ready to use until after my Init method has been called.
            @param[in]    inDeviceSpecifier    Specifies the AJA device to use.
                                            Defaults to "0" (first device found).
            @param[in]    inChannel            Specifies the channel to use.
                                            Defaults to NTV2_CHANNEL1.
            @param[in]    inM31Preset            Specifies the m31 preset to use.
                                            Defaults to 8-bit 1280x720 5994p.
            @param[in]    inPixelFormat        Specifies the pixel format to use.
                                          aja_video_src->ntv2Hevc->  Defaults to NTV2_FBF_10BIT_YCBCR_420PL.
            @param[in]    inQuadMode          Specifies UHD mode.
                                            Defaults to HD mode.
            @param[in]    inAudioChannels        Specifies number of audio channels to write to AIFF file.
                                            Defaults to 2 channels.
            @param[in]    inTimeCodeBurn      Add timecode burn.
                                            Defaults to no timecode burn.
            @param[in]    inInfoData          Use picture and encoded information.
                                            Defaults to no info data.
            @param[in]    inCaptureTall       Capture Tall Video (i.e. with VBI)
                                            Defaults to regular (non-tall) video height
        **/
        NTV2GstAV(const std::string            inDeviceSpecifier    = "0",
                      const NTV2Channel            inChannel            = NTV2_CHANNEL1);

        virtual     ~NTV2GstAV ();
    
        /**
         @brief    Open/Close, discover and allocate/release an NTV2 device.
         **/
        virtual AJAStatus        Open (void);
        virtual AJAStatus        Close (void);

        /**
            @brief    Initializes me and prepares me to Run.
        **/
        virtual AJAStatus Init (const M31VideoPreset            inPreset        = M31_FILE_720X480_420_8_5994i,
                                const NTV2VideoFormat           inVideoFormat   = NTV2_FORMAT_525_5994,
                                const NTV2InputSource           inInputSource   = NTV2_INPUTSOURCE_SDI1,
                                const uint32_t                  inBitDepth      = 8,
                                const bool                      inIs422         = false,
                                const bool                      inIsAuto        = false,
                                const bool                      inHevcOutput    = false,
                                const SDIInputMode              inSDIInputMode  = SDI_INPUT_MODE_SINGLE_LINK,
                                const NTV2TCIndex               inTimeCode      = NTV2_TCINDEX_SDI1,
                                const bool                      inInfoData      = false,
				const bool                      inCaptureTall   = false,
                                const bool                      inPassthrough   = false);

        virtual AJAStatus InitAudio (const NTV2AudioSource inAudioSource, uint32_t *numAudioChannels);

        /**
            @brief    Gracefully stops me from running.
         **/
        virtual void            Quit (void);

        /**
            @brief    Runs me.
            @note    Do not call this method without first calling my Init method.
        **/
        virtual AJAStatus        Run (void);


        /**
            @brief    Set the callback and callback refcon.
        **/
        virtual void            SetCallback(CallBackType cbType, NTV2Callback callback, void * callbackRefcon);
    
        /**
            @brief    Acquire video buffer (just finds the first free buffer in the pool)
        **/
        virtual AjaVideoBuff*   AcquireVideoBuffer();
    
        /**
            @brief    Acquire video buffer (just finds the first free buffer in the pool)
        **/
        virtual AjaAudioBuff*   AcquireAudioBuffer();

        /**
            @brief    Release video buffer
        **/
        virtual void            ReleaseVideoBuffer(AjaVideoBuff * videoBuffer);
    
        /**
            @brief    Release audio buffer
        **/
        virtual void            ReleaseAudioBuffer(AjaAudioBuff * audioBuffer);

        /**
            @brief    Add a reference to the video buffer
        **/
        virtual void            AddRefVideoBuffer(AjaVideoBuff * videoBuffer);

        /**
            @brief    Add a reference to the audio buffer
        **/
        virtual void            AddRefAudioBuffer(AjaAudioBuff * audioBuffer);

        /**
            @brief    Add a reference to the audio buffer
        **/
        virtual bool            GetHardwareClock(uint64_t desiredTimeScale, uint64_t * time);


        /**
            @brief    Update the currently configured timecode index
        **/
        virtual void            UpdateTimecodeIndex(const NTV2TCIndex inTimeCode);

    
    //    Protected Instance Methods
    protected:
        /**
            @brief    Sets up everything I need for encoding video.
         **/
        virtual AJAStatus        SetupHEVC (void);

        /**
            @brief    Sets up everything I need for capturing video.
        **/
        virtual AJAStatus        SetupVideo (void);

        /**
            @brief    Sets up everything I need for capturing audio.
        **/
        virtual AJAStatus        SetupAudio (void);

        /**
            @brief    Sets/Frees up my circular aja_video_src->ntv2Hevc->buffers.
        **/
        virtual void            SetupHostBuffers (void);
        virtual void            FreeHostBuffers (void);

        /**
            @brief    Initializes AutoCirculate.
        **/
        virtual void            SetupAutoCirculate (void);

        /**
            @brief    Start/Stop the AC input thread.
        **/
        virtual void            StartACThread (void);
        virtual void            StopACThread (void);

        /**
            @brief    Start the codec raw thread.  
        **/
        virtual void            StartCodecRawThread (void);
        virtual void            StopCodecRawThread (void);

        /**
            @brief    Start the codec hevc thread.  
        **/
        virtual void            StartCodecHevcThread (void);
        virtual void            StopCodecHevcThread (void);

        /**
            @brief    Repeatedly captures video frames using AutoCirculate and sends them to the raw
                      audio/video consumers, or the HEVC worker threads
        **/
        virtual void            ACInputWorker (void);

        /**
            @brief    Repeatedly removes video frames from the raw video ring and transfers
                    them to the codec.
        **/
        virtual void            CodecRawWorker (void);

        /**
            @brief    Repeatedly transfers hevc frames from the codec and sends them to the consumer.
        **/
        virtual void            CodecHevcWorker (void);

        //    Protected Class Methods
    protected:
        /**aja_video_src->ntv2Hevc->
            @brief    This is the video input thread's static callback function that gets called when the thread starts.
                    This function gets "Attached" to the AJAThread instance.
            @param[in]    pThread        Points to the AJAThread instance.
            @param[in]    pContext    Context information to pass to the thread.
        **/
        static void                ACInputThreadStatic (AJAThread * pThread, void * pContext);

        /**
            @brief    This is the codec raw thread's static callback function that gets called when the thread starts.
                    This function gets "Attached" to the consumer thread's AJAThread instance.
            @param[in]    pThread        A valid pointer to the consumer thread's AJAThread instance.
            @param[in]    pContext    Context information to pass to the thread.
        **/
        static void                CodecRawThreadStatic (AJAThread * pThread, void * pContext);

        /**
            @brief    This is the codec hevc thread's static callback function that gets called when the thread starts.
                    This function gets "Attached" to the consumer thread's AJAThread instance.
            @param[in]    pThread        A valid pointer to the consumer thread's AJAThread instance.
            @param[in]    pContext    Context information to pass to the thread.
        **/
        static void                CodecHevcThreadStatic (AJAThread * pThread, void * pContext);

    private:
    
        AJAStatus DetermineInputFormat(NTV2Channel inputChannel, bool quad, NTV2VideoFormat& videoFormat);
        AJA_FrameRate GetAJAFrameRate(NTV2FrameRate frameRate);

        bool DoCallback(CallBackType type, void * msg);

    //    Private Member Data
    private:
        AJAThread *                    mACInputThread;         ///    AutoCirculate input thread
        AJAThread *                    mCodecRawThread;        ///    Codec raw transfer
        AJAThread *                    mCodecHevcThread;        ///    Codec hevc transfer thread
        CNTV2m31 *                    mM31;                    /// Object used to interface to m31
        AJALock *                    mLock;                  /// My mutex object

        CNTV2Card                    mDevice;                ///    CNTV2Card instance
        NTV2DeviceID                mDeviceID;                ///    Device identifier
        const std::string            mDeviceSpecifier;        ///    The device specifier string
        bool                        mHevcOutput;            /// Output compressed data
        NTV2Channel                 mInputChannel;            ///    Input channel
        NTV2Channel                 mOutputChannel;            ///    Output channel
        M31Channel                  mEncodeChannel;         /// Encoder channel
        M31VideoPreset                mPreset;                /// M31 HEVC Preset 
        NTV2InputSource                mInputSource;            ///    The input source I'm using
        NTV2VideoFormat                mVideoFormat;            ///    Video format
        NTV2FrameBufferFormat        mPixelFormat;            ///    Pixel format
        uint32_t                    mBitDepth;              /// Bit depth of frame store (8 or 10)
        bool                        mIs422;                    /// Is 422 video otherwise it is 420
        bool                        mIsAuto;                /// Is auto mode (should only be used when called from the audiosrc to set device automatically based on input)
        SDIInputMode                mSDIInputMode;           /// SDI input mode
        bool                        mQuad;
        bool                        mMultiStream;            /// Demonstrates how to configure the board for multi-stream
        bool                        mWithInfo;              /// Demonstrates how to configure picture information mode
        NTV2TCIndex                 mTimecodeMode;        /// Add timecode burn
	bool                        mCaptureTall;	    /// Capture Tall Video
        NTV2InputSource             mVideoSource;
        bool                        mPassthrough;
        NTV2AudioSystem             mAudioSystem;            ///    The audio system I'm using
        NTV2AudioSource             mAudioSource;
        uint32_t                    mNumAudioChannels;
        bool                        mLastFrame;                ///    Set "true" to signal last frame
        bool                        mLastFrameInput;        ///    Set "true" to signal last frame captured from input
        bool                        mLastFrameVideoOut;     ///    Set "true" to signal last frame of video output
        bool                        mLastFrameHevc;            ///    Set "true" to signal last frame transfered from codec
        bool                        mLastFrameHevcOut;      ///    Set "true" to signal last frame of Hevc output
        bool                        mLastFrameAudioOut;        ///    Set "true" to signal last frame of audio output
        bool                        mGlobalQuit;            ///    Set "true" to gracefully stop
        bool                        mStarted;               ///    Set "true" when threads are running

        uint32_t                    mVideoBufferSize;        ///    My video buffer size (bytes)
        uint32_t                    mPicInfoBufferSize;     /// My picture info buffer size (bytes)
        uint32_t                    mEncInfoBufferSize;     /// My encoded info buffer size (bytes)
        uint32_t                    mAudioBufferSize;        ///    My audio buffer size (bytes)

        NTV2Callback               mVideoCallback;         /// Callback for video output
        void *                     mVideoCallbackRefcon;   /// Callback refcon for video output
        NTV2Callback               mAudioCallback;         /// Callback for HEVC output (compressed frames)
        void *                     mAudioCallbackRefcon;   /// Callback refcon for AC output

        AjaVideoBuff                            mHevcInputBuffer [VIDEO_RING_SIZE];           ///    My Hevc input buffers
        AJACircularBuffer <AjaVideoBuff *>      mHevcInputCircularBuffer;                     ///    My Hevc input ring

        GstBufferPool *                         mAudioBufferPool;
        GstBufferPool *                         mVideoBufferPool;


        AUTOCIRCULATE_TRANSFER        mInputTransferStruct;                                    ///    My A/C input transfer info
    
        AJATimeBase                 mTimeBase;                                              /// Timebase for timecode string

};    //    NTV2EncodeHEVC

#endif    //    _NTV2ENCODEHEVC_H
