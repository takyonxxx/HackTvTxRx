/****************************************************************************
** Meta object code from reading C++ file 'freqctrl.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../widgets/freqctrl.h"
#include <QtGui/qtextcursor.h>
#include <QtGui/qscreen.h>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'freqctrl.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.11.1. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN9CFreqCtrlE_t {};
} // unnamed namespace

template <> constexpr inline auto CFreqCtrl::qt_create_metaobjectdata<qt_meta_tag_ZN9CFreqCtrlE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "CFreqCtrl",
        "newFrequency",
        "",
        "freq",
        "setFrequency",
        "setFrequencyFocus"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'newFrequency'
        QtMocHelpers::SignalData<void(qint64)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::LongLong, 3 },
        }}),
        // Slot 'setFrequency'
        QtMocHelpers::SlotData<void(qint64)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::LongLong, 3 },
        }}),
        // Slot 'setFrequencyFocus'
        QtMocHelpers::SlotData<void()>(5, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<CFreqCtrl, qt_meta_tag_ZN9CFreqCtrlE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject CFreqCtrl::staticMetaObject = { {
    QMetaObject::SuperData::link<QFrame::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9CFreqCtrlE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9CFreqCtrlE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN9CFreqCtrlE_t>.metaTypes,
    nullptr
} };

void CFreqCtrl::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<CFreqCtrl *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->newFrequency((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1]))); break;
        case 1: _t->setFrequency((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1]))); break;
        case 2: _t->setFrequencyFocus(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (CFreqCtrl::*)(qint64 )>(_a, &CFreqCtrl::newFrequency, 0))
            return;
    }
}

const QMetaObject *CFreqCtrl::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *CFreqCtrl::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9CFreqCtrlE_t>.strings))
        return static_cast<void*>(this);
    return QFrame::qt_metacast(_clname);
}

int CFreqCtrl::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QFrame::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 3)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 3)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 3;
    }
    return _id;
}

// SIGNAL 0
void CFreqCtrl::newFrequency(qint64 _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}
QT_WARNING_POP
