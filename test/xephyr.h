#pragma once

#include <QProcess>

class Xephyr : public QProcess
{
public:
    explicit Xephyr(const QByteArray &display)
    {
        start(QStringLiteral("Xephyr -ac -br -noreset -screen 640x480 ") + display);
        waitForStarted();
        QThread::sleep(1);
        qputenv("DISPLAY", display);
    }

    ~Xephyr() Q_DECL_OVERRIDE
    {
        terminate();
        waitForFinished();
    }
};
