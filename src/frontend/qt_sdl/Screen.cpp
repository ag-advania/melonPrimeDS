/*
    Copyright 2016-2025 melonDS team
    ... (略) ...
*/

#include <string.h>

#include <optional>
#include <cmath>

#include <QPaintEvent>
#include <QPainter>

#include <QDateTime>

#include "OpenGLSupport.h"
#include "duckstation/gl/context.h"

#include "main.h"
#include "EmuInstance.h"

#include "NDS.h"
#include "GPU.h"
#include "GPU3D_Soft.h"
#include "GPU3D_OpenGL.h"
#include "Platform.h"
#include "Config.h"

#include "main_shaders.h"
#include "OSD_shaders.h"
#include "font.h"
#include "version.h"

// MelonPrimeDS Integration
#include "MelonPrime.h"



// melonprimeds
#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>


#endif

// MelonPrimeDS
#ifdef _WIN32
#include <windows.h>
// 仮想デスクトップ矩形取得用ヘルパー
inline RECT getVirtualScreenRect() {
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return RECT{ vx, vy, vx + vw, vy + vh };
}

// 幅1pxのクリップ縦帯を生成
static RECT computeCenter1pxClipRectSafe(HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc);
    POINT tl{ rc.left, rc.top }, br{ rc.right, rc.bottom };
    ClientToScreen(hwnd, &tl);
    ClientToScreen(hwnd, &br);

    LONG cx = (tl.x + br.x) / 2;

    const RECT vs = getVirtualScreenRect();

    if (cx < vs.left)  cx = vs.left;
    if (cx >= vs.right) cx = vs.right - 1;

    LONG top = (tl.y > vs.top) ? tl.y : vs.top;
    LONG bottom = (br.y < vs.bottom) ? br.y : vs.bottom;

    if (top >= bottom) {
        top = vs.top;
        bottom = vs.bottom;
    }

    RECT clip{ cx, top, cx + 1, bottom };
    return clip;
}

// 垂直中央を維持してRECTの高さを1/2に縮小
inline RECT shrinkRectHeightToHalfCentered(RECT r) {
    const LONG h = r.bottom - r.top;
    const LONG cy = (r.top + r.bottom) / 2;
    const LONG quarter = h / 4;
    r.top = cy - quarter;
    r.bottom = cy + quarter;
    return r;
}
#endif

using namespace melonDS;

#if !defined(_WIN32) && !defined(APPLE)
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
using namespace QNativeInterface;
#else
#include <qpa/qplatformnativeinterface.h>
#endif
#endif


const u32 kOSDMargin = 6;
const int kLogoWidth = 192;

void ScreenPanel::clipCursorCenter1px() { // MelonPrimeDS
    setClipWanted(true);
    setCursor(Qt::BlankCursor);

#ifdef _WIN32
    if (!isVisible() || !window() || !window()->isActiveWindow()) return;
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    RECT clip = computeCenter1pxClipRectSafe(hwnd);
    clip = shrinkRectHeightToHalfCentered(clip);
    ClipCursor(&clip);
#endif
}

void ScreenPanel::unclip() { // MelonPrimeDS
    setClipWanted(false);
#ifdef _WIN32
    ClipCursor(nullptr);
#endif
}

void ScreenPanel::updateClipIfNeeded() { // MelonPrimeDS
    if (!getClipWanted()) return;
    clipCursorCenter1px();
}


ScreenPanel::ScreenPanel(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_AcceptTouchEvents);

    QWidget* w = parent;
    for (;;)
    {
        mainWindow = qobject_cast<MainWindow*>(w);
        if (mainWindow) break;
        w = w->parentWidget();
        if (!w) break;
    }

    emuInstance = mainWindow->getEmuInstance();

    mouseHide = false;
    mouseHideDelay = 0;

    osdEnabled = false;
    osdID = 1;

    loadConfig();
    setFilter(mainWindow->getWindowConfig().GetBool("ScreenFilter"));

    splashLogo = QPixmap(":/melon-logo");

    strncpy(splashText[0].text, "File->Open ROM...", 256);
    splashText[0].id = 0x80000000;
    splashText[0].color = 0;
    splashText[0].rendered = false;
    splashText[0].rainbowstart = -1;

    strncpy(splashText[1].text, "to get started", 256);
    splashText[1].id = 0x80000001;
    splashText[1].color = 0;
    splashText[1].rendered = false;
    splashText[1].rainbowstart = -1;

    std::string url = MELONDS_URL;
    int urlpos = url.find("://");
    urlpos = (urlpos == std::string::npos) ? 0 : urlpos + 3;
    strncpy(splashText[2].text, url.c_str() + urlpos, 256);
    splashText[2].id = 0x80000002;
    splashText[2].color = 0;
    splashText[2].rendered = false;
    splashText[2].rainbowstart = -1;
}

ScreenPanel::~ScreenPanel()
{
#if defined(_WIN32)
    unclip(); // MelonPrimeDS
#endif
}

void ScreenPanel::loadConfig()
{
    auto& cfg = mainWindow->getWindowConfig();

    screenRotation = cfg.GetInt("ScreenRotation");
    screenGap = cfg.GetInt("ScreenGap");
    screenLayout = cfg.GetInt("ScreenLayout");
    screenSwap = cfg.GetBool("ScreenSwap");
    screenSizing = cfg.GetInt("ScreenSizing");
    integerScaling = cfg.GetBool("IntegerScaling");
    screenAspectTop = cfg.GetInt("ScreenAspectTop");
    screenAspectBot = cfg.GetInt("ScreenAspectBot");
}

void ScreenPanel::setFilter(bool filter)
{
    this->filter = filter;
}

void ScreenPanel::setMouseHide(bool enable, int delay)
{
    mouseHide = enable;
    mouseHideDelay = delay;
}

void ScreenPanel::setupScreenLayout()
{
    int w = width();
    int h = height();

    int sizing = screenSizing;
    if (sizing == screenSizing_Auto) sizing = autoScreenSizing;

    float aspectTop, aspectBot;

    for (auto ratio : aspectRatios)
    {
        if (ratio.id == screenAspectTop)
            aspectTop = ratio.ratio;
        if (ratio.id == screenAspectBot)
            aspectBot = ratio.ratio;
    }

    if (aspectTop == 0)
        aspectTop = ((float)w / h) / (4.f / 3.f);

    if (aspectBot == 0)
        aspectBot = ((float)w / h) / (4.f / 3.f);

    layout.Setup(w, h,
        static_cast<ScreenLayoutType>(screenLayout),
        static_cast<ScreenRotation>(screenRotation),
        static_cast<ScreenSizing>(sizing),
        screenGap,
        integerScaling != 0,
        screenSwap != 0,
        aspectTop,
        aspectBot);

    numScreens = layout.GetScreenTransforms(screenMatrix[0], screenKind);

    calcSplashLayout();

    // MelonPrimeDS: Notify layout change
    if (auto* core = emuInstance->getEmuThread()->GetMelonPrimeCore()) {
        core->NotifyLayoutChange();
    }
}

QSize ScreenPanel::screenGetMinSize(int factor = 1)
{
    bool isHori = (screenRotation == screenRot_90Deg
        || screenRotation == screenRot_270Deg);
    int gap = screenGap * factor;

    int w = 256 * factor;
    int h = 192 * factor;

    if (screenSizing == screenSizing_TopOnly
        || screenSizing == screenSizing_BotOnly)
    {
        return QSize(w, h);
    }

    if (screenLayout == screenLayout_Natural)
    {
        if (isHori)
            return QSize(h + gap + h, w);
        else
            return QSize(w, h + gap + h);
    }
    else if (screenLayout == screenLayout_Vertical)
    {
        if (isHori)
            return QSize(h, w + gap + w);
        else
            return QSize(w, h + gap + h);
    }
    else if (screenLayout == screenLayout_Horizontal)
    {
        if (isHori)
            return QSize(h + gap + h, w);
        else
            return QSize(w + gap + w, h);
    }
    else // hybrid
    {
        if (isHori)
            return QSize(h + gap + h, 3 * w + (int)ceil((4 * gap) / 3.0));
        else
            return QSize(3 * w + (int)ceil((4 * gap) / 3.0), h + gap + h);
    }
}

void ScreenPanel::onScreenLayoutChanged()
{
    loadConfig();

    setMinimumSize(screenGetMinSize());
    setupScreenLayout();
}

void ScreenPanel::onAutoScreenSizingChanged(int sizing)
{
    autoScreenSizing = sizing;
    if (screenSizing != screenSizing_Auto) return;

    setupScreenLayout();
}

void ScreenPanel::resizeEvent(QResizeEvent* event)
{
    setupScreenLayout();
#if defined(_WIN32)
    // melonprimeds
    updateClipIfNeeded();
#endif
    QWidget::resizeEvent(event);
}

void ScreenPanel::mousePressEvent(QMouseEvent* event)
{
    event->accept();
    auto* const emu = emuInstance;
    auto* const thr = emu->getEmuThread();
    auto* const core = thr->GetMelonPrimeCore();

    if (Q_UNLIKELY(!emu->emuIsActive()))
    {
        touching = false;
        return;
    }

    // ★修正: クリックでフォーカスONにする処理を復活
    if (core) core->isFocused = true;

    emu->onMousePress(event);

    if (event->button() != Qt::LeftButton)
        return;

    // マウスエイムモード（!isStylusMode）かつ、ゲーム中の場合
    if (core && !core->isStylusMode && core->IsInGame())
    {
        // エイムモード（!isCursorMode）の時は、クリックをエイム復帰（クリップ）として扱い、
        // タッチパネルへの入力をブロックして return する。
        if (!core->isCursorMode)
        {
            clipCursorCenter1px();
            return;
        }

        // isCursorMode == true (メニュー画面等) の場合は、
        // ここで return せず、下の「layout.GetTouchCoords」へ処理を流す。
    }

    const QPoint p = event->pos();
    int x = p.x();
    int y = p.y();

    if (layout.GetTouchCoords(x, y, false))
    {
        touching = true;
        emu->touchScreen(x, y);
    }

    // カーソルモードならクリップしない
    if (core && !core->isStylusMode && !core->isCursorMode)
    {
        clipCursorCenter1px();
    }
}

void ScreenPanel::mouseReleaseEvent(QMouseEvent* event)
{
    event->accept();

    auto* const emu = emuInstance;

    emu->onMouseRelease(event); // MelonPrimeDS

    // 非アクティブなら押下状態を必ず解除
    if (Q_UNLIKELY(!emu->emuIsActive()))
    {
        touching = false;
        return;
    }

    if (event->button() != Qt::LeftButton)
        return;

    if (!touching)
        return;

    touching = false;
    emu->releaseScreen();
}

void ScreenPanel::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();

    auto* const emu = emuInstance;

    // 非アクティブはホットパスじゃない想定
    if (Q_UNLIKELY(!emu->emuIsActive()))
        return;

    if (!touching)
        return;

    const QPoint p = event->pos();
    int x = p.x();
    int y = p.y();

    if (layout.GetTouchCoords(x, y, true))
    {
        emu->touchScreen(x, y);
    }
}


void ScreenPanel::tabletEvent(QTabletEvent* event)
{
    event->accept();
    if (!emuInstance->emuIsActive()) { touching = false; return; }

    switch (event->type())
    {
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    {
#if QT_VERSION_MAJOR == 6
        const QPointF pos = event->position();
        int x = pos.x();
        int y = pos.y();
#else
        int x = event->x();
        int y = event->y();
#endif

        if (layout.GetTouchCoords(x, y, event->type() == QEvent::TabletMove))
        {
            touching = true;
            emuInstance->touchScreen(x, y);
        }
    }
    break;
    case QEvent::TabletRelease:
        if (touching)
        {
            emuInstance->releaseScreen();
            touching = false;
        }
        break;
    default:
        break;
    }
}

void ScreenPanel::touchEvent(QTouchEvent* event)
{
#if QT_VERSION_MAJOR == 6
    if (event->device()->type() == QInputDevice::DeviceType::TouchPad)
        return;
#endif

    event->accept();
    if (!emuInstance->emuIsActive()) { touching = false; return; }

    switch (event->type())
    {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
#if QT_VERSION_MAJOR == 6
        if (event->points().length() > 0)
        {
            QPointF lastPosition = event->points().first().lastPosition();
#else
        if (event->touchPoints().length() > 0)
        {
            QPointF lastPosition = event->touchPoints().first().lastPos();
#endif
            int x = (int)lastPosition.x();
            int y = (int)lastPosition.y();

            if (layout.GetTouchCoords(x, y, event->type() == QEvent::TouchUpdate))
            {
                touching = true;
                emuInstance->touchScreen(x, y);
            }
        }
        break;
    case QEvent::TouchEnd:
        if (touching)
        {
            emuInstance->releaseScreen();
            touching = false;
        }
        break;
    default:
        break;
        }
    }

bool ScreenPanel::event(QEvent * event)
{
    if (event->type() == QEvent::TouchBegin
        || event->type() == QEvent::TouchEnd
        || event->type() == QEvent::TouchUpdate)
    {
        touchEvent((QTouchEvent*)event);
        return true;
    }
    else if (event->type() == QEvent::FocusIn)
        mainWindow->onFocusIn();
    else if (event->type() == QEvent::FocusOut)
        mainWindow->onFocusOut();

    return QWidget::event(event);
}

int ScreenPanel::osdFindBreakPoint(const char* text, int i)
{
    for (int j = i; j >= 0; j--)
    {
        if (text[j] == ' ')
            return j;
    }

    return i;
}

void ScreenPanel::osdLayoutText(const char* text, int* width, int* height, int* breaks)
{
    int w = 0;
    int h = 14;
    int totalw = 0;
    int maxw = ((QWidget*)this)->width() - (kOSDMargin * 2);
    int lastbreak = -1;
    int numbrk = 0;
    u16* ptr;

    memset(breaks, 0, sizeof(int) * 64);

    for (int i = 0; text[i] != '\0'; )
    {
        int glyphsize;
        if (text[i] == ' ')
        {
            glyphsize = 6;
        }
        else
        {
            u32 ch = text[i];
            if (ch < 0x10 || ch > 0x7E) ch = 0x7F;

            ptr = &::font[(ch - 0x10) << 4];
            glyphsize = ptr[0];
            if (!glyphsize) glyphsize = 6;
            else            glyphsize += 2;
        }

        w += glyphsize;
        if (w > maxw)
        {
            if (text[i] == ' ')
            {
                if (numbrk >= 64) break;
                breaks[numbrk++] = i;
                i++;
            }
            else
            {
                int brk = osdFindBreakPoint(text, i);
                if (brk != lastbreak) i = brk;

                if (numbrk >= 64) break;
                breaks[numbrk++] = i;

                lastbreak = brk;
            }

            w = 0;
            h += 14;
        }
        else
            i++;

        if (w > totalw) totalw = w;
    }

    *width = totalw;
    *height = h;
}

unsigned int ScreenPanel::osdRainbowColor(int inc)
{
    if (inc < 100) return 0xFFFF9B9B + (inc << 8);
    else if (inc < 200) return 0xFFFFFF9B - ((inc - 100) << 16);
    else if (inc < 300) return 0xFF9BFF9B + (inc - 200);
    else if (inc < 400) return 0xFF9BFFFF - ((inc - 300) << 8);
    else if (inc < 500) return 0xFF9B9BFF + ((inc - 400) << 16);
    else                return 0xFFFF9BFF - (inc - 500);
}

void ScreenPanel::osdRenderItem(OSDItem * item)
{
    int w, h;
    int breaks[64];

    char* text = item->text;
    u32 color = item->color;

    bool rainbow = (color == 0);
    u32 rainbowinc;
    if (item->rainbowstart == -1)
    {
        u32 ticks = (u32)QDateTime::currentMSecsSinceEpoch();
        rainbowinc = ((text[0] * 17) + (ticks * 13)) % 600;
    }
    else
        rainbowinc = (u32)item->rainbowstart;

    color |= 0xFF000000;
    const u32 shadow = 0xE0000000;

    osdLayoutText(text, &w, &h, breaks);

    item->bitmap = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
    u32* bitmap = (u32*)item->bitmap.bits();
    memset(bitmap, 0, w * h * sizeof(u32));

    int x = 0, y = 1;
    int curline = 0;
    u16* ptr;

    for (int i = 0; text[i] != '\0'; )
    {
        int glyphsize;
        if (text[i] == ' ')
        {
            x += 6;
        }
        else
        {
            u32 ch = text[i];
            if (ch < 0x10 || ch > 0x7E) ch = 0x7F;

            ptr = &::font[(ch - 0x10) << 4];
            int glyphsize = ptr[0];
            if (!glyphsize) x += 6;
            else
            {
                x++;

                if (rainbow)
                {
                    color = osdRainbowColor(rainbowinc);
                    rainbowinc = (rainbowinc + 30) % 600;
                }

                for (int cy = 0; cy < 12; cy++)
                {
                    u16 val = ptr[4 + cy];

                    for (int cx = 0; cx < glyphsize; cx++)
                    {
                        if (val & (1 << cx))
                            bitmap[((y + cy) * w) + x + cx] = color;
                    }
                }

                x += glyphsize;
                x++;
            }
        }

        i++;
        if (breaks[curline] && i >= breaks[curline])
        {
            i = breaks[curline++];
            if (text[i] == ' ') i++;

            x = 0;
            y += 14;
        }
    }

    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            u32 val;

            val = bitmap[(y * w) + x];
            if ((val >> 24) == 0xFF) continue;

            if (x > 0)   val = bitmap[(y * w) + x - 1];
            if (x < w - 1) val |= bitmap[(y * w) + x + 1];
            if (y > 0)
            {
                if (x > 0)   val |= bitmap[((y - 1) * w) + x - 1];
                val |= bitmap[((y - 1) * w) + x];
                if (x < w - 1) val |= bitmap[((y - 1) * w) + x + 1];
            }
            if (y < h - 1)
            {
                if (x > 0)   val |= bitmap[((y + 1) * w) + x - 1];
                val |= bitmap[((y + 1) * w) + x];
                if (x < w - 1) val |= bitmap[((y + 1) * w) + x + 1];
            }

            if ((val >> 24) == 0xFF)
                bitmap[(y * w) + x] = shadow;
        }
    }

    item->rainbowend = (int)rainbowinc;
}

void ScreenPanel::osdDeleteItem(OSDItem * item)
{
}

void ScreenPanel::osdSetEnabled(bool enabled)
{
    osdMutex.lock();
    osdEnabled = enabled;
    osdMutex.unlock();
}

void ScreenPanel::osdAddMessage(unsigned int color, const char* text)
{
    if (!osdEnabled) return;

    osdMutex.lock();

    OSDItem item;

    item.id = (osdID++) & 0x7FFFFFFF;
    item.timestamp = QDateTime::currentMSecsSinceEpoch();
    strncpy(item.text, text, 255); item.text[255] = '\0';
    item.color = color;
    item.rendered = false;
    item.rainbowstart = -1;

    osdItems.push_back(item);

    osdMutex.unlock();
}

void ScreenPanel::osdUpdate()
{
    osdMutex.lock();

    qint64 tick_now = QDateTime::currentMSecsSinceEpoch();
    qint64 tick_min = tick_now - 2500;

    for (auto it = osdItems.begin(); it != osdItems.end(); )
    {
        OSDItem& item = *it;

        if ((!osdEnabled) || (item.timestamp < tick_min))
        {
            osdDeleteItem(&item);
            it = osdItems.erase(it);
            continue;
        }

        if (!item.rendered)
        {
            osdRenderItem(&item);
            item.rendered = true;
        }

        it++;
    }

    int rainbowinc = -1;
    bool needrecalc = false;

    for (int i = 0; i < 3; i++)
    {
        if (!splashText[i].rendered)
        {
            splashText[i].rainbowstart = rainbowinc;
            osdRenderItem(&splashText[i]);
            splashText[i].rendered = true;
            rainbowinc = splashText[i].rainbowend;
            needrecalc = true;
        }
    }

    osdMutex.unlock();

    if (needrecalc)
        calcSplashLayout();
}

void ScreenPanel::calcSplashLayout()
{
    if (!splashText[0].rendered)
        return;

    osdMutex.lock();

    int w = width();
    int h = height();

    int xlogo = (w - kLogoWidth) / 2;
    int ylogo = (h - kLogoWidth) / 2;

    int totalwidth = splashText[0].bitmap.width() + 6 + splashText[1].bitmap.width();
    if (totalwidth >= w)
    {
        splashPos[0].setX((width() - splashText[0].bitmap.width()) / 2);
        splashPos[1].setX((width() - splashText[1].bitmap.width()) / 2);

        int basey = ylogo / 2;
        splashPos[0].setY(basey - splashText[0].bitmap.height() - 1);
        splashPos[1].setY(basey + 1);
    }
    else
    {
        splashPos[0].setX((w - totalwidth) / 2);
        splashPos[1].setX(splashPos[0].x() + splashText[0].bitmap.width() + 6);

        int basey = (ylogo - splashText[0].bitmap.height()) / 2;
        splashPos[0].setY(basey);
        splashPos[1].setY(basey);
    }

    splashPos[2].setX((w - splashText[2].bitmap.width()) / 2);
    splashPos[2].setY(ylogo + kLogoWidth + ((ylogo - splashText[2].bitmap.height()) / 2));

    splashPos[3].setX(xlogo);
    splashPos[3].setY(ylogo);

    osdMutex.unlock();
}



ScreenPanelNative::ScreenPanelNative(QWidget * parent) : ScreenPanel(parent)
{
    hasBuffers = false;

    screen[0] = QImage(256, 192, QImage::Format_RGB32);
    screen[1] = QImage(256, 192, QImage::Format_RGB32);

    screenTrans[0].reset();
    screenTrans[1].reset();
}

ScreenPanelNative::~ScreenPanelNative()
{
}

void ScreenPanelNative::setupScreenLayout()
{
    ScreenPanel::setupScreenLayout();

    for (int i = 0; i < numScreens; i++)
    {
        float* mtx = screenMatrix[i];
        screenTrans[i].setMatrix(mtx[0], mtx[1], 0.f,
            mtx[2], mtx[3], 0.f,
            mtx[4], mtx[5], 1.f);
    }
}

void ScreenPanelNative::drawScreen()
{
    auto emuThread = emuInstance->getEmuThread();
    if (!emuThread->emuIsActive())
    {
        hasBuffers = false;
        return;
    }

    auto nds = emuInstance->getNDS();
    assert(nds != nullptr);

    bufferLock.lock();
    hasBuffers = nds->GPU.GetFramebuffers(&topBuffer, &bottomBuffer);
    bufferLock.unlock();
}

void ScreenPanelNative::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);

    painter.fillRect(event->rect(), QColor::fromRgb(0, 0, 0));

    auto emuThread = emuInstance->getEmuThread();
    
    if (emuThread->emuIsActive())
    {
        emuInstance->renderLock.lock();

        bufferLock.lock();
        if (hasBuffers)
        {
            memcpy(screen[0].scanLine(0), topBuffer, 256 * 192 * 4);
            memcpy(screen[1].scanLine(0), bottomBuffer, 256 * 192 * 4);
        }
        bufferLock.unlock();

        QRect screenrc(0, 0, 256, 192);

        for (int i = 0; i < numScreens; i++)
        {
            painter.setTransform(screenTrans[i]);
            painter.drawImage(screenrc, screen[screenKind[i]]);
        }
        emuInstance->renderLock.unlock();
    }

    osdUpdate();

    if (!emuThread->emuIsActive())
    {
        osdMutex.lock();

        painter.drawPixmap(QRect(splashPos[3], QSize(kLogoWidth, kLogoWidth)), splashLogo);

        for (int i = 0; i < 3; i++)
            painter.drawImage(splashPos[i], splashText[i].bitmap);

        osdMutex.unlock();
    }

    if (osdEnabled)
    {
        osdMutex.lock();

        u32 y = kOSDMargin;

        painter.resetTransform();

        for (auto it = osdItems.begin(); it != osdItems.end(); )
        {
            OSDItem& item = *it;

            painter.drawImage(kOSDMargin, y, item.bitmap);

            y += item.bitmap.height();
            it++;
        }

        osdMutex.unlock();
    }
}



ScreenPanelGL::ScreenPanelGL(QWidget * parent) : ScreenPanel(parent)
{
    setAutoFillBackground(false);
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_KeyCompression, false);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(screenGetMinSize());

    glInited = false;
}

ScreenPanelGL::~ScreenPanelGL()
{
}

bool ScreenPanelGL::createContext()
{
    std::optional<WindowInfo> windowinfo = getWindowInfo();

    MainWindow* ourwin = (MainWindow*)parentWidget();
    MainWindow* parentwin = (MainWindow*)parentWidget()->parentWidget();

    if (ourwin->getWindowID() != 0)
    {
        if (windowinfo.has_value())
            if ((glContext = parentwin->getOGLContext()->CreateSharedContext(*windowinfo)))
                glContext->DoneCurrent();
    }
    else
    {
        std::array<GL::Context::Version, 2> versionsToTry = {
                GL::Context::Version{GL::Context::Profile::Core, 4, 3},
                GL::Context::Version{GL::Context::Profile::Core, 3, 2} };
        if (windowinfo.has_value())
            if ((glContext = GL::Context::Create(*windowinfo, versionsToTry)))
                glContext->DoneCurrent();
    }

    return glContext != nullptr;
}

void ScreenPanelGL::setSwapInterval(int intv)
{
    if (!glContext) return;

    glContext->SetSwapInterval(intv);
}

void ScreenPanelGL::initOpenGL()
{
    if (!glContext) return;
    if (glInited) return;

    glContext->MakeCurrent();

    OpenGL::CompileVertexFragmentProgram(screenShaderProgram,
        kScreenVS, kScreenFS,
        "ScreenShader",
        { {"vPosition", 0}, {"vTexcoord", 1} },
        { {"oColor", 0} });

    glUseProgram(screenShaderProgram);
    glUniform1i(glGetUniformLocation(screenShaderProgram, "TopScreenTex"), 0);
    glUniform1i(glGetUniformLocation(screenShaderProgram, "BottomScreenTex"), 1);

    screenShaderScreenSizeULoc = glGetUniformLocation(screenShaderProgram, "uScreenSize");
    screenShaderTransformULoc = glGetUniformLocation(screenShaderProgram, "uTransform");

    const float vertices[] =
    {
        0.f,   0.f,    0.f, 0.f, 0.f,
        0.f,   192.f,  0.f, 1.f, 0.f,
        256.f, 192.f,  1.f, 1.f, 0.f,
        0.f,   0.f,    0.f, 0.f, 0.f,
        256.f, 192.f,  1.f, 1.f, 0.f,
        256.f, 0.f,    1.f, 0.f, 0.f,

        0.f,   0.f,    0.f, 0.f, 1.f,
        0.f,   192.f,  0.f, 1.f, 1.f,
        256.f, 192.f,  1.f, 1.f, 1.f,
        0.f,   0.f,    0.f, 0.f, 1.f,
        256.f, 192.f,  1.f, 1.f, 1.f,
        256.f, 0.f,    1.f, 0.f, 1.f
    };

    glGenBuffers(1, &screenVertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, screenVertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glGenVertexArrays(1, &screenVertexArray);
    glBindVertexArray(screenVertexArray);
    glEnableVertexAttribArray(0); // position
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5*4, (void*)(0));
    glEnableVertexAttribArray(1); // texcoord
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5*4, (void*)(2*4));

    glGenTextures(1, &screenTexture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, screenTexture);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, 256, 192, 2, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);


    OpenGL::CompileVertexFragmentProgram(osdShader,
        kScreenVS_OSD, kScreenFS_OSD,
        "OSDShader",
        { {"vPosition", 0} },
        { {"oColor", 0} });

    glUseProgram(osdShader);
    glUniform1i(glGetUniformLocation(osdShader, "OSDTex"), 0);

    osdScreenSizeULoc = glGetUniformLocation(osdShader, "uScreenSize");
    osdPosULoc = glGetUniformLocation(osdShader, "uOSDPos");
    osdSizeULoc = glGetUniformLocation(osdShader, "uOSDSize");
    osdScaleFactorULoc = glGetUniformLocation(osdShader, "uScaleFactor");
    osdTexScaleULoc = glGetUniformLocation(osdShader, "uTexScale");

    const float osdvertices[6 * 2] =
    {
        0, 0,
        1, 1,
        1, 0,
        0, 0,
        0, 1,
        1, 1
    };

    glGenBuffers(1, &osdVertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, osdVertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(osdvertices), osdvertices, GL_STATIC_DRAW);

    glGenVertexArrays(1, &osdVertexArray);
    glBindVertexArray(osdVertexArray);
    glEnableVertexAttribArray(0); // position
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void*)(0));

    // splash logo texture
    QImage logo = splashLogo.scaled(kLogoWidth * 2, kLogoWidth * 2).toImage();
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, logo.width(), logo.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, logo.bits());
    logoTexture = tex;

    transferLayout();
    glInited = true;
}

void ScreenPanelGL::deinitOpenGL()
{
    if (!glContext) return;
    if (!glInited) return;

    glContext->MakeCurrent();

    glDeleteTextures(1, &screenTexture);

    glDeleteVertexArrays(1, &screenVertexArray);
    glDeleteBuffers(1, &screenVertexBuffer);

    glDeleteProgram(screenShaderProgram);


    for (const auto& [key, tex] : osdTextures)
    {
        glDeleteTextures(1, &tex);
    }
    osdTextures.clear();

    glDeleteVertexArrays(1, &osdVertexArray);
    glDeleteBuffers(1, &osdVertexBuffer);

    glDeleteTextures(1, &logoTexture);

    glDeleteProgram(osdShader);


    glContext->DoneCurrent();

    lastScreenWidth = lastScreenHeight = -1;
    glInited = false;
}

void ScreenPanelGL::makeCurrentGL()
{
    if (!glContext) return;

    glContext->MakeCurrent();
}

void ScreenPanelGL::releaseGL()
{
    if (!glContext) return;

    glContext->DoneCurrent();
}

void ScreenPanelGL::osdRenderItem(OSDItem * item)
{
    ScreenPanel::osdRenderItem(item);

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, item->bitmap.width(), item->bitmap.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, item->bitmap.bits());

    osdTextures[item->id] = tex;
}

void ScreenPanelGL::osdDeleteItem(OSDItem * item)
{
    if (osdTextures.count(item->id))
    {
        GLuint tex = osdTextures[item->id];
        glDeleteTextures(1, &tex);
        osdTextures.erase(item->id);
    }

    ScreenPanel::osdDeleteItem(item);
}

void ScreenPanelGL::drawScreen()
{
    if (!glContext) return;

    auto emuThread = emuInstance->getEmuThread();

    glContext->MakeCurrent();

    int w = windowInfo.surface_width;
    int h = windowInfo.surface_height;
    float factor = windowInfo.surface_scale;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    glViewport(0, 0, w, h);

    if (emuThread->emuIsActive())
    {
        auto nds = emuInstance->getNDS();

        glUseProgram(screenShaderProgram);
        glUniform2f(screenShaderScreenSizeULoc, w / factor, h / factor);

        void* topbuf; void* bottombuf;
        if (nds->GPU.GetFramebuffers(&topbuf, &bottombuf))
        {
            // if we're doing a regular render, use the provided framebuffers
            // otherwise, GetFramebuffers() will set up the required state

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D_ARRAY, screenTexture);

            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, 256, 192, 1, GL_BGRA,
                            GL_UNSIGNED_BYTE, topbuf);
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 1, 256, 192, 1, GL_BGRA,
                            GL_UNSIGNED_BYTE, bottombuf);
        }
        else
        {
            GLuint texid = *(GLuint*)topbuf;

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D_ARRAY, texid);
        }

        screenSettingsLock.lock();

        GLint filter = this->filter ? GL_LINEAR : GL_NEAREST;
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, filter);

        glBindBuffer(GL_ARRAY_BUFFER, screenVertexBuffer);
        glBindVertexArray(screenVertexArray);

        for (int i = 0; i < numScreens; i++)
        {
            glUniformMatrix2x3fv(screenShaderTransformULoc, 1, GL_TRUE, screenMatrix[i]);
            glDrawArrays(GL_TRIANGLES, screenKind[i] == 0 ? 0 : 2 * 3, 2 * 3);
        }

        screenSettingsLock.unlock();
    }

    osdUpdate();

    if (!emuThread->emuIsActive())
    {
        // splashscreen
        osdMutex.lock();

        glUseProgram(osdShader);

        glUniform2f(osdScreenSizeULoc, w, h);
        glUniform1f(osdScaleFactorULoc, factor);
        glUniform1f(osdTexScaleULoc, 2.0);

        glBindBuffer(GL_ARRAY_BUFFER, osdVertexBuffer);
        glBindVertexArray(osdVertexArray);

        glActiveTexture(GL_TEXTURE0);

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        glBindTexture(GL_TEXTURE_2D, logoTexture);
        glUniform2i(osdPosULoc, splashPos[3].x(), splashPos[3].y());
        glUniform2i(osdSizeULoc, kLogoWidth, kLogoWidth);
        glDrawArrays(GL_TRIANGLES, 0, 2 * 3);

        glUniform1f(osdTexScaleULoc, 1.0);

        for (int i = 0; i < 3; i++)
        {
            OSDItem& item = splashText[i];

            if (!osdTextures.count(item.id))
                continue;

            glBindTexture(GL_TEXTURE_2D, osdTextures[item.id]);
            glUniform2i(osdPosULoc, splashPos[i].x(), splashPos[i].y());
            glUniform2i(osdSizeULoc, item.bitmap.width(), item.bitmap.height());
            glDrawArrays(GL_TRIANGLES, 0, 2 * 3);
        }

        glDisable(GL_BLEND);
        glUseProgram(0);

        osdMutex.unlock();
    }

    if (osdEnabled)
    {
        osdMutex.lock();

        u32 y = kOSDMargin;

        glUseProgram(osdShader);

        glUniform2f(osdScreenSizeULoc, w, h);
        glUniform1f(osdScaleFactorULoc, factor);
        glUniform1f(osdTexScaleULoc, 1.0);

        glBindBuffer(GL_ARRAY_BUFFER, osdVertexBuffer);
        glBindVertexArray(osdVertexArray);

        glActiveTexture(GL_TEXTURE0);

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        for (auto it = osdItems.begin(); it != osdItems.end(); )
        {
            OSDItem& item = *it;

            if (!osdTextures.count(item.id))
                continue;

            glBindTexture(GL_TEXTURE_2D, osdTextures[item.id]);
            glUniform2i(osdPosULoc, kOSDMargin, y);
            glUniform2i(osdSizeULoc, item.bitmap.width(), item.bitmap.height());
            glDrawArrays(GL_TRIANGLES, 0, 2 * 3);

            y += item.bitmap.height();
            it++;
        }

        glDisable(GL_BLEND);
        glUseProgram(0);

        osdMutex.unlock();
    }

    glContext->SwapBuffers();

    // glFinish(); // MelonPrimeDS

}

qreal ScreenPanelGL::devicePixelRatioFromScreen() const
{
    const QScreen* screen_for_ratio = window()->windowHandle()->screen();
    if (!screen_for_ratio)
        screen_for_ratio = QGuiApplication::primaryScreen();

    return screen_for_ratio ? screen_for_ratio->devicePixelRatio() : static_cast<qreal>(1);
}

int ScreenPanelGL::scaledWindowWidth() const
{
    return std::max(static_cast<int>(std::ceil(static_cast<qreal>(width()) * devicePixelRatioFromScreen())), 1);
}

int ScreenPanelGL::scaledWindowHeight() const
{
    return std::max(static_cast<int>(std::ceil(static_cast<qreal>(height()) * devicePixelRatioFromScreen())), 1);
}

std::optional<WindowInfo> ScreenPanelGL::getWindowInfo()
{
    WindowInfo wi;

    // Windows and Apple are easy here since there's no display connection.
#if defined(_WIN32)
    wi.type = WindowInfo::Type::Win32;
    wi.window_handle = reinterpret_cast<void*>(winId());
#elif defined(__APPLE__)
    wi.type = WindowInfo::Type::MacOS;
    wi.window_handle = reinterpret_cast<void*>(winId());
#else
    const QString platform_name = QGuiApplication::platformName();

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (platform_name == QStringLiteral("xcb"))
    {
        wi.type = WindowInfo::Type::X11;
        const QX11Application* x11 = qApp->nativeInterface<QX11Application>();
        wi.display_connection = x11->display();
        wi.window_handle = reinterpret_cast<void*>(winId());
    }
#if defined(WAYLAND_ENABLED)
    else if (platform_name == QStringLiteral("wayland"))
    {
        wi.type = WindowInfo::Type::Wayland;
        const QWaylandApplication* wl = qApp->nativeInterface<QWaylandApplication>();
        wi.display_connection = wl->display();
        wi.window_handle = reinterpret_cast<void*>(winId());
    }
#endif
#else
    QPlatformNativeInterface* pni = QGuiApplication::platformNativeInterface();
    if (platform_name == QStringLiteral("xcb"))
    {
        wi.type = WindowInfo::Type::X11;
        wi.display_connection = pni->nativeResourceForWindow("display", windowHandle());
        wi.window_handle = reinterpret_cast<void*>(winId());
    }
    else if (platform_name == QStringLiteral("wayland"))
    {
        wi.type = WindowInfo::Type::Wayland;
        QWindow* handle = windowHandle();
        if (handle == nullptr)
            return std::nullopt;

        wi.display_connection = pni->nativeResourceForWindow("display", handle);
        wi.window_handle = pni->nativeResourceForWindow("surface", handle);
    }
#endif
    else
    {
        Platform::Log(Platform::LogLevel::Error, "Unknown PNI platform %s\n", platform_name.toStdString().c_str());
        return std::nullopt;
    }
#endif

    wi.surface_width = static_cast<u32>(scaledWindowWidth());
    wi.surface_height = static_cast<u32>(scaledWindowHeight());
    wi.surface_scale = static_cast<float>(devicePixelRatioFromScreen());

    return wi;
}


QPaintEngine* ScreenPanelGL::paintEngine() const
{
    return nullptr;
}

void ScreenPanelGL::setupScreenLayout()
{
    ScreenPanel::setupScreenLayout();
    transferLayout();
}

void ScreenPanelGL::transferLayout()
{
    std::optional<WindowInfo> windowInfo = getWindowInfo();
    if (windowInfo.has_value())
    {
        screenSettingsLock.lock();

        if (lastScreenWidth != windowInfo->surface_width || lastScreenHeight != windowInfo->surface_height)
        {
            if (glContext)
                glContext->ResizeSurface(windowInfo->surface_width, windowInfo->surface_height);
            lastScreenWidth = windowInfo->surface_width;
            lastScreenHeight = windowInfo->surface_height;
        }

        this->windowInfo = *windowInfo;

        screenSettingsLock.unlock();
    }
}

/* MelonPrimeDS */
void ScreenPanel::unfocus()
{
    // ★復活: CoreのisFocusedフラグをOFFにする
    if (auto* core = emuInstance->getEmuThread()->GetMelonPrimeCore())
        core->isFocused = false;

    setCursor(Qt::ArrowCursor);
#if defined(_WIN32)
    unclip(); // MelonPrimeDS
#endif
}

void ScreenPanel::focusOutEvent(QFocusEvent * event)
{
    // ★復活: 判定なしで強制的にunfocusを呼び出す
    /*
    if (emuInstance->getEmuThread()->GetMelonPrimeCore() && emuInstance->getEmuThread()->GetMelonPrimeCore()->isFocused) {
        return;
    }
    */

    unfocus();
}

void ScreenPanel::moveEvent(QMoveEvent * e) {
    updateClipIfNeeded(); // MelonPrimeDS
    QWidget::moveEvent(e);
}


__attribute__((always_inline)) inline void ScreenPanel::setClipWanted(bool value)
{
    if (emuInstance->getEmuThread()->GetMelonPrimeCore())
        emuInstance->getEmuThread()->GetMelonPrimeCore()->isClipWanted = value;
}

__attribute__((always_inline)) inline bool ScreenPanel::getClipWanted()
{
    if (!emuInstance->getEmuThread()->GetMelonPrimeCore()) return false;
    return emuInstance->getEmuThread()->GetMelonPrimeCore()->isClipWanted;
}