#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow> // Or QWidget if you prefer a simpler window
#include <QImage>
#include <QThread>
#include "ui_MainWindow.h"
#include "rtspplayer.h"


class VideoWorker; // Forward declaration

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_startButton_clicked();
    void on_recordButton_clicked();
    void on_snapshotButton_clicked();
    void slotScreenshotFinished(const QString& filePath, bool success);
    void slotRecordFinished(QString path);
    void slotStreamFailed(QString error);
    void slotGetFirstFrame();
    //void displayFrame(const QImage &frame);
    //void updateStatus(const QString &status);
    //void handleError(const QString &error);
    //void handleRecordingStarted(bool success);
    //void handleRecordingStopped();
    //void handleSnapshotTaken(bool success, const QString& path);


private:
    Ui_MainWindowClass ui;
    RTSPPlayer* m_player;
    bool m_isPlaying = false;
    bool m_isRecording = false;
};
#endif // MAINWINDOW_H