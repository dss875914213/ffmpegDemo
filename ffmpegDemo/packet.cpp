#include "player.h"
#include <iostream>
using namespace std;

// 初始化编码队列
int PacketQueueInit(PacketQueue* packetQueue)
{
	memset(packetQueue, 0, sizeof(PacketQueue));
	// 创建互斥量
	packetQueue->mutex = SDL_CreateMutex();
	if (!packetQueue->mutex)
	{
		cout << "SDL_CreateMutex(): " << SDL_GetError() << endl;
		return AVERROR(ENOMEM);
	}
	// 创建条件变量
	packetQueue->cond = SDL_CreateCond();
	if (!packetQueue->cond)
	{
		cout << "SDL_CreateCond(): " << SDL_GetError() << endl;
		return AVERROR(ENOMEM);
	}
	// 设置停止请求为 0
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
	// pktList 分配内存
	pktList = static_cast<AVPacketList*>(av_malloc(sizeof(AVPacketList)));
	if (!pktList)
		return -1;

	// 构建 pktList， 并赋与初值
	pktList->pkt = *packet;
	pktList->next = NULL;

	// 加锁
	SDL_LockMutex(packetQueue->mutex);
	// 如果队列为空,则将该数据作为队头
	if (!packetQueue->lastPacket)
		packetQueue->firstPacket = pktList;
	else  // 否则，将数据添加到尾部
		packetQueue->lastPacket->next = pktList;
	// 更新队列尾部，为新加入的数据
	packetQueue->lastPacket = pktList;
	//未解码帧数 +1
	packetQueue->numberPackets++;
	// 队列长度 + 新加入数据长度
	packetQueue->size += pktList->pkt.size;
	// 重启等待此信号变量的线程
	SDL_CondSignal(packetQueue->cond);
	// 解锁
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





