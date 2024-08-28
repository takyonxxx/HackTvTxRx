QT       += core gui widgets

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17
#PATH_DESKTOP = $$system(echo %USERPROFILE%\\Desktop)
PARENT_DIR = $$absolute_path($$PWD/../)

INCLUDEPATH += $$PARENT_DIR/include
LIBS += -L$$PARENT_DIR/lib -lHackTvLib

SOURCES += \   
    main.cpp \
    mainwindow.cpp

HEADERS += \    
    mainwindow.h

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
