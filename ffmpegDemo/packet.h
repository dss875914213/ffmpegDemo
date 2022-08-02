#pragma once
#include "player.h"

int PacketQueueInit(PacketQueue* packetQueue);	// ��ʼ��������� ��Ϊ0�������� mutex �� cond
void PacketQueueDestroy(PacketQueue* packetQueue);	// ���� packetQueue
int PacketQueuePut(PacketQueue* packetQueue, AVPacket* packet);  // д����β���� packet ��һ��δ�������Ƶ����
int PacketQueueGet(PacketQueue* packetQueue, AVPacket* packet, int block);	// ������ͷ��
int PacketQueuePutNullPacket(PacketQueue* packetQueue, int streamIndex); // ������������ӿհ�
void PacketQueueAbort(PacketQueue* packetQueue);

