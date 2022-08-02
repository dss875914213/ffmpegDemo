#pragma once
#include "player.h"

int PacketQueueInit(PacketQueue* packetQueue);	// 初始化编码队列 置为0，并创建 mutex 和 cond
void PacketQueueDestroy(PacketQueue* packetQueue);	// 销毁 packetQueue
int PacketQueuePut(PacketQueue* packetQueue, AVPacket* packet);  // 写队列尾部， packet 是一包未解码的音频数据
int PacketQueueGet(PacketQueue* packetQueue, AVPacket* packet, int block);	// 读队列头部
int PacketQueuePutNullPacket(PacketQueue* packetQueue, int streamIndex); // 向编码队列中添加空包
void PacketQueueAbort(PacketQueue* packetQueue);

