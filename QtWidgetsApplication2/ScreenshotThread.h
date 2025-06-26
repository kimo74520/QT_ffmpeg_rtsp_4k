#ifndef SCREENSHOTTHREAD_H
#define SCREENSHOTTHREAD_H

#include <QThread>
#include <QString>

// forward declaration
struct AVFrame;

class ScreenshotThread : public QThread
{
    Q_OBJECT
public:
    explicit ScreenshotThread(AVFrame* frameToSave, const QString& filePath, QObject* parent = nullptr);
    ~ScreenshotThread();

protected:
    void run() override;

signals:
    // 我们可以定义一个更具体的信号，传递成功与否和文件路径
    void screenshotSaved(const QString& filePath, bool success);

private:
    AVFrame* m_frame;
    QString m_filePath;
};

#endif // SCREENSHOTTHREAD_H