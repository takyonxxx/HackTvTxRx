#ifndef ANDROIDUSB_H
#define ANDROIDUSB_H

// Android USB-host bridge for the HackRF. Thin JNI wrapper over the Java
// com.hacktvstudio.HackRfUsb helper. Only compiled on Android; on every other
// platform libusb enumerates the device itself.
#ifdef __ANDROID__

#include <QString>

namespace AndroidUsb {

// True if a HackRF is physically attached to the USB-host (OTG) port.
bool isPresent();

// Short identifier of the attached HackRF (device node + id), or empty.
QString deviceName();

// Ask for USB permission and open the device.
//  1  = open, fd ready
//  0  = permission dialog shown, call fileDescriptor() after the user accepts
// -1  = no device
int requestOpen();

// Current fd once granted: >=0 fd, -2 still waiting, -1 denied/failed.
int fileDescriptor();

// Release the Java UsbDeviceConnection (after hackrf_close).
void close();

} // namespace AndroidUsb

#endif // __ANDROID__
#endif // ANDROIDUSB_H
