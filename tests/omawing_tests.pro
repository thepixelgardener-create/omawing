QT += core gui network qml quick quickcontrols2 multimedia testlib
CONFIG += testcase
TARGET = omawing_tests
TEMPLATE = app

include(../src/src.pri)
SOURCES += omawing_tests.cpp
# The tests play the soundtrack and read the catalogs and sources from the
# source tree.
DEFINES += OMAWING_TEST_MUSIC=\\\"$$PWD/../src/audio\\\" OMAWING_TEST_SOURCE=\\\"$$PWD/../src\\\"
