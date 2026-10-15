QT += core gui widgets multimedia

greaterThan(QT_MAJOR_VERSION, 5): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

# HackRF driver wrapper (HackRfDevice) lives in hackrf/; everything PALBDecoder
# needs is inside this folder, nothing is pulled from ../HackTvLib.
DRIVER_DIR = $$PWD/hackrf
INCLUDEPATH += $$DRIVER_DIR

DEFINES += _USE_MATH_DEFINES

win32 {
    DEFINES += _WIN32_WINNT=0x0601
    DEFINES += WINVER=0x0601
    DEFINES += __USE_MINGW_ANSI_STDIO=1

    # libhackrf + libusb (WinUSB backend) are vendored under 3rdparty/ and built
    # straight into the exe: no MSYS2, no extra DLLs.
    LIBUSB_DIR  = $$PWD/3rdparty/libusb
    LIBHACKRF_DIR = $$PWD/3rdparty/libhackrf

    # libusb: config.h is ours; <libusb.h> is used by hackrf.c
    INCLUDEPATH += $$LIBUSB_DIR
    INCLUDEPATH += $$LIBHACKRF_DIR/include
    INCLUDEPATH += $$LIBHACKRF_DIR/src

    SOURCES += \
        $$LIBUSB_DIR/core.c \
        $$LIBUSB_DIR/descriptor.c \
        $$LIBUSB_DIR/hotplug.c \
        $$LIBUSB_DIR/io.c \
        $$LIBUSB_DIR/strerror.c \
        $$LIBUSB_DIR/sync.c \
        $$LIBUSB_DIR/os/events_windows.c \
        $$LIBUSB_DIR/os/threads_windows.c \
        $$LIBUSB_DIR/os/windows_common.c \
        $$LIBUSB_DIR/os/windows_usbdk.c \
        $$LIBUSB_DIR/os/windows_winusb.c \
        $$LIBHACKRF_DIR/src/hackrf.c

    HEADERS += \
        $$LIBUSB_DIR/config.h \
        $$LIBUSB_DIR/libusb.h \
        $$LIBUSB_DIR/libusbi.h \
        $$LIBHACKRF_DIR/src/hackrf.h

    # hackrf.c is C99 with libusb/pthread; keep vendored C warnings out of the build log
    QMAKE_CFLAGS += -std=gnu11 -w
    QMAKE_LIBS += -lole32 -lsetupapi -luuid

    QMAKE_CXXFLAGS += -fpermissive
}

macx {
    # Same vendored libusb + libhackrf, IOKit backend. Only Apple system
    # frameworks are linked; no MacPorts / Homebrew needed.
    LIBUSB_DIR    = $$PWD/3rdparty/libusb
    LIBHACKRF_DIR = $$PWD/3rdparty/libhackrf

    INCLUDEPATH += $$LIBUSB_DIR
    INCLUDEPATH += $$LIBHACKRF_DIR/include
    INCLUDEPATH += $$LIBHACKRF_DIR/src

    SOURCES += \
        $$LIBUSB_DIR/core.c \
        $$LIBUSB_DIR/descriptor.c \
        $$LIBUSB_DIR/hotplug.c \
        $$LIBUSB_DIR/io.c \
        $$LIBUSB_DIR/strerror.c \
        $$LIBUSB_DIR/sync.c \
        $$LIBUSB_DIR/os/events_posix.c \
        $$LIBUSB_DIR/os/threads_posix.c \
        $$LIBUSB_DIR/os/darwin_usb.c \
        $$LIBHACKRF_DIR/src/hackrf.c

    HEADERS += \
        $$LIBUSB_DIR/config.h \
        $$LIBUSB_DIR/libusb.h \
        $$LIBUSB_DIR/libusbi.h \
        $$LIBHACKRF_DIR/src/hackrf.h

    QMAKE_CFLAGS += -std=gnu11 -w
    LIBS += -framework IOKit -framework CoreFoundation -framework Security
    LIBS += -lobjc
}

linux {
    # Same vendored libusb + libhackrf, usbfs backend with netlink hotplug
    # (no libudev). Nothing beyond libc / pthread is required.
    LIBUSB_DIR    = $$PWD/3rdparty/libusb
    LIBHACKRF_DIR = $$PWD/3rdparty/libhackrf

    INCLUDEPATH += $$LIBUSB_DIR
    INCLUDEPATH += $$LIBHACKRF_DIR/include
    INCLUDEPATH += $$LIBHACKRF_DIR/src

    SOURCES += \
        $$LIBUSB_DIR/core.c \
        $$LIBUSB_DIR/descriptor.c \
        $$LIBUSB_DIR/hotplug.c \
        $$LIBUSB_DIR/io.c \
        $$LIBUSB_DIR/strerror.c \
        $$LIBUSB_DIR/sync.c \
        $$LIBUSB_DIR/os/events_posix.c \
        $$LIBUSB_DIR/os/threads_posix.c \
        $$LIBUSB_DIR/os/linux_usbfs.c \
        $$LIBUSB_DIR/os/linux_netlink.c \
        $$LIBHACKRF_DIR/src/hackrf.c

    HEADERS += \
        $$LIBUSB_DIR/config.h \
        $$LIBUSB_DIR/libusb.h \
        $$LIBUSB_DIR/libusbi.h \
        $$LIBHACKRF_DIR/src/hackrf.h

    QMAKE_CFLAGS += -std=gnu11 -w
    LIBS += -lpthread
}

SOURCES += \
    audiodemodulator.cpp \
    audiooutput.cpp \
    main.cpp \
    MainWindow.cpp \
    PALDecoder.cpp \
    $$DRIVER_DIR/hackrfdevice.cpp

HEADERS += \
    FrameBuffer.h \
    MainWindow.h \
    PALDecoder.h \
    audiodemodulator.h \
    audiooutput.h \
    $$DRIVER_DIR/hackrfdevice.h \
    $$DRIVER_DIR/constants.h \
    $$DRIVER_DIR/modulation.h \
    $$DRIVER_DIR/types.h

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

# # Windows specific
# win32 {
#     RC_ICONS = icon.ico
# }

TARGET = PALBDecoder
TEMPLATE = app

#https://repo.radio/VE7UWU/sdrangel/src/commit/b2be9f6a0d4d4b161b190ca296fae6f0232c103d/plugins/channelrx/demodatv