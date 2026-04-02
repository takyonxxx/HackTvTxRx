#include "frequencywidget.h"
#include <QPainter>
#include <QFont>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <cmath>

FrequencyWidget::FrequencyWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(380, 130);
    setMaximumHeight(150);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::WheelFocus);
    setAttribute(Qt::WA_AcceptTouchEvents, true);

    // Up/Down buttons overlaid on right side
    m_upBtn = new QPushButton(this);
    m_upBtn->setText("+");
    m_upBtn->setFixedSize(48, 48);
    m_upBtn->setStyleSheet(
        "QPushButton { background-color: #1A2A44; color: #55BBFF; font-size: 24px; "
        "font-weight: bold; border: 1px solid #334466; border-radius: 8px; }"
        "QPushButton:pressed { background-color: #2A4466; }");
    connect(m_upBtn, &QPushButton::clicked, [this]() { adjustFrequency(1); });

    m_downBtn = new QPushButton(this);
    m_downBtn->setText("-");
    m_downBtn->setFixedSize(48, 48);
    m_downBtn->setStyleSheet(
        "QPushButton { background-color: #1A2A44; color: #55BBFF; font-size: 24px; "
        "font-weight: bold; border: 1px solid #334466; border-radius: 8px; }"
        "QPushButton:pressed { background-color: #2A4466; }");
    connect(m_downBtn, &QPushButton::clicked, [this]() { adjustFrequency(-1); });
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

    int btnAreaW = 56; // reserved width for buttons on the right

    // Background
    p.fillRect(rect(), QColor(16, 16, 28));

    // Border
    p.setPen(QPen(QColor(50, 60, 80), 2));
    p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 8, 8);

    // Format frequency
    uint64_t freq = m_frequency;
    int ghz = freq / 1000000000ULL;
    int mhz = (freq / 1000000ULL) % 1000;
    int khz = (freq / 1000ULL) % 1000;
    int hz  = freq % 1000;

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

    QFont font("Courier New", freqFontSize(), QFont::Bold);
    p.setFont(font);

    QFontMetrics fm(font);
    int totalW = fm.horizontalAdvance(freqStr);
    int drawAreaW = width() - btnAreaW;
    int startX = (drawAreaW - totalW) / 2;
    int textY = height() / 2 + fm.ascent() / 2 - 6;

    // Draw each character, highlight selected digit
    int digitIndex = 0;
    for (int i = 0; i < freqStr.length(); i++) {
        QChar ch = freqStr[i];
        int charWidth = fm.horizontalAdvance(ch);

        if (ch.isDigit()) {
            if (digitIndex == m_selectedDigit) {
                // Highlight selected digit with visible box
                QRect digitRect(startX - 3, 6, charWidth + 6, height() - 12);
                p.fillRect(digitRect, QColor(0, 100, 200, 60));
                p.setPen(QPen(QColor(0, 150, 255, 120), 1));
                p.drawRoundedRect(digitRect, 4, 4);
                p.setPen(QColor(0, 200, 255));
            } else {
                p.setPen(QColor(0, 255, 100));
            }
            digitIndex++;
        } else {
            p.setPen(QColor(80, 80, 100));
        }

        p.drawText(startX, textY, QString(ch));
        startX += charWidth;
    }

    // Unit label
    QFont unitFont("Arial", 16);
    p.setFont(unitFont);
    p.setPen(QColor(130, 130, 150));
    p.drawText(startX + 6, textY, "Hz");

    // Tuning hint at bottom
    QFont hintFont("Arial", 9);
    p.setFont(hintFont);
    p.setPen(QColor(70, 80, 100));
    p.drawText(8, height() - 6, "Tap digit, swipe up/down to tune");

    // Position buttons on right side
    int btnX = width() - btnAreaW + 4;
    int btnGap = 4;
    int totalBtnH = 48 + btnGap + 48;
    int btnY = (height() - totalBtnH) / 2;
    m_upBtn->move(btnX, btnY);
    m_downBtn->move(btnX, btnY + 48 + btnGap);
}

void FrequencyWidget::wheelEvent(QWheelEvent *event)
{
    int delta = event->angleDelta().y() > 0 ? 1 : -1;
    adjustFrequency(delta);
    event->accept();
}

void FrequencyWidget::mousePressEvent(QMouseEvent *event)
{
    // Check if tap is on a digit (not on button area)
    int btnAreaX = width() - 56;
    if (event->pos().x() < btnAreaX) {
        int digit = digitAtPosition(event->pos().x());
        if (digit >= 0) {
            m_selectedDigit = digit;
            update();
        }

        // Start drag tracking
        m_dragging = true;
        m_dragStartY = event->pos().y();
        m_dragAccumulated = 0;
    }
    QWidget::mousePressEvent(event);
}

void FrequencyWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging) return;

    int dy = m_dragStartY - event->pos().y(); // up = positive = increase freq
    m_dragAccumulated += dy;
    m_dragStartY = event->pos().y();

    // Apply steps based on accumulated drag distance
    while (m_dragAccumulated >= DRAG_THRESHOLD) {
        adjustFrequency(1);
        m_dragAccumulated -= DRAG_THRESHOLD;
    }
    while (m_dragAccumulated <= -DRAG_THRESHOLD) {
        adjustFrequency(-1);
        m_dragAccumulated += DRAG_THRESHOLD;
    }

    event->accept();
}

void FrequencyWidget::mouseReleaseEvent(QMouseEvent *event)
{
    m_dragging = false;
    m_dragAccumulated = 0;
    QWidget::mouseReleaseEvent(event);
}

int FrequencyWidget::digitAtPosition(int x) const
{
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

    QFont font("Courier New", freqFontSize(), QFont::Bold);
    QFontMetrics fm(font);
    int btnAreaW = 56;
    int drawAreaW = width() - btnAreaW;
    int totalW = fm.horizontalAdvance(freqStr);
    int startX = (drawAreaW - totalW) / 2;

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
    uint64_t freq = m_frequency;
    int ghz = freq / 1000000000ULL;
    int totalDigits = ghz > 0 ? 10 : 9;

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
