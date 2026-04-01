/*
    Copyright 2016-2025 melonDS team
    (MelonPrime specific configuration extension)
*/

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QSlider>
#include <QSpinBox>

#include "MelonPrimeInputConfig.h"
#include "ui_MelonPrimeInputConfig.h"
#include "Config.h"
#ifdef MELONPRIME_CUSTOM_HUD
#include "MelonPrimeCustomHud.h"
#endif

using namespace melonDS;

static void applyPixmapToPreview(QWidget* preview, QPixmap& pixmap)
{
    QPalette pal = preview->palette();
    pal.setBrush(QPalette::Window, pixmap);
    preview->setPalette(pal);
    preview->setAutoFillBackground(true);
    preview->update();
}

static QFont loadHudFont(float scale)
{
    static int s_fontId = -2;  // -2 = not yet loaded
    static QStringList s_families;
    if (s_fontId == -2) {
        s_fontId = QFontDatabase::addApplicationFont(":/mph-font");
        if (s_fontId >= 0)
            s_families = QFontDatabase::applicationFontFamilies(s_fontId);
    }
    QFont font;
    if (!s_families.isEmpty()) font = QFont(s_families.at(0));
    font.setPixelSize(std::max(1, static_cast<int>(6.0f * scale)));
    font.setStyleStrategy(QFont::NoAntialias);
    font.setHintingPreference(QFont::PreferFullHinting);
    return font;
}

void MelonPrimeInputConfig::updateRadarPreview()
{
    QWidget* preview = ui->widgetRadarPreview;
    if (!preview) return;

    const int pw = preview->width();
    const int ph = preview->height();

    QPixmap pixmap(pw, ph);
    pixmap.fill(Qt::transparent);

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Draw top screen rectangle (256x192 scaled to fit preview)
    const float dsW = 256.0f, dsH = 192.0f;
    const float scale = std::min(static_cast<float>(pw) / dsW, static_cast<float>(ph) / dsH);
    const float offX = (pw - dsW * scale) / 2.0f;
    const float offY = (ph - dsH * scale) / 2.0f;

    QRectF screenRect(offX, offY, dsW * scale, dsH * scale);
    p.setPen(QPen(QColor(100, 100, 100), 1));
    p.setBrush(QColor(30, 30, 40));
    p.drawRect(screenRect);

    // Draw "TOP SCREEN" label
    p.setPen(QColor(80, 80, 80));
    p.setFont(QFont("sans-serif", 8));
    p.drawText(screenRect, Qt::AlignCenter, "TOP SCREEN");

    // Draw radar circle overlay
    bool enabled = ui->cbMetroidBtmOverlayEnable->isChecked();
    int dstX = ui->spinMetroidBtmOverlayDstX->value();
    int dstY = ui->spinMetroidBtmOverlayDstY->value();
    int dstSize = ui->spinMetroidBtmOverlayDstSize->value();
    double opacity = ui->spinMetroidBtmOverlayOpacity->value();

    if (enabled)
    {
        float cx = offX + (dstX + dstSize / 2.0f) * scale;
        float cy = offY + (dstY + dstSize / 2.0f) * scale;
        float r = (dstSize / 2.0f) * scale;

        QColor fillColor(0, 200, 80, static_cast<int>(opacity * 180));
        QColor borderColor(0, 255, 100, static_cast<int>(opacity * 255));

        p.setPen(QPen(borderColor, 2));
        p.setBrush(fillColor);
        p.drawEllipse(QPointF(cx, cy), r, r);

        // Cross at center
        p.setPen(QPen(borderColor, 1));
        p.drawLine(QPointF(cx - 4, cy), QPointF(cx + 4, cy));
        p.drawLine(QPointF(cx, cy - 4), QPointF(cx, cy + 4));
    }

    p.end();
    applyPixmapToPreview(preview, pixmap);
}

void MelonPrimeInputConfig::snapshotVisualConfig()
{
    QVariantMap& s = m_visualSnapshot;
    s.clear();

    Config::Table& instcfg = emuInstance->getLocalConfig();

    auto sI  = [&](const char* k, QObject* w) {
        if (auto sb = qobject_cast<QSpinBox*>(w)) s[k] = sb->value();
        else if (auto sl = qobject_cast<QSlider*>(w)) s[k] = sl->value();
    };
    auto sSl = [&](const char* k, QSlider* w)         { s[k] = w->value(); };
    auto sD  = [&](const char* k, QDoubleSpinBox* w)  { s[k] = w->value(); };
    auto sB  = [&](const char* k, QCheckBox* w)       { s[k] = w->isChecked(); };
    auto sC  = [&](const char* k, QComboBox* w)       { s[k] = w->currentIndex(); };
    auto sE  = [&](const char* k, QLineEdit* w)       { s[k] = w->text(); };
    // Snapshot a color from config (since colors are now stored directly in config via button pickers)
    auto sCfgI = [&](const char* snapKey, const char* cfgKey) { s[snapKey] = instcfg.GetInt(cfgKey); };

    sB("cCustomHud",       ui->cbMetroidEnableCustomHud);
    sB("cAspectRatio",     ui->cbMetroidInGameAspectRatio);
    sC("cAspectRatioMode", ui->comboMetroidInGameAspectRatioMode);
    // Match Status
    sB("cMatchShow",  ui->cbMetroidHudMatchStatusShow);
    sSl("sMatchX",     ui->spinMetroidHudMatchStatusX);
    sSl("sMatchY",     ui->spinMetroidHudMatchStatusY);
    sSl("sMatchLOfsX", ui->spinMetroidHudMatchStatusLabelOfsX);
    sSl("sMatchLOfsY", ui->spinMetroidHudMatchStatusLabelOfsY);
    sC("cMatchLPos",  ui->comboMetroidHudMatchStatusLabelPos);
    sE("eMatchLP",    ui->leMetroidHudMatchStatusLabelPoints);
    sE("eMatchLO",    ui->leMetroidHudMatchStatusLabelOctoliths);
    sE("eMatchLL",    ui->leMetroidHudMatchStatusLabelLives);
    sE("eMatchLR",    ui->leMetroidHudMatchStatusLabelRingTime);
    sE("eMatchLPT",   ui->leMetroidHudMatchStatusLabelPrimeTime);
    sC("cMatchClr", ui->comboMetroidHudMatchStatusColor);
    sE("eMatchClr", ui->leMetroidHudMatchStatusColorCode);
    sI("sMatchClrSpinR", ui->spinMetroidHudMatchStatusColorR);
    sI("sMatchClrSpinG", ui->spinMetroidHudMatchStatusColorG);
    sI("sMatchClrSpinB", ui->spinMetroidHudMatchStatusColorB);
    sC("cMatchLblClr", ui->comboMetroidHudMatchStatusLabelColor);
    sE("eMatchLblClr", ui->leMetroidHudMatchStatusLabelColorCode);
    sI("sMatchLblClrSpinR", ui->spinMetroidHudMatchStatusLabelColorR);
    sI("sMatchLblClrSpinG", ui->spinMetroidHudMatchStatusLabelColorG);
    sI("sMatchLblClrSpinB", ui->spinMetroidHudMatchStatusLabelColorB);
    sC("cMatchValClr", ui->comboMetroidHudMatchStatusValueColor);
    sE("eMatchValClr", ui->leMetroidHudMatchStatusValueColorCode);
    sI("sMatchValClrSpinR", ui->spinMetroidHudMatchStatusValueColorR);
    sI("sMatchValClrSpinG", ui->spinMetroidHudMatchStatusValueColorG);
    sI("sMatchValClrSpinB", ui->spinMetroidHudMatchStatusValueColorB);
    sC("cMatchSepClr", ui->comboMetroidHudMatchStatusSepColor);
    sE("eMatchSepClr", ui->leMetroidHudMatchStatusSepColorCode);
    sI("sMatchSepClrSpinR", ui->spinMetroidHudMatchStatusSepColorR);
    sI("sMatchSepClrSpinG", ui->spinMetroidHudMatchStatusSepColorG);
    sI("sMatchSepClrSpinB", ui->spinMetroidHudMatchStatusSepColorB);
    sC("cMatchGolClr", ui->comboMetroidHudMatchStatusGoalColor);
    sE("eMatchGolClr", ui->leMetroidHudMatchStatusGoalColorCode);
    sI("sMatchGolClrSpinR", ui->spinMetroidHudMatchStatusGoalColorR);
    sI("sMatchGolClrSpinG", ui->spinMetroidHudMatchStatusGoalColorG);
    sI("sMatchGolClrSpinB", ui->spinMetroidHudMatchStatusGoalColorB);
    // Match Status colors (from config)
    sCfgI("sMatchClrR",  "Metroid.Visual.HudMatchStatusColorR");
    sCfgI("sMatchClrG",  "Metroid.Visual.HudMatchStatusColorG");
    sCfgI("sMatchClrB",  "Metroid.Visual.HudMatchStatusColorB");
    sCfgI("sMatchLblClrR", "Metroid.Visual.HudMatchStatusLabelColorR");
    sCfgI("sMatchLblClrG", "Metroid.Visual.HudMatchStatusLabelColorG");
    sCfgI("sMatchLblClrB", "Metroid.Visual.HudMatchStatusLabelColorB");
    sCfgI("sMatchValClrR", "Metroid.Visual.HudMatchStatusValueColorR");
    sCfgI("sMatchValClrG", "Metroid.Visual.HudMatchStatusValueColorG");
    sCfgI("sMatchValClrB", "Metroid.Visual.HudMatchStatusValueColorB");
    sCfgI("sMatchSepClrR", "Metroid.Visual.HudMatchStatusSepColorR");
    sCfgI("sMatchSepClrG", "Metroid.Visual.HudMatchStatusSepColorG");
    sCfgI("sMatchSepClrB", "Metroid.Visual.HudMatchStatusSepColorB");
    sCfgI("sMatchGolClrR", "Metroid.Visual.HudMatchStatusGoalColorR");
    sCfgI("sMatchGolClrG", "Metroid.Visual.HudMatchStatusGoalColorG");
    sCfgI("sMatchGolClrB", "Metroid.Visual.HudMatchStatusGoalColorB");
    // HP/Weapon
    sSl("sHpX",  ui->spinMetroidHudHpX);       sSl("sHpY",  ui->spinMetroidHudHpY);
    sE("eHpPfx", ui->leMetroidHudHpPrefix);
    sC("cHpAlign", ui->comboMetroidHudHpAlign);
    sB("cHpTxtAuto", ui->cbMetroidHudHpTextAutoColor);
    sC("cHpTxtClr", ui->comboMetroidHudHpTextColor);
    sI("sHpTxtClrR", ui->spinMetroidHudHpTextColorR);
    sI("sHpTxtClrG", ui->spinMetroidHudHpTextColorG);
    sI("sHpTxtClrB", ui->spinMetroidHudHpTextColorB);
    sE("eHpTxtClr",  ui->leMetroidHudHpTextColorCode);
    sCfgI("sCfgHpTxtClrR", "Metroid.Visual.HudHpTextColorR");
    sCfgI("sCfgHpTxtClrG", "Metroid.Visual.HudHpTextColorG");
    sCfgI("sCfgHpTxtClrB", "Metroid.Visual.HudHpTextColorB");
    sI("sWpnX", ui->spinMetroidHudWeaponX);    sI("sWpnY", ui->spinMetroidHudWeaponY);
    sE("eAmmoPfx", ui->leMetroidHudAmmoPrefix);
    sC("cAmmoAlign", ui->comboMetroidHudAmmoAlign);
    sC("cAmmoTxtClr", ui->comboMetroidHudAmmoTextColor);
    sI("sAmmoTxtClrR", ui->spinMetroidHudAmmoTextColorR);
    sI("sAmmoTxtClrG", ui->spinMetroidHudAmmoTextColorG);
    sI("sAmmoTxtClrB", ui->spinMetroidHudAmmoTextColorB);
    sE("eAmmoTxtClr",  ui->leMetroidHudAmmoTextColorCode);
    sCfgI("sCfgAmmoTxtClrR", "Metroid.Visual.HudAmmoTextColorR");
    sCfgI("sCfgAmmoTxtClrG", "Metroid.Visual.HudAmmoTextColorG");
    sCfgI("sCfgAmmoTxtClrB", "Metroid.Visual.HudAmmoTextColorB");
    sC("cHpPos",  ui->comboMetroidHudHpPosition);
    sC("cWpnPos", ui->comboMetroidHudWeaponPosition);
    sB("cWpnIconShow",  ui->cbMetroidHudWeaponIconShow);
    sC("cWpnIconMode",  ui->comboMetroidHudWeaponIconMode);
    sSl("sWpnIconOfsX",  ui->spinMetroidHudWeaponIconOffsetX);
    sSl("sWpnIconOfsY",  ui->spinMetroidHudWeaponIconOffsetY);
    sSl("sWpnIconPosX",   ui->spinMetroidHudWeaponIconPosX);
    sSl("sWpnIconPosY",   ui->spinMetroidHudWeaponIconPosY);
    sC("cWpnIconPos",    ui->comboMetroidHudWeaponIconPosition);
    sC("cWpnIconAnchX",  ui->comboMetroidHudWeaponIconAnchorX);
    sC("cWpnIconAnchY",  ui->comboMetroidHudWeaponIconAnchorY);
    sB("cWpnIconClrOv",  ui->cbMetroidHudWeaponIconColorOverlay);
    // HP Gauge
    sB("cHpGauge",       ui->cbMetroidHudHpGauge);
    sC("cHpGaugeOrient", ui->comboMetroidHudHpGaugeOrientation);
    sSl("sHpGaugeLen",    ui->spinMetroidHudHpGaugeLength);
    sSl("sHpGaugeW",      ui->spinMetroidHudHpGaugeWidth);
    sSl("sHpGaugeOfsX",   ui->spinMetroidHudHpGaugeOffsetX);
    sSl("sHpGaugeOfsY",   ui->spinMetroidHudHpGaugeOffsetY);
    sC("cHpGaugeAnch",   ui->comboMetroidHudHpGaugeAnchor);
    sC("cHpGaugePosMode",ui->comboMetroidHudHpGaugePosMode);
    sSl("sHpGaugePosX",   ui->spinMetroidHudHpGaugePosX);
    sSl("sHpGaugePosY",   ui->spinMetroidHudHpGaugePosY);
    sB("cHpGaugeAutoClr",ui->cbMetroidHudHpGaugeAutoColor);
    sC("cHpGaugeClr", ui->comboMetroidHudHpGaugeColor);
    sE("eHpGaugeClr", ui->leMetroidHudHpGaugeColorCode);
    sI("sHpGaugeClrSpinR", ui->spinMetroidHudHpGaugeColorR);
    sI("sHpGaugeClrSpinG", ui->spinMetroidHudHpGaugeColorG);
    sI("sHpGaugeClrSpinB", ui->spinMetroidHudHpGaugeColorB);
    sCfgI("sHpGaugeClrR", "Metroid.Visual.HudHpGaugeColorR");
    sCfgI("sHpGaugeClrG", "Metroid.Visual.HudHpGaugeColorG");
    sCfgI("sHpGaugeClrB", "Metroid.Visual.HudHpGaugeColorB");
    // Ammo Gauge
    sB("cAmmoGauge",       ui->cbMetroidHudAmmoGauge);
    sC("cAmmoGaugeOrient", ui->comboMetroidHudAmmoGaugeOrientation);
    sSl("sAmmoGaugeLen",    ui->spinMetroidHudAmmoGaugeLength);
    sSl("sAmmoGaugeW",      ui->spinMetroidHudAmmoGaugeWidth);
    sSl("sAmmoGaugeOfsX",   ui->spinMetroidHudAmmoGaugeOffsetX);
    sSl("sAmmoGaugeOfsY",   ui->spinMetroidHudAmmoGaugeOffsetY);
    sC("cAmmoGaugeAnch",   ui->comboMetroidHudAmmoGaugeAnchor);
    sC("cAmmoGaugePosMode",ui->comboMetroidHudAmmoGaugePosMode);
    sSl("sAmmoGaugePosX",   ui->spinMetroidHudAmmoGaugePosX);
    sSl("sAmmoGaugePosY",   ui->spinMetroidHudAmmoGaugePosY);
    sC("cAmmoGaugeClr", ui->comboMetroidHudAmmoGaugeColor);
    sE("eAmmoGaugeClr", ui->leMetroidHudAmmoGaugeColorCode);
    sI("sAmmoGaugeClrSpinR", ui->spinMetroidHudAmmoGaugeColorR);
    sI("sAmmoGaugeClrSpinG", ui->spinMetroidHudAmmoGaugeColorG);
    sI("sAmmoGaugeClrSpinB", ui->spinMetroidHudAmmoGaugeColorB);
    sCfgI("sAmmoGaugeClrR", "Metroid.Visual.HudAmmoGaugeColorR");
    sCfgI("sAmmoGaugeClrG", "Metroid.Visual.HudAmmoGaugeColorG");
    sCfgI("sAmmoGaugeClrB", "Metroid.Visual.HudAmmoGaugeColorB");
    // HUD Radar
    sB("cRadarEnable", ui->cbMetroidBtmOverlayEnable);
    sSl("sRadarDstX", ui->spinMetroidBtmOverlayDstX);
    sSl("sRadarDstY", ui->spinMetroidBtmOverlayDstY);
    sSl("sRadarSize", ui->spinMetroidBtmOverlayDstSize);
    sD("dRadarOpacity", ui->spinMetroidBtmOverlayOpacity);
    sSl("sRadarSrcR",  ui->spinMetroidBtmOverlaySrcRadius);
    // Rank & Time HUD
    sB("cRankShow", ui->cbMetroidHudRankShow);
    sSl("sRankX", ui->spinMetroidHudRankX);
    sSl("sRankY", ui->spinMetroidHudRankY);
    sC("cRankAlign", ui->comboMetroidHudRankAlign);
    sE("eRankPrefix", ui->leMetroidHudRankPrefix);
    sB("cRankShowOrdinal", ui->cbMetroidHudRankShowOrdinal);
    sE("eRankSuffix", ui->leMetroidHudRankSuffix);
    sC("cRankColor", ui->comboMetroidHudRankColor);
    sE("eRankColorCode", ui->leMetroidHudRankColorCode);
    sI("sRankClrSpinR", ui->spinMetroidHudRankColorR);
    sI("sRankClrSpinG", ui->spinMetroidHudRankColorG);
    sI("sRankClrSpinB", ui->spinMetroidHudRankColorB);
    sCfgI("sRankClrR", "Metroid.Visual.HudRankColorR");
    sCfgI("sRankClrG", "Metroid.Visual.HudRankColorG");
    sCfgI("sRankClrB", "Metroid.Visual.HudRankColorB");
    sB("cTimeLeftShow", ui->cbMetroidHudTimeLeftShow);
    sSl("sTimeLeftX", ui->spinMetroidHudTimeLeftX);
    sSl("sTimeLeftY", ui->spinMetroidHudTimeLeftY);
    sC("cTimeLeftAlign", ui->comboMetroidHudTimeLeftAlign);
    sC("cTimeLeftColor", ui->comboMetroidHudTimeLeftColor);
    sE("eTimeLeftColorCode", ui->leMetroidHudTimeLeftColorCode);
    sI("sTimeLeftClrSpinR", ui->spinMetroidHudTimeLeftColorR);
    sI("sTimeLeftClrSpinG", ui->spinMetroidHudTimeLeftColorG);
    sI("sTimeLeftClrSpinB", ui->spinMetroidHudTimeLeftColorB);
    sCfgI("sTimeLeftClrR", "Metroid.Visual.HudTimeLeftColorR");
    sCfgI("sTimeLeftClrG", "Metroid.Visual.HudTimeLeftColorG");
    sCfgI("sTimeLeftClrB", "Metroid.Visual.HudTimeLeftColorB");
    sB("cTimeLimitShow", ui->cbMetroidHudTimeLimitShow);
    sSl("sTimeLimitX", ui->spinMetroidHudTimeLimitX);
    sSl("sTimeLimitY", ui->spinMetroidHudTimeLimitY);
    sC("cTimeLimitAlign", ui->comboMetroidHudTimeLimitAlign);
    sC("cTimeLimitColor", ui->comboMetroidHudTimeLimitColor);
    sE("eTimeLimitColorCode", ui->leMetroidHudTimeLimitColorCode);
    sI("sTimeLimitClrSpinR", ui->spinMetroidHudTimeLimitColorR);
    sI("sTimeLimitClrSpinG", ui->spinMetroidHudTimeLimitColorG);
    sI("sTimeLimitClrSpinB", ui->spinMetroidHudTimeLimitColorB);
    sCfgI("sTimeLimitClrR", "Metroid.Visual.HudTimeLimitColorR");
    sCfgI("sTimeLimitClrG", "Metroid.Visual.HudTimeLimitColorG");
    sCfgI("sTimeLimitClrB", "Metroid.Visual.HudTimeLimitColorB");
    // Bomb Left HUD
    sB("cBombLeftShow",     ui->cbMetroidHudBombLeftShow);
    sB("cBombLeftTextShow", ui->cbMetroidHudBombLeftTextShow);
    sSl("sBombLeftX", ui->spinMetroidHudBombLeftX);
    sSl("sBombLeftY", ui->spinMetroidHudBombLeftY);
    sC("cBombLeftAlign", ui->comboMetroidHudBombLeftAlign);
    sC("cBombLeftColor", ui->comboMetroidHudBombLeftColor);
    sE("eBombLeftColorCode", ui->leMetroidHudBombLeftColorCode);
    sI("sBombLeftClrSpinR", ui->spinMetroidHudBombLeftColorR);
    sI("sBombLeftClrSpinG", ui->spinMetroidHudBombLeftColorG);
    sI("sBombLeftClrSpinB", ui->spinMetroidHudBombLeftColorB);
    sCfgI("sBombLeftClrR", "Metroid.Visual.HudBombLeftColorR");
    sCfgI("sBombLeftClrG", "Metroid.Visual.HudBombLeftColorG");
    sCfgI("sBombLeftClrB", "Metroid.Visual.HudBombLeftColorB");
    sE("eBombLeftPrefix", ui->leMetroidHudBombLeftPrefix);
    sE("eBombLeftSuffix", ui->leMetroidHudBombLeftSuffix);
    sB("cBombLeftIconShow",         ui->cbMetroidHudBombLeftIconShow);
    sB("cBombLeftIconColorOverlay", ui->cbMetroidHudBombLeftIconColorOverlay);
    sC("cBombLeftIconColor",        ui->comboMetroidHudBombLeftIconColor);
    sE("eBombLeftIconColorCode",    ui->leMetroidHudBombLeftIconColorCode);
    sI("sBombLeftIconClrSpinR",     ui->spinMetroidHudBombLeftIconColorR);
    sI("sBombLeftIconClrSpinG",     ui->spinMetroidHudBombLeftIconColorG);
    sI("sBombLeftIconClrSpinB",     ui->spinMetroidHudBombLeftIconColorB);
    sCfgI("sBombLeftIconClrR", "Metroid.Visual.HudBombLeftIconColorR");
    sCfgI("sBombLeftIconClrG", "Metroid.Visual.HudBombLeftIconColorG");
    sCfgI("sBombLeftIconClrB", "Metroid.Visual.HudBombLeftIconColorB");
    sC("cBombLeftIconMode",         ui->comboMetroidHudBombLeftIconMode);
    sSl("sBombLeftIconOfsX",        ui->spinMetroidHudBombLeftIconOfsX);
    sSl("sBombLeftIconOfsY",        ui->spinMetroidHudBombLeftIconOfsY);
    sSl("sBombLeftIconPosX",        ui->spinMetroidHudBombLeftIconPosX);
    sSl("sBombLeftIconPosY",        ui->spinMetroidHudBombLeftIconPosY);
    sC("cBombLeftIconAnchorX",      ui->comboMetroidHudBombLeftIconAnchorX);
    sC("cBombLeftIconAnchorY",      ui->comboMetroidHudBombLeftIconAnchorY);
    // Crosshair
    sCfgI("sChR", "Metroid.Visual.CrosshairColorR");
    sCfgI("sChG", "Metroid.Visual.CrosshairColorG");
    sCfgI("sChB", "Metroid.Visual.CrosshairColorB");
    sB("cChOutline",  ui->cbMetroidCrosshairOutline);
    sD("dChOlOp",     ui->spinMetroidCrosshairOutlineOpacity);
    sI("sChOlThick",  ui->spinMetroidCrosshairOutlineThickness);
    sB("cChDot",      ui->cbMetroidCrosshairCenterDot);
    sD("dChDotOp",    ui->spinMetroidCrosshairDotOpacity);
    sI("sChDotThick", ui->spinMetroidCrosshairDotThickness);
    sB("cChTStyle",   ui->cbMetroidCrosshairTStyle);
    sB("cChInnerShow",  ui->cbMetroidCrosshairInnerShow);
    sD("dChInnerOp",    ui->spinMetroidCrosshairInnerOpacity);
    sI("sChInnerLX",    ui->spinMetroidCrosshairInnerLengthX);
    sI("sChInnerLY",    ui->spinMetroidCrosshairInnerLengthY);
    sI("sChInnerThick", ui->spinMetroidCrosshairInnerThickness);
    sI("sChInnerOfs",   ui->spinMetroidCrosshairInnerOffset);
    sB("cChInnerLink",  ui->cbMetroidCrosshairInnerLinkXY);
    sB("cChOuterShow",  ui->cbMetroidCrosshairOuterShow);
    sD("dChOuterOp",    ui->spinMetroidCrosshairOuterOpacity);
    sI("sChOuterLX",    ui->spinMetroidCrosshairOuterLengthX);
    sI("sChOuterLY",    ui->spinMetroidCrosshairOuterLengthY);
    sI("sChOuterThick", ui->spinMetroidCrosshairOuterThickness);
    sI("sChOuterOfs",   ui->spinMetroidCrosshairOuterOffset);
    sB("cChOuterLink",  ui->cbMetroidCrosshairOuterLinkXY);
}

void MelonPrimeInputConfig::restoreVisualSnapshot()
{
    if (m_visualSnapshot.isEmpty()) return;
    m_applyPreviewEnabled = false;

    const QVariantMap& s = m_visualSnapshot;
    auto rI = [&](const char* k, QSpinBox* w) {
        auto it = s.find(k); if (it == s.end()) return;
        w->blockSignals(true); w->setValue(it->toInt()); w->blockSignals(false);
    };
    auto rSl = [&](const char* k, QSlider* w, QSpinBox* input, QLabel* lbl) {
        auto it = s.find(k); if (it == s.end()) return;
        int v = it->toInt();
        w->blockSignals(true); w->setValue(v); w->blockSignals(false);
        if (input) {
            input->blockSignals(true);
            input->setValue(v);
            input->blockSignals(false);
        }
        if (lbl) lbl->setText(QString::number(v));
    };
    auto rD = [&](const char* k, QDoubleSpinBox* w) {
        auto it = s.find(k); if (it == s.end()) return;
        w->blockSignals(true); w->setValue(it->toDouble()); w->blockSignals(false);
    };
    auto rB = [&](const char* k, QCheckBox* w) {
        auto it = s.find(k); if (it == s.end()) return;
        w->blockSignals(true); w->setChecked(it->toBool()); w->blockSignals(false);
    };
    auto rC = [&](const char* k, QComboBox* w) {
        auto it = s.find(k); if (it == s.end()) return;
        w->blockSignals(true); w->setCurrentIndex(it->toInt()); w->blockSignals(false);
    };
    auto rE = [&](const char* k, QLineEdit* w) {
        auto it = s.find(k); if (it == s.end()) return;
        w->blockSignals(true); w->setText(it->toString()); w->blockSignals(false);
    };

    rB("cCustomHud",       ui->cbMetroidEnableCustomHud);
    rB("cAspectRatio",     ui->cbMetroidInGameAspectRatio);
    rC("cAspectRatioMode", ui->comboMetroidInGameAspectRatioMode);
    // Match Status
    rB("cMatchShow",  ui->cbMetroidHudMatchStatusShow);
    rSl("sMatchX",     ui->spinMetroidHudMatchStatusX,    ui->inputMetroidHudMatchStatusX,    ui->labelMetroidHudMatchStatusX);
    rSl("sMatchY",     ui->spinMetroidHudMatchStatusY,    ui->inputMetroidHudMatchStatusY,    ui->labelMetroidHudMatchStatusY);
    rSl("sMatchLOfsX", ui->spinMetroidHudMatchStatusLabelOfsX, ui->inputMetroidHudMatchStatusLabelOfsX, ui->labelMetroidHudMatchStatusLabelOfsX);
    rSl("sMatchLOfsY", ui->spinMetroidHudMatchStatusLabelOfsY, ui->inputMetroidHudMatchStatusLabelOfsY, ui->labelMetroidHudMatchStatusLabelOfsY);
    rC("cMatchLPos",  ui->comboMetroidHudMatchStatusLabelPos);
    rE("eMatchLP",    ui->leMetroidHudMatchStatusLabelPoints);
    rE("eMatchLO",    ui->leMetroidHudMatchStatusLabelOctoliths);
    rE("eMatchLL",    ui->leMetroidHudMatchStatusLabelLives);
    rE("eMatchLR",    ui->leMetroidHudMatchStatusLabelRingTime);
    rE("eMatchLPT",   ui->leMetroidHudMatchStatusLabelPrimeTime);
    rC("cMatchClr", ui->comboMetroidHudMatchStatusColor);
    rE("eMatchClr", ui->leMetroidHudMatchStatusColorCode);
    rI("sMatchClrSpinR", ui->spinMetroidHudMatchStatusColorR);
    rI("sMatchClrSpinG", ui->spinMetroidHudMatchStatusColorG);
    rI("sMatchClrSpinB", ui->spinMetroidHudMatchStatusColorB);
    rC("cMatchLblClr", ui->comboMetroidHudMatchStatusLabelColor);
    rE("eMatchLblClr", ui->leMetroidHudMatchStatusLabelColorCode);
    rI("sMatchLblClrSpinR", ui->spinMetroidHudMatchStatusLabelColorR);
    rI("sMatchLblClrSpinG", ui->spinMetroidHudMatchStatusLabelColorG);
    rI("sMatchLblClrSpinB", ui->spinMetroidHudMatchStatusLabelColorB);
    rC("cMatchValClr", ui->comboMetroidHudMatchStatusValueColor);
    rE("eMatchValClr", ui->leMetroidHudMatchStatusValueColorCode);
    rI("sMatchValClrSpinR", ui->spinMetroidHudMatchStatusValueColorR);
    rI("sMatchValClrSpinG", ui->spinMetroidHudMatchStatusValueColorG);
    rI("sMatchValClrSpinB", ui->spinMetroidHudMatchStatusValueColorB);
    rC("cMatchSepClr", ui->comboMetroidHudMatchStatusSepColor);
    rE("eMatchSepClr", ui->leMetroidHudMatchStatusSepColorCode);
    rI("sMatchSepClrSpinR", ui->spinMetroidHudMatchStatusSepColorR);
    rI("sMatchSepClrSpinG", ui->spinMetroidHudMatchStatusSepColorG);
    rI("sMatchSepClrSpinB", ui->spinMetroidHudMatchStatusSepColorB);
    rC("cMatchGolClr", ui->comboMetroidHudMatchStatusGoalColor);
    rE("eMatchGolClr", ui->leMetroidHudMatchStatusGoalColorCode);
    rI("sMatchGolClrSpinR", ui->spinMetroidHudMatchStatusGoalColorR);
    rI("sMatchGolClrSpinG", ui->spinMetroidHudMatchStatusGoalColorG);
    rI("sMatchGolClrSpinB", ui->spinMetroidHudMatchStatusGoalColorB);
    // HP/Weapon
    rSl("sHpX",  ui->spinMetroidHudHpX,  ui->inputMetroidHudHpX,  ui->labelMetroidHudHpX);
    rSl("sHpY",  ui->spinMetroidHudHpY,  ui->inputMetroidHudHpY,  ui->labelMetroidHudHpY);
    rE("eHpPfx", ui->leMetroidHudHpPrefix);
    rC("cHpAlign", ui->comboMetroidHudHpAlign);
    rB("cHpTxtAuto", ui->cbMetroidHudHpTextAutoColor);
    rC("cHpTxtClr", ui->comboMetroidHudHpTextColor);
    rI("sHpTxtClrR", ui->spinMetroidHudHpTextColorR);
    rI("sHpTxtClrG", ui->spinMetroidHudHpTextColorG);
    rI("sHpTxtClrB", ui->spinMetroidHudHpTextColorB);
    rE("eHpTxtClr",  ui->leMetroidHudHpTextColorCode);
    rSl("sWpnX", ui->spinMetroidHudWeaponX, ui->inputMetroidHudWeaponX, ui->labelMetroidHudWeaponX);
    rSl("sWpnY", ui->spinMetroidHudWeaponY, ui->inputMetroidHudWeaponY, ui->labelMetroidHudWeaponY);
    rE("eAmmoPfx", ui->leMetroidHudAmmoPrefix);
    rC("cAmmoAlign", ui->comboMetroidHudAmmoAlign);
    rC("cAmmoTxtClr", ui->comboMetroidHudAmmoTextColor);
    rI("sAmmoTxtClrR", ui->spinMetroidHudAmmoTextColorR);
    rI("sAmmoTxtClrG", ui->spinMetroidHudAmmoTextColorG);
    rI("sAmmoTxtClrB", ui->spinMetroidHudAmmoTextColorB);
    rE("eAmmoTxtClr",  ui->leMetroidHudAmmoTextColorCode);
    rC("cHpPos",  ui->comboMetroidHudHpPosition);
    rC("cWpnPos", ui->comboMetroidHudWeaponPosition);
    rB("cWpnIconShow",  ui->cbMetroidHudWeaponIconShow);
    rC("cWpnIconMode",  ui->comboMetroidHudWeaponIconMode);
    rSl("sWpnIconOfsX",  ui->spinMetroidHudWeaponIconOffsetX, ui->inputMetroidHudWeaponIconOffsetX, ui->labelMetroidHudWeaponIconOffsetX);
    rSl("sWpnIconOfsY",  ui->spinMetroidHudWeaponIconOffsetY, ui->inputMetroidHudWeaponIconOffsetY, ui->labelMetroidHudWeaponIconOffsetY);
    rSl("sWpnIconPosX",  ui->spinMetroidHudWeaponIconPosX,    ui->inputMetroidHudWeaponIconPosX,    ui->labelMetroidHudWeaponIconPosX);
    rSl("sWpnIconPosY",  ui->spinMetroidHudWeaponIconPosY,    ui->inputMetroidHudWeaponIconPosY,    ui->labelMetroidHudWeaponIconPosY);
    rC("cWpnIconPos",    ui->comboMetroidHudWeaponIconPosition);
    rC("cWpnIconAnchX",  ui->comboMetroidHudWeaponIconAnchorX);
    rC("cWpnIconAnchY",  ui->comboMetroidHudWeaponIconAnchorY);
    rB("cWpnIconClrOv",  ui->cbMetroidHudWeaponIconColorOverlay);
    // HP Gauge
    rB("cHpGauge",       ui->cbMetroidHudHpGauge);
    rC("cHpGaugeOrient", ui->comboMetroidHudHpGaugeOrientation);
    rSl("sHpGaugeLen",   ui->spinMetroidHudHpGaugeLength,  ui->inputMetroidHudHpGaugeLength,  ui->labelMetroidHudHpGaugeLength);
    rSl("sHpGaugeW",     ui->spinMetroidHudHpGaugeWidth,   ui->inputMetroidHudHpGaugeWidth,   ui->labelMetroidHudHpGaugeWidth);
    rSl("sHpGaugeOfsX",  ui->spinMetroidHudHpGaugeOffsetX, ui->inputMetroidHudHpGaugeOffsetX, ui->labelMetroidHudHpGaugeOffsetX);
    rSl("sHpGaugeOfsY",  ui->spinMetroidHudHpGaugeOffsetY, ui->inputMetroidHudHpGaugeOffsetY, ui->labelMetroidHudHpGaugeOffsetY);
    rC("cHpGaugeAnch",   ui->comboMetroidHudHpGaugeAnchor);
    rC("cHpGaugePosMode",ui->comboMetroidHudHpGaugePosMode);
    rSl("sHpGaugePosX",  ui->spinMetroidHudHpGaugePosX,    ui->inputMetroidHudHpGaugePosX,    ui->labelMetroidHudHpGaugePosX);
    rSl("sHpGaugePosY",  ui->spinMetroidHudHpGaugePosY,    ui->inputMetroidHudHpGaugePosY,    ui->labelMetroidHudHpGaugePosY);
    rB("cHpGaugeAutoClr",ui->cbMetroidHudHpGaugeAutoColor);
    rC("cHpGaugeClr", ui->comboMetroidHudHpGaugeColor);
    rE("eHpGaugeClr", ui->leMetroidHudHpGaugeColorCode);
    rI("sHpGaugeClrSpinR", ui->spinMetroidHudHpGaugeColorR);
    rI("sHpGaugeClrSpinG", ui->spinMetroidHudHpGaugeColorG);
    rI("sHpGaugeClrSpinB", ui->spinMetroidHudHpGaugeColorB);
    // Ammo Gauge
    rB("cAmmoGauge",       ui->cbMetroidHudAmmoGauge);
    rC("cAmmoGaugeOrient", ui->comboMetroidHudAmmoGaugeOrientation);
    rSl("sAmmoGaugeLen",   ui->spinMetroidHudAmmoGaugeLength,  ui->inputMetroidHudAmmoGaugeLength,  ui->labelMetroidHudAmmoGaugeLength);
    rSl("sAmmoGaugeW",     ui->spinMetroidHudAmmoGaugeWidth,   ui->inputMetroidHudAmmoGaugeWidth,   ui->labelMetroidHudAmmoGaugeWidth);
    rSl("sAmmoGaugeOfsX",  ui->spinMetroidHudAmmoGaugeOffsetX, ui->inputMetroidHudAmmoGaugeOffsetX, ui->labelMetroidHudAmmoGaugeOffsetX);
    rSl("sAmmoGaugeOfsY",  ui->spinMetroidHudAmmoGaugeOffsetY, ui->inputMetroidHudAmmoGaugeOffsetY, ui->labelMetroidHudAmmoGaugeOffsetY);
    rC("cAmmoGaugeAnch",   ui->comboMetroidHudAmmoGaugeAnchor);
    rC("cAmmoGaugePosMode",ui->comboMetroidHudAmmoGaugePosMode);
    rSl("sAmmoGaugePosX",  ui->spinMetroidHudAmmoGaugePosX,    ui->inputMetroidHudAmmoGaugePosX,    ui->labelMetroidHudAmmoGaugePosX);
    rSl("sAmmoGaugePosY",  ui->spinMetroidHudAmmoGaugePosY,    ui->inputMetroidHudAmmoGaugePosY,    ui->labelMetroidHudAmmoGaugePosY);
    rC("cAmmoGaugeClr", ui->comboMetroidHudAmmoGaugeColor);
    rE("eAmmoGaugeClr", ui->leMetroidHudAmmoGaugeColorCode);
    rI("sAmmoGaugeClrSpinR", ui->spinMetroidHudAmmoGaugeColorR);
    rI("sAmmoGaugeClrSpinG", ui->spinMetroidHudAmmoGaugeColorG);
    rI("sAmmoGaugeClrSpinB", ui->spinMetroidHudAmmoGaugeColorB);
    // HUD Radar
    rB("cRadarEnable", ui->cbMetroidBtmOverlayEnable);
    rSl("sRadarDstX", ui->spinMetroidBtmOverlayDstX, ui->inputMetroidBtmOverlayDstX, ui->labelMetroidBtmOverlayDstX);
    rSl("sRadarDstY", ui->spinMetroidBtmOverlayDstY, ui->inputMetroidBtmOverlayDstY, ui->labelMetroidBtmOverlayDstY);
    rSl("sRadarSize", ui->spinMetroidBtmOverlayDstSize, ui->inputMetroidBtmOverlayDstSize, ui->labelMetroidBtmOverlayDstSize);
    rD("dRadarOpacity", ui->spinMetroidBtmOverlayOpacity);
    ui->sliderMetroidBtmOverlayOpacity->blockSignals(true);
    ui->sliderMetroidBtmOverlayOpacity->setValue(qRound(ui->spinMetroidBtmOverlayOpacity->value() * 100));
    ui->sliderMetroidBtmOverlayOpacity->blockSignals(false);
    rSl("sRadarSrcR",  ui->spinMetroidBtmOverlaySrcRadius,  ui->inputMetroidBtmOverlaySrcRadius,  ui->labelMetroidBtmOverlaySrcRadius);
    // Rank & Time HUD
    rB("cRankShow", ui->cbMetroidHudRankShow);
    rSl("sRankX", ui->spinMetroidHudRankX, ui->inputMetroidHudRankX, nullptr);
    rSl("sRankY", ui->spinMetroidHudRankY, ui->inputMetroidHudRankY, nullptr);
    rC("cRankAlign", ui->comboMetroidHudRankAlign);
    rE("eRankPrefix", ui->leMetroidHudRankPrefix);
    rB("cRankShowOrdinal", ui->cbMetroidHudRankShowOrdinal);
    rE("eRankSuffix", ui->leMetroidHudRankSuffix);
    rC("cRankColor", ui->comboMetroidHudRankColor);
    rE("eRankColorCode", ui->leMetroidHudRankColorCode);
    rI("sRankClrSpinR", ui->spinMetroidHudRankColorR);
    rI("sRankClrSpinG", ui->spinMetroidHudRankColorG);
    rI("sRankClrSpinB", ui->spinMetroidHudRankColorB);
    rB("cTimeLeftShow", ui->cbMetroidHudTimeLeftShow);
    rSl("sTimeLeftX", ui->spinMetroidHudTimeLeftX, ui->inputMetroidHudTimeLeftX, nullptr);
    rSl("sTimeLeftY", ui->spinMetroidHudTimeLeftY, ui->inputMetroidHudTimeLeftY, nullptr);
    rC("cTimeLeftAlign", ui->comboMetroidHudTimeLeftAlign);
    rC("cTimeLeftColor", ui->comboMetroidHudTimeLeftColor);
    rE("eTimeLeftColorCode", ui->leMetroidHudTimeLeftColorCode);
    rI("sTimeLeftClrSpinR", ui->spinMetroidHudTimeLeftColorR);
    rI("sTimeLeftClrSpinG", ui->spinMetroidHudTimeLeftColorG);
    rI("sTimeLeftClrSpinB", ui->spinMetroidHudTimeLeftColorB);
    rB("cTimeLimitShow", ui->cbMetroidHudTimeLimitShow);
    rSl("sTimeLimitX", ui->spinMetroidHudTimeLimitX, ui->inputMetroidHudTimeLimitX, nullptr);
    rSl("sTimeLimitY", ui->spinMetroidHudTimeLimitY, ui->inputMetroidHudTimeLimitY, nullptr);
    rC("cTimeLimitAlign", ui->comboMetroidHudTimeLimitAlign);
    rC("cTimeLimitColor", ui->comboMetroidHudTimeLimitColor);
    rE("eTimeLimitColorCode", ui->leMetroidHudTimeLimitColorCode);
    rI("sTimeLimitClrSpinR", ui->spinMetroidHudTimeLimitColorR);
    rI("sTimeLimitClrSpinG", ui->spinMetroidHudTimeLimitColorG);
    rI("sTimeLimitClrSpinB", ui->spinMetroidHudTimeLimitColorB);
    // Bomb Left HUD
    rB("cBombLeftShow",     ui->cbMetroidHudBombLeftShow);
    rB("cBombLeftTextShow", ui->cbMetroidHudBombLeftTextShow);
    rSl("sBombLeftX", ui->spinMetroidHudBombLeftX, ui->inputMetroidHudBombLeftX, nullptr);
    rSl("sBombLeftY", ui->spinMetroidHudBombLeftY, ui->inputMetroidHudBombLeftY, nullptr);
    rC("cBombLeftAlign", ui->comboMetroidHudBombLeftAlign);
    rC("cBombLeftColor", ui->comboMetroidHudBombLeftColor);
    rE("eBombLeftColorCode", ui->leMetroidHudBombLeftColorCode);
    rI("sBombLeftClrSpinR", ui->spinMetroidHudBombLeftColorR);
    rI("sBombLeftClrSpinG", ui->spinMetroidHudBombLeftColorG);
    rI("sBombLeftClrSpinB", ui->spinMetroidHudBombLeftColorB);
    rE("eBombLeftPrefix", ui->leMetroidHudBombLeftPrefix);
    rE("eBombLeftSuffix", ui->leMetroidHudBombLeftSuffix);
    rB("cBombLeftIconShow",         ui->cbMetroidHudBombLeftIconShow);
    rB("cBombLeftIconColorOverlay", ui->cbMetroidHudBombLeftIconColorOverlay);
    rC("cBombLeftIconColor",        ui->comboMetroidHudBombLeftIconColor);
    rE("eBombLeftIconColorCode",    ui->leMetroidHudBombLeftIconColorCode);
    rI("sBombLeftIconClrSpinR",     ui->spinMetroidHudBombLeftIconColorR);
    rI("sBombLeftIconClrSpinG",     ui->spinMetroidHudBombLeftIconColorG);
    rI("sBombLeftIconClrSpinB",     ui->spinMetroidHudBombLeftIconColorB);
    rC("cBombLeftIconMode",         ui->comboMetroidHudBombLeftIconMode);
    rSl("sBombLeftIconOfsX", ui->spinMetroidHudBombLeftIconOfsX, ui->inputMetroidHudBombLeftIconOfsX, ui->labelMetroidHudBombLeftIconOfsX);
    rSl("sBombLeftIconOfsY", ui->spinMetroidHudBombLeftIconOfsY, ui->inputMetroidHudBombLeftIconOfsY, ui->labelMetroidHudBombLeftIconOfsY);
    rSl("sBombLeftIconPosX", ui->spinMetroidHudBombLeftIconPosX, ui->inputMetroidHudBombLeftIconPosX, ui->labelMetroidHudBombLeftIconPosX);
    rSl("sBombLeftIconPosY", ui->spinMetroidHudBombLeftIconPosY, ui->inputMetroidHudBombLeftIconPosY, ui->labelMetroidHudBombLeftIconPosY);
    rC("cBombLeftIconAnchorX",      ui->comboMetroidHudBombLeftIconAnchorX);
    rC("cBombLeftIconAnchorY",      ui->comboMetroidHudBombLeftIconAnchorY);
    // Crosshair
    rI("sChR", ui->spinMetroidCrosshairR);
    rI("sChG", ui->spinMetroidCrosshairG);
    rI("sChB", ui->spinMetroidCrosshairB);
    rB("cChOutline",  ui->cbMetroidCrosshairOutline);
    rD("dChOlOp",     ui->spinMetroidCrosshairOutlineOpacity);
    rI("sChOlThick",  ui->spinMetroidCrosshairOutlineThickness);
    rB("cChDot",      ui->cbMetroidCrosshairCenterDot);
    rD("dChDotOp",    ui->spinMetroidCrosshairDotOpacity);
    rI("sChDotThick", ui->spinMetroidCrosshairDotThickness);
    rB("cChTStyle",   ui->cbMetroidCrosshairTStyle);
    rB("cChInnerShow",  ui->cbMetroidCrosshairInnerShow);
    rD("dChInnerOp",    ui->spinMetroidCrosshairInnerOpacity);
    rI("sChInnerLX",    ui->spinMetroidCrosshairInnerLengthX);
    rI("sChInnerLY",    ui->spinMetroidCrosshairInnerLengthY);
    rI("sChInnerThick", ui->spinMetroidCrosshairInnerThickness);
    rI("sChInnerOfs",   ui->spinMetroidCrosshairInnerOffset);
    rB("cChInnerLink",  ui->cbMetroidCrosshairInnerLinkXY);
    rB("cChOuterShow",  ui->cbMetroidCrosshairOuterShow);
    rD("dChOuterOp",    ui->spinMetroidCrosshairOuterOpacity);
    rI("sChOuterLX",    ui->spinMetroidCrosshairOuterLengthX);
    rI("sChOuterLY",    ui->spinMetroidCrosshairOuterLengthY);
    rI("sChOuterThick", ui->spinMetroidCrosshairOuterThickness);
    rI("sChOuterOfs",   ui->spinMetroidCrosshairOuterOffset);
    rB("cChOuterLink",  ui->cbMetroidCrosshairOuterLinkXY);
    // Sync crosshair sliders to their restored spinbox values
    {
        auto syncSl = [](QSlider* sl, QSpinBox* sb, QLabel* lbl) {
            sl->blockSignals(true); sl->setValue(sb->value()); sl->blockSignals(false);
            lbl->setText(QString::number(sb->value()));
        };
        syncSl(ui->sliderMetroidCrosshairOutlineThickness, ui->spinMetroidCrosshairOutlineThickness, ui->labelMetroidCrosshairOutlineThickness);
        syncSl(ui->sliderMetroidCrosshairDotThickness,     ui->spinMetroidCrosshairDotThickness,     ui->labelMetroidCrosshairDotThickness);
        syncSl(ui->sliderMetroidCrosshairInnerLengthX,     ui->spinMetroidCrosshairInnerLengthX,     ui->labelMetroidCrosshairInnerLengthX);
        syncSl(ui->sliderMetroidCrosshairInnerLengthY,     ui->spinMetroidCrosshairInnerLengthY,     ui->labelMetroidCrosshairInnerLengthY);
        syncSl(ui->sliderMetroidCrosshairInnerThickness,   ui->spinMetroidCrosshairInnerThickness,   ui->labelMetroidCrosshairInnerThickness);
        syncSl(ui->sliderMetroidCrosshairInnerOffset,      ui->spinMetroidCrosshairInnerOffset,      ui->labelMetroidCrosshairInnerOffset);
        syncSl(ui->sliderMetroidCrosshairOuterLengthX,     ui->spinMetroidCrosshairOuterLengthX,     ui->labelMetroidCrosshairOuterLengthX);
        syncSl(ui->sliderMetroidCrosshairOuterLengthY,     ui->spinMetroidCrosshairOuterLengthY,     ui->labelMetroidCrosshairOuterLengthY);
        syncSl(ui->sliderMetroidCrosshairOuterThickness,   ui->spinMetroidCrosshairOuterThickness,   ui->labelMetroidCrosshairOuterThickness);
        syncSl(ui->sliderMetroidCrosshairOuterOffset,      ui->spinMetroidCrosshairOuterOffset,      ui->labelMetroidCrosshairOuterOffset);
    }

    // Restore color values from snapshot into config and update button backgrounds
    {
        Config::Table& instcfg = emuInstance->getLocalConfig();
        auto restoreColor = [&](QPushButton* btn, const char* kR, const char* kG, const char* kB,
                                const char* cfgR, const char* cfgG, const char* cfgB) {
            auto itR = s.find(kR), itG = s.find(kG), itB = s.find(kB);
            if (itR == s.end()) return;
            int r = itR->toInt(), g = itG->toInt(), b = itB->toInt();
            instcfg.SetInt(cfgR, r); instcfg.SetInt(cfgG, g); instcfg.SetInt(cfgB, b);
            btn->setStyleSheet(QString("background-color: %1;").arg(QColor(r, g, b).name()));
        };
        restoreColor(ui->btnMetroidCrosshairColor, "sChR", "sChG", "sChB",
            "Metroid.Visual.CrosshairColorR", "Metroid.Visual.CrosshairColorG", "Metroid.Visual.CrosshairColorB");
        restoreColor(ui->btnMetroidHudHpTextColor, "sCfgHpTxtClrR", "sCfgHpTxtClrG", "sCfgHpTxtClrB",
            "Metroid.Visual.HudHpTextColorR", "Metroid.Visual.HudHpTextColorG", "Metroid.Visual.HudHpTextColorB");
        restoreColor(ui->btnMetroidHudAmmoTextColor, "sCfgAmmoTxtClrR", "sCfgAmmoTxtClrG", "sCfgAmmoTxtClrB",
            "Metroid.Visual.HudAmmoTextColorR", "Metroid.Visual.HudAmmoTextColorG", "Metroid.Visual.HudAmmoTextColorB");
        restoreColor(ui->btnMetroidHudHpGaugeColor, "sHpGaugeClrR", "sHpGaugeClrG", "sHpGaugeClrB",
            "Metroid.Visual.HudHpGaugeColorR", "Metroid.Visual.HudHpGaugeColorG", "Metroid.Visual.HudHpGaugeColorB");
        restoreColor(ui->btnMetroidHudAmmoGaugeColor, "sAmmoGaugeClrR", "sAmmoGaugeClrG", "sAmmoGaugeClrB",
            "Metroid.Visual.HudAmmoGaugeColorR", "Metroid.Visual.HudAmmoGaugeColorG", "Metroid.Visual.HudAmmoGaugeColorB");
        restoreColor(ui->btnMetroidHudMatchStatusColor, "sMatchClrR", "sMatchClrG", "sMatchClrB",
            "Metroid.Visual.HudMatchStatusColorR", "Metroid.Visual.HudMatchStatusColorG", "Metroid.Visual.HudMatchStatusColorB");
        restoreColor(ui->btnMetroidHudMatchStatusLabelColor, "sMatchLblClrR", "sMatchLblClrG", "sMatchLblClrB",
            "Metroid.Visual.HudMatchStatusLabelColorR", "Metroid.Visual.HudMatchStatusLabelColorG", "Metroid.Visual.HudMatchStatusLabelColorB");
        restoreColor(ui->btnMetroidHudMatchStatusValueColor, "sMatchValClrR", "sMatchValClrG", "sMatchValClrB",
            "Metroid.Visual.HudMatchStatusValueColorR", "Metroid.Visual.HudMatchStatusValueColorG", "Metroid.Visual.HudMatchStatusValueColorB");
        restoreColor(ui->btnMetroidHudMatchStatusSepColor, "sMatchSepClrR", "sMatchSepClrG", "sMatchSepClrB",
            "Metroid.Visual.HudMatchStatusSepColorR", "Metroid.Visual.HudMatchStatusSepColorG", "Metroid.Visual.HudMatchStatusSepColorB");
        restoreColor(ui->btnMetroidHudMatchStatusGoalColor, "sMatchGolClrR", "sMatchGolClrG", "sMatchGolClrB",
            "Metroid.Visual.HudMatchStatusGoalColorR", "Metroid.Visual.HudMatchStatusGoalColorG", "Metroid.Visual.HudMatchStatusGoalColorB");
        restoreColor(ui->btnMetroidHudRankColor, "sRankClrR", "sRankClrG", "sRankClrB",
            "Metroid.Visual.HudRankColorR", "Metroid.Visual.HudRankColorG", "Metroid.Visual.HudRankColorB");
        restoreColor(ui->btnMetroidHudTimeLeftColor, "sTimeLeftClrR", "sTimeLeftClrG", "sTimeLeftClrB",
            "Metroid.Visual.HudTimeLeftColorR", "Metroid.Visual.HudTimeLeftColorG", "Metroid.Visual.HudTimeLeftColorB");
        restoreColor(ui->btnMetroidHudTimeLimitColor, "sTimeLimitClrR", "sTimeLimitClrG", "sTimeLimitClrB",
            "Metroid.Visual.HudTimeLimitColorR", "Metroid.Visual.HudTimeLimitColorG", "Metroid.Visual.HudTimeLimitColorB");
        restoreColor(ui->btnMetroidHudBombLeftColor, "sBombLeftClrR", "sBombLeftClrG", "sBombLeftClrB",
            "Metroid.Visual.HudBombLeftColorR", "Metroid.Visual.HudBombLeftColorG", "Metroid.Visual.HudBombLeftColorB");
        restoreColor(ui->btnMetroidHudBombLeftIconColor, "sBombLeftIconClrR", "sBombLeftIconClrG", "sBombLeftIconClrB",
            "Metroid.Visual.HudBombLeftIconColorR", "Metroid.Visual.HudBombLeftIconColorG", "Metroid.Visual.HudBombLeftIconColorB");
    }

    m_applyPreviewEnabled = true;
    applyVisualPreview();
}

void MelonPrimeInputConfig::applyVisualPreview()
{
#ifdef MELONPRIME_CUSTOM_HUD
    if (!m_applyPreviewEnabled || m_applyPreviewActive) return;
    m_applyPreviewActive = true;

    Config::Table& instcfg = emuInstance->getLocalConfig();
    instcfg.SetBool("Metroid.Visual.CustomHUD",              ui->cbMetroidEnableCustomHud->isChecked());
    instcfg.SetBool("Metroid.Visual.InGameAspectRatio",      ui->cbMetroidInGameAspectRatio->isChecked());
    instcfg.SetInt ("Metroid.Visual.InGameAspectRatioMode",  ui->comboMetroidInGameAspectRatioMode->currentIndex());
    instcfg.SetBool("Metroid.Visual.ClipCursorToBottomScreenWhenNotInGame", ui->cbMetroidClipCursorToBottomScreenWhenNotInGame->isChecked());

    m_applyPreviewActive = false;
    applyAndPreviewMatchStatus();
    applyAndPreviewHpAmmo();
    applyAndPreviewCrosshair();
    applyAndPreviewRadar();
#endif
}

void MelonPrimeInputConfig::applyAndPreviewMatchStatus()
{
#ifdef MELONPRIME_CUSTOM_HUD
    if (!m_applyPreviewEnabled || m_applyPreviewActive) return;
    m_applyPreviewActive = true;
    Config::Table& instcfg = emuInstance->getLocalConfig();
    instcfg.SetBool("Metroid.Visual.HudMatchStatusShow",     ui->cbMetroidHudMatchStatusShow->isChecked());
    instcfg.SetInt ("Metroid.Visual.HudMatchStatusX",        ui->spinMetroidHudMatchStatusX->value());
    instcfg.SetInt ("Metroid.Visual.HudMatchStatusY",        ui->spinMetroidHudMatchStatusY->value());
    instcfg.SetInt ("Metroid.Visual.HudMatchStatusLabelOfsX",ui->spinMetroidHudMatchStatusLabelOfsX->value());
    instcfg.SetInt ("Metroid.Visual.HudMatchStatusLabelOfsY",ui->spinMetroidHudMatchStatusLabelOfsY->value());
    instcfg.SetInt ("Metroid.Visual.HudMatchStatusLabelPos", ui->comboMetroidHudMatchStatusLabelPos->currentIndex());
    instcfg.SetString("Metroid.Visual.HudMatchStatusLabelPoints",    ui->leMetroidHudMatchStatusLabelPoints->text().toStdString());
    instcfg.SetString("Metroid.Visual.HudMatchStatusLabelOctoliths", ui->leMetroidHudMatchStatusLabelOctoliths->text().toStdString());
    instcfg.SetString("Metroid.Visual.HudMatchStatusLabelLives",     ui->leMetroidHudMatchStatusLabelLives->text().toStdString());
    instcfg.SetString("Metroid.Visual.HudMatchStatusLabelRingTime",  ui->leMetroidHudMatchStatusLabelRingTime->text().toStdString());
    instcfg.SetString("Metroid.Visual.HudMatchStatusLabelPrimeTime", ui->leMetroidHudMatchStatusLabelPrimeTime->text().toStdString());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusColorR", ui->spinMetroidHudMatchStatusColorR->value());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusColorG", ui->spinMetroidHudMatchStatusColorG->value());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusColorB", ui->spinMetroidHudMatchStatusColorB->value());
    instcfg.SetBool("Metroid.Visual.HudMatchStatusLabelColorOverall", ui->comboMetroidHudMatchStatusLabelColor->currentIndex() == 0);
    instcfg.SetInt("Metroid.Visual.HudMatchStatusLabelColorR", ui->spinMetroidHudMatchStatusLabelColorR->value());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusLabelColorG", ui->spinMetroidHudMatchStatusLabelColorG->value());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusLabelColorB", ui->spinMetroidHudMatchStatusLabelColorB->value());
    instcfg.SetBool("Metroid.Visual.HudMatchStatusValueColorOverall", ui->comboMetroidHudMatchStatusValueColor->currentIndex() == 0);
    instcfg.SetInt("Metroid.Visual.HudMatchStatusValueColorR", ui->spinMetroidHudMatchStatusValueColorR->value());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusValueColorG", ui->spinMetroidHudMatchStatusValueColorG->value());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusValueColorB", ui->spinMetroidHudMatchStatusValueColorB->value());
    instcfg.SetBool("Metroid.Visual.HudMatchStatusSepColorOverall", ui->comboMetroidHudMatchStatusSepColor->currentIndex() == 0);
    instcfg.SetInt("Metroid.Visual.HudMatchStatusSepColorR", ui->spinMetroidHudMatchStatusSepColorR->value());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusSepColorG", ui->spinMetroidHudMatchStatusSepColorG->value());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusSepColorB", ui->spinMetroidHudMatchStatusSepColorB->value());
    instcfg.SetBool("Metroid.Visual.HudMatchStatusGoalColorOverall", ui->comboMetroidHudMatchStatusGoalColor->currentIndex() == 0);
    instcfg.SetInt("Metroid.Visual.HudMatchStatusGoalColorR", ui->spinMetroidHudMatchStatusGoalColorR->value());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusGoalColorG", ui->spinMetroidHudMatchStatusGoalColorG->value());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusGoalColorB", ui->spinMetroidHudMatchStatusGoalColorB->value());
    instcfg.SetBool("Metroid.Visual.HudRankShow",        ui->cbMetroidHudRankShow->isChecked());
    instcfg.SetInt ("Metroid.Visual.HudRankX",           ui->spinMetroidHudRankX->value());
    instcfg.SetInt ("Metroid.Visual.HudRankY",           ui->spinMetroidHudRankY->value());
    instcfg.SetInt ("Metroid.Visual.HudRankAlign",       ui->comboMetroidHudRankAlign->currentIndex());
    instcfg.SetBool("Metroid.Visual.HudRankShowOrdinal", ui->cbMetroidHudRankShowOrdinal->isChecked());
    instcfg.SetInt ("Metroid.Visual.HudRankColorR",      ui->spinMetroidHudRankColorR->value());
    instcfg.SetInt ("Metroid.Visual.HudRankColorG",      ui->spinMetroidHudRankColorG->value());
    instcfg.SetInt ("Metroid.Visual.HudRankColorB",      ui->spinMetroidHudRankColorB->value());
    instcfg.SetBool("Metroid.Visual.HudTimeLeftShow",    ui->cbMetroidHudTimeLeftShow->isChecked());
    instcfg.SetInt ("Metroid.Visual.HudTimeLeftX",       ui->spinMetroidHudTimeLeftX->value());
    instcfg.SetInt ("Metroid.Visual.HudTimeLeftY",       ui->spinMetroidHudTimeLeftY->value());
    instcfg.SetInt ("Metroid.Visual.HudTimeLeftAlign",   ui->comboMetroidHudTimeLeftAlign->currentIndex());
    instcfg.SetInt ("Metroid.Visual.HudTimeLeftColorR",  ui->spinMetroidHudTimeLeftColorR->value());
    instcfg.SetInt ("Metroid.Visual.HudTimeLeftColorG",  ui->spinMetroidHudTimeLeftColorG->value());
    instcfg.SetInt ("Metroid.Visual.HudTimeLeftColorB",  ui->spinMetroidHudTimeLeftColorB->value());
    instcfg.SetBool("Metroid.Visual.HudTimeLimitShow",   ui->cbMetroidHudTimeLimitShow->isChecked());
    instcfg.SetInt ("Metroid.Visual.HudTimeLimitX",      ui->spinMetroidHudTimeLimitX->value());
    instcfg.SetInt ("Metroid.Visual.HudTimeLimitY",      ui->spinMetroidHudTimeLimitY->value());
    instcfg.SetInt ("Metroid.Visual.HudTimeLimitAlign",  ui->comboMetroidHudTimeLimitAlign->currentIndex());
    instcfg.SetInt ("Metroid.Visual.HudTimeLimitColorR", ui->spinMetroidHudTimeLimitColorR->value());
    instcfg.SetInt ("Metroid.Visual.HudTimeLimitColorG", ui->spinMetroidHudTimeLimitColorG->value());
    instcfg.SetInt ("Metroid.Visual.HudTimeLimitColorB", ui->spinMetroidHudTimeLimitColorB->value());
    instcfg.SetBool("Metroid.Visual.HudBombLeftShow",     ui->cbMetroidHudBombLeftShow->isChecked());
    instcfg.SetBool("Metroid.Visual.HudBombLeftTextShow", ui->cbMetroidHudBombLeftTextShow->isChecked());
    instcfg.SetInt ("Metroid.Visual.HudBombLeftX",      ui->spinMetroidHudBombLeftX->value());
    instcfg.SetInt ("Metroid.Visual.HudBombLeftY",      ui->spinMetroidHudBombLeftY->value());
    instcfg.SetInt ("Metroid.Visual.HudBombLeftAlign",  ui->comboMetroidHudBombLeftAlign->currentIndex());
    instcfg.SetInt ("Metroid.Visual.HudBombLeftColorR", ui->spinMetroidHudBombLeftColorR->value());
    instcfg.SetInt ("Metroid.Visual.HudBombLeftColorG", ui->spinMetroidHudBombLeftColorG->value());
    instcfg.SetInt ("Metroid.Visual.HudBombLeftColorB", ui->spinMetroidHudBombLeftColorB->value());
    instcfg.SetString("Metroid.Visual.HudBombLeftPrefix", ui->leMetroidHudBombLeftPrefix->text().toStdString());
    instcfg.SetString("Metroid.Visual.HudBombLeftSuffix", ui->leMetroidHudBombLeftSuffix->text().toStdString());
    instcfg.SetBool("Metroid.Visual.HudBombLeftIconShow",         ui->cbMetroidHudBombLeftIconShow->isChecked());
    instcfg.SetBool("Metroid.Visual.HudBombLeftIconColorOverlay", ui->cbMetroidHudBombLeftIconColorOverlay->isChecked());
    instcfg.SetInt ("Metroid.Visual.HudBombLeftIconColorR",       ui->spinMetroidHudBombLeftIconColorR->value());
    instcfg.SetInt ("Metroid.Visual.HudBombLeftIconColorG",       ui->spinMetroidHudBombLeftIconColorG->value());
    instcfg.SetInt ("Metroid.Visual.HudBombLeftIconColorB",       ui->spinMetroidHudBombLeftIconColorB->value());
    instcfg.SetInt ("Metroid.Visual.HudBombLeftIconMode",         ui->comboMetroidHudBombLeftIconMode->currentIndex());
    instcfg.SetInt ("Metroid.Visual.HudBombLeftIconOfsX",         ui->spinMetroidHudBombLeftIconOfsX->value());
    instcfg.SetInt ("Metroid.Visual.HudBombLeftIconOfsY",         ui->spinMetroidHudBombLeftIconOfsY->value());
    instcfg.SetInt ("Metroid.Visual.HudBombLeftIconPosX",         ui->spinMetroidHudBombLeftIconPosX->value());
    instcfg.SetInt ("Metroid.Visual.HudBombLeftIconPosY",         ui->spinMetroidHudBombLeftIconPosY->value());
    instcfg.SetInt ("Metroid.Visual.HudBombLeftIconAnchorX",      ui->comboMetroidHudBombLeftIconAnchorX->currentIndex());
    instcfg.SetInt ("Metroid.Visual.HudBombLeftIconAnchorY",      ui->comboMetroidHudBombLeftIconAnchorY->currentIndex());
    MelonPrime::CustomHud_InvalidateConfigCache();
    updateMatchStatusPreview();
    m_applyPreviewActive = false;
#endif
}

void MelonPrimeInputConfig::applyAndPreviewHpAmmo()
{
#ifdef MELONPRIME_CUSTOM_HUD
    if (!m_applyPreviewEnabled || m_applyPreviewActive) return;
    m_applyPreviewActive = true;
    Config::Table& instcfg = emuInstance->getLocalConfig();
    instcfg.SetInt ("Metroid.Visual.HudHpX",              ui->spinMetroidHudHpX->value());
    instcfg.SetInt ("Metroid.Visual.HudHpY",              ui->spinMetroidHudHpY->value());
    instcfg.SetString("Metroid.Visual.HudHpPrefix",       ui->leMetroidHudHpPrefix->text().toStdString());
    instcfg.SetInt ("Metroid.Visual.HudHpAlign",          ui->comboMetroidHudHpAlign->currentIndex());
    instcfg.SetBool("Metroid.Visual.HudHpTextAutoColor",   ui->cbMetroidHudHpTextAutoColor->isChecked());
    instcfg.SetInt ("Metroid.Visual.HudHpTextColorR",      ui->spinMetroidHudHpTextColorR->value());
    instcfg.SetInt ("Metroid.Visual.HudHpTextColorG",      ui->spinMetroidHudHpTextColorG->value());
    instcfg.SetInt ("Metroid.Visual.HudHpTextColorB",      ui->spinMetroidHudHpTextColorB->value());
    instcfg.SetInt ("Metroid.Visual.HudWeaponX",          ui->spinMetroidHudWeaponX->value());
    instcfg.SetInt ("Metroid.Visual.HudWeaponY",          ui->spinMetroidHudWeaponY->value());
    instcfg.SetString("Metroid.Visual.HudAmmoPrefix",     ui->leMetroidHudAmmoPrefix->text().toStdString());
    instcfg.SetInt ("Metroid.Visual.HudAmmoAlign",        ui->comboMetroidHudAmmoAlign->currentIndex());
    instcfg.SetInt ("Metroid.Visual.HudAmmoTextColorR",    ui->spinMetroidHudAmmoTextColorR->value());
    instcfg.SetInt ("Metroid.Visual.HudAmmoTextColorG",    ui->spinMetroidHudAmmoTextColorG->value());
    instcfg.SetInt ("Metroid.Visual.HudAmmoTextColorB",    ui->spinMetroidHudAmmoTextColorB->value());
    instcfg.SetBool("Metroid.Visual.HudWeaponIconShow",   ui->cbMetroidHudWeaponIconShow->isChecked());
    instcfg.SetInt ("Metroid.Visual.HudWeaponIconMode",   ui->comboMetroidHudWeaponIconMode->currentIndex());
    instcfg.SetInt ("Metroid.Visual.HudWeaponIconOffsetX",ui->spinMetroidHudWeaponIconOffsetX->value());
    instcfg.SetInt ("Metroid.Visual.HudWeaponIconOffsetY",ui->spinMetroidHudWeaponIconOffsetY->value());
    instcfg.SetInt ("Metroid.Visual.HudWeaponIconPosX",     ui->spinMetroidHudWeaponIconPosX->value());
    instcfg.SetInt ("Metroid.Visual.HudWeaponIconPosY",     ui->spinMetroidHudWeaponIconPosY->value());
    instcfg.SetInt ("Metroid.Visual.HudWeaponIconAnchorX",  ui->comboMetroidHudWeaponIconAnchorX->currentIndex());
    instcfg.SetInt ("Metroid.Visual.HudWeaponIconAnchorY",  ui->comboMetroidHudWeaponIconAnchorY->currentIndex());
    instcfg.SetBool("Metroid.Visual.HudWeaponIconColorOverlay", ui->cbMetroidHudWeaponIconColorOverlay->isChecked());
    instcfg.SetBool("Metroid.Visual.HudHpGauge",               ui->cbMetroidHudHpGauge->isChecked());
    instcfg.SetInt ("Metroid.Visual.HudHpGaugeOrientation",    ui->comboMetroidHudHpGaugeOrientation->currentIndex());
    instcfg.SetInt ("Metroid.Visual.HudHpGaugeLength",         ui->spinMetroidHudHpGaugeLength->value());
    instcfg.SetInt ("Metroid.Visual.HudHpGaugeWidth",          ui->spinMetroidHudHpGaugeWidth->value());
    instcfg.SetInt ("Metroid.Visual.HudHpGaugeOffsetX",        ui->spinMetroidHudHpGaugeOffsetX->value());
    instcfg.SetInt ("Metroid.Visual.HudHpGaugeOffsetY",        ui->spinMetroidHudHpGaugeOffsetY->value());
    instcfg.SetInt ("Metroid.Visual.HudHpGaugeAnchor",         ui->comboMetroidHudHpGaugeAnchor->currentIndex());
    instcfg.SetInt ("Metroid.Visual.HudHpGaugePosMode",        ui->comboMetroidHudHpGaugePosMode->currentIndex());
    instcfg.SetInt ("Metroid.Visual.HudHpGaugePosX",           ui->spinMetroidHudHpGaugePosX->value());
    instcfg.SetInt ("Metroid.Visual.HudHpGaugePosY",           ui->spinMetroidHudHpGaugePosY->value());
    instcfg.SetBool("Metroid.Visual.HudHpGaugeAutoColor",      ui->cbMetroidHudHpGaugeAutoColor->isChecked());
    instcfg.SetInt("Metroid.Visual.HudHpGaugeColorR", ui->spinMetroidHudHpGaugeColorR->value());
    instcfg.SetInt("Metroid.Visual.HudHpGaugeColorG", ui->spinMetroidHudHpGaugeColorG->value());
    instcfg.SetInt("Metroid.Visual.HudHpGaugeColorB", ui->spinMetroidHudHpGaugeColorB->value());
    instcfg.SetBool("Metroid.Visual.HudAmmoGauge",             ui->cbMetroidHudAmmoGauge->isChecked());
    instcfg.SetInt ("Metroid.Visual.HudAmmoGaugeOrientation",  ui->comboMetroidHudAmmoGaugeOrientation->currentIndex());
    instcfg.SetInt ("Metroid.Visual.HudAmmoGaugeLength",       ui->spinMetroidHudAmmoGaugeLength->value());
    instcfg.SetInt ("Metroid.Visual.HudAmmoGaugeWidth",        ui->spinMetroidHudAmmoGaugeWidth->value());
    instcfg.SetInt ("Metroid.Visual.HudAmmoGaugeOffsetX",      ui->spinMetroidHudAmmoGaugeOffsetX->value());
    instcfg.SetInt ("Metroid.Visual.HudAmmoGaugeOffsetY",      ui->spinMetroidHudAmmoGaugeOffsetY->value());
    instcfg.SetInt ("Metroid.Visual.HudAmmoGaugeAnchor",       ui->comboMetroidHudAmmoGaugeAnchor->currentIndex());
    instcfg.SetInt ("Metroid.Visual.HudAmmoGaugePosMode",      ui->comboMetroidHudAmmoGaugePosMode->currentIndex());
    instcfg.SetInt ("Metroid.Visual.HudAmmoGaugePosX",         ui->spinMetroidHudAmmoGaugePosX->value());
    instcfg.SetInt ("Metroid.Visual.HudAmmoGaugePosY",         ui->spinMetroidHudAmmoGaugePosY->value());
    instcfg.SetInt("Metroid.Visual.HudAmmoGaugeColorR", ui->spinMetroidHudAmmoGaugeColorR->value());
    instcfg.SetInt("Metroid.Visual.HudAmmoGaugeColorG", ui->spinMetroidHudAmmoGaugeColorG->value());
    instcfg.SetInt("Metroid.Visual.HudAmmoGaugeColorB", ui->spinMetroidHudAmmoGaugeColorB->value());
    MelonPrime::CustomHud_InvalidateConfigCache();
    updateHpAmmoPreview();
    m_applyPreviewActive = false;
#endif
}

void MelonPrimeInputConfig::applyAndPreviewCrosshair()
{
#ifdef MELONPRIME_CUSTOM_HUD
    if (!m_applyPreviewEnabled || m_applyPreviewActive) return;
    m_applyPreviewActive = true;
    Config::Table& instcfg = emuInstance->getLocalConfig();
    instcfg.SetInt("Metroid.Visual.CrosshairColorR", ui->spinMetroidCrosshairR->value());
    instcfg.SetInt("Metroid.Visual.CrosshairColorG", ui->spinMetroidCrosshairG->value());
    instcfg.SetInt("Metroid.Visual.CrosshairColorB", ui->spinMetroidCrosshairB->value());
    instcfg.SetBool("Metroid.Visual.CrosshairOutline",         ui->cbMetroidCrosshairOutline->isChecked());
    instcfg.SetDouble("Metroid.Visual.CrosshairOutlineOpacity",ui->spinMetroidCrosshairOutlineOpacity->value());
    instcfg.SetInt ("Metroid.Visual.CrosshairOutlineThickness",ui->spinMetroidCrosshairOutlineThickness->value());
    instcfg.SetBool("Metroid.Visual.CrosshairCenterDot",       ui->cbMetroidCrosshairCenterDot->isChecked());
    instcfg.SetDouble("Metroid.Visual.CrosshairDotOpacity",    ui->spinMetroidCrosshairDotOpacity->value());
    instcfg.SetInt ("Metroid.Visual.CrosshairDotThickness",    ui->spinMetroidCrosshairDotThickness->value());
    instcfg.SetBool("Metroid.Visual.CrosshairTStyle",          ui->cbMetroidCrosshairTStyle->isChecked());
    instcfg.SetBool("Metroid.Visual.CrosshairInnerShow",       ui->cbMetroidCrosshairInnerShow->isChecked());
    instcfg.SetDouble("Metroid.Visual.CrosshairInnerOpacity",  ui->spinMetroidCrosshairInnerOpacity->value());
    instcfg.SetInt ("Metroid.Visual.CrosshairInnerLengthX",    ui->spinMetroidCrosshairInnerLengthX->value());
    instcfg.SetInt ("Metroid.Visual.CrosshairInnerLengthY",    ui->spinMetroidCrosshairInnerLengthY->value());
    instcfg.SetInt ("Metroid.Visual.CrosshairInnerThickness",  ui->spinMetroidCrosshairInnerThickness->value());
    instcfg.SetInt ("Metroid.Visual.CrosshairInnerOffset",     ui->spinMetroidCrosshairInnerOffset->value());
    instcfg.SetBool("Metroid.Visual.CrosshairInnerLinkXY",     ui->cbMetroidCrosshairInnerLinkXY->isChecked());
    instcfg.SetBool("Metroid.Visual.CrosshairOuterShow",       ui->cbMetroidCrosshairOuterShow->isChecked());
    instcfg.SetDouble("Metroid.Visual.CrosshairOuterOpacity",  ui->spinMetroidCrosshairOuterOpacity->value());
    instcfg.SetInt ("Metroid.Visual.CrosshairOuterLengthX",    ui->spinMetroidCrosshairOuterLengthX->value());
    instcfg.SetInt ("Metroid.Visual.CrosshairOuterLengthY",    ui->spinMetroidCrosshairOuterLengthY->value());
    instcfg.SetInt ("Metroid.Visual.CrosshairOuterThickness",  ui->spinMetroidCrosshairOuterThickness->value());
    instcfg.SetInt ("Metroid.Visual.CrosshairOuterOffset",     ui->spinMetroidCrosshairOuterOffset->value());
    instcfg.SetBool("Metroid.Visual.CrosshairOuterLinkXY",     ui->cbMetroidCrosshairOuterLinkXY->isChecked());
    MelonPrime::CustomHud_InvalidateConfigCache();
    updateCrosshairPreview();
    m_applyPreviewActive = false;
#endif
}

void MelonPrimeInputConfig::applyAndPreviewRadar()
{
#ifdef MELONPRIME_CUSTOM_HUD
    if (!m_applyPreviewEnabled || m_applyPreviewActive) return;
    m_applyPreviewActive = true;
    Config::Table& instcfg = emuInstance->getLocalConfig();
    instcfg.SetBool  ("Metroid.Visual.BtmOverlayEnable",     ui->cbMetroidBtmOverlayEnable->isChecked());
    instcfg.SetInt   ("Metroid.Visual.BtmOverlayDstX",       ui->spinMetroidBtmOverlayDstX->value());
    instcfg.SetInt   ("Metroid.Visual.BtmOverlayDstY",       ui->spinMetroidBtmOverlayDstY->value());
    instcfg.SetInt   ("Metroid.Visual.BtmOverlayDstSize",    ui->spinMetroidBtmOverlayDstSize->value());
    instcfg.SetDouble("Metroid.Visual.BtmOverlayOpacity",    ui->spinMetroidBtmOverlayOpacity->value());
    instcfg.SetInt   ("Metroid.Visual.BtmOverlaySrcRadius",  ui->spinMetroidBtmOverlaySrcRadius->value());
    MelonPrime::CustomHud_InvalidateConfigCache();
    updateRadarPreview();
    m_applyPreviewActive = false;
#endif
}

void MelonPrimeInputConfig::updateCrosshairPreview()
{
    QWidget* preview = ui->widgetCrosshairPreview;
    if (!preview) return;

    Config::Table& instcfg = emuInstance->getLocalConfig();

    const int pw = preview->width();
    const int ph = preview->height();
    if (pw < 2 || ph < 2) return;

    QPixmap pixmap(pw, ph);
    pixmap.fill(QColor(30, 30, 40));

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);

    int cx = pw / 2;
    int cy = ph / 2;

    int chR = instcfg.GetInt("Metroid.Visual.CrosshairColorR");
    int chG = instcfg.GetInt("Metroid.Visual.CrosshairColorG");
    int chB = instcfg.GetInt("Metroid.Visual.CrosshairColorB");
    QColor chColor(chR, chG, chB);

    bool showOutline = instcfg.GetBool("Metroid.Visual.CrosshairOutline");
    double outlineOp = instcfg.GetDouble("Metroid.Visual.CrosshairOutlineOpacity");
    int outlineThick = instcfg.GetInt("Metroid.Visual.CrosshairOutlineThickness");
    bool showDot = instcfg.GetBool("Metroid.Visual.CrosshairCenterDot");
    double dotOp = instcfg.GetDouble("Metroid.Visual.CrosshairDotOpacity");
    int dotThick = instcfg.GetInt("Metroid.Visual.CrosshairDotThickness");
    bool tStyle = instcfg.GetBool("Metroid.Visual.CrosshairTStyle");

    bool showInner = instcfg.GetBool("Metroid.Visual.CrosshairInnerShow");
    double innerOp = instcfg.GetDouble("Metroid.Visual.CrosshairInnerOpacity");
    int innerLX = instcfg.GetInt("Metroid.Visual.CrosshairInnerLengthX");
    int innerLY = instcfg.GetInt("Metroid.Visual.CrosshairInnerLengthY");
    int innerThick = instcfg.GetInt("Metroid.Visual.CrosshairInnerThickness");
    int innerOfs = instcfg.GetInt("Metroid.Visual.CrosshairInnerOffset");

    bool showOuter = instcfg.GetBool("Metroid.Visual.CrosshairOuterShow");
    double outerOp = instcfg.GetDouble("Metroid.Visual.CrosshairOuterOpacity");
    int outerLX = instcfg.GetInt("Metroid.Visual.CrosshairOuterLengthX");
    int outerLY = instcfg.GetInt("Metroid.Visual.CrosshairOuterLengthY");
    int outerThick = instcfg.GetInt("Metroid.Visual.CrosshairOuterThickness");
    int outerOfs = instcfg.GetInt("Metroid.Visual.CrosshairOuterOffset");

    // Scale factor for visibility in preview
    const int S = 3;

    // Helper: draw a crosshair arm set (4 lines or 3 for T-style)
    auto drawArms = [&](int lenX, int lenY, int thick, int offset, double opacity, bool isOutline) {
        QColor c = isOutline ? QColor(0,0,0, static_cast<int>(opacity * 255)) : chColor;
        if (!isOutline) c.setAlpha(static_cast<int>(opacity * 255));
        int extra = isOutline ? outlineThick : 0;
        int t = (thick + extra * 2) * S;
        int ofsS = offset * S;

        p.setPen(Qt::NoPen);
        p.setBrush(c);

        // Right
        p.drawRect(cx + ofsS, cy - t/2, lenX * S, t);
        // Left
        p.drawRect(cx - ofsS - lenX * S, cy - t/2, lenX * S, t);
        // Down
        p.drawRect(cx - t/2, cy + ofsS, t, lenY * S);
        // Up (skip if T-style)
        if (!tStyle)
            p.drawRect(cx - t/2, cy - ofsS - lenY * S, t, lenY * S);
    };

    // Draw outline behind lines
    if (showOutline) {
        if (showOuter) drawArms(outerLX, outerLY, outerThick, outerOfs, outlineOp, true);
        if (showInner) drawArms(innerLX, innerLY, innerThick, innerOfs, outlineOp, true);
    }
    // Draw outer lines
    if (showOuter) drawArms(outerLX, outerLY, outerThick, outerOfs, outerOp, false);
    // Draw inner lines
    if (showInner) drawArms(innerLX, innerLY, innerThick, innerOfs, innerOp, false);

    // Center dot
    if (showDot) {
        QColor dc = chColor;
        dc.setAlpha(static_cast<int>(dotOp * 255));
        int ds = dotThick * S;
        if (showOutline) {
            QColor oc(0, 0, 0, static_cast<int>(outlineOp * 255));
            int os = (dotThick + outlineThick * 2) * S;
            p.setPen(Qt::NoPen);
            p.setBrush(oc);
            p.drawRect(cx - os/2, cy - os/2, os, os);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(dc);
        p.drawRect(cx - ds/2, cy - ds/2, ds, ds);
    }

    p.end();
    applyPixmapToPreview(preview, pixmap);
}

void MelonPrimeInputConfig::updateHpAmmoPreview()
{
    QWidget* preview = ui->widgetHpAmmoPreview;
    if (!preview) return;

    Config::Table& instcfg = emuInstance->getLocalConfig();

    const int pw = preview->width();
    const int ph = preview->height();
    if (pw < 2 || ph < 2) return;

    QPixmap pixmap(pw, ph);
    pixmap.fill(QColor(30, 30, 40));

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);

    // DS top screen is 256x192. Scale to fit preview.
    const float dsW = 256.0f, dsH = 192.0f;
    const float scale = std::min(static_cast<float>(pw) / dsW, static_cast<float>(ph) / dsH);
    const float offX = (pw - dsW * scale) / 2.0f;
    const float offY = (ph - dsH * scale) / 2.0f;

    // Draw screen border
    p.setPen(QPen(QColor(80, 80, 80), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(offX, offY, dsW * scale, dsH * scale));

    // Use the same font as the actual game HUD (:/mph-font, pixel size 6)
    p.setFont(loadHudFont(scale));

    // HP text
    // hpY is the text baseline in DS coords (matches DrawCachedText/DrawHP in the game).
    // drawText(QPointF, text) uses Y as baseline, so map directly: no extra font offset needed.
    int hpX = instcfg.GetInt("Metroid.Visual.HudHpX");
    int hpY = instcfg.GetInt("Metroid.Visual.HudHpY");
    int hpAlign = instcfg.GetInt("Metroid.Visual.HudHpAlign");
    int hpR = instcfg.GetInt("Metroid.Visual.HudHpTextColorR");
    int hpG = instcfg.GetInt("Metroid.Visual.HudHpTextColorG");
    int hpB = instcfg.GetInt("Metroid.Visual.HudHpTextColorB");
    int hpGaugeR = instcfg.GetInt("Metroid.Visual.HudHpGaugeColorR");
    int hpGaugeG = instcfg.GetInt("Metroid.Visual.HudHpGaugeColorG");
    int hpGaugeB = instcfg.GetInt("Metroid.Visual.HudHpGaugeColorB");
    QString hpText = QString::fromStdString(instcfg.GetString("Metroid.Visual.HudHpPrefix")) + "199";
    p.setPen(QColor(hpR, hpG, hpB));
    float hpSy = offY + hpY * scale;
    int hpTextW = p.fontMetrics().horizontalAdvance(hpText);
    float hpSx = offX + hpX * scale;
    if (hpAlign == 1) hpSx -= hpTextW / 2.0f;
    else if (hpAlign == 2) hpSx -= hpTextW;
    p.drawText(QPointF(hpSx, hpSy), hpText);

    // HP gauge bar - mirrors CalcGaugePos() from MelonPrimeCustomHud.cpp
    if (instcfg.GetBool("Metroid.Visual.HudHpGauge")) {
        int gLen   = instcfg.GetInt("Metroid.Visual.HudHpGaugeLength");
        int gWid   = instcfg.GetInt("Metroid.Visual.HudHpGaugeWidth");
        int gOfsX  = instcfg.GetInt("Metroid.Visual.HudHpGaugeOffsetX");
        int gOfsY  = instcfg.GetInt("Metroid.Visual.HudHpGaugeOffsetY");
        int orient = instcfg.GetInt("Metroid.Visual.HudHpGaugeOrientation");
        int anchor = instcfg.GetInt("Metroid.Visual.HudHpGaugeAnchor");
        int posMode= instcfg.GetInt("Metroid.Visual.HudHpGaugePosMode");
        float gx, gy;
        if (posMode == 1) {
            gx = offX + instcfg.GetInt("Metroid.Visual.HudHpGaugePosX") * scale;
            gy = offY + instcfg.GetInt("Metroid.Visual.HudHpGaugePosY") * scale;
        } else {
            const QFontMetrics fm = p.fontMetrics();
            float tH = fm.height(), tW = static_cast<float>(hpTextW);
            float gL = gLen * scale, gW = gWid * scale;
            float ox = gOfsX * scale, oy = gOfsY * scale;
            switch (anchor) {
            case 1: gx = hpSx + ox; gy = hpSy - tH - (orient==0?gW:gL) + oy; break;
            case 2: gx = hpSx + tW + ox; gy = hpSy - tH/2.f - (orient==0?gW:gL)/2.f + oy; break;
            case 3: gx = hpSx - (orient==0?gL:gW) + ox; gy = hpSy - tH/2.f - (orient==0?gW:gL)/2.f + oy; break;
            case 4: gx = hpSx + tW/2.f - (orient==0?gL:gW)/2.f + ox; gy = hpSy - tH/2.f - (orient==0?gW:gL)/2.f + oy; break;
            default: gx = hpSx + ox; gy = hpSy + 2 * scale + oy; break;
            }
        }
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(hpGaugeR, hpGaugeG, hpGaugeB));
        if (orient == 0) // Horizontal
            p.drawRect(QRectF(gx, gy, gLen * scale, gWid * scale));
        else // Vertical
            p.drawRect(QRectF(gx, gy, gWid * scale, gLen * scale));
    }

    // Weapon icon
    if (instcfg.GetBool("Metroid.Visual.HudWeaponIconShow")) {
        static QPixmap s_missileIcon;
        if (s_missileIcon.isNull())
            s_missileIcon.load(":/mph-icon-missile");
        if (!s_missileIcon.isNull()) {
            int iconPosX = instcfg.GetInt("Metroid.Visual.HudWeaponIconPosX");
            int iconPosY = instcfg.GetInt("Metroid.Visual.HudWeaponIconPosY");
            float iconSx = offX + iconPosX * scale;
            float iconSy = offY + iconPosY * scale;
            int iconW = static_cast<int>(s_missileIcon.width() * scale);
            int iconH = static_cast<int>(s_missileIcon.height() * scale);
            p.drawPixmap(QRectF(iconSx - iconW / 2.0f, iconSy - iconH / 2.0f, iconW, iconH),
                         s_missileIcon, s_missileIcon.rect());
        }
    }

    // Ammo text - same baseline logic as HP
    int wpnX = instcfg.GetInt("Metroid.Visual.HudWeaponX");
    int wpnY = instcfg.GetInt("Metroid.Visual.HudWeaponY");
    int ammoAlign = instcfg.GetInt("Metroid.Visual.HudAmmoAlign");
    int amR = instcfg.GetInt("Metroid.Visual.HudAmmoTextColorR");
    int amG = instcfg.GetInt("Metroid.Visual.HudAmmoTextColorG");
    int amB = instcfg.GetInt("Metroid.Visual.HudAmmoTextColorB");
    int amGaugeR = instcfg.GetInt("Metroid.Visual.HudAmmoGaugeColorR");
    int amGaugeG = instcfg.GetInt("Metroid.Visual.HudAmmoGaugeColorG");
    int amGaugeB = instcfg.GetInt("Metroid.Visual.HudAmmoGaugeColorB");
    QString ammoText = QString::fromStdString(instcfg.GetString("Metroid.Visual.HudAmmoPrefix")) + "30";
    p.setPen(QColor(amR, amG, amB));
    float wpnSy = offY + wpnY * scale;
    int ammoTextW = p.fontMetrics().horizontalAdvance(ammoText);
    float wpnSx = offX + wpnX * scale;
    if (ammoAlign == 1) wpnSx -= ammoTextW / 2.0f;
    else if (ammoAlign == 2) wpnSx -= ammoTextW;
    p.drawText(QPointF(wpnSx, wpnSy), ammoText);

    // Ammo gauge bar - mirrors CalcGaugePos() from MelonPrimeCustomHud.cpp
    if (instcfg.GetBool("Metroid.Visual.HudAmmoGauge")) {
        int gLen   = instcfg.GetInt("Metroid.Visual.HudAmmoGaugeLength");
        int gWid   = instcfg.GetInt("Metroid.Visual.HudAmmoGaugeWidth");
        int gOfsX  = instcfg.GetInt("Metroid.Visual.HudAmmoGaugeOffsetX");
        int gOfsY  = instcfg.GetInt("Metroid.Visual.HudAmmoGaugeOffsetY");
        int orient = instcfg.GetInt("Metroid.Visual.HudAmmoGaugeOrientation");
        int anchor = instcfg.GetInt("Metroid.Visual.HudAmmoGaugeAnchor");
        int posMode= instcfg.GetInt("Metroid.Visual.HudAmmoGaugePosMode");
        float gx, gy;
        if (posMode == 1) {
            gx = offX + instcfg.GetInt("Metroid.Visual.HudAmmoGaugePosX") * scale;
            gy = offY + instcfg.GetInt("Metroid.Visual.HudAmmoGaugePosY") * scale;
        } else {
            const QFontMetrics fm = p.fontMetrics();
            float tH = fm.height(), tW = static_cast<float>(ammoTextW);
            float gL = gLen * scale, gW = gWid * scale;
            float ox = gOfsX * scale, oy = gOfsY * scale;
            switch (anchor) {
            case 1: gx = wpnSx + ox; gy = wpnSy - tH - (orient==0?gW:gL) + oy; break;
            case 2: gx = wpnSx + tW + ox; gy = wpnSy - tH/2.f - (orient==0?gW:gL)/2.f + oy; break;
            case 3: gx = wpnSx - (orient==0?gL:gW) + ox; gy = wpnSy - tH/2.f - (orient==0?gW:gL)/2.f + oy; break;
            case 4: gx = wpnSx + tW/2.f - (orient==0?gL:gW)/2.f + ox; gy = wpnSy - tH/2.f - (orient==0?gW:gL)/2.f + oy; break;
            default: gx = wpnSx + ox; gy = wpnSy + 2 * scale + oy; break;
            }
        }
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(amGaugeR, amGaugeG, amGaugeB));
        if (orient == 0)
            p.drawRect(QRectF(gx, gy, gLen * scale, gWid * scale));
        else
            p.drawRect(QRectF(gx, gy, gWid * scale, gLen * scale));
    }

    p.end();
    applyPixmapToPreview(preview, pixmap);
}

void MelonPrimeInputConfig::updateMatchStatusPreview()
{
    QWidget* preview = ui->widgetMatchStatusPreview;
    if (!preview) return;

    Config::Table& instcfg = emuInstance->getLocalConfig();

    const int pw = preview->width();
    const int ph = preview->height();
    if (pw < 2 || ph < 2) return;

    QPixmap pixmap(pw, ph);
    pixmap.fill(QColor(30, 30, 40));

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, false);

    const float dsW = 256.0f, dsH = 192.0f;
    const float scale = std::min(static_cast<float>(pw) / dsW, static_cast<float>(ph) / dsH);
    const float offX = (pw - dsW * scale) / 2.0f;
    const float offY = (ph - dsH * scale) / 2.0f;
    p.setPen(QPen(QColor(80, 80, 80), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(offX, offY, dsW * scale, dsH * scale));

    p.setFont(loadHudFont(scale));

    auto formatTimeText = [](int seconds) {
        int safeSeconds = std::max(0, seconds);
        return QString("%1:%2")
            .arg(safeSeconds / 60)
            .arg(safeSeconds % 60, 2, 10, QLatin1Char('0'));
    };

    auto formatMinuteText = [](int minutes) {
        return QString("%1:00").arg(std::max(0, minutes));
    };

    auto buildRankText = [&instcfg]() {
        constexpr const char* ordinals[] = { "st", "nd", "rd", "th" };
        constexpr int rankIndex = 0;
        QString text = QString::fromStdString(instcfg.GetString("Metroid.Visual.HudRankPrefix"));
        text += QString::number(rankIndex + 1);
        if (instcfg.GetBool("Metroid.Visual.HudRankShowOrdinal"))
            text += QString::fromLatin1(ordinals[rankIndex]);
        text += QString::fromStdString(instcfg.GetString("Metroid.Visual.HudRankSuffix"));
        return text;
    };

    auto drawPreviewText = [&](const QString& text, int x, int y, const QColor& color, int align = 0) {
        float sx = offX + x * scale;
        float sy = offY + y * scale;
        if (align != 0) {
            float tw = p.fontMetrics().horizontalAdvance(text);
            if (align == 1) sx -= tw / 2.0f;
            else if (align == 2) sx -= tw;
        }
        p.setPen(color);
        p.drawText(QPointF(sx, sy), text);
    };

    if (!instcfg.GetBool("Metroid.Visual.HudMatchStatusShow")) {
        p.setPen(QColor(80, 80, 80));
        p.drawText(QRectF(offX, offY, dsW * scale, dsH * scale), Qt::AlignCenter, "HIDDEN");
    } else {
        const int msX = instcfg.GetInt("Metroid.Visual.HudMatchStatusX");
        const int msY = instcfg.GetInt("Metroid.Visual.HudMatchStatusY");

        QColor valueClr(
            instcfg.GetInt("Metroid.Visual.HudMatchStatusValueColorR"),
            instcfg.GetInt("Metroid.Visual.HudMatchStatusValueColorG"),
            instcfg.GetInt("Metroid.Visual.HudMatchStatusValueColorB"));
        QColor sepClr(
            instcfg.GetInt("Metroid.Visual.HudMatchStatusSepColorR"),
            instcfg.GetInt("Metroid.Visual.HudMatchStatusSepColorG"),
            instcfg.GetInt("Metroid.Visual.HudMatchStatusSepColorB"));
        QColor goalClr(
            instcfg.GetInt("Metroid.Visual.HudMatchStatusGoalColorR"),
            instcfg.GetInt("Metroid.Visual.HudMatchStatusGoalColorG"),
            instcfg.GetInt("Metroid.Visual.HudMatchStatusGoalColorB"));
        QColor labelClr(
            instcfg.GetInt("Metroid.Visual.HudMatchStatusLabelColorR"),
            instcfg.GetInt("Metroid.Visual.HudMatchStatusLabelColorG"),
            instcfg.GetInt("Metroid.Visual.HudMatchStatusLabelColorB"));

        const float sx = offX + msX * scale;
        const float sy = offY + msY * scale;

        const QString labelText = QString::fromStdString(instcfg.GetString("Metroid.Visual.HudMatchStatusLabelPoints"));
        const int labelPos = instcfg.GetInt("Metroid.Visual.HudMatchStatusLabelPos");
        const int labelOfsX = instcfg.GetInt("Metroid.Visual.HudMatchStatusLabelOfsX");
        const int labelOfsY = instcfg.GetInt("Metroid.Visual.HudMatchStatusLabelOfsY");
        // Mirror game logic: base position depends on labelPos (±10 DS px), then add user offset
        float lx, ly;
        switch (labelPos) {
        case 0:  lx = sx;               ly = sy - 10 * scale; break; // above
        case 1:  lx = sx;               ly = sy + 10 * scale; break; // below
        case 2:  lx = sx - 50 * scale;  ly = sy;              break; // left
        case 3:  lx = sx + 50 * scale;  ly = sy;              break; // right
        default: lx = sx;               ly = sy;              break; // same
        }
        lx += labelOfsX * scale;
        ly += labelOfsY * scale;

        p.setPen(labelClr);
        p.drawText(QPointF(lx, ly), labelText);

        p.setPen(valueClr);
        p.drawText(QPointF(sx, sy), "3");
        float xOfs = static_cast<float>(p.fontMetrics().horizontalAdvance("3"));
        p.setPen(sepClr);
        p.drawText(QPointF(sx + xOfs, sy), "/");
        xOfs += static_cast<float>(p.fontMetrics().horizontalAdvance("/"));
        p.setPen(goalClr);
        p.drawText(QPointF(sx + xOfs, sy), "7");
    }

    if (instcfg.GetBool("Metroid.Visual.HudRankShow")) {
        drawPreviewText(
            buildRankText(),
            instcfg.GetInt("Metroid.Visual.HudRankX"),
            instcfg.GetInt("Metroid.Visual.HudRankY"),
            QColor(
                instcfg.GetInt("Metroid.Visual.HudRankColorR"),
                instcfg.GetInt("Metroid.Visual.HudRankColorG"),
                instcfg.GetInt("Metroid.Visual.HudRankColorB")),
            instcfg.GetInt("Metroid.Visual.HudRankAlign"));
    }

    if (instcfg.GetBool("Metroid.Visual.HudTimeLeftShow")) {
        drawPreviewText(
            formatTimeText(5 * 60),
            instcfg.GetInt("Metroid.Visual.HudTimeLeftX"),
            instcfg.GetInt("Metroid.Visual.HudTimeLeftY"),
            QColor(
                instcfg.GetInt("Metroid.Visual.HudTimeLeftColorR"),
                instcfg.GetInt("Metroid.Visual.HudTimeLeftColorG"),
                instcfg.GetInt("Metroid.Visual.HudTimeLeftColorB")),
            instcfg.GetInt("Metroid.Visual.HudTimeLeftAlign"));
    }

    if (instcfg.GetBool("Metroid.Visual.HudTimeLimitShow")) {
        drawPreviewText(
            formatMinuteText(7),
            instcfg.GetInt("Metroid.Visual.HudTimeLimitX"),
            instcfg.GetInt("Metroid.Visual.HudTimeLimitY"),
            QColor(
                instcfg.GetInt("Metroid.Visual.HudTimeLimitColorR"),
                instcfg.GetInt("Metroid.Visual.HudTimeLimitColorG"),
                instcfg.GetInt("Metroid.Visual.HudTimeLimitColorB")),
            instcfg.GetInt("Metroid.Visual.HudTimeLimitAlign"));
    }

    if (instcfg.GetBool("Metroid.Visual.HudBombLeftShow")) {
        const QString prefix = QString::fromStdString(instcfg.GetString("Metroid.Visual.HudBombLeftPrefix"));
        const QString suffix = QString::fromStdString(instcfg.GetString("Metroid.Visual.HudBombLeftSuffix"));
        const QString bombText = instcfg.GetBool("Metroid.Visual.HudBombLeftTextShow")
            ? (prefix + "3" + suffix)
            : (prefix + suffix);
        if (!bombText.isEmpty())
            drawPreviewText(
                bombText,
                instcfg.GetInt("Metroid.Visual.HudBombLeftX"),
                instcfg.GetInt("Metroid.Visual.HudBombLeftY"),
                QColor(
                    instcfg.GetInt("Metroid.Visual.HudBombLeftColorR"),
                    instcfg.GetInt("Metroid.Visual.HudBombLeftColorG"),
                    instcfg.GetInt("Metroid.Visual.HudBombLeftColorB")),
                instcfg.GetInt("Metroid.Visual.HudBombLeftAlign"));
    }

    if (instcfg.GetBool("Metroid.Visual.HudBombLeftIconShow")) {
        // Preview with bombs=3 icon
        static QPixmap s_bombIconPreviews[4];
        static const char* kBombIconRes[4] = {
            ":/mph-icon-bombs0", ":/mph-icon-bombs1",
            ":/mph-icon-bombs2", ":/mph-icon-bombs3"
        };
        constexpr int kPreviewBombs = 3;
        if (s_bombIconPreviews[kPreviewBombs].isNull())
            s_bombIconPreviews[kPreviewBombs].load(kBombIconRes[kPreviewBombs]);

        const QPixmap& iconPm = s_bombIconPreviews[kPreviewBombs];
        if (!iconPm.isNull()) {
            const bool useOverlay = instcfg.GetBool("Metroid.Visual.HudBombLeftIconColorOverlay");
            const QColor overlayColor(
                instcfg.GetInt("Metroid.Visual.HudBombLeftIconColorR"),
                instcfg.GetInt("Metroid.Visual.HudBombLeftIconColorG"),
                instcfg.GetInt("Metroid.Visual.HudBombLeftIconColorB"));
            const int iconMode = instcfg.GetInt("Metroid.Visual.HudBombLeftIconMode");
            const int baseX = instcfg.GetInt("Metroid.Visual.HudBombLeftX");
            const int baseY = instcfg.GetInt("Metroid.Visual.HudBombLeftY");
            float ix = offX + ((iconMode == 0)
                ? (baseX + instcfg.GetInt("Metroid.Visual.HudBombLeftIconOfsX"))
                : instcfg.GetInt("Metroid.Visual.HudBombLeftIconPosX")) * scale;
            float iy = offY + ((iconMode == 0)
                ? (baseY + instcfg.GetInt("Metroid.Visual.HudBombLeftIconOfsY"))
                : instcfg.GetInt("Metroid.Visual.HudBombLeftIconPosY")) * scale;
            const int iw = static_cast<int>(iconPm.width() * scale);
            const int ih = static_cast<int>(iconPm.height() * scale);
            const int iconAlignX = instcfg.GetInt("Metroid.Visual.HudBombLeftIconAnchorX");
            const int iconAlignY = instcfg.GetInt("Metroid.Visual.HudBombLeftIconAnchorY");
            if (iconAlignX == 1) ix -= iw / 2.0f;
            else if (iconAlignX == 2) ix -= iw;
            if (iconAlignY == 1) iy -= ih / 2.0f;
            else if (iconAlignY == 2) iy -= ih;

            if (useOverlay) {
                QImage img = iconPm.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
                QPainter tp(&img);
                tp.setCompositionMode(QPainter::CompositionMode_SourceIn);
                tp.fillRect(img.rect(), overlayColor);
                tp.end();
                p.drawImage(QRectF(ix, iy, iw, ih), img, img.rect());
            } else {
                p.drawPixmap(QRectF(ix, iy, iw, ih), iconPm, iconPm.rect());
            }
        }
    }

    p.end();
    applyPixmapToPreview(preview, pixmap);
}

