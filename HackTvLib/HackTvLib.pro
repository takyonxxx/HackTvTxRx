QT += core multimedia

TEMPLATE = lib
DEFINES += HACKTVLIB_LIBRARY

CONFIG += c++17

DEFINES += _USE_MATH_DEFINES
DEFINES += HAVE_HACKRF
DEFINES += HAVE_SOAPYSDR
DEFINES += HAVE_FL2K

win32:DEFINES += _WIN32

TOOLCHAIN_PATH = C:/msys64/ucrt64
MINGW_PATH = C:/msys64/mingw64

INCLUDEPATH += $$TOOLCHAIN_PATH/include
INCLUDEPATH += $$MINGW_PATH/include

LIBS += -L$$TOOLCHAIN_PATH/lib
LIBS += -L$$TOOLCHAIN_PATH/bin
LIBS += -L$$MINGW_PATH/lib
LIBS += -L$$MINGW_PATH/bin

LIBS += -lusb-1.0 -lhackrf -lfftw3f -losmo-fl2k -lSoapySDR -lfdk-aac -lopus -lportaudio
LIBS += -lfltk -lfltk_forms -lfltk_gl -lfltk_images
LIBS += -lavformat -lavdevice -lavcodec -lavutil -lavfilter -lswscale -lswresample

win32:LIBS += -lwinmm -lcomctl32 -lole32 -luuid -lws2_32 -lgdi32 -lshell32 -ladvapi32 -lcomdlg32

QMAKE_LFLAGS += -Wl,--subsystem,windows

SOURCES += \
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
    modulation.h

TRANSLATIONS += \
    HackTvLib_tr_TR.ts

# Default rules for deployment.
unix {
    target.path = /usr/lib
}
!isEmpty(target.path): INSTALLS += target

DISTFILES += \
    hacktv/README.md
