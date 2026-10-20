QT += core gui opengl
CONFIG += c++20 release console
QMAKE_CXXFLAGS += -Wno-missing-field-initializers
TARGET = render_benchmark
INCLUDEPATH += ../src
SOURCES += render_benchmark.cpp ../src/scene.cpp ../src/view.cpp ../src/sky.cpp ../src/ground.cpp ../src/worldlayer.cpp ../src/overlay.cpp ../src/flight.cpp ../src/game.cpp
