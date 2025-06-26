#include "decodethread.h"
#include <QDebug>
#include <QElapsedTimer>
#include "demuxthread.h"
#include "rtspplayer.h"
#include "screenshotthread.h"

// 这是硬件解码器选择像素格式的关键回调
enum AVPixelFormat get_hw_format(AVCodecContext* ctx, const enum AVPixelFormat* pix_fmts)
{
    const enum AVPixelFormat* p;
    for (p = pix_fmts; *p != -1; p++) {
        if (*p == AV_PIX_FMT_D3D11) {
            return *p;
        }
    }
    qCritical() << "Failed to get D3D11 format.";
    return AV_PIX_FMT_NONE;
}

DecodeThread::DecodeThread(QQueue<AVPacket*>* packetQueue, QMutex* queueMutex, QQueue<AVFrame*>* packetQueue2, QMutex* queueMutex2, QObject* parent)
    : QThread(parent), m_packetQueue(packetQueue), m_queueMutex(queueMutex), m_showPackerQueue(packetQueue2),m_showMutex(queueMutex2)
{
}

DecodeThread::~DecodeThread()
{
    stop();
    wait();
}

void DecodeThread::stop()
{
    m_stopped = true;
}

bool DecodeThread::initHardwareDecoder(AVCodecParameters* params)
{
    // 1. 查找解码器
    const AVCodec* codec = avcodec_find_decoder(params->codec_id);
    if (!codec) {
        qCritical() << "Failed to find decoder for codec" << params->codec_id;
        return false;
    }

    // 2. 创建硬件设备上下文 (D3D11VA)
    int ret = av_hwdevice_ctx_create(&m_hwDeviceCtx, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0);
    if (ret < 0) {
        qCritical() << "Failed to create D3D11VA device context.";
        return false;
    }

    // 3. 创建解码器上下文
    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        qCritical() << "Failed to alloc codec context.";
        return false;
    }

    // 4. 将流参数复制到解码器上下文
    if (avcodec_parameters_to_context(m_codecCtx, params) < 0) {
        qCritical() << "Failed to copy codec parameters to decoder context.";
        return false;
    }

    // 5. 设置硬件解码回调和设备上下文
    m_codecCtx->get_format = get_hw_format;
    m_codecCtx->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);

    // 6. 打开解码器
    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
        qCritical() << "Failed to open codec.";
        return false;
    }

    qDebug() << "Hardware decoder initialized successfully (D3D11VA).";
    return true;
}

void DecodeThread::cleanup()
{
    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
        m_codecCtx = nullptr;
    }
    if (m_hwDeviceCtx) {
        av_buffer_unref(&m_hwDeviceCtx);
        m_hwDeviceCtx = nullptr;
    }
}
void DecodeThread::run()
{
    const int MAX_FRAME_QUEUE_SIZE = 15;
    // 等待 DemuxThread 初始化
    DemuxThread* demux = nullptr;
    while (!m_stopped) {
        demux = parent()->findChild<DemuxThread*>();
        if (demux && demux->videoStream()) {
            break;
        }
        msleep(10);
    }
    if (m_stopped || !demux) return;

    if (!initHardwareDecoder(demux->videoStream()->codecpar)) {
        cleanup();
        return;
    }

    AVFrame* hw_frame = av_frame_alloc();     // 硬件解码输出帧 (在GPU上)
    AVFrame* sw_frame = av_frame_alloc();     // 从GPU下载到CPU的帧 (NV12格式)

    // ★★★ 丢帧逻辑所需变量 ★★★
    QElapsedTimer streamTimer;                // 追踪真实物理时间
    int64_t first_pts = AV_NOPTS_VALUE;       // 记录第一帧的PTS，作为时间基准
    const AVRational timeBase = demux->videoStream()->time_base; // 获取输入流的时间基
    const int64_t threshold_ms = 100;         // 丢帧阈值，可以调整。100ms意味着允许大约3帧的延迟。

    while (!m_stopped) {
        m_queueMutex->lock();
        if (m_packetQueue->isEmpty()) {
            m_queueMutex->unlock();
            msleep(10);
            continue;
        }
        AVPacket* packet = m_packetQueue->dequeue();
        m_queueMutex->unlock();

        //发送信号告诉UI 已经接收到第一包可以显示了
        if (m_bSendSig)
        {
            if(packet->flags & AV_PKT_FLAG_KEY)
            {
                m_bSendSig = false;
                emit sigGetFirstFrame();
            }
        }

        int ret = avcodec_send_packet(m_codecCtx, packet);
        av_packet_free(&packet);

        if (ret < 0) {
            continue;
        }

        while (ret >= 0) {
            ret = avcodec_receive_frame(m_codecCtx, hw_frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            else if (ret < 0) {
                break;
            }

            // 如果帧没有被丢弃，才执行后续的昂贵操作
            int transfer_ret = av_hwframe_transfer_data(sw_frame, hw_frame, 0);
            av_frame_unref(hw_frame); // 硬件帧用完后立即释放

            if (transfer_ret < 0) {
                qWarning() << "Error transferring frame data from GPU to CPU.";
                continue;
            }

            if (!sw_frame->data[0] || !sw_frame->data[1]) {
                qWarning() << "Transferred frame data pointers are NULL!";
                continue;
            }

            /// <截图>
            if (m_screenshotFlag.load())
            {
                // 使用 compare_exchange 来确保只对一次请求截一张图
                bool expected = true;
                if (m_screenshotFlag.compare_exchange_strong(expected, false)) {

                    // 为新任务克隆一帧，因为sw_frame在循环中会被重用
                    AVFrame* frame_for_screenshot = av_frame_clone(sw_frame);

                    if (frame_for_screenshot) {
                        QString path;
                        {
                            QMutexLocker locker(&m_screenshotMutex);
                            path = m_screenshotPath;
                        }

                        // 创建并配置截图线程
                        ScreenshotThread* workerThread = new ScreenshotThread(frame_for_screenshot, path);

                        // 关键连接1：线程结束后自动删除对象，防止内存泄漏
                        connect(workerThread, &QThread::finished, workerThread, &QObject::deleteLater);

                        // 关键连接2：将截图结果信号连接到RTSPPlayer
                        RTSPPlayer* player = qobject_cast<RTSPPlayer*>(parent());
                        if (player) {
                            connect(workerThread, &ScreenshotThread::screenshotSaved, player, &RTSPPlayer::onScreenshotFinished, Qt::QueuedConnection);
                        }

                        // 启动线程
                        workerThread->start();
                    }
                }
            }
            /// </summary>

            m_showMutex->lock();
            if(m_showPackerQueue->size() > MAX_FRAME_QUEUE_SIZE)
            {
                av_frame_unref(sw_frame);
                m_showMutex->unlock();
                qDebug() << "Drop one frame";
                continue;
            }else
            {

                // 使用 av_frame_clone 来转移所有权，确保线程安全
                AVFrame* frame_to_emit = av_frame_clone(sw_frame);
                if (frame_to_emit) {
                    m_showPackerQueue->enqueue(frame_to_emit);
                }
                m_showMutex->unlock();
                av_frame_unref(sw_frame);
            }
        }
    }

    av_frame_free(&hw_frame);
    av_frame_free(&sw_frame);
    cleanup();
    qDebug() << "Decode thread finished.";
}


void DecodeThread::onScreenshotRequested(const QString& filePath)
{
    QMutexLocker locker(&m_screenshotMutex);
    m_screenshotPath = filePath;
    m_screenshotFlag = true; // 原子地设置标志，通知run()循环
    qDebug() << "DecodeThread: Screenshot request received for" << filePath;
}