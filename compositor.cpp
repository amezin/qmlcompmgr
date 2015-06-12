#include "compositor.h"

#include <memory>

#include <QDebug>
#include <QCoreApplication>
#include <QWindow>
#include <QX11Info>

#include <xcb/composite.h>
#include <xcb/xfixes.h>
#include <xcb/xcb_event.h>

#include "clientwindow.h"
#include "windowpixmap.h"

template<typename T>
std::unique_ptr<T, decltype(&std::free)> xcbReply(T *ptr)
{
    return std::unique_ptr<T, decltype(&std::free)>(ptr, std::free);
}

Compositor::Compositor()
    : connection_(QX11Info::connection()),
      root_(QX11Info::appRootWindow()),
      damageExt_(xcb_get_extension_data(connection_, &xcb_damage_id)),
      initFinished_(false)
{
    qRegisterMetaType<ClientWindow *>("ClientWindow*");

    Q_ASSERT(QCoreApplication::instance());
    QCoreApplication::instance()->installNativeEventFilter(this);

    auto ewmhCookie = xcb_ewmh_init_atoms(connection_, &ewmh_);
    if (!xcb_ewmh_init_atoms_replies(&ewmh_, ewmhCookie, Q_NULLPTR)) {
        qFatal("Cannot init EWMH");
    }

    auto wmCmCookie = xcb_ewmh_get_wm_cm_owner_unchecked(&ewmh_, QX11Info::appScreen());
    xcb_window_t wmCmOwnerWin = XCB_NONE;
    if (!xcb_ewmh_get_wm_cm_owner_reply(&ewmh_, wmCmCookie, &wmCmOwnerWin, Q_NULLPTR)) {
        qFatal("Cannot check _NET_WM_CM_Sn");
    }
    if (wmCmOwnerWin) {
        qFatal("Another compositing manager is already running");
    }

    auto attributesCookie = xcb_get_window_attributes_unchecked(connection_, root_);
    auto damageQueryVersionCookie = xcb_damage_query_version_unchecked(connection_, 1, 1);
    auto overlayWindowCookie = xcb_composite_get_overlay_window_unchecked(connection_, root_);

    auto attributes =
            xcbReply(xcb_get_window_attributes_reply(connection_, attributesCookie, Q_NULLPTR));
    if (!attributes) {
        qFatal("Cannot get root window attributes");
    }
    auto newEventMask = attributes->your_event_mask | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_STRUCTURE_NOTIFY;
    xcb_change_window_attributes(connection_, root_, XCB_CW_EVENT_MASK, &newEventMask);

    auto treeCookie = xcb_query_tree_unchecked(connection_, root_);
    auto rootGeometryCookie = xcb_get_geometry_unchecked(connection_, root_);

    auto damageVersion =
            xcbReply(xcb_damage_query_version_reply(connection_, damageQueryVersionCookie, Q_NULLPTR));
    if (!damageVersion) {
        qFatal("Cannot query version of Damage extension");
    }

    auto overlayWindow =
            xcbReply(xcb_composite_get_overlay_window_reply(connection_, overlayWindowCookie, Q_NULLPTR));
    if (!overlayWindow) {
        qFatal("Cannot get overlay window");
    }
    overlayWindow_.reset(QWindow::fromWinId(overlayWindow->overlay_win));

    auto region = xcb_generate_id(connection_);
    xcb_xfixes_create_region(connection_, region, 0, Q_NULLPTR);
    xcb_xfixes_set_window_shape_region(connection_, overlayWindow->overlay_win, XCB_SHAPE_SK_INPUT, 0, 0, region);
    xcb_xfixes_destroy_region(connection_, region);

    xcb_composite_redirect_subwindows(connection_, root_, XCB_COMPOSITE_REDIRECT_MANUAL);

    auto rootGeometry =
            xcbReply(xcb_get_geometry_reply(connection_, rootGeometryCookie, Q_NULLPTR));
    if (!rootGeometry) {
        qFatal("Cannot query root window geometry");
    }
    rootGeometry_ = QRect(rootGeometry->x, rootGeometry->y, rootGeometry->width, rootGeometry->height);

    auto tree = xcbReply(xcb_query_tree_reply(connection_, treeCookie, Q_NULLPTR));
    if (!tree) {
        qFatal("Cannot query window tree");
    }

    auto children = xcb_query_tree_children(tree.get());
    for (int i = 0; i < xcb_query_tree_children_length(tree.get()); i++) {
        addChildWindow(children[i]);
    }
    initFinished_ = true;
}

Compositor::~Compositor()
{
    xcb_ewmh_connection_wipe(&ewmh_);
}

void Compositor::registerCompositor(QWindow *w)
{
    xcb_ewmh_set_wm_cm_owner(&ewmh_, QX11Info::appScreen(), w->winId(), QX11Info::getTimestamp(), 0, 0);

    auto wmCmCookie = xcb_ewmh_get_wm_cm_owner_unchecked(&ewmh_, QX11Info::appScreen());
    xcb_window_t wmCmOwnerWin = XCB_NONE;
    if (!xcb_ewmh_get_wm_cm_owner_reply(&ewmh_, wmCmCookie, &wmCmOwnerWin, Q_NULLPTR)) {
        qFatal("Cannot check _NET_WM_CM_Sn");
    }
    if (wmCmOwnerWin != w->winId()) {
        qFatal("Another compositing manager is already running");
    }
}

template<typename T>
bool Compositor::xcbDispatchEvent(const T *e, xcb_window_t window)
{
    auto i = windows_.constFind(window);
    if (i != windows_.constEnd()) {
        (*i)->xcbEvent(e);
        return true;
    }
    return false;
}

template<typename T>
bool Compositor::xcbDispatchEvent(const T *e)
{
    return xcbDispatchEvent(e, e->window);
}

template<typename T>
bool Compositor::xcbEvent(const T *e)
{
    if (e->event != root_) {
        return false;
    }
    return xcbDispatchEvent(e);
}

template<>
bool Compositor::xcbEvent(const xcb_configure_notify_event_t *e)
{
    if (e->window == root_) {
        QRect newGeometry(0, 0, e->width, e->height);
        if (rootGeometry_ != newGeometry) {
            rootGeometry_ = newGeometry;
            Q_EMIT rootGeometryChanged(rootGeometry_);
        }
    }

    if (e->window != e->event) {
        return false;
    }

    return xcbDispatchEvent(e);
}

template<>
bool Compositor::xcbEvent(const xcb_create_notify_event_t *e)
{
    if (e->parent != root_) {
        return false;
    }

    addChildWindow(e->window);
    return true;
}

template<>
bool Compositor::xcbEvent(const xcb_destroy_notify_event_t *e)
{
    if (e->event != root_) {
        return false;
    }

    removeChildWindow(e->window);
    return true;
}

template<>
bool Compositor::xcbEvent(const xcb_reparent_notify_event_t *e)
{
    if (e->event != root_) {
        return false;
    }

    if (e->parent == root_) {
        addChildWindow(e->window);
    } else {
        removeChildWindow(e->window);
    }

    return xcbDispatchEvent(e);
}

bool Compositor::nativeEventFilter(const QByteArray &eventType, void *message, long *)
{
    Q_ASSERT(eventType == QByteArrayLiteral("xcb_generic_event_t"));

    auto responseType = XCB_EVENT_RESPONSE_TYPE(static_cast<xcb_generic_event_t *>(message));
    if (responseType == damageExt_->first_event + XCB_DAMAGE_NOTIFY) {
        auto e = static_cast<xcb_damage_notify_event_t *>(message);
        auto i = pixmaps_.constFind(e->damage);
        if (i == pixmaps_.constEnd()) {
            return false;
        }
        (*i)->xcbEvent(e);
        return true;
    }

    switch (responseType) {
    case XCB_CREATE_NOTIFY:
        return xcbEvent(static_cast<xcb_create_notify_event_t *>(message));
    case XCB_DESTROY_NOTIFY:
        return xcbEvent(static_cast<xcb_destroy_notify_event_t *>(message));
    case XCB_REPARENT_NOTIFY:
        return xcbEvent(static_cast<xcb_reparent_notify_event_t *>(message));
    case XCB_CONFIGURE_NOTIFY:
        return xcbEvent(static_cast<xcb_configure_notify_event_t *>(message));
    case XCB_MAP_NOTIFY:
        return xcbEvent(static_cast<xcb_map_notify_event_t *>(message));
    case XCB_UNMAP_NOTIFY:
        return xcbEvent(static_cast<xcb_unmap_notify_event_t *>(message));
    case XCB_GRAVITY_NOTIFY:
        return xcbEvent(static_cast<xcb_gravity_notify_event_t *>(message));
    case XCB_CIRCULATE_NOTIFY:
        restack();
        return true;
    case XCB_PROPERTY_NOTIFY:
        return xcbDispatchEvent(static_cast<xcb_property_notify_event_t *>(message));
    default:
        return false;
    }
}

void Compositor::addChildWindow(xcb_window_t window)
{
    Q_ASSERT(window != root_);
    Q_ASSERT(!overlayWindow_ || window != overlayWindow_->winId());

    if (windows_.contains(window)) {
        return;
    }

    QSharedPointer<ClientWindow> w(new ClientWindow(connection_, window)); // TODO: replace with ::create
    if (w->isValid() && w->windowClass() != XCB_WINDOW_CLASS_INPUT_ONLY) {
        windows_.insert(window, w);
        connect(w.data(), SIGNAL(pixmapChanged(WindowPixmap*)), SLOT(registerPixmap(WindowPixmap*)));
        connect(w.data(), SIGNAL(stackingOrderChanged()), SLOT(restack()));
        restack();

        if (initFinished_) {
            Q_EMIT windowCreated(w.data());
        } else {
            QMetaObject::invokeMethod(this, "windowCreated", Qt::QueuedConnection, Q_ARG(ClientWindow*, w.data()));
        }
    }
}

void Compositor::removeChildWindow(xcb_window_t window)
{
    auto i = windows_.find(window);
    if (i == windows_.end()) {
        return;
    }
    (*i)->invalidate();
    (*i)->disconnect(this);
    windows_.erase(i);
}

void Compositor::registerPixmap(WindowPixmap *pixmap)
{
    if (pixmap->isValid()) {
        connect(pixmap, SIGNAL(destroyed(WindowPixmap*)),
                SLOT(unregisterPixmap(WindowPixmap*)), Qt::DirectConnection);
        pixmaps_.insert(pixmap->damage(), pixmap);
    }
}

void Compositor::unregisterPixmap(WindowPixmap *pixmap)
{
    pixmaps_.remove(pixmap->damage());
}

void Compositor::restack() // TODO: maintain stacking order somehow
{
    auto treeCookie = xcb_query_tree_unchecked(connection_, root_);
    auto tree = xcbReply(xcb_query_tree_reply(connection_, treeCookie, Q_NULLPTR));
    auto children = xcb_query_tree_children(tree.get());
    for (int i = 0; i < xcb_query_tree_children_length(tree.get()); i++) {
        auto w = windows_.constFind(children[i]);
        if (w != windows_.constEnd()) {
            (*w)->setZIndex(i);
            if (i) {
                (*w)->setAbove(children[i - 1]);
            } else {
                (*w)->setAbove(XCB_NONE);
            }
        }
    }
}
