#pragma once
#include "player.h"

void FrameQueueUnrefItem(Frame* vp);
int FrameQueueInit(FrameQueue* frame, PacketQueue* packetQueue, int maxSize, int keepLast);
void FrameQueueDestroy(FrameQueue* frame);
void FrameQueueSignal(FrameQueue* frame);
Frame* FrameQueuePeek(FrameQueue* frame);
Frame* FrameQueuePeekNext(FrameQueue* frame);
Frame* FrameQueuePeekLast(FrameQueue* frame);
Frame* FrameQueuePeekWritable(FrameQueue* frame);
Frame* FrameQueuePeekReadable(FrameQueue* frame);
void FrameQueuePush(FrameQueue* frame);
void FrameQueueNext(FrameQueue* frame);
int FrameQueueNumberRemaining(FrameQueue* frame);


