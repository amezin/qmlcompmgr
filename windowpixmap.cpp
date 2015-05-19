#include "windowpixmap.h"

#include <xcb/composite.h>

WindowPixmap::WindowPixmap(xcb_connection_t *connection, xcb_window_t window, QObject *parent)
    : QObject(parent),
      connection_(connection),
      window_(window),
      valid_(false),
      pixmap_(xcb_generate_id(connection)),
      damage_(xcb_generate_id(connection)),
      damaged_(false)
{
    xcb_composite_name_window_pixmap(connection_, window_, pixmap_);
    auto geometryCookie = xcb_get_geometry_unchecked(connection_, pixmap_);
    xcb_damage_create(connection_, damage_, pixmap_, XCB_DAMAGE_REPORT_LEVEL_NON_EMPTY);
    auto geometry = xcb_get_geometry_reply(connection_, geometryCookie, Q_NULLPTR);
    if (geometry) {
        size_ = QSize(geometry->width, geometry->height);
        valid_ = true;
        std::free(geometry);
    }
}

WindowPixmap::~WindowPixmap()
{
    xcb_damage_destroy(connection_, damage_);
    xcb_free_pixmap(connection_, pixmap_);
    xcb_flush(connection_);
    Q_EMIT destroyed(this);
}

void WindowPixmap::clearDamage()
{
    if (damaged_) {
        xcb_damage_subtract(connection_, damage_, XCB_NONE, XCB_NONE);
        xcb_flush(connection_);
        damaged_ = false;
    }
}

void WindowPixmap::xcbEvent(const xcb_damage_notify_event_t *e)
{
    Q_ASSERT(e->damage == damage_);
    if (e->drawable == pixmap_ && !damaged_) {
        damaged_ = true;
        Q_EMIT damaged();
    }
}
