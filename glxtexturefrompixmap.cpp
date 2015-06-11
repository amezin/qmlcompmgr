#include "glxtexturefrompixmap.h"

#include <QMap>
#include <QMetaType>
#include <QDataStream>
#include <QTextStream>

#include <QDebug>
#include <QOpenGLContext>
#include <QLoggingCategory>
#include <QX11Info>

#include <xcb/xcb_renderutil.h>

#include "compositor.h"

#include <GL/glx.h>

class GLXInfo
{
public:
    static GLXInfo &instance();

    Display *display;
    PFNGLXBINDTEXIMAGEEXTPROC tfpBind;
    PFNGLXRELEASETEXIMAGEEXTPROC tfpRelease;

    struct VisualInfo
    {
        GLXFBConfig config;
        int textureFormat;
        int yInverted;
        int depth;
        int stencil;
        bool alphaMatches;

        VisualInfo()
            : config(Q_NULLPTR), textureFormat(0), yInverted(0),
              depth(INT_MAX / 2), stencil(INT_MAX / 2), alphaMatches(false)
        {
        }
    };
    const VisualInfo &configFor(xcb_visualid_t);

private:
    Q_DISABLE_COPY(GLXInfo)

    static const QLoggingCategory &log();
    VisualInfo createVisualInfo(xcb_visualid_t) const;

    xcb_connection_t *connection;
    int screen;
    xcb_render_query_pict_formats_reply_t *pictFormats;
    QMap<xcb_visualid_t, int> visualDepth;
    QMap<xcb_visualid_t, VisualInfo> visualInfos;

    GLXInfo();
    ~GLXInfo();
};

GLXInfo::GLXInfo()
    : display(QX11Info::display()),
      tfpBind(Q_NULLPTR),
      tfpRelease(Q_NULLPTR),
      connection(QX11Info::connection()),
      screen(QX11Info::appScreen()),
      pictFormats(Q_NULLPTR)
{
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

    auto pictFormatsCookie = xcb_render_query_pict_formats_unchecked(connection);
    pictFormats = xcb_render_query_pict_formats_reply(connection, pictFormatsCookie, Q_NULLPTR);
    if (!pictFormats) {
        qCritical(log) << "xcb_render_query_pict_formats failed";
    }

    auto setup = xcb_get_setup(connection);
    for (auto screen = xcb_setup_roots_iterator(setup); screen.rem; xcb_screen_next(&screen)) {
        for (auto depth = xcb_screen_allowed_depths_iterator(screen.data); depth.rem; xcb_depth_next(&depth)) {
            int len = xcb_depth_visuals_length(depth.data);
            auto visuals = xcb_depth_visuals(depth.data);

            for (int i = 0; i < len; i++) {
                visualDepth[visuals[i].visual_id] = depth.data->depth;
            }
        }
    }
}

GLXInfo::~GLXInfo()
{
}

GLXInfo &GLXInfo::instance()
{
    static GLXInfo instance_;
    return instance_;
}

const QLoggingCategory &GLXInfo::log()
{
    static const QLoggingCategory log_("GLXInfo");
    return log_;
}

GLXInfo::VisualInfo GLXInfo::createVisualInfo(xcb_visualid_t visual) const
{
    VisualInfo bestConfig;

    auto pictVisual = xcb_render_util_find_visual_format(pictFormats, visual);
    if (!pictVisual) {
        qCritical(log) << "Can't find pictformat for visual" << visual;
        return bestConfig;
    }

    auto pictFormInfos = xcb_render_query_pict_formats_formats(pictFormats);
    auto nPictFormInfos = xcb_render_query_pict_formats_formats_length(pictFormats);
    const xcb_render_pictforminfo_t *pictFormInfo = Q_NULLPTR;
    for (int i = 0; i < nPictFormInfos; i++) {
        if (pictFormInfos[i].id == pictVisual->format) {
            pictFormInfo = &pictFormInfos[i];
        }
    }
    Q_ASSERT(pictFormInfo);

    int redBits = __builtin_popcount(pictFormInfo->direct.red_mask);
    int greenBits = __builtin_popcount(pictFormInfo->direct.green_mask);
    int blueBits = __builtin_popcount(pictFormInfo->direct.blue_mask);
    int alphaBits = __builtin_popcount(pictFormInfo->direct.alpha_mask);

    int attrs[] = {
        GLX_RENDER_TYPE,    GLX_RGBA_BIT,
        GLX_DRAWABLE_TYPE,  GLX_WINDOW_BIT | GLX_PIXMAP_BIT,
        GLX_X_VISUAL_TYPE,  GLX_TRUE_COLOR,
        GLX_X_RENDERABLE,   True,
        GLX_CONFIG_CAVEAT,  int(GLX_DONT_CARE),
        GLX_BUFFER_SIZE,    redBits + greenBits + blueBits + alphaBits,
        GLX_RED_SIZE,       redBits,
        GLX_GREEN_SIZE,     greenBits,
        GLX_BLUE_SIZE,      blueBits,
        GLX_ALPHA_SIZE,     alphaBits,
        GLX_STENCIL_SIZE,   0,
        GLX_DEPTH_SIZE,     0,
        0
    };

    int nConfigs = 0;
    GLXFBConfig *configs = glXChooseFBConfig(display, screen, attrs, &nConfigs);
    if (nConfigs <= 0) {
        qCritical(log) << "glXChooseFBConfig: no FBConfig for visual" << visual;
        return bestConfig;
    }

    for (int i = 0; i < nConfigs; i++) {
        VisualInfo info;
        info.config = configs[i];

#define INIT_FROM_ATTR(var, attr) \
        if (glXGetFBConfigAttrib(display, configs[i], attr, &var) != Success) { \
            qWarning(log) << ("Can't get " #attr " of FBConfig") << configs[i]; \
            continue; \
        }

        int fbRedBits, fbGreenBits, fbBlueBits;
        INIT_FROM_ATTR(fbRedBits, GLX_RED_SIZE);
        INIT_FROM_ATTR(fbGreenBits, GLX_GREEN_SIZE);
        INIT_FROM_ATTR(fbBlueBits, GLX_BLUE_SIZE);
        if (fbRedBits != redBits || fbGreenBits != greenBits || fbBlueBits != blueBits) {
            continue;
        }

        int fbVisualId;
        INIT_FROM_ATTR(fbVisualId, GLX_VISUAL_ID);
        if (visualDepth[xcb_visualid_t(fbVisualId)] != visualDepth[visual]) {
            continue;
        }

        int fbBindRGB = 0, fbBindRGBA = 0;
        glXGetFBConfigAttrib(display, configs[i], GLX_BIND_TO_TEXTURE_RGB_EXT, &fbBindRGB);
        glXGetFBConfigAttrib(display, configs[i], GLX_BIND_TO_TEXTURE_RGBA_EXT, &fbBindRGBA);
        if (!fbBindRGB && !fbBindRGBA) {
            continue;
        }

        glXGetFBConfigAttrib(display, configs[i], GLX_Y_INVERTED_EXT, &info.yInverted);

        int fbTextureTargets;
        INIT_FROM_ATTR(fbTextureTargets, GLX_BIND_TO_TEXTURE_TARGETS_EXT);
        if (!(fbTextureTargets & GLX_TEXTURE_2D_BIT_EXT)) {
            continue;
        }

        if (alphaBits) {
            info.alphaMatches = (fbBindRGBA != 0);
            info.textureFormat = fbBindRGBA ? GLX_TEXTURE_FORMAT_RGBA_EXT : GLX_TEXTURE_FORMAT_RGB_EXT;
        } else {
            info.alphaMatches = (fbBindRGB != 0);
            info.textureFormat = fbBindRGB ? GLX_TEXTURE_FORMAT_RGB_EXT : GLX_TEXTURE_FORMAT_RGBA_EXT;
        }

        INIT_FROM_ATTR(info.depth, GLX_DEPTH_SIZE);
        INIT_FROM_ATTR(info.stencil, GLX_STENCIL_SIZE);

        if (info.alphaMatches != bestConfig.alphaMatches) {
            if (info.alphaMatches) {
                bestConfig = info;
                continue;
            }
        } else if (info.depth + info.stencil < bestConfig.depth + bestConfig.stencil) {
            bestConfig = info;
        }
    }

    XFree(configs);
    return bestConfig;
}

const GLXInfo::VisualInfo &GLXInfo::configFor(xcb_visualid_t visual)
{
    auto existing = visualInfos.constFind(visual);
    if (existing != visualInfos.constEnd()) {
        return existing.value();
    }
    return visualInfos.insert(visual, createVisualInfo(visual)).value();
}

GLXTextureFromPixmap::GLXTextureFromPixmap(xcb_pixmap_t pixmap, xcb_visualid_t visual, const QSize &size)
    : QOpenGLFunctions(QOpenGLContext::currentContext()),
      texture_(0),
      glxPixmap_(XCB_NONE),
      pixmap_(pixmap),
      hasAlpha_(false),
      isYInverted_(false),
      size_(size),
      rebindTFP_(false)
{
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);

    setFiltering(Linear);
    setHorizontalWrapMode(ClampToEdge);
    setVerticalWrapMode(ClampToEdge);
    updateBindOptions(true);

    auto &glx = GLXInfo::instance();
    auto &info = glx.configFor(visual);
    if (info.config) {
        int attr[] = {
            GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
            GLX_TEXTURE_FORMAT_EXT, info.textureFormat,
            GLX_MIPMAP_TEXTURE_EXT, false,
            None
        };
        glxPixmap_ = glXCreatePixmap(glx.display, info.config, pixmap_, attr);
        rebindTFP_ = true;
        hasAlpha_ = (info.textureFormat == GLX_TEXTURE_FORMAT_RGBA_EXT);
        isYInverted_ = !info.yInverted;
    }
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
    return hasAlpha_;
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
    glBindTexture(GL_TEXTURE_2D, texture_);
    updateBindOptions();

    if (glxPixmap_ && rebindTFP_) {
        rebindTFP_ = false;

        auto &glx = GLXInfo::instance();
        glx.tfpBind(glx.display, glxPixmap_, GLX_FRONT_LEFT_EXT, Q_NULLPTR);
    }
}
