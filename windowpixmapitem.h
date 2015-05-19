#pragma once

#include <QQuickItem>
#include <QSharedPointer>

class ClientWindow;
class WindowPixmap;

class WindowPixmapItem : public QQuickItem
{
    Q_OBJECT

    Q_PROPERTY(ClientWindow *clientWindow READ clientWindow WRITE setClientWindow NOTIFY clientWindowChanged)
public:
    WindowPixmapItem();
    ~WindowPixmapItem() Q_DECL_OVERRIDE;

    ClientWindow *clientWindow() const
    {
        return clientWindow_.data();
    }
    void setClientWindow(ClientWindow *);

    static void registerQmlTypes();

Q_SIGNALS:
    void clientWindowChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *, UpdatePaintNodeData *) Q_DECL_OVERRIDE;

private Q_SLOTS:
    void updateImplicitSize();

private:
    QSharedPointer<ClientWindow> clientWindow_;
    QSharedPointer<WindowPixmap> pixmap_;
};
