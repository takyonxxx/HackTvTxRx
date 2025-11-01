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

        blueScreen = new QWidget(this);
        blueScreen->setObjectName("screen");
        blueScreen->setStyleSheet("background-color:#102849; border-radius: 10px;");

        imageLabel = new QLabel(blueScreen);
        imageLabel->setAlignment(Qt::AlignCenter);
        imageLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

        QVBoxLayout *mainLayout = new QVBoxLayout(this);
        mainLayout->addWidget(blueScreen);
        mainLayout->setContentsMargins(10, 10, 10, 10);
        setLayout(mainLayout);        

        QVBoxLayout *blueScreenLayout = new QVBoxLayout(blueScreen);
        blueScreenLayout->addWidget(imageLabel);
        blueScreenLayout->setContentsMargins(0, 0, 0, 0);
        blueScreen->setLayout(blueScreenLayout);

        setStyleSheet(R"(
            #frame {
                background-color: #404040;
                border-radius: 5px;
            }
            #screen {
                background-color: #112750;
                border-radius: 5px;
            }
        )");

        if (!imageLabel->pixmap().isNull()) {
            updateDisplay(imageLabel->pixmap().toImage());
        }
    }

    void updateDisplay(const QImage& image) {

        if (image.isNull()) {
            qWarning() << "TVDisplay: Received null image!";
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
        path.addRoundedRect(roundedPixmap.rect(), 10, 10);
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
        if (!imageLabel->pixmap().isNull()) {
            updateDisplay(imageLabel->pixmap().toImage());
        }
    }

private:
    QWidget *frameWidget;
    QWidget *blueScreen;
    QLabel *imageLabel;
};

#endif // TV_DISPLAY_H
