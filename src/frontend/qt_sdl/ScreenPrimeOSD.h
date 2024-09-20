#include <QImage>
#include <QPainter>
#include <vector>



namespace PrimeOSD
{

struct Canvas
{
    QImage* CanvasBuffer;
    QPainter* Painter;
    uint GLTexture;
    Canvas(int w,int h);
    Canvas();
};

}

