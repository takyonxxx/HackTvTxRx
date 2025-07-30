QT       += core gui multimedia
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

PARENT_DIR = $$absolute_path($$PWD/../)
INCLUDEPATH += $$PARENT_DIR/include

message($$PARENT_DIR)

win32 {
    WIN_LIB_DIR = $$absolute_path($$PARENT_DIR/lib/windows)
    INCLUDEPATH += $$PARENT_DIR/HackTvLib
    # Link to the import library
    LIBS += -L$$WIN_LIB_DIR -lHackTvLib
}

macx {
    # Path to the library directory for macOS
    MACOS_LIB_DIR = $$absolute_path($$PARENT_DIR/lib/macos)
    # Include path for headers
    INCLUDEPATH += $$PARENT_DIR/HackTvLib
    # Link to the library with full path
    LIBS += -L$$MACOS_LIB_DIR -lHackTvLib
    # Add rpath so the app can find the library at runtime
    QMAKE_RPATHDIR += $$MACOS_LIB_DIR
    # Also add the library directory to the loader path
    QMAKE_LFLAGS += -Wl,-rpath,@loader_path/../../../lib/macos
    QMAKE_LFLAGS += -Wl,-rpath,$$MACOS_LIB_DIR
    # Set the install name for the library
    QMAKE_LFLAGS += -Wl,-headerpad_max_install_names
    # Set minimum macOS version if needed
    QMAKE_MACOSX_DEPLOYMENT_TARGET = 11.0
}

linux {
    # Path to the library directory for Linux
    LINUX_LIB_DIR = $$absolute_path($$PARENT_DIR/lib/linux)

    # Include path for headers
    INCLUDEPATH += $$PARENT_DIR/HackTvLib

    # Link to the library with full path
    LIBS += -L$$LINUX_LIB_DIR -lHackTvLib

    # Add rpath so the app can find the library at runtime
    QMAKE_RPATHDIR += $$LINUX_LIB_DIR

    # Set up runtime library paths
    QMAKE_LFLAGS += -Wl,-rpath,\$$ORIGIN/../../../lib/linux
    QMAKE_LFLAGS += -Wl,-rpath,$$LINUX_LIB_DIR
    QMAKE_LFLAGS += -Wl,-rpath,/usr/local/lib

    # Enable position independent code
    QMAKE_CXXFLAGS += -fPIC

    # Link additional Linux libraries that might be needed
    LIBS += -ldl -lpthread

    # Debug: Print library directory path
    message("Linux library directory: $$LINUX_LIB_DIR")

    # Create a post-build step to copy library if needed
    QMAKE_POST_LINK += echo "Checking library dependencies..." && \
                       ldd $$TARGET 2>/dev/null | grep -q "libHackTvLib" || \
                       echo "Warning: libHackTvLib not found in library path"

    # Add debug information
       CONFIG += debug_and_release
       CONFIG += separate_debug_info

       # Enable all warnings
       QMAKE_CXXFLAGS += -Wall -Wextra -Wno-unused-parameter

       # Add debug symbols
       QMAKE_CXXFLAGS_DEBUG += -g3 -O0
       QMAKE_CXXFLAGS_RELEASE += -O2 -DNDEBUG

       # Link against debug versions if available
       CONFIG(debug, debug|release) {
           LIBS += -ldl -lpthread -lrt
           TARGET = HackTvGuid
       } else {
           TARGET = HackTvGui
       }

       # Add memory debugging flags
       DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000
}

SOURCES += \
    audiooutput.cpp \
    cplotter.cpp \
    freqctrl.cpp \
    main.cpp \
    mainwindow.cpp \
    meter.cpp \
    palbdemodulator.cpp

HEADERS += \
    audiooutput.h \
    constants.h \
    cplotter.h \
    freqctrl.h \
    mainwindow.h \
    meter.h \
    modulator.h \
    palbdemodulator.h \
    tv_display.h

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    resources.qrc
#sudo apt install qt6-multimedia-dev gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly
#sudo apt install gstreamer1.0-plugins-good gstreamer1.0-plugins-bad
#export QT_MEDIA_BACKEND=gstreamer
