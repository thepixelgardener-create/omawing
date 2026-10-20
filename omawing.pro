QT += core gui qml quick quickcontrols2 multimedia

CONFIG += c++20 release
QMAKE_CXXFLAGS += -Wno-missing-field-initializers
TARGET = omawing
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/flight.h \
    src/flightview.h \
    src/scene.h \
    src/synth.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/flight.cpp \
    src/flightview.cpp \
    src/scene.cpp \
    src/synth.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
