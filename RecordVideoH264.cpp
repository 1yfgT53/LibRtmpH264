// RtmpH264.cpp : 定义 DLL 应用程序的导出函数。
//

#include "stdafx.h"
#include "RecordVideoH264.h"
#include "objbase.h"

MP4FileHandle hMp4File;
MP4TrackId videoTrackId;
MP4TrackId audioTrackId;


long InitMp4(char* fileName);
void CloseMp4File();
int WriteSpsPPs(int width, int height, int fps, int audiochannel, int audioSample, char* spsData, int spsLen, char* ppsData, int ppsLen);



VideoRecord* pVideoRecord = NULL;
bool isInitFaacEncode = false;
bool finishedFirstVideoImage = false;
unsigned long lastFrameTime = 0;

#define EB_MAX(a,b)               (((a) > (b)) ? (a) : (b))
#define EB_MIN(a,b)               (((a) < (b)) ? (a) : (b))

#define PCM_BITSIZE               2
//停止音频编码
void AudioEncoderFree(AudioEncodeObj* psObj)
{
	if (!isInitFaacEncode)
		return;

	int						nRet;
	if (psObj->hEncoder)
	{
		while (1)
		{
			nRet = faacEncEncode(psObj->hEncoder, 0, 0, (unsigned char*)(psObj->szAacAudio), psObj->nAacAudioSize);
			if (0 == nRet) break;
		}
		nRet = faacEncClose(psObj->hEncoder);
	}
	if (psObj->szPcmAudio)
	{
		free(psObj->szPcmAudio);
	}
	if (psObj->szAacAudio)
	{
		free(psObj->szAacAudio);
	}
	psObj->hEncoder = 0;
	psObj->nSampleRate = 0;
	psObj->nChannels = 0;
	psObj->nTimeDelta = 0;
	psObj->szPcmAudio = 0;
	psObj->nPcmAudioSize = 0;
	psObj->nPcmAudioLen = 0;
	psObj->szAacAudio = 0;
	psObj->nAacAudioSize = 0;

	isInitFaacEncode = false;
}



//初始化音频编码器
bool AudioEncoderInit(AudioEncodeObj* psObj, unsigned long nSampleRate, unsigned long nChannels)
{
	faacEncConfigurationPtr pConfiguration;
	unsigned long			nInputSamples;
	unsigned long			nMaxOutputBytes;
	int						nRet;
	char*					szPcmAudio;
	unsigned long			nPcmAudioSize;
	char*					szAacAudio;
	unsigned long			nAacAudioSize;
	unsigned char *			buf;
	unsigned long			len;

	// skip
	if ((psObj->hEncoder) && (nSampleRate == psObj->nSampleRate) && (nChannels == psObj->nChannels))
	{
		return true;
	}


	// open FAAC engine
	faacEncHandle hEncoder = faacEncOpen(nSampleRate, nChannels, &nInputSamples, &nMaxOutputBytes);
	if (hEncoder == NULL)
	{
		//assert(0);
		return false;
	}

	// set encoding configuration
	pConfiguration = faacEncGetCurrentConfiguration(hEncoder);
	pConfiguration->aacObjectType = LOW;
	pConfiguration->bitRate = 32000;
	pConfiguration->mpegVersion = MPEG4;
	pConfiguration->allowMidside = 0;
	pConfiguration->useTns = 0;
	pConfiguration->useLfe = 0;
	pConfiguration->bandWidth = 0;
	pConfiguration->quantqual = 100;
	pConfiguration->outputFormat = 0;
	pConfiguration->inputFormat = (PCM_BITSIZE == 2) ? FAAC_INPUT_16BIT : FAAC_INPUT_32BIT;
	pConfiguration->shortctl = SHORTCTL_NORMAL;
	nRet = faacEncSetConfiguration(hEncoder, pConfiguration);
	if (!nRet)
	{
		//assert(0);
		nRet = faacEncClose(hEncoder);
		return false;
	}

	// buffer
	nPcmAudioSize = nInputSamples * PCM_BITSIZE * nChannels;
	szPcmAudio = (char*)malloc(nPcmAudioSize);
	nAacAudioSize = nMaxOutputBytes;
	szAacAudio = (char*)malloc(nAacAudioSize);
	if ((!szPcmAudio) || (!szAacAudio))
	{
		nRet = faacEncClose(hEncoder);
		if (szPcmAudio)
		{
			free(szPcmAudio);
		}
		if (szAacAudio)
		{
			free(szAacAudio);
		}
		//assert(0);
		return false;
	}

	// success
	psObj->hEncoder = hEncoder;
	psObj->nSampleRate = nSampleRate;
	psObj->nChannels = nChannels;
	psObj->nTimeDelta = (nInputSamples * 1000 / nSampleRate);
	psObj->szPcmAudio = szPcmAudio;
	psObj->nPcmAudioSize = nPcmAudioSize;
	psObj->nPcmAudioLen = 0;
	psObj->szAacAudio = szAacAudio;
	psObj->nAacAudioSize = nAacAudioSize;

	// send aac header
	faacEncGetDecoderSpecificInfo(hEncoder, &buf, &len);

	if (pVideoRecord->m_pAudioConfig)
	{
		free(pVideoRecord->m_pAudioConfig);
		pVideoRecord->m_pAudioConfig = NULL;
	}
	pVideoRecord->m_pAudioConfig = new char[len];
	memcpy(pVideoRecord->m_pAudioConfig, buf, len);
	pVideoRecord->m_nAudioConfigLen = len;


	isInitFaacEncode = true;
	return true;
}


// Class constructor
VideoRecord::VideoRecord() :m_SwsContext(NULL)
	, h(NULL)
	, nal_t(NULL)
	, m_pAudioConfig(NULL)
	, i_nal(0)
	, m_nAudioConfigLen(0)
	, m_pPreFrame(NULL)
	, m_nPreFrameLen(0)
{

}


//释放编码器内存
void VideoRecord::FreeEncodeParams()
{
	if(h)
	{
		x264_encoder_close(h);
		h = NULL;
	}

	if(m_SwsContext)
	{
		sws_freeContext(m_SwsContext);
		m_SwsContext = NULL;
	}

	AudioEncoderFree(&m_AudioEncodeObj);
	
	//DeleteCriticalSection(&m_Cs);
}


//初始化视频编码器
int VideoRecord::WriteVideoParams(unsigned long width, unsigned long height, unsigned long fps, unsigned long bitrate)
{
	m_width = width;//宽，根据实际情况改
	m_height = height;//高
	m_frameRate = fps;
	m_bitRate = bitrate;


	x264_param_default(&param);//设置默认参数具体见common/common.c


	//* 使用默认参数，在这里因为我的是实时网络传输，所以我使用了zerolatency的选项，使用这个选项之后就不会有delayed_frames，如果你使用的不是这样的话，还需要在编码完成之后得到缓存的编码帧  
	x264_param_default_preset(&param, "veryfast", "zerolatency");

	//* cpuFlags 
	param.i_threads = X264_SYNC_LOOKAHEAD_AUTO;// 并行编码线程为0 

	//* 视频选项
	param.i_width = m_width;
	param.i_height = m_height;
	param.i_frame_total = 0; //* 编码总帧数.不知道用0.
	param.i_keyint_min = 0;//关键帧最小间隔
	param.i_keyint_max = fps;//关键帧最大间隔
	param.b_annexb = 1;//1前面为0x00000001,0为nal长度
	param.b_repeat_headers = 0;//关键帧前面是否放sps跟pps帧，0 否 1，放	

	param.i_csp = X264_CSP_I420;


	//* 流参数
	param.i_bframe = 0;	//B帧
	param.b_open_gop = 0;
	param.i_bframe_pyramid = 0;
	param.i_bframe_adaptive = X264_B_ADAPT_FAST;

	//* 速率控制参数 
	param.rc.i_lookahead = 0;
	param.rc.i_bitrate = bitrate; //* 码率(比特率,单位Kbps)
	param.rc.f_rf_constant = 5;	//rc.f_rf_constant是实际质量，越大图像越花，越小越清晰。
	param.rc.f_rf_constant_max = 25;
	param.rc.i_rc_method = X264_RC_ABR;//参数i_rc_method表示码率控制，CQP(恒定质量)，CRF(恒定码率)，ABR(平均码率)
	param.rc.i_vbv_max_bitrate = (int)(m_bitRate*1.2); // 平均码率模式下，最大瞬时码率，默认0(与-B设置相同)
	param.rc.i_bitrate = (int)m_bitRate;

	//* muxing parameters
	param.i_fps_den = 1; // 帧率分母
	param.i_fps_num = fps;// 帧率分子
	param.i_timebase_num = 1;
	param.i_timebase_den = 1000;


	//* 设置Profile.使用Baseline profile  
	//x264_param_apply_profile(&param, x264_profile_names[0]);

	h = x264_encoder_open(&param);//根据参数初始化X264级别

	x264_picture_init(&m_picOutput);//初始化图片信息

	x264_picture_alloc(&m_picInput, X264_CSP_I420, m_width, m_height);//图片按I420格式分配空间，最后要x264_picture_clean 

	i_nal = 0;
	x264_encoder_headers(h, &nal_t, &i_nal);
	if (i_nal > 0)
	{
		unsigned char *pps = 0;
		int pps_len;
		unsigned char * sps = 0;
		int sps_len;

		for (int i = 0; i < i_nal; i++)
		{
			//获取SPS数据，PPS数据
			if (nal_t[i].i_type == NAL_SPS)
			{
				sps = new unsigned char[nal_t[i].i_payload - 4];
				sps_len = nal_t[i].i_payload - 4;
				memcpy(sps, nal_t[i].p_payload + 4, nal_t[i].i_payload - 4);
			}
			else if (nal_t[i].i_type == NAL_PPS)
			{
				pps = new unsigned char[nal_t[i].i_payload - 4];;
				pps_len = nal_t[i].i_payload - 4;
				memcpy(pps, nal_t[i].p_payload + 4, nal_t[i].i_payload - 4);
			}
		}

		int nResult = WriteSpsPPs(m_width, m_height, m_frameRate, m_audioChannel, m_audioSample, (char*)sps, sps_len, (char*)pps, pps_len);

		if (pps)
		{
			delete[] pps;
			pps = NULL;
		}

		if (sps)
		{
			delete[] sps;
			sps = NULL;
		}
		

		m_SwsContext= sws_getContext(m_width, m_height, AV_PIX_FMT_BGR24, m_width, m_height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);

		return nResult;
	}

	return 0;
}

//图片编码发送
int VideoRecord::WriteScreenCapture(BYTE * frame, unsigned long Stride, unsigned long StrideHeight, unsigned long timespan)
{
	int nDataLen = Stride * StrideHeight;

	uint8_t * rgb_buff = new uint8_t[nDataLen];
	memcpy(rgb_buff, frame, nDataLen);

	
	//下面的位图转完是倒立的
	uint8_t *rgb_src[3]= {rgb_buff, NULL, NULL};
	int rgb_stride[3]={3*m_width, 0, 0};
	sws_scale(m_SwsContext, rgb_src, rgb_stride, 0, m_height, m_picInput.img.plane, m_picInput.img.i_stride);
	
	delete[] rgb_buff;

	i_nal = 0;
	x264_encoder_encode(h, &nal_t, &i_nal, &m_picInput, &m_picOutput);
	//少这句的话会出现 x264 [warning]: non-strictly-monotonic PTS
	m_picInput.i_pts++;


	unsigned long durationTime = (timespan - lastFrameTime)*90;
	lastFrameTime = timespan;

	printf("当前时间戳  %ld.  \n", timespan);

	int rResult = 5;
	for (int i = 0; i < i_nal; i++)
	{
		int bKeyFrame = 0;
		//获取帧数据
		if (nal_t[i].i_type == NAL_SLICE || nal_t[i].i_type == NAL_SLICE_IDR)
		{
			if (nal_t[i].i_type == NAL_SLICE_IDR)
				bKeyFrame = 1;

			uint32_t* p = (uint32_t*)nal_t[i].p_payload;
			*p = htonl(nal_t[i].i_payload - 4);//大端,去掉头部四个字节


			/*if (MP4WriteSample(hMp4File, videoTrackId, (uint8_t *)nal_t[i].p_payload, nal_t[i].i_payload, durationTime, 0, 1))
				rResult = 1;
				else
				rResult = 0;*/

			if (m_pPreFrame)
			{
				if (MP4WriteSample(hMp4File, videoTrackId, (uint8_t *)m_pPreFrame, m_nPreFrameLen, durationTime, 0, 1))
					rResult = 1;
				else
					rResult = 0;

				delete[] m_pPreFrame;
				m_pPreFrame = NULL;
			}

			m_pPreFrame = new uint8_t[nal_t[i].i_payload];
			memcpy(m_pPreFrame, nal_t[i].p_payload, nal_t[i].i_payload);
			m_nPreFrameLen = nal_t[i].i_payload;
		}
	}

	return rResult;
}



long RTMPMOD_WriteAudioFrame(AudioEncodeObj* hObj, char* szBuf, unsigned long nBufLen, unsigned long nSampleRate, unsigned long nChannels, unsigned long timespan)
{
	

	AudioEncodeObj*	psObj = (AudioEncodeObj*)hObj;
	unsigned long			nDone = 0;
	unsigned long			nCopy;
	int						nRet;
	int						nResult;

	// check
	if ((!psObj) || (!szBuf) || (!nBufLen) || (!nSampleRate) || ((1 != nChannels) && (2 != nChannels)))
	{
		return 0;
	}

	// init
	if (!isInitFaacEncode)
	{
		return 0;
	}

	// encode and send
	while (nDone < nBufLen)
	{
		// copy
		nCopy = EB_MIN((nBufLen - nDone), (psObj->nPcmAudioSize - psObj->nPcmAudioLen));
		memcpy(psObj->szPcmAudio + psObj->nPcmAudioLen, szBuf + nDone, nCopy);
		nDone += nCopy;
		psObj->nPcmAudioLen += nCopy;

		

		// ready
		if (psObj->nPcmAudioLen == psObj->nPcmAudioSize)
		{
			// encode
			nRet = faacEncEncode(psObj->hEncoder, (int*)psObj->szPcmAudio, (psObj->nPcmAudioSize / PCM_BITSIZE) / psObj->nChannels,
				(unsigned char*)(psObj->szAacAudio), psObj->nAacAudioSize);
			psObj->nPcmAudioLen = 0;
			if (nRet <= 0)
			{
				continue;
			}
			psObj->nTimeStamp = timespan;

			
			if(MP4WriteSample(hMp4File, audioTrackId, (uint8_t *)(psObj->szAacAudio), nRet, MP4_INVALID_DURATION, 0, 1))
				nResult = 1;
			else
				nResult = 0;
		}
		
	}


	// success
	return nResult;
}



// 创建文件 成功返回0， 失败返回1
long InitMp4(char* fileName)
{
	hMp4File = MP4CreateEx(fileName, /*MP4_DETAILS_ALL*/MP4_CREATE_64BIT_DATA);//创建mp4文件
	if (hMp4File == MP4_INVALID_FILE_HANDLE)
	{
		printf("open file fialed.\n");
		return 1;
	}

	lastFrameTime = 0;

	return 0;
}

int WriteSpsPPs(int width, int height, int fps, int audiochannel, int audioSample, char* spsData, int spsLen, char* ppsData, int ppsLen)
{
	MP4SetTimeScale(hMp4File, 90000);

	//添加h264 track    
	videoTrackId = MP4AddH264VideoTrack(hMp4File, 90000,  MP4_INVALID_DURATION, width, height,
		spsData[1], //sps[1] AVCProfileIndication
		spsData[2], //sps[2] profile_compat
		spsData[3], //sps[3] AVCLevelIndication
		3); // 4 bytes length before each NAL unit

	if (videoTrackId == MP4_INVALID_TRACK_ID)
	{
		printf("add video track failed.\n");
		return 0;
	}
	MP4SetVideoProfileLevel(hMp4File, 0x7f);

	//添加aac音频
	if(audiochannel == 1)
		audioTrackId = MP4AddAudioTrack(hMp4File, audioSample*2, 2048, MP4_MPEG4_AUDIO_TYPE);
	else
		audioTrackId = MP4AddAudioTrack(hMp4File, audioSample, 2048, MP4_MPEG4_AUDIO_TYPE);
	if (audioTrackId == MP4_INVALID_TRACK_ID)
	{
		printf("add audio track failed.\n");
		return 0;
	}
	MP4SetAudioProfileLevel(hMp4File, 0x2);

	// write sps  
	MP4AddH264SequenceParameterSet(hMp4File, videoTrackId, (uint8_t *)spsData, spsLen);

	// write pps  
	MP4AddH264PictureParameterSet(hMp4File, videoTrackId, (uint8_t *)ppsData, ppsLen);

	//初始化音频解码器
	AudioEncoderInit(&pVideoRecord->m_AudioEncodeObj, audioSample, audiochannel);

	//设置音频解码信息
	MP4SetTrackESConfiguration(hMp4File, audioTrackId, (uint8_t*)pVideoRecord->m_pAudioConfig, pVideoRecord->m_nAudioConfigLen);

	return 1;
}


void CloseMp4File()
{
	MP4Close(hMp4File);
}


long RTMP_CreateMp4File(char* fileName, int audioChannel, int audioSample)
{
	int rResult = 0;

	rResult = InitMp4(fileName);
	if(rResult != 0)
		return 0;

	if (!pVideoRecord)
		pVideoRecord = new VideoRecord();

	pVideoRecord->FreeEncodeParams();

	pVideoRecord->m_audioChannel = audioChannel;
	pVideoRecord->m_audioSample = audioSample;

	return 1;
}
void RTMP_CloseMp4File()
{

	CloseMp4File();

	isInitFaacEncode = false;

	finishedFirstVideoImage = false;

	if (isInitFaacEncode)
	{
		// close
		AudioEncoderFree(&pVideoRecord->m_AudioEncodeObj);
	}

	if (pVideoRecord)
	{
		if (pVideoRecord->m_pAudioConfig)
		{
			delete pVideoRecord->m_pAudioConfig;
		}
		pVideoRecord->m_pAudioConfig = NULL;

		if (pVideoRecord->m_pPreFrame)
		{
			delete[] pVideoRecord->m_pPreFrame;
			pVideoRecord->m_pPreFrame = NULL;
		}
		pVideoRecord->m_nPreFrameLen = 0;

		pVideoRecord->FreeEncodeParams();
		delete pVideoRecord;
	}
	pVideoRecord = NULL;

	lastFrameTime = 0;
	
}

//初始化编码器
long RTMP_WriteVideoParams(unsigned long width, unsigned long height, unsigned long fps, unsigned long bitrate)
{
	int nResult = -1;
	if (!pVideoRecord)
	{
		return -1;
	}
	nResult = pVideoRecord->WriteVideoParams(width, height, fps, bitrate);
	return nResult;
}

//编码截图  对截图进行x264编码
long RTMP_WriteScreenCapture(char * frame, unsigned long Stride, unsigned long Height, unsigned long timespan)
{
	int nResult = -1;
	if (!pVideoRecord)
	{
		return -1;
	}
	
	__try
	{
		nResult = pVideoRecord->WriteScreenCapture((BYTE *)frame, Stride, Height, timespan);
		if (nResult == 1)
		{
			finishedFirstVideoImage = true;
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return nResult = -4;
	}

	return nResult;
}

//编码音频数据  原始数据为PCM
long RTMP_WriteAudioFrame(char* szBuf, unsigned long nBufLen, unsigned long nSampleRate, unsigned long nChannels, unsigned long timespan)
{
	int nResult = -1;
	if (!pVideoRecord)
	{
		return -1;
	}

	
	if (finishedFirstVideoImage)
	{
		__try
		{
			nResult = RTMPMOD_WriteAudioFrame(&pVideoRecord->m_AudioEncodeObj, szBuf, nBufLen, nSampleRate, nChannels, timespan);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return nResult = -5;
		}
	}

	return nResult;
}