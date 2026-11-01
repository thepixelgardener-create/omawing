# The game's sources, shared by the app, the tests, and the benchmark, so a
# new file is listed once. main.cpp is the app's alone.

lessThan(QT_MAJOR_VERSION, 6) | if(equals(QT_MAJOR_VERSION, 6):lessThan(QT_MINOR_VERSION, 8)) {
    # QAudioBufferOutput, which the beat tracker listens through, is new in 6.8.
    error("Omawing needs Qt 6.8 or newer; this is Qt $$QT_VERSION.")
}

VERSION = 0.4.0
DEFINES += OMAWING_VERSION=\\\"$$VERSION\\\"

CONFIG += c++20
# The tree builds cleanly with these; keep it that way.
QMAKE_CXXFLAGS += -Wall -Wextra -Wno-missing-field-initializers

INCLUDEPATH += $$PWD

HEADERS += \
    $$PWD/backend.h \
    $$PWD/flight.h \
    $$PWD/game.h \
    $$PWD/geometry.h \
    $$PWD/flightview.h \
    $$PWD/scene.h \
    $$PWD/view.h \
    $$PWD/sky.h \
    $$PWD/ground.h \
    $$PWD/worldlayer.h \
    $$PWD/overlay.h \
    $$PWD/shipshape.h \
    $$PWD/scoreboard.h \
    $$PWD/audio.h \
    $$PWD/theme.h \
    $$PWD/translator.h

SOURCES += \
    $$PWD/backend.cpp \
    $$PWD/flight.cpp \
    $$PWD/game.cpp \
    $$PWD/flightview.cpp \
    $$PWD/scene.cpp \
    $$PWD/view.cpp \
    $$PWD/sky.cpp \
    $$PWD/ground.cpp \
    $$PWD/worldlayer.cpp \
    $$PWD/overlay.cpp \
    $$PWD/scoreboard.cpp \
    $$PWD/audio.cpp \
    $$PWD/theme.cpp \
    $$PWD/translator.cpp

RESOURCES += $$PWD/resources.qrc
