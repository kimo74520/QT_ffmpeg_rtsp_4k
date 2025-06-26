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
// 1. 定义一个结构体来传递数据给回调函数
struct InterruptCallbackData {
    QElapsedTimer timer;
    long timeout_ms = -1; // 超时时间，单位毫秒
};

// 2. 定义静态的回调函数
//    它必须是一个全局函数或静态成员函数，不能是普通的成员函数
//    因为它需要被C语言风格的FFmpeg库调用
static int interrupt_callback(void* ctx)
{
    InterruptCallbackData* data = static_cast<InterruptCallbackData*>(ctx);
    if (!data) {
        return 0; // 没有提供数据，不中断
    }

    if (data->timer.hasExpired(data->timeout_ms)) {
        qDebug() << "Interrupt callback: Timeout detected!";
        return 1; // 返回1表示中断
    }

    return 0; // 返回0表示继续
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