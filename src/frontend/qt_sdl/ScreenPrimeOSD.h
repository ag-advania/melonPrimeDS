#include <QImage>
#include <QPainter>
#include <vector>



namespace PrimeOSD
{

struct Canvas
{
    QImage* CanvasBuffer;
    QPainter* Painter;
    Canvas(int w,int h);
    Canvas();
};

}

