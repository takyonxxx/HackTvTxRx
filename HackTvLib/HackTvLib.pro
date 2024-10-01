QT += core multimedia

TEMPLATE = lib
CONFIG += c++17

DEFINES += HACKTVLIB_LIBRARY
DEFINES += _USE_MATH_DEFINES
DEFINES += HAVE_HACKRF
DEFINES += HAVE_SOAPYSDR
DEFINES += HAVE_FL2K

win32 {
    DEFINES += _WIN32
    TOOLCHAIN_PATH = C:/msys64/ucrt64
    MINGW_PATH = C:/msys64/mingw64
    INCLUDEPATH += $$TOOLCHAIN_PATH/include
    INCLUDEPATH += $$MINGW_PATH/include
    LIBS += -L$$TOOLCHAIN_PATH/lib
    LIBS += -L$$TOOLCHAIN_PATH/bin
    LIBS += -L$$MINGW_PATH/lib
    LIBS += -L$$MINGW_PATH/bin
    LIBS += -lwinmm -lcomctl32 -lole32 -luuid -lws2_32 -lgdi32 -lshell32 -ladvapi32 -lcomdlg32
    QMAKE_LFLAGS += -Wl,--subsystem,windows
}

unix {
    INCLUDEPATH += /usr/local/include
    LIBS += -L/usr/local/lib
    macx {    
       HOMEBREW_PREFIX = $$system(brew --prefix)
       !isEmpty(HOMEBREW_PREFIX) {
           message("Homebrew found at $$HOMEBREW_PREFIX")
           INCLUDEPATH += $$HOMEBREW_PREFIX/include
           LIBS += -L$$HOMEBREW_PREFIX/lib

           # Add specific paths for Qt
           QT_PREFIX = $$system(brew --prefix qt)
           !isEmpty(QT_PREFIX) {
               INCLUDEPATH += $$QT_PREFIX/include
               LIBS += -L$$QT_PREFIX/lib
           }
           HACKRF_PREFIX = $$system(brew --prefix hackrf)
           !isEmpty(HACKRF_PREFIX) {
               INCLUDEPATH += $$HACKRF_PREFIX/include
               LIBS += -L$$HACKRF_PREFIX/lib
           }
       } else {
           error("Homebrew not found. Please install Homebrew and required dependencies.")
       }
    } else {
        # Linux specific configurations
        INCLUDEPATH += /usr/include
        LIBS += -L/usr/lib
    }
}

LIBS += -lusb-1.0 -lhackrf -lfftw3f -losmo-fl2k -lSoapySDR -lfdk-aac -lopus -lportaudio
LIBS += -lfltk -lfltk_forms -lfltk_gl -lfltk_images
LIBS += -lavformat -lavdevice -lavcodec -lavutil -lavfilter -lswscale -lswresample

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
    hacktv/rf_fl2k.c \
    hacktv/rf_hackrf.c \
    hacktv/rf_soapysdr.c \
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
    hacktvlib.cpp

HEADERS += \
    HackTvLib_global.h \
    audioinput.h \
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
    hacktv/rf_fl2k.h \
    hacktv/rf_hackrf.h \
    hacktv/rf_soapysdr.h \
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
    stream_tx.h \
    types.h

TRANSLATIONS += \
    HackTvLib_tr_TR.ts

# Default rules for deployment.
unix {
    target.path = /usr/lib
}
!isEmpty(target.path): INSTALLS += target

DISTFILES += \
    hacktv/README.md

#sudo apt install qt5-default qtmultimedia5-dev build-essential libusb-1.0-0-dev libhackrf-dev libfftw3-dev libosmo-fl2k-dev libsoapysdr-dev libfdk-aac-dev libopus-dev portaudio19-dev libfltk1.3-dev libavcodec-dev libavdevice-dev libavfilter-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev
#brew install qt libusb hackrf fftw osmo-fl2k soapysdr fdk-aac opus portaudio fltk ffmpeg
#export PATH="/usr/local/opt/qt/bin:$PATH"
#brew link qt --force
