QT += core gui qml quick quickcontrols2 sql
CONFIG += c++17 release
TARGET = lore
TEMPLATE = app
HEADERS += src/store.h src/backend.h src/theme.h src/translator.h
SOURCES += src/main.cpp src/store.cpp src/backend.cpp src/theme.cpp src/translator.cpp
RESOURCES += src/resources.qrc
