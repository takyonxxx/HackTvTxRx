# HackTvRxTx QT 6.6 Library Project

This project is a clone of https://github.com/fsphil/hacktv with modifications for building on Windows. 

Enabled Rx WFM && FFt spectrum

Still continue to develop.

<b>Please compile with Desktop Qt 6.6x MinGW 64-bit</b>

HackTvLib creates library (HackTvLib.dll)
HackTvGui shows how you will use library

![HackTvGui Screenshot](hacktvgui_screen.png)

## Installation Instructions

### 1. Install MSYS2

Download and install MSYS2 from: https://www.msys2.org/

### 2. Install Dependencies

Open MSYS2 and run the following commands:

```bash
pacman -Syu
pacman -S git mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-make mingw-w64-ucrt-x86_64-libusb mingw-w64-ucrt-x86_64-fftw mingw-w64-clang-x86_64-toolchain mingw-w64-ucrt-x86_64-git mingw-w64-x86_64-ffmpeg mingw-w64-x86_64-soapysdr mingw-w64-x86_64-fltk mingw-w64-x86_64-opus mingw-w64-x86_64-fdk-aac mingw-w64-x86_64-portaudio
```

### 3. Set up PKG_CONFIG_PATH

```bash
export PKG_CONFIG_PATH=/mingw64/lib/pkgconfig:$PKG_CONFIG_PATH
```

### 4. Build HackRF

```bash
git clone https://github.com/mossmann/hackrf.git
cd hackrf/host
mkdir build && cd build
cmake -G "MinGW Makefiles" -DCMAKE_INSTALL_PREFIX=/mingw64 ..
```

If you encounter an error, try:

```bash
cmake -G "MinGW Makefiles" -DCMAKE_INSTALL_PREFIX=/mingw64 \
-DLIBUSB_INCLUDE_DIR=/ucrt64/include/libusb-1.0 \
-DLIBUSB_LIBRARIES=/ucrt64/lib/libusb-1.0.dll.a \
-DFFTW_INCLUDES=/ucrt64/include \
-DFFTW_LIBRARIES=/ucrt64/lib/libfftw3.dll.a \
..
```

Then build and install:

```bash
mingw32-make
mingw32-make install
```

### 5. Build osmo-fl2k

```bash
git clone https://gitea.osmocom.org/sdr/osmo-fl2k.git
cd osmo-fl2k
mkdir build && cd build
cmake -G "MinGW Makefiles" -DCMAKE_INSTALL_PREFIX=/mingw64 ..
```

**Note:** Go to `C:\msys64\osmo-fl2k\src\fl2k_fm.c` and comment out the while loop between lines 455 and 484.

### 6. Additional Steps

You may need to install additional dependencies or make further modifications depending on your specific requirements.

## Troubleshooting

If you encounter any issues during the installation or build process, please check the error messages and consult the project's documentation or seek help in the project's issue tracker.
 
