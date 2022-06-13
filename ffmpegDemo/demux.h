#pragma once
#include <string>
#include <windows.h>
#include "player.h"

using namespace std;

class  FFDemux
{
public:
	FFDemux(string filename);
	~FFDemux();
	BOOL Init();
	BOOL DemuxDeinit();
	BOOL Open();
	BOOL Close();
	void Stop(BOOL flag);

private:
	BOOL StreamHasEnoughPackets(AVStream* stream, int streamIndex, PacketQueue* queue);
	static BOOL DemuxThread(void* mux);
	static int DecodeInterruptCallback(void* context);

private:
	std::string			m_fileName;
	SDL_Thread*			m_readThread;
	BOOL				m_stop;
	PacketQueue			m_audioPacketQueue;		// ��Ƶδ����֡����
	PacketQueue			m_videoPacketQueue;		// ��Ƶδ����֡����
	AVFormatContext*	m_pFormatContext;		// ��ý�����������
	AVStream*			m_pAudioStream;			// ��Ƶ��
	AVStream*			m_pVideoStream;			// ��Ƶ��
	AVCodecContext*		m_pAudioCodecContext;	// ��Ƶ������������
	AVCodecContext*		m_pVideoCodecContext;	// ��Ƶ������������
	int					m_audioIndex;			// ��Ƶ������
	int					m_videoIndex;			// ��Ƶ������
	SDL_cond*			m_continueReadThread;	// ���߳��ź���
};

