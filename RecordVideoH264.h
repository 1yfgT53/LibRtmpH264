
#pragma once

#include "faac.h"
#include <winbase.h>
#include <winsock.h>

#ifdef  _BASEFUNC_EXPORT_
#define  BASE_API  extern "C" __declspec(dllexport)
#else
#define  BASE_API   __declspec(dllimport)
#endif

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
}AudioEncodeObj;

typedef unsigned long long  QWORD, ULONG64, UINT64, ULONGLONG;

class VideoRecord
{

public:
	VideoRecord(void);


	~VideoRecord()
	{
	}

	int WriteVideoParams(unsigned long width, unsigned long height, unsigned long fps, unsigned long bitrate);
	int WriteScreenCapture(BYTE * frame, unsigned long Stride, unsigned long Height, unsigned long timespan);

	void FreeEncodeParams(); //释放占用内存资源

public:
	DWORD m_startTime;
	AudioEncodeObj m_AudioEncodeObj;

	struct SwsContext * m_SwsContext;

	int m_width;	//宽度
	int m_height;	// 高度
	int	m_frameRate;	//帧率fps
	int m_bitRate;	//比特率

	int m_audioChannel;	//声道数
	int m_audioSample;	//音频采样率

	int i_nal ;

	x264_t * h;//对象句柄，
	x264_picture_t m_picInput;//传入图像YUV
	x264_picture_t m_picOutput;//输出图像RAW
	x264_param_t param;//参数设置

	x264_nal_t* nal_t;

	char* m_pAudioConfig;
	long m_nAudioConfigLen;

	uint8_t * m_pPreFrame;
	int m_nPreFrameLen;
};


/*创建MP4文件
*<param name="fileName">文件名</param>
*<param name="audioSample">采样率</param>
*<param name="audioChannel">声道数</param>
*/
BASE_API long RTMP_CreateMp4File(char* fileName, int audioChannel, int audioSample);
BASE_API void RTMP_CloseMp4File();

/*初始化编码参数
*<param name="width">视频宽度</param>
*<param name="height">视频高度</param>
*<param name="fps">帧率</param>
*<param name="bitrate">比特率</param>
*/
BASE_API long RTMP_WriteVideoParams(unsigned long width, unsigned long height, unsigned long fps, unsigned long bitrate);


/*编码图像数据并写入文件
*<param name="szBuf">音频数据地址</param>
*<param name="nBufLen">音频数据长度 单位字节</param>
*<param name="nSampleRate">采样率</param>
*<param name="nChannels">声道数</param>
*<param name="timespan">时间戳</param>
*/
BASE_API long RTMP_WriteScreenCapture(char * frame, unsigned long Stride, unsigned long Height, unsigned long timespan);


/*编码音频数据并写入文件
*<param name="szBuf">音频数据地址</param>
*<param name="nBufLen">音频数据长度 单位字节</param>
*<param name="nSampleRate">采样率</param>
*<param name="nChannels">声道数</param>
*<param name="timespan">时间戳</param>
*/
BASE_API long RTMP_WriteAudioFrame(char* szBuf, unsigned long nBufLen, unsigned long nSampleRate, unsigned long nChannels, unsigned long timespan);