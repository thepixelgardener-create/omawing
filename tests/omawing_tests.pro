QT += core gui quick quickcontrols2 multimedia testlib
CONFIG += c++20 testcase
QMAKE_CXXFLAGS += -Wno-missing-field-initializers
TARGET = omawing_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/flight.h ../src/scene.h ../src/synth.h ../src/theme.h ../src/translator.h
SOURCES += omawing_tests.cpp ../src/backend.cpp ../src/flight.cpp ../src/scene.cpp ../src/synth.cpp \
    ../src/theme.cpp ../src/translator.cpp
