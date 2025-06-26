#ifndef VIDEOWIDGET_H
#define VIDEOWIDGET_H

#include <QElapsedTimer>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QMutex>
#include <QTimer>
#include <QQueue>
extern "C" {
#include "libavutil/frame.h"
}

class VideoWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    explicit VideoWidget(QWidget* parent = nullptr);
    ~VideoWidget();
    void clearScreen();
    void setMyQueue(QQueue< AVFrame*>* queue)
    {
        m_queue = queue;
    }

    void setMyMutex(QMutex* mutex)
    {
        m_mutex = mutex;
    }

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
private slots:
    void UpdateImg();
private:
    void cleanup();

    QOpenGLShaderProgram* m_program = nullptr;
    AVFrame* m_frame = nullptr;

    QQueue< AVFrame*>* m_queue = nullptr;
    QMutex* m_mutex;

    GLuint m_textureY, m_textureUV;
    int m_videoW = 0, m_videoH = 0;

    QTimer m_timer;
};

#endif // VIDEOWIDGET_H