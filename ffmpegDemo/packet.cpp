#include "player.h"
#include <iostream>
using namespace std;

int PacketQueueInit(PacketQueue* packetQueue)
{
	memset(packetQueue, 0, sizeof(PacketQueue));
	packetQueue->mutex = SDL_CreateMutex();
	if (!packetQueue->mutex)
	{
		cout << "SDL_CreateMutex(): " << SDL_GetError() << endl;
		return AVERROR(ENOMEM);
	}
	packetQueue->cond = SDL_CreateCond();
	if (!packetQueue->cond)
	{
		cout << "SDL_CreateCond(): " << SDL_GetError() << endl;
		return AVERROR(ENOMEM);
	}
	packetQueue->abortRequest = 0;
	return 0;
}

// 写队列尾部， packet 是一包未解码的音频数据
int PacketQueuePut(PacketQueue* packetQueue, AVPacket* packet)
{
	AVPacketList* pktList;
	if (av_packet_make_refcounted(packet) < 0)
	{
		cout << "[packet] is not reference counted" << endl;
		return -1;
	}
	pktList = static_cast<AVPacketList*>(av_malloc(sizeof(AVPacketList)));
	if (!pktList)
		return -1;

	pktList->pkt = *packet;
	pktList->next = NULL;

	SDL_LockMutex(packetQueue->mutex);
	if (!packetQueue->lastPacket)
		packetQueue->firstPacket = pktList;
	else
		packetQueue->lastPacket->next = pktList;
	packetQueue->lastPacket = pktList;
	packetQueue->numberPackets++;
	packetQueue->size += pktList->pkt.size;
	SDL_CondSignal(packetQueue->cond);
	SDL_UnlockMutex(packetQueue->mutex);
	return 0;
}

// 读队列头部
int PacketQueueGet(PacketQueue* packetQueue, AVPacket* packet, int block)
{
	AVPacketList* pPacketNode;
	int ret = 0;
	SDL_LockMutex(packetQueue->mutex);
	while (1)
	{
		if (packetQueue->abortRequest)
			break;
		pPacketNode = packetQueue->firstPacket;
		if (pPacketNode)
		{
			packetQueue->firstPacket = pPacketNode->next;
			if (!packetQueue->firstPacket)
				packetQueue->lastPacket = NULL;
			packetQueue->numberPackets--;
			packetQueue->size -= pPacketNode->pkt.size;
			*packet = pPacketNode->pkt;
			av_free(pPacketNode);
			ret = 1;
			break;
		}
		else if (!block)
		{
			ret = 0;
			break;
		}
		else
			SDL_CondWait(packetQueue->cond, packetQueue->mutex);
	}
	SDL_UnlockMutex(packetQueue->mutex);
	return ret;
}

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





