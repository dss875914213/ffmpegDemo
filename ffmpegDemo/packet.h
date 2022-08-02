#pragma once
#include "player.h"

INT32 PacketQueueInit(PacketQueue* packetQueue);	// 初始化编码队列 置为0，并创建 mutex 和 cond
void PacketQueueDestroy(PacketQueue* packetQueue);	// 销毁 packetQueue
INT32 PacketQueuePut(PacketQueue* packetQueue, AVPacket* packet);  // 写队列尾部， packet 是一包未解码的音频数据
BOOL PacketQueueGet(PacketQueue* packetQueue, AVPacket* packet, BOOL block);	// 读队列头部
INT32 PacketQueuePutNullPacket(PacketQueue* packetQueue, INT32 streamIndex); // 向编码队列中添加空包
void PacketQueueAbort(PacketQueue* packetQueue);

