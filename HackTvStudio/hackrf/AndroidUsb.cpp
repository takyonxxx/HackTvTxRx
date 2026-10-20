#include "AndroidUsb.h"

#ifdef __ANDROID__

#include <QJniObject>
#include <QCoreApplication>
#include <QtCore/qnativeinterface.h>

namespace {
constexpr const char* kClass = "com/hacktvstudio/HackRfUsb";

// Android context (the QtActivity). QNativeInterface returns the Activity,
// which is a Context subclass, so it is accepted where a Context is wanted.
QJniObject context()
{
    return QJniObject(QNativeInterface::QAndroidApplication::context());
}
} // namespace

namespace AndroidUsb {

bool isPresent()
{
    QJniObject ctx = context();
    if (!ctx.isValid()) return false;
    return QJniObject::callStaticMethod<jboolean>(
        kClass, "isPresent", "(Landroid/content/Context;)Z", ctx.object());
}

QString deviceName()
{
    QJniObject ctx = context();
    if (!ctx.isValid()) return QString();
    QJniObject name = QJniObject::callStaticObjectMethod(
        kClass, "deviceName", "(Landroid/content/Context;)Ljava/lang/String;", ctx.object());
    return name.isValid() ? name.toString() : QString();
}

int requestOpen()
{
    QJniObject ctx = context();
    if (!ctx.isValid()) return -1;
    return QJniObject::callStaticMethod<jint>(
        kClass, "requestOpen", "(Landroid/content/Context;)I", ctx.object());
}

int fileDescriptor()
{
    QJniObject ctx = context();
    if (!ctx.isValid()) return -1;
    return QJniObject::callStaticMethod<jint>(
        kClass, "pollFd", "(Landroid/content/Context;)I", ctx.object());
}

void close()
{
    QJniObject::callStaticMethod<void>(kClass, "close", "()V");
}

} // namespace AndroidUsb

#endif // __ANDROID__
