#include "frequencywidget.h"
#include <QPainter>
#include <QFont>
#include <cmath>

FrequencyWidget::FrequencyWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(380, 60);
    setMaximumHeight(70);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::WheelFocus);
}

void FrequencyWidget::setFrequency(uint64_t freq)
{
    if (freq < 1000000) freq = 1000000;
    if (freq > 6000000000ULL) freq = 6000000000ULL;

    if (m_frequency != freq) {
        m_frequency = freq;
        update();
        emit frequencyChanged(m_frequency);
    }
}

void FrequencyWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Background
    p.fillRect(rect(), QColor(20, 20, 30));

    // Border
    p.setPen(QPen(QColor(60, 60, 80), 2));
    p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 6, 6);

    // Format frequency as: X,XXX.XXX,XXX Hz
    // Show as GHz.MHz.kHz format
    uint64_t freq = m_frequency;
    int ghz = freq / 1000000000ULL;
    int mhz = (freq / 1000000ULL) % 1000;
    int khz = (freq / 1000ULL) % 1000;
    int hz  = freq % 1000;

    // Each "digit group" position
    // Format: G,MMM.KKK.HHH
    QString freqStr;
    if (ghz > 0) {
        freqStr = QString("%1,%2.%3.%4")
                      .arg(ghz)
                      .arg(mhz, 3, 10, QChar('0'))
                      .arg(khz, 3, 10, QChar('0'))
                      .arg(hz, 3, 10, QChar('0'));
    } else {
        freqStr = QString("%1.%2.%3")
                      .arg(mhz)
                      .arg(khz, 3, 10, QChar('0'))
                      .arg(hz, 3, 10, QChar('0'));
    }

    QFont font("Courier New", 28, QFont::Bold);
    p.setFont(font);

    QFontMetrics fm(font);
    int charW = fm.horizontalAdvance('0');
    int totalW = fm.horizontalAdvance(freqStr);
    int startX = (width() - totalW) / 2;
    int textY = height() / 2 + fm.ascent() / 2 - 2;

    // Draw each character, highlight selected digit
    int digitIndex = 0;
    for (int i = 0; i < freqStr.length(); i++) {
        QChar ch = freqStr[i];
        int charWidth = fm.horizontalAdvance(ch);

        if (ch.isDigit()) {
            if (digitIndex == m_selectedDigit) {
                // Highlight selected digit
                QRect digitRect(startX - 2, 4, charWidth + 4, height() - 8);
                p.fillRect(digitRect, QColor(0, 100, 200, 80));
                p.setPen(QColor(0, 200, 255));
            } else {
                p.setPen(QColor(0, 255, 100));
            }
            digitIndex++;
        } else {
            // Separator (dot, comma)
            p.setPen(QColor(100, 100, 120));
        }

        p.drawText(startX, textY, QString(ch));
        startX += charWidth;
    }

    // Unit label
    QFont unitFont("Arial", 12);
    p.setFont(unitFont);
    p.setPen(QColor(150, 150, 170));
    p.drawText(startX + 8, textY, "Hz");

    // Tuning hint
    QFont hintFont("Arial", 8);
    p.setFont(hintFont);
    p.setPen(QColor(80, 80, 100));
    p.drawText(5, height() - 4, "Click digit + scroll to tune");
}

void FrequencyWidget::wheelEvent(QWheelEvent *event)
{
    int delta = event->angleDelta().y() > 0 ? 1 : -1;
    adjustFrequency(delta);
    event->accept();
}

void FrequencyWidget::mousePressEvent(QMouseEvent *event)
{
    int digit = digitAtPosition(event->pos().x());
    if (digit >= 0) {
        m_selectedDigit = digit;
        update();
    }
    QWidget::mousePressEvent(event);
}

int FrequencyWidget::digitAtPosition(int x) const
{
    // Approximate digit positions based on font metrics
    uint64_t freq = m_frequency;
    int ghz = freq / 1000000000ULL;

    QString freqStr;
    if (ghz > 0) {
        freqStr = QString("%1,%2.%3.%4")
                      .arg(ghz)
                      .arg((int)((freq / 1000000ULL) % 1000), 3, 10, QChar('0'))
                      .arg((int)((freq / 1000ULL) % 1000), 3, 10, QChar('0'))
                      .arg((int)(freq % 1000), 3, 10, QChar('0'));
    } else {
        freqStr = QString("%1.%2.%3")
                      .arg((int)(freq / 1000000ULL))
                      .arg((int)((freq / 1000ULL) % 1000), 3, 10, QChar('0'))
                      .arg((int)(freq % 1000), 3, 10, QChar('0'));
    }

    QFont font("Courier New", 28, QFont::Bold);
    QFontMetrics fm(font);
    int totalW = fm.horizontalAdvance(freqStr);
    int startX = (width() - totalW) / 2;

    int digitIndex = 0;
    for (int i = 0; i < freqStr.length(); i++) {
        int charW = fm.horizontalAdvance(freqStr[i]);
        if (freqStr[i].isDigit()) {
            if (x >= startX && x < startX + charW) {
                return digitIndex;
            }
            digitIndex++;
        }
        startX += charW;
    }
    return -1;
}

void FrequencyWidget::adjustFrequency(int delta)
{
    // Count total digits
    uint64_t freq = m_frequency;
    int ghz = freq / 1000000000ULL;
    int totalDigits = ghz > 0 ? 10 : 9;

    // Map selected digit to frequency step
    // For 10-digit (with GHz): digit 0=GHz, 1-3=MHz, 4-6=kHz, 7-9=Hz
    // For 9-digit (no GHz): digit 0-2=MHz, 3-5=kHz, 6-8=Hz
    int digitFromRight;
    if (totalDigits == 10) {
        digitFromRight = 9 - m_selectedDigit;
    } else {
        digitFromRight = 8 - m_selectedDigit;
    }

    if (digitFromRight < 0) digitFromRight = 0;

    uint64_t step = 1;
    for (int i = 0; i < digitFromRight; i++) {
        step *= 10;
    }

    int64_t newFreq = static_cast<int64_t>(m_frequency) + delta * static_cast<int64_t>(step);
    if (newFreq < 1000000) newFreq = 1000000;
    if (newFreq > 6000000000LL) newFreq = 6000000000LL;

    setFrequency(static_cast<uint64_t>(newFreq));
}
