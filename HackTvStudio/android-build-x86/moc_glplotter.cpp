/****************************************************************************
** Meta object code from reading C++ file 'glplotter.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.5.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../widgets/glplotter.h"
#include <QtCore/qmetatype.h>

#if __has_include(<QtCore/qtmochelpers.h>)
#include <QtCore/qtmochelpers.h>
#else
QT_BEGIN_MOC_NAMESPACE
#endif


#include <memory>

#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'glplotter.h' doesn't include <QObject>."
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
struct qt_meta_stringdata_CLASSCPlotterENDCLASS_t {};
static constexpr auto qt_meta_stringdata_CLASSCPlotterENDCLASS = QtMocHelpers::stringData(
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
);
#else  // !QT_MOC_HAS_STRING_DATA
struct qt_meta_stringdata_CLASSCPlotterENDCLASS_t {
    uint offsetsAndSizes[72];
    char stringdata0[9];
    char stringdata1[14];
    char stringdata2[1];
    char stringdata3[2];
    char stringdata4[13];
    char stringdata5[5];
    char stringdata6[6];
    char stringdata7[14];
    char stringdata8[15];
    char stringdata9[14];
    char stringdata10[4];
    char stringdata11[5];
    char stringdata12[22];
    char stringdata13[4];
    char stringdata14[4];
    char stringdata15[13];
    char stringdata16[6];
    char stringdata17[16];
    char stringdata18[10];
    char stringdata19[20];
    char stringdata20[17];
    char stringdata21[16];
    char stringdata22[12];
    char stringdata23[16];
    char stringdata24[6];
    char stringdata25[11];
    char stringdata26[8];
    char stringdata27[12];
    char stringdata28[12];
    char stringdata29[18];
    char stringdata30[18];
    char stringdata31[17];
    char stringdata32[2];
    char stringdata33[14];
    char stringdata34[19];
    char stringdata35[8];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_CLASSCPlotterENDCLASS_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_CLASSCPlotterENDCLASS_t qt_meta_stringdata_CLASSCPlotterENDCLASS = {
    {
        QT_MOC_LITERAL(0, 8),  // "CPlotter"
        QT_MOC_LITERAL(9, 13),  // "newCenterFreq"
        QT_MOC_LITERAL(23, 0),  // ""
        QT_MOC_LITERAL(24, 1),  // "f"
        QT_MOC_LITERAL(26, 12),  // "newDemodFreq"
        QT_MOC_LITERAL(39, 4),  // "freq"
        QT_MOC_LITERAL(44, 5),  // "delta"
        QT_MOC_LITERAL(50, 13),  // "newLowCutFreq"
        QT_MOC_LITERAL(64, 14),  // "newHighCutFreq"
        QT_MOC_LITERAL(79, 13),  // "newFilterFreq"
        QT_MOC_LITERAL(93, 3),  // "low"
        QT_MOC_LITERAL(97, 4),  // "high"
        QT_MOC_LITERAL(102, 21),  // "pandapterRangeChanged"
        QT_MOC_LITERAL(124, 3),  // "min"
        QT_MOC_LITERAL(128, 3),  // "max"
        QT_MOC_LITERAL(132, 12),  // "newZoomLevel"
        QT_MOC_LITERAL(145, 5),  // "level"
        QT_MOC_LITERAL(151, 15),  // "wheelFreqChange"
        QT_MOC_LITERAL(167, 9),  // "direction"
        QT_MOC_LITERAL(177, 19),  // "resetHorizontalZoom"
        QT_MOC_LITERAL(197, 16),  // "moveToCenterFreq"
        QT_MOC_LITERAL(214, 15),  // "moveToDemodFreq"
        QT_MOC_LITERAL(230, 11),  // "zoomOnXAxis"
        QT_MOC_LITERAL(242, 15),  // "setFftPlotColor"
        QT_MOC_LITERAL(258, 5),  // "color"
        QT_MOC_LITERAL(264, 10),  // "setFftFill"
        QT_MOC_LITERAL(275, 7),  // "enabled"
        QT_MOC_LITERAL(283, 11),  // "setPeakHold"
        QT_MOC_LITERAL(295, 11),  // "setFftRange"
        QT_MOC_LITERAL(307, 17),  // "setPandapterRange"
        QT_MOC_LITERAL(325, 17),  // "setWaterfallRange"
        QT_MOC_LITERAL(343, 16),  // "setPeakDetection"
        QT_MOC_LITERAL(360, 1),  // "c"
        QT_MOC_LITERAL(362, 13),  // "updateOverlay"
        QT_MOC_LITERAL(376, 18),  // "setPercent2DScreen"
        QT_MOC_LITERAL(395, 7)   // "percent"
    },
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
#undef QT_MOC_LITERAL
#endif // !QT_MOC_HAS_STRING_DATA
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_CLASSCPlotterENDCLASS[] = {

 // content:
      11,       // revision
       0,       // classname
       0,    0, // classinfo
      21,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       8,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,  140,    2, 0x06,    1 /* Public */,
       4,    2,  143,    2, 0x06,    3 /* Public */,
       7,    1,  148,    2, 0x06,    6 /* Public */,
       8,    1,  151,    2, 0x06,    8 /* Public */,
       9,    2,  154,    2, 0x06,   10 /* Public */,
      12,    2,  159,    2, 0x06,   13 /* Public */,
      15,    1,  164,    2, 0x06,   16 /* Public */,
      17,    1,  167,    2, 0x06,   18 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
      19,    0,  170,    2, 0x0a,   20 /* Public */,
      20,    0,  171,    2, 0x0a,   21 /* Public */,
      21,    0,  172,    2, 0x0a,   22 /* Public */,
      22,    1,  173,    2, 0x0a,   23 /* Public */,
      23,    1,  176,    2, 0x0a,   25 /* Public */,
      25,    1,  179,    2, 0x0a,   27 /* Public */,
      27,    1,  182,    2, 0x0a,   29 /* Public */,
      28,    2,  185,    2, 0x0a,   31 /* Public */,
      29,    2,  190,    2, 0x0a,   34 /* Public */,
      30,    2,  195,    2, 0x0a,   37 /* Public */,
      31,    2,  200,    2, 0x0a,   40 /* Public */,
      33,    0,  205,    2, 0x0a,   43 /* Public */,
      34,    1,  206,    2, 0x0a,   44 /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::LongLong,    3,
    QMetaType::Void, QMetaType::LongLong, QMetaType::LongLong,    5,    6,
    QMetaType::Void, QMetaType::Int,    3,
    QMetaType::Void, QMetaType::Int,    3,
    QMetaType::Void, QMetaType::Int, QMetaType::Int,   10,   11,
    QMetaType::Void, QMetaType::Float, QMetaType::Float,   13,   14,
    QMetaType::Void, QMetaType::Float,   16,
    QMetaType::Void, QMetaType::Int,   18,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Float,   16,
    QMetaType::Void, QMetaType::QColor,   24,
    QMetaType::Void, QMetaType::Bool,   26,
    QMetaType::Void, QMetaType::Bool,   26,
    QMetaType::Void, QMetaType::Float, QMetaType::Float,   13,   14,
    QMetaType::Void, QMetaType::Float, QMetaType::Float,   13,   14,
    QMetaType::Void, QMetaType::Float, QMetaType::Float,   13,   14,
    QMetaType::Void, QMetaType::Bool, QMetaType::Float,   26,   32,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   35,

       0        // eod
};

Q_CONSTINIT const QMetaObject CPlotter::staticMetaObject = { {
    QMetaObject::SuperData::link<QOpenGLWidget::staticMetaObject>(),
    qt_meta_stringdata_CLASSCPlotterENDCLASS.offsetsAndSizes,
    qt_meta_data_CLASSCPlotterENDCLASS,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_CLASSCPlotterENDCLASS_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<CPlotter, std::true_type>,
        // method 'newCenterFreq'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<qint64, std::false_type>,
        // method 'newDemodFreq'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<qint64, std::false_type>,
        QtPrivate::TypeAndForceComplete<qint64, std::false_type>,
        // method 'newLowCutFreq'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'newHighCutFreq'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'newFilterFreq'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'pandapterRangeChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<float, std::false_type>,
        QtPrivate::TypeAndForceComplete<float, std::false_type>,
        // method 'newZoomLevel'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<float, std::false_type>,
        // method 'wheelFreqChange'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'resetHorizontalZoom'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'moveToCenterFreq'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'moveToDemodFreq'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'zoomOnXAxis'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<float, std::false_type>,
        // method 'setFftPlotColor'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QColor, std::false_type>,
        // method 'setFftFill'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'setPeakHold'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'setFftRange'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<float, std::false_type>,
        QtPrivate::TypeAndForceComplete<float, std::false_type>,
        // method 'setPandapterRange'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<float, std::false_type>,
        QtPrivate::TypeAndForceComplete<float, std::false_type>,
        // method 'setWaterfallRange'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<float, std::false_type>,
        QtPrivate::TypeAndForceComplete<float, std::false_type>,
        // method 'setPeakDetection'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<float, std::false_type>,
        // method 'updateOverlay'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setPercent2DScreen'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>
    >,
    nullptr
} };

void CPlotter::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<CPlotter *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->newCenterFreq((*reinterpret_cast< std::add_pointer_t<qint64>>(_a[1]))); break;
        case 1: _t->newDemodFreq((*reinterpret_cast< std::add_pointer_t<qint64>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<qint64>>(_a[2]))); break;
        case 2: _t->newLowCutFreq((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 3: _t->newHighCutFreq((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 4: _t->newFilterFreq((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 5: _t->pandapterRangeChanged((*reinterpret_cast< std::add_pointer_t<float>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<float>>(_a[2]))); break;
        case 6: _t->newZoomLevel((*reinterpret_cast< std::add_pointer_t<float>>(_a[1]))); break;
        case 7: _t->wheelFreqChange((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 8: _t->resetHorizontalZoom(); break;
        case 9: _t->moveToCenterFreq(); break;
        case 10: _t->moveToDemodFreq(); break;
        case 11: _t->zoomOnXAxis((*reinterpret_cast< std::add_pointer_t<float>>(_a[1]))); break;
        case 12: _t->setFftPlotColor((*reinterpret_cast< std::add_pointer_t<QColor>>(_a[1]))); break;
        case 13: _t->setFftFill((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 14: _t->setPeakHold((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 15: _t->setFftRange((*reinterpret_cast< std::add_pointer_t<float>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<float>>(_a[2]))); break;
        case 16: _t->setPandapterRange((*reinterpret_cast< std::add_pointer_t<float>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<float>>(_a[2]))); break;
        case 17: _t->setWaterfallRange((*reinterpret_cast< std::add_pointer_t<float>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<float>>(_a[2]))); break;
        case 18: _t->setPeakDetection((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<float>>(_a[2]))); break;
        case 19: _t->updateOverlay(); break;
        case 20: _t->setPercent2DScreen((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (CPlotter::*)(qint64 );
            if (_t _q_method = &CPlotter::newCenterFreq; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (CPlotter::*)(qint64 , qint64 );
            if (_t _q_method = &CPlotter::newDemodFreq; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (CPlotter::*)(int );
            if (_t _q_method = &CPlotter::newLowCutFreq; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (CPlotter::*)(int );
            if (_t _q_method = &CPlotter::newHighCutFreq; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (CPlotter::*)(int , int );
            if (_t _q_method = &CPlotter::newFilterFreq; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (CPlotter::*)(float , float );
            if (_t _q_method = &CPlotter::pandapterRangeChanged; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 5;
                return;
            }
        }
        {
            using _t = void (CPlotter::*)(float );
            if (_t _q_method = &CPlotter::newZoomLevel; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 6;
                return;
            }
        }
        {
            using _t = void (CPlotter::*)(int );
            if (_t _q_method = &CPlotter::wheelFreqChange; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 7;
                return;
            }
        }
    }
}

const QMetaObject *CPlotter::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *CPlotter::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_CLASSCPlotterENDCLASS.stringdata0))
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
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 21)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 21;
    }
    return _id;
}

// SIGNAL 0
void CPlotter::newCenterFreq(qint64 _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void CPlotter::newDemodFreq(qint64 _t1, qint64 _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void CPlotter::newLowCutFreq(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void CPlotter::newHighCutFreq(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void CPlotter::newFilterFreq(int _t1, int _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void CPlotter::pandapterRangeChanged(float _t1, float _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void CPlotter::newZoomLevel(float _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}

// SIGNAL 7
void CPlotter::wheelFreqChange(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 7, _a);
}
QT_WARNING_POP
