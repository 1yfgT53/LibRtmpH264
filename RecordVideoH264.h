
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

	void FreeEncodeParams(); //�ͷ�ռ���ڴ���Դ

public:
	DWORD m_startTime;
	AudioEncodeObj m_AudioEncodeObj;

	struct SwsContext * m_SwsContext;

	int m_width;	//���
	int m_height;	// �߶�
	int	m_frameRate;	//֡��fps
	int m_bitRate;	//������

	int m_audioChannel;	//������
	int m_audioSample;	//��Ƶ������

	int i_nal ;

	x264_t * h;//��������
	x264_picture_t m_picInput;//����ͼ��YUV
	x264_picture_t m_picOutput;//���ͼ��RAW
	x264_param_t param;//��������

	x264_nal_t* nal_t;

	char* m_pAudioConfig;
	long m_nAudioConfigLen;

	uint8_t * m_pPreFrame;
	int m_nPreFrameLen;
};


/*����MP4�ļ�
*<param name="fileName">�ļ���</param>
*<param name="audioSample">������</param>
*<param name="audioChannel">������</param>
*/
BASE_API long RTMP_CreateMp4File(char* fileName, int audioChannel, int audioSample);
BASE_API void RTMP_CloseMp4File();

/*��ʼ���������
*<param name="width">��Ƶ���</param>
*<param name="height">��Ƶ�߶�</param>
*<param name="fps">֡��</param>
*<param name="bitrate">������</param>
*/
BASE_API long RTMP_WriteVideoParams(unsigned long width, unsigned long height, unsigned long fps, unsigned long bitrate);


/*����ͼ�����ݲ�д���ļ�
*<param name="szBuf">��Ƶ���ݵ�ַ</param>
*<param name="nBufLen">��Ƶ���ݳ��� ��λ�ֽ�</param>
*<param name="nSampleRate">������</param>
*<param name="nChannels">������</param>
*<param name="timespan">ʱ���</param>
*/
BASE_API long RTMP_WriteScreenCapture(char * frame, unsigned long Stride, unsigned long Height, unsigned long timespan);


/*������Ƶ���ݲ�д���ļ�
*<param name="szBuf">��Ƶ���ݵ�ַ</param>
*<param name="nBufLen">��Ƶ���ݳ��� ��λ�ֽ�</param>
*<param name="nSampleRate">������</param>
*<param name="nChannels">������</param>
*<param name="timespan">ʱ���</param>
*/
BASE_API long RTMP_WriteAudioFrame(char* szBuf, unsigned long nBufLen, unsigned long nSampleRate, unsigned long nChannels, unsigned long timespan);