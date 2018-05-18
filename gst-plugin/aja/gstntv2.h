/**
	@file		ntv2capture.h
	@brief		Declares the NTV2Capture class.
	@copyright	Copyright (C) 2012-2015 AJA Video Systems, Inc.  All rights reserved.
**/


#ifndef _NTV2ENCODEHEVC_H
#define _NTV2ENCODEHEVC_H

#include <gst/gst.h>
#include "ntv2enums.h"
#include "ntv2m31enums.h"

#include "ntv2devicescanner.h"
#include "ntv2rp188.h"

#include "ajabase/common/videotypes.h"
#include "ajabase/common/circularbuffer.h"
#include "ajabase/common/timebase.h"
#include "ajabase/system/thread.h"

#include "ntv2m31.h"

#define VIDEO_RING_SIZE			16
#define VIDEO_ARRAY_SIZE        60
#define AUDIO_RING_SIZE			(3*VIDEO_RING_SIZE)
#define AUDIO_ARRAY_SIZE        (3*VIDEO_ARRAY_SIZE)

#define ASECOND                 (1000000000)

typedef int64_t (*NTV2Callback) (int64_t refcon, int64_t msg);

typedef enum
{
    VIDEO_CALLBACK,
    AUDIO_CALLBACK
} CallBackType;

typedef struct
{
    uint32_t        bufferId;               /// Unique buffer number to identify buffer when it come back to us to be freed
    uint32_t        bufferRef;              /// Buffer use counter
    uint32_t        fameNumber;             /// Frame number
	uint32_t *		pVideoBuffer;			///	Pointer to host video buffer
    uint32_t		videoBufferSize;		///	Size of host video buffer (bytes)
    uint32_t		videoDataSize;			///	Size of video data (bytes)
    uint32_t *      pInfoBuffer;            /// Picture information (raw) or encode information (hevc)
    uint32_t        infoBufferSize;         /// Size of the host information buffer (bytes)
    uint32_t        infoDataSize;           /// Size of the information data (bytes)
    uint32_t        timeCodeDBB;            /// Time code data dbb
    uint32_t        timeCodeLow;            /// Time code data low
    uint32_t        timeCodeHigh;           /// Time code data high
    uint64_t        timeStamp;              /// Time stamp of video data
    uint64_t        timeDuration;           /// Time duration
    bool			lastFrame;              /// Indicates last captured frame
} AjaVideoBuff;


typedef struct
{
    uint32_t        bufferId;               /// Unique buffer number to identify buffer when it come back to us to be freed
    uint32_t        bufferRef;              /// Buffer use counter
	uint32_t *		pAudioBuffer;			///	Pointer to host audio buffer
    uint32_t		audioBufferSize;		///	Size of host audio buffer (bytes)
    uint32_t		audioDataSize;			///	Size of audio data (bytes)
    uint64_t        timeStamp;              /// Time stamp of video data
    uint64_t        timeDuration;           /// Time Duration
	bool			lastFrame;				/// Indicates last captured frame
} AjaAudioBuff;

		

/**
	@brief	Instances of me capture frames in real time from a video signal provided to an input of
			an AJA device.
**/

class NTV2GstAVHevc
{
	//	Public Instance Methods
	public:
		/**
			@brief	Constructs me using the given settings.
			@note	I'm not completely initialized and ready to use until after my Init method has been called.
			@param[in]	inDeviceSpecifier	Specifies the AJA device to use.
											Defaults to "0" (first device found).
			@param[in]	inChannel			Specifies the channel to use.
											Defaults to NTV2_CHANNEL1.
			@param[in]	inM31Preset			Specifies the m31 preset to use.
											Defaults to 8-bit 1280x720 5994p.
            @param[in]	inPixelFormat		Specifies the pixel format to use.
                                          aja_video_src->ntv2Hevc->  Defaults to NTV2_FBF_10BIT_YCBCR_420PL.
            @param[in]	inQuadMode  		Specifies UHD mode.
                                            Defaults to HD mode.
            @param[in]	inAudioChannels		Specifies number of audio channels to write to AIFF file.
                                            Defaults to 2 channels.
            @param[in]	inTimeCodeBurn      Add timecode burn.
                                            Defaults to no timecode burn.
            @param[in]	inInfoData          Use picture and encoded information.
                                            Defaults to no info data.
        **/
		NTV2GstAVHevc(const std::string			inDeviceSpecifier	= "0",
                      const NTV2Channel			inChannel			= NTV2_CHANNEL1);

        virtual     ~NTV2GstAVHevc ();
    
        /**
         @brief	Open/Close, discover and allocate/release an NTV2 device.
         **/
        virtual AJAStatus		Open (void);
        virtual AJAStatus		Close (void);

		/**
			@brief	Initializes me and prepares me to Run.
		**/
        virtual AJAStatus Init (const M31VideoPreset            inPreset        = M31_FILE_720X480_420_8_5994i,
                                const NTV2VideoFormat           inVideoFormat   = NTV2_FORMAT_525_5994,
                                const uint32_t                  inBitDepth      = 8,
                                const bool                      inIs422         = false,
                                const bool                      inIsAuto        = false,
                                const bool                      inHevcOutput    = false,
                                const bool                      inQuadMode      = false,
                                const bool                      inTimeCode      = false,
                                const bool                      inInfoData      = false);

        /**
            @brief	Gracefully stops me from running.
         **/
        virtual void			Quit (void);

		/**
			@brief	Runs me.
			@note	Do not call this method without first calling my Init method.
		**/
		virtual AJAStatus		Run (void);


        /**
            @brief	Set the callback and callback refcon.
        **/
        virtual void            SetCallback(CallBackType cbType, int64_t callback, int64_t callbackRefcon);
    
        /**
            @brief	Acquire video buffer (just finds the first free buffer in the pool)
        **/
        virtual AjaVideoBuff*   AcquireVideoBuffer();
    
        /**
            @brief	Acquire video buffer (just finds the first free buffer in the pool)
        **/
        virtual AjaAudioBuff*   AcquireAudioBuffer();

        /**
            @brief	Release video buffer
        **/
        virtual void            ReleaseVideoBuffer(AjaVideoBuff * videoBuffer);
    
        /**
            @brief	Release audio buffer
        **/
        virtual void            ReleaseAudioBuffer(AjaAudioBuff * audioBuffer);

        /**
            @brief	Add a reference to the video buffer
        **/
        virtual void            AddRefVideoBuffer(AjaVideoBuff * videoBuffer);

        /**
            @brief	Add a reference to the audio buffer
        **/
        virtual void            AddRefAudioBuffer(AjaAudioBuff * audioBuffer);

        /**
            @brief	Add a reference to the audio buffer
        **/
        virtual bool            GetHardwareClock(uint64_t desiredTimeScale, uint64_t * time);

    
	//	Protected Instance Methods
	protected:
        /**
            @brief	Sets up everything I need for encoding video.
         **/
        virtual AJAStatus		SetupHEVC (void);

		/**
			@brief	Sets up everything I need for capturing video.
		**/
		virtual AJAStatus		SetupVideo (void);

		/**
			@brief	Sets up everything I need for capturing audio.
		**/
		virtual AJAStatus		SetupAudio (void);

		/**
			@brief	Sets up device routing for capture.
		**/
		virtual void			RouteInputSignal (void);

		/**
            @brief	Sets/Frees up my circular aja_video_src->ntv2Hevc->buffers.
		**/
        virtual void			SetupHostBuffers (void);
        virtual void			FreeHostBuffers (void);

		/**
			@brief	Initializes AutoCirculate.
		**/
        virtual void			SetupAutoCirculate (void);

		/**
			@brief	Start/Stop the AC input thread.
		**/
        virtual void			StartACThread (void);
        virtual void			StopACThread (void);

		/**
			@brief	Start the video output thread.
		**/
        virtual void			StartVideoOutputThread (void);
        virtual void			StopVideoOutputThread (void);

		/**
			@brief	Start the codec raw thread.  
		**/
        virtual void			StartCodecRawThread (void);
        virtual void			StopCodecRawThread (void);

		/**
			@brief	Start the codec hevc thread.  
		**/
        virtual void			StartCodecHevcThread (void);
        virtual void			StopCodecHevcThread (void);

		/**
            @brief	Start the hevc output thread.
		**/
        virtual void			StartHevcOutputThread (void);
        virtual void			StopHevcOutputThread (void);

        /**
            @brief	Start the audio output thread.
        **/
        virtual void			StartAudioOutputThread (void);
        virtual void			StopAudioOutputThread (void);

        /**
            @brief	Repeatedly captures video frames using AutoCirculate and add them to
					the video input ring.
        **/
        virtual void			ACInputWorker (void);

        /**
            @brief	Repeatedly removes video frames from the video input ring, calls a
					custom video process method and adds the result to the raw video ring.
        **/
        virtual void			VideoOutputWorker (void);

        /**
            @brief	Repeatedly removes video frames from the raw video ring and transfers
					them to the codec.
        **/
        virtual void			CodecRawWorker (void);

        /**
            @brief	Repeatedly transfers hevc frames from the codec and adds them to the
					hevc ring.
        **/
        virtual void			CodecHevcWorker (void);

        /**
            @brief	Repeatedly removes hevc frame from the hevc ring and writes them to the
					hevc output file.
        **/
        virtual void			HevcOutputWorker (void);

        /**
            @brief	Repeatedly removes audio samples from the audio input ring and writes them to the
                    audio output file.
        **/
        virtual void			AudioOutputWorker (void);

        //	Protected Class Methods
	protected:
        /**aja_video_src->ntv2Hevc->
			@brief	This is the video input thread's static callback function that gets called when the thread starts.
					This function gets "Attached" to the AJAThread instance.
			@param[in]	pThread		Points to the AJAThread instance.
			@param[in]	pContext	Context information to pass to the thread.
		**/
        static void				ACInputThreadStatic (AJAThread * pThread, void * pContext);

		/**
			@brief	This is the video process thread's static callback function that gets called when the thread starts.
					This function gets "Attached" to the consumer thread's AJAThread instance.
			@param[in]	pThread		A valid pointer to the consumer thread's AJAThread instance.
			@param[in]	pContext	Context information to pass to the thread.
		**/
        static void				VideoOutputThreadStatic (AJAThread * pThread, void * pContext);

		/**
			@brief	This is the codec raw thread's static callback function that gets called when the thread starts.
					This function gets "Attached" to the consumer thread's AJAThread instance.
			@param[in]	pThread		A valid pointer to the consumer thread's AJAThread instance.
			@param[in]	pContext	Context information to pass to the thread.
		**/
        static void				CodecRawThreadStatic (AJAThread * pThread, void * pContext);

		/**
			@brief	This is the codec hevc thread's static callback function that gets called when the thread starts.
					This function gets "Attached" to the consumer thread's AJAThread instance.
			@param[in]	pThread		A valid pointer to the consumer thread's AJAThread instance.
			@param[in]	pContext	Context information to pass to the thread.
		**/
        static void				CodecHevcThreadStatic (AJAThread * pThread, void * pContext);

		/**
            @brief	This is the video file writer thread's static callback function that gets called when the thread starts.
					This function gets "Attached" to the consumer thread's AJAThread instance.
			@param[in]	pThread		A valid pointer to the consumer thread's AJAThread instance.
			@param[in]	pContext	Context information to pass to the thread.
		**/
        static void				HevcOutputThreadStatic (AJAThread * pThread, void * pContext);
        /**
            @brief	This is the audio file writer thread's static callback function that gets called when the thread starts.
                    This function gets "Attached" to the consumer thread's AJAThread instance.
            @param[in]	pThread		A valid pointer to the consumer thread's AJAThread instance.
            @param[in]	pContext	Context information to pass to the thread.
        **/
        static void				AudioOutputThreadStatic (AJAThread * pThread, void * pContext);

    private:
	
        AJAStatus DetermineInputFormat(NTV2Channel inputChannel, bool quad, NTV2VideoFormat& videoFormat);
        AJA_FrameRate GetAJAFrameRate(NTV2FrameRate frameRate);

        bool DoCallback(CallBackType type, int64_t msg);

    //	Private Member Data
	private:
        AJAThread *					mACInputThread;         ///	AutoCirculate input thread
        AJAThread *					mVideoOutputThread;     ///	Video output thread
        AJAThread *					mCodecRawThread;		///	Codec raw transfer
        AJAThread *					mCodecHevcThread;		///	Codec hevc transfer thread
        AJAThread *					mHevcOutputThread;		///	Hevc output thread
        AJAThread *					mAudioOutputThread;		///	Audio output thread
        CNTV2m31 *					mM31;					/// Object used to interface to m31
        AJALock *					mLock;                  /// My mutex object

        CNTV2Card					mDevice;				///	CNTV2Card instance
        NTV2DeviceID				mDeviceID;				///	Device identifier
		const std::string			mDeviceSpecifier;		///	The device specifier string
        bool                        mHevcOutput;            /// Output compressed data
        NTV2Channel                 mInputChannel;			///	Input channel
        NTV2Channel                 mOutputChannel;			///	Output channel
        M31Channel                  mEncodeChannel;         /// Encoder channel
		M31VideoPreset				mPreset;				/// M31 HEVC Preset 
		NTV2InputSource				mInputSource;			///	The input source I'm using
        NTV2VideoFormat				mVideoFormat;			///	Video format
        NTV2FrameBufferFormat		mPixelFormat;			///	Pixel format
        uint32_t                    mBitDepth;              /// Bit depth of frame store (8 or 10)
        bool						mIs422;					/// Is 422 video otherwise it is 420
        bool						mIsAuto;                /// Is auto mode (should only be used when called from the audiosrc to set device automatically based on input)
        bool						mQuad;					/// VideoFormat is quad
        bool						mMultiStream;			/// Demonstrates how to configure the board for multi-stream
        bool                        mWithInfo;              /// Demonstrates how to configure picture information mode
        bool                        mWithAnc;               /// Add timecode burn
		NTV2AudioSystem				mAudioSystem;			///	The audio system I'm using
        uint32_t                    mNumAudioChannels;      /// Number of input audio channels

		bool						mLastFrame;				///	Set "true" to signal last frame
		bool						mLastFrameInput;		///	Set "true" to signal last frame captured from input
		bool						mLastFrameVideoOut;     ///	Set "true" to signal last frame of video output
		bool						mLastFrameHevc;			///	Set "true" to signal last frame transfered from codec
        bool						mLastFrameHevcOut;      ///	Set "true" to signal last frame of Hevc output
        bool						mLastFrameAudioOut;		///	Set "true" to signal last frame of audio output
        bool						mGlobalQuit;			///	Set "true" to gracefully stop
        bool						mStarted;               ///	Set "true" when threads are running

		uint32_t					mQueueSize;				///	My queue size
        uint32_t					mVideoBufferSize;		///	My video buffer size (bytes)
        uint32_t                    mPicInfoBufferSize;     /// My picture info buffer size (bytes)
        uint32_t                    mEncInfoBufferSize;     /// My encoded info buffer size (bytes)
        uint32_t					mAudioBufferSize;		///	My audio buffer size (bytes)

        int64_t                     mVideoCallback;         /// Callback for video output
        int64_t                     mVideoCallbackRefcon;   /// Callback refcon for video output
        int64_t                     mAudioCallback;         /// Callback for HEVC output (compressed frames)
        int64_t                     mAudioCallbackRefcon;   /// Callback refcon for AC output

        AjaVideoBuff							mACInputBuffer [VIDEO_RING_SIZE];           ///	My AC input buffers
        AJACircularBuffer <AjaVideoBuff *>      mACInputCircularBuffer;                     ///	My AC input ring

        AjaVideoBuff							mVideoHevcBuffer [VIDEO_RING_SIZE];			///	My video hevc buffers
        AJACircularBuffer <AjaVideoBuff *>      mVideoHevcCircularBuffer;					///	My video hevc ring

        AjaAudioBuff							mAudioInputBuffer [AUDIO_RING_SIZE];		///	My audio input buffers
        AJACircularBuffer <AjaAudioBuff *>      mAudioInputCircularBuffer;					///	My audio input ring
    
        AjaVideoBuff							mVideoOutBuffer [VIDEO_ARRAY_SIZE];         ///	Video out buffers passed using callback
        AjaAudioBuff							mAudioOutBuffer [AUDIO_ARRAY_SIZE];         ///	Audio out buffers passed using callback


        AUTOCIRCULATE_TRANSFER		mInputTransferStruct;						            ///	My A/C input transfer info
	
        uint32_t					mVideoInputFrameCount;                                  /// Input thread frame counter
        uint32_t					mVideoOutFrameCount;                                    /// Video output frame counter
        uint32_t					mCodecRawFrameCount;                                    /// Raw thread frame counter
        uint32_t					mCodecHevcFrameCount;                                   /// HEVC thread frame counter
        uint32_t					mHevcOutFrameCount;                                     /// Hevc output frame counter
        uint32_t					mAudioOutFrameCount;                                    /// Audio output frame counter

        AJATimeBase                 mTimeBase;                                              /// Timebase for timecode string

};	//	NTV2EncodeHEVC

#endif	//	_NTV2ENCODEHEVC_H
