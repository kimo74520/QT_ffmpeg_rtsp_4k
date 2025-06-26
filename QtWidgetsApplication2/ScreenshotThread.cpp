#include "screenshotthread.h"
#include <QDebug>
#include <QImage>

extern "C" {
#include "libavutil/frame.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h" // for av_image_get_buffer_size
}

ScreenshotThread::ScreenshotThread(AVFrame* frameToSave, const QString& filePath, QObject* parent)
    : QThread(parent), m_frame(frameToSave), m_filePath(filePath)
{
}

ScreenshotThread::~ScreenshotThread()
{
    // 确保线程拥有的AVFrame被释放
    if (m_frame) {
        av_frame_free(&m_frame);
    }
    qDebug() << "ScreenshotThread destroyed.";
}

void ScreenshotThread::run()
{
    qDebug() << "ScreenshotThread started for file:" << m_filePath;
    bool success = false;

    if (m_frame && !m_filePath.isEmpty()) 
    {
        AVFrame* frame = m_frame;
        int width = frame->width;
        int height = frame->height;

        AVFrame* rgbFrame = av_frame_alloc();
        SwsContext* sws_ctx = nullptr;
        uint8_t* buffer = nullptr;

        do { // 使用do-while(0)来方便错误处理和资源释放
            if (!rgbFrame) break;

            int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, width, height, 1);
            buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));
            if (!buffer) break;

            av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, buffer, AV_PIX_FMT_RGB24, width, height, 1);

            sws_ctx = sws_getContext(
                width, height, (AVPixelFormat)frame->format,
                width, height, AV_PIX_FMT_RGB24,
                SWS_BILINEAR, nullptr, nullptr, nullptr
            );
            if (!sws_ctx) break;

            sws_scale(
                sws_ctx, (const uint8_t* const*)frame->data,
                frame->linesize, 0, height,
                rgbFrame->data, rgbFrame->linesize
            );

            // QImage构造函数会复制数据，所以可以立即保存
            QImage image(rgbFrame->data[0], width, height, QImage::Format_RGB888);
            if (image.save(m_filePath)) {
                success = true;
                qDebug() << "Original resolution screenshot saved to" << m_filePath;
            }
            else {
                qWarning() << "Failed to save screenshot to" << m_filePath;
            }

        } while (0);

        // 清理所有分配的资源
        if (sws_ctx) sws_freeContext(sws_ctx);
        if (buffer) av_freep(&buffer);
        if (rgbFrame) av_frame_free(&rgbFrame);
    }

    emit screenshotSaved(m_filePath, success);
}