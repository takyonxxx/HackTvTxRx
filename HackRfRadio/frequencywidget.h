#ifndef FREQUENCYWIDGET_H
#define FREQUENCYWIDGET_H

#include <QWidget>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>

class FrequencyWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FrequencyWidget(QWidget *parent = nullptr);

    uint64_t frequency() const { return m_frequency; }
    void setFrequency(uint64_t freq);

signals:
    void frequencyChanged(uint64_t freq);

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    uint64_t m_frequency = 145000000; // 145 MHz default
    int m_selectedDigit = 6; // which digit is selected for tuning

    int digitAtPosition(int x) const;
    void adjustFrequency(int delta);
};

#endif // FREQUENCYWIDGET_H
