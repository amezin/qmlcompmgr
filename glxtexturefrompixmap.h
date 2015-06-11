#pragma once

#include <QSGTexture>
#include <QOpenGLFunctions>

#include <xcb/xcb.h>
#include <xcb/glx.h>

class GLXTextureFromPixmap : public QSGTexture,
                             protected QOpenGLFunctions
{
    Q_OBJECT
public:
    explicit GLXTextureFromPixmap(xcb_pixmap_t pixmap, xcb_visualid_t visual, const QSize &size);
    ~GLXTextureFromPixmap() Q_DECL_OVERRIDE;

    int textureId() const Q_DECL_OVERRIDE;
    QSize textureSize() const Q_DECL_OVERRIDE;
    bool hasAlphaChannel() const Q_DECL_OVERRIDE;
    bool hasMipmaps() const Q_DECL_OVERRIDE;

    void bind() Q_DECL_OVERRIDE;

    xcb_pixmap_t pixmap() const
    {
        return pixmap_;
    }

    bool isYInverted() const
    {
        return isYInverted_;
    }

public Q_SLOTS:
    void rebind();

private:
    uint texture_;
    xcb_glx_pixmap_t glxPixmap_;
    xcb_pixmap_t pixmap_;
    bool hasAlpha_, isYInverted_;
    QSize size_;
    bool rebindTFP_;
};
