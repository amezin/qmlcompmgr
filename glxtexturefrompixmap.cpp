#include "glxtexturefrompixmap.h"

#include <QMap>
#include <QMetaType>
#include <QDataStream>
#include <QTextStream>

#include <QDebug>
#include <QOpenGLContext>
#include <QLoggingCategory>
#include <QX11Info>

#include "compositor.h"

#include <GL/glx.h>

class GLXInfo
{
public:
    static const GLXInfo &instance();

    Display *display;
    GLXFBConfig fbConfig;
    PFNGLXBINDTEXIMAGEEXTPROC tfpBind;
    PFNGLXRELEASETEXIMAGEEXTPROC tfpRelease;

private:
    Q_DISABLE_COPY(GLXInfo)

    static const QLoggingCategory &log();

    GLXInfo();
    ~GLXInfo();
};

GLXInfo::GLXInfo()
    : display(QX11Info::display()),
      fbConfig(Q_NULLPTR),
      tfpBind(Q_NULLPTR),
      tfpRelease(Q_NULLPTR)
{
    auto screen = QX11Info::appScreen();
    auto extensionsString = glXQueryExtensionsString(display, screen);
    QByteArrayList extensions(QByteArray(extensionsString).split(' '));
    if (!extensions.contains(QByteArrayLiteral("GLX_EXT_texture_from_pixmap"))) {
        qCritical(log) << "GLX_EXT_texture_from_pixmap is not supported";
        return;
    }

    tfpBind = reinterpret_cast<PFNGLXBINDTEXIMAGEEXTPROC>
            (glXGetProcAddress(reinterpret_cast<const GLubyte *>("glXBindTexImageEXT")));
    if (!tfpBind) {
        qCritical(log) << "GLX_EXT_texture_from_pixmap is reported, but glXBindTexImageEXT isn't available";
    }

    tfpRelease = reinterpret_cast<PFNGLXRELEASETEXIMAGEEXTPROC>
            (glXGetProcAddress(reinterpret_cast<const GLubyte *>("glXReleaseTexImageEXT")));
    if (!tfpRelease) {
        qCritical(log) << "GLX_EXT_texture_from_pixmap is reported, but glXReleaseTexImageEXT isn't available";
    }

    int pixmapConfig[] = {
        GLX_BIND_TO_TEXTURE_RGBA_EXT, True,
        GLX_DRAWABLE_TYPE, GLX_PIXMAP_BIT,
        GLX_BIND_TO_TEXTURE_TARGETS_EXT, GLX_TEXTURE_2D_BIT_EXT,
        GLX_DOUBLEBUFFER, True,
        GLX_Y_INVERTED_EXT, static_cast<int>(GLX_DONT_CARE),
        None
    };

    int nConfigs = 0;
    auto configs = glXChooseFBConfig(display, screen, pixmapConfig, &nConfigs);
    if (!configs) {
        qWarning(log) << "No appropriate GLXFBConfig found!";
        return;
    }
    fbConfig = configs[0];
    XFree(configs);
}

GLXInfo::~GLXInfo()
{
}

const GLXInfo &GLXInfo::instance()
{
    static const GLXInfo instance_;
    return instance_;
}

const QLoggingCategory &GLXInfo::log()
{
    static const QLoggingCategory log_("GLXInfo");
    return log_;
}

GLXTextureFromPixmap::GLXTextureFromPixmap(xcb_pixmap_t pixmap, const QSize &size)
    : QOpenGLFunctions(QOpenGLContext::currentContext()),
      texture_(0),
      glxPixmap_(XCB_NONE),
      pixmap_(pixmap),
      size_(size),
      rebindTFP_(false)
{
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);

    setFiltering(Linear);
    setHorizontalWrapMode(ClampToEdge);
    setVerticalWrapMode(ClampToEdge);
    updateBindOptions(true);
}

GLXTextureFromPixmap::~GLXTextureFromPixmap()
{
    if (texture_) {
        glDeleteTextures(1, &texture_);
    }

    if (!glxPixmap_) {
        return;
    }

    auto &glx = GLXInfo::instance();
    glx.tfpRelease(glx.display, glxPixmap_, GLX_FRONT_LEFT_EXT);
    glXDestroyPixmap(glx.display, glxPixmap_);
    glxPixmap_ = 0;
}

int GLXTextureFromPixmap::textureId() const
{
    return static_cast<int>(texture_);
}

QSize GLXTextureFromPixmap::textureSize() const
{
    return size_;
}

bool GLXTextureFromPixmap::hasAlphaChannel() const
{
    return true;
}

bool GLXTextureFromPixmap::hasMipmaps() const
{
    return false;
}

void GLXTextureFromPixmap::rebind()
{
    if (glxPixmap_) {
        rebindTFP_ = true;
    }
}

void GLXTextureFromPixmap::bind()
{
    auto &glx = GLXInfo::instance();

    if (!glxPixmap_) {
        int attr[] = {
            GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
            GLX_TEXTURE_FORMAT_EXT, GLX_TEXTURE_FORMAT_RGBA_EXT,
            None
        };
        glxPixmap_ = glXCreatePixmap(glx.display, glx.fbConfig, pixmap_, attr);
        rebindTFP_ = true;
    }

    if (glxPixmap_) {
        glBindTexture(GL_TEXTURE_2D, texture_);
        updateBindOptions();

        if (rebindTFP_) {
            rebindTFP_ = false;
            glx.tfpBind(glx.display, glxPixmap_, GLX_FRONT_LEFT_EXT, Q_NULLPTR);
        }
    }
}
