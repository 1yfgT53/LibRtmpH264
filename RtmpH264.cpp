// RtmpH264.cpp : ���� DLL Ӧ�ó���ĵ���������
//

#include "stdafx.h"
#include "RtmpH264.h"
#include "objbase.h"

MP4FileHandle hMp4File;
MP4TrackId videoTrackId;
MP4TrackId audioTrackId;
char* audioConfig = NULL;
long audioConfigLen = 0;

void stream_stop(RTMPMOD_SPublishObj* psObj);

long InitMp4(char* fileName);
void CloseMp4File();
void WriteSpsPPs(int width, int height, int fps, int audiochannel, int audioSample, char* spsData, int spsLen, char* ppsData, int ppsLen);


x264_t * h = NULL;//��������
x264_picture_t m_picInput;//����ͼ��YUV
x264_picture_t m_picOutput;//���ͼ��RAW
x264_param_t param;//��������

		
unsigned int  nSendCount = 0;
x264_nal_t* nal_t = NULL;
int i_nal = 0;
unsigned char *pps = 0;
int pps_len;
unsigned char * sps = 0;
int sps_len;

// Class constructor
RtmpH264::RtmpH264() : m_isCreatePublish(false),m_SwsContext(NULL)
{
	rtmp_PublishObj.hEncoder = 0;
	rtmp_PublishObj.nSampleRate = 0;
	rtmp_PublishObj.nChannels = 0;
	rtmp_PublishObj.nTimeDelta = 0;
	rtmp_PublishObj.szPcmAudio = 0;
	rtmp_PublishObj.nPcmAudioSize = 0;
	rtmp_PublishObj.nPcmAudioLen = 0;
	rtmp_PublishObj.szAacAudio = 0;
	rtmp_PublishObj.nAacAudioSize = 0;
}

//������������
int RtmpH264::CreatePublish(char* url, int outChunkSize)
{
	int rResult = 0;
	rResult = RTMP264_Connect(url, &rtmp_PublishObj.rtmp);
	if (rResult == 1)
	{
		m_isCreatePublish = true;
		m_startTime = ::GetTickCount();

		//�޸ķ��ͷְ��Ĵ�С  Ĭ��128�ֽ�
		RTMPPacket pack;
		RTMPPacket_Alloc(&pack, 4);
		pack.m_packetType = RTMP_PACKET_TYPE_CHUNK_SIZE;
		pack.m_nChannel = 0x02;
		pack.m_headerType = RTMP_PACKET_SIZE_LARGE;
		pack.m_nTimeStamp = 0;
		pack.m_nInfoField2 = 0;
		pack.m_nBodySize = 4;
		pack.m_body[3] = outChunkSize & 0xff; //���ֽ���
		pack.m_body[2] = outChunkSize >> 8;
		pack.m_body[1] = outChunkSize >> 16;
		pack.m_body[0] = outChunkSize >> 24;
		rtmp_PublishObj.rtmp->m_outChunkSize = outChunkSize;
		RTMP_SendPacket(rtmp_PublishObj.rtmp,&pack,1);
		RTMPPacket_Free(&pack);
	}

	return rResult;
}

//�Ͽ���������
void RtmpH264::DeletePublish()
{
	rtmp_PublishObj.hEncoder = 0;
	rtmp_PublishObj.nSampleRate = 0;
	rtmp_PublishObj.nChannels = 0;
	rtmp_PublishObj.nTimeDelta = 0;
	rtmp_PublishObj.szPcmAudio = 0;
	rtmp_PublishObj.nPcmAudioSize = 0;
	rtmp_PublishObj.nPcmAudioLen = 0;
	rtmp_PublishObj.szAacAudio = 0;
	rtmp_PublishObj.nAacAudioSize = 0;

	if (m_isCreatePublish)
	{
		RTMP264_Close();
	}
	m_isCreatePublish = false;
}

//��ʼ����Ƶ������
int RtmpH264::InitVideoParams(unsigned long width, unsigned long height, unsigned long fps, unsigned long bitrate, bool bConstantsBitrate = false)
{
	m_width = width;//������ʵ�������
	m_height = height;//��
	m_frameRate = fps;
	m_bitRate = bitrate;

	x264_param_default(&param);//����Ĭ�ϲ��������common/common.c


	//* ʹ��Ĭ�ϲ�������������Ϊ��ʵʱ���紫�䣬����ʹ����zerolatency��ѡ�ʹ�����ѡ��֮��Ͳ�����delayed_frames�����ʹ�õĲ��������Ļ�������Ҫ�ڱ������֮��õ�����ı���֡  
	x264_param_default_preset(&param, "veryfast", "zerolatency");

	//* cpuFlags
	param.i_threads = X264_SYNC_LOOKAHEAD_AUTO; /* ȡ�ջ���������ʹ�ò������ı�֤ */
	param.i_sync_lookahead = X264_SYNC_LOOKAHEAD_AUTO;

	//* ��Ƶѡ��
	param.i_width = m_width;
	param.i_height = m_height;
	param.i_frame_total = 0; //* ������֡��.��֪����0.
	param.i_keyint_min = 5;//�ؼ�֡��С���
	param.i_keyint_max = (int)fps*2;//�ؼ�֡�����
	param.b_annexb = 1;//1ǰ��Ϊ0x00000001,0Ϊnal����
	param.b_repeat_headers = 0;//�ؼ�֡ǰ���Ƿ��sps��pps֡��0 �� 1����	

	//param.vui.i_sar_width = m_width;
	//param.vui.i_sar_height = m_height;
	param.i_csp = X264_CSP_I420;


	//* B֡����
	param.i_bframe = 0;	//B֡
	param.b_open_gop = 0;
	param.i_bframe_pyramid = 0;
	param.i_bframe_adaptive = X264_B_ADAPT_FAST;

	//* ���ʿ��Ʋ��� 
	param.rc.i_lookahead = 0;
	param.rc.i_bitrate = (int)m_bitRate; //* ����(������,��λKbps)

	if(bConstantsBitrate)
	{
		param.rc.f_rf_constant = 10;	//rc.f_rf_constant��ʵ��������Խ��ͼ��Խ����ԽСԽ������
		param.rc.f_rf_constant_max = 35;
		param.rc.i_rc_method = X264_RC_ABR;//����i_rc_method��ʾ���ʿ��ƣ�CQP(�㶨����)��CRF(�㶨����)��ABR(ƽ������)
		param.rc.i_vbv_max_bitrate = (int)m_bitRate*1.1; // ƽ������ģʽ�£����˲ʱ���ʣ�Ĭ��0(��-B������ͬ)
	}
	else
	{
		param.rc.b_filler = 1;
		param.rc.f_rf_constant = 0.0f;;	//rc.f_rf_constant��ʵ��������Խ��ͼ��Խ����ԽСԽ������
		param.rc.i_rc_method = X264_RC_ABR;//����i_rc_method��ʾ���ʿ��ƣ�CQP(�㶨����)��CRF(�㶨����)��ABR(ƽ������)
		param.rc.i_vbv_max_bitrate = m_bitRate; // ƽ������ģʽ�£����˲ʱ���ʣ�Ĭ��0(��-B������ͬ)
		param.rc.i_vbv_buffer_size  = m_bitRate; //vbv-bufsize
	}

	//* muxing parameters
	param.i_fps_den = 1; // ֡�ʷ�ĸ
	param.i_fps_num = fps;// ֡�ʷ���
	param.i_timebase_num = 1;
	param.i_timebase_den = 1000;


	h = x264_encoder_open(&param);//���ݲ�����ʼ��X264����

	x264_picture_init(&m_picOutput);//��ʼ��ͼƬ��Ϣ
	x264_picture_alloc(&m_picInput, X264_CSP_I420, m_width, m_height);//ͼƬ��I420��ʽ����ռ䣬���Ҫx264_picture_clean 

	i_nal = 0;
	x264_encoder_headers(h, &nal_t, &i_nal);
	if (i_nal > 0)
	{
		for (int i = 0; i < i_nal; i++)
		{
			//��ȡSPS���ݣ�PPS����
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

		InitSpsPps(pps, pps_len, sps, sps_len, m_width, m_height, m_frameRate);

		m_SwsContext= sws_getContext(m_width, m_height, AV_PIX_FMT_BGR24, m_width, m_height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);

		return 1;
	}

	

	return 0;
}

//�ͷű������ڴ�
void RtmpH264::FreeEncodeParams()
{
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

	stream_stop(&rtmp_PublishObj);
	
}

//ͼƬ���뷢��
int RtmpH264::SendScreenCapture(BYTE * frame, unsigned long Stride, unsigned long StrideHeight, unsigned long timespan)
{

	int nDataLen = Stride * StrideHeight;

	uint8_t * rgb_buff = new uint8_t[nDataLen];
	memcpy(rgb_buff, frame, nDataLen);

	
	//�����λͼת���ǵ�����
	uint8_t *rgb_src[3]= {rgb_buff, NULL, NULL};
	int rgb_stride[3]={Stride, 0, 0};
	sws_scale(m_SwsContext, rgb_src, rgb_stride, 0, m_height, m_picInput.img.plane, m_picInput.img.i_stride);
	
	delete[] rgb_buff;
	
	i_nal = 0;
	x264_encoder_encode(h, &nal_t, &i_nal, &m_picInput, &m_picOutput);	
	m_picInput.i_pts++;//�����Ļ������ x264 [warning]: non-strictly-monotonic PTS

	int rResult = 5;
	for (int i = 0; i < i_nal; i++)
	{
		int bKeyFrame = 0;
		//��ȡ֡����
		if (nal_t[i].i_type == NAL_SLICE || nal_t[i].i_type == NAL_SLICE_IDR)
		{
			if (nal_t[i].i_type == NAL_SLICE_IDR)
				bKeyFrame = 1;


			rResult = SendH264Packet(nal_t[i].p_payload + 4, nal_t[i].i_payload - 4, bKeyFrame, timespan);
		}
	}

	return rResult;
}

//��ʼ����Ƶ������
int RtmpH264::WriteVideoParams(unsigned long width, unsigned long height, unsigned long fps, unsigned long bitrate)
{
	m_width = width;//������ʵ�������
	m_height = height;//��
	m_frameRate = fps;
	m_bitRate = bitrate;

	int yuvsize = m_height*m_width * 3 / 2;

	x264_param_default(&param);//����Ĭ�ϲ��������common/common.c


	//* ʹ��Ĭ�ϲ�������������Ϊ�ҵ���ʵʱ���紫�䣬������ʹ����zerolatency��ѡ�ʹ�����ѡ��֮��Ͳ�����delayed_frames�������ʹ�õĲ��������Ļ�������Ҫ�ڱ������֮��õ�����ı���֡  
	x264_param_default_preset(&param, "veryfast", "zerolatency");

	//* cpuFlags 
	param.i_threads = X264_SYNC_LOOKAHEAD_AUTO;//* ȡ�ջ���������ʹ�ò������ı�֤.  

	//* ��Ƶѡ��
	param.i_width = m_width;
	param.i_height = m_height;
	param.i_frame_total = 0; //* ������֡��.��֪����0.
	param.i_keyint_min = 0;//�ؼ�֡��С���
	param.i_keyint_max = fps;//�ؼ�֡�����
	param.b_annexb = 1;//1ǰ��Ϊ0x00000001,0Ϊnal����
	param.b_repeat_headers = 0;//�ؼ�֡ǰ���Ƿ��sps��pps֡��0 �� 1����	

	//param.vui.i_sar_width = m_width;
	//param.vui.i_sar_height = m_height;
	param.i_csp = X264_CSP_I420;


	//* ������
	param.i_bframe = 0;	//B֡
	param.b_open_gop = 0;
	param.i_bframe_pyramid = 0;
	param.i_bframe_adaptive = X264_B_ADAPT_FAST;

	//* ���ʿ��Ʋ��� 
	param.rc.i_lookahead = 0;
	param.rc.i_bitrate = bitrate; //* ����(������,��λKbps)
	param.rc.f_rf_constant = 5;	//rc.f_rf_constant��ʵ��������Խ��ͼ��Խ����ԽСԽ������
	param.rc.f_rf_constant_max = 25;
	param.rc.i_rc_method = X264_RC_ABR;//����i_rc_method��ʾ���ʿ��ƣ�CQP(�㶨����)��CRF(�㶨����)��ABR(ƽ������)
	param.rc.i_vbv_max_bitrate = (int)(m_bitRate*1.2); // ƽ������ģʽ�£����˲ʱ���ʣ�Ĭ��0(��-B������ͬ)
	param.rc.i_bitrate = (int)m_bitRate;

	//* muxing parameters
	param.i_fps_den = 1; // ֡�ʷ�ĸ
	param.i_fps_num = fps;// ֡�ʷ���
	param.i_timebase_num = 1;
	param.i_timebase_den = 1000;

	//* ����Profile.ʹ��Baseline profile  
	//x264_param_apply_profile(&param, x264_profile_names[0]);

	h = x264_encoder_open(&param);//���ݲ�����ʼ��X264����

	x264_picture_init(&m_picOutput);//��ʼ��ͼƬ��Ϣ
	//x264_picture_init(&m_picInput);//��ʼ��ͼƬ��Ϣ
	x264_picture_alloc(&m_picInput, X264_CSP_I420, m_width, m_height);//ͼƬ��I420��ʽ����ռ䣬���Ҫx264_picture_clean 

	i_nal = 0;
	x264_encoder_headers(h, &nal_t, &i_nal);
	if (i_nal > 0)
	{
		for (int i = 0; i < i_nal; i++)
		{
			//��ȡSPS���ݣ�PPS����
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

		WriteSpsPPs(m_width, m_height, m_frameRate, m_audioChannel, m_audioSample, (char*)sps, sps_len, (char*)pps, pps_len);

		m_SwsContext= sws_getContext(m_width, m_height, AV_PIX_FMT_BGR24, m_width, m_height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);

		return 1;
	}

	return 0;
}

unsigned long lastFrameTime = 0;

//ͼƬ���뷢��
int RtmpH264::WriteScreenCapture(BYTE * frame, unsigned long Stride, unsigned long StrideHeight, unsigned long timespan)
{
	int nDataLen = Stride * StrideHeight;

	uint8_t * rgb_buff = new uint8_t[nDataLen];
	memcpy(rgb_buff, frame, nDataLen);

	
	//�����λͼת���ǵ�����
	uint8_t *rgb_src[3]= {rgb_buff, NULL, NULL};
	int rgb_stride[3]={3*m_width, 0, 0};
	sws_scale(m_SwsContext, rgb_src, rgb_stride, 0, m_height, m_picInput.img.plane, m_picInput.img.i_stride);
	
	delete[] rgb_buff;

	i_nal = 0;
	x264_encoder_encode(h, &nal_t, &i_nal, &m_picInput, &m_picOutput);
	//�����Ļ������ x264 [warning]: non-strictly-monotonic PTS
	m_picInput.i_pts++;


	unsigned long durationTime = (timespan - lastFrameTime)*90;
	lastFrameTime = timespan;

	int rResult = 5;
	for (int i = 0; i < i_nal; i++)
	{
		int bKeyFrame = 0;
		//��ȡ֡����
		if (nal_t[i].i_type == NAL_SLICE || nal_t[i].i_type == NAL_SLICE_IDR)
		{
			if (nal_t[i].i_type == NAL_SLICE_IDR)
				bKeyFrame = 1;

			uint32_t* p = (uint32_t*) nal_t[i].p_payload;
			*p = htonl(nal_t[i].i_payload - 4);//���,ȥ��ͷ���ĸ��ֽ�
			if(MP4WriteSample(hMp4File, videoTrackId, (uint8_t *)nal_t[i].p_payload, nal_t[i].i_payload, durationTime, 0, 1))
				rResult = 1;
			else
				rResult = 0;

		}
	}

	return rResult;
}


#define EB_MAX(a,b)               (((a) > (b)) ? (a) : (b))
#define EB_MIN(a,b)               (((a) < (b)) ? (a) : (b))

#define PCM_BITSIZE               2
//ֹͣ��Ƶ����
void stream_stop(RTMPMOD_SPublishObj* psObj)
{
	int						nRet;
	if (psObj->hEncoder)
	{
		while (1)
		{
			nRet = faacEncEncode(psObj->hEncoder, 0, 0, (unsigned char*)(psObj->szAacAudio + RTMP_MAX_HEADER_SIZE + 2), psObj->nAacAudioSize - RTMP_MAX_HEADER_SIZE - 2);
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
}

//��ʼ����Ƶ������
bool stream_init(RTMPMOD_SPublishObj* psObj, unsigned long nSampleRate, unsigned long nChannels)
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

	// close
	stream_stop(psObj);

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
	nAacAudioSize = nMaxOutputBytes + RTMP_MAX_HEADER_SIZE + 2;
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
	memcpy(psObj->szAacAudio + RTMP_MAX_HEADER_SIZE + 2, buf, len);
	len += 2;
	psObj->szAacAudio[RTMP_MAX_HEADER_SIZE + 0] = (char)0xAF;
	psObj->szAacAudio[RTMP_MAX_HEADER_SIZE + 1] = (char)0x00;
	psObj->packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
	psObj->packet.m_packetType = RTMP_PACKET_TYPE_AUDIO;
	psObj->packet.m_hasAbsTimestamp = 0;
	psObj->packet.m_nChannel = 0x04;
	psObj->packet.m_nTimeStamp = psObj->nTimeStamp;
	psObj->packet.m_nInfoField2 = psObj->rtmp->m_stream_id;
	psObj->packet.m_nBodySize = len;
	psObj->packet.m_body = psObj->szAacAudio + RTMP_MAX_HEADER_SIZE;
	psObj->packet.m_nBytesRead = 0;

	// send packet
	//assert(psObj->rtmp);


	RTMP_SendPacket(psObj->rtmp, &psObj->packet, true);

	// success
	return true;
}

//��ʼ����Ƶ������
bool write_stream_init(RTMPMOD_SPublishObj* psObj, unsigned long nSampleRate, unsigned long nChannels)
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

	// close
	stream_stop(psObj);

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
	nAacAudioSize = nMaxOutputBytes + RTMP_MAX_HEADER_SIZE + 2;
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

	if (audioConfig)
	{
		free(audioConfig);
		audioConfig = NULL;
	}
	audioConfig = new char[len];
	memcpy(audioConfig, buf, len);
	audioConfigLen = len;
	

	memcpy(psObj->szAacAudio + RTMP_MAX_HEADER_SIZE + 2, buf, len);
	len += 2;
	psObj->szAacAudio[RTMP_MAX_HEADER_SIZE + 0] = (char)0xAF;
	psObj->szAacAudio[RTMP_MAX_HEADER_SIZE + 1] = (char)0x00;
	psObj->packet.m_headerType = RTMP_PACKET_SIZE_LARGE;
	psObj->packet.m_packetType = RTMP_PACKET_TYPE_AUDIO;
	psObj->packet.m_hasAbsTimestamp = 0;
	psObj->packet.m_nChannel = 0x04;
	psObj->packet.m_nTimeStamp = psObj->nTimeStamp;
	//psObj->packet.m_nInfoField2 = psObj->rtmp->m_stream_id;
	psObj->packet.m_nBodySize = len;
	psObj->packet.m_body = psObj->szAacAudio + RTMP_MAX_HEADER_SIZE;
	psObj->packet.m_nBytesRead = 0;

	// send packet
	//assert(psObj->rtmp);

	// success
	return true;
}

		
//���������Ƶ����
long RTMPMOD_PublishSendAudio(RTMPMOD_SPublishObj* hObj, char* szBuf, unsigned long nBufLen, unsigned long nSampleRate, unsigned long nChannels, unsigned long timespan)
{
	

	RTMPMOD_SPublishObj*	psObj = (RTMPMOD_SPublishObj*)hObj;
	unsigned long			nDone = 0;
	unsigned long			nCopy;
	int						nRet;

	// check
	if ((!psObj) || (!szBuf) || (!nBufLen) || (!nSampleRate) || ((1 != nChannels) && (2 != nChannels)))
	{
		return 0;
	}

	// init
	if (!psObj->rtmp)
	{
		return 0;
	}
	if (!stream_init(psObj, nSampleRate, nChannels))
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
				(unsigned char*)(psObj->szAacAudio + RTMP_MAX_HEADER_SIZE + 2), psObj->nAacAudioSize - RTMP_MAX_HEADER_SIZE - 2);
			psObj->nPcmAudioLen = 0;
			if (nRet <= 0)
			{
				continue;
			}
			psObj->nTimeStamp = timespan;
			//psObj->nTimeStamp += psObj->nTimeDelta;

			// packet
			psObj->szAacAudio[RTMP_MAX_HEADER_SIZE + 0] = (char)0xAF;
			psObj->szAacAudio[RTMP_MAX_HEADER_SIZE + 1] = (char)0x01;
			psObj->packet.m_headerType = RTMP_PACKET_SIZE_MEDIUM;
			psObj->packet.m_packetType = RTMP_PACKET_TYPE_AUDIO;
			psObj->packet.m_hasAbsTimestamp = 0;
			psObj->packet.m_nChannel = 0x04;
			psObj->packet.m_nTimeStamp = psObj->nTimeStamp;
			psObj->packet.m_nInfoField2 = psObj->rtmp->m_stream_id;
			psObj->packet.m_nBodySize = nRet + 2;
			psObj->packet.m_body = psObj->szAacAudio + RTMP_MAX_HEADER_SIZE;
			psObj->packet.m_nBytesRead = 0;


			// send packet
			if (!RTMP_SendPacket(psObj->rtmp, &psObj->packet, true))
			{
				return 0;
			}
		}
		
	}


	// success
	return 1;
}

long RTMPMOD_WriteSendAudio(RTMPMOD_SPublishObj* hObj, char* szBuf, unsigned long nBufLen, unsigned long nSampleRate, unsigned long nChannels, unsigned long timespan)
{
	

	RTMPMOD_SPublishObj*	psObj = (RTMPMOD_SPublishObj*)hObj;
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
	if (!write_stream_init(psObj, nSampleRate, nChannels))
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
				(unsigned char*)(psObj->szAacAudio + RTMP_MAX_HEADER_SIZE + 2), psObj->nAacAudioSize - RTMP_MAX_HEADER_SIZE - 2);
			psObj->nPcmAudioLen = 0;
			if (nRet <= 0)
			{
				continue;
			}
			psObj->nTimeStamp = timespan;
			//psObj->nTimeStamp += psObj->nTimeDelta;

			// packet
			psObj->szAacAudio[RTMP_MAX_HEADER_SIZE + 0] = (char)0xAF;
			psObj->szAacAudio[RTMP_MAX_HEADER_SIZE + 1] = (char)0x01;
			psObj->packet.m_headerType = RTMP_PACKET_SIZE_MEDIUM;
			psObj->packet.m_packetType = RTMP_PACKET_TYPE_AUDIO;
			psObj->packet.m_hasAbsTimestamp = 0;
			psObj->packet.m_nChannel = 0x04;
			psObj->packet.m_nTimeStamp = psObj->nTimeStamp;
			//psObj->packet.m_nInfoField2 = psObj->rtmp->m_stream_id;
			psObj->packet.m_nBodySize = nRet + 2;
			psObj->packet.m_body = psObj->szAacAudio + RTMP_MAX_HEADER_SIZE;
			psObj->packet.m_nBytesRead = 0;

			
			if(MP4WriteSample(hMp4File, audioTrackId, (uint8_t *)(psObj->szAacAudio + RTMP_MAX_HEADER_SIZE + 2), nRet, MP4_INVALID_DURATION, 0, 1))
				nResult = 1;
			else
				nResult = 0;
		}
		
	}

	

	// success
	return nResult;
}


RtmpH264* pRtmpH264 = NULL;

//��������������
long RTMP_CreatePublish(char* url,unsigned long outChunkSize)
{
	int nResult = -1;
	if (pRtmpH264)
	{
		pRtmpH264->FreeEncodeParams();
		pRtmpH264->DeletePublish();

		delete pRtmpH264;
	}

	pRtmpH264 = new RtmpH264();
	nResult = pRtmpH264->CreatePublish(url, (int)outChunkSize);
	if (nResult != 1)
		pRtmpH264->m_isCreatePublish = false;

	return nResult;
}

//�Ͽ�������
void RTMP_DeletePublish()
{
	if (pRtmpH264)
	{
		pRtmpH264->FreeEncodeParams();
		pRtmpH264->DeletePublish();
		delete pRtmpH264;
	}
	pRtmpH264 = NULL;
}

//��ʼ��������
long RTMP_InitVideoParams(unsigned long width, unsigned long height, unsigned long fps, unsigned long bitrate, bool bConstantsBitrate)
{
	int nResult = -1;
	if (!pRtmpH264)
	{
		return -1;
	}
	__try
	{
		nResult = pRtmpH264->InitVideoParams(width, height, fps, bitrate, bConstantsBitrate);		
	}
	__except( EXCEPTION_EXECUTE_HANDLER ) 
	{
		nResult = -1;
		pRtmpH264->m_isCreatePublish = false;
		RTMP_DeletePublish();
		throw(-1);
	}

	if (nResult != 1)
	{
		pRtmpH264->m_isCreatePublish = false;
		RTMP_DeletePublish();		
	}

	return nResult;
}

//���ͽ�ͼ  �Խ�ͼ����x264����
long RTMP_SendScreenCapture(char * frame, unsigned long Stride, unsigned long Height, unsigned long timespan)
{
	int nResult = -1;
	if (!pRtmpH264)
	{
		return -1;
	}

	__try
	{
		if (!pRtmpH264->m_isCreatePublish)
			return -1;
		nResult = pRtmpH264->SendScreenCapture((BYTE *)frame, Stride, Height, timespan);
	}
	__except( EXCEPTION_EXECUTE_HANDLER ) 
	{
		nResult = -1;
		pRtmpH264->m_isCreatePublish = false;
		RTMP_DeletePublish();
		throw(-1);
	}

	if (nResult != 1)
	{
		pRtmpH264->m_isCreatePublish = false;
		RTMP_DeletePublish();		
	}
	
	return nResult;
}

//������Ƶ����  ԭʼ����ΪPCM
long RTMP_SendAudioFrame(char* szBuf, unsigned long nBufLen, unsigned long nSampleRate, unsigned long nChannels, unsigned long timespan)
{
	int nResult = -1;
	if (!pRtmpH264)
	{
		return -1;
	}
	__try
	{
		if (!pRtmpH264->m_isCreatePublish)
			return -1;
		nResult = RTMPMOD_PublishSendAudio(&pRtmpH264->rtmp_PublishObj, szBuf, nBufLen, nSampleRate, nChannels, timespan);
	}
	__except( EXCEPTION_EXECUTE_HANDLER ) 
	{
		nResult = -1;
		pRtmpH264->m_isCreatePublish = false;
		RTMP_DeletePublish();
		throw(-1);
	}
	if (nResult != 1)
	{
		pRtmpH264->m_isCreatePublish = false;
		RTMP_DeletePublish();		
	}
	
	return nResult;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////
///
///
/////////////////////////////////////////////////////////////////////////////////////////////////////


// �����ļ� �ɹ�����0�� ʧ�ܷ���1
long InitMp4(char* fileName)
{
	hMp4File = MP4CreateEx(fileName, /*MP4_DETAILS_ALL*/MP4_CREATE_64BIT_DATA);//����mp4�ļ�
	if (hMp4File == MP4_INVALID_FILE_HANDLE)
	{
		printf("open file fialed.\n");
		return 1;
	}

	return 0;
}

void WriteSpsPPs(int width, int height, int fps, int audiochannel, int audioSample, char* spsData, int spsLen, char* ppsData, int ppsLen)
{
	MP4SetTimeScale(hMp4File, 90000);

	//���h264 track    
	videoTrackId = MP4AddH264VideoTrack(hMp4File, 90000,  MP4_INVALID_DURATION, width, height,
		spsData[1], //sps[1] AVCProfileIndication
		spsData[2], //sps[2] profile_compat
		spsData[3], //sps[3] AVCLevelIndication
		3); // 4 bytes length before each NAL unit

	if (videoTrackId == MP4_INVALID_TRACK_ID)
	{
		printf("add video track failed.\n");
		return;
	}
	MP4SetVideoProfileLevel(hMp4File, 0x7f);

	//���aac��Ƶ
	if(audiochannel == 1)
		audioTrackId = MP4AddAudioTrack(hMp4File, audioSample*2, 2048, MP4_MPEG4_AUDIO_TYPE);
	else
		audioTrackId = MP4AddAudioTrack(hMp4File, audioSample, 2048, MP4_MPEG4_AUDIO_TYPE);
	if (audioTrackId == MP4_INVALID_TRACK_ID)
	{
		printf("add audio track failed.\n");
		return;
	}
	MP4SetAudioProfileLevel(hMp4File, 0x2);

	// write sps  
	MP4AddH264SequenceParameterSet(hMp4File, videoTrackId, (uint8_t *)spsData, spsLen);

	// write pps  
	MP4AddH264PictureParameterSet(hMp4File, videoTrackId, (uint8_t *)ppsData, ppsLen);

	//��ʼ����Ƶ������
	write_stream_init(&pRtmpH264->rtmp_PublishObj, audioSample, audiochannel);

	//������Ƶ������Ϣ
	MP4SetTrackESConfiguration(hMp4File, audioTrackId, (uint8_t*)audioConfig, audioConfigLen);
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

	if (!pRtmpH264)
		pRtmpH264 = new RtmpH264();

	pRtmpH264->FreeEncodeParams();
	pRtmpH264->m_isCreatePublish = true;
	pRtmpH264->m_type = 1;
	pRtmpH264->m_audioChannel = audioChannel;
	pRtmpH264->m_audioSample = audioSample;

	return 1;
}
void RTMP_CloseMp4File()
{
	if(audioConfig)
	{
		delete audioConfig;
	}
	audioConfig = NULL;

	if (pRtmpH264)
	{
		pRtmpH264->FreeEncodeParams();
		delete pRtmpH264;
	}
	pRtmpH264 = NULL;


	CloseMp4File();
}

//��ʼ��������
long RTMP_WriteVideoParams(unsigned long width, unsigned long height, unsigned long fps, unsigned long bitrate)
{
	int nResult = -1;
	if (!pRtmpH264)
	{
		return -1;
	}
	nResult = pRtmpH264->WriteVideoParams(width, height, fps, bitrate);
	return nResult;
}

//���ͽ�ͼ  �Խ�ͼ����x264����
long RTMP_WriteScreenCapture(char * frame, unsigned long Stride, unsigned long Height, unsigned long timespan)
{
	int nResult = -1;
	if (!pRtmpH264)
	{
		return -1;
	}
	nResult = pRtmpH264->WriteScreenCapture((BYTE *)frame, Stride, Height, timespan);
	return nResult;
}

//������Ƶ����  ԭʼ����ΪPCM
long RTMP_WriteAudioFrame(char* szBuf, unsigned long nBufLen, unsigned long nSampleRate, unsigned long nChannels, unsigned long timespan)
{
	int nResult = -1;
	if (!pRtmpH264)
	{
		return -1;
	}
	nResult = RTMPMOD_WriteSendAudio(&pRtmpH264->rtmp_PublishObj, szBuf, nBufLen, nSampleRate, nChannels, timespan);
	return nResult;
}