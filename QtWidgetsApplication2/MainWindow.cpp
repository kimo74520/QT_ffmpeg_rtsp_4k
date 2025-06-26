#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QDebug>
#include <QDateTime>

QString sst = "image: url(:/QtWidgetsApplication2/Image/NoVideo.svg);background - color: rgb(230, 230, 230); ";
QString sst1 = "image: url(:/QtWidgetsApplication2/Image/Connecting.svg);background - color: rgb(230, 230, 230); ";
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

    m_player = new RTSPPlayer(this);
    connect(m_player, &RTSPPlayer::sigGetFirstFrame, this, &MainWindow::slotGetFirstFrame);
    connect(m_player, &RTSPPlayer::screenshotFinished, this, &MainWindow::slotScreenshotFinished);
    connect(m_player, &RTSPPlayer::sigStreamFailed, this, &MainWindow::slotStreamFailed);
    connect(m_player, &RTSPPlayer::sigRecordFinished, this, &MainWindow::slotRecordFinished);
    connect(m_player, &RTSPPlayer::sigRealRecordStart, this,[=]
    {
        ui.recordButton->setEnabled(true);
    });

    m_player->setVideoWidget(ui.openGLWidget);
    ui.openGLWidget->setVisible(false);

    ui.rtspUrlLineEdit->setText("rtsp://192.168.89.34:8554/test");
}

MainWindow::~MainWindow()
{
    if(m_isPlaying)
    {        
        if (m_isRecording) {
            on_recordButton_clicked();
        }
        m_player->stopPlay();
    }
    delete ui.openGLWidget;
    delete m_player;
}

void MainWindow::on_startButton_clicked()
{
    if (!m_isPlaying) 
    {
        QString url = ui.rtspUrlLineEdit->text();
        if (!url.isEmpty()) 
        {
            m_player->startPlay(url);
            ui.startButton->setText(u8"ֹͣ");
            m_isPlaying = true;
        }
        ui.lbl_NoPlaying->setStyleSheet(sst1);
    }
    else {
        // ֹͣ����ʱ��Ҳֹͣ¼��
        if (m_isRecording) 
        {
            on_recordButton_clicked();
        }
        m_player->stopPlay();
        ui.startButton->setText(u8"��ʼ");
        m_isPlaying = false;
        ui.openGLWidget->clearScreen();
        ui.openGLWidget->setVisible(false);

        ui.lbl_NoPlaying->setStyleSheet(sst);
        ui.lbl_NoPlaying->setVisible(true);
        
        ui.recordButton->setEnabled(false);
        ui.snapshotButton->setEnabled(false);
    }
}

void MainWindow::on_recordButton_clicked()
{
    if (!m_isPlaying) 
        return;

    if (!m_isRecording) 
    {
        QString filePath = QCoreApplication::applicationDirPath()+"/" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".mp4";
        if (!filePath.isEmpty()) 
        {
            ui.recordButton->setEnabled(false);
            m_player->startRecord(filePath);
            ui.recordButton->setText(u8"ֹͣ¼��");
            m_isRecording = true;
        }
    }
    else 
    {
        m_player->stopRecord();
        ui.recordButton->setText(u8"¼��");
        m_isRecording = false;
    }
}

void MainWindow::on_snapshotButton_clicked()
{
    if (!m_isPlaying) return;
    ui.snapshotButton->setEnabled(false);
    ui.snapshotButton->setText(u8"��ͼ�С�����");

    QString filePath = QCoreApplication::applicationDirPath() + "/" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".png";
    if (!filePath.isEmpty()) 
    {
        m_player->screenshot(filePath);
    }
}

void MainWindow::slotScreenshotFinished(const QString& filePath, bool success)
{
    ui.snapshotButton->setEnabled(true);
    ui.snapshotButton->setText(u8"��ͼ");

    if (success) 
    {
        QMessageBox::information(this, u8"��ͼ�ɹ�", u8"ͼƬ�ѱ��浽: " + filePath);
    }
    else {
        QMessageBox::warning(this, u8"��ͼʧ��", u8"�޷�����ͼƬ��");
    }
}

void MainWindow::slotRecordFinished(QString path)
{
    QMessageBox::information(this, u8"¼�Ƴɹ�", u8"¼���ѱ��浽: " + path);
}

void MainWindow::slotStreamFailed(QString error)
{
    QMessageBox::warning(this, u8"����", error);

	if(m_isPlaying)
	{
        if (m_isRecording) 
        {
            on_recordButton_clicked();
        }
        ui.recordButton->setEnabled(false);
        ui.snapshotButton->setEnabled(false);
        ui.openGLWidget->clearScreen();

        ui.openGLWidget->setVisible(false);

        ui.lbl_NoPlaying->setStyleSheet(sst);
        ui.lbl_NoPlaying->setVisible(true);

        m_player->stopPlay();
        ui.startButton->setText(u8"��ʼ");
        m_isPlaying = false;

	}
}

void MainWindow::slotGetFirstFrame()
{
    ui.recordButton->setEnabled(true);
    ui.snapshotButton->setEnabled(true);

    ui.openGLWidget->setVisible(true);
    ui.lbl_NoPlaying->setVisible(false);
}