
#pragma once

#include "faac.h"

#include "librtmp_send264.h"

//#ifdef  _BASEFUNC_EXPORT_
#define  BASE_API  extern "C" __declspec(dllexport)
//#else
//#define  BASE_API   __declspec(dllimport)
//#endif

#define __STDC_CONSTANT_MACROS

extern "C" {
#include "x264.h"
#include "x264_config.h"
#include "mp4v2\mp4v2.h"
#include "libswscale\swscale.h"
#include "libavutil\opt.h"
#include "libavutil\imgutils.h"
}


typedef struct
{
	// rtmp object
	char*				szUrl;
	RTMP*				rtmp;
	RTMPPacket			packet;
	// faac encoder
	faacEncHandle		hEncoder;
	unsigned long       nSampleRate;
	unsigned long       nChannels;
	unsigned long       nTimeStamp;
	unsigned long       nTimeDelta;
	char*				szPcmAudio;
	unsigned long       nPcmAudioSize;
	unsigned long       nPcmAudioLen;
	char*				szAacAudio;
	unsigned long       nAacAudioSize;
}RTMPMOD_SPublishObj;

typedef unsigned long long  QWORD, ULONG64, UINT64, ULONGLONG;

class RtmpH264
{

public:
	RtmpH264(void);


	~RtmpH264()
	{
	}

	int CreatePublish(char* url, int outChunkSize);
	void DeletePublish();

	int InitVideoParams(unsigned long width, unsigned long height, unsigned long fps, unsigned long bitrate, bool bConstantsBitrate);
	int SendScreenCapture(BYTE * frame, unsigned long Stride, unsigned long Height, unsigned long timespan);

	int WriteVideoParams(unsigned long width, unsigned long height, unsigned long fps, unsigned long bitrate);
	int WriteScreenCapture(BYTE * frame, unsigned long Stride, unsigned long Height, unsigned long timespan);

	void FreeEncodeParams(); //释放占用内存资源

public:
	DWORD m_startTime;
	RTMPMOD_SPublishObj rtmp_PublishObj;

	struct SwsContext * m_SwsContext;

	int m_width;	//宽度
	int m_height;	// 高度
	int	m_frameRate;	//帧率fps
	int m_bitRate;	//比特率

	int m_audioChannel;	//声道数
	int m_audioSample;	//音频采样率

	int m_type;//0 rtmp推流模式， 1 mp4录像模式
	bool m_isCreatePublish;
};

BASE_API long RTMP_CreatePublish(char* url, unsigned long outChunkSize);
BASE_API void RTMP_DeletePublish();
BASE_API long RTMP_InitVideoParams(unsigned long width, unsigned long height, unsigned long fps, unsigned long bitrate,bool bConstantsBitrate = false);
BASE_API long RTMP_SendScreenCapture(char * frame, unsigned long Stride, unsigned long Height, unsigned long timespan);
BASE_API long RTMP_SendAudioFrame(char* szBuf, unsigned long nBufLen, unsigned long nSampleRate, unsigned long nChannels, unsigned long timespan);

BASE_API long RTMP_CreateMp4File(char* fileName, int audioChannel, int audioSample);
BASE_API void RTMP_CloseMp4File();
BASE_API long RTMP_WriteVideoParams(unsigned long width, unsigned long height, unsigned long fps, unsigned long bitrate);
BASE_API long RTMP_WriteScreenCapture(char * frame, unsigned long Stride, unsigned long Height, unsigned long timespan);
BASE_API long RTMP_WriteAudioFrame(char* szBuf, unsigned long nBufLen, unsigned long nSampleRate, unsigned long nChannels, unsigned long timespan);