#include "frame.h"

void FrameQueueUnrefItem(Frame* vp)
{
	av_frame_unref(vp->frame);
}

// 未编码数据队列初始化
int FrameQueueInit(FrameQueue* frameQueue, PacketQueue* packetQueue, int maxSize, int keepLast)
{
	memset(frameQueue, 0, sizeof(FrameQueue));
	// 创建互斥量
	if (!(frameQueue->mutex = SDL_CreateMutex()))
	{
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex():%s\n", SDL_GetError());
		return AVERROR(ENOMEM);
	}
	// 条件变量
	if (!(frameQueue->cond = SDL_CreateCond()))
	{
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
		return AVERROR(ENOMEM);
	}
	frameQueue->packetQueue = packetQueue;  // 设置未编码队列对应的编码队列
	frameQueue->maxSize = FFMIN(maxSize, FRAME_QUEUE_SIZE); // 不同类型数据，设置不同的 size 大小
	frameQueue->keepLast = !!keepLast; // 保存上一帧数据
	for (int i = 0; i < frameQueue->maxSize; i++)
	{
		if (!(frameQueue->queue[i].frame = av_frame_alloc())) // 为该类型数据的 AVFrame 分配空间
			return AVERROR(ENOMEM);
	}
	return 0;
}

void FrameQueueDestroy(FrameQueue* frameQueue)
{
	for (int i = 0; i < frameQueue->maxSize; i++)
	{
		Frame* videoFrame = &frameQueue->queue[i];
		FrameQueueUnrefItem(videoFrame);
		av_frame_free(&videoFrame->frame);
	}
	SDL_DestroyMutex(frameQueue->mutex);
	SDL_DestroyCond(frameQueue->cond);
}

void FrameQueueSignal(FrameQueue* frameQueue)
{
	SDL_LockMutex(frameQueue->mutex);
	frameQueue->packetQueue->abortRequest = 1;
	SDL_CondSignal(frameQueue->cond);
	SDL_UnlockMutex(frameQueue->mutex);
}

Frame* FrameQueuePeek(FrameQueue* frameQueue)
{
	return &frameQueue->queue[(frameQueue->rindex + frameQueue->rindexShown) % frameQueue->maxSize];
}

Frame* FrameQueuePeekNext(FrameQueue* frameQueue)
{
	return &frameQueue->queue[(frameQueue->rindex + frameQueue->rindexShown + 1) % frameQueue->maxSize];
}

// 取出此帧进行播放，只读取不删除
Frame* FrameQueuePeekLast(FrameQueue* frameQueue)
{
	return &frameQueue->queue[frameQueue->rindex];
}

// 向队列尾部申请一个可写的帧空间，若无空间则等待
Frame* FrameQueuePeekWritable(FrameQueue* frameQueue)
{
	SDL_LockMutex(frameQueue->mutex);
	while (frameQueue->size >= frameQueue->maxSize &&
		!frameQueue->packetQueue->abortRequest)
	{
		SDL_CondWait(frameQueue->cond, frameQueue->mutex);
	}
	SDL_UnlockMutex(frameQueue->mutex);
	if (frameQueue->packetQueue->abortRequest)
		return NULL;
	return &frameQueue->queue[frameQueue->windex];
}

// 从队列头部读取一帧，只读取不删除
Frame* FrameQueuePeekReadable(FrameQueue* frameQueue)
{
	SDL_LockMutex(frameQueue->mutex);
	while (frameQueue->size - frameQueue->rindexShown <= 0 &&
		!frameQueue->packetQueue->abortRequest)
	{
		SDL_CondWait(frameQueue->cond, frameQueue->mutex);
	}
	SDL_UnlockMutex(frameQueue->mutex);
	if (frameQueue->packetQueue->abortRequest)
		return NULL;
	return &frameQueue->queue[(frameQueue->rindex + frameQueue->rindexShown) % frameQueue->maxSize];
}

// 向队列尾部压入一帧，只更新计数与写指针，因此调用此函数前应将帧数据写入队列相应位置
void FrameQueuePush(FrameQueue* frameQueue)
{
	if (++frameQueue->windex == frameQueue->maxSize)
		frameQueue->windex = 0;
	SDL_LockMutex(frameQueue->mutex);
	frameQueue->size++;
	SDL_CondSignal(frameQueue->cond);
	SDL_UnlockMutex(frameQueue->mutex);
}

// 读指针指向的帧已显示，删除此帧，注意不读取直接删除。读指针加 1
void FrameQueueNext(FrameQueue* frameQueue)
{
	if (frameQueue->keepLast && !frameQueue->rindexShown)
	{
		frameQueue->rindexShown = 1;
		return;
	}

	FrameQueueUnrefItem(&frameQueue->queue[frameQueue->rindex]);
	if (++frameQueue->rindex == frameQueue->maxSize)
		frameQueue->rindex = 0;
	SDL_LockMutex(frameQueue->mutex);
	frameQueue->size--;
	SDL_CondSignal(frameQueue->cond);
	SDL_UnlockMutex(frameQueue->mutex);
}

// frameQueue 中未显示的帧数
int FrameQueueNumberRemaining(FrameQueue* frameQueue)
{
	return frameQueue->size - frameQueue->rindexShown;
}

int64_t FrameQueueLastPosition(FrameQueue* frameQueue)
{
	Frame* frame = &frameQueue->queue[frameQueue->rindex];
	if (frameQueue->rindexShown && frame->serial == frameQueue->packetQueue->serial)
		return frame->pos;
	else
		return -1;
}



