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
	BOOL					Init(const string& filename, Player* player); // ��ʼ��
	BOOL					Open();	// �������ļ��߳� (�̺߳���, �߳�����, �����̵߳Ĳ���)
	BOOL					Close();	// �ر�
	PacketQueue*			GetPacketQueue(BOOL isVideo);  // �õ���Ӧ����
	AVStream*				GetStream(BOOL isVideo);	// �õ���Ӧ��

private:
	BOOL					IsStop();	// �Ƿ�ֹͣ
	BOOL					StreamHasEnoughPackets(AVStream* stream, INT32 streamIndex, PacketQueue* queue);
	static BOOL				DemuxThread(void* mux);
	static INT32			DecodeInterruptCallback(void* context);

private:
	AVFormatContext*	m_pFormatContext;		// ��ý�����������
	INT32				m_audioIndex;			// ��Ƶ������
	INT32				m_videoIndex;			// ��Ƶ������

	Player*				m_player;				// ������
	PacketQueue			m_audioPacketQueue;		// ��Ƶδ����֡����
	PacketQueue			m_videoPacketQueue;		// ��Ƶδ����֡����

	SDL_cond*			m_continueReadThread;	// ���߳��ź���
	SDL_Thread*			m_readThread;			// ���߳�

};

