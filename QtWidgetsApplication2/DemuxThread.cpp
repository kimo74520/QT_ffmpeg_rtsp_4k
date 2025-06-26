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

    // ���ûص�����
    m_interruptCallbackData.timeout_ms = 5000; // ����5�볬ʱ
    m_interruptCallbackData.timer.start(); // ������ʱ��

    // ���ص����������ݹ����� format context
    m_formatCtx->interrupt_callback.callback = interrupt_callback;
    m_formatCtx->interrupt_callback.opaque = &m_interruptCallbackData;

    AVDictionary* options = nullptr;
    av_dict_set(&options, "rtsp_transport", "tcp", 0); // ʹ��TCPģʽ����ֹ����
    av_dict_set(&options, "stimeout", "5000000", 0);   // 5�볬ʱ

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
        // ���� ��������Լ��������߼������߷���һ��ʧ���ź� ����
        avformat_close_input(&m_formatCtx); // ȷ������
        m_formatCtx = nullptr;
        emit sigStreamFailed(u8"����ʧ�ܣ���ȷ�����������Ƿ�����������ַ�Ƿ���ȷ��");
        return;
    }

    if (avformat_find_stream_info(m_formatCtx, nullptr) < 0) 
    {
        qCritical() << "Could not find stream information";
        emit sigStreamFailed(u8"��ȡ����Ϣʧ��");
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
            if (m_stopped) break;  // �ļ����������
                        // ��������
            if (ret == AVERROR_EOF) 
            {
                //�Է�ֹͣ�˷���
                qDebug() << "End of file reached.";
                emit sigStreamFailed(u8"�ļ�������");
                break;
            }
            else if (ret == AVERROR_EXIT) 
            {
                //����ǶԷ�ͻȻ������Ȼ��ʱ
                qCritical() << "av_read_frame timed out by interrupt callback. Stream seems to be dead.";
                emit sigStreamFailed(u8"�������ӳ�ʱ��������ֹ��");
                break; // �˳�ѭ��
            }
            else 
            {
                //����ֻʵ�����ҷ�ͻȻ����
                char errbuf[1024] = { 0 };
                av_strerror(ret, errbuf, sizeof(errbuf));
                qWarning() << "av_read_frame error:" << errbuf;
                emit sigStreamFailed(u8"�����������ӣ����߶Ͽ������������á�������ֹ��");
                break; 
            }
        }

        if (packet->stream_index == m_videoStreamIndex) 
        {
            // Ϊ������п�¡һ��
            AVPacket* decodePacket = av_packet_clone(packet);
            m_decodeMutex->lock();
            m_decodeQueue->enqueue(decodePacket);
            m_decodeMutex->unlock();

            // Ϊ¼�ƶ��п�¡һ��
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