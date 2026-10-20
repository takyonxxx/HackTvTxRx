/****************************************************************************
** Meta object code from reading C++ file 'RadioRxPanel.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../ui/RadioRxPanel.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'RadioRxPanel.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN11RadioRxSinkE_t {};
} // unnamed namespace

template <> constexpr inline auto RadioRxSink::qt_create_metaobjectdata<qt_meta_tag_ZN11RadioRxSinkE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "RadioRxSink",
        "stereoChanged",
        "",
        "stereo"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'stereoChanged'
        QtMocHelpers::SignalData<void(bool)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 3 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<RadioRxSink, qt_meta_tag_ZN11RadioRxSinkE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject RadioRxSink::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11RadioRxSinkE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11RadioRxSinkE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN11RadioRxSinkE_t>.metaTypes,
    nullptr
} };

void RadioRxSink::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<RadioRxSink *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->stereoChanged((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (RadioRxSink::*)(bool )>(_a, &RadioRxSink::stereoChanged, 0))
            return;
    }
}

const QMetaObject *RadioRxSink::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *RadioRxSink::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11RadioRxSinkE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "IRxSink"))
        return static_cast< IRxSink*>(this);
    return QObject::qt_metacast(_clname);
}

int RadioRxSink::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 1)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 1;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 1)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 1;
    }
    return _id;
}

// SIGNAL 0
void RadioRxSink::stereoChanged(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}
namespace {
struct qt_meta_tag_ZN12RadioRxPanelE_t {};
} // unnamed namespace

template <> constexpr inline auto RadioRxPanel::qt_create_metaobjectdata<qt_meta_tag_ZN12RadioRxPanelE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "RadioRxPanel",
        "updateStatus",
        ""
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'updateStatus'
        QtMocHelpers::SlotData<void()>(1, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<RadioRxPanel, qt_meta_tag_ZN12RadioRxPanelE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject RadioRxPanel::staticMetaObject = { {
    QMetaObject::SuperData::link<ModePanel::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12RadioRxPanelE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12RadioRxPanelE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN12RadioRxPanelE_t>.metaTypes,
    nullptr
} };

void RadioRxPanel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<RadioRxPanel *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->updateStatus(); break;
        default: ;
        }
    }
    (void)_a;
}

const QMetaObject *RadioRxPanel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *RadioRxPanel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12RadioRxPanelE_t>.strings))
        return static_cast<void*>(this);
    return ModePanel::qt_metacast(_clname);
}

int RadioRxPanel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = ModePanel::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 1)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 1;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 1)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 1;
    }
    return _id;
}
QT_WARNING_POP
