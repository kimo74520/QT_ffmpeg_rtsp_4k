#include "decodethread.h"
#include <QDebug>
#include <QElapsedTimer>
#include "demuxthread.h"
#include "rtspplayer.h"
#include "screenshotthread.h"

// ����Ӳ��������ѡ�����ظ�ʽ�Ĺؼ��ص�
enum AVPixelFormat get_hw_format(AVCodecContext* ctx, const enum AVPixelFormat* pix_fmts)
{
    const enum AVPixelFormat* p;
    for (p = pix_fmts; *p != -1; p++) {
        if (*p == AV_PIX_FMT_D3D11) {
            return *p;
        }
    }
    qCritical() << "Failed to get D3D11 format.";
    return AV_PIX_FMT_NONE;
}

DecodeThread::DecodeThread(QQueue<AVPacket*>* packetQueue, QMutex* queueMutex, QQueue<AVFrame*>* packetQueue2, QMutex* queueMutex2, QObject* parent)
    : QThread(parent), m_packetQueue(packetQueue), m_queueMutex(queueMutex), m_showPackerQueue(packetQueue2),m_showMutex(queueMutex2)
{
}

DecodeThread::~DecodeThread()
{
    stop();
    wait();
}

void DecodeThread::stop()
{
    m_stopped = true;
}

bool DecodeThread::initHardwareDecoder(AVCodecParameters* params)
{
    // 1. ���ҽ�����
    const AVCodec* codec = avcodec_find_decoder(params->codec_id);
    if (!codec) {
        qCritical() << "Failed to find decoder for codec" << params->codec_id;
        return false;
    }

    // 2. ����Ӳ���豸������ (D3D11VA)
    int ret = av_hwdevice_ctx_create(&m_hwDeviceCtx, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0);
    if (ret < 0) {
        qCritical() << "Failed to create D3D11VA device context.";
        return false;
    }

    // 3. ����������������
    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        qCritical() << "Failed to alloc codec context.";
        return false;
    }

    // 4. �����������Ƶ�������������
    if (avcodec_parameters_to_context(m_codecCtx, params) < 0) {
        qCritical() << "Failed to copy codec parameters to decoder context.";
        return false;
    }

    // 5. ����Ӳ������ص����豸������
    m_codecCtx->get_format = get_hw_format;
    m_codecCtx->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);

    // 6. �򿪽�����
    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
        qCritical() << "Failed to open codec.";
        return false;
    }

    qDebug() << "Hardware decoder initialized successfully (D3D11VA).";
    return true;
}

void DecodeThread::cleanup()
{
    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
        m_codecCtx = nullptr;
    }
    if (m_hwDeviceCtx) {
        av_buffer_unref(&m_hwDeviceCtx);
        m_hwDeviceCtx = nullptr;
    }
}
void DecodeThread::run()
{
    const int MAX_FRAME_QUEUE_SIZE = 15;
    // �ȴ� DemuxThread ��ʼ��
    DemuxThread* demux = nullptr;
    while (!m_stopped) {
        demux = parent()->findChild<DemuxThread*>();
        if (demux && demux->videoStream()) {
            break;
        }
        msleep(10);
    }
    if (m_stopped || !demux) return;

    if (!initHardwareDecoder(demux->videoStream()->codecpar)) {
        cleanup();
        return;
    }

    AVFrame* hw_frame = av_frame_alloc();     // Ӳ���������֡ (��GPU��)
    AVFrame* sw_frame = av_frame_alloc();     // ��GPU���ص�CPU��֡ (NV12��ʽ)

    // ���� ��֡�߼�������� ����
    QElapsedTimer streamTimer;                // ׷����ʵ����ʱ��
    int64_t first_pts = AV_NOPTS_VALUE;       // ��¼��һ֡��PTS����Ϊʱ���׼
    const AVRational timeBase = demux->videoStream()->time_base; // ��ȡ��������ʱ���
    const int64_t threshold_ms = 100;         // ��֡��ֵ�����Ե�����100ms��ζ�������Լ3֡���ӳ١�

    while (!m_stopped) {
        m_queueMutex->lock();
        if (m_packetQueue->isEmpty()) {
            m_queueMutex->unlock();
            msleep(10);
            continue;
        }
        AVPacket* packet = m_packetQueue->dequeue();
        m_queueMutex->unlock();

        //�����źŸ���UI �Ѿ����յ���һ��������ʾ��
        if (m_bSendSig)
        {
            if(packet->flags & AV_PKT_FLAG_KEY)
            {
                m_bSendSig = false;
                emit sigGetFirstFrame();
            }
        }

        int ret = avcodec_send_packet(m_codecCtx, packet);
        av_packet_free(&packet);

        if (ret < 0) {
            continue;
        }

        while (ret >= 0) {
            ret = avcodec_receive_frame(m_codecCtx, hw_frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            else if (ret < 0) {
                break;
            }

            // ���֡û�б���������ִ�к����İ������
            int transfer_ret = av_hwframe_transfer_data(sw_frame, hw_frame, 0);
            av_frame_unref(hw_frame); // Ӳ��֡����������ͷ�

            if (transfer_ret < 0) {
                qWarning() << "Error transferring frame data from GPU to CPU.";
                continue;
            }

            if (!sw_frame->data[0] || !sw_frame->data[1]) {
                qWarning() << "Transferred frame data pointers are NULL!";
                continue;
            }

            /// <��ͼ>
            if (m_screenshotFlag.load())
            {
                // ʹ�� compare_exchange ��ȷ��ֻ��һ�������һ��ͼ
                bool expected = true;
                if (m_screenshotFlag.compare_exchange_strong(expected, false)) {

                    // Ϊ�������¡һ֡����Ϊsw_frame��ѭ���лᱻ����
                    AVFrame* frame_for_screenshot = av_frame_clone(sw_frame);

                    if (frame_for_screenshot) {
                        QString path;
                        {
                            QMutexLocker locker(&m_screenshotMutex);
                            path = m_screenshotPath;
                        }

                        // ���������ý�ͼ�߳�
                        ScreenshotThread* workerThread = new ScreenshotThread(frame_for_screenshot, path);

                        // �ؼ�����1���߳̽������Զ�ɾ�����󣬷�ֹ�ڴ�й©
                        connect(workerThread, &QThread::finished, workerThread, &QObject::deleteLater);

                        // �ؼ�����2������ͼ����ź����ӵ�RTSPPlayer
                        RTSPPlayer* player = qobject_cast<RTSPPlayer*>(parent());
                        if (player) {
                            connect(workerThread, &ScreenshotThread::screenshotSaved, player, &RTSPPlayer::onScreenshotFinished, Qt::QueuedConnection);
                        }

                        // �����߳�
                        workerThread->start();
                    }
                }
            }
            /// </summary>

            m_showMutex->lock();
            if(m_showPackerQueue->size() > MAX_FRAME_QUEUE_SIZE)
            {
                av_frame_unref(sw_frame);
                m_showMutex->unlock();
                qDebug() << "Drop one frame";
                continue;
            }else
            {

                // ʹ�� av_frame_clone ��ת������Ȩ��ȷ���̰߳�ȫ
                AVFrame* frame_to_emit = av_frame_clone(sw_frame);
                if (frame_to_emit) {
                    m_showPackerQueue->enqueue(frame_to_emit);
                }
                m_showMutex->unlock();
                av_frame_unref(sw_frame);
            }
        }
    }

    av_frame_free(&hw_frame);
    av_frame_free(&sw_frame);
    cleanup();
    qDebug() << "Decode thread finished.";
}


void DecodeThread::onScreenshotRequested(const QString& filePath)
{
    QMutexLocker locker(&m_screenshotMutex);
    m_screenshotPath = filePath;
    m_screenshotFlag = true; // ԭ�ӵ����ñ�־��֪ͨrun()ѭ��
    qDebug() << "DecodeThread: Screenshot request received for" << filePath;
}