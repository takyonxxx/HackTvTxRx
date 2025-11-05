// tvdisplayadapter.h - YENİ DOSYA OLUŞTUR
#ifndef TVDISPLAYADAPTER_H
#define TVDISPLAYADAPTER_H

#include "tvscreen.h"
#include "tv_display.h"
#include <QImage>
#include <vector>
#include <cstring>

class TVDisplayAdapter : public TVScreen {
public:
    explicit TVDisplayAdapter(TVDisplay* display)
        : m_display(display)
        , m_currentRow(0)
        , m_frameWidth(720)
        , m_frameHeight(576)
    {
        m_lineBuffer.resize(m_frameWidth * 3); // RGB buffer for one line
        startNewFrame(m_frameWidth, m_frameHeight);
    }

    void selectRow(int row) override {
        // Önceki satırı commit et
        if (m_currentRow >= 0 && m_currentRow < m_frameHeight) {
            commitLine();
        }

        m_currentRow = row;

        // Yeni satır için buffer'ı temizle
        std::fill(m_lineBuffer.begin(), m_lineBuffer.end(), 0);
    }

    void setDataColor(int x, int r, int g, int b) override {
        if (x >= 0 && x < m_frameWidth && m_currentRow >= 0 && m_currentRow < m_frameHeight) {
            int idx = x * 3;
            if (idx + 2 < static_cast<int>(m_lineBuffer.size())) {
                m_lineBuffer[idx] = static_cast<uchar>(r);
                m_lineBuffer[idx + 1] = static_cast<uchar>(g);
                m_lineBuffer[idx + 2] = static_cast<uchar>(b);
            }
        }
    }

    void renderImage(int) override {     
        // Son satırı commit et
        if (m_currentRow >= 0 && m_currentRow < m_frameHeight) {
            commitLine();
        }

        // Display'e gönder
        if (m_display && !m_frameImage.isNull()) {
            QImage copy = m_frameImage.copy();
            m_display->updateDisplay(copy);
        } else {
            if (!m_display) {
                qWarning() << "TVDisplayAdapter: m_display is null!";
            }
            if (m_frameImage.isNull()) {
                qWarning() << "TVDisplayAdapter: m_frameImage is null!";
            }
        }

        // Yeni frame için hazırla
        startNewFrame(m_frameWidth, m_frameHeight);
    }

    void setFrameSize(int width, int height) {
        if (width != m_frameWidth || height != m_frameHeight) {
            m_frameWidth = width;
            m_frameHeight = height;
            m_lineBuffer.resize(m_frameWidth * 3);
            startNewFrame(m_frameWidth, m_frameHeight);
        }
    }

private:
    void startNewFrame(int width, int height) {
        m_frameImage = QImage(width, height, QImage::Format_RGB888);
        m_frameImage.fill(Qt::black);
        m_currentRow = -1;
    }

    void commitLine() {
        if (m_currentRow >= 0 && m_currentRow < m_frameImage.height() && !m_lineBuffer.empty()) {
            uchar* scanLine = m_frameImage.scanLine(m_currentRow);
            int bytesToCopy = std::min(
                static_cast<int>(m_lineBuffer.size()),
                m_frameImage.width() * 3
                );
            std::memcpy(scanLine, m_lineBuffer.data(), bytesToCopy);
        }
    }

    TVDisplay* m_display;
    int m_currentRow;
    int m_frameWidth;
    int m_frameHeight;
    QImage m_frameImage;
    std::vector<uchar> m_lineBuffer;
};

#endif // TVDISPLAYADAPTER_H
