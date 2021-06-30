plugin_LTLIBRARIES = libgstaja.la

libgstaja_la_CPPFLAGS = \
	-std=c++11 \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	-DAJALinux=1 \
	-DAJA_LINUX=1 \
	-I$(GST_NTV2)/ajalibraries/ajantv2/includes \
	-I$(GST_NTV2)/ajalibraries/ajantv2/src \
	-I$(GST_NTV2)/ajalibraries/ajantv2/src/lin \
	-I$(GST_NTV2)/ajalibraries/ajantv2/codecs/hevc/m31 \
	-I$(GST_NTV2)/ajalibraries/ajabase \
	-I$(GST_NTV2)/ajalibraries

if ENABLE_NVMM
libgstaja_la_CPPFLAGS += \
	-DENABLE_NVMM \
	-I$(GST_DEEPSTREAM)/sources/includes \
	-I$(GST_CUDA)/include
endif

libgstaja_la_LIBADD = \
	-L$(GST_NTV2)/lib \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-1.0 \
	-lgstvideo-1.0 \
	-lajantv2 \
	-lpthread \
	-lrt \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(LIBM)

if ENABLE_NVMM
libgstaja_la_LIBADD += \
	-L$(GST_DEEPSTREAM)/lib \
	-lnvdsbufferpool \
	-lcuda
endif

libgstaja_la_LDFLAGS = -std=c++11 $(GST_PLUGIN_LDFLAGS)
libgstaja_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

# sources used to compile this plug-in
libgstaja_la_SOURCES = \
	gstaja.cpp \
	gstajavideosrc.cpp \
	gstajaaudiosrc.cpp \
	gstajadeviceprovider.cpp \
	gstntv2.cpp
#	gstajavideosink.cpp
#	gstajaaudiosink.cpp
#	gstajahevcsrc.cpp

# headers we need but don't want installed
noinst_HEADERS = \
	gstaja.h \
	gstajavideosrc.h \
	gstajaaudiosrc.h \
	gstajadeviceprovider.h \
	gstntv2.h
#	gstajahevcsrc.h
#	gstajavideosink.cpp
#	gstajaaudiosink.cpp
