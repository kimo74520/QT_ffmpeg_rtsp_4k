#ifndef DEMUXTHREAD_H
#define DEMUXTHREAD_H

#include <QElapsedTimer>
#include <QThread>
#include <QString>
#include <QQueue>
#include <QMutex>
#include <QDebug>

extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
}
// 1. ����һ���ṹ�����������ݸ��ص�����
struct InterruptCallbackData {
    QElapsedTimer timer;
    long timeout_ms = -1; // ��ʱʱ�䣬��λ����
};

// 2. ���徲̬�Ļص�����
//    ��������һ��ȫ�ֺ�����̬��Ա��������������ͨ�ĳ�Ա����
//    ��Ϊ����Ҫ��C���Է���FFmpeg�����
static int interrupt_callback(void* ctx)
{
    InterruptCallbackData* data = static_cast<InterruptCallbackData*>(ctx);
    if (!data) {
        return 0; // û���ṩ���ݣ����ж�
    }

    if (data->timer.hasExpired(data->timeout_ms)) {
        qDebug() << "Interrupt callback: Timeout detected!";
        return 1; // ����1��ʾ�ж�
    }

    return 0; // ����0��ʾ����
}
class DemuxThread : public QThread
{
    Q_OBJECT
public:
    DemuxThread(QQueue<AVPacket*>* decodeQueue, QMutex* decodeMutex,
        QQueue<AVPacket*>* recordQueue, QMutex* recordMutex,
        QObject* parent = nullptr);
    ~DemuxThread();

    void start(const QString& url);
    void stop();
    AVStream* videoStream() const { return m_videoStream; }
signals:
    void sigStreamFailed(QString error);
protected:
    void run() override;

private:
    QString m_url;
    volatile bool m_stopped = false;

    QQueue<AVPacket*>* m_decodeQueue;
    QMutex* m_decodeMutex;
    QQueue<AVPacket*>* m_recordQueue;
    QMutex* m_recordMutex;

    AVFormatContext* m_formatCtx = nullptr;
    AVStream* m_videoStream = nullptr;
    int m_videoStreamIndex = -1;
    InterruptCallbackData m_interruptCallbackData;
};

#endif // DEMUXTHREAD_H