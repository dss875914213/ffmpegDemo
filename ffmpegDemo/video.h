#pragma once
#include "player.h"
#include <windows.h>

static int QueuePicture(PlayerStation* is, AVFrame* sourceFrame, double pts, double duration, int64_t pos);
static int VideoDecodeFrame(AVCodecContext* pCodecContext, PacketQueue* pPacketQueue, AVFrame* frame);
static int VideoDecodeThread(void* arg);
static double ComputeTargetDelay(double delay, PlayerStation* is);
static double VpDuration(PlayerStation* is, Frame* vp, Frame* nextvp);
static void UpdateVideoPts(PlayerStation* is, double pts, int serial);
static void VideoDisplay(PlayerStation* is);
static void VideoRefresh(void* opaque, double* remainingTime);
static int VideoPlayingThread(void* arg);
static int OpenVideoPlaying(void* arg);
static int OpenVideoStream(PlayerStation* is);


int OpenVideo(PlayerStation* is);

class Video
{
public:
	Video();
	~Video();
	BOOL Open();
	void Close();
private:
	BOOL QueuePicture(AVFrame* sourceFrame, double pts, double duration, int64_t pos);
	int  DecodeFrame(AVCodecContext* pCodecContext, PacketQueue* pPacketQueue, AVFrame* frame);
	BOOL OnDecodeThread();
	BOOL OnPlayingThread();
	double ComputeTargetDelay(double delay);
	double VpDuration(Frame* vp, Frame* nextvp);
	void UpdatePts(double pts, int serial);
	void Display();
	void Refresh(double* remainingTime);
	int OpenPlaying();
	int OpenStream();
	static BOOL DecodeThread(void* arg);
	static BOOL PlayingThread(void* arg);

private:
	BOOL				m_stop;
	BOOL				m_paused;							// ��ͣ
	AVStream*			m_pStream;				// ��Ƶ��
	AVCodecContext*		m_pCodecContext;		// ��Ƶ������������	
	PlayClock			m_videoPlayClock;		// ��Ƶ����ʱ��
	double				m_frameTimer;			// ��ǰ֡��ʼ���ŵ�ʱ��
	PacketQueue			m_packetQueue;			// ��Ƶδ����֡����
	FrameQueue			m_frameQueue;			// ��Ƶ����֡����
	SwsContext*			m_swsContext;			// ��Ƶת��������
	AVFrame*			m_pFrameYUV;			// ת����ʽ�����Ƶ����
	SDL_Thread*			m_playThread;			// ��Ƶ��Ⱦ�߳�
	SDL_Thread*			m_decodeThread;			// ��Ƶ�����߳�
	SDLVideo			m_sdlVideo;
	AVFormatContext*	m_pFormatContext;		// ��ý�����������
	PlayClock			m_audioPlayClock;		// ��Ƶ����ʱ�� // -DSS TODO ����紫����
};
