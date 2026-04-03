#ifndef FREQUENCYWIDGET_H
#define FREQUENCYWIDGET_H

#include <QWidget>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPushButton>

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
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    uint64_t m_frequency = 145000000; // 145 MHz default
    int m_selectedDigit = 6; // which digit is selected for tuning

    // Touch/drag state
    bool m_dragging = false;
    int m_dragStartY = 0;
    int m_dragAccumulated = 0;
    static constexpr int DRAG_THRESHOLD = 18; // pixels per step

    // Up/Down buttons
    QPushButton* m_upBtn;
    QPushButton* m_downBtn;

    int digitAtPosition(int x) const;
    void adjustFrequency(int delta);
    int freqFontSize() const { return 44; }
};

#endif // FREQUENCYWIDGET_H
