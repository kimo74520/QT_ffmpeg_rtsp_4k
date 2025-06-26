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
    m_startTime = AV_NOPTS_VALUE; // ���� ������ʼʱ��
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
            // ���֮ǰ��¼�ƣ�����ֹͣ�ˣ���Ҫ�ر��ļ�
            if (m_outputFmtCtx) {
                closeFile();
            }

            // ���� �ؼ��޸�������¼��ʱ�����¼�ƶ����Է�ֹ�ѻ� ����
            m_queueMutex->lock();
            while (!m_packetQueue->isEmpty()) {
                AVPacket* p = m_packetQueue->dequeue();
                av_packet_free(&p); // ȡ��������
            }
            m_queueMutex->unlock();

            // �������ߣ������ת����CPU
            msleep(100);
            continue;
        }

        // ���� �����߼��ع���ʼ ����

        m_queueMutex->lock();
        if (m_packetQueue->isEmpty()) {
            m_queueMutex->unlock();
            msleep(10);
            continue;
        }
        AVPacket* packet = m_packetQueue->dequeue();
        m_queueMutex->unlock();

        // ����1�����¼�ƻ�û������ʼ���ļ�δ�򿪣�����ȴ��ؼ�֡������
        if (!m_outputFmtCtx) {
            // ����ǲ��ǹؼ�֡
            if (packet->flags & AV_PKT_FLAG_KEY) {
                // �ǹؼ�֡��̫���ˣ����ڿ�ʼ��ʼ���ļ���
                qDebug() << "Key frame detected. Starting record initialization...";
                if(m_isRealStart == false)
                {
                    m_isRealStart = true;
                    emit sigRealRecordStart();
                }
                    
                // ��ʼ�����������
                if (avformat_alloc_output_context2(&m_outputFmtCtx, nullptr, nullptr, m_filePath.toStdString().c_str()) < 0) {
                    qWarning() << "Could not create output context";
                    m_isRecording = false; // ¼��ʧ��
                    av_packet_free(&packet);
                    continue;
                }

                // �����Ƶ��
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

                // ������ļ�
                if (!(m_outputFmtCtx->oformat->flags & AVFMT_NOFILE)) {
                    if (avio_open(&m_outputFmtCtx->pb, m_filePath.toStdString().c_str(), AVIO_FLAG_WRITE) < 0) {
                        qWarning() << "Could not open output file" << m_filePath;
                        closeFile();
                        m_isRecording = false;
                        av_packet_free(&packet);
                        continue;
                    }
                }

                // д���ļ�ͷ
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

        // ��ִ�е�����ģ�Ҫô�ǵ�һ���ؼ�֡��Ҫô�Ǻ�������ͨ֡��
        packet->pts -= m_startTime;
        packet->dts -= m_startTime;

        // ʱ���ת��
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