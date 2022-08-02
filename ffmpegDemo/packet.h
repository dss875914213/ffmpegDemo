#pragma once
#include "player.h"

INT32 PacketQueueInit(PacketQueue* packetQueue);	// ��ʼ��������� ��Ϊ0�������� mutex �� cond
void PacketQueueDestroy(PacketQueue* packetQueue);	// ���� packetQueue
INT32 PacketQueuePut(PacketQueue* packetQueue, AVPacket* packet);  // д����β���� packet ��һ��δ�������Ƶ����
BOOL PacketQueueGet(PacketQueue* packetQueue, AVPacket* packet, BOOL block);	// ������ͷ��
INT32 PacketQueuePutNullPacket(PacketQueue* packetQueue, INT32 streamIndex); // ������������ӿհ�
void PacketQueueAbort(PacketQueue* packetQueue);

