# HackTvWindows QT 6.6 Project

This project is a clone of https://github.com/fsphil/hacktv with modifications for building on Windows.

 <b>Installation Instructions</b>
 
 INSTALL MSYS: https://www.msys2.org/
 
 pacman -Syu
 
 pacman -S cmake
 
 pacman -S mingw-w64-clang-x86_64-toolchain
 
 git clone https://github.com/mossmann/hackrf.git
 
 git clone https://gitea.osmocom.org/sdr/osmo-fl2k.git
 
 mkdir build
 
 cd build
 
 cmake -G "MSYS Makefiles" -DCMAKE_INSTALL_PREFIX=/ucrt64 ..
 
 make
 
 make install
 
 pacman -S mingw-w64-x86_64-ffmpeg
 
 pacman -S mingw-w64-ucrt-x86_64-libusb
 
 pacman -S mingw-w64-x86_64-libusb
 
 pacman -S mingw-w64-x86_64-soapysdr
 
 pacman -S mingw-w64-x86_64-fltk
 
 pacman -S mingw-w64-x86_64-opus
 
 pacman -S mingw-w64-x86_64-fdk-aac
 
