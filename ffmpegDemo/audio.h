#pragma once
#include <windows.h>
#include "player.h"

class Player;
class Audio
{
public:
	Audio();
	BOOL Init(PacketQueue* pPacketQueue, Player* player);
	void DeInit();
	BOOL Open();
	void Close();
	PlayClock* GetClock();
private:
	BOOL OnDecodeThread();
	void SetClockAt(PlayClock* clock, double pts, int serial, double time);
	void SetClock(PlayClock* clock, double pts, int serial);
	void InitClock(PlayClock* clock, int* queueSerial);
	void OnSDLAudioCallback(Uint8* stream, int len);
	int	 AudioDecodeFrame(AVCodecContext* pCodecContext, PacketQueue* pPacketQueue, AVFrame* frame);
	BOOL OpenAudioStream();
	BOOL AudioResample(INT64 callbackTime);
	BOOL OpenAudioPlaying();
	static BOOL DecodeThread(void* arg);
	static void SDLAudioCallback(void* opaque, Uint8* stream, int len);

private:
	Player*				m_player;
	PacketQueue*		m_packetQueue;					// ��Ƶ����ǰ֡����

	FrameQueue			m_frameQueue;					// ��Ƶ�����֡����


	AVStream*			m_pStream;					// ��Ƶ��

	SwrContext*			m_swrContext;					// ��Ƶ��ʽת��������
	AudioParam			m_audioParamSource;				// Դ��Ƶ����(�������ݵĲ���)
	AudioParam			m_audioParamTarget;				// Ŀ����Ƶ����

	int					m_audioHardwareBufferSize;		// SDL ��Ƶ��������С����λ�ֽڣ�
	uint8_t*			m_pAudioFrame;					// ָ������ŵ�һ֡��Ƶ���ݣ�ָ������ݽ�������SDL��Ƶ������
	uint8_t*			m_pAudioFrameRwr;				// ��Ƶ�ز��������������
	unsigned int		m_audioFrameSize;				// �����ŵ�һ֡��Ƶ���ݴ�С
	unsigned int		m_audioFrameRwrSize;			// ���뵽����Ƶ������ pAudioFrameRwr ��С
	int					m_audioCopyIndex;				// ��ǰ��Ƶ֡���ѿ��� SDL ��Ƶ��������λ������
	int					m_audioWriteBufferSize;			// ��ǰ��Ƶ֡����δ���� SDL ��Ƶ�������������� audioFrameSize = audioCopyIndex+audioWriteBufferSize
	double				m_audioClock;					// ��Ƶʱ��(��ǰ֡���Ž�����ʱ�䣿)
	int					m_audioClockSerial;
	SDL_Thread*			m_decodeThread;					// ��Ƶ�����߳�
	AVCodecContext*		m_pAudioCodecContext;			// ��Ƶ������������
	SDL_AudioDeviceID	m_audioDevice;
	PlayClock			m_audioPlayClock;				// ��Ƶ����ʱ��

};

