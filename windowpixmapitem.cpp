#include "windowpixmapitem.h"

#include <QSGSimpleTextureNode>

#include "clientwindow.h"
#include "windowpixmap.h"
#include "glxtexturefrompixmap.h"

void WindowPixmapItem::registerQmlTypes()
{
    qmlRegisterType<ClientWindow>();
    qmlRegisterType<WindowPixmap>();
    qmlRegisterType<WindowPixmapItem>("Compositor", 1, 0, "WindowPixmap");
}

WindowPixmapItem::WindowPixmapItem()
{
    setFlag(ItemHasContents);
}

WindowPixmapItem::~WindowPixmapItem()
{
}

void WindowPixmapItem::setClientWindow(ClientWindow *w)
{
    if (w == clientWindow_.data()) {
        return;
    }

    if (clientWindow_) {
        clientWindow_->disconnect(this);
    }

    clientWindow_ = w->sharedFromThis();

    connect(clientWindow_.data(), SIGNAL(geometryChanged(QRect)), SLOT(updateImplicitSize()));
    connect(clientWindow_.data(), SIGNAL(mapStateChanged(bool)), SLOT(updateImplicitSize()));
    connect(clientWindow_.data(), SIGNAL(mapStateChanged(bool)), SLOT(update()));
    updateImplicitSize();

    update();
    Q_EMIT clientWindowChanged();
}

QSGNode *WindowPixmapItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    auto node = static_cast<QSGSimpleTextureNode *>(old);
    auto pixmap = clientWindow_->pixmap();
    if (!clientWindow_ || !pixmap || !pixmap->isValid()) {
        delete node;
        return Q_NULLPTR;
    }

    if (!node) {
        node = new QSGSimpleTextureNode;
    }

    auto texture = static_cast<GLXTextureFromPixmap *>(node->texture());
    if (texture && pixmap_ != pixmap) {
        delete texture;
        texture = Q_NULLPTR;
    }
    pixmap_ = pixmap;

    if (!texture) {
        texture = new GLXTextureFromPixmap(pixmap->pixmap(), pixmap->visual(), pixmap->size());
        node->setTexture(texture);
        node->setOwnsTexture(true);
        if (texture->isYInverted()) {
            node->setTextureCoordinatesTransform(QSGSimpleTextureNode::MirrorVertically);
        } else {
            node->setTextureCoordinatesTransform(QSGSimpleTextureNode::NoTransform);
        }
        connect(pixmap.data(), SIGNAL(damaged()), SLOT(update()));
    }
    node->setRect(0, 0, width(), height());
    if (pixmap->isDamaged()) {
        texture->rebind();
        pixmap->clearDamage();
    }
    return node;
}

void WindowPixmapItem::updateImplicitSize()
{
    QSize winSize;
    if (clientWindow_) {
        if (clientWindow()->isValid() && clientWindow_->isMapped()) {
            winSize = clientWindow_->geometry().size();
        } else if (pixmap_ && pixmap_->isValid()) {
            winSize = pixmap_->size();
        }
    }
    if (implicitWidth() != winSize.width() || implicitHeight() != winSize.height()) {
        setImplicitSize(winSize.width(), winSize.height());
    }
}
