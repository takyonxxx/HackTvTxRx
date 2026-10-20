/****************************************************************************
** Meta object code from reading C++ file 'TvRxPanel.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../ui/TvRxPanel.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'TvRxPanel.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN12TvRxPipelineE_t {};
} // unnamed namespace

template <> constexpr inline auto TvRxPipeline::qt_create_metaobjectdata<qt_meta_tag_ZN12TvRxPipelineE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "TvRxPipeline"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<TvRxPipeline, qt_meta_tag_ZN12TvRxPipelineE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject TvRxPipeline::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12TvRxPipelineE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12TvRxPipelineE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN12TvRxPipelineE_t>.metaTypes,
    nullptr
} };

void TvRxPipeline::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<TvRxPipeline *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *TvRxPipeline::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *TvRxPipeline::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN12TvRxPipelineE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "IRxSink"))
        return static_cast< IRxSink*>(this);
    return QObject::qt_metacast(_clname);
}

int TvRxPipeline::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    return _id;
}
namespace {
struct qt_meta_tag_ZN9TvRxPanelE_t {};
} // unnamed namespace

template <> constexpr inline auto TvRxPanel::qt_create_metaobjectdata<qt_meta_tag_ZN9TvRxPanelE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "TvRxPanel",
        "onFrameReady",
        "",
        "QImage",
        "frame",
        "onSyncStatsUpdated",
        "syncRate",
        "peakLevel",
        "minLevel",
        "onAudioReady",
        "std::vector<float>",
        "samples",
        "updateStatus",
        "toggleFullscreen",
        "toggleRecording",
        "openRecordFolder"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'onFrameReady'
        QtMocHelpers::SlotData<void(const QImage &)>(1, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 3, 4 },
        }}),
        // Slot 'onSyncStatsUpdated'
        QtMocHelpers::SlotData<void(float, float, float)>(5, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Float, 6 }, { QMetaType::Float, 7 }, { QMetaType::Float, 8 },
        }}),
        // Slot 'onAudioReady'
        QtMocHelpers::SlotData<void(const std::vector<float> &)>(9, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 10, 11 },
        }}),
        // Slot 'updateStatus'
        QtMocHelpers::SlotData<void()>(12, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'toggleFullscreen'
        QtMocHelpers::SlotData<void()>(13, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'toggleRecording'
        QtMocHelpers::SlotData<void()>(14, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'openRecordFolder'
        QtMocHelpers::SlotData<void()>(15, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<TvRxPanel, qt_meta_tag_ZN9TvRxPanelE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject TvRxPanel::staticMetaObject = { {
    QMetaObject::SuperData::link<ModePanel::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9TvRxPanelE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9TvRxPanelE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN9TvRxPanelE_t>.metaTypes,
    nullptr
} };

void TvRxPanel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<TvRxPanel *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->onFrameReady((*reinterpret_cast<std::add_pointer_t<QImage>>(_a[1]))); break;
        case 1: _t->onSyncStatsUpdated((*reinterpret_cast<std::add_pointer_t<float>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<float>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<float>>(_a[3]))); break;
        case 2: _t->onAudioReady((*reinterpret_cast<std::add_pointer_t<std::vector<float>>>(_a[1]))); break;
        case 3: _t->updateStatus(); break;
        case 4: _t->toggleFullscreen(); break;
        case 5: _t->toggleRecording(); break;
        case 6: _t->openRecordFolder(); break;
        default: ;
        }
    }
}

const QMetaObject *TvRxPanel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *TvRxPanel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN9TvRxPanelE_t>.strings))
        return static_cast<void*>(this);
    return ModePanel::qt_metacast(_clname);
}

int TvRxPanel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = ModePanel::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 7;
    }
    return _id;
}
QT_WARNING_POP
