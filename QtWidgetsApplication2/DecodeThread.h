#ifndef DECODETHREAD_H
#define DECODETHREAD_H

#include <QThread>
#include <QQueue>
#include <QMutex>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavutil/hwcontext.h"
#include "libavutil/pixdesc.h"
}

class DecodeThread : public QThread
{
    Q_OBJECT
public:
    DecodeThread(QQueue<AVPacket*>* packetQueue, QMutex* queueMutex, QQueue<AVFrame*>* packetQueue2, QMutex* queueMutex2, QObject* parent = nullptr);
    ~DecodeThread();

    void stop();

public slots:
    void onScreenshotRequested(const QString& filePath);

protected:
    void run() override;
signals:
    void sigGetFirstFrame();
private:
    bool initHardwareDecoder(AVCodecParameters* params);
    void cleanup();

    QQueue<AVPacket*>* m_packetQueue;
    QMutex* m_queueMutex;
    QQueue<AVFrame*>* m_showPackerQueue;
    QMutex* m_showMutex;

    volatile bool m_stopped = false;

    AVCodecContext* m_codecCtx = nullptr;
    AVBufferRef* m_hwDeviceCtx = nullptr;

    std::atomic<bool> m_screenshotFlag;
    QString m_screenshotPath;
    QMutex m_screenshotMutex;
    bool m_bSendSig = true;//�Ƿ���Ҫ�����źŸ���UI����һ֡�Ѿ�����

};

// ȫ�ֻص�����������ѡ��Ӳ�����ظ�ʽ
enum AVPixelFormat get_hw_format(AVCodecContext* ctx, const enum AVPixelFormat* pix_fmts);

#endif // DECODETHREAD_H