#include <iostream>
#include "video.h"
#include "packet.h"
#include "frame.h"
#include "player.h"
using namespace std;

Video::Video()
{
	// -DSS TODO 参数初始化
}

Video::~Video()
{

}

BOOL Video::Open()
{
	// 设置视频编码器上下文并创建视频解码器线程
	OpenStream();
	// 初始化格式转化上下文, 初始化SDL 图像渲染功能, 创建视频渲染线程
	OpenPlaying();
	return 0;
}

void Video::Close()
{

}

BOOL Video::QueuePicture(AVFrame* sourceFrame, double pts, double duration, int64_t pos)
{
	// 向队列尾部申请一个可写的帧空间，若无空间则等待
	Frame* frame = FrameQueuePeekWritable(&m_frameQueue);

	if (frame == NULL)
		return FALSE;

	frame->sar = sourceFrame->sample_aspect_ratio; // 视频纵横比
	frame->width = sourceFrame->width; // 使用解码帧的宽度
	frame->height = sourceFrame->height; // 使用解码帧的高度
	frame->format = sourceFrame->format; // 使用解码帧的格式

	frame->pts = pts; // 经过时间基转换后的渲染时间戳
	frame->duration = duration; // 渲染持续时间   帧率 分母/ 分子
	frame->pos = pos; // frame 对应 packet 在输入文件中的地址偏移

	// 将 AVFrame 拷入队列相应位置
	// 改变 data 指针的指向
	// 将 dst 指向 src ,并将 src 设为默认值
	av_frame_move_ref(frame->frame, sourceFrame);
	// 更新队列计数及写索引
	FrameQueuePush(&m_frameQueue);
	return TRUE;
}

int Video::DecodeFrame(AVCodecContext* pCodecContext, PacketQueue* pPacketQueue, AVFrame* frame)
{
	int ret;
	while (1)
	{
		AVPacket pkt;
		while (1)
		{
			// 停止请求
			if (pPacketQueue->abortRequest)
				return -1;

			// 3. 从解码器接收 frame
			// 3.1 一个视频 packet 包含一个 frame
			// 解码器缓存一定数量的 packet 后，才有解码后的 frame 输出
			// frame 输出顺序是按 pts 的顺序，如 IBBPBBP
			// frame->pkt_pos 变量是此 frame对应的packet 在视频文件中的偏移地址，值同 pkt.pos
			ret = avcodec_receive_frame(pCodecContext, frame);
			if (ret < 0)
			{
				// decode 已经被冲刷过了
				if (ret == AVERROR_EOF)
				{
					av_log(NULL, AV_LOG_INFO, "video avcodec_receive_frame(): the decoder has been fully flushed\n");
					avcodec_flush_buffers(pCodecContext);
					return 0;
				}
				// 输入数据不够，还需要输入数据才能输出解码帧
				else if (ret == AVERROR(EAGAIN))
				{
					av_log(NULL, AV_LOG_INFO, "video avcodec_receive_frame(): output is not available in this state - "
						"user must try to send new input\n");
					break;
				}
				else
				{
					av_log(NULL, AV_LOG_ERROR, "video avcodec_receive_frame(): other errors\n");
					continue;
				}
			}
			else
			{
				// 使用各种启发式方法估计的帧时间戳，在流时基中
				frame->pts = frame->best_effort_timestamp;
				return 1; // 成功解码得到一个视频帧
			}
		}

		// 1.取出一个 packet.使用 pkt 对应的 serial 赋值给 d->pkt_serial
		if (PacketQueueGet(pPacketQueue, &pkt, true) < 0)
			return -1;
		// packet queue 中第一个总是 flush_pkt. 每次 seek 操作会插入 flush_pkt,更新 serial,开启新的播放序列
		// 没有数据了，则冲刷 编码器上下文，将内部缓存的数据吐出来
		if (pkt.data == NULL)
			avcodec_flush_buffers(pCodecContext);
		else
		{
			// 2.将packet发送给解码器
			// 发送 packet 的顺序是按 dts 递增的顺序,如IPBBPBB
			// pkt.pos 变量可以标识当前 packet 在视频文件中的地址偏移
			if (avcodec_send_packet(pCodecContext, &pkt) == AVERROR(EAGAIN))
				av_log(NULL, AV_LOG_ERROR, "receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
			// 重置 pkt 里面的数据，并将 pkt data 空间释放
			av_packet_unref(&pkt);
		}
	}
}

BOOL Video::OnDecodeThread()
{
	// 分配 frame 空间, buffer 空间需要重新分配
	AVFrame* pFrame = av_frame_alloc();
	double pts;
	double duration;
	int ret;
	int gotPicture;
	// 时间基
	AVRational timebase = m_pStream->time_base;
	// 猜测帧率
	AVRational frameRate = av_guess_frame_rate(m_pFormatContext, m_pStream, NULL);
	if (pFrame == NULL)
	{
		av_log(NULL, AV_LOG_ERROR, "av_frame_alloc() for pFrame failed\n");
		return AVERROR(ENOMEM);
	}

	while (1)
	{
		// 停止请求
		if (m_stop)
			break;
		// 从 packet_queue 中取一个 packet, 解码生成 frame
		gotPicture = VideoDecodeFrame(m_pCodecContext, &m_packetQueue, pFrame);
		if (gotPicture < 0)
			goto EXIT;
		duration = (frameRate.num && frameRate.den ? av_q2d(AVRational{ frameRate.den, frameRate.num }) : 0);// 当前帧播放时长 帧率 分母/分子
		pts = (pFrame->pts == AV_NOPTS_VALUE) ? NAN : pFrame->pts * av_q2d(timebase); // 当前显示时间戳
		// 将解码后数据，放入解码后队列
		ret = QueuePicture(pFrame, pts, duration, pFrame->pkt_pos); // 将当前帧压入 frameQueue
		// av_frame_unref，它的作用是释放音视频数据资源，并给 frame 设置初值
		av_frame_unref(pFrame);
		if (ret < 0)
			goto EXIT;
	}
EXIT:
	// av_frame_free是释放所有资源，包括音视频数据资源和结构体本身的内存
	av_frame_free(&pFrame);
	return 0;
}

BOOL Video::OnPlayingThread()
{
	double remainingTime = 0.0; // 当前帧还需展示的时间 (单位 s)
	while (1)
	{
		// 停止请求
		if (m_stop)
			break;
		if (remainingTime > 0.0)
			av_usleep((unsigned)(remainingTime * 1000000.0));
		remainingTime = REFRESH_RATE;
		// 立即显示当前帧,或延时 remaining_time 后再显示
		Refresh(&remainingTime);
	}
	return 0;
}

double Video::ComputeTargetDelay(double delay)
{
	double syncThreshold, diff = 0;
	// 视频时钟与同步时钟的差异，时钟值是上一帧 pts 值（实为：上一帧pts+上一帧至今流逝的时间差）
	// 视频pts + 视频上一帧帧渲染时间 - 音频 pts - 音频上一帧帧渲染时间
	diff = GetClock(&m_videoPlayClock) - GetClock(&m_audioPlayClock);
	// delay 是上一帧播放时长：当前帧（待播放的帧）播放时间与上一帧时间差理论值
	// diff 是视频时钟与同步时钟的差值
	// 若 delay<AV_SYNC_THRESHOLD_MIN,则同步阈值为 AV_SYNC_THRESHOLD_MIN
	// 若 delay>AV_SYNC_THRESHOLD_MAX,则同步阈值为 AV_SYNC_THRESHOLD_MAX
	// 若AV_SYNC_THRESHOLD_MIN<delay<AV_SYNC_THRESHOLD_MAX, 则同步域值为 delay
	syncThreshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
	if (!isnan(diff))
	{
		// 若视频时钟落后于同步时钟，且超过同步域值，则当前视频缩短渲染时间
		if (diff <= -syncThreshold)
			delay = FFMAX(0, delay + diff); // 当前帧播放时刻落后于同步时钟 (delay+diff<0)则delay = 0(视频追赶，立即播放),否则delay=deley+diff
		// 视频时钟超前于同步时钟，且超过同步阈值，但上一帧播放时长超长
		else if (diff >= syncThreshold && delay > AV_SYNC_FRAMEUP_THRESHOLD)
			delay += diff; // 仅仅校正为 delay += diff, 主要是 AV_SYNC_FRAMEDUP_THRESHOLD 参数的作用
		// 视频时钟超前于同步时钟，且超过同步域值
		else if (diff >= syncThreshold)
			delay = 2 * delay; // 视频播放速度放慢脚步, delay扩大至2倍
	}
	av_log(NULL, AV_LOG_TRACE, "video: delay=%0.3f A-V=%f\n", delay, -diff);
	return delay;
}

double Video::VpDuration(Frame* vp, Frame* nextvp)
{
	if (vp->serial == nextvp->serial)
	{
		double duration = nextvp->pts - vp->pts;
		if (isnan(duration) || duration <= 0)
			return vp->duration;
		else
			return duration;
	}
	else
		return 0.0;
}

void Video::UpdatePts(double pts, int serial)
{
	SetClock(&m_videoPlayClock, pts, serial); // 更新 videoClock
}

void Video::Display()
{
	Frame* videoPicture;
	// 取出此帧进行播放，只读取不删除
	videoPicture = FrameQueuePeekLast(&m_frameQueue);
	// 图片转换： pFrameRaw->data ==> pFrameYUV->data
	// 将源图像中一片连续的区域经过处理后更新到目标图像对应区域，处理的图像区域必须逐行连续
	// plane:如YUV有 Y、U、V三个plane，RGB有 R、G、B 三个 plane
	// slice: 图像中一片连续的行, 必须是连续的, 顺序由顶部到底部或由底部到顶部
	// stride/pitch: 一行图像所占得字节数, stride = BytesPerPixel*width+Padding,注意对齐
	// AVFrame.*data[]:每个数组元素指向对应 plane
	// AVFrame.linesize[]:每个数组元素表示对应 plane 中一行图像所占得字节数
	sws_scale(m_swsContext, static_cast<const uint8_t* const*>(videoPicture->frame->data),
		videoPicture->frame->linesize, 0, m_pCodecContext->height, m_pFrameYUV->data,
		m_pFrameYUV->linesize);

	// 使用新的 YUV 像素数据更新 SDL_Rect
	SDL_UpdateYUVTexture(m_sdlVideo.texture, &m_sdlVideo.rect,
		m_pFrameYUV->data[0], m_pFrameYUV->linesize[0],
		m_pFrameYUV->data[1], m_pFrameYUV->linesize[1],
		m_pFrameYUV->data[2], m_pFrameYUV->linesize[2]);

	// 使用特定颜色清空当前渲染目标
	SDL_RenderClear(m_sdlVideo.renderer);
	// 使用部分图像数据(texture)更新当前渲染目标
	SDL_RenderCopy(m_sdlVideo.renderer, m_sdlVideo.texture, NULL, &m_sdlVideo.rect);
	// 执行渲染，更新屏幕显示
	SDL_RenderPresent(m_sdlVideo.renderer);
}

void Video::Refresh(double* remainingTime)
{
	double time;
	// 是否是首帧
	static bool firstFrame = true;

RETRY:
	// 获取 frameQueue 中未显示的帧数
	if (FrameQueueNumberRemaining(&m_frameQueue) == 0) // 所有帧已显示
		return;
	double lastDuration, duration, delay;
	Frame* vp, * lastvp;
	lastvp = FrameQueuePeekLast(&m_frameQueue);  // 上一帧：上次已显示的帧
	vp = FrameQueuePeek(&m_frameQueue); // 当前帧：当前帧显示的帧
	// lastvp 和 vp 不是同一个播放序列 (一个 seek 会开始一个新的播放序列),将frame timer 更新为当前时间
	if (firstFrame)
	{
		m_frameTimer = av_gettime_relative() / 1000000.0;
		firstFrame = false;
	}

	// 暂停处理：不停播放上一帧图像
	if (m_paused)
		goto DISPLAY;
	lastDuration = VpDuration(lastvp, vp); // 上一帧理论播放时长：vp->pts - lastvp->pts
	delay = ComputeTargetDelay(lastDuration); // 根据视频时钟和同步时钟的差值，计算 delay 值
	time = av_gettime_relative() / 1000000.0;
	// 当前帧播放时刻(is->frame_timer + delay) 大于当前时刻 (time), 表示播放时刻未到
	if (time < m_frameTimer + delay)
	{
		// 播放时刻未到，则更新刷新时间 remaining_time 为当前时刻到下一播放时刻的时间差
		*remainingTime = FFMIN(m_frameTimer + delay - time, *remainingTime);
		// 播放时刻未到, 则不播放, 直接返回
		return;
	}
	// 更新frame_timer 值
	m_frameTimer += delay;
	// 校正 frame_timer 值：若 frame_timer 落后于当前系统时间太久(超过最大同步阈值),则更新当前系统时间
	if (delay > 0 && time - m_frameTimer > AV_SYNC_THRESHOLD_MAX)
		m_frameTimer = time;
	SDL_LockMutex(m_frameQueue.mutex);
	if (!isnan(vp->pts))
		UpdatePts(vp->pts, vp->serial); // 更新视频时钟：时间戳、时钟时间
	SDL_UnlockMutex(m_frameQueue.mutex);

	// 是否要丢弃未能及时播放的视频帧
	if (FrameQueueNumberRemaining(&m_frameQueue) > 1) // 队列中未显示帧数>1(只有一帧则不考虑丢帧)
	{
		Frame* nextvp = FrameQueuePeekNext(&m_frameQueue); // 下一帧:下一帧待显示的帧
		duration = VpDuration(vp, nextvp); // 下一帧理论播放时长 = nextvp->pts - vp->pts
		// 当前帧 vp 未能及时播放, 即下一帧播放完成时刻(is->frameTimer+duration)小于当前系统时刻(time)
		if (time > m_frameTimer + duration)
		{
			FrameQueueNext(&m_frameQueue); // 删除上一帧已显示帧, 即删除 lastvp, 读指针加1(从lastvp更新到vp)
			goto RETRY;
		}
	}

	// 删除当前读指针元素, 读指针+1. 若未丢帧,读指针从lastvp更新到vp;若有丢帧,读指针从vp更新到nextvp
	FrameQueueNext(&m_frameQueue);
DISPLAY:
	Display(); // 取出当前帧 vp(若有丢帧是 nextvp)进行播放
}

int Video::OpenPlaying()
{
	int ret;
	// 视频缓冲区大小
	int bufferSize;
	// 视频缓冲区地址
	uint8_t* buffer = NULL;
	// 分配格式转换后的 frame
	m_pFrameYUV = av_frame_alloc();
	if (m_pFrameYUV == NULL)
	{
		cout << "av_frame_alloc() for p_frm_raw failed" << endl;
		return -1;
	}
	// 为AVFrame.*data[] 手工分配缓冲区，用于存储 sws_scale 中目的帧视频数据
	// 视频格式, 视频宽度, 视频高度, 视频对齐字节数
	// 此处 bufferSize = width*height*1.5
	bufferSize = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, m_pCodecContext->width, m_pCodecContext->height, 1);
	// 设置缓冲区空间
	buffer = static_cast<uint8_t*>(av_malloc(bufferSize));
	if (buffer == NULL)
	{
		cout << "av_malloc() for buffer failed" << endl;
		return -1;
	}

	// 使用给定参数设定 pFrameYUV->data 和 pFrameYUV->linesize
	ret = av_image_fill_arrays(m_pFrameYUV->data, m_pFrameYUV->linesize, buffer, AV_PIX_FMT_YUV420P,
		m_pCodecContext->width, m_pCodecContext->height, 1);
	if (ret < 0)
	{
		cout << "av_image_fill_arrays() failed " << ret << endl;
		return -1;
	}

	// A2. 初始化 SWS context, 用于后续图像转换
	m_swsContext = sws_getContext(m_pCodecContext->width, m_pCodecContext->height,
		m_pCodecContext->pix_fmt, m_pCodecContext->width, m_pCodecContext->height,
		AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
	if (m_swsContext == NULL)
	{
		cout << "sws_getContext() failed" << endl;
		return -1;
	}

	// SDL_Rect 赋值
	m_sdlVideo.rect.x = 0;
	m_sdlVideo.rect.y = 0;
	m_sdlVideo.rect.w = m_pCodecContext->width;
	m_sdlVideo.rect.h = m_pCodecContext->height;

	// 1.创建SDL窗口 (窗口名字, 窗口起始点x, 窗口起始点y, 窗口宽度w, 窗口高度h, 
	m_sdlVideo.window = SDL_CreateWindow("simple player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		m_sdlVideo.rect.w, m_sdlVideo.rect.h, SDL_WINDOW_OPENGL);
	if (m_sdlVideo.window == NULL)
	{
		cout << "SDL_CreateWindow() failed: " << SDL_GetError() << endl;
		return -1;
	}

	// 2.创建SDL_Renderer (渲染图片的窗口, 选择显卡, 软编)
	m_sdlVideo.renderer = SDL_CreateRenderer(m_sdlVideo.window, -1, 0);
	if (m_sdlVideo.renderer == NULL)
	{
		cout << "SDL_CreateRenderer() failed: " << SDL_GetError() << endl;
		return -1;
	}

	// 3.创建 SDL_Texture (渲染器上下文, 像素格式, 纹理权利(是否经常修改), 纹理宽, 纹理高)
	m_sdlVideo.texture = SDL_CreateTexture(m_sdlVideo.renderer,
		SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, m_sdlVideo.rect.w, m_sdlVideo.rect.h);
	if (m_sdlVideo.texture == NULL)
	{
		cout << "SDL_CreateTexture() failed " << SDL_GetError() << endl;
		return -1;
	}
	// 创建视频渲染线程
	m_playThread = SDL_CreateThread(PlayingThread, "video playing thread", this);
	return 0;
}

int Video::OpenStream()
{
	// 编解码参数
	AVCodecParameters* pCodecPar = NULL;
	// 编解码器
	AVCodec* pCodec = NULL;
	// 编解码器上下文
	AVCodecContext* pCodecContext = NULL;
	// 视频流
	AVStream* pStream = m_pStream;
	int ret;
	// 1.为视频流构建解码器 AVCodecContext
	// 1.1 获取解码器参数 AVCodecParameters
	pCodecPar = pStream->codecpar;

	// 1.2 获取解码器
	pCodec = const_cast<AVCodec*>(avcodec_find_decoder(pCodecPar->codec_id));
	if (pCodecPar == NULL)
	{
		cout << "Can't find codec" << endl;
		return -1;
	}

	// 1.3 构建解码器 AVCodecContext
	// 1.3.1 pCodecContext 初始化: 分配结构体，使用 pCodec 初始化对应成员为默认值
	pCodecContext = avcodec_alloc_context3(pCodec);
	if (pCodecContext == NULL)
	{
		cout << "avcodec_alloc_context3() failed" << endl;
		return -1;
	}

	// 1.3.2 pCodecContext 初始化: pCodecPar ==> pCodecContext 初始化相应成员
	ret = avcodec_parameters_to_context(pCodecContext, pCodecPar);
	if (ret < 0)
	{
		cout << "avcodec_parameters_to_context() failed" << endl;
		return -1;
	}

	// 1.3.3 pCodecContext 初始化: 使用 pCodec 初始化 pCodecContext 初始化完成
	ret = avcodec_open2(pCodecContext, pCodec, NULL);
	if (ret < 0)
	{
		cout << "avcodec_open2() failed " << ret << endl;
		return -1;
	}
	// 设置视频编码器上下文
	m_pCodecContext = pCodecContext;

	// 2.创建视频解码线程
	m_decodeThread = SDL_CreateThread(DecodeThread, "video decode thread", this);
	return 0;
}

BOOL Video::DecodeThread(void* arg)
{
	Video* is = static_cast<Video*>(arg);
	is->OnDecodeThread();
}

BOOL Video::PlayingThread(void* arg)
{
	Video* is = static_cast<Video*>(arg);
	is->OnPlayingThread();
}
