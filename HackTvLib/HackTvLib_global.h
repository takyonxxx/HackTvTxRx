#ifndef HACKTVLIB_GLOBAL_H
#define HACKTVLIB_GLOBAL_H

#include <QtCore/qglobal.h>

#if defined(HACKTVLIB_LIBRARY)
#  define HACKTVLIB_EXPORT Q_DECL_EXPORT
#else
#  define HACKTVLIB_EXPORT Q_DECL_IMPORT
#endif

#endif // HACKTVLIB_GLOBAL_H
