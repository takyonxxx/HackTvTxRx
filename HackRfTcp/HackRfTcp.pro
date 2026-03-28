QT = core network

CONFIG += c++17 cmdline

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
    LIBS += -L$$WIN_LIB_DIR -lHackTvLib
}

macx {
    MACOS_LIB_DIR = $$absolute_path($$PARENT_DIR/lib/macos)
    INCLUDEPATH += $$PARENT_DIR/HackTvLib
    LIBS += -L$$MACOS_LIB_DIR -lHackTvLib
}

linux {
    LINUX_LIB_DIR = $$absolute_path($$PARENT_DIR/lib/linux)
    INCLUDEPATH += $$PARENT_DIR/HackTvLib
    LIBS += -L$$LINUX_LIB_DIR -lHackTvLib
}

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

DISTFILES += \
    README.md
