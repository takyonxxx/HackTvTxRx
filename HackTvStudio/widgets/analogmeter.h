#ifndef ANALOGMETER_H
#define ANALOGMETER_H

#include <QColor>
#include <QString>
#include <QWidget>

/*
 * Classic needle S-meter (arc scale, red needle) for the head unit: sits at the
 * right edge of the spectrum / waterfall. The needle shows how far the tuned
 * channel's peak rises above the noise floor (SNR): S1 = floor, 5 dB per S unit,
 * S9 = +40 dB, then +20/+40/+60 dB over S9. Independent of gain and bandwidth.
 */
class AnalogMeter : public QWidget
{
    Q_OBJECT
public:
    explicit AnalogMeter(QWidget* parent = nullptr);

    void setLevel(float snrDb);           // channel peak above the noise floor, 0 .. 100 dB (0 = idle)
    void setInfo(const QString& text);    // small caption (e.g. "150 kHz wide")
    void setClipRatio(float ratio);       // share of ADC samples at the rails (0..1); shown as a footnote only
    void setThemeColors(const QColor& bg, const QColor& text, const QColor& accent);

    QSize sizeHint() const override { return QSize(230, 268); }
    QSize minimumSizeHint() const override { return QSize(230, 268); }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    float  m_target = 0.0f;
    float  m_shown  = 0.0f;
    float  m_clip = 0.0f;
    QString m_info;
    QColor m_bg{32, 32, 36}, m_text{224, 224, 232}, m_accent{130, 205, 255};
};

#endif // ANALOGMETER_H
