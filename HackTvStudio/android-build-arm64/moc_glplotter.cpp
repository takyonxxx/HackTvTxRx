/****************************************************************************
** Meta object code from reading C++ file 'glplotter.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../widgets/glplotter.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'glplotter.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN8CPlotterE_t {};
} // unnamed namespace

template <> constexpr inline auto CPlotter::qt_create_metaobjectdata<qt_meta_tag_ZN8CPlotterE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "CPlotter",
        "newCenterFreq",
        "",
        "f",
        "newDemodFreq",
        "freq",
        "delta",
        "newLowCutFreq",
        "newHighCutFreq",
        "newFilterFreq",
        "low",
        "high",
        "pandapterRangeChanged",
        "min",
        "max",
        "newZoomLevel",
        "level",
        "wheelFreqChange",
        "direction",
        "resetHorizontalZoom",
        "moveToCenterFreq",
        "moveToDemodFreq",
        "zoomOnXAxis",
        "setFftPlotColor",
        "QColor",
        "color",
        "setFftFill",
        "enabled",
        "setPeakHold",
        "setFftRange",
        "setPandapterRange",
        "setWaterfallRange",
        "setPeakDetection",
        "c",
        "updateOverlay",
        "setPercent2DScreen",
        "percent"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'newCenterFreq'
        QtMocHelpers::SignalData<void(qint64)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::LongLong, 3 },
        }}),
        // Signal 'newDemodFreq'
        QtMocHelpers::SignalData<void(qint64, qint64)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::LongLong, 5 }, { QMetaType::LongLong, 6 },
        }}),
        // Signal 'newLowCutFreq'
        QtMocHelpers::SignalData<void(int)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 },
        }}),
        // Signal 'newHighCutFreq'
        QtMocHelpers::SignalData<void(int)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 },
        }}),
        // Signal 'newFilterFreq'
        QtMocHelpers::SignalData<void(int, int)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 10 }, { QMetaType::Int, 11 },
        }}),
        // Signal 'pandapterRangeChanged'
        QtMocHelpers::SignalData<void(float, float)>(12, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Float, 13 }, { QMetaType::Float, 14 },
        }}),
        // Signal 'newZoomLevel'
        QtMocHelpers::SignalData<void(float)>(15, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Float, 16 },
        }}),
        // Signal 'wheelFreqChange'
        QtMocHelpers::SignalData<void(int)>(17, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 18 },
        }}),
        // Slot 'resetHorizontalZoom'
        QtMocHelpers::SlotData<void()>(19, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'moveToCenterFreq'
        QtMocHelpers::SlotData<void()>(20, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'moveToDemodFreq'
        QtMocHelpers::SlotData<void()>(21, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'zoomOnXAxis'
        QtMocHelpers::SlotData<void(float)>(22, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Float, 16 },
        }}),
        // Slot 'setFftPlotColor'
        QtMocHelpers::SlotData<void(const QColor)>(23, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 24, 25 },
        }}),
        // Slot 'setFftFill'
        QtMocHelpers::SlotData<void(bool)>(26, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 27 },
        }}),
        // Slot 'setPeakHold'
        QtMocHelpers::SlotData<void(bool)>(28, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 27 },
        }}),
        // Slot 'setFftRange'
        QtMocHelpers::SlotData<void(float, float)>(29, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Float, 13 }, { QMetaType::Float, 14 },
        }}),
        // Slot 'setPandapterRange'
        QtMocHelpers::SlotData<void(float, float)>(30, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Float, 13 }, { QMetaType::Float, 14 },
        }}),
        // Slot 'setWaterfallRange'
        QtMocHelpers::SlotData<void(float, float)>(31, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Float, 13 }, { QMetaType::Float, 14 },
        }}),
        // Slot 'setPeakDetection'
        QtMocHelpers::SlotData<void(bool, float)>(32, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 27 }, { QMetaType::Float, 33 },
        }}),
        // Slot 'updateOverlay'
        QtMocHelpers::SlotData<void()>(34, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'setPercent2DScreen'
        QtMocHelpers::SlotData<void(int)>(35, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 36 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<CPlotter, qt_meta_tag_ZN8CPlotterE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject CPlotter::staticMetaObject = { {
    QMetaObject::SuperData::link<QOpenGLWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8CPlotterE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8CPlotterE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN8CPlotterE_t>.metaTypes,
    nullptr
} };

void CPlotter::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<CPlotter *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->newCenterFreq((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1]))); break;
        case 1: _t->newDemodFreq((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2]))); break;
        case 2: _t->newLowCutFreq((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 3: _t->newHighCutFreq((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 4: _t->newFilterFreq((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 5: _t->pandapterRangeChanged((*reinterpret_cast<std::add_pointer_t<float>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<float>>(_a[2]))); break;
        case 6: _t->newZoomLevel((*reinterpret_cast<std::add_pointer_t<float>>(_a[1]))); break;
        case 7: _t->wheelFreqChange((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 8: _t->resetHorizontalZoom(); break;
        case 9: _t->moveToCenterFreq(); break;
        case 10: _t->moveToDemodFreq(); break;
        case 11: _t->zoomOnXAxis((*reinterpret_cast<std::add_pointer_t<float>>(_a[1]))); break;
        case 12: _t->setFftPlotColor((*reinterpret_cast<std::add_pointer_t<QColor>>(_a[1]))); break;
        case 13: _t->setFftFill((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 14: _t->setPeakHold((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 15: _t->setFftRange((*reinterpret_cast<std::add_pointer_t<float>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<float>>(_a[2]))); break;
        case 16: _t->setPandapterRange((*reinterpret_cast<std::add_pointer_t<float>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<float>>(_a[2]))); break;
        case 17: _t->setWaterfallRange((*reinterpret_cast<std::add_pointer_t<float>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<float>>(_a[2]))); break;
        case 18: _t->setPeakDetection((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<float>>(_a[2]))); break;
        case 19: _t->updateOverlay(); break;
        case 20: _t->setPercent2DScreen((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (CPlotter::*)(qint64 )>(_a, &CPlotter::newCenterFreq, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (CPlotter::*)(qint64 , qint64 )>(_a, &CPlotter::newDemodFreq, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (CPlotter::*)(int )>(_a, &CPlotter::newLowCutFreq, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (CPlotter::*)(int )>(_a, &CPlotter::newHighCutFreq, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (CPlotter::*)(int , int )>(_a, &CPlotter::newFilterFreq, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (CPlotter::*)(float , float )>(_a, &CPlotter::pandapterRangeChanged, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (CPlotter::*)(float )>(_a, &CPlotter::newZoomLevel, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (CPlotter::*)(int )>(_a, &CPlotter::wheelFreqChange, 7))
            return;
    }
}

const QMetaObject *CPlotter::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *CPlotter::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN8CPlotterE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "QOpenGLFunctions"))
        return static_cast< QOpenGLFunctions*>(this);
    return QOpenGLWidget::qt_metacast(_clname);
}

int CPlotter::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QOpenGLWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 21)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 21;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 21)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 21;
    }
    return _id;
}

// SIGNAL 0
void CPlotter::newCenterFreq(qint64 _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void CPlotter::newDemodFreq(qint64 _t1, qint64 _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1, _t2);
}

// SIGNAL 2
void CPlotter::newLowCutFreq(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void CPlotter::newHighCutFreq(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}

// SIGNAL 4
void CPlotter::newFilterFreq(int _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1, _t2);
}

// SIGNAL 5
void CPlotter::pandapterRangeChanged(float _t1, float _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1, _t2);
}

// SIGNAL 6
void CPlotter::newZoomLevel(float _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 6, nullptr, _t1);
}

// SIGNAL 7
void CPlotter::wheelFreqChange(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 7, nullptr, _t1);
}
QT_WARNING_POP
