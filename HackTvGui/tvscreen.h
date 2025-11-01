// tvscreen.h - YENİ DOSYA OLUŞTUR
#ifndef TVSCREEN_H
#define TVSCREEN_H

// TVScreen interface (DATV-style)
class TVScreen {
public:
    virtual ~TVScreen() {}

    virtual void selectRow(int row) = 0;
    virtual void setDataColor(int x, int r, int g, int b) = 0;
    virtual void renderImage(int) = 0;
};

#endif // TVSCREEN_H
