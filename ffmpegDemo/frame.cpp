#include "frame.h"

void FrameQueueUnrefItem(Frame* vp)
{
	av_frame_unref(vp->frame);
}

// δ�������ݶ��г�ʼ��
int FrameQueueInit(FrameQueue* frameQueue, PacketQueue* packetQueue, int maxSize, int keepLast)
{
	memset(frameQueue, 0, sizeof(FrameQueue));
	// ����������
	if (!(frameQueue->mutex = SDL_CreateMutex()))
	{
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex():%s\n", SDL_GetError());
		return AVERROR(ENOMEM);
	}
	// ��������
	if (!(frameQueue->cond = SDL_CreateCond()))
	{
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
		return AVERROR(ENOMEM);
	}
	frameQueue->packetQueue = packetQueue;  // ����δ������ж�Ӧ�ı������
	frameQueue->maxSize = FFMIN(maxSize, FRAME_QUEUE_SIZE); // ��ͬ�������ݣ����ò�ͬ�� size ��С
	frameQueue->keepLast = !!keepLast; // ������һ֡����
	for (int i = 0; i < frameQueue->maxSize; i++)
	{
		if (!(frameQueue->queue[i].frame = av_frame_alloc())) // Ϊ���������ݵ� AVFrame ����ռ�
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

// ȡ����֡���в��ţ�ֻ��ȡ��ɾ��
Frame* FrameQueuePeekLast(FrameQueue* frameQueue)
{
	return &frameQueue->queue[frameQueue->rindex];
}

// �����β������һ����д��֡�ռ䣬���޿ռ���ȴ�
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

// �Ӷ���ͷ����ȡһ֡��ֻ��ȡ��ɾ��
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

// �����β��ѹ��һ֡��ֻ���¼�����дָ�룬��˵��ô˺���ǰӦ��֡����д�������Ӧλ��
void FrameQueuePush(FrameQueue* frameQueue)
{
	if (++frameQueue->windex == frameQueue->maxSize)
		frameQueue->windex = 0;
	SDL_LockMutex(frameQueue->mutex);
	frameQueue->size++;
	SDL_CondSignal(frameQueue->cond);
	SDL_UnlockMutex(frameQueue->mutex);
}

// ��ָ��ָ���֡����ʾ��ɾ����֡��ע�ⲻ��ȡֱ��ɾ������ָ��� 1
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

// frameQueue ��δ��ʾ��֡��
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



