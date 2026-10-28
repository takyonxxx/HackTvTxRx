/****************************************************************************
** Meta object code from reading C++ file 'TvRxPanel.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.5.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../ui/TvRxPanel.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>

#if __has_include(<QtCore/qtmochelpers.h>)
#include <QtCore/qtmochelpers.h>
#else
QT_BEGIN_MOC_NAMESPACE
#endif


#include <memory>

#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'TvRxPanel.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.5.3. It"
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

#ifdef QT_MOC_HAS_STRINGDATA
struct qt_meta_stringdata_CLASSTvRxPipelineENDCLASS_t {};
static constexpr auto qt_meta_stringdata_CLASSTvRxPipelineENDCLASS = QtMocHelpers::stringData(
    "TvRxPipeline"
);
#else  // !QT_MOC_HAS_STRING_DATA
struct qt_meta_stringdata_CLASSTvRxPipelineENDCLASS_t {
    uint offsetsAndSizes[2];
    char stringdata0[13];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_CLASSTvRxPipelineENDCLASS_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_CLASSTvRxPipelineENDCLASS_t qt_meta_stringdata_CLASSTvRxPipelineENDCLASS = {
    {
        QT_MOC_LITERAL(0, 12)   // "TvRxPipeline"
    },
    "TvRxPipeline"
};
#undef QT_MOC_LITERAL
#endif // !QT_MOC_HAS_STRING_DATA
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_CLASSTvRxPipelineENDCLASS[] = {

 // content:
      11,       // revision
       0,       // classname
       0,    0, // classinfo
       0,    0, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

       0        // eod
};

Q_CONSTINIT const QMetaObject TvRxPipeline::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_CLASSTvRxPipelineENDCLASS.offsetsAndSizes,
    qt_meta_data_CLASSTvRxPipelineENDCLASS,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_CLASSTvRxPipelineENDCLASS_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<TvRxPipeline, std::true_type>
    >,
    nullptr
} };

void TvRxPipeline::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

const QMetaObject *TvRxPipeline::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *TvRxPipeline::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_CLASSTvRxPipelineENDCLASS.stringdata0))
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

#ifdef QT_MOC_HAS_STRINGDATA
struct qt_meta_stringdata_CLASSTvRxPanelENDCLASS_t {};
static constexpr auto qt_meta_stringdata_CLASSTvRxPanelENDCLASS = QtMocHelpers::stringData(
    "TvRxPanel",
    "onFrameReady",
    "",
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
);
#else  // !QT_MOC_HAS_STRING_DATA
struct qt_meta_stringdata_CLASSTvRxPanelENDCLASS_t {
    uint offsetsAndSizes[30];
    char stringdata0[10];
    char stringdata1[13];
    char stringdata2[1];
    char stringdata3[6];
    char stringdata4[19];
    char stringdata5[9];
    char stringdata6[10];
    char stringdata7[9];
    char stringdata8[13];
    char stringdata9[19];
    char stringdata10[8];
    char stringdata11[13];
    char stringdata12[17];
    char stringdata13[16];
    char stringdata14[17];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_CLASSTvRxPanelENDCLASS_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_CLASSTvRxPanelENDCLASS_t qt_meta_stringdata_CLASSTvRxPanelENDCLASS = {
    {
        QT_MOC_LITERAL(0, 9),  // "TvRxPanel"
        QT_MOC_LITERAL(10, 12),  // "onFrameReady"
        QT_MOC_LITERAL(23, 0),  // ""
        QT_MOC_LITERAL(24, 5),  // "frame"
        QT_MOC_LITERAL(30, 18),  // "onSyncStatsUpdated"
        QT_MOC_LITERAL(49, 8),  // "syncRate"
        QT_MOC_LITERAL(58, 9),  // "peakLevel"
        QT_MOC_LITERAL(68, 8),  // "minLevel"
        QT_MOC_LITERAL(77, 12),  // "onAudioReady"
        QT_MOC_LITERAL(90, 18),  // "std::vector<float>"
        QT_MOC_LITERAL(109, 7),  // "samples"
        QT_MOC_LITERAL(117, 12),  // "updateStatus"
        QT_MOC_LITERAL(130, 16),  // "toggleFullscreen"
        QT_MOC_LITERAL(147, 15),  // "toggleRecording"
        QT_MOC_LITERAL(163, 16)   // "openRecordFolder"
    },
    "TvRxPanel",
    "onFrameReady",
    "",
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
#undef QT_MOC_LITERAL
#endif // !QT_MOC_HAS_STRING_DATA
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_CLASSTvRxPanelENDCLASS[] = {

 // content:
      11,       // revision
       0,       // classname
       0,    0, // classinfo
       7,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   56,    2, 0x08,    1 /* Private */,
       4,    3,   59,    2, 0x08,    3 /* Private */,
       8,    1,   66,    2, 0x08,    7 /* Private */,
      11,    0,   69,    2, 0x08,    9 /* Private */,
      12,    0,   70,    2, 0x08,   10 /* Private */,
      13,    0,   71,    2, 0x08,   11 /* Private */,
      14,    0,   72,    2, 0x08,   12 /* Private */,

 // slots: parameters
    QMetaType::Void, QMetaType::QImage,    3,
    QMetaType::Void, QMetaType::Float, QMetaType::Float, QMetaType::Float,    5,    6,    7,
    QMetaType::Void, 0x80000000 | 9,   10,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject TvRxPanel::staticMetaObject = { {
    QMetaObject::SuperData::link<ModePanel::staticMetaObject>(),
    qt_meta_stringdata_CLASSTvRxPanelENDCLASS.offsetsAndSizes,
    qt_meta_data_CLASSTvRxPanelENDCLASS,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_CLASSTvRxPanelENDCLASS_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<TvRxPanel, std::true_type>,
        // method 'onFrameReady'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QImage &, std::false_type>,
        // method 'onSyncStatsUpdated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<float, std::false_type>,
        QtPrivate::TypeAndForceComplete<float, std::false_type>,
        QtPrivate::TypeAndForceComplete<float, std::false_type>,
        // method 'onAudioReady'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const std::vector<float> &, std::false_type>,
        // method 'updateStatus'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'toggleFullscreen'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'toggleRecording'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'openRecordFolder'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void TvRxPanel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<TvRxPanel *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->onFrameReady((*reinterpret_cast< std::add_pointer_t<QImage>>(_a[1]))); break;
        case 1: _t->onSyncStatsUpdated((*reinterpret_cast< std::add_pointer_t<float>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<float>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<float>>(_a[3]))); break;
        case 2: _t->onAudioReady((*reinterpret_cast< std::add_pointer_t<std::vector<float>>>(_a[1]))); break;
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
    if (!strcmp(_clname, qt_meta_stringdata_CLASSTvRxPanelENDCLASS.stringdata0))
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
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 7;
    }
    return _id;
}
QT_WARNING_POP
