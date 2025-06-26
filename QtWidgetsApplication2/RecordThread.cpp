#include "recordthread.h"
#include <QDebug>

RecordThread::RecordThread(QQueue<AVPacket*>* packetQueue, QMutex* queueMutex, QObject* parent)
    : QThread(parent), m_packetQueue(packetQueue), m_queueMutex(queueMutex), m_isRecording(false)
{
}

RecordThread::~RecordThread()
{
    stop();
    wait();
}

void RecordThread::startRecord(const QString& filePath, AVStream* videoStream)
{
    if (m_isRecording) {
        return;
    }
    m_filePath = filePath;
    m_inVideoStream = videoStream;
    m_isRecording = true;
    m_startTime = AV_NOPTS_VALUE; // ★★★ 重置起始时间
}

void RecordThread::stopRecord()
{
    m_isRecording = false;
}

void RecordThread::stop()
{
    m_stopped = true;
    m_isRecording = false;
}

void RecordThread::closeFile()
{
    if (m_outputFmtCtx) {
        av_write_trailer(m_outputFmtCtx);
        if (!(m_outputFmtCtx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&m_outputFmtCtx->pb);
        }
        avformat_free_context(m_outputFmtCtx);
        m_outputFmtCtx = nullptr;
        qDebug() << "Recording stopped and file saved to" << m_filePath;
        emit sigRecordFinished(m_filePath);
    }
}

void RecordThread::run()
{
    while (!m_stopped) {
        if (!m_isRecording) {
            // 如果之前在录制，现在停止了，需要关闭文件
            if (m_outputFmtCtx) {
                closeFile();
            }

            // ★★★ 关键修复：当不录制时，清空录制队列以防止堆积 ★★★
            m_queueMutex->lock();
            while (!m_packetQueue->isEmpty()) {
                AVPacket* p = m_packetQueue->dequeue();
                av_packet_free(&p); // 取出并丢弃
            }
            m_queueMutex->unlock();

            // 短暂休眠，避免空转消耗CPU
            msleep(100);
            continue;
        }

        // ★★★ 核心逻辑重构开始 ★★★

        m_queueMutex->lock();
        if (m_packetQueue->isEmpty()) {
            m_queueMutex->unlock();
            msleep(10);
            continue;
        }
        AVPacket* packet = m_packetQueue->dequeue();
        m_queueMutex->unlock();

        // 步骤1：如果录制还没真正开始（文件未打开），则等待关键帧来启动
        if (!m_outputFmtCtx) {
            // 检查是不是关键帧
            if (packet->flags & AV_PKT_FLAG_KEY) {
                // 是关键帧，太好了！现在开始初始化文件。
                qDebug() << "Key frame detected. Starting record initialization...";
                if(m_isRealStart == false)
                {
                    m_isRealStart = true;
                    emit sigRealRecordStart();
                }
                    
                // 初始化输出上下文
                if (avformat_alloc_output_context2(&m_outputFmtCtx, nullptr, nullptr, m_filePath.toStdString().c_str()) < 0) {
                    qWarning() << "Could not create output context";
                    m_isRecording = false; // 录制失败
                    av_packet_free(&packet);
                    continue;
                }

                // 添加视频流
                AVStream* outStream = avformat_new_stream(m_outputFmtCtx, nullptr);
                if (!outStream) {
                    qWarning() << "Failed allocating output stream";
                    closeFile(); 
                    m_isRecording = false;
                    av_packet_free(&packet);
                    continue;
                }
                avcodec_parameters_copy(outStream->codecpar, m_inVideoStream->codecpar);
                outStream->codecpar->codec_tag = 0;

                // 打开输出文件
                if (!(m_outputFmtCtx->oformat->flags & AVFMT_NOFILE)) {
                    if (avio_open(&m_outputFmtCtx->pb, m_filePath.toStdString().c_str(), AVIO_FLAG_WRITE) < 0) {
                        qWarning() << "Could not open output file" << m_filePath;
                        closeFile();
                        m_isRecording = false;
                        av_packet_free(&packet);
                        continue;
                    }
                }

                // 写入文件头
                if (avformat_write_header(m_outputFmtCtx, nullptr) < 0) {
                    qWarning() << "Error occurred when writing header";
                    closeFile();
                    m_isRecording = false;
                    av_packet_free(&packet);
                    continue;
                }

                qDebug() << "Recording started to" << m_filePath;
                m_startTime = packet->pts;
            }
            else 
            {
                av_packet_free(&packet);
                continue;
            }
        }

        // 能执行到这里的，要么是第一个关键帧，要么是后续的普通帧。
        packet->pts -= m_startTime;
        packet->dts -= m_startTime;

        // 时间基转换
        av_packet_rescale_ts(packet, m_inVideoStream->time_base, m_outputFmtCtx->streams[0]->time_base);
        packet->stream_index = 0;
        packet->pos = -1;

        if (av_interleaved_write_frame(m_outputFmtCtx, packet) < 0) {
            qWarning() << "Error muxing packet";
        }

        av_packet_free(&packet);
    }

    closeFile();
    qDebug() << "Record thread finished.";
}