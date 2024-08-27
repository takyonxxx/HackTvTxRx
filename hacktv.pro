QT = core

CONFIG += c++17 cmdline
CONFIG += mingw

DEFINES += _USE_MATH_DEFINES
DEFINES += HAVE_HACKRF

win32:DEFINES += _WIN32

MSYS2_PATH = C:/msys64/ucrt64
MSYS2_PATH_MINGW = C:/msys64/mingw64
# PATH_LIB = $$system(echo %USERPROFILE%\\Desktop)


INCLUDEPATH += $$MSYS2_PATH/include
INCLUDEPATH += $$MSYS2_PATH_MINGW/include

LIBS += -L$$MSYS2_PATH/lib
LIBS += -L$$MSYS2_PATH/bin
LIBS += -L$$MSYS2_PATH_MINGW/lib
LIBS += -L$$MSYS2_PATH_MINGW/bin

LIBS += -lusb-1.0 -lhackrf -lfftw3f -losmo-fl2k -lSoapySDR -lfdk-aac -lopus
LIBS += -lfltk -lfltk_forms -lfltk_gl -lfltk_images
LIBS += -lavformat -lavdevice -lavcodec -lavutil -lavfilter -lswscale -lswresample

win32:LIBS += -lwinmm -lcomctl32 -lole32 -luuid -lws2_32 -lgdi32 -lshell32 -ladvapi32 -lcomdlg32

QMAKE_LFLAGS += -Wl,--subsystem,windows

SOURCES += \
    acp.c \
    av.c \
    av_ffmpeg.c \
    av_test.c \
    common.c \
    dance.c \
    eurocrypt.c \
    fir.c \
    hacktv.c \
    mac.c \
    nicam728.c \
    rf.c \
    rf_file.c \
    rf_fl2k.c \
    rf_hackrf.c \
    rf_soapysdr.c \
    sis.c \
    syster.c \
    teletext.c \
    vbidata.c \
    video.c \
    videocrypt.c \
    videocrypts.c \
    vitc.c \
    vits.c \
    wss.c

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

DISTFILES += \
    README.md \
    demo.tti

HEADERS += \
    acp.h \
    av.h \
    av_ffmpeg.h \
    av_test.h \
    common.h \
    dance.h \
    eurocrypt.h \
    fir.h \
    hacktv.h \
    mac.h \
    nicam728.h \
    rf.h \
    rf_file.h \
    rf_fl2k.h \
    rf_hackrf.h \
    rf_soapysdr.h \
    sis.h \
    syster.h \
    teletext.h \
    vbidata.h \
    video.h \
    videocrypt.h \
    videocrypts-sequence.h \
    videocrypts.h \
    vitc.h \
    vits.h \
    wss.h

# INSTALL MSYS: https://www.msys2.org/

# pacman -Syu
# pacman -S git
# pacman -S mingw-w64-ucrt-x86_64-gcc
# pacman -S mingw-w64-ucrt-x86_64-cmake
# pacman -S mingw-w64-ucrt-x86_64-make
# pacman -S mingw-w64-ucrt-x86_64-libusb
# pacman -S mingw-w64-ucrt-x86_64-fftw
# pacman -S mingw-w64-clang-x86_64-toolchain
# pacman -S mingw-w64-ucrt-x86_64-git
# export PKG_CONFIG_PATH=/mingw64/lib/pkgconfig:$PKG_CONFIG_PATH

# git clone https://github.com/mossmann/hackrf.git
# cd hackrf/host
# mkdir build
# cd build
# cmake -G "MinGW Makefiles" -DCMAKE_INSTALL_PREFIX=/mingw64 ..
# if error do this
# cmake -G "MinGW Makefiles" -DCMAKE_INSTALL_PREFIX=/mingw64 \
# -DLIBUSB_INCLUDE_DIR=/ucrt64/include/libusb-1.0 \
# -DLIBUSB_LIBRARIES=/ucrt64/lib/libusb-1.0.dll.a \
# -DFFTW_INCLUDES=/ucrt64/include \
# -DFFTW_LIBRARIES=/ucrt64/lib/libfftw3.dll.a \
# ..
# goto C:\msys64\osmo-fl2k\src\fl2k_fm.c:455  comment between 455 and 484 while loop
# mingw32-make
# mingw32-make install

# git clone https://gitea.osmocom.org/sdr/osmo-fl2k.git
# cd osmo-fl2k
# mkdir build
# cd build
# cmake -G "MinGW Makefiles" -DCMAKE_INSTALL_PREFIX=/mingw64 ..

# pacman -S mingw-w64-x86_64-ffmpeg
# pacman -S mingw-w64-x86_64-soapysdr
# pacman -S mingw-w64-x86_64-fltk
# pacman -S mingw-w64-x86_64-opus
# pacman -S mingw-w64-x86_64-fdk-aac


