#include "clientwindow.h"

#include <xcb/xcb_event.h>

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

ClientWindow::ClientWindow(xcb_connection_t *connection, xcb_window_t window, QObject *parent)
    : QObject(parent),
      connection_(connection),
      window_(window),
      windowClass_(XCB_WINDOW_CLASS_COPY_FROM_PARENT),
      valid_(false),
      mapped_(false),
      pixmapRealloc_(true),
      above_(XCB_NONE),
      overrideRedirect_(false),
      boundingShaped_(false),
      clipShaped_(false),
      inputFocus_(true)
{
    XcbServerGrab grab(connection_);

    auto attributesCookie = xcb_get_window_attributes(connection_, window_);
    auto attributes = xcb_get_window_attributes_reply(connection_, attributesCookie, Q_NULLPTR);
    if (!attributes) {
        return;
    }

    attributes->your_event_mask = attributes->your_event_mask
            | XCB_EVENT_MASK_STRUCTURE_NOTIFY
            | XCB_EVENT_MASK_FOCUS_CHANGE;
    xcb_change_window_attributes(connection_, window_, XCB_CW_EVENT_MASK, &attributes->your_event_mask);
    xcb_shape_select_input(connection_, window_, 1);

    auto geometryCookie = xcb_get_geometry_unchecked(connection_, window_);
    auto shapeCookie = xcb_shape_query_extents_unchecked(connection_, window_);

    auto geometry = xcb_get_geometry_reply(connection_, geometryCookie, Q_NULLPTR);
    auto shape = xcb_shape_query_extents_reply(connection_, shapeCookie, Q_NULLPTR);

    if (!geometry || !shape) {
        std::free(attributes);
        std::free(geometry);
        std::free(shape);
        return;
    }

    valid_ = true;
    windowClass_ = static_cast<xcb_window_class_t>(attributes->_class);
    geometry_ = QRect(geometry->x, geometry->y, geometry->width, geometry->height);
    mapped_ = (attributes->map_state == XCB_MAP_STATE_VIEWABLE);
    overrideRedirect_ = attributes->override_redirect;
    boundingShaped_ = shape->bounding_shaped;
    clipShaped_ = shape->clip_shaped;

    std::free(attributes);
    std::free(geometry);
    std::free(shape);
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
        pixmap_ = newPixmap;
        Q_EMIT pixmapChanged(pixmap_.data());
    }
    return pixmap_;
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

void ClientWindow::xcbEvent(const xcb_shape_notify_event_t *e)
{
    Q_ASSERT(e->affected_window == window_);

    auto old = isShaped();
    if (e->shape_kind & XCB_SHAPE_SK_BOUNDING) {
        boundingShaped_ = e->shaped;
    }
    if (e->shape_kind & XCB_SHAPE_SK_CLIP) {
        clipShaped_ = e->shaped;
    }
    if (isShaped() != old) {
        Q_EMIT shapedChanged(isShaped());
    }
}

void ClientWindow::xcbEvent(const xcb_focus_in_event_t *e)
{
    Q_ASSERT(e->event == window_);

    if (inputFocus_ && XCB_EVENT_RESPONSE_TYPE(e) == XCB_FOCUS_OUT && e->detail != XCB_NOTIFY_DETAIL_INFERIOR) {
        inputFocus_ = false;
        Q_EMIT inputFocusChanged(inputFocus_);
    } else if (!inputFocus_ && XCB_EVENT_RESPONSE_TYPE(e) == XCB_FOCUS_IN) {
        inputFocus_ = true;
        Q_EMIT inputFocusChanged(inputFocus_);
    }
}
