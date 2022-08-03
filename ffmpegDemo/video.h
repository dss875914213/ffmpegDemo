#pragma once
#include "player.h"
#include <windows.h>

class Player;
class Video
{
public:
	Video();
	~Video();
	BOOL			Init(PacketQueue* videoPacketQueue, Player* player);	// ��ʼ��
	BOOL			Open();	// �����߳�
	void			Close();	// �ر�
	void			Pause();
private:
	void			InitClock(PlayClock* clock);
	double			GetClock(PlayClock* clock);
	void			SetClockAt(PlayClock* clock, DOUBLE pts, DOUBLE time);
	void			SetClock(PlayClock* clock, DOUBLE pts);

	int				OpenPlaying();
	int				OpenStream();

	double			ComputeTargetDelay(DOUBLE delay);
	double			VpDuration(Frame* vp, Frame* nextvp);
	void			UpdatePts(DOUBLE pts);

	BOOL			OnDecodeThread();
	BOOL			OnPlayingThread();
	static BOOL		DecodeThread(void* arg);
	static BOOL		PlayingThread(void* arg);

	void			Refresh(DOUBLE* remainingTime);
	void			Display();
	BOOL			QueuePicture(AVFrame* sourceFrame, DOUBLE pts, DOUBLE duration, INT64 pos);
	INT32			DecodeFrame(AVCodecContext* pCodecContext, PacketQueue* pPacketQueue, AVFrame* frame);

private:
	Player*				m_player;
	PacketQueue*		m_packetQueue;			// ��Ƶδ����֡����

	AVStream*			m_pStream;				// ��Ƶ��
	AVCodecContext*		m_pCodecContext;		// ��Ƶ������������	
	PlayClock			m_videoPlayClock;		// ��Ƶ����ʱ��
	DOUBLE				m_frameTimer;			// ��ǰ֡��ʼ���ŵ�ʱ��
	
	FrameQueue			m_frameQueue;			// ��Ƶ����֡����
	SwsContext*			m_swsContext;			// ��Ƶת��������
	AVFrame*			m_pFrameYUV;			// ת����ʽ�����Ƶ����
	SDL_Thread*			m_playThread;			// ��Ƶ��Ⱦ�߳�
	SDL_Thread*			m_decodeThread;			// ��Ƶ�����߳�
	SDLVideo			m_sdlVideo;
	AVFormatContext*	m_pFormatContext;		// ��ý�����������
	PlayClock*			m_audioPlayClock;		// ��Ƶ����ʱ�� 

};


