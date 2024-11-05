#include "ScreenPrimeOSD.h"

#include <vector>


namespace PrimeOSD
{

Canvas::Canvas(int w, int h)
{
    this->CanvasBuffer = new QImage(w,h,QImage::Format_ARGB32_Premultiplied);
    CanvasBuffer->fill(0x00000000);
    this->Painter = new QPainter(CanvasBuffer);
}

Canvas::Canvas(){
    this->CanvasBuffer = new QImage(256, 192,QImage::Format_ARGB32_Premultiplied);
    CanvasBuffer->fill(0x00000000);
    this->Painter = new QPainter(CanvasBuffer);
}


}