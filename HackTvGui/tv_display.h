#ifndef TV_DISPLAY_H
#define TV_DISPLAY_H

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QResizeEvent>
#include <QImage>
#include <QPixmap>

class TVDisplay : public QWidget {
    Q_OBJECT

public:
    TVDisplay(QWidget *parent = nullptr) : QWidget(parent) {
        //setMinimumSize(800, 450);  // 16:9 minimum boyut

        // Mavi arka plan
        blueScreen = new QWidget(this);
        blueScreen->setStyleSheet("background-color: #112750;");

        // Görüntü etiketi
        imageLabel = new QLabel(blueScreen);
        imageLabel->setAlignment(Qt::AlignCenter);
        imageLabel->setAttribute(Qt::WA_TransparentForMouseEvents);  // Fare olaylarını geçir

        QVBoxLayout *layout = new QVBoxLayout(this);
        layout->addWidget(blueScreen);
        layout->setContentsMargins(0, 0, 0, 0);  // Kenar boşluklarını kaldır
        setLayout(layout);

        QVBoxLayout *blueScreenLayout = new QVBoxLayout(blueScreen);
        blueScreenLayout->addWidget(imageLabel);
        blueScreenLayout->setContentsMargins(0, 0, 0, 0);
        blueScreen->setLayout(blueScreenLayout);
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

        imageLabel->setPixmap(scaledPixmap);

        // Görüntüyü merkezle
        QSize pixmapSize = scaledPixmap.size();
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
    QWidget *blueScreen;
    QLabel *imageLabel;

    void updateAspectRatio() {
        int w = width();
        int h = height();
        int newH = w * 9 / 16;

        if (newH <= h) {
            blueScreen->setFixedSize(w, newH);
        } else {
            int newW = h * 16 / 9;
            blueScreen->setFixedSize(newW, h);
        }
    }
};

#endif // TV_DISPLAY_H
