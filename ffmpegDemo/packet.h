#pragma once
#include "player.h"

int PacketQueueInit(PacketQueue* packetQueue);
int PacketQueuePut(PacketQueue* packetQueue, AVPacket* packet);
int PacketQueueGet(PacketQueue* packetQueue, AVPacket* packet, int block);
int PacketQueuePutNullPacket(PacketQueue* packetQueue, int streamIndex);
void PacketQueueDestroy(PacketQueue* packetQueue);
void PacketQueueAbort(PacketQueue* packetQueue);


