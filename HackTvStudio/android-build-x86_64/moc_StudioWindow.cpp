/****************************************************************************
** Meta object code from reading C++ file 'StudioWindow.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../ui/StudioWindow.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>
#include <QtCore/QList>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'StudioWindow.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN12StudioWindowE_t {};
} // unnamed namespace

template <> constexpr inline auto StudioWindow::qt_create_metaobjectdata<qt_meta_tag_ZN12StudioWindowE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "StudioWindow",
        "onFrequencyEdited",
        "",
        "hz",
        "onPlotterDemodFreq",
        "freq",
        "delta",
        "onPlotterWheel",
        "direction",
        "onSampleRateSelected",
        "index",
        "onStartStop",
        "onTabChanged",
        "onSpectrum",
        "QList<float>",
        "bins",
        "levelDbfs",
        "onDeviceState",
        "DeviceController::State",
        "state",
        "onDeviceError",
        "message",
        "onRecordIq",
        "onRecordingFinished",
        "path",
        "bytes",
        "updateStatusBar"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'onFrequencyEdited'
        QtMocHelpers::SlotData<void(qint64)>(1, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::LongLong, 3 },
        }}),
        // Slot 'onPlotterDemodFreq'
        QtMocHelpers::SlotData<void(qint64, qint64)>(4, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::LongLong, 5 }, { QMetaType::LongLong, 6 },
        }}),
        // Slot 'onPlotterWheel'
        QtMocHelpers::SlotData<void(int)>(7, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 8 },
        }}),
        // Slot 'onSampleRateSelected'
        QtMocHelpers::SlotData<void(int)>(9, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 10 },
        }}),
        // Slot 'onStartStop'
        QtMocHelpers::SlotData<void()>(11, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onTabChanged'
        QtMocHelpers::SlotData<void(int)>(12, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 10 },
        }}),
        // Slot 'onSpectrum'
        QtMocHelpers::SlotData<void(const QVector<float> &, float)>(13, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 14, 15 }, { QMetaType::Float, 16 },
        }}),
        // Slot 'onDeviceState'
        QtMocHelpers::SlotData<void(DeviceController::State)>(17, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 18, 19 },
        }}),
        // Slot 'onDeviceError'
        QtMocHelpers::SlotData<void(const QString &)>(20, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 21 },
        }}),
        // Slot 'onRecordIq'
        QtMocHelpers::SlotData<void()>(22, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onRecordingFinished'
        QtMocHelpers::SlotData<void(const QString &, qint64)>(23, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 24 }, { QMetaType::LongLong, 25 },
        }}),
        // Slot 'updateStatusBar'
        QtMocHelpers::SlotData<void()>(26, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<StudioWindow, qt_meta_tag_ZN12StudioWindowE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject StudioWindow::staticMetaObject = { {
    QMetaObject::SuperData::link<QMainWindow::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12StudioWindowE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12StudioWindowE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN12StudioWindowE_t>.metaTypes,
    nullptr
} };

void StudioWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<StudioWindow *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->onFrequencyEdited((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1]))); break;
        case 1: _t->onPlotterDemodFreq((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2]))); break;
        case 2: _t->onPlotterWheel((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 3: _t->onSampleRateSelected((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 4: _t->onStartStop(); break;
        case 5: _t->onTabChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 6: _t->onSpectrum((*reinterpret_cast<std::add_pointer_t<QList<float>>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<float>>(_a[2]))); break;
        case 7: _t->onDeviceState((*reinterpret_cast<std::add_pointer_t<DeviceController::State>>(_a[1]))); break;
        case 8: _t->onDeviceError((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 9: _t->onRecordIq(); break;
        case 10: _t->onRecordingFinished((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2]))); break;
        case 11: _t->updateStatusBar(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 6:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<float> >(); break;
            }
            break;
        }
    }
}

const QMetaObject *StudioWindow::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *StudioWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12StudioWindowE_t>.strings))
        return static_cast<void*>(this);
    return QMainWindow::qt_metacast(_clname);
}

int StudioWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 12)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 12;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 12)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 12;
    }
    return _id;
}
QT_WARNING_POP
