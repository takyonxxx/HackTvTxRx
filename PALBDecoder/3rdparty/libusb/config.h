/* config.h - manual libusb configuration for the three platforms PALBDecoder
 * builds on. Replaces the autotools-generated config.h; modelled on libusb's
 * msvc/config.h and on what configure.ac detects on macOS / glibc Linux. */

#ifndef LIBUSB_CONFIG_H
#define LIBUSB_CONFIG_H

/* Static build: no symbol visibility attribute needed. */
#define DEFAULT_VISIBILITY /**/

/* Enable message logging (needed by usbi_dbg / usbi_warn / usbi_err). */
#define ENABLE_LOGGING 1

/* Attribute for checking printf-style format strings. */
#define PRINTF_FORMAT(a, b) __attribute__((__format__(__printf__, a, b)))

#if defined(_WIN32)
/* ---------------- Windows (MinGW, WinUSB / libusbK / UsbDk backend) -------- */
#define PLATFORM_WINDOWS 1

#elif defined(__APPLE__)
/* ---------------- macOS (IOKit backend) ----------------------------------- */
#define PLATFORM_POSIX 1
#define HAVE_SYS_TIME_H 1
#define HAVE_NFDS_T 1
#define HAVE_CLOCK_GETTIME 1            /* macOS 10.12+ */
#define HAVE_PTHREAD_THREADID_NP 1
#define HAVE_IOKIT_USB_IOUSBHOSTFAMILYDEFINITIONS_H 1   /* SDK 10.11+ */

#elif defined(__linux__)
/* ---------------- Linux (usbfs backend, netlink hotplug, no libudev) ------ */
#define PLATFORM_POSIX 1
#define HAVE_SYS_TIME_H 1
#define HAVE_NFDS_T 1
#define HAVE_CLOCK_GETTIME 1
#define HAVE_PTHREAD_CONDATTR_SETCLOCK 1
#define HAVE_PTHREAD_SETNAME_NP 1
#define HAVE_ASM_TYPES_H 1
#define HAVE_EVENTFD 1
#define HAVE_TIMERFD 1
#define HAVE_PIPE2 1
/* HAVE_LIBUDEV intentionally undefined: linux_netlink.c is used instead so
 * that no system library beyond libc/pthread is required. */

#else
#error "libusb: unsupported platform for this vendored build"
#endif

#endif /* LIBUSB_CONFIG_H */
