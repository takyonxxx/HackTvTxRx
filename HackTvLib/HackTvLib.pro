QT += core multimedia

TEMPLATE = lib
CONFIG += c++17

DEFINES += HACKTVLIB_LIBRARY
DEFINES += _USE_MATH_DEFINES
DEFINES += HAVE_HACKRF

win32 {
    DEFINES += _WIN32
    MINGW_PATH = C:/msys64/mingw64
    INCLUDEPATH += $$MINGW_PATH/include
    LIBS += -L$$MINGW_PATH/lib
    LIBS += -L$$MINGW_PATH/bin
    LIBS += -lusb-1.0 -lhackrf -lfftw3f -lfdk-aac -lopus -lportaudio -lrtlsdr
    LIBS += -lavformat -lavdevice -lavcodec -lavutil -lavfilter -lswscale -lswresample

    # Define the target library directory
    DESTDIR = $$PWD/../lib/windows

    # Create the directory if it doesn't exist
    !exists($$DESTDIR) {
        system(mkdir $$shell_path($$DESTDIR))
    }

    # Windows uses different naming for debug/release builds
    CONFIG(debug, debug|release) {
        TARGET = HackTvLibd  # Add 'd' suffix for debug builds
    } else {
        TARGET = HackTvLib
    }

    # Copy the DLL after building
    QMAKE_POST_LINK += $$QMAKE_COPY $$shell_path($$OUT_PWD/$(DESTDIR_TARGET)) $$shell_path($$DESTDIR/)

    # Copy dependent DLLs for distribution (optional)
    # You might want to copy these only for release builds
    CONFIG(release, debug|release) {
        # Define the DLLs you want to copy
        DEPENDENT_DLLS = \
            libusb-1.0.dll \
            libhackrf.dll \
            libfftw3f-3.dll \
            libfdk-aac-2.dll \
            libopus-0.dll \
            libportaudio.dll \
            librtlsdr.dll

        # Copy each DLL
        for(dll, DEPENDENT_DLLS) {
            QMAKE_POST_LINK += && $$QMAKE_COPY $$shell_path($$MINGW_PATH/bin/$$dll) $$shell_path($$DESTDIR/)
        }

        # Copy FFmpeg DLLs (these often have version numbers)
        FFMPEG_DLLS = \
            avformat-*.dll \
            avdevice-*.dll \
            avcodec-*.dll \
            avutil-*.dll \
            avfilter-*.dll \
            swscale-*.dll \
            swresample-*.dll

        # Copy FFmpeg DLLs with wildcards
        QMAKE_POST_LINK += && (for %i in ($$MINGW_PATH/bin/avformat-*.dll) do copy "%i" $$shell_path($$DESTDIR/))
        QMAKE_POST_LINK += && (for %i in ($$MINGW_PATH/bin/avdevice-*.dll) do copy "%i" $$shell_path($$DESTDIR/))
        QMAKE_POST_LINK += && (for %i in ($$MINGW_PATH/bin/avcodec-*.dll) do copy "%i" $$shell_path($$DESTDIR/))
        QMAKE_POST_LINK += && (for %i in ($$MINGW_PATH/bin/avutil-*.dll) do copy "%i" $$shell_path($$DESTDIR/))
        QMAKE_POST_LINK += && (for %i in ($$MINGW_PATH/bin/avfilter-*.dll) do copy "%i" $$shell_path($$DESTDIR/))
        QMAKE_POST_LINK += && (for %i in ($$MINGW_PATH/bin/swscale-*.dll) do copy "%i" $$shell_path($$DESTDIR/))
        QMAKE_POST_LINK += && (for %i in ($$MINGW_PATH/bin/swresample-*.dll) do copy "%i" $$shell_path($$DESTDIR/))
    }
}

macx {
    # macOS specific configurations
    DEFINES += __APPLE__

    # MacPorts paths
    MACPORTS_PREFIX = /opt/local

    # Include paths
    INCLUDEPATH += $$MACPORTS_PREFIX/include
    INCLUDEPATH += $$MACPORTS_PREFIX/include/libusb-1.0

    # Library paths
    LIBS += -L$$MACPORTS_PREFIX/lib

    # Libraries
    LIBS += -lusb-1.0 -lhackrf -lfftw3f -lfdk-aac -lopus -lportaudio -lrtlsdr

    # FFmpeg libraries
    LIBS += -lavformat -lavdevice -lavcodec -lavutil -lavfilter -lswscale -lswresample

    # Framework paths (if needed)
    QMAKE_MACOSX_DEPLOYMENT_TARGET = 11.0
    QMAKE_CXXFLAGS += -I$$MACPORTS_PREFIX/include
    QMAKE_LFLAGS += -L$$MACPORTS_PREFIX/lib

    # Set up proper install name for the library
    QMAKE_LFLAGS_SONAME = -Wl,-install_name,@rpath/

    # Define the target library directory
    DESTDIR = $$PWD/../lib/macos

    # Create the directory if it doesn't exist
    !exists($$DESTDIR) {
        system(mkdir -p $$DESTDIR)
    }

    # Post-build commands to create symlinks
    QMAKE_POST_LINK += cd $$DESTDIR && \
                       ln -sf libHackTvLib.1.0.0.dylib libHackTvLib.dylib && \
                       ln -sf libHackTvLib.1.0.0.dylib libHackTvLib.1.dylib && \
                       ln -sf libHackTvLib.1.0.0.dylib libHackTvLib.1.0.dylib && \
                       install_name_tool -id @rpath/libHackTvLib.1.dylib libHackTvLib.1.0.0.dylib
}

SOURCES += \
    hackrfdevice.cpp \
    hacktv/acp.c \
    hacktv/av.c \
    hacktv/av_ffmpeg.c \
    hacktv/av_test.c \
    hacktv/common.c \
    hacktv/dance.c \
    hacktv/eurocrypt.c \
    hacktv/fir.c \
    hacktv/mac.c \
    hacktv/nicam728.c \
    hacktv/rf.c \
    hacktv/rf_file.c \
    hacktv/rf_hackrf.c \
    hacktv/sis.c \
    hacktv/syster.c \
    hacktv/teletext.c \
    hacktv/vbidata.c \
    hacktv/video.c \
    hacktv/videocrypt.c \
    hacktv/videocrypts.c \
    hacktv/vitc.c \
    hacktv/vits.c \
    hacktv/wss.c \
    hacktvlib.cpp \
    rtlsdrdevice.cpp

HEADERS += \
    HackTvLib_global.h \
    audioinput.h \
    constants.h \
    hackrfdevice.h \
    hacktv/acp.h \
    hacktv/av.h \
    hacktv/av_ffmpeg.h \
    hacktv/av_test.h \
    hacktv/common.h \
    hacktv/dance.h \
    hacktv/eurocrypt.h \
    hacktv/fir.h \
    hacktv/hacktv.h \
    hacktv/mac.h \
    hacktv/nicam728.h \
    hacktv/rf.h \
    hacktv/rf_file.h \
    hacktv/rf_hackrf.h \
    hacktv/sis.h \
    hacktv/syster.h \
    hacktv/teletext.h \
    hacktv/vbidata.h \
    hacktv/video.h \
    hacktv/videocrypt.h \
    hacktv/videocrypts-sequence.h \
    hacktv/videocrypts.h \
    hacktv/vitc.h \
    hacktv/vits.h \
    hacktv/wss.h \
    hacktvlib.h \
    modulation.h \
    rtlsdrdevice.h \
    stream_tx.h \
    types.h

TRANSLATIONS += \
    HackTvLib_tr_TR.ts

!isEmpty(target.path): INSTALLS += target

DISTFILES += \
    hacktv/README.md

#sudo apt install qt5-default qtmultimedia5-dev build-essential libusb-1.0-0-dev libhackrf-dev libfftw3-dev libosmo-fl2k-dev portaudio19-dev libfltk1.3-dev libavcodec-dev libavdevice-dev libavfilter-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev
#brew install qt libusb hackrf fftw portaudio ffmpeg
#export PATH="/usr/local/opt/qt/bin:$PATH"
#brew link qt --force
# pacman -S mingw-w64-x86_64-gcc
# pacman -S mingw-w64-x86_64-qt5
# pacman -S mingw-w64-x86_64-hackrf
# pacman -S mingw-w64-x86_64-rtl-sdr
# pacman -S mingw-w64-x86_64-ffmpeg
# pacman -S mingw-w64-x86_64-portaudio
# pacman -S mingw-w64-x86_64-fftw
# pacman -S mingw-w64-x86_64-fdk-aac
# pacman -S mingw-w64-x86_64-opus
# pacman -S mingw-w64-x86_64-libusb
