#include <QImage>
#include <QPainter>

struct OSD_Canvas
{
    int x;
    int y;
    QImage* CanvasBuffer;
    QPainter* Painter;
    OSD_Canvas(int x,int y, int w,int h);
};