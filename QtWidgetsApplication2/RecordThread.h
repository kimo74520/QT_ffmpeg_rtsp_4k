#ifndef RECORDTHREAD_H
#define RECORDTHREAD_H

#include <QThread>
#include <QQueue>
#include <QMutex>
#include <QString>
#include <atomic>

extern "C" {
#include "libavformat/avformat.h"
}

class RecordThread : public QThread
{
    Q_OBJECT
public:
    RecordThread(QQueue<AVPacket*>* packetQueue, QMutex* queueMutex, QObject* parent = nullptr);
    ~RecordThread();

    void startRecord(const QString& filePath, AVStream* videoStream);
    void stopRecord();
    void stop();
signals:
    void sigRealRecordStart();
    void sigRecordFinished(QString path);
protected:
    void run() override;

private:
    void closeFile();

    QQueue<AVPacket*>* m_packetQueue;
    QMutex* m_queueMutex;

    std::atomic<bool> m_isRecording;
    volatile bool m_stopped = false;

    AVFormatContext* m_outputFmtCtx = nullptr;
    AVStream* m_inVideoStream = nullptr;
    QString m_filePath;
    int64_t m_startTime = AV_NOPTS_VALUE;
    bool m_isRealStart = false;
};

#endif // RECORDTHREAD_H