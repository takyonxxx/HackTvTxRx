QT += core gui widgets network multimedia

greaterThan(QT_MAJOR_VERSION, 5): QT += core5compat

CONFIG += c++17

TARGET = HackRfRadio

SOURCES += \
    main.cpp \
    radiowindow.cpp \
    tcpclient.cpp \
    audiocapture.cpp \
    audioplayback.cpp \
    fmdemodulator.cpp \
    amdemodulator.cpp \
    frequencywidget.cpp \
    signalmeter.cpp \
    gainsettingsdialog.cpp

HEADERS += \
    radiowindow.h \
    tcpclient.h \
    audiocapture.h \
    audioplayback.h \
    fmdemodulator.h \
    amdemodulator.h \
    frequencywidget.h \
    signalmeter.h \
    gainsettingsdialog.h

win32 {
    DEFINES += _WIN32
    QMAKE_CXXFLAGS += -fpermissive
    QMAKE_LFLAGS += -static-libgcc -static-libstdc++
}

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
