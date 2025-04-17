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
