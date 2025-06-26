#ifndef RTSPPLAYER_H
#define RTSPPLAYER_H

#include <QObject>
#include <QString>
#include <QQueue>
#include <QMutex>

extern "C" {
#include "libavcodec/avcodec.h"
}

#include "demuxthread.h"
#include "decodethread.h"
#include "recordthread.h"

class VideoWidget;

class RTSPPlayer : public QObject
{
    Q_OBJECT
public:
    explicit RTSPPlayer(QObject* parent = nullptr);
    ~RTSPPlayer();

    void setVideoWidget(VideoWidget* widget);
    void startPlay(const QString& url);
    void stopPlay();

    void startRecord(const QString& filePath);
    void stopRecord();

    void screenshot(const QString& filePath);

signals:
    void screenshotRequested(const QString& filePath);
    void screenshotFinished(const QString& filePath, bool success);
    void sigRealRecordStart();
    void sigRecordFinished(QString);
    void sigStreamFailed(QString error);
    void sigGetFirstFrame();

public slots:
    void onScreenshotFinished(const QString& filePath, bool success);
private:
    DemuxThread* m_demuxThread = nullptr;
    DecodeThread* m_decodeThread = nullptr;
    RecordThread* m_recordThread = nullptr;

    // 线程安全队列
    QQueue<AVPacket*> m_decodePacketQueue;
    QMutex m_decodeMutex;

    QQueue<AVPacket*> m_recordPacketQueue;
    QMutex m_recordMutex;

    QQueue<AVFrame*> m_showPacketQueue;
    QMutex m_showMutex;

    QString m_rtspUrl;
    VideoWidget* m_videoWidget = nullptr;
};

#endif // RTSPPLAYER_H