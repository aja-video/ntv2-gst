/**
	@file		ntv2encodehevc.cpp
	@brief		Implementation of NTV2EncodeHEVC class.
	@copyright	Copyright (C) 2015 AJA Video Systems, Inc.  All rights reserved.
**/

#include <stdio.h>

#include "gstntv2.h"
#include "ntv2utils.h"
#include "ntv2devicefeatures.h"
#include "ajabase/system/process.h"
#include "ajabase/system/systemtime.h"

#define NTV2_AUDIOSIZE_MAX		(401 * 1024)


NTV2GstAVHevc::NTV2GstAVHevc (const string inDeviceSpecifier, const NTV2Channel inChannel)

:	mACInputThread          (NULL),
	mVideoOutputThread		(NULL),
	mCodecRawThread			(NULL),
	mCodecHevcThread		(NULL),
    mHevcOutputThread 		(NULL),
    mAudioOutputThread 		(NULL),
    mM31					(NULL),
    mLock                   (new AJALock),
	mDeviceID				(DEVICE_ID_NOTFOUND),
    mDeviceSpecifier		(inDeviceSpecifier),
    mInputChannel			(inChannel),
    mEncodeChannel          (M31_CH0),
	mInputSource			(NTV2_INPUTSOURCE_SDI1),
    mVideoFormat			(NTV2_MAX_NUM_VIDEO_FORMATS),
    mMultiStream			(false),
	mAudioSystem			(NTV2_AUDIOSYSTEM_1),
    mNumAudioChannels       (0),
	mLastFrame				(false),
	mLastFrameInput			(false),
	mLastFrameVideoOut      (false),
	mLastFrameHevc			(false),
    mLastFrameHevcOut       (false),
    mLastFrameAudioOut      (false),
    mGlobalQuit				(false),
    mStarted                (false),
    mVideoCallback          (0),
    mVideoCallbackRefcon    (0),
    mAudioCallback          (0),
    mAudioCallbackRefcon    (0),
	mVideoInputFrameCount	(0),		
	mVideoOutFrameCount     (0),
	mCodecRawFrameCount		(0),
	mCodecHevcFrameCount	(0),
    mHevcOutFrameCount      (0),
    mAudioOutFrameCount 	(0)

{
    ::memset (mACInputBuffer,       0x0, sizeof (mACInputBuffer));
    ::memset (mVideoHevcBuffer,     0x0, sizeof (mVideoHevcBuffer));
    ::memset (mAudioInputBuffer,    0x0, sizeof (mAudioInputBuffer));

}	//	constructor


NTV2GstAVHevc::~NTV2GstAVHevc ()
{
	//	Stop my capture and consumer threads, then destroy them...
	Quit ();

    if (mM31 != NULL)
	{
		delete mM31;
		mM31 = NULL;
	}
	
	// unsubscribe from input vertical event...
	mDevice.UnsubscribeInputVerticalEvent (mInputChannel);
    
    FreeHostBuffers();

    delete mLock;
    mLock = NULL;
    

} // destructor


AJAStatus NTV2GstAVHevc::Open (void)
{
    //	Open the device...
	if (!CNTV2DeviceScanner::GetFirstDeviceFromArgument (mDeviceSpecifier, mDevice))
    {
        GST_ERROR ("ERROR: Device not found");
        return AJA_STATUS_OPEN;
    }
    
	mDevice.SetEveryFrameServices (NTV2_OEM_TASKS);			//	Since this is an OEM app, use the OEM service level
	mDeviceID = mDevice.GetDeviceID ();						//	Keep the device ID handy, as it's used frequently

    return AJA_STATUS_SUCCESS;
}


AJAStatus NTV2GstAVHevc::Close (void)
{
    AJAStatus	status	(AJA_STATUS_SUCCESS);

    return status;
}


AJAStatus NTV2GstAVHevc::Init (const M31VideoPreset         inPreset,
                               const NTV2VideoFormat        inVideoFormat,
                               const uint32_t               inBitDepth,
                               const bool                   inIs422,
                               const bool                   inIsAuto,
                               const bool                   inHevcOutput,
                               const bool                   inQuadMode,
                               const bool                   inTimeCode,
                               const bool                   inInfoData)
{
    AJAStatus	status	(AJA_STATUS_SUCCESS);

    mPreset			= inPreset;
    mVideoFormat    = inVideoFormat;
    mBitDepth       = inBitDepth;
    mIs422          = inIs422;
    mIsAuto         = inIsAuto;
    mHevcOutput     = inHevcOutput;
    mQuad           = inQuadMode;
    mWithAnc        = inTimeCode;
    mWithInfo       = inInfoData;
    
    //  If we are in auto mode then do nothing if we are already running, otherwise force raw, 422, 8 bit.
    //  This flag shoud only be driven by the audiosrc to either start a non running channel without having
    //  to know anything about the video or to latch onto an alreay running channel in the event it has been
    //  started by the videosrc or hevcsrc.
    if (mIsAuto)
    {
        if (mStarted)
            return AJA_STATUS_SUCCESS;
        
        //  Get SDI input format
        status = DetermineInputFormat(mInputChannel, mQuad, mVideoFormat);
        if (AJA_FAILURE(status))
            return status;
        
        mBitDepth = 8;
        mHevcOutput = false;
    }
    
    // Figure out frame buffer format
    if (mHevcOutput)
    {
        if (mBitDepth == 8)
        {
            if (mIs422)
                mPixelFormat = NTV2_FBF_8BIT_YCBCR_422PL;
            else
                mPixelFormat = NTV2_FBF_8BIT_YCBCR_420PL;
        }
        else
        {
            if (mIs422)
                mPixelFormat = NTV2_FBF_10BIT_YCBCR_422PL;
            else
                mPixelFormat = NTV2_FBF_10BIT_YCBCR_420PL;
        }
    }
    else
    {
        if (mBitDepth == 8)
            mPixelFormat = NTV2_FBF_8BIT_YCBCR;
        else
            mPixelFormat = NTV2_FBF_10BIT_YCBCR;
    }

    //  Quad mode must be channel 1
    if (mQuad)
    {
        mInputChannel = NTV2_CHANNEL1;
        mOutputChannel = NTV2_CHANNEL5;
        mEncodeChannel = M31_CH0;
    }
    else
    {
        //  When input channel specified we are multistream
        switch (mInputChannel)
        {
            case NTV2_CHANNEL1: { mEncodeChannel = M31_CH0; mOutputChannel = NTV2_CHANNEL5; mMultiStream = true; break; }
            case NTV2_CHANNEL2: { mEncodeChannel = M31_CH1; mOutputChannel = NTV2_CHANNEL6; mMultiStream = true; break; }
            case NTV2_CHANNEL3: { mEncodeChannel = M31_CH2; mOutputChannel = NTV2_CHANNEL7; mMultiStream = true; break; }
            case NTV2_CHANNEL4: { mEncodeChannel = M31_CH3; mOutputChannel = NTV2_CHANNEL8; mMultiStream = true; break; }
            default: { mInputChannel = NTV2_CHANNEL1; mOutputChannel = NTV2_CHANNEL5; mEncodeChannel = M31_CH0; }
        }
    }

    //	Setup frame buffer
	status = SetupVideo ();
	if (AJA_FAILURE (status))
    {
        GST_ERROR ("Video setup failure");
		return status;
    }

    //	Route input signals to frame buffers
	RouteInputSignal ();
    
	//	Setup audio buffer
	status = SetupAudio ();
	if (AJA_FAILURE (status))
    {
        GST_ERROR ("Audio setup failure");
		return status;
    }
    
	//	Setup to capture video/audio/anc input
    SetupAutoCirculate ();
    
	//	Setup codec
    if (mHevcOutput)
    {
        status = SetupHEVC ();
        if (AJA_FAILURE (status))
        {
            GST_ERROR ("Encoder setup failure");
            return status;
        }
    }
    
	//	Setup the circular buffers
	SetupHostBuffers ();
    
    return AJA_STATUS_SUCCESS;
}


void NTV2GstAVHevc::Quit (void)
{
    if (!mLastFrame && !mGlobalQuit)
	{
		//	Set the last frame flag to start the quit process
		mLastFrame = true;

		//	Wait for the last frame to be written to disk
		int i;
		int timeout = 300;
		for (i = 0; i < timeout; i++)
		{
            if (mHevcOutput)
            {
                if (mLastFrameHevcOut && mLastFrameAudioOut) break;
            }
            else
            {
                if (mLastFrameVideoOut && mLastFrameAudioOut) break;
            }
			AJATime::Sleep (10);
		}

		if (i == timeout)
            GST_ERROR ("ERROR: Wait for last frame timeout");

        if (mM31 && mHevcOutput)
        {
            //	Stop the encoder stream
            if (!mM31->ChangeEHState(Hevc_EhState_ReadyToStop, mEncodeChannel))
                GST_ERROR ("ERROR: ChangeEHState ready to stop failed");

            if (!mM31->ChangeEHState(Hevc_EhState_Stop, mEncodeChannel))
                GST_ERROR ("ERROR: ChangeEHState stop failed");

            // stop the video input stream
            if (!mM31->ChangeVInState(Hevc_VinState_Stop, mEncodeChannel))
                GST_ERROR ("ERROR: ChangeVInState stop failed");

            if(!mMultiStream)
            {
                //	Now go to the init state
                if (!mM31->ChangeMainState(Hevc_MainState_Init, Hevc_EncodeMode_Single))
                    GST_ERROR ("ERROR: ChangeMainState to init failed");
            }
        }
	}

	//	Stop the worker threads
	mGlobalQuit = true;
    mStarted = false;

    StopACThread();
    StopVideoOutputThread();
    StopCodecRawThread();
    StopCodecHevcThread();
    StopAudioOutputThread();

    //  Stop video capture
    mDevice.SetMode(mInputChannel, NTV2_MODE_DISPLAY, false);
}


AJAStatus NTV2GstAVHevc::SetupHEVC (void)
{
    HevcMainState   mainState;
    HevcEncodeMode  encodeMode;
    HevcVinState    vInState;
    HevcEhState     ehState;

    // Allocate our M31 helper class
    mM31 = new CNTV2m31 (&mDevice);

    if (mMultiStream)
    {
        mM31->GetMainState(&mainState, &encodeMode);
        if ((mainState != Hevc_MainState_Encode) || (encodeMode != Hevc_EncodeMode_Multiple))
        {
            // Here we need to start up the M31 so we reset the part then go into the init state
            if (!mM31->Reset())
                { GST_ERROR ("ERROR: Reset of M31 failed"); return AJA_STATUS_INITIALIZE; }

            // After a reset we should be in the boot state so lets check this
            mM31->GetMainState(&mainState);
            if (mainState != Hevc_MainState_Boot)
                { GST_ERROR ("ERROR: Not in boot state after reset"); return AJA_STATUS_INITIALIZE; }

            // Now go to the init state
            if (!mM31->ChangeMainState(Hevc_MainState_Init, Hevc_EncodeMode_Multiple))
                { GST_ERROR ("ERROR: ChangeMainState to init failed"); return AJA_STATUS_INITIALIZE; }

            mM31->GetMainState(&mainState);
            if (mainState != Hevc_MainState_Init)
                { GST_ERROR ("ERROR: Not in init state after change"); return AJA_STATUS_INITIALIZE; }

            // Now lets configure the device for a given preset.  First we must clear out all of the params which
            // is necessary since the param space is basically uninitialized memory.
            mM31->ClearAllParams();

            // Load and set common params for all channels
            if (!mM31->SetupCommonParams(mPreset, M31_CH0))
                { GST_ERROR ("ERROR: SetCommonParams failed ch0"); return AJA_STATUS_INITIALIZE; }

            // Change state to encode
            if (!mM31->ChangeMainState(Hevc_MainState_Encode, Hevc_EncodeMode_Multiple))
                { GST_ERROR ("ERROR: ChangeMainState to encode failed"); return AJA_STATUS_INITIALIZE; }

            mM31->GetMainState(&mainState);
            if (mainState != Hevc_MainState_Encode)
                { GST_ERROR ("ERROR: Not in encode state after change"); return AJA_STATUS_INITIALIZE; }
        }

        // Write out stream params
        if (!mM31->SetupVIParams(mPreset, mEncodeChannel))
            { GST_ERROR ("ERROR: SetupVIParams failed"); return AJA_STATUS_INITIALIZE; }
        if (!mM31->SetupVInParams(mPreset, mEncodeChannel))
            { GST_ERROR ("ERROR: SetupVinParams failed"); return AJA_STATUS_INITIALIZE; }
        if (!mM31->SetupVAParams(mPreset, mEncodeChannel))
            { GST_ERROR ("ERROR: SetupVAParams failed"); return AJA_STATUS_INITIALIZE; }
        if (!mM31->SetupEHParams(mPreset, mEncodeChannel))
            { GST_ERROR ("ERROR: SetupEHParams failed"); return AJA_STATUS_INITIALIZE; }

        if (mWithInfo)
        {
            // Enable picture information
            if (!mM31->mpM31VInParam->SetPTSMode(M31_PTSModeHost, (M31VirtualChannel)mEncodeChannel))
            { GST_ERROR ("ERROR: SetPTSMode failed"); return AJA_STATUS_INITIALIZE; }
        }

        // Now that we have setup the M31 lets change the VIn and EH states for channel 0 to start
        if (!mM31->ChangeVInState(Hevc_VinState_Start, mEncodeChannel))
            { GST_ERROR ("ERROR: ChangeVInState failed"); return AJA_STATUS_INITIALIZE; }

        mM31->GetVInState(&vInState, mEncodeChannel);
        if (vInState != Hevc_VinState_Start)
        { GST_ERROR ("ERROR: VIn didn't start = %d", vInState); return AJA_STATUS_INITIALIZE; }

        if (!mM31->ChangeEHState(Hevc_EhState_Start, mEncodeChannel))
            { GST_ERROR ("ERROR: ChangeEHState failed"); return AJA_STATUS_INITIALIZE; }

        mM31->GetEHState(&ehState, mEncodeChannel);
        if (ehState != Hevc_EhState_Start)
        { GST_ERROR ("ERROR: EH didn't start = %d", ehState); return AJA_STATUS_INITIALIZE; }
    }
    else
    {
        // if we are in the init state assume that last stop was good
        // otherwise reset the codec
        mM31->GetMainState(&mainState, &encodeMode);
        if ((mainState != Hevc_MainState_Init) || (encodeMode != Hevc_EncodeMode_Single))
        {
            // Here we need to start up the M31 so we reset the part then go into the init state
            if (!mM31->Reset())
                { GST_ERROR ("ERROR: Reset of M31 failed"); return AJA_STATUS_INITIALIZE; }

            // After a reset we should be in the boot state so lets check this
            mM31->GetMainState(&mainState);
            if (mainState != Hevc_MainState_Boot)
                { GST_ERROR ("ERROR: Not in boot state after reset"); return AJA_STATUS_INITIALIZE; }

            // Now go to the init state
            if (!mM31->ChangeMainState(Hevc_MainState_Init, Hevc_EncodeMode_Single))
                { GST_ERROR ("ERROR: ChangeMainState to init failed"); return AJA_STATUS_INITIALIZE; }

            mM31->GetMainState(&mainState);
            if (mainState != Hevc_MainState_Init)
                { GST_ERROR ("ERROR: Not in init state after change"); return AJA_STATUS_INITIALIZE; }
        }

        // Now lets configure the device for a given preset.  First we must clear out all of the params which
        // is necessary since the param space is basically uninitialized memory.
        mM31->ClearAllParams();

        // Now load params for M31 preset into local structures in CNTV2m31
        if (!mM31->LoadAllParams(mPreset))
            { GST_ERROR ("ERROR: LoadAllPresets failed"); return AJA_STATUS_INITIALIZE; }

        // Here is where you can alter params sent to the M31 because all of these structures are public

        // Write out all of the params to each of the 4 physical channels
        if (!mM31->SetAllParams(M31_CH0))
            { GST_ERROR ("ERROR: SetVideoPreset failed ch0"); return AJA_STATUS_INITIALIZE; }

        if (!mM31->SetAllParams(M31_CH1))
            { GST_ERROR ("ERROR: SetVideoPreset failed ch1"); return AJA_STATUS_INITIALIZE; }

        if (!mM31->SetAllParams(M31_CH2))
            { GST_ERROR ("ERROR: SetVideoPreset failed ch2"); return AJA_STATUS_INITIALIZE; }

        if (!mM31->SetAllParams(M31_CH3))
            { GST_ERROR ("ERROR: SetVideoPreset failed ch3"); return AJA_STATUS_INITIALIZE; }

        if (mWithInfo)
        {
            // Enable picture information
            if (!mM31->mpM31VInParam->SetPTSMode(M31_PTSModeHost, (M31VirtualChannel)M31_CH0))
            { GST_ERROR ("ERROR: SetPTSMode failed"); return AJA_STATUS_INITIALIZE; }
        }

        // Change state to encode
        if (!mM31->ChangeMainState(Hevc_MainState_Encode, Hevc_EncodeMode_Single))
            { GST_ERROR ("ERROR: ChangeMainState to encode failed"); return AJA_STATUS_INITIALIZE; }

        mM31->GetMainState(&mainState);
        if (mainState != Hevc_MainState_Encode)
            { GST_ERROR ("ERROR: Not in encode state after change"); return AJA_STATUS_INITIALIZE; }

        // Now that we have setup the M31 lets change the VIn and EH states for channel 0 to start
        if (!mM31->ChangeVInState(Hevc_VinState_Start, 0x01))
            { GST_ERROR ("ERROR: ChangeVInState failed"); return AJA_STATUS_INITIALIZE; }

        mM31->GetVInState(&vInState, M31_CH0);
        if (vInState != Hevc_VinState_Start)
            { GST_ERROR ("ERROR: VIn didn't start = %d", vInState); return AJA_STATUS_INITIALIZE; }

        if (!mM31->ChangeEHState(Hevc_EhState_Start, 0x01))
            { GST_ERROR ("ERROR: ChangeEHState failed"); return AJA_STATUS_INITIALIZE; }

        mM31->GetEHState(&ehState, M31_CH0);
        if (ehState != Hevc_EhState_Start)
            { GST_ERROR ("ERROR: EH didn't start = %d", ehState); return AJA_STATUS_INITIALIZE; }
    }

	return AJA_STATUS_SUCCESS;
}
    
    
AJAStatus NTV2GstAVHevc::SetupVideo (void)
{
	//	Setup frame buffer
	if (mQuad)
	{
		if (mInputChannel != NTV2_CHANNEL1)
			return AJA_STATUS_FAIL;

		//	Disable multiformat
		if (::NTV2DeviceCanDoMultiFormat (mDeviceID))
			mDevice.SetMultiFormatMode (false);

		//	Set the board video format
		mDevice.SetVideoFormat (mVideoFormat, false, false, NTV2_CHANNEL1);

		//	Set frame buffer format
		mDevice.SetFrameBufferFormat (NTV2_CHANNEL1, mPixelFormat);
		mDevice.SetFrameBufferFormat (NTV2_CHANNEL2, mPixelFormat);
		mDevice.SetFrameBufferFormat (NTV2_CHANNEL3, mPixelFormat);
		mDevice.SetFrameBufferFormat (NTV2_CHANNEL4, mPixelFormat);
        mDevice.SetFrameBufferFormat (NTV2_CHANNEL5, mPixelFormat);
        mDevice.SetFrameBufferFormat (NTV2_CHANNEL6, mPixelFormat);
        mDevice.SetFrameBufferFormat (NTV2_CHANNEL7, mPixelFormat);
        mDevice.SetFrameBufferFormat (NTV2_CHANNEL8, mPixelFormat);

		//	Set catpure mode
		mDevice.SetMode (NTV2_CHANNEL1, NTV2_MODE_CAPTURE, false);
		mDevice.SetMode (NTV2_CHANNEL2, NTV2_MODE_CAPTURE, false);
		mDevice.SetMode (NTV2_CHANNEL3, NTV2_MODE_CAPTURE, false);
		mDevice.SetMode (NTV2_CHANNEL4, NTV2_MODE_CAPTURE, false);
        mDevice.SetMode (NTV2_CHANNEL5, NTV2_MODE_DISPLAY, false);
        mDevice.SetMode (NTV2_CHANNEL6, NTV2_MODE_DISPLAY, false);
        mDevice.SetMode (NTV2_CHANNEL7, NTV2_MODE_DISPLAY, false);
        mDevice.SetMode (NTV2_CHANNEL8, NTV2_MODE_DISPLAY, false);

		//	Enable frame buffers
		mDevice.EnableChannel (NTV2_CHANNEL1);
		mDevice.EnableChannel (NTV2_CHANNEL2);
		mDevice.EnableChannel (NTV2_CHANNEL3);
		mDevice.EnableChannel (NTV2_CHANNEL4);
        mDevice.EnableChannel (NTV2_CHANNEL5);
        mDevice.EnableChannel (NTV2_CHANNEL6);
        mDevice.EnableChannel (NTV2_CHANNEL7);
        mDevice.EnableChannel (NTV2_CHANNEL8);

		//	Save input source
		mInputSource = ::NTV2ChannelToInputSource (NTV2_CHANNEL1);
	}
    else if (mMultiStream)
	{
		//	Configure for multiformat
		if (::NTV2DeviceCanDoMultiFormat (mDeviceID))
			mDevice.SetMultiFormatMode (true);

		//	Set the channel video format
		mDevice.SetVideoFormat (mVideoFormat, false, false, mInputChannel);
        mDevice.SetVideoFormat (mVideoFormat, false, false, mOutputChannel);

		//	Set frame buffer format
		mDevice.SetFrameBufferFormat (mInputChannel, mPixelFormat);
        mDevice.SetFrameBufferFormat (mOutputChannel, mPixelFormat);

		//	Set catpure mode
		mDevice.SetMode (mInputChannel, NTV2_MODE_CAPTURE, false);
        mDevice.SetMode (mOutputChannel, NTV2_MODE_DISPLAY, false);

		//	Enable frame buffer
		mDevice.EnableChannel (mInputChannel);
        mDevice.EnableChannel (mOutputChannel);

		//	Save input source
		mInputSource = ::NTV2ChannelToInputSource (mInputChannel);
	}
	else
	{
		//	Disable multiformat mode
		if (::NTV2DeviceCanDoMultiFormat (mDeviceID))
			mDevice.SetMultiFormatMode (false);

		//	Set the board format
        mDevice.SetVideoFormat (mVideoFormat, false, false, NTV2_CHANNEL1);
        mDevice.SetVideoFormat (mVideoFormat, false, false, NTV2_CHANNEL5);

		//	Set frame buffer format
		mDevice.SetFrameBufferFormat (mInputChannel, mPixelFormat);
        mDevice.SetFrameBufferFormat (mOutputChannel, mPixelFormat);

		//	Set display mode
		mDevice.SetMode (NTV2_CHANNEL1, NTV2_MODE_DISPLAY, false);
		mDevice.SetMode (NTV2_CHANNEL2, NTV2_MODE_DISPLAY, false);
		mDevice.SetMode (NTV2_CHANNEL3, NTV2_MODE_DISPLAY, false);
		mDevice.SetMode (NTV2_CHANNEL4, NTV2_MODE_DISPLAY, false);
        mDevice.SetMode (NTV2_CHANNEL5, NTV2_MODE_DISPLAY, false);
        mDevice.SetMode (NTV2_CHANNEL6, NTV2_MODE_DISPLAY, false);
        mDevice.SetMode (NTV2_CHANNEL7, NTV2_MODE_DISPLAY, false);
        mDevice.SetMode (NTV2_CHANNEL8, NTV2_MODE_DISPLAY, false);

		//	Set catpure mode
		mDevice.SetMode (mInputChannel, NTV2_MODE_CAPTURE, false);

		//	Enable frame buffer
		mDevice.EnableChannel (mInputChannel);
        mDevice.EnableChannel (mOutputChannel);

		//	Save input source
		mInputSource = ::NTV2ChannelToInputSource (mInputChannel);
	}

	//	Set the device reference to the input...
    if (mMultiStream)
    {
        mDevice.SetReference (NTV2_REFERENCE_FREERUN);
    }
    else
    {
        mDevice.SetReference (::NTV2InputSourceToReferenceSource (mInputSource));
    }

	//	Enable and subscribe to the interrupts for the channel to be used...
	mDevice.EnableInputInterrupt (mInputChannel);
	mDevice.SubscribeInputVerticalEvent (mInputChannel);

    mTimeBase.SetAJAFrameRate (GetAJAFrameRate(GetNTV2FrameRateFromVideoFormat (mVideoFormat)));

	return AJA_STATUS_SUCCESS;

}	//	SetupVideo


AJAStatus NTV2GstAVHevc::SetupAudio (void)
{
    //	In multiformat mode, base the audio system on the channel...
    if (mMultiStream && ::NTV2DeviceGetNumAudioStreams (mDeviceID) > 1 && UWord (mInputChannel) < ::NTV2DeviceGetNumAudioStreams (mDeviceID))
		mAudioSystem = ::NTV2ChannelToAudioSystem (mInputChannel);

	//	Have the audio system capture audio from the designated device input (i.e., ch1 uses SDIIn1, ch2 uses SDIIn2, etc.)...
	mDevice.SetAudioSystemInputSource (mAudioSystem, NTV2_AUDIO_EMBEDDED, ::NTV2ChannelToEmbeddedAudioInput (mInputChannel));

    mNumAudioChannels = ::NTV2DeviceGetMaxAudioChannels (mDeviceID);
    mDevice.SetNumberAudioChannels (mNumAudioChannels, mAudioSystem);
	mDevice.SetAudioRate (NTV2_AUDIO_48K, mAudioSystem);
    mDevice.SetEmbeddedAudioClock (NTV2_EMBEDDED_AUDIO_CLOCK_VIDEO_INPUT, mAudioSystem);

	//	The on-device audio buffer should be 4MB to work best across all devices & platforms...
	mDevice.SetAudioBufferSize (NTV2_AUDIO_BUFFER_BIG, mAudioSystem);

	return AJA_STATUS_SUCCESS;

}	//	SetupAudio


void NTV2GstAVHevc::SetupHostBuffers (void)
{
	mVideoBufferSize = GetVideoActiveSize (mVideoFormat, mPixelFormat, false, false);
    mPicInfoBufferSize = sizeof(HevcPictureInfo)*2;
    mEncInfoBufferSize = sizeof(HevcEncodedInfo)*2;
    mAudioBufferSize = NTV2_AUDIOSIZE_MAX;
	
	// video input ring
    mACInputCircularBuffer.SetAbortFlag (&mGlobalQuit);
    for (unsigned bufferNdx = 0; bufferNdx < VIDEO_RING_SIZE; bufferNdx++ )
	{
        memset (&mACInputBuffer[bufferNdx], 0, sizeof(AjaVideoBuff));
        mACInputBuffer[bufferNdx].pVideoBuffer		= new uint32_t [mVideoBufferSize/4];
        mACInputBuffer[bufferNdx].videoBufferSize	= mVideoBufferSize;
        mACInputBuffer[bufferNdx].videoDataSize		= 0;
        mACInputBuffer[bufferNdx].pInfoBuffer		= new uint32_t [mPicInfoBufferSize/4];
        mACInputBuffer[bufferNdx].infoBufferSize    = mPicInfoBufferSize;
        mACInputBuffer[bufferNdx].infoDataSize		= 0;
        mACInputCircularBuffer.Add (& mACInputBuffer[bufferNdx]);
	}
    
    // audio input ring
    mAudioInputCircularBuffer.SetAbortFlag (&mGlobalQuit);
    for (unsigned bufferNdx = 0; bufferNdx < AUDIO_RING_SIZE; bufferNdx++ )
    {
        memset (&mAudioInputBuffer[bufferNdx], 0, sizeof(AjaAudioBuff));
        mAudioInputBuffer[bufferNdx].pAudioBuffer		= new uint32_t [mAudioBufferSize/4];
        mAudioInputBuffer[bufferNdx].audioBufferSize	= mAudioBufferSize;
        mAudioInputBuffer[bufferNdx].audioDataSize		= 0;
        mAudioInputCircularBuffer.Add (& mAudioInputBuffer[bufferNdx]);
    }

    if (mHevcOutput)
    {
        // video hevc ring
        mVideoHevcCircularBuffer.SetAbortFlag (&mGlobalQuit);
        for (unsigned bufferNdx = 0; bufferNdx < VIDEO_RING_SIZE; bufferNdx++ )
        {
            memset (&mVideoHevcBuffer[bufferNdx], 0, sizeof(AjaVideoBuff));
            mVideoHevcBuffer[bufferNdx].pVideoBuffer	= new uint32_t [mVideoBufferSize/4];
            mVideoHevcBuffer[bufferNdx].videoBufferSize	= mVideoBufferSize;
            mVideoHevcBuffer[bufferNdx].videoDataSize	= 0;
            mVideoHevcBuffer[bufferNdx].pInfoBuffer		= new uint32_t [mEncInfoBufferSize/4];
            mVideoHevcBuffer[bufferNdx].infoBufferSize   = mEncInfoBufferSize;
            mVideoHevcBuffer[bufferNdx].infoDataSize		= 0;
            mVideoHevcCircularBuffer.Add (& mVideoHevcBuffer[bufferNdx]);
        }
    }
    
    // These video buffers are actually passed out of this class so we need to assign them unique numbers
    // so they can be tracked and also they have a state
    for (unsigned bufferNdx = 0; bufferNdx < VIDEO_ARRAY_SIZE; bufferNdx++ )
	{
        memset (&mVideoOutBuffer[bufferNdx], 0, sizeof(AjaVideoBuff));
        mVideoOutBuffer[bufferNdx].bufferId         = bufferNdx + 1;
        mVideoOutBuffer[bufferNdx].bufferRef        = 0;
        mVideoOutBuffer[bufferNdx].pVideoBuffer		= new uint32_t [mVideoBufferSize/4];
        mVideoOutBuffer[bufferNdx].videoBufferSize	= mVideoBufferSize;
        mVideoOutBuffer[bufferNdx].videoDataSize	= 0;
        mVideoOutBuffer[bufferNdx].pInfoBuffer		= new uint32_t [mPicInfoBufferSize/4];
        mVideoOutBuffer[bufferNdx].infoBufferSize   = mPicInfoBufferSize;
        mVideoOutBuffer[bufferNdx].infoDataSize		= 0;
	}

    // These audio buffers are actually passed out of this class so we need to assign them unique numbers
    // so they can be tracked and also they have a state
    for (unsigned bufferNdx = 0; bufferNdx < AUDIO_ARRAY_SIZE; bufferNdx++ )
    {
        memset (&mAudioOutBuffer[bufferNdx], 0, sizeof(AjaAudioBuff));
        mAudioOutBuffer[bufferNdx].bufferId             = bufferNdx + 1;
        mAudioOutBuffer[bufferNdx].bufferRef            = 0;
        mAudioOutBuffer[bufferNdx].pAudioBuffer         = new uint32_t [mAudioBufferSize/4];
        mAudioOutBuffer[bufferNdx].audioBufferSize      = mAudioBufferSize;
        mAudioOutBuffer[bufferNdx].audioDataSize		= 0;
    }
}	//	SetupHostBuffers


void NTV2GstAVHevc::FreeHostBuffers (void)
{
    for (unsigned bufferNdx = 0; bufferNdx < VIDEO_RING_SIZE; bufferNdx++)
    {
        if (mACInputBuffer[bufferNdx].pVideoBuffer)
        {
            delete [] mACInputBuffer[bufferNdx].pVideoBuffer;
            mACInputBuffer[bufferNdx].pVideoBuffer = NULL;
        }
        if (mACInputBuffer[bufferNdx].pInfoBuffer)
        {
            delete [] mACInputBuffer[bufferNdx].pInfoBuffer;
            mACInputBuffer[bufferNdx].pInfoBuffer = NULL;
        }
    }

    for (unsigned bufferNdx = 0; bufferNdx < AUDIO_RING_SIZE; bufferNdx++)
    {
        if (mAudioInputBuffer[bufferNdx].pAudioBuffer)
        {
            delete [] mAudioInputBuffer[bufferNdx].pAudioBuffer;
            mAudioInputBuffer[bufferNdx].pAudioBuffer = NULL;
        }
    }

    if (mHevcOutput)
    {
        for (unsigned bufferNdx = 0; bufferNdx < VIDEO_RING_SIZE; bufferNdx++)
        {
            if (mVideoHevcBuffer[bufferNdx].pVideoBuffer)
            {
                delete [] mVideoHevcBuffer[bufferNdx].pVideoBuffer;
                mVideoHevcBuffer[bufferNdx].pVideoBuffer = NULL;
            }
            if (mVideoHevcBuffer[bufferNdx].pInfoBuffer)
            {
                delete [] mVideoHevcBuffer[bufferNdx].pInfoBuffer;
                mVideoHevcBuffer[bufferNdx].pInfoBuffer = NULL;
            }
        }
    }

    for (unsigned bufferNdx = 0; bufferNdx < VIDEO_ARRAY_SIZE; bufferNdx++)
    {
        if (mVideoOutBuffer[bufferNdx].pVideoBuffer)
        {
            delete [] mVideoOutBuffer[bufferNdx].pVideoBuffer;
            mVideoOutBuffer[bufferNdx].pVideoBuffer = NULL;
        }
        if (mVideoOutBuffer[bufferNdx].pInfoBuffer)
        {
            delete [] mVideoOutBuffer[bufferNdx].pInfoBuffer;
            mVideoOutBuffer[bufferNdx].pInfoBuffer = NULL;
        }
    }

    for (unsigned bufferNdx = 0; bufferNdx < AUDIO_ARRAY_SIZE; bufferNdx++)
    {
        if (mAudioOutBuffer[bufferNdx].pAudioBuffer)
        {
            delete [] mAudioOutBuffer[bufferNdx].pAudioBuffer;
            mAudioOutBuffer[bufferNdx].pAudioBuffer = NULL;
        }
    }
}


void NTV2GstAVHevc::RouteInputSignal (void)
{
    // setup sdi io
	mDevice.SetSDITransmitEnable (NTV2_CHANNEL1, false);
	mDevice.SetSDITransmitEnable (NTV2_CHANNEL2, false);
	mDevice.SetSDITransmitEnable (NTV2_CHANNEL3, false);
	mDevice.SetSDITransmitEnable (NTV2_CHANNEL4, false);
    mDevice.SetSDITransmitEnable (NTV2_CHANNEL5, true);
    mDevice.SetSDITransmitEnable (NTV2_CHANNEL6, true);
    mDevice.SetSDITransmitEnable (NTV2_CHANNEL7, true);
    mDevice.SetSDITransmitEnable (NTV2_CHANNEL8, true);

	//	Give the device some time to lock to the input signal...
	mDevice.WaitForOutputVerticalInterrupt (mInputChannel, 8);

	//	When input is 3Gb convert to 3Ga for capture (no RGB support?)
	bool is3Gb = false;
	mDevice.GetSDIInput3GbPresent (is3Gb, mInputChannel);

	if (mQuad)
	{
		mDevice.SetSDIInLevelBtoLevelAConversion (NTV2_CHANNEL1, is3Gb);
		mDevice.SetSDIInLevelBtoLevelAConversion (NTV2_CHANNEL2, is3Gb);
		mDevice.SetSDIInLevelBtoLevelAConversion (NTV2_CHANNEL3, is3Gb);
		mDevice.SetSDIInLevelBtoLevelAConversion (NTV2_CHANNEL4, is3Gb);
        mDevice.SetSDIOutLevelAtoLevelBConversion (NTV2_CHANNEL5, false);
        mDevice.SetSDIOutLevelAtoLevelBConversion (NTV2_CHANNEL6, false);
        mDevice.SetSDIOutLevelAtoLevelBConversion (NTV2_CHANNEL7, false);
        mDevice.SetSDIOutLevelAtoLevelBConversion (NTV2_CHANNEL8, false);
    }
	else
	{
		mDevice.SetSDIInLevelBtoLevelAConversion (mInputChannel, is3Gb);
        mDevice.SetSDIOutLevelAtoLevelBConversion (mOutputChannel, false);
    }

	if (!mMultiStream)			//	If not doing multistream...
		mDevice.ClearRouting();	//	...replace existing routing

	//	Connect FB inputs to SDI input spigots...
	mDevice.Connect (NTV2_XptFrameBuffer1Input, NTV2_XptSDIIn1);
	mDevice.Connect (NTV2_XptFrameBuffer2Input, NTV2_XptSDIIn2);
	mDevice.Connect (NTV2_XptFrameBuffer3Input, NTV2_XptSDIIn3);
	mDevice.Connect (NTV2_XptFrameBuffer4Input, NTV2_XptSDIIn4);

	//	Connect SDI output spigots to FB outputs...
    mDevice.Connect (NTV2_XptSDIOut5Input, NTV2_XptFrameBuffer5YUV);
    mDevice.Connect (NTV2_XptSDIOut6Input, NTV2_XptFrameBuffer6YUV);
    mDevice.Connect (NTV2_XptSDIOut7Input, NTV2_XptFrameBuffer7YUV);
    mDevice.Connect (NTV2_XptSDIOut8Input, NTV2_XptFrameBuffer8YUV);

	//	Give the device some time to lock to the input signal...
	mDevice.WaitForOutputVerticalInterrupt (mInputChannel, 8);
}


void NTV2GstAVHevc::SetupAutoCirculate (void)
{
	//	Tell capture AutoCirculate to use 8 frame buffers on the device...
    mInputTransferStruct.Clear();
    mInputTransferStruct.acFrameBufferFormat = mPixelFormat;

	mDevice.AutoCirculateStop (mInputChannel);
	mDevice.AutoCirculateInitForInput (mInputChannel,	8,                  //	Frames to circulate
										mAudioSystem,                       //	Which audio system
										AUTOCIRCULATE_WITH_RP188);          //	With RP188?
}


AJAStatus NTV2GstAVHevc::Run ()
{
	if (mDevice.GetInputVideoFormat (mInputSource) == NTV2_FORMAT_UNKNOWN)
        GST_WARNING ("No video signal present on the input connector");

	// always start the AC thread
    StartACThread ();
    
    // if not doing hevc output then just start the video output thread otherwise start the hevc threads
    if (!mHevcOutput)
    {
        StartVideoOutputThread ();
        StartAudioOutputThread ();
    }
    else
    {
        StartCodecRawThread ();
        StartCodecHevcThread ();
        StartHevcOutputThread ();
        StartAudioOutputThread ();
    }
    
    mStarted = true;
	return AJA_STATUS_SUCCESS;
}


// This is where we will start the AC thread
void NTV2GstAVHevc::StartACThread (void)
{
    mACInputThread = new AJAThread ();
    mACInputThread->Attach (ACInputThreadStatic, this);
    mACInputThread->SetPriority (AJA_ThreadPriority_High);
    mACInputThread->Start ();
}


// This is where we will stop the AC thread
void NTV2GstAVHevc::StopACThread (void)
{
    if (mACInputThread)
    {
        while (mACInputThread->Active ())
            AJATime::Sleep (10);
        
        delete mACInputThread;
		mACInputThread = NULL;
    }
}


// The video input thread static callback
void NTV2GstAVHevc::ACInputThreadStatic (AJAThread * pThread, void * pContext)
{
	(void) pThread;

	NTV2GstAVHevc *	pApp (reinterpret_cast <NTV2GstAVHevc *> (pContext));
    pApp->ACInputWorker ();
}


void NTV2GstAVHevc::ACInputWorker (void)
{
	// start AutoCirculate running...
	mDevice.AutoCirculateStart (mInputChannel);

	while (!mGlobalQuit)
	{
		AUTOCIRCULATE_STATUS	acStatus;
		mDevice.AutoCirculateGetStatus (mInputChannel, acStatus);

        // wait for captured frame
		if (acStatus.acState == NTV2_AUTOCIRCULATE_RUNNING && acStatus.acBufferLevel > 1)
		{
			// At this point, there's at least one fully-formed frame available in the device's
			// frame buffer to transfer to the host. Reserve an AvaDataBuffer to "produce", and
			// use it in the next transfer from the device...
            AjaVideoBuff *	pVideoData	(mACInputCircularBuffer.StartProduceNextBuffer ());
            if (pVideoData)
            {
                // setup buffer pointers for transfer
                mInputTransferStruct.SetVideoBuffer(pVideoData->pVideoBuffer, pVideoData->videoBufferSize);
                mInputTransferStruct.SetAudioBuffer(NULL, 0);

                AjaAudioBuff *	pAudioData = NULL;
                pAudioData = mAudioInputCircularBuffer.StartProduceNextBuffer ();
                if (pAudioData)
                {
                    mInputTransferStruct.SetAudioBuffer(pAudioData->pAudioBuffer, pAudioData->audioBufferSize);
                }

                // do the transfer from the device into our host AvaDataBuffer...
                mDevice.AutoCirculateTransfer (mInputChannel, mInputTransferStruct);

                // get the video data size
                pVideoData->videoDataSize = pVideoData->videoBufferSize;
                pVideoData->lastFrame = mLastFrame;

                // get the audio data size
                pAudioData->audioDataSize = mInputTransferStruct.acTransferStatus.acAudioTransferSize;
                pAudioData->lastFrame = mLastFrame;

                if (mWithAnc)
                {
                    // get the sdi input anc data
                    pVideoData->timeCodeDBB = mInputTransferStruct.acTransferStatus.acFrameStamp.acRP188.fDBB;
                    pVideoData->timeCodeLow = mInputTransferStruct.acTransferStatus.acFrameStamp.acRP188.fLo;
                    pVideoData->timeCodeHigh = mInputTransferStruct.acTransferStatus.acFrameStamp.acRP188.fHi;
                }

                if (mWithInfo)
                {
                    // get picture and additional data pointers
                    HevcPictureInfo * pInfo = (HevcPictureInfo*)pVideoData->pInfoBuffer;
                    HevcPictureData * pPicData = &pInfo->pictureData;

                    // initialize info buffer to 0
                    memset(pInfo, 0, pVideoData->infoBufferSize);

                    // calculate pts based on 90 Khz clock tick
                    uint64_t pts = (uint64_t)mTimeBase.FramesToMicroseconds(mVideoInputFrameCount)*90000/1000000;

                    // set serial number, pts and picture number
                    pPicData->serialNumber = mVideoInputFrameCount;         // can be anything
                    pPicData->ptsValueLow = (uint32_t)(pts & 0xffffffff);
                    pPicData->ptsValueHigh = (uint32_t)(pts >> 32);
                    pPicData->pictureNumber = mVideoInputFrameCount + 1;    // must count starting with 1

                    // set info data size
                    pVideoData->infoDataSize = sizeof(HevcPictureData);
                }

                if(pVideoData->lastFrame && !mLastFrameInput)
                {
                    GST_INFO ("Capture last frame number %d", mVideoInputFrameCount);
                    mLastFrameInput = true;
                }

                mVideoInputFrameCount++;

                // signal that we're done "producing" the frame, making it available for future "consumption"...
                if (pAudioData)
                {
                    mAudioInputCircularBuffer.EndProduceNextBuffer ();
                }

                if (pVideoData)
                {
                    mACInputCircularBuffer.EndProduceNextBuffer ();
                }
            }	// if A/C running and frame(s) are available for transfer
        }
		else
		{
			// Either AutoCirculate is not running, or there were no frames available on the device to transfer.
			// Rather than waste CPU cycles spinning, waiting until a frame becomes available, it's far more
			// efficient to wait for the next input vertical interrupt event to get signaled...
            mDevice.WaitForInputVerticalInterrupt (mInputChannel);
		}
	}	// loop til quit signaled

	// Stop AutoCirculate...
	mDevice.AutoCirculateStop (mInputChannel);
}


// This is where we start the video output thread
void NTV2GstAVHevc::StartVideoOutputThread (void)
{
    mVideoOutputThread = new AJAThread ();
    mVideoOutputThread->Attach (VideoOutputThreadStatic, this);
    mVideoOutputThread->SetPriority (AJA_ThreadPriority_High);
    mVideoOutputThread->Start ();
}

// This is where we stop the video output thread
void NTV2GstAVHevc::StopVideoOutputThread (void)
{
    if (mVideoOutputThread)
    {
        while (mVideoOutputThread->Active ())
            AJATime::Sleep (10);
        
        delete mVideoOutputThread;
		mVideoOutputThread = NULL;
    }
}


// The video output static callback
void NTV2GstAVHevc::VideoOutputThreadStatic (AJAThread * pThread, void * pContext)
{
	(void) pThread;

	NTV2GstAVHevc *	pApp (reinterpret_cast <NTV2GstAVHevc *> (pContext));
    pApp->VideoOutputWorker ();
}


void NTV2GstAVHevc::VideoOutputWorker (void)
{
	while (!mGlobalQuit)
	{
		// wait for the next video input buffer
        AjaVideoBuff *	pFrameData (mACInputCircularBuffer.StartConsumeNextBuffer ());
		if (pFrameData)
		{
            if (!mLastFrameVideoOut)
			{
                AjaVideoBuff * pDstFrame = AcquireVideoBuffer();
                if (pDstFrame)
                {
                    memcpy(pDstFrame->pVideoBuffer, pFrameData->pVideoBuffer, pFrameData->videoDataSize);
                    pDstFrame->fameNumber = mVideoOutFrameCount;
                    pDstFrame->videoDataSize = pFrameData->videoDataSize;
                    pDstFrame->timeCodeDBB = pFrameData->timeCodeDBB;
                    pDstFrame->timeCodeLow = pFrameData->timeCodeLow;
                    pDstFrame->timeCodeHigh = pFrameData->timeCodeHigh;
                    pDstFrame->lastFrame = pFrameData->lastFrame;
                    if (mWithInfo)
                    {
                        memcpy(pDstFrame->pInfoBuffer, pFrameData->pInfoBuffer, pFrameData->infoDataSize);
                        pDstFrame->infoDataSize = pFrameData->infoDataSize;
                    }

                    // The time duration is based off the frame rate and for now we will pass the absolute
                    // time which will be adjusted by the start time in the layer above.
                    pDstFrame->timeDuration = (uint64_t)mTimeBase.FramesToMicroseconds(1)*1000;
                    GetHardwareClock(ASECOND, &pDstFrame->timeStamp);

                    // Possible callbacks are not setup yet so make sure we release the buffer if
                    // no one is there to catch them
                    if (!DoCallback(VIDEO_CALLBACK, (int64_t) pDstFrame))
                        ReleaseVideoBuffer(pDstFrame);
                }
                
                if (pFrameData->lastFrame)
				{
                    GST_INFO ("Video out last frame number %d", mHevcOutFrameCount);
					mLastFrameVideoOut = true;
				}
                
                mVideoOutFrameCount++;
            }
            
			// release the video input buffer
            mACInputCircularBuffer.EndConsumeNextBuffer ();

        }
	}	// loop til quit signaled
}


// This is where we start the codec raw thread
void NTV2GstAVHevc::StartCodecRawThread (void)
{
    mCodecRawThread = new AJAThread ();
    mCodecRawThread->Attach (CodecRawThreadStatic, this);
    mCodecRawThread->SetPriority (AJA_ThreadPriority_High);
    mCodecRawThread->Start ();
}


// This is where we stop the codec raw thread
void NTV2GstAVHevc::StopCodecRawThread (void)
{
    if (mCodecRawThread)
    {
        while (mCodecRawThread->Active ())
            AJATime::Sleep (10);
        
        delete mCodecRawThread;
		mCodecRawThread = NULL;
    }
}


// The codec raw static callback
void NTV2GstAVHevc::CodecRawThreadStatic (AJAThread * pThread, void * pContext)
{
	(void) pThread;

	NTV2GstAVHevc *	pApp (reinterpret_cast <NTV2GstAVHevc *> (pContext));
    pApp->CodecRawWorker ();
}


void NTV2GstAVHevc::CodecRawWorker (void)
{
	while (!mGlobalQuit)
	{
		// wait for the next raw video frame
        AjaVideoBuff *	pFrameData (mACInputCircularBuffer.StartConsumeNextBuffer ());
		if (pFrameData)
		{
			if (!mLastFrameVideoOut)
			{
                // transfer the raw video frame to the codec
                if (mWithInfo)
                {
                    mM31->RawTransfer(mEncodeChannel,
                                      (uint8_t*)pFrameData->pVideoBuffer,
                                      pFrameData->videoDataSize,
                                      (uint8_t*)pFrameData->pInfoBuffer,
                                      pFrameData->infoDataSize,
                                      pFrameData->lastFrame);
                }
                else
                {
                    mM31->RawTransfer(mEncodeChannel,
                                      (uint8_t*)pFrameData->pVideoBuffer,
                                      pFrameData->videoDataSize,
                                      pFrameData->lastFrame);
                }
                if (pFrameData->lastFrame)
				{
					mLastFrameVideoOut = true;
				}

                mCodecRawFrameCount++;
            }

			// release the raw video frame
            mACInputCircularBuffer.EndConsumeNextBuffer ();
		}
	}  // loop til quit signaled
}


// This is where we will start the codec hevc thread
void NTV2GstAVHevc::StartCodecHevcThread (void)
{
    mCodecHevcThread = new AJAThread ();
    mCodecHevcThread->Attach (CodecHevcThreadStatic, this);
    mCodecHevcThread->SetPriority (AJA_ThreadPriority_High);
    mCodecHevcThread->Start ();
}

// This is where we will stop the codec hevc thread
void NTV2GstAVHevc::StopCodecHevcThread (void)
{
    if (mCodecHevcThread)
    {
        while (mCodecHevcThread->Active ())
            AJATime::Sleep (10);
        
        delete mCodecHevcThread;
		mCodecHevcThread = NULL;
    }
}


// The codec hevc static callback
void NTV2GstAVHevc::CodecHevcThreadStatic (AJAThread * pThread, void * pContext)
{
    (void) pThread;

    NTV2GstAVHevc *	pApp (reinterpret_cast <NTV2GstAVHevc *> (pContext));
    pApp->CodecHevcWorker ();
}


void NTV2GstAVHevc::CodecHevcWorker (void)
{
    while (!mGlobalQuit)
    {
        // wait for the next hevc frame 
        AjaVideoBuff *	pFrameData (mVideoHevcCircularBuffer.StartProduceNextBuffer ());
        if (pFrameData)
        {
			if (!mLastFrameHevc)
			{
                // transfer an hevc frame from the codec including encoded information
                mM31->EncTransfer(mEncodeChannel,
                                  (uint8_t*)pFrameData->pVideoBuffer,
								  pFrameData->videoBufferSize,
                                  (uint8_t*)pFrameData->pInfoBuffer,
                                  pFrameData->infoBufferSize,
                                  pFrameData->videoDataSize,
                                  pFrameData->infoDataSize,
                                  pFrameData->lastFrame);

                if (pFrameData->lastFrame)
				{
					mLastFrameHevc = true;
                }

                mCodecHevcFrameCount++;
            }

            // release and recycle the buffer...
            mVideoHevcCircularBuffer.EndProduceNextBuffer ();
        }
    }	//	loop til quit signaled
}


// This is where we start the Hevc output thread
void NTV2GstAVHevc::StartHevcOutputThread (void)
{
    mHevcOutputThread = new AJAThread ();
    mHevcOutputThread->Attach (HevcOutputThreadStatic, this);
    mHevcOutputThread->SetPriority (AJA_ThreadPriority_High);
    mHevcOutputThread->Start ();
}


// This is where we stop the Hevc output thread
void NTV2GstAVHevc::StopHevcOutputThread (void)
{
    if (mHevcOutputThread)
    {
        while (mHevcOutputThread->Active ())
            AJATime::Sleep (10);
        
        delete mHevcOutputThread;
		mHevcOutputThread = NULL;
    }
}


// The Hevc output static callback
void NTV2GstAVHevc::HevcOutputThreadStatic (AJAThread * pThread, void * pContext)
{
    (void) pThread;

    NTV2GstAVHevc *	pApp (reinterpret_cast <NTV2GstAVHevc *> (pContext));
    pApp->HevcOutputWorker ();

} // HevcOutputThreadStatic


void NTV2GstAVHevc::HevcOutputWorker (void)
{
    while (!mGlobalQuit)
    {
        // wait for the next video input buffer
        AjaVideoBuff *	pFrameData (mVideoHevcCircularBuffer.StartConsumeNextBuffer ());
        if (pFrameData)
        {
            if (!mLastFrameHevcOut)
            {
                AjaVideoBuff * pDstFrame = AcquireVideoBuffer();
                if (pDstFrame)
                {
                    memcpy(pDstFrame->pVideoBuffer, pFrameData->pVideoBuffer, pFrameData->videoDataSize);
                    pDstFrame->fameNumber = mHevcOutFrameCount;
                    pDstFrame->videoDataSize = pFrameData->videoDataSize;
                    pDstFrame->timeCodeDBB = pFrameData->timeCodeDBB;
                    pDstFrame->timeCodeLow = pFrameData->timeCodeLow;
                    pDstFrame->timeCodeHigh = pFrameData->timeCodeHigh;
                    pDstFrame->lastFrame = pFrameData->lastFrame;
                    if (mWithInfo)
                    {
                        memcpy(pDstFrame->pInfoBuffer, pFrameData->pInfoBuffer, pFrameData->infoDataSize);
                        pDstFrame->infoDataSize = pFrameData->infoDataSize;
                    }

                    // The time duration is based off the frame rate and for now we will pass the absolute
                    // time which will be adjusted by the start time in the layer above.
                    pDstFrame->timeDuration = (uint64_t)mTimeBase.FramesToMicroseconds(1)*1000;
                    GetHardwareClock(ASECOND, &pDstFrame->timeStamp);

                    // Possible callbacks are not setup yet so make sure we release the buffer if
                    // no one is there to catch them
                    if (!DoCallback(VIDEO_CALLBACK, (int64_t) pDstFrame))
                        ReleaseVideoBuffer(pDstFrame);
                }

                if (pFrameData->lastFrame)
                {
                    GST_INFO ("Hevc out last frame number %d", mHevcOutFrameCount);
                    mLastFrameHevcOut = true;
                }

                mHevcOutFrameCount++;
            }
            // release the video input buffer
            mVideoHevcCircularBuffer.EndConsumeNextBuffer ();
        }
    }	// loop til quit signaled
}


// This is where we start the audio output thread
void NTV2GstAVHevc::StartAudioOutputThread (void)
{
    mAudioOutputThread = new AJAThread ();
    mAudioOutputThread->Attach (AudioOutputThreadStatic, this);
    mAudioOutputThread->SetPriority (AJA_ThreadPriority_High);
    mAudioOutputThread->Start ();
}


// This is where we stop the audio output thread
void NTV2GstAVHevc::StopAudioOutputThread (void)
{
    if (mAudioOutputThread)
    {
        while (mAudioOutputThread->Active ())
            AJATime::Sleep (10);
        
        delete mAudioOutputThread;
		mAudioOutputThread = NULL;
    }
}


// The audio output static callback
void NTV2GstAVHevc::AudioOutputThreadStatic (AJAThread * pThread, void * pContext)
{
    (void) pThread;

    NTV2GstAVHevc *	pApp (reinterpret_cast <NTV2GstAVHevc *> (pContext));
    pApp->AudioOutputWorker ();
}


void NTV2GstAVHevc::AudioOutputWorker (void)
{
    while (!mGlobalQuit)
    {
        // wait for the next codec hevc frame
        AjaAudioBuff *	pFrameData (mAudioInputCircularBuffer.StartConsumeNextBuffer ());
        if (pFrameData)
        {
            if (!mLastFrameAudioOut)
            {
                if (pFrameData->lastFrame)
                {
                    GST_INFO ("Audio out last frame number %d", mAudioOutFrameCount);
                    mLastFrameAudioOut = true;
                }
            }

            mAudioOutFrameCount++;

            // release the hevc buffer
            mAudioInputCircularBuffer.EndConsumeNextBuffer ();
        }
    } // loop til quit signaled
}


void NTV2GstAVHevc::SetCallback(CallBackType cbType, int64_t callback, int64_t callbackRefcon)
{
    if (cbType == VIDEO_CALLBACK)
    {
        mVideoCallback = callback;
        mVideoCallbackRefcon = callbackRefcon;
    }
    else if (cbType == AUDIO_CALLBACK)
    {
        mAudioCallback = callback;
        mAudioCallbackRefcon = callbackRefcon;
    }
}


AjaVideoBuff* NTV2GstAVHevc::AcquireVideoBuffer()
{
    AJAAutoLock	autoLock (mLock);
    
    for (unsigned bufferNdx = 0; bufferNdx < VIDEO_ARRAY_SIZE; bufferNdx++ )
	{
        if (mVideoOutBuffer[bufferNdx].bufferRef == 0)
        {
            mVideoOutBuffer[bufferNdx].bufferRef++;
            //printf("acquired video buffer %d\n", mVideoOutBuffer[bufferNdx].bufferId);

            return &mVideoOutBuffer[bufferNdx];
        }
	}
    printf("Error: could not find a video buffer\n");
    return NULL;
}


AjaAudioBuff* NTV2GstAVHevc::AcquireAudioBuffer()
{
    AJAAutoLock	autoLock (mLock);

    for (unsigned bufferNdx = 0; bufferNdx < AUDIO_ARRAY_SIZE; bufferNdx++ )
	{
        if (mAudioOutBuffer[bufferNdx].bufferRef == 0)
        {
            mAudioOutBuffer[bufferNdx].bufferRef++;
            //printf("acquired audio buffer %d\n", mAudioOutBuffer[bufferNdx].bufferId);

            return &mAudioOutBuffer[bufferNdx];
        }
	}
    printf("Error: could not find an audio buffer\n");
    return NULL;
}


void NTV2GstAVHevc::ReleaseVideoBuffer(AjaVideoBuff * videoBuffer)
{
    AJAAutoLock	autoLock (mLock);

    if (videoBuffer->bufferRef)
    {
        videoBuffer->bufferRef--;
        //printf("released video buffer %d %d\n", videoBuffer->bufferId, videoBuffer->bufferRef);
    }
}


void NTV2GstAVHevc::ReleaseAudioBuffer(AjaAudioBuff * audioBuffer)
{
    AJAAutoLock	autoLock (mLock);

    if (audioBuffer->bufferRef)
    {
        audioBuffer->bufferRef--;
        //printf("released audio buffer %d %d\n", audioBuffer->bufferId, audioBuffer->bufferRef);
    }
}


void NTV2GstAVHevc::AddRefVideoBuffer(AjaVideoBuff * videoBuffer)
{
    AJAAutoLock	autoLock (mLock);
    videoBuffer->bufferRef++;
    //printf("add ref video buffer %d %d\n", videoBuffer->bufferId, videoBuffer->bufferRef);
}


void NTV2GstAVHevc::AddRefAudioBuffer(AjaAudioBuff * audioBuffer)
{
    AJAAutoLock	autoLock (mLock);
    audioBuffer->bufferRef++;
    //printf("add ref audio buffer %d %d\n", audioBuffer->bufferId, audioBuffer->bufferRef);
}


bool NTV2GstAVHevc::GetHardwareClock(uint64_t desiredTimeScale, uint64_t * time)
{
    uint32_t    audioCounter(0);

    bool status = mDevice.ReadRegister(kRegAud1Counter, &audioCounter);
    *time = (audioCounter * desiredTimeScale) / 48000;
    return status;
}


AJAStatus NTV2GstAVHevc::DetermineInputFormat(NTV2Channel inputChannel, bool quad, NTV2VideoFormat& videoFormat)
{
    NTV2VideoFormat sdiFormat = mDevice.GetSDIInputVideoFormat (inputChannel);
    if (sdiFormat == NTV2_FORMAT_UNKNOWN)
        return AJA_STATUS_FAIL;
    
    switch (sdiFormat)
    {
        case NTV2_FORMAT_1080p_5000_A:
        case NTV2_FORMAT_1080p_5000_B:
            videoFormat = NTV2_FORMAT_1080p_5000_A;
            if (quad) videoFormat = NTV2_FORMAT_4x1920x1080p_5000;
            break;
        case NTV2_FORMAT_1080p_5994_A:
        case NTV2_FORMAT_1080p_5994_B:
            videoFormat = NTV2_FORMAT_1080p_5994_A;
            if (quad) videoFormat = NTV2_FORMAT_4x1920x1080p_5994;
            break;
        case NTV2_FORMAT_1080p_6000_A:
        case NTV2_FORMAT_1080p_6000_B:
            videoFormat = NTV2_FORMAT_1080p_6000_A;
            if (quad) videoFormat = NTV2_FORMAT_4x1920x1080p_6000;
            break;
        default:
            videoFormat = sdiFormat;
            break;
    }
    
    return AJA_STATUS_SUCCESS;
}


AJA_FrameRate NTV2GstAVHevc::GetAJAFrameRate(NTV2FrameRate frameRate)
{
   switch (frameRate)
   {
   case NTV2_FRAMERATE_2398: return AJA_FrameRate_2398;
   case NTV2_FRAMERATE_2400: return AJA_FrameRate_2400;
   case NTV2_FRAMERATE_2500: return AJA_FrameRate_2500;
   case NTV2_FRAMERATE_2997: return AJA_FrameRate_2997;
   case NTV2_FRAMERATE_3000: return AJA_FrameRate_3000;
   case NTV2_FRAMERATE_4795: return AJA_FrameRate_4795;
   case NTV2_FRAMERATE_4800: return AJA_FrameRate_4800;
   case NTV2_FRAMERATE_5000: return AJA_FrameRate_5000;
   case NTV2_FRAMERATE_5994: return AJA_FrameRate_5994;
   case NTV2_FRAMERATE_6000: return AJA_FrameRate_6000;
   default: break;
   }

   return AJA_FrameRate_Unknown;
}


bool NTV2GstAVHevc::DoCallback(CallBackType type, int64_t msg)
{
    if (type == VIDEO_CALLBACK)
    {
        if (mVideoCallback)
        {
            NTV2Callback cb = (NTV2Callback) mVideoCallback;
            return (*cb)(mVideoCallbackRefcon, msg);
        }
    }
    else if (type == AUDIO_CALLBACK)
    {
        if (mAudioCallback)
        {
            NTV2Callback cb = (NTV2Callback) mAudioCallback;
            return (*cb)(mAudioCallbackRefcon, msg);
        }
    }
    return false;
}
