#pragma once

#include <QObject>
#include <QEnableSharedFromThis>
#include <QSize>

#include <xcb/xcb.h>
#include <xcb/damage.h>

class WindowPixmap : public QObject, public QEnableSharedFromThis<WindowPixmap>
{
    Q_OBJECT

    Q_PROPERTY(QSize size READ size CONSTANT)
    Q_PROPERTY(bool valid READ isValid CONSTANT)
public:
    WindowPixmap(xcb_connection_t *, xcb_window_t, QObject *parent = Q_NULLPTR);
    ~WindowPixmap() Q_DECL_OVERRIDE;

    xcb_connection_t *connection() const
    {
        return connection_;
    }

    xcb_window_t window() const
    {
        return window_;
    }

    xcb_pixmap_t pixmap() const
    {
        return pixmap_;
    }

    xcb_damage_damage_t damage() const
    {
        return damage_;
    }

    const QSize &size() const
    {
        return size_;
    }

    bool isValid() const
    {
        return valid_;
    }

    bool isDamaged() const
    {
        return damaged_;
    }

    void clearDamage();

    void xcbEvent(const xcb_damage_notify_event_t *);

Q_SIGNALS:
    void damaged();
    void destroyed(WindowPixmap *);

private:
    xcb_connection_t *connection_;
    xcb_window_t window_;
    bool valid_;
    xcb_pixmap_t pixmap_;
    xcb_damage_damage_t damage_;
    QSize size_;
    bool damaged_;
};
