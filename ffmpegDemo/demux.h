#pragma once
#include <string>
#include "player.h"
#include <windows.h>

using namespace std;

class Player;
class  FFDemux
{
public:
	FFDemux();
	~FFDemux();
	BOOL Init(string filename, Player* player);
	BOOL DemuxDeinit();
	BOOL Open();
	BOOL IsStop();
	BOOL Close();
	PacketQueue* GetVideoPacketQueue();
	PacketQueue* GetAudioPacketQueue();

private:
	BOOL StreamHasEnoughPackets(AVStream* stream, int streamIndex, PacketQueue* queue);
	static BOOL DemuxThread(void* mux);
	static int DecodeInterruptCallback(void* context);

private:
	std::string			m_fileName;
	Player*				m_player;

	SDL_Thread*			m_readThread;
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

