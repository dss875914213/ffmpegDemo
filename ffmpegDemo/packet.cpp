#include "player.h"
#include <iostream>
#include "config.h"
using namespace std;

// ��ʼ��������� ��Ϊ0�������� mutex �� cond
int PacketQueueInit(PacketQueue* packetQueue)
{
	memset(packetQueue, 0, sizeof(PacketQueue));
	// ����������
	packetQueue->mutex = SDL_CreateMutex();
	if (!packetQueue->mutex)
	{
		cout << "SDL_CreateMutex(): " << SDL_GetError() << endl;
		return AVERROR(ENOMEM);
	}
	// ������������
	packetQueue->cond = SDL_CreateCond();
	if (!packetQueue->cond)
	{
		cout << "SDL_CreateCond(): " << SDL_GetError() << endl;
		return AVERROR(ENOMEM);
	}
	// ����ֹͣ����Ϊ 0
	packetQueue->abortRequest = 0;
	return 0;
}

// д����β���� packet ��һ��δ�������Ƶ����
int PacketQueuePut(PacketQueue* packetQueue, AVPacket* packet)
{
	if (av_packet_make_refcounted(packet) < 0)
	{
		cout << "[packet] is not reference counted" << endl;
		return -1;
	}
	// pktList �����ڴ�
	AVPacketList* pktList = static_cast<AVPacketList*>(av_malloc(sizeof(AVPacketList)));
	if (pktList == NULL)
		return -1;

	// ���� pktList�� �������ֵ
	pktList->pkt = *packet;
	pktList->next = NULL;

	// ����
	SDL_LockMutex(packetQueue->mutex);
	// �������Ϊ��,�򽫸�������Ϊ��ͷ
	if (!packetQueue->lastPacket)
		packetQueue->firstPacket = pktList;
	else  // ���򣬽�������ӵ�β��
		packetQueue->lastPacket->next = pktList;
	// ���¶���β����Ϊ�¼��������
	packetQueue->lastPacket = pktList;
	// δ����֡�� +1
	packetQueue->numberPackets++;
	// ���г��� + �¼������ݳ���
	packetQueue->size += pktList->pkt.size;
	// �����ȴ����źű������߳�
	SDL_CondSignal(packetQueue->cond);
	// ����
	SDL_UnlockMutex(packetQueue->mutex);
	return 0;
}

// ������ͷ��
int PacketQueueGet(PacketQueue* packetQueue, AVPacket* packet, int block)
{
	AVPacketList* pPacketNode;
	int ret = 0;
	SDL_LockMutex(packetQueue->mutex);
	while (1)
	{
		// ֹͣ����
		// -DSS TODO �� player ������
		if (packetQueue->abortRequest)
			break;
		pPacketNode = packetQueue->firstPacket;
		// ��������ݣ���ȡ����
		if (pPacketNode)
		{
			packetQueue->firstPacket = pPacketNode->next;
			// �������ȡ���ˣ��� lastPacket Ҳ��Ϊ NULL  
			if (!packetQueue->firstPacket)
				packetQueue->lastPacket = NULL;
			packetQueue->numberPackets--;
			packetQueue->size -= pPacketNode->pkt.size;
			*packet = pPacketNode->pkt;
			// ֻ�ͷ� pPacketNode �������ͷ������ packet
			av_free(pPacketNode);
			ret = 1;
			break;
		}
		// ���û���ݣ��Ҳ���������ֹͣ
		else if (!block)
		{
			ret = 0;
			break;
		}
		// û���ݣ���������ȴ� mutex �̣߳������ݷ���δ���������
		else
			SDL_CondWait(packetQueue->cond, packetQueue->mutex);
	}
	SDL_UnlockMutex(packetQueue->mutex);
	return ret;
}

// ������������ӿհ�
int PacketQueuePutNullPacket(PacketQueue* packetQueue, int streamIndex)
{
	AVPacket pkt1, * pkt = &pkt1;
	av_init_packet(pkt);
	pkt->data = NULL;
	pkt->size = 0;
	pkt->stream_index = streamIndex;
	return PacketQueuePut(packetQueue, pkt);
}

void PacketQueueFlush(PacketQueue* packetQueue)
{
	AVPacketList* pkt, * pkt1;
	SDL_LockMutex(packetQueue->mutex);
	for (pkt = packetQueue->firstPacket; pkt; pkt = pkt1)
	{
		pkt1 = pkt->next;
		av_packet_unref(&pkt->pkt);
		av_freep(&pkt);
	}
	packetQueue->lastPacket = NULL;
	packetQueue->firstPacket = NULL;
	packetQueue->numberPackets = 0;
	packetQueue->size = 0;
	packetQueue->duration = 0;
	SDL_UnlockMutex(packetQueue->mutex);
}

void PacketQueueDestroy(PacketQueue* packetQueue)
{
	PacketQueueFlush(packetQueue);
	SDL_DestroyMutex(packetQueue->mutex);
	SDL_DestroyCond(packetQueue->cond);
}

void PacketQueueAbort(PacketQueue* packetQueue)
{
	SDL_LockMutex(packetQueue->mutex);
	packetQueue->abortRequest = 1;
	SDL_CondSignal(packetQueue->cond);
	SDL_UnlockMutex(packetQueue->mutex);
}





