#include "ScreenPrimeOSD.h"

OSD_Canvas::OSD_Canvas(int x, int y, int w, int h)
{
    this->CanvasBuffer = new QImage(w,h,QImage::Format_ARGB32_Premultiplied);
    CanvasBuffer->fill(0x00000000);
    this->x = x;
    this->y = y;
    this->Painter = new QPainter(CanvasBuffer); 
}
