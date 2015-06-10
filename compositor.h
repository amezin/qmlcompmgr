#pragma once

#include <QAbstractNativeEventFilter>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QSharedPointer>
#include <QRect>

#include <xcb/xcb.h>
#include <xcb/damage.h>

class QWindow;
class ClientWindow;
class WindowPixmap;

class Compositor : public QObject, private QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    Compositor();
    ~Compositor() Q_DECL_OVERRIDE;

    QWindow *overlayWindow() const
    {
        return overlayWindow_.data();
    }

    const QRect &rootGeometry() const
    {
        return rootGeometry_;
    }

Q_SIGNALS:
    void windowCreated(ClientWindow *clientWindow);
    void rootGeometryChanged(const QRect &);

private Q_SLOTS:
    void registerPixmap(WindowPixmap *);
    void unregisterPixmap(WindowPixmap *);
    void restack();

private:
    template<typename T> bool xcbDispatchEvent(const T *, xcb_window_t);
    template<typename T> bool xcbDispatchEvent(const T *);
    template<typename T> bool xcbEvent(const T *);

    bool nativeEventFilter(const QByteArray &eventType, void *message, long *) Q_DECL_OVERRIDE;

    void addChildWindow(xcb_window_t);
    void removeChildWindow(xcb_window_t);

    xcb_connection_t *connection_;
    xcb_window_t root_;
    const xcb_query_extension_reply_t *damageExt_, *shapeExt_;

    QMap<xcb_damage_damage_t, WindowPixmap *> pixmaps_;
    QMap<xcb_window_t, QSharedPointer<ClientWindow> > windows_;
    QScopedPointer<QWindow> overlayWindow_;
    QRect rootGeometry_;
    bool initFinished_;
};
