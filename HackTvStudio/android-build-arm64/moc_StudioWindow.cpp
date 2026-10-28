/****************************************************************************
** Meta object code from reading C++ file 'StudioWindow.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.5.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../ui/StudioWindow.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>
#include <QtCore/QList>

#if __has_include(<QtCore/qtmochelpers.h>)
#include <QtCore/qtmochelpers.h>
#else
QT_BEGIN_MOC_NAMESPACE
#endif


#include <memory>

#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'StudioWindow.h' doesn't include <QObject>."
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
struct qt_meta_stringdata_CLASSStudioWindowENDCLASS_t {};
static constexpr auto qt_meta_stringdata_CLASSStudioWindowENDCLASS = QtMocHelpers::stringData(
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
);
#else  // !QT_MOC_HAS_STRING_DATA
struct qt_meta_stringdata_CLASSStudioWindowENDCLASS_t {
    uint offsetsAndSizes[54];
    char stringdata0[13];
    char stringdata1[18];
    char stringdata2[1];
    char stringdata3[3];
    char stringdata4[19];
    char stringdata5[5];
    char stringdata6[6];
    char stringdata7[15];
    char stringdata8[10];
    char stringdata9[21];
    char stringdata10[6];
    char stringdata11[12];
    char stringdata12[13];
    char stringdata13[11];
    char stringdata14[13];
    char stringdata15[5];
    char stringdata16[10];
    char stringdata17[14];
    char stringdata18[24];
    char stringdata19[6];
    char stringdata20[14];
    char stringdata21[8];
    char stringdata22[11];
    char stringdata23[20];
    char stringdata24[5];
    char stringdata25[6];
    char stringdata26[16];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_CLASSStudioWindowENDCLASS_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_CLASSStudioWindowENDCLASS_t qt_meta_stringdata_CLASSStudioWindowENDCLASS = {
    {
        QT_MOC_LITERAL(0, 12),  // "StudioWindow"
        QT_MOC_LITERAL(13, 17),  // "onFrequencyEdited"
        QT_MOC_LITERAL(31, 0),  // ""
        QT_MOC_LITERAL(32, 2),  // "hz"
        QT_MOC_LITERAL(35, 18),  // "onPlotterDemodFreq"
        QT_MOC_LITERAL(54, 4),  // "freq"
        QT_MOC_LITERAL(59, 5),  // "delta"
        QT_MOC_LITERAL(65, 14),  // "onPlotterWheel"
        QT_MOC_LITERAL(80, 9),  // "direction"
        QT_MOC_LITERAL(90, 20),  // "onSampleRateSelected"
        QT_MOC_LITERAL(111, 5),  // "index"
        QT_MOC_LITERAL(117, 11),  // "onStartStop"
        QT_MOC_LITERAL(129, 12),  // "onTabChanged"
        QT_MOC_LITERAL(142, 10),  // "onSpectrum"
        QT_MOC_LITERAL(153, 12),  // "QList<float>"
        QT_MOC_LITERAL(166, 4),  // "bins"
        QT_MOC_LITERAL(171, 9),  // "levelDbfs"
        QT_MOC_LITERAL(181, 13),  // "onDeviceState"
        QT_MOC_LITERAL(195, 23),  // "DeviceController::State"
        QT_MOC_LITERAL(219, 5),  // "state"
        QT_MOC_LITERAL(225, 13),  // "onDeviceError"
        QT_MOC_LITERAL(239, 7),  // "message"
        QT_MOC_LITERAL(247, 10),  // "onRecordIq"
        QT_MOC_LITERAL(258, 19),  // "onRecordingFinished"
        QT_MOC_LITERAL(278, 4),  // "path"
        QT_MOC_LITERAL(283, 5),  // "bytes"
        QT_MOC_LITERAL(289, 15)   // "updateStatusBar"
    },
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
#undef QT_MOC_LITERAL
#endif // !QT_MOC_HAS_STRING_DATA
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_CLASSStudioWindowENDCLASS[] = {

 // content:
      11,       // revision
       0,       // classname
       0,    0, // classinfo
      12,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   86,    2, 0x08,    1 /* Private */,
       4,    2,   89,    2, 0x08,    3 /* Private */,
       7,    1,   94,    2, 0x08,    6 /* Private */,
       9,    1,   97,    2, 0x08,    8 /* Private */,
      11,    0,  100,    2, 0x08,   10 /* Private */,
      12,    1,  101,    2, 0x08,   11 /* Private */,
      13,    2,  104,    2, 0x08,   13 /* Private */,
      17,    1,  109,    2, 0x08,   16 /* Private */,
      20,    1,  112,    2, 0x08,   18 /* Private */,
      22,    0,  115,    2, 0x08,   20 /* Private */,
      23,    2,  116,    2, 0x08,   21 /* Private */,
      26,    0,  121,    2, 0x08,   24 /* Private */,

 // slots: parameters
    QMetaType::Void, QMetaType::LongLong,    3,
    QMetaType::Void, QMetaType::LongLong, QMetaType::LongLong,    5,    6,
    QMetaType::Void, QMetaType::Int,    8,
    QMetaType::Void, QMetaType::Int,   10,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   10,
    QMetaType::Void, 0x80000000 | 14, QMetaType::Float,   15,   16,
    QMetaType::Void, 0x80000000 | 18,   19,
    QMetaType::Void, QMetaType::QString,   21,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString, QMetaType::LongLong,   24,   25,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject StudioWindow::staticMetaObject = { {
    QMetaObject::SuperData::link<QMainWindow::staticMetaObject>(),
    qt_meta_stringdata_CLASSStudioWindowENDCLASS.offsetsAndSizes,
    qt_meta_data_CLASSStudioWindowENDCLASS,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_CLASSStudioWindowENDCLASS_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<StudioWindow, std::true_type>,
        // method 'onFrequencyEdited'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<qint64, std::false_type>,
        // method 'onPlotterDemodFreq'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<qint64, std::false_type>,
        QtPrivate::TypeAndForceComplete<qint64, std::false_type>,
        // method 'onPlotterWheel'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'onSampleRateSelected'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'onStartStop'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onTabChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'onSpectrum'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QVector<float> &, std::false_type>,
        QtPrivate::TypeAndForceComplete<float, std::false_type>,
        // method 'onDeviceState'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<DeviceController::State, std::false_type>,
        // method 'onDeviceError'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'onRecordIq'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onRecordingFinished'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<qint64, std::false_type>,
        // method 'updateStatusBar'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void StudioWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<StudioWindow *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->onFrequencyEdited((*reinterpret_cast< std::add_pointer_t<qint64>>(_a[1]))); break;
        case 1: _t->onPlotterDemodFreq((*reinterpret_cast< std::add_pointer_t<qint64>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<qint64>>(_a[2]))); break;
        case 2: _t->onPlotterWheel((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 3: _t->onSampleRateSelected((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 4: _t->onStartStop(); break;
        case 5: _t->onTabChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 6: _t->onSpectrum((*reinterpret_cast< std::add_pointer_t<QList<float>>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<float>>(_a[2]))); break;
        case 7: _t->onDeviceState((*reinterpret_cast< std::add_pointer_t<DeviceController::State>>(_a[1]))); break;
        case 8: _t->onDeviceError((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 9: _t->onRecordIq(); break;
        case 10: _t->onRecordingFinished((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<qint64>>(_a[2]))); break;
        case 11: _t->updateStatusBar(); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
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
    if (!strcmp(_clname, qt_meta_stringdata_CLASSStudioWindowENDCLASS.stringdata0))
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
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 12)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 12;
    }
    return _id;
}
QT_WARNING_POP
