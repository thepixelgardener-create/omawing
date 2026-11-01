QT += core gui network qml quick quickcontrols2 multimedia

CONFIG += release
TARGET = omawing
TEMPLATE = app

include(src/src.pri)
SOURCES += src/main.cpp
