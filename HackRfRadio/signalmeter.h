#ifndef SIGNALMETER_H
#define SIGNALMETER_H

#include <QWidget>
#include <QPainter>
#include <atomic>

class SignalMeter : public QWidget
{
    Q_OBJECT

public:
    explicit SignalMeter(QWidget *parent = nullptr);

    void setLevel(float level); // 0.0 - 1.0
    float level() const { return m_level; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    float m_level = 0.0f;
    float m_peak = 0.0f;
};

#endif // SIGNALMETER_H
