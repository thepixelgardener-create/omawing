QT += core gui network qml quick quickcontrols2 multimedia opengl
CONFIG += release console
TARGET = render_benchmark
TEMPLATE = app

include(../src/src.pri)
SOURCES += render_benchmark.cpp
