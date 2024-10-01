#ifndef TV_DISPLAY_H
#define TV_DISPLAY_H

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QResizeEvent>
#include <QImage>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>

class TVDisplay : public QWidget {
    Q_OBJECT

public:
    TVDisplay(QWidget *parent = nullptr) : QWidget(parent) {
        frameWidget = new QWidget(this);
        frameWidget->setObjectName("frame");

        blueScreen = new QWidget(frameWidget);
        blueScreen->setObjectName("screen");
        blueScreen->setStyleSheet("background-color:#051a42; border-radius: 20px;");

        imageLabel = new QLabel(blueScreen);
        imageLabel->setAlignment(Qt::AlignCenter);
        imageLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

        QVBoxLayout *mainLayout = new QVBoxLayout(this);
        mainLayout->addWidget(frameWidget);
        mainLayout->setContentsMargins(20, 20, 20, 20);
        setLayout(mainLayout);

        QVBoxLayout *frameLayout = new QVBoxLayout(frameWidget);
        frameLayout->addWidget(blueScreen);
        frameLayout->setContentsMargins(10, 10, 10, 10);
        frameWidget->setLayout(frameLayout);

        QVBoxLayout *blueScreenLayout = new QVBoxLayout(blueScreen);
        blueScreenLayout->addWidget(imageLabel);
        blueScreenLayout->setContentsMargins(0, 0, 0, 0);
        blueScreen->setLayout(blueScreenLayout);

        setStyleSheet(R"(
            #frame {
                background-color: #404040;
                border-radius: 30px;
            }
            #screen {
                background-color: #112750;
                border-radius: 20px;
            }
        )");

        updateAspectRatio();
        if (!imageLabel->pixmap().isNull()) {
            updateDisplay(imageLabel->pixmap().toImage());
        }
    }

    void updateDisplay(const QImage& image) {
        if (image.isNull()) {
            imageLabel->clear();
            return;
        }

        QSize displaySize = blueScreen->size();
        QPixmap scaledPixmap = QPixmap::fromImage(image).scaled(
            displaySize,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
            );

        // Create a rounded version of the pixmap
        QPixmap roundedPixmap(scaledPixmap.size());
        roundedPixmap.fill(Qt::transparent);

        QPainter painter(&roundedPixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(roundedPixmap.rect(), 20, 20);
        painter.setClipPath(path);
        painter.drawPixmap(0, 0, scaledPixmap);

        imageLabel->setPixmap(roundedPixmap);

        // Center the image
        QSize pixmapSize = roundedPixmap.size();
        int x = (displaySize.width() - pixmapSize.width()) / 2;
        int y = (displaySize.height() - pixmapSize.height()) / 2;
        imageLabel->setGeometry(x, y, pixmapSize.width(), pixmapSize.height());
    }

protected:
    void resizeEvent(QResizeEvent *event) override {
        QWidget::resizeEvent(event);
        updateAspectRatio();
        if (!imageLabel->pixmap().isNull()) {
            updateDisplay(imageLabel->pixmap().toImage());
        }
    }

private:
    QWidget *frameWidget;
    QWidget *blueScreen;
    QLabel *imageLabel;

    void updateAspectRatio() {
        int w = width() - 40;  // Account for main layout margins
        int h = height() - 40;
        int newH = w * 9 / 16;
        if (newH <= h) {
            frameWidget->setFixedSize(w, newH);
        } else {
            int newW = h * 16 / 9;
            frameWidget->setFixedSize(newW, h);
        }
    }
};

#endif // TV_DISPLAY_H
