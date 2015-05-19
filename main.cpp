#include "compositor.h"

#include <QGuiApplication>
#include <QOpenGLContext>
#include <QOpenGLDebugMessage>
#include <QOpenGLFunctions>
#include <QScreen>
#include <QQmlContext>
#include <QQuickView>
#include <QX11Info>
#include <QThread>

#include <xcb/xcb.h>
#include <xcb/damage.h>
#include <xcb/composite.h>

#include "windowpixmapitem.h"

class DebugLog : public QObject
{
    Q_OBJECT

public:
    DebugLog()
    {
        connect(&glLog, SIGNAL(messageLogged(QOpenGLDebugMessage)),
                SLOT(message(QOpenGLDebugMessage)), Qt::DirectConnection);
    }

public Q_SLOTS:
    void init()
    {
        QOpenGLContext *context = QOpenGLContext::currentContext();
        QOpenGLFunctions *functions = context->functions();
        qDebug() << "GL_VENDOR:"
                 << reinterpret_cast<const char *>(functions->glGetString(GL_VENDOR));
        qDebug() << "GL_RENDERER:"
                 << reinterpret_cast<const char *>(functions->glGetString(GL_RENDERER));
        qDebug() << "GL_VERSION:"
                 << reinterpret_cast<const char *>(functions->glGetString(GL_VERSION));

        if (glLog.initialize()) {
            glLog.enableMessages();
            glLog.startLogging();
        } else {
            qWarning() << "Can't initialize QOpenGLDebugLogger";
        }
    }

    void message(const QOpenGLDebugMessage &m)
    {
        qDebug() << m;
    }

private:
    QOpenGLDebugLogger glLog;
};

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    auto connection = QX11Info::connection();
    qDebug() << "Damage major_opcode:" << xcb_get_extension_data(connection, &xcb_damage_id)->major_opcode;
    qDebug() << "Composite major_opcode:" << xcb_get_extension_data(connection, &xcb_composite_id)->major_opcode;

    WindowPixmapItem::registerQmlTypes();

    Compositor compositor;

    QQuickView view(compositor.overlayWindow());
    QObject::connect(&view, &QQuickView::sceneGraphError,
                     [](QQuickWindow::SceneGraphError, const QString &message)
    {
        qCritical() << "Scene graph error:" << message;
    });

#ifndef NDEBUG
    QSurfaceFormat format(view.format());
    format.setOption(QSurfaceFormat::DebugContext);
    view.setFormat(format);
#endif

    DebugLog logger;
    QObject::connect(&view, SIGNAL(sceneGraphInitialized()),
                     &logger, SLOT(init()), Qt::DirectConnection);

    view.rootContext()->setContextProperty(QStringLiteral("compositor"), &compositor);
    view.setSource(QStringLiteral("qrc:/main.qml"));
    view.show();

    qDebug() << "Root geometry:" << compositor.rootGeometry();
    view.setGeometry(compositor.rootGeometry());
    QObject::connect(&compositor, &Compositor::rootGeometryChanged,
                     &view, static_cast<void (QQuickView::*)(const QRect &)>(&QQuickView::setGeometry));

    qDebug() << "Main thread:" << QThread::currentThread();
    return app.exec();
}

#include "main.moc"
