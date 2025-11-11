QT = core network

CONFIG += c++17 cmdline

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

PARENT_DIR = $$absolute_path($$PWD/../)
INCLUDEPATH += $$PARENT_DIR/include

SOURCES += \
        main.cpp \
        sdrdevice.cpp

HEADERS += \
    sdrdevice.h

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
}

linux {
    # Path to the library directory for Linux
    LINUX_LIB_DIR = $$absolute_path($$PARENT_DIR/lib/linux)
    # Include path for headers
    INCLUDEPATH += $$PARENT_DIR/HackTvLib
    # Link to the library with full path
    LIBS += -L$$LINUX_LIB_DIR -lHackTvLib
}

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
#C:\Qt\6.10.0\mingw_64\bin\windeployqt.exe C:\Users\MSI\Desktop\buildHackRfTcp\release\HackRfTcp.exe
#C:\Qt\6.10.0\mingw_64\bin\windeployqt.exe C:\Users\turka\Desktop\build-HackRfTcp\release\HackRfTcp.exe


DISTFILES += \
    README.md
