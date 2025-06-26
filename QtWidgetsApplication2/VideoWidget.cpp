#include "videowidget.h"
#include <QOpenGLShader>
#include <QDebug>
#include <QImage>
#include <QQueue>

#define  MAX_QUEUE_SIZE 30
// YUV420P 渲染的顶点着色器
const char* vertexShaderSource =
"attribute vec4 vertexIn;\n"
"attribute vec2 textureIn;\n"
"varying vec2 textureOut;\n"
"void main(void)\n"
"{\n"
"    gl_Position = vertexIn;\n"
"    textureOut = textureIn;\n"
"}\n";

const char* fragmentShaderSource_NV12_fixed =
"varying vec2 textureOut;\n"
"uniform sampler2D tex_y;\n"
"uniform sampler2D tex_uv;\n"
"void main(void)\n"
"{\n"
"    // 1. 从纹理采样原始 YUV 值\n"
"    float y_raw = texture2D(tex_y, textureOut).r;\n"
"    vec2 uv_raw = texture2D(tex_uv, textureOut).rg;\n"
"\n"
"    // 2. 将 Limited Range (16-235 for Y, 16-240 for UV) 扩展到 Full Range (0-1.0)\n"
"    // Y: (y_raw * 255 - 16) / (235 - 16) / 255 = (y_raw - 16.0/255.0) / (219.0/255.0)\n"
"    float y = (y_raw - 0.0627) / 0.8588;\n"
"    // UV: (uv_raw * 255 - 16) / (240 - 16) / 255 = (uv_raw - 16.0/255.0) / (224.0/255.0)\n"
"    vec2 uv = (uv_raw - 0.0627) / 0.8784;\n"
"    uv -= 0.5; // 将 UV 从 [0,1] 偏移到 [-0.5, 0.5]\n"
"\n"
"    // 3. 使用标准 BT.709 转换矩阵 (HDTV 标准)\n"
"    float r = y + 1.402 * uv.y;\n"
"    float g = y - 0.344136 * uv.x - 0.714136 * uv.y;\n"
"    float b = y + 1.772 * uv.x;\n"
"\n"
"    gl_FragColor = vec4(r, g, b, 1.0);\n"
"}\n";

VideoWidget::VideoWidget(QWidget* parent) : QOpenGLWidget(parent)/*, m_screenshotRequested(false)*/
{
    m_timer.setInterval(33);
    m_timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&m_timer, &QTimer::timeout, this, &VideoWidget::UpdateImg);
    m_timer.start();

}

VideoWidget::~VideoWidget()
{
    cleanup();
}

void VideoWidget::UpdateImg()
{
    update();
}

void VideoWidget::initializeGL()
{
    initializeOpenGLFunctions();

    m_program = new QOpenGLShaderProgram(this);
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource_NV12_fixed);
    m_program->link();

	m_program->bind();
    m_program->setUniformValue("tex_y", 0);
    m_program->setUniformValue("tex_uv", 1);
    m_program->release();

    glGenTextures(1, &m_textureY);
    glGenTextures(1, &m_textureUV);

    glBindTexture(GL_TEXTURE_2D, m_textureY);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, GL_NONE);

    glBindTexture(GL_TEXTURE_2D, m_textureUV);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, GL_NONE);
}

void VideoWidget::paintGL()
{
    if (m_queue == nullptr || m_mutex == nullptr)
    {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    m_mutex->lock();
    if (!m_queue->isEmpty()) 
    {
        if (m_frame) 
        {
            av_frame_free(&m_frame);
        }
        m_frame = m_queue->dequeue();
    }
    m_mutex->unlock(); 

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (!m_frame || !m_frame->data[0] || !m_frame->data[1]) 
    {
        return;
    }

    if (m_videoW != m_frame->width || m_videoH != m_frame->height) {
        m_videoW = m_frame->width;
        m_videoH = m_frame->height;
        glBindTexture(GL_TEXTURE_2D, m_textureY);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, m_videoW, m_videoH, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, m_textureUV);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG, m_videoW / 2, m_videoH / 2, 0, GL_RG, GL_UNSIGNED_BYTE, nullptr);
    }

	glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_textureY);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, m_frame->linesize[0]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_videoW, m_videoH, GL_RED, GL_UNSIGNED_BYTE, m_frame->data[0]);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_textureUV);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, m_frame->linesize[1] / 2);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_videoW / 2, m_videoH / 2, GL_RG, GL_UNSIGNED_BYTE, m_frame->data[1]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

	m_program->bind();

	float videoAspect = (float)m_videoW / (float)m_videoH;
    float widgetAspect = (float)this->width() / (float)this->height();


    float scaleX = 1.0f;
    float scaleY = 1.0f;
    if (widgetAspect > videoAspect) 
    {
    	scaleX = videoAspect / widgetAspect;
    }
    else 
    {
        scaleY = widgetAspect / videoAspect;
    }

    const GLfloat vertices_raw[] = 
    {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f,
    };

	const GLfloat texcoords[] = 
    {
        0.0f, 1.0f,
        1.0f, 1.0f,
        0.0f, 0.0f,
        1.0f, 0.0f,
    };

	const GLfloat vertices[] = 
    {
        vertices_raw[0] * scaleX, vertices_raw[1] * scaleY,
        vertices_raw[2] * scaleX, vertices_raw[3] * scaleY,
        vertices_raw[4] * scaleX, vertices_raw[5] * scaleY,
        vertices_raw[6] * scaleX, vertices_raw[7] * scaleY,
    };

    int vertexIn = m_program->attributeLocation("vertexIn");
    int textureIn = m_program->attributeLocation("textureIn");

    m_program->enableAttributeArray(vertexIn);
    m_program->enableAttributeArray(textureIn);

    m_program->setAttributeArray(vertexIn, GL_FLOAT, vertices, 2);
    m_program->setAttributeArray(textureIn, GL_FLOAT, texcoords, 2);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    m_program->disableAttributeArray(vertexIn);
    m_program->disableAttributeArray(textureIn);
    m_program->release();
}



void VideoWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void VideoWidget::cleanup()
{
    makeCurrent(); 

    clearScreen();

    if (m_program) 
    {
        delete m_program;
        m_program = nullptr;
    }

    glDeleteTextures(1, &m_textureY);
    glDeleteTextures(1, &m_textureUV);

    doneCurrent();
}



void VideoWidget::clearScreen()
{
    if (m_mutex) 
    { 
        QMutexLocker locker(m_mutex);
        if (m_queue) 
        {
            while (!m_queue->isEmpty()) 
            {
                AVFrame* frame = m_queue->dequeue();
                av_frame_free(&frame);
            }
        }
        if (m_frame) 
        {
            av_frame_free(&m_frame);
            m_frame = nullptr;
        }
    }
    else 
    {
        if (m_frame) 
        {
            av_frame_free(&m_frame);
            m_frame = nullptr;
        }
    }
    m_videoW = 0;
    m_videoH = 0;

    update();
}