QT += core gui widgets network multimedia openglwidgets

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
    meter.cpp \
    glplotter.cpp \
    gainsettingsdialog.cpp

HEADERS += \
    radiowindow.h \
    tcpclient.h \
    audiocapture.h \
    audioplayback.h \
    fmdemodulator.h \
    amdemodulator.h \
    frequencywidget.h \
    meter.h \
    glplotter.h \
    constants.h \
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

contains(ANDROID_TARGET_ARCH,arm64-v8a) {
    ANDROID_PACKAGE_SOURCE_DIR = \
        $$PWD/android
}

DISTFILES += \
    android/AndroidManifest.xml \
    android/build.gradle \
    android/gradle.properties \
    android/gradle/wrapper/gradle-wrapper.jar \
    android/gradle/wrapper/gradle-wrapper.properties \
    android/gradlew \
    android/gradlew.bat \
    android/res/drawable-hdpi/icon.png \
    android/res/drawable-ldpi/icon.png \
    android/res/drawable-mdpi/icon.png \
    android/res/drawable-xhdpi/icon.png \
    android/res/drawable-xxhdpi/icon.png \
    android/res/drawable-xxxhdpi/icon.png \
    android/res/values/libs.xml \
    android/res/xml/qtprovider_paths.xml
