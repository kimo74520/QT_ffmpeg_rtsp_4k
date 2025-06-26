#include <qfile.h>
#include <QDateTime>
#include <QtWidgets/QApplication>

#include "mainwindow.h"
QString pLogFile;

QString TimeToString(QDateTime time, QString formate);
void outputMessage(QtMsgType type, const QMessageLogContext& context, const QString& msg);
int main(int argc, char *argv[])
{
    Q_INIT_RESOURCE(QtWidgetsApplication2); // �����һ��
    QApplication app(argc, argv);
    QString time = TimeToString(QDateTime::currentDateTime(), "yyyy-MM-dd");  //��ȡ��������
    pLogFile = QCoreApplication::applicationDirPath() + "/Test" + time + ".log"; 
    qInstallMessageHandler(outputMessage);  //ע��MessageHandler

    MainWindow window;
    window.show();
    return app.exec();
}
QString TimeToString(QDateTime time, QString formate)
{
    QLocale local(QLocale::Chinese);
    return local.toString(time, formate);
}
void outputMessage(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    //qDebug("This is a debug message");    ������Ϣ
    //qInfo("This is a info message");    ��Ϣ
    //qWarning("This is a warning message");    ������Ϣ
    //qCritical("This is a critical message");  ���ش���
    //qFatal("This is a fatal message");    ��������

    if (msg.compare("Unknown property box-shadow") == 0
        || msg.compare("Unknown property indicator-height") == 0
        || type == QtWarningMsg)
    {
        return;
    }

    static QMutex mutex;
    mutex.lock();

    QString text;
    switch (type)
    {
    case QtDebugMsg:
        text = QString("Debug:");
        break;
    case QtWarningMsg:
        text = QString("Warning:");
        mutex.unlock();
        return;
    case QtCriticalMsg:
        text = QString("Critical:");
        break;
    case QtFatalMsg:
        text = QString("Fatal:");
        break;
    case QtInfoMsg:
        text = QString("Info:");
        break;
    default:
        mutex.unlock();
        return;
    }

    //QString context_info = QString("File:(%1) Line:(%2)").arg(QString(context.file)).arg(context.line);
    QString current_date_time = TimeToString(QDateTime::currentDateTime(), "yyyy-MM-dd HH:mm:ss,zzz");
    QString message = QString("%1 %2 - %3").arg(current_date_time).arg(text).arg(msg);

    QFile file(pLogFile);
    bool b = file.open(QIODevice::WriteOnly | QIODevice::Append);
    if (b)
    {
        QTextStream text_stream(&file);
        text_stream << message << "\r\n";
        file.flush();
        file.close();
    }

    mutex.unlock();
}
