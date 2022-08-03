#pragma once
#include <windows.h>
#include "player.h"

class Player;
class Audio
{
public:
	Audio();
	BOOL			Init(PacketQueue* pPacketQueue, Player* player);	// ��ʼ��
	BOOL			Open();	// �����߳�
	void			Close();	// �ر�
	PlayClock*		GetClock();	// ���ʱ�䣬����ͬ��
private:
	void			InitClock(PlayClock* clock);
	void			SetClockAt(PlayClock* clock, DOUBLE pts, DOUBLE time);
	void			SetClock(PlayClock* clock, DOUBLE pts);

	BOOL			OpenAudioStream();
	BOOL			OpenAudioPlaying();

	BOOL			OnDecodeThread();
	void			OnSDLAudioCallback(Uint8* stream, INT32 len);
	static BOOL		DecodeThread(void* arg);
	static void		SDLAudioCallback(void* opaque, UINT8* stream, INT32 len);

	BOOL			AudioDecodeFrame(AVCodecContext* pCodecContext, PacketQueue* pPacketQueue, AVFrame* frame);
	BOOL			AudioResample(INT64 callbackTime);


private:
	Player*				m_player;
	PacketQueue*		m_packetQueue;					// ��Ƶ����ǰ֡����
	FrameQueue			m_frameQueue;					// ��Ƶ�����֡����
	AVStream*			m_pStream;						// ��Ƶ��
	SwrContext*			m_swrContext;					// ��Ƶ��ʽת��������
	AudioParam			m_audioParamSource;				// Դ��Ƶ����(�������ݵĲ���)
	AudioParam			m_audioParamTarget;				// Ŀ����Ƶ����
	INT32				m_audioHardwareBufferSize;		// SDL ��Ƶ��������С����λ�ֽڣ�
	UINT8*				m_pAudioFrame;					// ָ������ŵ�һ֡��Ƶ���ݣ�ָ������ݽ�������SDL��Ƶ������
	UINT8*				m_pAudioFrameRwr;				// ��Ƶ�ز��������������
	UINT32				m_audioFrameSize;				// �����ŵ�һ֡��Ƶ���ݴ�С
	UINT32				m_audioFrameRwrSize;			// ���뵽����Ƶ������ pAudioFrameRwr ��С
	INT32				m_audioCopyIndex;				// ��ǰ��Ƶ֡���ѿ��� SDL ��Ƶ��������λ������
	INT32				m_audioWriteBufferSize;			// ��ǰ��Ƶ֡����δ���� SDL ��Ƶ�������������� audioFrameSize = audioCopyIndex+audioWriteBufferSize
	DOUBLE				m_audioClock;					// ��Ƶʱ��(��ǰ֡���Ž�����ʱ�䣿)
	SDL_Thread*			m_decodeThread;					// ��Ƶ�����߳�
	AVCodecContext*		m_pAudioCodecContext;			// ��Ƶ������������
	SDL_AudioDeviceID	m_audioDevice;
	PlayClock			m_audioPlayClock;				// ��Ƶ����ʱ��

};

