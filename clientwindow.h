#pragma once

#include <QObject>
#include <QEnableSharedFromThis>
#include <QRect>

#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>

class WindowPixmap;

class ClientWindow : public QObject, public QEnableSharedFromThis<ClientWindow>
{
    Q_OBJECT

    Q_PROPERTY(bool valid READ isValid NOTIFY invalidated)
    Q_PROPERTY(QRect geometry READ geometry NOTIFY geometryChanged)
    Q_PROPERTY(bool mapped READ isMapped NOTIFY mapStateChanged)
    Q_PROPERTY(int zIndex READ zIndex WRITE setZIndex NOTIFY zIndexChanged)
    Q_PROPERTY(bool overrideRedirect READ isOverrideRedirect NOTIFY overrideRedirectChanged)
    Q_PROPERTY(bool transient READ isTransient NOTIFY transientChanged)
    Q_PROPERTY(WmType wmType READ wmType NOTIFY wmTypeChanged)

    Q_ENUMS(WmType)
public:
    enum WmType {
        NONE,
        UNKNOWN,
        DESKTOP,
        DOCK,
        TOOLBAR,
        MENU,
        UTILITY,
        SPLASH,
        DIALOG,
        DROPDOWN_MENU,
        POPUP_MENU,
        TOOLTIP,
        NOTIFICATION,
        COMBO,
        DND,
        NORMAL
    };

    ClientWindow(xcb_ewmh_connection_t *, xcb_window_t, QObject *parent = Q_NULLPTR);
    ~ClientWindow() Q_DECL_OVERRIDE;

    xcb_connection_t *connection() const
    {
        return connection_;
    }

    xcb_window_t window() const
    {
        return window_;
    }

    xcb_window_class_t windowClass() const
    {
        return windowClass_;
    }

    bool isValid() const
    {
        return valid_;
    }

    const QRect &geometry() const
    {
        return geometry_;
    }

    bool isMapped() const
    {
        return mapped_;
    }

    int zIndex() const
    {
        return zIndex_;
    }
    void setZIndex(int i)
    {
        if (i != zIndex_) {
            zIndex_ = i;
            Q_EMIT zIndexChanged(i);
        }
    }

    bool isOverrideRedirect() const
    {
        return overrideRedirect_;
    }

    xcb_window_t transientFor() const
    {
        return transientFor_;
    }

    bool isTransient() const
    {
        return transientFor_ != XCB_NONE;
    }

    WmType wmType() const;

    void xcbEvent(const xcb_configure_notify_event_t *);
    void xcbEvent(const xcb_map_notify_event_t *);
    void xcbEvent(const xcb_unmap_notify_event_t *);
    void xcbEvent(const xcb_reparent_notify_event_t *);
    void xcbEvent(const xcb_gravity_notify_event_t *);
    void xcbEvent(const xcb_circulate_notify_event_t *);
    void xcbEvent(const xcb_property_notify_event_t *);
    void invalidate();
    void setAbove(xcb_window_t above)
    {
        above_ = above;
    }

    const QSharedPointer<WindowPixmap> &pixmap();

Q_SIGNALS:
    void invalidated();
    void geometryChanged(const QRect &geometry);
    void mapStateChanged(bool mapped);
    void zIndexChanged(int zIndex);
    void overrideRedirectChanged(bool overrideRedirect);
    void transientChanged(bool transient);
    void transientForChanged();
    void wmTypeChanged(WmType wmType);

    void pixmapChanged(WindowPixmap *pixmap);
    void stackingOrderChanged();

private:
    void setMapped(bool);
    void setGeometry(const QRect &);
    void setOverrideRedirect(bool);

    void updateTransientFor();
    void updateWmType();

    xcb_connection_t *connection_;
    xcb_ewmh_connection_t *ewmh_;
    xcb_window_t window_;
    xcb_window_class_t windowClass_;
    bool valid_;
    QRect geometry_;
    bool mapped_;
    QSharedPointer<WindowPixmap> pixmap_;
    bool pixmapRealloc_;
    int zIndex_;
    xcb_window_t above_;
    bool overrideRedirect_;
    xcb_window_t transientFor_;
    xcb_atom_t wmType_;
};

Q_DECLARE_METATYPE(ClientWindow*)
