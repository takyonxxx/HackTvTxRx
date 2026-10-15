QT += core gui widgets multimedia opengl openglwidgets

greaterThan(QT_MAJOR_VERSION, 5): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

# HackRF driver wrapper (HackRfDevice) lives in hackrf/; everything HackTvStudio
# needs is inside this folder, nothing is pulled from ../HackTvLib.
DRIVER_DIR = $$PWD/hackrf
INCLUDEPATH += $$DRIVER_DIR
INCLUDEPATH += $$PWD

DEFINES += _USE_MATH_DEFINES
DEFINES += _CRT_SECURE_NO_WARNINGS
INCLUDEPATH += $$PWD/hacktv

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

    LIBS += -lole32 -lsetupapi -luuid -ladvapi32

    # Toolchain-specific flags for the vendored C code (hackrf.c is C99 + pthreads)
    win32-g++ {
        # MinGW ships winpthreads; keep vendored C warnings out of the build log
        QMAKE_CFLAGS += -std=gnu11 -w
    }
    win32-msvc {
        # MSVC has no pthread.h: use our minimal Win32-backed shim
        INCLUDEPATH += $$PWD/3rdparty/pthread-msvc
        # hacktv's teletext.c wants POSIX dirent.h: FindFirstFile-based shim
        INCLUDEPATH += $$PWD/3rdparty/msvc-compat
        HEADERS += $$PWD/3rdparty/msvc-compat/dirent.h
        HEADERS += $$PWD/3rdparty/pthread-msvc/pthread.h
        QMAKE_CFLAGS += /std:c11 /w
    }
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
    PALDecoder.cpp \
    core/BandPlan.cpp \
    core/DeviceController.cpp \
    core/RxEngine.cpp \
    core/TxEngine.cpp \
    dsp/RadioModulator.cpp \
    tv/QtAvSource.cpp \
    tv/TvTxEncoder.cpp \
    ui/RadioTxPanel.cpp \
    ui/TvTxPanel.cpp \
    hacktv/acp.c \
    hacktv/av.c \
    hacktv/av_test.c \
    hacktv/common.c \
    hacktv/dance.c \
    hacktv/eurocrypt.c \
    hacktv/fir.c \
    hacktv/mac.c \
    hacktv/nicam728.c \
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
    ui/StudioWindow.cpp \
    ui/TvRxPanel.cpp \
    ui/RadioRxPanel.cpp \
    ui/TvRecorder.cpp \
    dsp/fmdemodulator.cpp \
    dsp/amdemodulator.cpp \
    widgets/glplotter.cpp \
    widgets/freqctrl.cpp \
    widgets/meter.cpp \
    $$DRIVER_DIR/hackrfdevice.cpp

HEADERS += \
    FrameBuffer.h \
    PALDecoder.h \
    audiodemodulator.h \
    audiooutput.h \
    core/BandPlan.h \
    core/DeviceController.h \
    core/Fft.h \
    core/RxEngine.h \
    core/TxEngine.h \
    dsp/RadioModulator.h \
    tv/QtAvSource.h \
    tv/TvTxEncoder.h \
    ui/RadioTxPanel.h \
    ui/TvTxPanel.h \
    hacktv/acp.h \
    hacktv/av.h \
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
    core/SimdDot.h \
    core/SpscRing.h \
    core/WorkerQueue.h \
    ui/ModePanel.h \
    ui/StudioWindow.h \
    ui/TvRxPanel.h \
    ui/RadioRxPanel.h \
    ui/TvRecorder.h \
    dsp/fmdemodulator.h \
    dsp/amdemodulator.h \
    ui/VideoView.h \
    widgets/glplotter.h \
    widgets/freqctrl.h \
    widgets/meter.h \
    $$DRIVER_DIR/hackrfdevice.h \
    $$DRIVER_DIR/constants.h \
    $$DRIVER_DIR/modulation.h \
    $$DRIVER_DIR/types.h

RESOURCES += resources.qrc

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

# # Windows specific
# win32 {
#     RC_ICONS = icon.ico
# }

TARGET = HackTvStudio
TEMPLATE = app

#https://repo.radio/VE7UWU/sdrangel/src/commit/b2be9f6a0d4d4b161b190ca296fae6f0232c103d/plugins/channelrx/demodatv