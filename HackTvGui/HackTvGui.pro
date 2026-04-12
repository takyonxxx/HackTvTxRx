QT       += core gui multimedia concurrent openglwidgets
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17
CONFIG -= debug_and_release
CONFIG += release

PARENT_DIR = $$absolute_path($$PWD/../)
INCLUDEPATH += $$PARENT_DIR/include
message($$PARENT_DIR)

win32 {
    WIN_LIB_DIR = $$absolute_path($$PARENT_DIR/lib/windows)
    INCLUDEPATH += $$PARENT_DIR/HackTvLib
    # Add lib/windows to DLL search path (no copy needed)
    # DLLs are loaded from lib/windows at runtime
    LIBS += -L$$WIN_LIB_DIR -lHackTvLib
}

macx {
    MACOS_LIB_DIR = $$absolute_path($$PARENT_DIR/lib/macos)
    INCLUDEPATH += $$PARENT_DIR/HackTvLib
    LIBS += -L$$MACOS_LIB_DIR -lHackTvLib
    QMAKE_RPATHDIR += $$MACOS_LIB_DIR
    QMAKE_LFLAGS += -Wl,-rpath,@loader_path/../../../lib/macos
    QMAKE_LFLAGS += -Wl,-rpath,$$MACOS_LIB_DIR
    QMAKE_LFLAGS += -Wl,-headerpad_max_install_names
    QMAKE_MACOSX_DEPLOYMENT_TARGET = 11.0
}

linux {
    LINUX_LIB_DIR = $$absolute_path($$PARENT_DIR/lib/linux)
    INCLUDEPATH += $$PARENT_DIR/HackTvLib
    LIBS += -L$$LINUX_LIB_DIR -lHackTvLib
    QMAKE_RPATHDIR += $$LINUX_LIB_DIR
    QMAKE_LFLAGS += -Wl,-rpath,\$$ORIGIN/../../../lib/linux
    QMAKE_LFLAGS += -Wl,-rpath,$$LINUX_LIB_DIR
    QMAKE_LFLAGS += -Wl,-rpath,/usr/local/lib
    QMAKE_CXXFLAGS += -fPIC
    LIBS += -ldl -lpthread
}

SOURCES += \
    audiooutput.cpp \
    glplotter.cpp \
    freqctrl.cpp \
    fmdemodulator.cpp \
    amdemodulator.cpp \
    main.cpp \
    mainwindow.cpp \
    meter.cpp

HEADERS += \
    audiooutput.h \
    constants.h \
    glplotter.h \
    freqctrl.h \
    fmdemodulator.h \
    amdemodulator.h \
    mainwindow.h \
    meter.h \
    modulator.h

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    resources.qrc
