#include "demuxthread.h"
#include <QDebug>

DemuxThread::DemuxThread(QQueue<AVPacket*>* decodeQueue, QMutex* decodeMutex,
    QQueue<AVPacket*>* recordQueue, QMutex* recordMutex,
    QObject* parent)
    : QThread(parent), m_decodeQueue(decodeQueue), m_decodeMutex(decodeMutex),
    m_recordQueue(recordQueue), m_recordMutex(recordMutex)
{
}

DemuxThread::~DemuxThread()
{
    stop();
    wait();
}

void DemuxThread::start(const QString& url)
{
    m_url = url;
    m_stopped = false;
    QThread::start();
}

void DemuxThread::stop()
{
    m_stopped = true;
}

void DemuxThread::run()
{
    m_formatCtx = avformat_alloc_context();
    if (!m_formatCtx) 
    {
        qCritical() << "Could not allocate format context";
        return;
    }

    // 设置回调数据
    m_interruptCallbackData.timeout_ms = 5000; // 设置5秒超时
    m_interruptCallbackData.timer.start(); // 启动计时器

    // 将回调函数和数据关联到 format context
    m_formatCtx->interrupt_callback.callback = interrupt_callback;
    m_formatCtx->interrupt_callback.opaque = &m_interruptCallbackData;

    AVDictionary* options = nullptr;
    av_dict_set(&options, "rtsp_transport", "tcp", 0); // 使用TCP模式，防止丢包
    av_dict_set(&options, "stimeout", "5000000", 0);   // 5秒超时

    int ret = avformat_open_input(&m_formatCtx, m_url.toStdString().c_str(), nullptr, &options);
    av_dict_free(&options);

    if (ret < 0) 
    {
        if (ret == AVERROR_EXIT) 
        {
            qCritical() << "avformat_open_input timed out by interrupt callback.";
        }
        else 
        {
            char errbuf[1024] = { 0 };
            av_strerror(ret, errbuf, sizeof(errbuf));
            qCritical() << "Could not open input stream:" << m_url << "Error:" << errbuf;
        }
        // ★★★ 在这里可以加入重连逻辑，或者发射一个失败信号 ★★★
        avformat_close_input(&m_formatCtx); // 确保清理
        m_formatCtx = nullptr;
        emit sigStreamFailed(u8"连接失败，请确认网络链接是否正常或流地址是否正确。");
        return;
    }

    if (avformat_find_stream_info(m_formatCtx, nullptr) < 0) 
    {
        qCritical() << "Could not find stream information";
        emit sigStreamFailed(u8"获取流信息失败");
        return;
    }

    m_videoStreamIndex = av_find_best_stream(m_formatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (m_videoStreamIndex < 0) 
    {
        qCritical() << "Could not find video stream";
        return;
    }
    m_videoStream = m_formatCtx->streams[m_videoStreamIndex];

    while (!m_stopped) 
    {
        AVPacket* packet = av_packet_alloc();
        m_interruptCallbackData.timer.restart();
        ret = av_read_frame(m_formatCtx, packet);

        if (ret < 0) 
        {
            av_packet_free(&packet);
            if (m_stopped) break;  // 文件结束或出错
                        // 检查错误码
            if (ret == AVERROR_EOF) 
            {
                //对方停止了发送
                qDebug() << "End of file reached.";
                emit sigStreamFailed(u8"文件结束。");
                break;
            }
            else if (ret == AVERROR_EXIT) 
            {
                //这个是对方突然断网，然后超时
                qCritical() << "av_read_frame timed out by interrupt callback. Stream seems to be dead.";
                emit sigStreamFailed(u8"网络连接超时，程序中止。");
                break; // 退出循环
            }
            else 
            {
                //这里只实验了我方突然断网
                char errbuf[1024] = { 0 };
                av_strerror(ret, errbuf, sizeof(errbuf));
                qWarning() << "av_read_frame error:" << errbuf;
                emit sigStreamFailed(u8"请检查网络连接，网线断开或网卡被禁用。程序中止。");
                break; 
            }
        }

        if (packet->stream_index == m_videoStreamIndex) 
        {
            // 为解码队列克隆一份
            AVPacket* decodePacket = av_packet_clone(packet);
            m_decodeMutex->lock();
            m_decodeQueue->enqueue(decodePacket);
            m_decodeMutex->unlock();

            // 为录制队列克隆一份
            AVPacket* recordPacket = av_packet_clone(packet);
            m_recordMutex->lock();
            m_recordQueue->enqueue(recordPacket);
            m_recordMutex->unlock();
        }

        av_packet_unref(packet);
        av_packet_free(&packet);
    }

    if (m_formatCtx) 
    {
        avformat_close_input(&m_formatCtx);
        m_formatCtx = nullptr;
    }
    qDebug() << "Demux thread finished.";
}