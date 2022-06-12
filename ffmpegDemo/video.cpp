#include <iostream>
#include "video.h"
#include "packet.h"
#include "frame.h"
#include "player.h"
using namespace std;

// 将解码后数据，放入解码后队列
static int QueuePicture(PlayerStation* is, AVFrame* sourceFrame, double pts, double duration, int64_t pos)
{
	// 向队列尾部申请一个可写的帧空间，若无空间则等待
	Frame* videoFrame = FrameQueuePeekWritable(&is->videoFrameQueue);

	if (videoFrame == NULL)
		return -1;

	videoFrame->sar = sourceFrame->sample_aspect_ratio; // 视频纵横比
	videoFrame->width = sourceFrame->width; // 使用解码帧的宽度
	videoFrame->height = sourceFrame->height; // 使用解码帧的高度
	videoFrame->format = sourceFrame->format; // 使用解码帧的格式

	videoFrame->pts = pts; // 经过时间基转换后的渲染时间戳
	videoFrame->duration = duration; // 渲染持续时间   帧率 分母/ 分子
	videoFrame->pos = pos; // frame 对应 packet 在输入文件中的地址偏移

	// 将 AVFrame 拷入队列相应位置
	// 改变 data 指针的指向
	// 将 dst 指向 src ,并将 src 设为默认值
	av_frame_move_ref(videoFrame->frame, sourceFrame);
	// 更新队列计数及写索引
	FrameQueuePush(&is->videoFrameQueue);
	return 0;
}

// 从 packet_queue 中取一个 packet, 解码生成 frame
static int VideoDecodeFrame(AVCodecContext* pCodecContext, PacketQueue* pPacketQueue, AVFrame* frame)
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

// 将视频包解码得到视频帧，然后写入 picture 队列
// 视频解码线程
static int VideoDecodeThread(void* arg)
{
	PlayerStation* is = static_cast<PlayerStation*>(arg);
	// 分配 frame 空间, buffer 空间需要重新分配
	AVFrame* pFrame = av_frame_alloc();
	double pts;
	double duration;
	int ret;
	int gotPicture;
	// 时间基
	AVRational timebase = is->pVideoStream->time_base;
	// 猜测帧率
	AVRational frameRate = av_guess_frame_rate(is->pFormatContext, is->pVideoStream, NULL);
	if (pFrame == NULL)
	{
		av_log(NULL, AV_LOG_ERROR, "av_frame_alloc() for pFrame failed\n");
		return AVERROR(ENOMEM);
	}

	while (1)
	{
		// 停止请求
		if (is->abortRequest)
			break;
		// 从 packet_queue 中取一个 packet, 解码生成 frame
		gotPicture = VideoDecodeFrame(is->pVideoCodecContext, &is->videoPacketQueue, pFrame);
		if (gotPicture < 0)
			goto EXIT;
		duration = (frameRate.num && frameRate.den ? av_q2d(AVRational{ frameRate.den, frameRate.num }) : 0);// 当前帧播放时长 帧率 分母/分子
		pts = (pFrame->pts == AV_NOPTS_VALUE) ? NAN : pFrame->pts * av_q2d(timebase); // 当前显示时间戳
		// 将解码后数据，放入解码后队列
		ret = QueuePicture(is, pFrame, pts, duration, pFrame->pkt_pos); // 将当前帧压入 frameQueue
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

// 根据视频时钟与同步时钟的差值，校正 delay 值，使视频时钟追赶或等待同步时钟
// 输入参数 delay 是上一帧播放时长, 即上一帧播放后应延长时间后再播放当前帧，通过调节此值来调节当前帧播放快慢
// 返回值 delay 是将输入参数 delay 经校正后得到的值
static double ComputeTargetDelay(double delay, PlayerStation* is)
{
	double syncThreshold, diff = 0;
	// 视频时钟与同步时钟的差异，时钟值是上一帧 pts 值（实为：上一帧pts+上一帧至今流逝的时间差）
	// 视频pts + 视频上一帧帧渲染时间 - 音频 pts - 音频上一帧帧渲染时间
	diff = GetClock(&is->videoPlayClock) - GetClock(&is->audioPlayClock);
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

static double VpDuration(PlayerStation* is, Frame* vp, Frame* nextvp)
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

// 更新视频渲染时间戳
static void UpdateVideoPts(PlayerStation* is, double pts, int serial)
{
	SetClock(&is->videoPlayClock, pts, serial); // 更新 videoClock
}

// 渲染图片
static void VideoDisplay(PlayerStation* is)
{
	Frame* videoPicture;
	// 取出此帧进行播放，只读取不删除
	videoPicture = FrameQueuePeekLast(&is->videoFrameQueue);
	// 图片转换： pFrameRaw->data ==> pFrameYUV->data
	// 将源图像中一片连续的区域经过处理后更新到目标图像对应区域，处理的图像区域必须逐行连续
	// plane:如YUV有 Y、U、V三个plane，RGB有 R、G、B 三个 plane
	// slice: 图像中一片连续的行, 必须是连续的, 顺序由顶部到底部或由底部到顶部
	// stride/pitch: 一行图像所占得字节数, stride = BytesPerPixel*width+Padding,注意对齐
	// AVFrame.*data[]:每个数组元素指向对应 plane
	// AVFrame.linesize[]:每个数组元素表示对应 plane 中一行图像所占得字节数
	sws_scale(is->imgConvertContext, static_cast<const uint8_t* const*>(videoPicture->frame->data),
		videoPicture->frame->linesize, 0, is->pVideoCodecContext->height, is->pFrameYUV->data,
		is->pFrameYUV->linesize);

	// 使用新的 YUV 像素数据更新 SDL_Rect
	SDL_UpdateYUVTexture(is->sdlVideo.texture, &is->sdlVideo.rect,
		is->pFrameYUV->data[0], is->pFrameYUV->linesize[0],
		is->pFrameYUV->data[1], is->pFrameYUV->linesize[1],
		is->pFrameYUV->data[2], is->pFrameYUV->linesize[2]);

	// 使用特定颜色清空当前渲染目标
	SDL_RenderClear(is->sdlVideo.renderer);
	// 使用部分图像数据(texture)更新当前渲染目标
	SDL_RenderCopy(is->sdlVideo.renderer, is->sdlVideo.texture, NULL, &is->sdlVideo.rect);
	// 执行渲染，更新屏幕显示
	SDL_RenderPresent(is->sdlVideo.renderer);
}

// 视频刷新
static void VideoRefresh(void* opaque, double* remainingTime)
{
	PlayerStation* is = static_cast<PlayerStation*>(opaque);
	double time;
	// 是否是首帧
	static bool firstFrame = true;

RETRY:
	// 获取 frameQueue 中未显示的帧数
	if (FrameQueueNumberRemaining(&is->videoFrameQueue) == 0) // 所有帧已显示
		return;
	double lastDuration, duration, delay;
	Frame* vp, * lastvp;
	lastvp = FrameQueuePeekLast(&is->videoFrameQueue);  // 上一帧：上次已显示的帧
	vp = FrameQueuePeek(&is->videoFrameQueue); // 当前帧：当前帧显示的帧
	// lastvp 和 vp 不是同一个播放序列 (一个 seek 会开始一个新的播放序列),将frame timer 更新为当前时间
	if (firstFrame)
	{
		is->frameTimer = av_gettime_relative() / 1000000.0;
		firstFrame = false;
	}

	// 暂停处理：不停播放上一帧图像
	if (is->paused)
		goto DISPLAY;
	lastDuration = VpDuration(is, lastvp, vp); // 上一帧理论播放时长：vp->pts - lastvp->pts
	delay = ComputeTargetDelay(lastDuration, is); // 根据视频时钟和同步时钟的差值，计算 delay 值
	time = av_gettime_relative() / 1000000.0;
	// 当前帧播放时刻(is->frame_timer + delay) 大于当前时刻 (time), 表示播放时刻未到
	if (time < is->frameTimer + delay)
	{
		// 播放时刻未到，则更新刷新时间 remaining_time 为当前时刻到下一播放时刻的时间差
		*remainingTime = FFMIN(is->frameTimer + delay - time, *remainingTime);
		// 播放时刻未到, 则不播放, 直接返回
		return;
	}
	// 更新frame_timer 值
	is->frameTimer += delay;
	// 校正 frame_timer 值：若 frame_timer 落后于当前系统时间太久(超过最大同步阈值),则更新当前系统时间
	if (delay > 0 && time - is->frameTimer > AV_SYNC_THRESHOLD_MAX)
		is->frameTimer = time;
	SDL_LockMutex(is->videoFrameQueue.mutex);
	if (!isnan(vp->pts))
		UpdateVideoPts(is, vp->pts, vp->serial); // 更新视频时钟：时间戳、时钟时间
	SDL_UnlockMutex(is->videoFrameQueue.mutex);

	// 是否要丢弃未能及时播放的视频帧
	if (FrameQueueNumberRemaining(&is->videoFrameQueue) > 1) // 队列中未显示帧数>1(只有一帧则不考虑丢帧)
	{
		Frame* nextvp = FrameQueuePeekNext(&is->videoFrameQueue); // 下一帧:下一帧待显示的帧
		duration = VpDuration(is, vp, nextvp); // 下一帧理论播放时长 = nextvp->pts - vp->pts
		// 当前帧 vp 未能及时播放, 即下一帧播放完成时刻(is->frameTimer+duration)小于当前系统时刻(time)
		if (time > is->frameTimer + duration)
		{
			FrameQueueNext(&is->videoFrameQueue); // 删除上一帧已显示帧, 即删除 lastvp, 读指针加1(从lastvp更新到vp)
			goto RETRY;
		}
	}

	// 删除当前读指针元素, 读指针+1. 若未丢帧,读指针从lastvp更新到vp;若有丢帧,读指针从vp更新到nextvp
	FrameQueueNext(&is->videoFrameQueue);
DISPLAY:
	VideoDisplay(is); // 取出当前帧 vp(若有丢帧是 nextvp)进行播放
}

// 视频渲染线程
static int VideoPlayingThread(void* arg)
{
	PlayerStation* is = static_cast<PlayerStation*>(arg);
	double remainingTime = 0.0; // 当前帧还需展示的时间 (单位 s)
	while (1)
	{
		// 停止请求
		if (is->abortRequest)
			break;
		if (remainingTime > 0.0)
			av_usleep((unsigned)(remainingTime * 1000000.0));
		remainingTime = REFRESH_RATE;
		// 立即显示当前帧,或延时 remaining_time 后再显示
		VideoRefresh(is, &remainingTime);
	}
	return 0;
}

// 打开视频播放  初始化格式转化上下文, 初始化SDL 图像渲染功能, 开启视频渲染线程
static int OpenVideoPlaying(void* arg)
{
	PlayerStation* is = static_cast<PlayerStation*>(arg);
	int ret;
	// 视频缓冲区大小
	int bufferSize;
	// 视频缓冲区地址
	uint8_t* buffer = NULL;
	// 分配格式转换后的 frame
	is->pFrameYUV = av_frame_alloc();
	if (is->pFrameYUV == NULL)
	{
		cout << "av_frame_alloc() for p_frm_raw failed" << endl;
		return -1;
	}
	// 为AVFrame.*data[] 手工分配缓冲区，用于存储 sws_scale 中目的帧视频数据
	// 视频格式, 视频宽度, 视频高度, 视频对齐字节数
	// 此处 bufferSize = width*height*1.5
	bufferSize = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, is->pVideoCodecContext->width, is->pVideoCodecContext->height, 1);
	// 设置缓冲区空间
	buffer = static_cast<uint8_t*>(av_malloc(bufferSize));
	if (buffer == NULL)
	{
		cout << "av_malloc() for buffer failed" << endl;
		return -1;
	}

	// 使用给定参数设定 pFrameYUV->data 和 pFrameYUV->linesize
	ret = av_image_fill_arrays(is->pFrameYUV->data, is->pFrameYUV->linesize, buffer, AV_PIX_FMT_YUV420P, 
		is->pVideoCodecContext->width, is->pVideoCodecContext->height, 1);
	if (ret < 0)
	{
		cout << "av_image_fill_arrays() failed " << ret << endl;
		return -1;
	}

	// A2. 初始化 SWS context, 用于后续图像转换
	is->imgConvertContext = sws_getContext(is->pVideoCodecContext->width, is->pVideoCodecContext->height,
		is->pVideoCodecContext->pix_fmt, is->pVideoCodecContext->width, is->pVideoCodecContext->height,
		AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
	if (is->imgConvertContext == NULL)
	{
		cout << "sws_getContext() failed" << endl;
		return -1;
	}

	// SDL_Rect 赋值
	is->sdlVideo.rect.x = 0;
	is->sdlVideo.rect.y = 0;
	is->sdlVideo.rect.w = is->pVideoCodecContext->width;
	is->sdlVideo.rect.h = is->pVideoCodecContext->height;

	// 1.创建SDL窗口 (窗口名字, 窗口起始点x, 窗口起始点y, 窗口宽度w, 窗口高度h, 
	is->sdlVideo.window = SDL_CreateWindow("simple player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		is->sdlVideo.rect.w, is->sdlVideo.rect.h, SDL_WINDOW_OPENGL);
	if (is->sdlVideo.window == NULL)
	{
		cout << "SDL_CreateWindow() failed: " << SDL_GetError() << endl;
		return -1;
	}

	// 2.创建SDL_Renderer (渲染图片的窗口, 选择显卡, 软编)
	is->sdlVideo.renderer = SDL_CreateRenderer(is->sdlVideo.window, -1, 0);
	if (is->sdlVideo.renderer == NULL)
	{
		cout << "SDL_CreateRenderer() failed: " << SDL_GetError() << endl;
		return -1;
	}

	// 3.创建 SDL_Texture (渲染器上下文, 像素格式, 纹理权利(是否经常修改), 纹理宽, 纹理高)
	is->sdlVideo.texture = SDL_CreateTexture(is->sdlVideo.renderer,
		SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, is->sdlVideo.rect.w, is->sdlVideo.rect.h);
	if (is->sdlVideo.texture == NULL)
	{
		cout << "SDL_CreateTexture() failed " << SDL_GetError() << endl;
		return -1;
	}
	// 创建视频渲染线程
	is->videoPlayThreadID = SDL_CreateThread(VideoPlayingThread, "video playing thread", is);
	return 0;
}

// 打开视频流
static int OpenVideoStream(PlayerStation* is)
{
	// 编解码参数
	AVCodecParameters* pCodecPar = NULL;
	// 编解码器
	AVCodec* pCodec = NULL;
	// 编解码器上下文
	AVCodecContext* pCodecContext = NULL;
	// 视频流
	AVStream* pStream = is->pVideoStream;
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
	is->pVideoCodecContext = pCodecContext;

	// 2.创建视频解码线程
	is->videoDecodeThreadID = SDL_CreateThread(VideoDecodeThread, "video decode thread", is);
	return 0;
}

// 打开图像播放
int OpenVideo(PlayerStation* is)
{
	// 设置视频编码器上下文并创建视频解码器线程
	OpenVideoStream(is);
	// 初始化格式转化上下文, 初始化SDL 图像渲染功能, 创建视频渲染线程
	OpenVideoPlaying(is);
	return 0;
}
