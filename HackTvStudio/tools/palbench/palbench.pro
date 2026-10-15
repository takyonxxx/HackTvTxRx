# Offline PAL decoder / audio demodulator benchmark (no hardware needed).
# Feeds one synthetic PAL-like frame repeatedly and prints ms per 40 ms frame.
# Build: qmake palbench.pro "CONFIG+=release" && make      (MinGW or MSVC kit)
QT += core gui
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = palbench
INCLUDEPATH += $$PWD/../..
SOURCES += main.cpp $$PWD/../../PALDecoder.cpp $$PWD/../../audiodemodulator.cpp
HEADERS += $$PWD/../../PALDecoder.h $$PWD/../../audiodemodulator.h
