QT += core multimedia

TEMPLATE = lib
CONFIG += c++17 shared

DEFINES += HACKTVLIB_LIBRARY
DEFINES += _USE_MATH_DEFINES
DEFINES += HAVE_HACKRF

win32 {
    DEFINES += _WIN32
    DEFINES += _WIN32_WINNT=0x0601
    DEFINES += WINVER=0x0601
    DEFINES += __USE_MINGW_ANSI_STDIO=1

    MSYS2_PATH = C:/msys64/mingw64

    # Include paths for dependencies from MSYS2
    INCLUDEPATH += $$MSYS2_PATH/include
    INCLUDEPATH += $$MSYS2_PATH/include/libusb-1.0

    # Library paths - ORDER MATTERS!
    LIBS += -L$$MSYS2_PATH/lib
    LIBS += -L$$MSYS2_PATH/bin

    # CRITICAL: Add pthread and Windows libraries FIRST
    LIBS += -lwinpthread -lpthread
    LIBS += -lws2_32 -lmingw32 -lmingwex

    # HackRF and USB (depends on pthread)
    LIBS += -lhackrf -lusb-1.0

    # Audio/Signal processing libraries
    LIBS += -lfftw3f -lfdk-aac -lopus -lportaudio -lrtlsdr

    # FFmpeg libraries (ORDER MATTERS - most dependent first)
    LIBS += -lavdevice -lavfilter -lavformat -lavcodec -lswresample -lswscale -lavutil

    # Additional Windows runtime libraries
    LIBS += -lm -lz -lbz2

    # Static linking for C++ runtime to avoid DLL dependencies
    QMAKE_LFLAGS += -static-libgcc -static-libstdc++

    # Enable large address aware (for 32-bit builds) - CORRECTED SYNTAX
    # Only use for 32-bit builds, comment out for 64-bit
    # QMAKE_LFLAGS += -Wl,--large-address-aware

    # Export all symbols for DLL (optional, can cause larger DLL)
    # QMAKE_LFLAGS += -Wl,--export-all-symbols

    # Better: Export only needed symbols
    QMAKE_LFLAGS += -Wl,--enable-auto-import

    # Compiler flags
    QMAKE_CXXFLAGS += -fpermissive
    QMAKE_CXXFLAGS += -D__STDC_CONSTANT_MACROS
    QMAKE_CXXFLAGS += -D__STDC_FORMAT_MACROS

    # For nanosleep compatibility
    QMAKE_CXXFLAGS += -D_POSIX_C_SOURCE=200112L
    QMAKE_CXXFLAGS += -D_GNU_SOURCE

    # Define the target library directory
    DESTDIR = $$PWD/../lib/windows

    # Create the directory if it doesn't exist
    !exists($$DESTDIR) {
        system(mkdir $$shell_path($$DESTDIR))
    }

    # Windows uses different naming for debug/release builds
    CONFIG(debug, debug|release) {
        TARGET = HackTvLibd  # Add 'd' suffix for debug builds
        QMAKE_CXXFLAGS += -g -O0
    } else {
        TARGET = HackTvLib
        QMAKE_CXXFLAGS += -O2
    }

    # Copy required DLLs to output directory (optional)
    # QMAKE_POST_LINK += $$quote(cmd /c copy /y $$shell_path($$MSYS2_PATH/bin/libwinpthread-1.dll) $$shell_path($$DESTDIR))
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
    LIBS += -lwinpthread

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

linux {
    # Linux specific configurations
    DEFINES += __linux__

    # Standard include paths for most distributions
    INCLUDEPATH += /usr/include
    INCLUDEPATH += /usr/include/libusb-1.0
    INCLUDEPATH += /usr/local/include
    INCLUDEPATH += /usr/local/include/libusb-1.0

    # Standard library paths
    LIBS += -L/usr/lib
    LIBS += -L/usr/lib/x86_64-linux-gnu
    LIBS += -L/usr/local/lib
    LIBS += -L/lib/x86_64-linux-gnu

    # Link to libraries
    LIBS += -lusb-1.0 -lhackrf -lfftw3f -lfdk-aac -lopus -lportaudio -lrtlsdr

    # FFmpeg libraries
    LIBS += -lavformat -lavdevice -lavcodec -lavutil -lavfilter -lswscale -lswresample

    # Additional Linux-specific libraries
    LIBS += -lpthread -lm -ldl

    # Compiler and linker flags
    QMAKE_CXXFLAGS += -fPIC
    QMAKE_LFLAGS += -Wl,-rpath,\$$ORIGIN

    # Define the target library directory
    DESTDIR = $$PWD/../lib/linux

    # Create the directory if it doesn't exist
    !exists($$DESTDIR) {
        system(mkdir -p $$DESTDIR)
    }

    # Set library version for Linux
    VERSION = 1.0.0

    # Post-build commands to create symlinks
    QMAKE_POST_LINK += cd $$DESTDIR && \
                       ln -sf libHackTvLib.so.1.0.0 libHackTvLib.so && \
                       ln -sf libHackTvLib.so.1.0.0 libHackTvLib.so.1 && \
                       ln -sf libHackTvLib.so.1.0.0 libHackTvLib.so.1.0

    # Installation commands for system-wide installation (optional)
    target.path = /usr/local/lib
    headers.path = /usr/local/include/hacktvlib
    headers.files = $$HEADERS
    INSTALLS += target headers
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
