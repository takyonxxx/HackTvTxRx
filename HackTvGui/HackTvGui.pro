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
