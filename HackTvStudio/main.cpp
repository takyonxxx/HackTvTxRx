#include "ui/StudioWindow.h"
#include "ui/Theme.h"
#include <QSettings>
#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QtWidgets/QStyleFactory>
#include <QSurfaceFormat>
#include <QDebug>

int main(int argc, char *argv[])
{
    // Qt Multimedia: use the bundled FFmpeg backend (video file sources for TV TX)
    qputenv("QT_MEDIA_BACKEND", "ffmpeg");
    qputenv("AV_LOG_FORCE_NOCOLOR", "1");

#ifdef Q_OS_ANDROID
    // Weak car head-unit GPUs often refuse the default GL context (which asks for a
    // depth/stencil/MSAA surface), which crashes the spectrum's QOpenGLWidget on its
    // first show. Request the most widely supported surface: OpenGL ES 2.0, no extras.
    // (Android only: on Windows an ES request has no backing context and every
    // QOpenGLWidget fails with "Failed to create context".)
    {
        QSurfaceFormat fmt;
        fmt.setRenderableType(QSurfaceFormat::OpenGLES);
        fmt.setVersion(2, 0);
        fmt.setDepthBufferSize(0);
        fmt.setStencilBufferSize(0);
        fmt.setSamples(0);
        fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
        QSurfaceFormat::setDefaultFormat(fmt);
    }
#endif

    QApplication app(argc, argv);

    QApplication::setApplicationName("HackTvStudio");
    QApplication::setOrganizationName("MarenRobotics");
    QApplication::setApplicationVersion("2.0");
    app.setWindowIcon(QIcon(":/ico/hacktv.ico"));

    qDebug() << "Starting HackTvStudio...";
    qDebug() << "Qt Version:" << qVersion();

    app.setStyle(QStyleFactory::create("Fusion"));

#ifdef Q_OS_ANDROID
    // Head-unit screens are short; shrink the global font so the dense SDR
    // panels fit and leave more room for the spectrum / waterfall.
    {
        QFont f = app.font();
        qreal ps = f.pointSizeF() > 0 ? f.pointSizeF() : 10.0;
        f.setPointSizeF(ps * 0.72);
        app.setFont(f);
    }
#endif

    // Colour theme (switchable in the settings dialog; palette + stylesheet + spectrum colours)
    {
        QSettings s("MarenRobotics", "HackTvStudio");
        Themes::apply(Themes::byId(s.value("theme", "gray").toString()));   // SDR#-style grey; "cyber" = the original look
    }

    StudioWindow window;
    window.show();

    return app.exec();
}
