QT += core gui widgets network multimedia

CONFIG += c++17

TARGET = HackRfRadio

SOURCES += \
    main.cpp \
    radiowindow.cpp \
    tcpclient.cpp \
    audioplayback.cpp \
    fmdemodulator.cpp \
    amdemodulator.cpp \
    frequencywidget.cpp \
    meter.cpp \
    glplotter.cpp \
    gainsettingsdialog.cpp

ios {
    OBJECTIVE_SOURCES += audiocapture.mm
} else {
    SOURCES += audiocapture.cpp
}

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

# ══════════════════════════════════════
# Android
# ══════════════════════════════════════
contains(ANDROID_TARGET_ARCH,arm64-v8a) {
    ANDROID_PACKAGE_SOURCE_DIR = \
        $$PWD/android
}

# ══════════════════════════════════════
# iOS
# ══════════════════════════════════════
ios {
    QMAKE_INFO_PLIST = $$PWD/ios/Info.plist
    QMAKE_ASSET_CATALOGS += $$PWD/Assets.xcassets

    QMAKE_TARGET_BUNDLE_PREFIX = com.tbiliyor
    QMAKE_BUNDLE = HackRfRadio

    # Deployment target
    QMAKE_IOS_DEPLOYMENT_TARGET = 16.0

    # Device orientation
    QMAKE_APPLE_TARGETED_DEVICE_FAMILY = 1,2

    # Qt 6.10 Multimedia uses FFmpeg backend on iOS.
    # FFmpeg is shipped as dynamic xcframeworks under Qt's iOS SDK.
    FFMPEG_DIR = $$(HOME)/Qt/6.10.0/ios/lib/ffmpeg
    LIBS += -F$$FFMPEG_DIR/libavcodec.xcframework/ios-arm64 \
            -F$$FFMPEG_DIR/libavformat.xcframework/ios-arm64 \
            -F$$FFMPEG_DIR/libavutil.xcframework/ios-arm64 \
            -F$$FFMPEG_DIR/libswresample.xcframework/ios-arm64 \
            -F$$FFMPEG_DIR/libswscale.xcframework/ios-arm64
    LIBS += -framework libavcodec \
            -framework libavformat \
            -framework libavutil \
            -framework libswresample \
            -framework libswscale

    # Embed FFmpeg frameworks into app bundle Frameworks/ folder
    FFMPEG_FRAMEWORKS = libavcodec libavformat libavutil libswresample libswscale
    for(fw, FFMPEG_FRAMEWORKS) {
        eval($${fw}_embed.files = $$FFMPEG_DIR/$${fw}.xcframework/ios-arm64/$${fw}.framework)
        eval($${fw}_embed.path = Frameworks)
        eval(QMAKE_BUNDLE_DATA += $${fw}_embed)
    }

    # Ensure rpath includes @executable_path/Frameworks
    QMAKE_RPATHDIR += @executable_path/Frameworks

    # Apple frameworks required by FFmpeg + Qt Multimedia on iOS
    LIBS += -framework AudioToolbox \
            -framework AVFoundation \
            -framework CoreAudio \
            -framework CoreMedia \
            -framework CoreVideo \
            -framework VideoToolbox \
            -framework Security \
            -framework MediaPlayer \
            -liconv -lz -lbz2
}

# ══════════════════════════════════════
# macOS
# ══════════════════════════════════════
macos {
    QMAKE_INFO_PLIST = $$PWD/macos/Info.plist

    QMAKE_TARGET_BUNDLE_PREFIX = com.tbiliyor
    QMAKE_BUNDLE = HackRfRadio

    QMAKE_MACOSX_DEPLOYMENT_TARGET = 12.0

    ICON = $$PWD/ico/icon.png

    # Qt 6.10 Multimedia uses FFmpeg backend on macOS.
    # Link FFmpeg from Homebrew directly with full paths
    # to bypass Xcode library search path issues.
    LIBS += /usr/local/lib/libavcodec.dylib \
            /usr/local/lib/libavformat.dylib \
            /usr/local/lib/libavutil.dylib \
            /usr/local/lib/libswresample.dylib \
            /usr/local/lib/libswscale.dylib

    # Header search path for FFmpeg
    INCLUDEPATH += /usr/local/include

    # Apple frameworks required by FFmpeg + Qt Multimedia on macOS
    LIBS += -framework AudioToolbox \
            -framework AVFoundation \
            -framework CoreAudio \
            -framework CoreMedia \
            -framework CoreVideo \
            -framework VideoToolbox \
            -framework Security \
            -framework MediaPlayer \
            -framework OpenGL \
            -liconv -lz -lbz2
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
    android/res/xml/qtprovider_paths.xml \
    ios/Info.plist \
    macos/Info.plist
