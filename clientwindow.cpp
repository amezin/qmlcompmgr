#include "clientwindow.h"

#include <xcb/xcb_event.h>
#include <xcb/xcb_icccm.h>

#include "windowpixmap.h"

class XcbServerGrab
{
public:
    explicit XcbServerGrab(xcb_connection_t *connection)
        : connection(connection)
    {
        xcb_grab_server(connection);
    }

    ~XcbServerGrab()
    {
        xcb_ungrab_server(connection);
        xcb_flush(connection);
    }

private:
    Q_DISABLE_COPY(XcbServerGrab)

    xcb_connection_t *connection;
};

ClientWindow::ClientWindow(xcb_ewmh_connection_t *ewmh, xcb_window_t window, QObject *parent)
    : QObject(parent),
      connection_(ewmh->connection),
      ewmh_(ewmh),
      window_(window),
      windowClass_(XCB_WINDOW_CLASS_COPY_FROM_PARENT),
      valid_(false),
      mapped_(false),
      pixmapRealloc_(true),
      above_(XCB_NONE),
      overrideRedirect_(false),
      transientFor_(XCB_NONE),
      wmType_(XCB_NONE)
{
    XcbServerGrab grab(connection_);

    auto attributesCookie = xcb_get_window_attributes(connection_, window_);
    auto attributes = xcb_get_window_attributes_reply(connection_, attributesCookie, Q_NULLPTR);
    if (!attributes) {
        return;
    }

    attributes->your_event_mask = attributes->your_event_mask
            | XCB_EVENT_MASK_STRUCTURE_NOTIFY
            | XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_change_window_attributes(connection_, window_, XCB_CW_EVENT_MASK, &attributes->your_event_mask);

    auto geometryCookie = xcb_get_geometry_unchecked(connection_, window_);
    auto transientCookie = xcb_icccm_get_wm_transient_for_unchecked(connection_, window_);
    auto wmTypeCookie = xcb_ewmh_get_wm_window_type_unchecked(ewmh_, window_);

    auto geometry = xcb_get_geometry_reply(connection_, geometryCookie, Q_NULLPTR);
    xcb_icccm_get_wm_transient_for_reply(connection_, transientCookie, &transientFor_, Q_NULLPTR);
    xcb_ewmh_get_atoms_reply_t wmType = {0};
    xcb_ewmh_get_wm_window_type_reply(ewmh_, wmTypeCookie, &wmType, Q_NULLPTR);
    if (wmType.atoms_len > 0) {
        wmType_ = wmType.atoms[0];
    }
    xcb_ewmh_get_atoms_reply_wipe(&wmType);
    if (!geometry) {
        std::free(attributes);
        std::free(geometry);
        return;
    }

    valid_ = true;
    windowClass_ = static_cast<xcb_window_class_t>(attributes->_class);
    geometry_ = QRect(geometry->x, geometry->y, geometry->width, geometry->height);
    mapped_ = (attributes->map_state == XCB_MAP_STATE_VIEWABLE);
    overrideRedirect_ = attributes->override_redirect;

    std::free(attributes);
    std::free(geometry);
}

ClientWindow::~ClientWindow()
{
}

const QSharedPointer<WindowPixmap> &ClientWindow::pixmap()
{
    if (!pixmapRealloc_ || !mapped_) {
        return pixmap_;
    }
    pixmapRealloc_ = false;

    XcbServerGrab grab(connection_);
    QSharedPointer<WindowPixmap> newPixmap(new WindowPixmap(connection_, window_)); // TODO: replace with ::create
    if (newPixmap->isValid()) {
        if (newPixmap->thread() != thread()) { // This method is called from render thread
            newPixmap->moveToThread(thread());
        }
        pixmap_ = newPixmap;
        Q_EMIT pixmapChanged(pixmap_.data());
    }
    return pixmap_;
}

ClientWindow::WmType ClientWindow::wmType() const
{
    if (wmType_ == XCB_NONE) {
        return NONE;
    }

#define DETECT_WM_TYPE(x) \
    if (wmType_ == ewmh_->_NET_WM_WINDOW_TYPE_##x) { \
        return x; \
    }

    DETECT_WM_TYPE(DESKTOP);
    DETECT_WM_TYPE(DOCK);
    DETECT_WM_TYPE(TOOLBAR);
    DETECT_WM_TYPE(MENU);
    DETECT_WM_TYPE(UTILITY);
    DETECT_WM_TYPE(SPLASH);
    DETECT_WM_TYPE(DIALOG);
    DETECT_WM_TYPE(DROPDOWN_MENU);
    DETECT_WM_TYPE(POPUP_MENU);
    DETECT_WM_TYPE(TOOLTIP);
    DETECT_WM_TYPE(NOTIFICATION);
    DETECT_WM_TYPE(COMBO);
    DETECT_WM_TYPE(DND);
    DETECT_WM_TYPE(NORMAL);

    return UNKNOWN;
}

void ClientWindow::invalidate()
{
    if (valid_) {
        valid_ = false;
        Q_EMIT invalidated();
    }
}

void ClientWindow::setGeometry(const QRect &geometry)
{
    if (geometry_ != geometry) {
        if (geometry.size() != geometry_.size()) {
            pixmapRealloc_ = true;
        }
        geometry_ = geometry;
        Q_EMIT geometryChanged(geometry);
    }
}

void ClientWindow::setMapped(bool mapped)
{
    if (mapped_ != mapped) {
        mapped_ = mapped;
        Q_EMIT mapStateChanged(mapped);
    }
}

void ClientWindow::setOverrideRedirect(bool overrideRedirect)
{
    if (overrideRedirect_ != overrideRedirect) {
        overrideRedirect_ = overrideRedirect;
        Q_EMIT overrideRedirectChanged(overrideRedirect);
    }
}

void ClientWindow::xcbEvent(const xcb_configure_notify_event_t *e)
{
    Q_ASSERT(e->window == window_);
    setGeometry(QRect(e->x, e->y, e->width, e->height));
    setOverrideRedirect(e->override_redirect);
    if (e->above_sibling != above_) {
        Q_EMIT stackingOrderChanged();
    }
}

void ClientWindow::xcbEvent(const xcb_map_notify_event_t *e)
{
    Q_ASSERT(e->window == window_);
    pixmapRealloc_ = true;
    setOverrideRedirect(e->override_redirect);
    setMapped(true);
}

void ClientWindow::xcbEvent(const xcb_unmap_notify_event_t *e)
{
    Q_ASSERT(e->window == window_);
    setMapped(false);
}

void ClientWindow::xcbEvent(const xcb_reparent_notify_event_t *e)
{
    Q_ASSERT(e->window == window_);
    setGeometry(QRect(e->x, e->y, geometry_.width(), geometry_.height()));
    setOverrideRedirect(e->override_redirect);
}

void ClientWindow::xcbEvent(const xcb_gravity_notify_event_t *e)
{
    Q_ASSERT(e->window == window_);
    setGeometry(QRect(e->x, e->y, geometry_.width(), geometry_.height()));
}

void ClientWindow::xcbEvent(const xcb_circulate_notify_event_t *e)
{
    Q_ASSERT(e->window == window_);
    Q_EMIT stackingOrderChanged();
}

void ClientWindow::updateTransientFor()
{
    auto oldIsTransient = isTransient();
    auto oldTransientFor = transientFor_;

    auto cookie = xcb_icccm_get_wm_transient_for(connection_, window_);
    xcb_icccm_get_wm_transient_for_reply(connection_, cookie, &transientFor_, Q_NULLPTR);

    if (oldTransientFor != transientFor_) {
        Q_EMIT transientForChanged();
    }
    if (oldIsTransient != isTransient()) {
        Q_EMIT transientChanged(isTransient());
    }
}

void ClientWindow::updateWmType()
{
    auto wmTypeCookie = xcb_ewmh_get_wm_window_type(ewmh_, window_);

    xcb_ewmh_get_atoms_reply_t wmTypeReply = {0};
    if (!xcb_ewmh_get_wm_window_type_reply(ewmh_, wmTypeCookie, &wmTypeReply, Q_NULLPTR)) {
        return;
    }

    xcb_atom_t newWmType = wmType_;
    if (wmTypeReply.atoms_len > 0) {
        newWmType = wmTypeReply.atoms[0];
    }
    xcb_ewmh_get_atoms_reply_wipe(&wmTypeReply);

    if (newWmType != wmType_) {
        wmType_ = newWmType;
        Q_EMIT wmTypeChanged(wmType());
    }
}

void ClientWindow::xcbEvent(const xcb_property_notify_event_t *e)
{
    Q_ASSERT(e->window == window_);

    if (e->atom == XCB_ATOM_WM_TRANSIENT_FOR) {
        updateTransientFor();
    } else if (e->atom == ewmh_->_NET_WM_WINDOW_TYPE) {
        updateWmType();
    }
}
