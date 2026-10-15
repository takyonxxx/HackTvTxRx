# 3rdparty (vendored sources)

These sources are compiled directly into HackTvStudio on **all three platforms**
(Windows, macOS, Linux) so that no package manager (MSYS2, MacPorts, Homebrew,
apt) and no external `libusb` / `libhackrf` library is needed. Only OS-provided
system libraries / frameworks are linked (see `HackTvStudio.pro`).

| Directory    | Upstream                                      | Version          | License  |
|--------------|-----------------------------------------------|------------------|----------|
| `libusb/`    | https://github.com/libusb/libusb              | v1.0.27          | LGPL-2.1 |
| `libhackrf/` | https://github.com/greatscottgadgets/hackrf   | master @ 468c5d9 | BSD-3 (see header of `src/hackrf.c`) |
| `pthread-msvc/` | (ours) | — | minimal pthread shim for MSVC, used by `hackrf.c` |
| `msvc-compat/` | (ours) | — | `dirent.h` + `gmtime_r` shim for MSVC, used by hacktv's `teletext.c` |

The analogue TV encoder itself (`../hacktv/`, GPL-3) is fsphil's hacktv with `av_ffmpeg.*`
and the `rf_*` backends removed: video comes from `tv/QtAvSource` (Qt Multimedia) and RF
goes through `core/TxEngine`. Local patches: `wss.c` (C17-compatible table terminator,
`strcasecmp` for MSVC).

## libusb/

Unmodified upstream files: core, io, descriptor, hotplug, strerror, sync, plus the
per-OS backends in `os/`:

| Platform | Backend sources                                            | System libs linked                          |
|----------|------------------------------------------------------------|---------------------------------------------|
| Windows  | `events_windows.c threads_windows.c windows_common.c windows_usbdk.c windows_winusb.c` | `ole32 setupapi uuid` |
| macOS    | `events_posix.c threads_posix.c darwin_usb.c`              | `IOKit CoreFoundation Security` frameworks  |
| Linux    | `events_posix.c threads_posix.c linux_usbfs.c linux_netlink.c` | `pthread` (netlink hotplug, **no libudev**) |

`config.h` is ours: a hand-written replacement for the autotools-generated one,
with one `#if` branch per platform.

## libhackrf/

Unmodified `src/hackrf.c` / `src/hackrf.h`.
`include/libhackrf/hackrf.h` is a one-line forwarding header so that the usual
`#include <libhackrf/hackrf.h>` resolves to the vendored copy.

## Licensing note

libusb is LGPL-2.1. When distributing a HackTvStudio binary, provide the libusb
sources (this directory) and allow relinking, as the LGPL requires.
