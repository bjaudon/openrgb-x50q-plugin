TEMPLATE = lib
CONFIG += plugin c++17
QT += core widgets
TARGET = OpenRGBX50QPlugin
DESTDIR = build

X50Q_PLUGIN_COMMIT = $$(X50Q_PLUGIN_COMMIT)
isEmpty(X50Q_PLUGIN_COMMIT): X50Q_PLUGIN_COMMIT = unknown
DEFINES += X50Q_PLUGIN_COMMIT=\\\"$$X50Q_PLUGIN_COMMIT\\\"

X50Q_PLUGIN_URL = $$(X50Q_PLUGIN_URL)
isEmpty(X50Q_PLUGIN_URL): X50Q_PLUGIN_URL = ""
DEFINES += X50Q_PLUGIN_URL=\\\"$$X50Q_PLUGIN_URL\\\"

OPENRGB_SOURCE = $$(OPENRGB_SOURCE)
isEmpty(OPENRGB_SOURCE): error("Set OPENRGB_SOURCE to the matching OpenRGB source tree")

INCLUDEPATH += $$OPENRGB_SOURCE
INCLUDEPATH += $$OPENRGB_SOURCE/RGBController
INCLUDEPATH += $$OPENRGB_SOURCE/i2c_smbus

SOURCES += X50QPlugin.cpp \
           X50QController.cpp \
           $$OPENRGB_SOURCE/RGBController/RGBController.cpp

HEADERS += X50QPlugin.h \
           X50QController.h \
           X50QTargets.h

unix: LIBS += -lusb-1.0

