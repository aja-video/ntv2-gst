TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
    ../gst-plugin/aja/gstaja.cpp \
    ../gst-plugin/aja/gstajaaudiosink.cpp \
    ../gst-plugin/aja/gstajaaudiosrc.cpp \
    ../gst-plugin/aja/gstajavideosink.cpp \
    ../gst-plugin/aja/gstajavideosrc.cpp \
    ../gst-plugin/aja/gstntv2.cpp \
    ../gst-plugin/aja/gstajahevcsrc.cpp

include(deployment.pri)
qtcAddDeployment()

HEADERS += \
    ../gst-plugin/aja/gstaja.h \
    ../gst-plugin/aja/gstajaaudiosink.h \
    ../gst-plugin/aja/gstajaaudiosrc.h \
    ../gst-plugin/aja/gstajavideosink.h \
    ../gst-plugin/aja/gstajavideosrc.h \
    ../gst-plugin/aja/gstntv2.h \
    ../gst-plugin/aja/gstajahevcsrc.h

