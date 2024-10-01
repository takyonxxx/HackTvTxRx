QT       += core gui multimedia

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17
PARENT_DIR = $$absolute_path($$PWD/../)
INCLUDEPATH += $$PARENT_DIR/include

win32 {
    #PATH_DESKTOP = $$system(echo %USERPROFILE%\\Desktop)
    LIBS += -L$$PARENT_DIR/lib -lHackTvLib
}

unix {
    INCLUDEPATH += /usr/local/include
    LIBS += -L/usr/local/lib

    linux {
        INCLUDEPATH += /usr/include
        LIBS += -L/usr/lib
    }

    macx {
       HOMEBREW_PREFIX = $$system(brew --prefix)
       !isEmpty(HOMEBREW_PREFIX) {
           message("Homebrew found at $$HOMEBREW_PREFIX")
           INCLUDEPATH += $$HOMEBREW_PREFIX/include
           LIBS += -L$$HOMEBREW_PREFIX/lib
       } else {
           error("Homebrew not found. Please install Homebrew and required dependencies.")
       }
    }
     LIBS += -lHackTvLib
}

SOURCES += \
    audiooutput.cpp \
    cplotter.cpp \
    freqctrl.cpp \
    main.cpp \
    mainwindow.cpp \
    meter.cpp \
    palbdemodulator.cpp \
    signalprocessor.cpp

HEADERS += \
    audiooutput.h \
    constants.h \
    cplotter.h \
    freqctrl.h \
    mainwindow.h \
    meter.h \
    modulator.h \
    palbdemodulator.h \
    signalprocessor.h \
    tv_display.h

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    resources.qrc
