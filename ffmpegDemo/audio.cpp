#include "audio.h"
#include "packet.h"
#include "frame.h"

Audio::Audio()
	:m_swrContext(NULL),
	m_pAudioFrame(NULL),
	m_pAudioFrameRwrBuffer(NULL),
	m_pAudioCodecContext(NULL),
	m_player(NULL),
	m_packetQueue(NULL),
	m_pStream(NULL),
	m_decodeThread(NULL),
	m_audioFrameRwrSize(0),
	m_audioHardwareBufferSize(0),
	m_audioFrameSize(0),
	m_audioCopyIndex(0),
	m_audioClock(0)
{
	ZeroMemory(&m_frameQueue, sizeof(m_frameQueue));
	ZeroMemory(&m_audioParamSource, sizeof(m_audioParamSource));
	ZeroMemory(&m_audioPlayClock, sizeof(m_audioPlayClock));
}

BOOL Audio::Init(PacketQueue* pPacketQueue, Player* player)
{
	m_packetQueue = pPacketQueue;
	if (FrameQueueInit(&m_frameQueue, pPacketQueue, SAMPLE_QUEUE_SIZE, 1) < 0)
		return FALSE;
	m_player = player;
	InitClock(&m_audioPlayClock);
	m_pStream = player->GetDemux()->GetStream(FALSE);
	return TRUE;
}

BOOL Audio::Open()
{
	OpenAudioStream(); // 设置音频解码上下文(设置时间基), 创建音频解码线程
	OpenAudioPlaying(); // 打开音频播放设备，并设置回调函数
	return TRUE;
}

void Audio::Close()
{
	SDL_PauseAudioDevice(m_audioDevice, 1);
	FrameQueueSignal(&m_frameQueue);
	PacketQueueAbort(m_packetQueue);
	SDL_WaitThread(m_decodeThread, NULL);
	FrameQueueDestroy(&m_frameQueue);
}

PlayClock* Audio::GetClock()
{
	return &m_audioPlayClock;
}

BOOL Audio::OnDecodeThread()
{
	AVFrame* pFrame = av_frame_alloc();
	Frame* audioFrame;

	BOOL receive = FALSE;
	AVRational timebase;
	int ret = 0;

	if (pFrame == NULL)
		return AVERROR(ENOMEM);

	while (1)
	{
		if (m_player->IsStop())
			break;
		// 解码
		receive = AudioDecodeFrame(m_pAudioCodecContext, m_packetQueue, pFrame);
		if (!receive)
			goto END;

		timebase = AVRational{ 1, pFrame->sample_rate };
		if (!(audioFrame = FrameQueuePeekWritable(&m_frameQueue)))
			goto END;
		// 里面只是时间基的换算。这里是换算到真实时间
		audioFrame->pts = (pFrame->pts == AV_NOPTS_VALUE) ? NAN : pFrame->pts * av_q2d(timebase);
		// 当前帧包含的（单个声道）采样数/采样率就是当前帧的播放时长
		audioFrame->duration = av_q2d(AVRational{ pFrame->nb_samples, pFrame->sample_rate });
		// 将 frame 数据拷入 audioFrame->frame，audioFrame->frame指向音频 frame 队列尾部
		av_frame_move_ref(audioFrame->frame, pFrame);
		// 更新音频 frame 队列大小及写指针
		FrameQueuePush(&m_frameQueue);
	}
END:
	av_frame_free(&pFrame);
	return ret;
}

void Audio::SetClockAt(PlayClock* clock, DOUBLE pts, DOUBLE time)
{
	clock->pts = pts;	// 设置渲染时间
	clock->lastUpdated = time;	// 设置上次更新时间
	clock->ptsDrift = clock->pts - time;	// 当前帧显示时间戳与当前系统时钟时间的差值
}

void Audio::SetClock(PlayClock* clock, DOUBLE pts)
{
	// time 单位(ns)
	double time = av_gettime_relative() / 1000000.0; // av_gettime_relative 获取自某个未指定起点以来的当前时间（以微秒为单位）
	SetClockAt(clock, pts, time);
}

void Audio::InitClock(PlayClock* clock)
{
	SetClock(clock, NAN);
}

void Audio::OnSDLAudioCallback(UINT8* stream, INT32 needSize)
{
	// 回调开始的时间
	INT64 audioCallbackTime = av_gettime_relative();

	if (m_player->IsPause())
	{
		memset(stream, 0, needSize);
		return;
	}
	while (needSize > 0)
	{
		// 索引超出范围，则去获取新数据
		if (m_audioCopyIndex >= static_cast<INT32>(m_audioFrameSize))
		{
			// 1.从音频 frame 队列中取出一个 frame，转换为音频设备支持的格式，返回值是重采样音频帧的大小
			INT32 audioSize = AudioResample(audioCallbackTime);
			if (audioSize < 0)
			{
				m_pAudioFrame = NULL;
				m_audioFrameSize = SDL_AUDIO_MIN_BUFFER_SIZE / m_audioParamTarget.frameSize * m_audioParamTarget.frameSize;
			}
			else
				m_audioFrameSize = audioSize;
			m_audioCopyIndex = 0;
		}
		// 引入 is->audioCopyIndex 的作用：防止一帧音频数据大小超过 SDL 音频缓冲区大小，这样一帧数据需要经过多次拷贝
		// 用 is->audioCopyIndex 标识重采样帧中已拷入 SDL 音频缓冲区的数据位置索引， len1 标识本次拷贝的数据量
		INT32 copySize = m_audioFrameSize - m_audioCopyIndex;
		if (copySize > needSize)
			copySize = needSize;
		// 2.将转换后的音频数据拷贝到音频缓冲区 stream 中，之后的播放就是音频设备驱动程序工作
		if (m_pAudioFrame != NULL)
			memcpy(stream, static_cast<UINT8*>(m_pAudioFrame) + m_audioCopyIndex, copySize);
		else
			memset(stream, 0, copySize);
		needSize -= copySize;
		stream += copySize;
		m_audioCopyIndex += copySize;
	}

	// 3.更新时钟
	if (!isnan(m_audioClock))
	{
		// -DSS TODO 这个 clock 是不是要加锁
		// 更新音频时钟，更新时刻：每次往声卡缓冲区拷入数据后
		// 前面 audioDecodeFrame 中更新的 is->audioClock 是以音频帧为单位，所以此处第二个参数要减去未拷贝数据量占用的时间
		// 2 * m_audioHardwareBufferSize ；减去一个表示这个回调帧的渲染时间，在减去一个表示当前真正播放帧的渲染时间

		INT32 remainSize = m_audioFrameSize - m_audioCopyIndex;
		SetClockAt(&m_audioPlayClock, m_audioClock - static_cast<DOUBLE>(2 * m_audioHardwareBufferSize + remainSize) /
			m_audioParamTarget.bytesPerSec, audioCallbackTime / 1000000.0);
	}
}

BOOL Audio::AudioDecodeFrame(AVCodecContext* pCodecContext, PacketQueue* pPacketQueue, AVFrame* frame)
{
	INT32 ret;
	while (1)
	{
		AVPacket pkt;
		while (1)
		{
			if (m_player->IsStop())
				return TRUE;
			// 3.2 一个音频 packet 含一至多个 frame，每次 avcodec_receive_frame() 返回一个 frame
			ret = avcodec_receive_frame(pCodecContext, frame);
			// -DSS TODO 能获取数据，是不是也要把数据放到解码队列里面，感觉这样快一点
			if (ret >= 0)
			{
				// 时基变换，从 d->avcontex->packetTimebase 时基转换到 1/frame->sampleRate 时基
				AVRational timebase = AVRational{ 1, frame->sample_rate };
				if (frame->pts != AV_NOPTS_VALUE)
					// pts 进行转换
					frame->pts = av_rescale_q(frame->pts, pCodecContext->pkt_timebase, timebase);
				else
					av_log(NULL, AV_LOG_WARNING, "frame->pts no\n");
				return TRUE;
			}
			else if (ret == AVERROR_EOF)
			{
				av_log(NULL, AV_LOG_INFO, "audio avcodec_receive_frame(): the decoder has been flushed\n");
				avcodec_flush_buffers(pCodecContext);
				return 0;
			}
			else if (ret == AVERROR(EAGAIN))
			{
				av_log(NULL, AV_LOG_INFO, "audio avcodec_receive_frame(): input is not accepted in the current state\n");
				break;
			}
			else
			{
				av_log(NULL, AV_LOG_INFO, "audio avcodec_receive_frame(): other errors\n");
				continue;
			}
		}
		// 1.取出一个 packet, 使用 packet 对应的 serial 赋值给 d->packetSerial
		if (PacketQueueGet(pPacketQueue, &pkt, TRUE) < 0)
			return FALSE;
		// packetQueue 中第一个总是 flushPacket。每次 seek 操作会插入 flushPacket，更新 serial, 开启新的播放序列
		if (pkt.data == NULL)
			avcodec_flush_buffers(pCodecContext);  // 复位解码器内部状态/刷新内部缓冲区。当 seek 操作或切换流时应调用此函数
		else
		{
			// 2.将 packet 发送给解码器
			// 发送给 packet 的顺序按 dts 递增顺序
			// packet.pos 变量可以标识当前 packet 在视频文件中的地址偏移
			if (avcodec_send_packet(pCodecContext, &pkt) == AVERROR(EAGAIN))
				av_log(NULL, AV_LOG_ERROR, "receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
			av_packet_unref(&pkt);
		}
	}
	return TRUE;
}

BOOL Audio::OpenAudioStream()
{
	AVCodecContext* pCodecContext = NULL;
	AVCodecParameters* pCodecParameters = NULL;
	AVCodec* pCodec = NULL;
	INT32 ret;

	// 1.为音频流构建解码器 AVCodecContext
	// 1.1 获取解码器参数 AVCodecParameters
	pCodecParameters = m_pStream->codecpar;
	// 1.2 获取解码器
	pCodec = const_cast<AVCodec*>(avcodec_find_decoder(pCodecParameters->codec_id));
	if (pCodec == NULL)
	{
		av_log(NULL, AV_LOG_ERROR, "Can't find codec!\n");
		return -1;
	}

	// 1.3 构建解码器 AVCodecContext
	// 1.3.1 pCodecContext 初始化：分配结构体，使用 pCodec 初始化相应的成员函数为默认值
	pCodecContext = avcodec_alloc_context3(pCodec);
	if (pCodecContext == NULL)
	{
		av_log(NULL, AV_LOG_ERROR, "avcodec_alloc_context3() failed\n");
		return -1;
	}

	// 1.3.2 pCodecContext 初始化：pCodecParameters ==>pCodecContext, 初始化相应成员
	ret = avcodec_parameters_to_context(pCodecContext, pCodecParameters);
	if (ret < 0)
	{
		av_log(NULL, AV_LOG_ERROR, "avcodec_parameters_to_context() failed %d\n", ret);
		return -1;
	}

	// 1.3.3 pCodecContext 初始化：使用 pCodec 初始化 pCodecContext，初始化完成
	ret = avcodec_open2(pCodecContext, pCodec, NULL);
	if (ret < 0)
	{
		av_log(NULL, AV_LOG_ERROR, "avcodecc_open2() failed %d\n", ret);
		return -1;
	}
	// 时间基准
	pCodecContext->pkt_timebase = m_pStream->time_base;
	// 设置音频解码上下文
	m_pAudioCodecContext = pCodecContext;

	// 2.创建音频解码线程
	m_decodeThread = SDL_CreateThread(DecodeThread, "audio decode thread", this);
	return 0;
}

INT32 Audio::AudioResample(INT64 callbackTime)
{
	INT32 resampledDataSize;
	INT64 decodeChannelLayout;
	INT32 wantedNumberSamples;
	Frame* audioFrame;

	// 队列中没有帧则等待
	while (FrameQueueNumberRemaining(&m_frameQueue) == 0)
	{
		// 等待时间太长，则退出
		if ((av_gettime_relative() - callbackTime) > 1000000LL * m_audioHardwareBufferSize / m_audioParamTarget.bytesPerSec / 2)
			return -1;
		av_usleep(1000);
	}

	// 若队列头部可读，则由 audioFrame 指向可读帧
	if (!(audioFrame = FrameQueuePeekReadable(&m_frameQueue)))
		return -1;
	// 删除上一帧
	FrameQueueNext(&m_frameQueue);

	// 获取声道布局
	// 判断 frame 保存的 channel_layout 和 channels 是否对应
	decodeChannelLayout = (audioFrame->frame->channel_layout &&
		audioFrame->frame->channels == av_get_channel_layout_nb_channels(audioFrame->frame->channel_layout)) ?
		audioFrame->frame->channel_layout : av_get_default_channel_layout(audioFrame->frame->channels);
	wantedNumberSamples = audioFrame->frame->nb_samples;

	// is->audioParamTarget 是 SDL 可接受的音频帧数
	if (audioFrame->frame->format != m_audioParamSource.fmt ||
		decodeChannelLayout != m_audioParamSource.channelLayout ||
		audioFrame->frame->sample_rate != m_audioParamSource.freq)
	{
		swr_free(&m_swrContext);
		// 设置重采样参数
		m_swrContext = swr_alloc_set_opts(NULL, m_audioParamTarget.channelLayout,
			m_audioParamTarget.fmt, m_audioParamTarget.freq,
			decodeChannelLayout, static_cast<AVSampleFormat>(audioFrame->frame->format),
			audioFrame->frame->sample_rate, 0, NULL);
		if (!m_swrContext || swr_init(m_swrContext) < 0)
		{
			av_log(NULL, AV_LOG_ERROR, "Can't create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
				audioFrame->frame->sample_rate,
				av_get_sample_fmt_name(static_cast<AVSampleFormat>(audioFrame->frame->format)),
				audioFrame->frame->channels, m_audioParamTarget.freq, av_get_sample_fmt_name(m_audioParamTarget.fmt),
				m_audioParamTarget.channels);
			swr_free(&m_swrContext);
			return -1;
		}
		// 使用 frame 中的参数更新 is->audioSource
		m_audioParamSource.channelLayout = decodeChannelLayout;
		m_audioParamSource.channels = audioFrame->frame->channels;
		m_audioParamSource.freq = audioFrame->frame->sample_rate;
		m_audioParamSource.fmt = static_cast<AVSampleFormat>(audioFrame->frame->format);
	}

	if (m_swrContext)
	{
		const UINT8** in = const_cast<const UINT8**>(audioFrame->frame->extended_data);
		UINT8** out = &m_pAudioFrameRwrBuffer;
		INT32 outCount = static_cast<int64_t>(wantedNumberSamples * m_audioParamTarget.freq / audioFrame->frame->sample_rate + 256);
		INT32 outSize = av_samples_get_buffer_size(NULL, m_audioParamTarget.channels, outCount, m_audioParamTarget.fmt, 0);
		INT32 len2;
		if (outSize < 0)
		{
			av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
			return -1;
		}
		av_fast_malloc(&m_pAudioFrameRwrBuffer, &m_audioFrameRwrSize, outSize);
		if (!m_pAudioFrameRwrBuffer)
			return AVERROR(ENOMEM);
		// 音频重采样：返回值是重采样后得到的音频数据中单个声道的样本数
		len2 = swr_convert(m_swrContext, out, outCount, in, audioFrame->frame->nb_samples);
		if (len2 < 0)
		{
			av_log(NULL, AV_LOG_WARNING, "audio buffer is probably too small\n");
			if (swr_init(m_swrContext) < 0)
				swr_free(&m_swrContext);
		}
		m_pAudioFrame = m_pAudioFrameRwrBuffer;
		// 重采样返回的一帧音频数据大小
		resampledDataSize = len2 * m_audioParamTarget.channels * av_get_bytes_per_sample(m_audioParamTarget.fmt);
	}
	else
	{
		// 未经重采样，则将指针指向frame 中的音频数据
		m_pAudioFrame = audioFrame->frame->data[0];
		// 根据 frame 中指向的音频参数获取缓冲区大小
		resampledDataSize = av_samples_get_buffer_size(NULL, audioFrame->frame->channels, audioFrame->frame->nb_samples,
			static_cast<AVSampleFormat>(audioFrame->frame->format), 1);;
	}

	if (!isnan(audioFrame->pts))
		m_audioClock = audioFrame->pts + static_cast<DOUBLE>(audioFrame->frame->nb_samples) / audioFrame->frame->sample_rate;
	else
		m_audioClock = NAN;
	return resampledDataSize;
}

BOOL Audio::OpenAudioPlaying()
{
	// 文件中保存音频的格式
	SDL_AudioSpec wantedSpec;
	// 音频播放设备需要的格式
	SDL_AudioSpec actualSpec;
	// 2. 打开音频设备并创建音频处理线程
	// 2.1 打开音频设备，获取 SDL 设备支持的音频参数 actualSpec
	wantedSpec.freq = m_pAudioCodecContext->sample_rate; // 采样率( 每秒采样的样本数)
	wantedSpec.format = AUDIO_S16SYS;  // 样本格式  Signed 16-bit samples
	wantedSpec.channels = m_pAudioCodecContext->channels; // 声音通道数
	wantedSpec.silence = 0; // 静音值
	wantedSpec.samples = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE, 2 << av_log2(wantedSpec.freq / SDL_AUDIO_MAX_CALLBACKS_PER_SEC)); // 每个通道的样本数
	wantedSpec.callback = SDLAudioCallback; // 回调函数
	wantedSpec.userdata = this; // 用户数据
	if (!(m_audioDevice = SDL_OpenAudioDevice(NULL, 0, &wantedSpec, &actualSpec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE)))
	{
		av_log(NULL, AV_LOG_ERROR, "SDL_OpenAudio() failed: %s\n", SDL_GetError());
		return -1;
	}

	if (actualSpec.format != AUDIO_S16SYS)
	{
		av_log(NULL, AV_LOG_ERROR, "Audio Device not support S16SYS\n");
		return -1;
	}
	// 2.2 根据 SDL 音频参数构建音频重采样参数
	m_audioParamTarget.fmt = AV_SAMPLE_FMT_S16; // 目标音频格式
	m_audioParamTarget.freq = actualSpec.freq; // 目标音频频率
	// av_get_channel_layout_nb_channels  av_get_default_channel_layout
	m_audioParamTarget.channelLayout = av_get_default_channel_layout(actualSpec.channels); // 目标音频布局
	m_audioParamTarget.channels = actualSpec.channels; // 目标音频通道数
	// 目标音频帧大小(channels*fmt(对应字节数))
	m_audioParamTarget.frameSize = av_samples_get_buffer_size(NULL, actualSpec.channels, 1, m_audioParamTarget.fmt, 1);
	// 每秒的字节数
	m_audioParamTarget.bytesPerSec = av_samples_get_buffer_size(NULL, actualSpec.channels, actualSpec.freq, m_audioParamTarget.fmt, 1);
	if (m_audioParamTarget.bytesPerSec <= 0 || m_audioParamTarget.frameSize <= 0)
	{
		av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size failed\n");
		return -1;
	}
	m_audioParamSource = m_audioParamTarget;
	// 每帧的字节数
	m_audioHardwareBufferSize = actualSpec.size;
	m_audioFrameSize = 0;
	m_audioCopyIndex = 0;

	// 3. 暂停/ 继续音频回调处理
	SDL_PauseAudioDevice(m_audioDevice, 0);
}

BOOL Audio::DecodeThread(void* arg)
{
	Audio* is = static_cast<Audio*>(arg);
	return is->OnDecodeThread();
}

void Audio::SDLAudioCallback(void* opaque, UINT8* stream, INT32 len)
{
	Audio* is = static_cast<Audio*>(opaque);
	return is->OnSDLAudioCallback(stream, len);
}
