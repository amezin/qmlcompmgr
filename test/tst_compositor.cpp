#include <QtTest>
#include <QX11Info>
#include <QRasterWindow>

#include "xephyr.h"
#include "compositor.h"
#include "clientwindow.h"
#include "windowpixmap.h"

#define VERIFY_SINGLE_SIGNAL(spy, value) \
    (spy).clear(); \
    QVERIFY((spy).wait()); \
    QCOMPARE((spy).count(), 1); \
    QCOMPARE((spy).first().first(), QVariant((value)))

#define VERIFY_SINGLE_PROPERTY_CHANGE(spy, value, getExpr) \
    VERIFY_SINGLE_SIGNAL((spy), (value)); \
    QCOMPARE((getExpr), (value))

struct EwmhConnection
{
    xcb_ewmh_connection_t connection;

    EwmhConnection()
    {
        xcb_ewmh_init_atoms_replies(&connection,
                                    xcb_ewmh_init_atoms(QX11Info::connection(), &connection),
                                    Q_NULLPTR);
    }

    ~EwmhConnection()
    {
        xcb_ewmh_connection_wipe(&connection);
    }
};

class CompositorTest : public QObject
{
    Q_OBJECT
private:
    QSharedPointer<ClientWindow> getWindowCreated(Compositor &c)
    {
        QSignalSpy windowCreatedSignalSpy(&c, SIGNAL(windowCreated(ClientWindow*)));
        if (!QTest::qVerify(windowCreatedSignalSpy.wait(1000),
                            "windowCreatedSignalSpy.wait(1000)",
                            "", __FILE__, __LINE__))
        {
            return QSharedPointer<ClientWindow>();
        }
        if (!QTest::qCompare(windowCreatedSignalSpy.count(), 1,
                             "windowCreatedSignalSpy.count()", "1",
                             __FILE__, __LINE__))
        {
            return QSharedPointer<ClientWindow>();
        }
        return windowCreatedSignalSpy.first().first().value<ClientWindow *>()->sharedFromThis();
    }

private Q_SLOTS:
    void testWindowCtor()
    {
        EwmhConnection ewmh;
        QWindow window;
        window.setGeometry(0, 0, 300, 300);

        ClientWindow xcbWindow(&ewmh.connection, window.winId());
        QVERIFY(xcbWindow.isValid());
        QCOMPARE(xcbWindow.geometry(), window.geometry());
    }

    void testWindowCtorInvalid()
    {
        EwmhConnection ewmh;
        QWindow window;
        window.setGeometry(0, 0, 300, 300);
        auto id = window.winId();
        window.destroy();

        ClientWindow xcbWindow(&ewmh.connection, id);
        QVERIFY(!xcbWindow.isValid());
    }

    void testWindowCreate()
    {
        Compositor comp;
        QCoreApplication::processEvents();
        QSignalSpy spy(&comp, SIGNAL(windowCreated(ClientWindow*)));

        QWindow w;
        w.create();

        QVERIFY(spy.wait());
        QCOMPARE(spy.count(), 1);
    }

    void testWindowGeometryChanges()
    {
        Compositor comp;
        QCoreApplication::processEvents();
        QWindow win;
        win.setGeometry(0, 0, 300, 300);
        win.create();
        auto w = getWindowCreated(comp);
        QVERIFY(w);
        QCOMPARE(w->geometry(), QRect(0, 0, 300, 300));
        QSignalSpy geometrySpy(w.data(), SIGNAL(geometryChanged(QRect)));
        win.resize(400, 400);
        VERIFY_SINGLE_PROPERTY_CHANGE(geometrySpy, QRect(0, 0, 400, 400), w->geometry());
    }

    void testWindowMapStateChanges()
    {
        Compositor comp;
        QCoreApplication::processEvents();
        QWindow win;
        win.setGeometry(0, 0, 300, 300);
        win.create();
        auto w = getWindowCreated(comp);
        QVERIFY(w);
        QVERIFY(!w->isMapped());
        QSignalSpy mapSpy(w.data(), SIGNAL(mapStateChanged(bool)));
        win.show();
        VERIFY_SINGLE_PROPERTY_CHANGE(mapSpy, true, w->isMapped());
    }

    void testWindowMapStateChanges2()
    {
        Compositor comp;
        QCoreApplication::processEvents();
        QWindow win;
        win.setGeometry(0, 0, 300, 300);
        win.show();
        auto w = getWindowCreated(comp);
        QVERIFY(w);
        QVERIFY(w->isMapped());
        QSignalSpy mapSpy(w.data(), SIGNAL(mapStateChanged(bool)));
        win.hide();
        VERIFY_SINGLE_PROPERTY_CHANGE(mapSpy, false, w->isMapped());
    }

    void testWindowDestroy()
    {
        Compositor comp;
        QCoreApplication::processEvents();
        QWindow win;
        win.create();
        auto w = getWindowCreated(comp);
        QVERIFY(w);
        QVERIFY(w->isValid());
        QSignalSpy destroySpy(w.data(), SIGNAL(invalidated()));
        win.destroy();
        QVERIFY(destroySpy.wait());
        QCOMPARE(destroySpy.count(), 1);
        QVERIFY(!w->isValid());
    }

    void testWindowPixmap()
    {
        Compositor comp;
        QCoreApplication::processEvents();
        QRasterWindow win;
        win.setGeometry(0, 0, 300, 300);
        win.show();
        auto w = getWindowCreated(comp);
        QVERIFY(w);
        auto pixmap = w->pixmap();
        QVERIFY(pixmap);
        QVERIFY(pixmap == w->pixmap());
        QCOMPARE(pixmap->size(), QSize(300, 300));

        QSignalSpy damageSpy(pixmap.data(), SIGNAL(damaged()));
        win.update();
        QVERIFY(damageSpy.wait());
        QCOMPARE(damageSpy.count(), 1);
        damageSpy.clear();

        win.update();
        QVERIFY(!damageSpy.wait(500));

        pixmap->clearDamage();
        win.update();
        QVERIFY(damageSpy.wait());
        QCOMPARE(damageSpy.count(), 1);
        QVERIFY(w->pixmap() == pixmap);

        win.resize(400, 400);
        QVERIFY(QSignalSpy(w.data(), SIGNAL(geometryChanged(QRect))).wait());
        auto pixmap2 = w->pixmap();
        QVERIFY(pixmap2 != pixmap);
        QCOMPARE(pixmap2->size(), QSize(400, 400));

        QSignalSpy damageSpy2(pixmap2.data(), SIGNAL(damaged()));
        win.update();
        QVERIFY(damageSpy2.wait());
        QCOMPARE(damageSpy2.count(), 1);
    }
};

static Xephyr xephyr(QByteArrayLiteral(":981"));

QTEST_MAIN(CompositorTest)

#include "tst_compositor.moc"
