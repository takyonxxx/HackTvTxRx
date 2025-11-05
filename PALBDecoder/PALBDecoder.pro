QT += core gui widgets multimedia

greaterThan(QT_MAJOR_VERSION, 5): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

PARENT_DIR = $$absolute_path($$PWD/../)
INCLUDEPATH += $$PARENT_DIR/include

message($$PARENT_DIR)

win32 {
    WIN_LIB_DIR = $$absolute_path($$PARENT_DIR/lib/windows)
    INCLUDEPATH += $$PARENT_DIR/HackTvLib
    # Link to the import library
    LIBS += -L$$WIN_LIB_DIR -lHackTvLib
}

SOURCES += \
    audiodemodulator.cpp \
    audiooutput.cpp \
    main.cpp \
    MainWindow.cpp \
    PALDecoder.cpp

HEADERS += \
    FrameBuffer.h \
    MainWindow.h \
    PALDecoder.h \
    audiodemodulator.h \
    audiooutput.h

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

# # Windows specific
# win32 {
#     RC_ICONS = icon.ico
# }

TARGET = PALBDecoder
TEMPLATE = app

#https://repo.radio/VE7UWU/sdrangel/src/commit/b2be9f6a0d4d4b161b190ca296fae6f0232c103d/plugins/channelrx/demodatv
