QT       += core gui multimedia concurrent openglwidgets network
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
    LIBS += -L$$WIN_LIB_DIR -lHackTvLib

    # Force EXE + moc + obj all into release/ subfolder
    DESTDIR = $$OUT_PWD/release
    OBJECTS_DIR = $$DESTDIR/.obj
    MOC_DIR = $$DESTDIR/.moc
    RCC_DIR = $$DESTDIR/.rcc
    UI_DIR = $$DESTDIR/.ui

    # 1. Copy project DLLs (HackTvLib, avcodec, etc.)
    WIN_LIB_DIR_NATIVE = $$shell_path($$WIN_LIB_DIR)
    QMAKE_POST_LINK += xcopy /Y /D \"$$WIN_LIB_DIR_NATIVE\\*.dll\" \"$$shell_path($$DESTDIR/)\" $$escape_expand(\\n\\t)

    # 2. windeployqt copies all Qt DLLs + platform plugins
    QMAKE_POST_LINK += windeployqt --release --no-translations \"$$shell_path($$DESTDIR/$${TARGET}.exe)\" $$escape_expand(\\n\\t)

    # 3. Copy ALL DLLs from MSYS2 mingw64/bin — guarantees no missing deps
    QMAKE_POST_LINK += xcopy /Y /D \"C:\\msys64\\mingw64\\bin\\*.dll\" \"$$shell_path($$DESTDIR/)\" $$escape_expand(\\n\\t)
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
