QT += core gui network qml quick quickcontrols2 multimedia

CONFIG += c++20 release
QMAKE_CXXFLAGS += -Wno-missing-field-initializers
TARGET = omawing
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/flight.h \
    src/game.h \
    src/geometry.h \
    src/flightview.h \
    src/scene.h \
    src/audio.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/flight.cpp \
    src/game.cpp \
    src/flightview.cpp \
    src/scene.cpp \
    src/audio.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
