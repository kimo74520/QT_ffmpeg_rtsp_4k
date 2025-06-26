#include "rtspplayer.h"
#include "videowidget.h"

RTSPPlayer::RTSPPlayer(QObject* parent) : QObject(parent)
{
    avformat_network_init();
}

RTSPPlayer::~RTSPPlayer()
{
    stopPlay();
    avformat_network_deinit();
}

void RTSPPlayer::setVideoWidget(VideoWidget* widget)
{
    m_videoWidget = widget;
    m_videoWidget->setMyQueue(&m_showPacketQueue);
    m_videoWidget->setMyMutex(&m_showMutex);
}

void RTSPPlayer::startPlay(const QString& url)
{
    if (m_demuxThread && m_demuxThread->isRunning()) {
        return;
    }

    stopPlay(); //确保清理旧的

    m_rtspUrl = url;

    m_demuxThread = new DemuxThread(&m_decodePacketQueue, &m_decodeMutex, &m_recordPacketQueue, &m_recordMutex, this);
    m_decodeThread = new DecodeThread(&m_decodePacketQueue, &m_decodeMutex,&m_showPacketQueue, &m_showMutex, this);
    m_recordThread = new RecordThread(&m_recordPacketQueue, &m_recordMutex, this);
    connect(m_decodeThread, &DecodeThread::sigGetFirstFrame, this, &RTSPPlayer::sigGetFirstFrame, Qt::QueuedConnection);
	connect(m_demuxThread, &DemuxThread::sigStreamFailed, this, &RTSPPlayer::sigStreamFailed, Qt::QueuedConnection);
    connect(m_recordThread, &RecordThread::sigRealRecordStart, this, &RTSPPlayer::sigRealRecordStart, Qt::QueuedConnection);
    connect(m_recordThread, &RecordThread::sigRecordFinished, this, &RTSPPlayer::sigRecordFinished, Qt::QueuedConnection);
    connect(this, &RTSPPlayer::screenshotRequested, m_decodeThread, &DecodeThread::onScreenshotRequested, Qt::QueuedConnection);

    m_decodeThread->start();
    m_recordThread->start();
    m_demuxThread->start(m_rtspUrl);
}

void RTSPPlayer::stopPlay()
{
    if (m_demuxThread) {
        m_demuxThread->stop();
        m_demuxThread->wait();
        delete m_demuxThread;
        m_demuxThread = nullptr;
    }

    if (m_decodeThread) {
        m_decodeThread->stop();
        m_decodeThread->wait();
        delete m_decodeThread;
        m_decodeThread = nullptr;
    }

    if (m_recordThread) {
        m_recordThread->stop();
        m_recordThread->wait();
        delete m_recordThread;
        m_recordThread = nullptr;
    }

    // 清理队列
    auto clearQueue = [](QQueue<AVPacket*>& queue, QMutex& mutex) {
        QMutexLocker locker(&mutex);
        while (!queue.isEmpty()) {
            AVPacket* temp = queue.dequeue();
            av_packet_free(&temp);
        }
    };
    clearQueue(m_decodePacketQueue, m_decodeMutex);
    clearQueue(m_recordPacketQueue, m_recordMutex);
}

void RTSPPlayer::startRecord(const QString& filePath)
{
    if (m_demuxThread && m_recordThread) {
        m_recordThread->startRecord(filePath, m_demuxThread->videoStream());
    }
}

void RTSPPlayer::stopRecord()
{
    if (m_recordThread) {
        m_recordThread->stopRecord();
    }
}

void RTSPPlayer::screenshot(const QString& filePath)
{
    emit screenshotRequested(filePath);
}
void RTSPPlayer::onScreenshotFinished(const QString& filePath, bool success)
{
    emit screenshotFinished(filePath, success);
}