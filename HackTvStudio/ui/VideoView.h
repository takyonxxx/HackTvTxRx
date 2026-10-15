#ifndef VIDEOVIEW_H
#define VIDEOVIEW_H

#include <QWidget>
#include <QImage>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>

/*
 * Aspect-correct video surface. Unlike a QLabel+QPixmap it has no size-hint
 * feedback, so it can freely expand with the layout and shrink again.
 */
class VideoView : public QWidget
{
    Q_OBJECT
public:
    explicit VideoView(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(320, 180);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setFocusPolicy(Qt::StrongFocus);
        setAutoFillBackground(false);
        setAttribute(Qt::WA_OpaquePaintEvent);
    }

    void setFrame(const QImage& img)
    {
        m_frame = img;
        update();
    }

    void setWideAspect(bool wide)
    {
        m_wide = wide;
        update();
    }

    const QImage& frame() const { return m_frame; }

signals:
    void doubleClicked();
    void escapePressed();

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::black);
        if (m_frame.isNull()) return;

        const QSize aspect = m_wide ? QSize(16, 9) : m_frame.size();
        const QSize target = aspect.scaled(size(), Qt::KeepAspectRatio);
        const QRect dst((width() - target.width()) / 2, (height() - target.height()) / 2,
                        target.width(), target.height());
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawImage(dst, m_frame);
    }

    void mouseDoubleClickEvent(QMouseEvent*) override { emit doubleClicked(); }

    void keyPressEvent(QKeyEvent* e) override
    {
        if (e->key() == Qt::Key_Escape) { emit escapePressed(); return; }
        QWidget::keyPressEvent(e);
    }

private:
    QImage m_frame;
    bool m_wide = true;
};

#endif // VIDEOVIEW_H
