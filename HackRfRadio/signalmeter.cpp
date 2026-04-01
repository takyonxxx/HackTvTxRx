#include "signalmeter.h"
#include <QPainter>
#include <cmath>

SignalMeter::SignalMeter(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(200, 30);
    setMaximumHeight(35);
}

void SignalMeter::setLevel(float level)
{
    m_level = std::clamp(level, 0.0f, 1.0f);

    // Peak hold with decay
    if (m_level > m_peak) {
        m_peak = m_level;
    } else {
        m_peak *= 0.98f; // slow decay
    }

    update();
}

void SignalMeter::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    int w = width();
    int h = height();

    // Background
    p.fillRect(rect(), QColor(20, 20, 30));
    p.setPen(QPen(QColor(60, 60, 80), 1));
    p.drawRect(rect().adjusted(0, 0, -1, -1));

    // Meter bar area
    int barX = 4;
    int barY = 4;
    int barW = w - 8;
    int barH = h - 8;

    // Background gradient for meter
    p.fillRect(barX, barY, barW, barH, QColor(10, 10, 15));

    // Signal bar with gradient: green -> yellow -> red
    int fillW = static_cast<int>(m_level * barW);

    for (int x = 0; x < fillW; x++) {
        float ratio = static_cast<float>(x) / barW;
        QColor color;
        if (ratio < 0.5f) {
            // Green to Yellow
            float t = ratio / 0.5f;
            color = QColor(
                static_cast<int>(t * 255),
                255,
                0
            );
        } else {
            // Yellow to Red
            float t = (ratio - 0.5f) / 0.5f;
            color = QColor(
                255,
                static_cast<int>((1.0f - t) * 255),
                0
            );
        }
        p.setPen(color);
        p.drawLine(barX + x, barY, barX + x, barY + barH);
    }

    // Peak marker
    int peakX = barX + static_cast<int>(m_peak * barW);
    p.setPen(QPen(QColor(255, 255, 255), 2));
    p.drawLine(peakX, barY, peakX, barY + barH);

    // S-meter labels
    QFont labelFont("Arial", 7);
    p.setFont(labelFont);
    p.setPen(QColor(120, 120, 140));

    const char* labels[] = {"S1", "S3", "S5", "S7", "S9", "+20", "+40"};
    int labelCount = 7;
    for (int i = 0; i < labelCount; i++) {
        int lx = barX + (barW * i) / (labelCount - 1);
        p.drawText(lx - 8, barY + barH + 10, labels[i]);
    }

    // dB value
    float db = 20.0f * std::log10(std::max(m_level, 0.001f));
    QFont dbFont("Arial", 9, QFont::Bold);
    p.setFont(dbFont);
    p.setPen(QColor(200, 200, 220));
    p.drawText(w - 55, barY + barH - 2, QString("%1 dB").arg(db, 0, 'f', 1));
}
