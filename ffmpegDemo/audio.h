#pragma once
#include <windows.h>
#include "player.h"

class Audio
{
public:
	Audio();
	BOOL Open();
	void Close();

private:
	BOOL OnDecodeThread();
	int	 AudioDecodeFrame(AVCodecContext* pCodecContext, PacketQueue* pPacketQueue, AVFrame* frame);
	BOOL OpenAudioStream();
	BOOL AudioResample(INT64 callbackTime);
	BOOL OpenAudioPlaying();
	static BOOL DecodeThread(void* arg);

private:
	FrameQueue		m_frameQueue;					// ��Ƶ�����֡����
	PacketQueue		m_packetQueue;					// ��Ƶ����ǰ֡����
	BOOL			m_stop;
	AVStream*		m_pAudioStream;					// ��Ƶ��
	SwrContext*		m_swrContext;					// ��Ƶ��ʽת��������
	AudioParam		m_audioParamSource;				// Դ��Ƶ����(�������ݵĲ���)
	AudioParam		m_audioParamTarget;				// Ŀ����Ƶ����
	int				m_audioHardwareBufferSize;		// SDL ��Ƶ��������С����λ�ֽڣ�
	uint8_t*		m_pAudioFrame;					// ָ������ŵ�һ֡��Ƶ���ݣ�ָ������ݽ�������SDL��Ƶ������
	uint8_t*		m_pAudioFrameRwr;				// ��Ƶ�ز��������������
	unsigned int	m_audioFrameSize;				// �����ŵ�һ֡��Ƶ���ݴ�С
	unsigned int	m_audioFrameRwrSize;			// ���뵽����Ƶ������ pAudioFrameRwr ��С
	int				m_audioCopyIndex;				// ��ǰ��Ƶ֡���ѿ��� SDL ��Ƶ��������λ������
	int				m_audioWriteBufferSize;			// ��ǰ��Ƶ֡����δ���� SDL ��Ƶ�������������� audioFrameSize = audioCopyIndex+audioWriteBufferSize
	double			m_audioClock;					// ��Ƶʱ��(��ǰ֡���Ž�����ʱ�䣿)
	int				m_audioClockSerial;
	SDL_Thread*		m_decodeThread;					// ��Ƶ�����߳�
	AVCodecContext* m_pAudioCodecContext;			// ��Ƶ������������
	SDL_AudioDeviceID m_audioDevice;

};

