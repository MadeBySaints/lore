QT += core gui quick quickcontrols2 sql testlib
CONFIG += c++17 testcase
TARGET = lore_tests
TEMPLATE = app
INCLUDEPATH += ../src
HEADERS += ../src/store.h ../src/backend.h ../src/theme.h ../src/translator.h
SOURCES += lore_tests.cpp ../src/store.cpp ../src/backend.cpp ../src/theme.cpp ../src/translator.cpp
RESOURCES += ../src/resources.qrc
