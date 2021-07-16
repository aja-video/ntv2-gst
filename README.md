# AJA NTV2 GStreamer

## Overview

This repository contains the AJA NTV2 GStreamer Plugin.  Currently AJA
provides source elements for Uncompressed video (x-raw), HEVC/H.265
Compressed video (x-h265), and uncompressed PCM audio (x-raw S16LE, S32LE).

## Version (tag)

	v1.0.0		initial release

## Requirements

Building the gstreamer plugin requires the AJA NTV2 SDK (enroll as a
developer at www.aja.com) and gstreamer developer library and tools.

The gstreamer plugin supports most AJA Video input boards including
the corvidHEVC.

## Building the plugin

The plugin links with static builds of ajabase and ajantv2.

Build ajabase and ajantv2:

    make -C ajalibraries/ajabase
    make -C ajalibraries/ajantv2

Build the NTV2 GST plugin:

    export GST_NTV2=your_ntv2_sdk_dir
    cd ajaplugins/gstreamer/gst-plugin
    ./autogen.sh
    make
    sudo make install

## Testing the plugin

Follow the instructions included with the SDK to build and install the
AJA driver.

Test the gst plugin:

    export GST_PLUGIN_PATH=/usr/local/lib/gstreamer-1.0
    gst-launch-1.0 ajavideosrc ! videoconvert ! autovideosink

## NVIDIA RDMA Support

This plugin includes optional support for outputting video frames directly
to NVIDIA GPU memory via NVMM buffers using RDMA. Enabling this support
requires the [NVIDIA CUDA Toolkit](https://developer.nvidia.com/cuda-toolkit)
and [DeepStream](https://developer.nvidia.com/deepstream-sdk) to be installed,
and the AJA NTV2 drivers must be compiled with the `AJA_RDMA` flag enabled.

To enable NVMM RDMA support in this plugin, the `GST_CUDA` and `GST_DEEPSTREAM`
environment variables must be set to the CUDA and DeepStream install paths
before running `./autogen.sh` in the build steps above. For example:

    export GST_CUDA=/usr/local/cuda-11.1/targets/sbsa-linux
    export GST_DEEPSTREAM=/opt/nvidia/deepstream/deepstream-5.1

When compiled with NVMM RDMA support, the `nvmm` boolean option is added to the
`ajavideosrc` plugin in order to enable NVMM RDMA buffer outputs. Testing NVMM
RDMA output can be done using the following:

    gst-launch-1.0 ajavideosrc mode=4Kp60-rgba nvmm=true ! nv3dsink

Note that the NVIDIA GStreamer plugins provided by DeepStream do not support
packed YUV formats, and so the use of an RGBA mode is required.

## License

Copyright 2016 AJA Video Systems Inc. All rights reserved.

This program is free software; you may redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; version 2 of the License.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
