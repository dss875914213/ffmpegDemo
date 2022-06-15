#pragma once
#include "player.h"
#include <windows.h>

class Player;
class Video
{
public:
	Video();
	~Video();
	BOOL Init(PacketQueue* videoPacketQueue, Player* player);
	BOOL Open();
	void Close();
	void Destroy();
	void TogglePause();
private:
	double GetClock(PlayClock* clock);
	void SetClockAt(PlayClock* clock, double pts, int serial, double time);
	void SetClock(PlayClock* clock, double pts, int serial);
	void InitClock(PlayClock* clock, int* queueSerial);
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
	Player*				m_player;
	PacketQueue*		m_packetQueue;			// ��Ƶδ����֡����

	AVStream*			m_pStream;				// ��Ƶ��
	AVCodecContext*		m_pCodecContext;		// ��Ƶ������������	
	PlayClock			m_videoPlayClock;		// ��Ƶ����ʱ��
	double				m_frameTimer;			// ��ǰ֡��ʼ���ŵ�ʱ��
	
	FrameQueue			m_frameQueue;			// ��Ƶ����֡����
	SwsContext*			m_swsContext;			// ��Ƶת��������
	AVFrame*			m_pFrameYUV;			// ת����ʽ�����Ƶ����
	SDL_Thread*			m_playThread;			// ��Ƶ��Ⱦ�߳�
	SDL_Thread*			m_decodeThread;			// ��Ƶ�����߳�
	SDLVideo			m_sdlVideo;
	AVFormatContext*	m_pFormatContext;		// ��ý�����������
	PlayClock*			m_audioPlayClock;		// ��Ƶ����ʱ�� // -DSS TODO ����紫����

};


