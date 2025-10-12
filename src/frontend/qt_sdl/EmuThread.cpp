﻿/*
    Copyright 2016-2024 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

#include <optional>
#include <vector>
#include <string>
#include <algorithm>

#include <SDL2/SDL.h>

#include "main.h"

#include "types.h"
#include "version.h"

#include "ScreenLayout.h"

#include "Args.h"
#include "NDS.h"
#include "NDSCart.h"
#include "GBACart.h"
#include "GPU.h"
#include "SPU.h"
#include "Wifi.h"
#include "Platform.h"
#include "LocalMP.h"
#include "Config.h"
#include "RTC.h"
#include "DSi.h"
#include "DSi_I2C.h"
#include "GPU3D_Soft.h"
#include "GPU3D_OpenGL.h"
#include "GPU3D_Compute.h"

#include "Savestate.h"

#include "EmuInstance.h"

// melonPrimeDS
#include <cstdint>
#include <cmath>

#include "MelonPrimeDef.h"
#include "MelonPrimeRomAddrTable.h"

#if defined(_WIN32)

#include "MelonPrimeRawInputWinFilter.h"
#include "MelonPrimeHotkeyVkBinding.h"

// MelonPrimeDS joystick
/*
#include "MelonPrimeXInputFilter.h"
#include "MelonPrimeXInputBinding.h"
#include "MelonPrimeDirectInputBinding.h"
#include "MelonPrimeDirectInputFilter.h"
*/

// Anonymous namespace: Visible only within this translation unit
namespace {
    /*
    struct RawHK {
        bool nowShoot = false, prevShoot = false;
        bool nowZoom = false, prevZoom = false;

        void refresh(RawInputWinFilter* f) noexcept {
            if (!f) { nowShoot = nowZoom = false; return; }
            // 必要になったらここに他HKも足していけます
            nowShoot = f->hotkeyDown(HK_MetroidShootScan)
                || f->hotkeyDown(HK_MetroidScanShoot);
            nowZoom = f->hotkeyDown(HK_MetroidZoom);
        }
        void commit() noexcept {
            prevShoot = nowShoot;
            prevZoom = nowZoom;
        }


        // 使いたければ
        bool shootDown()   const noexcept { return nowShoot; }
        bool zoomDown()    const noexcept { return nowZoom; }
        bool shootPressed()const noexcept { return  nowShoot && !prevShoot; }
        bool shootReleased()const noexcept { return !nowShoot && prevShoot; }
    };

    RawHK g_rawHK;                       // ← これでどこからでも使える
    */
    RawInputWinFilter* g_rawFilter = nullptr; // ← フィルタもファイルスコープに出す

} // namespace
#endif




// Aim可否ビット管理（single-thread最小コスト版：必要ならAIMBLOCK_ATOMIC=1でatomic化）
#include <cstdint>

#ifndef AIMBLOCK_ATOMIC
#define AIMBLOCK_ATOMIC 0
#endif

#if AIMBLOCK_ATOMIC
#include <atomic>
using AimBitsType = std::atomic<uint32_t>;
#define AIMBITS_LOAD(x)    (x.load(std::memory_order_relaxed))
#define AIMBITS_STORE(x,v) (x.store((v), std::memory_order_relaxed))
#define AIMBITS_OR(x, m)   (x.fetch_or((m), std::memory_order_relaxed))
#define AIMBITS_AND(x, m)  (x.fetch_and((m), std::memory_order_relaxed))
#else
using AimBitsType = uint32_t;
#define AIMBITS_LOAD(x)    (x)
#define AIMBITS_STORE(x,v) ((x) = (v))
#define AIMBITS_OR(x, m)   ((x) |= (m))
#define AIMBITS_AND(x, m)  ((x) &= (m))
#endif

namespace {
    // gAimBlockBits==0 → Aim可、どれか1ビットでも立つとAim不可
    static AimBitsType gAimBlockBits
#if AIMBLOCK_ATOMIC
    { 0 };
#else
        = 0u;
#endif

    // 外部が直接参照する「総和ビット」：非0ならAim不可（理由ビット込み）
    // 例: if (isAimDisabled) { … } / if (isAimDisabled & AIMBLK_CHECK_WEAPON) { … }
    static uint32_t isAimDisabled = 0u;

    // 理由ビット定義（必要に応じて追加）
    enum : uint32_t {
        AIMBLK_CHECK_WEAPON = 1u << 0, // 武器チェック/切替中はAim停止
        AIMBLK_MORPHBALL_BOOST = 1u << 1, // モーフボールBoost中はAim停止
        AIMBLK_CURSOR_MODE = 1u << 2, // ← 追加：カーソルモード中はAim禁止
    };

    // 内部→外部の同期（総和ビットをミラー）
    __attribute__((always_inline)) inline void syncIsAimDisabled() noexcept {
        isAimDisabled = AIMBLOCK_ATOMIC ? AIMBITS_LOAD(gAimBlockBits) : gAimBlockBits;
    }

    // 指定ビットのON/OFF（更新のたびに isAimDisabled を同期）
    __attribute__((always_inline)) inline void setAimBlock(uint32_t bitMask, bool enable) noexcept {
        if (enable) {
            AIMBITS_OR(gAimBlockBits, bitMask);
        }
        else {
            AIMBITS_AND(gAimBlockBits, ~bitMask);
        }
        syncIsAimDisabled();
    }

    // スコープ限定の一時ブロック（解除漏れ防止）
    struct ScopeAimBlock {
        uint32_t mask;
        explicit ScopeAimBlock(uint32_t m) : mask(m) { setAimBlock(mask, true); }
        ~ScopeAimBlock() { setAimBlock(mask, false); }
        ScopeAimBlock(const ScopeAimBlock&) = delete;
        void operator=(const ScopeAimBlock&) = delete;
    };
} // namespace








// ===== AIM_ADJUST: deadzone + snap（0.5未満捨て、0.5〜1未満は±1、1以上そのまま）=====
#include <cmath>

namespace {
    // 設定で変更可能。0.0f で無効（即 return）
    static float gAimAdjust = 0.5f;

    // 0で無効／負値・NaNは0扱い
    inline void setAimAdjustFromValue(double v) noexcept {
        if (std::isnan(v) || v < 0.0) v = 0.0;
        gAimAdjust = static_cast<float>(v);
    }

    // Config::Table 非const版（本体）
    inline void applyAimAdjustSetting(Config::Table& cfg) noexcept {
        // GetDouble は 1引数・非const。未浮動は内部で 0.0 を既定挿入して返す実装
        double v = cfg.GetDouble("Metroid.Aim.Adjust");
        setAimAdjustFromValue(v);
    }

    // Config::Table const版（getLocalConfig() が const を返す場合用ラッパ）
    inline void applyAimAdjustSetting(const Config::Table& cfgConst) noexcept {
        applyAimAdjustSetting(const_cast<Config::Table&>(cfgConst));
    }

    __attribute__((always_inline, flatten)) inline void applyAimAdjustFloat(float& dx, float& dy) noexcept {
        const float a = gAimAdjust;
        if (a <= 0.0f) return; // 無効（即スキップ）

        auto adj1 = [a](float& v) __attribute__((always_inline, flatten)) {
            float av = std::fabs(v);
            if (av < a) { v = 0.0f; }
            else if (av < 1.0f) { v = (v >= 0.0f ? 1.0f : -1.0f); }
            // av >= 1.0f はそのまま
        };
        adj1(dx);
        adj1(dy);
    }
}

// 互換マクロ（float前提）
#define AIM_ADJUST(dx, dy) do { applyAimAdjustFloat((dx), (dy)); } while (0)


























using namespace melonDS;


EmuThread::EmuThread(EmuInstance* inst, QObject* parent) : QThread(parent)
{
    emuInstance = inst;

    emuStatus = emuStatus_Paused;
    emuPauseStack = emuPauseStackRunning;
    emuActive = false;
}

void EmuThread::attachWindow(MainWindow* window)
{
    connect(this, SIGNAL(windowTitleChange(QString)), window, SLOT(onTitleUpdate(QString)));
    connect(this, SIGNAL(windowEmuStart()), window, SLOT(onEmuStart()));
    connect(this, SIGNAL(windowEmuStop()), window, SLOT(onEmuStop()));
    connect(this, SIGNAL(windowEmuPause(bool)), window, SLOT(onEmuPause(bool)));
    connect(this, SIGNAL(windowEmuReset()), window, SLOT(onEmuReset()));
    connect(this, SIGNAL(autoScreenSizingChange(int)), window->panel, SLOT(onAutoScreenSizingChanged(int)));
    connect(this, SIGNAL(windowFullscreenToggle()), window, SLOT(onFullscreenToggled()));
    connect(this, SIGNAL(screenEmphasisToggle()), window, SLOT(onScreenEmphasisToggled()));

    if (window->winHasMenu())
    {
        connect(this, SIGNAL(windowLimitFPSChange()), window->actLimitFramerate, SLOT(trigger()));
        connect(this, SIGNAL(swapScreensToggle()), window->actScreenSwap, SLOT(trigger()));
    }
}

void EmuThread::detachWindow(MainWindow* window)
{
    disconnect(this, SIGNAL(windowTitleChange(QString)), window, SLOT(onTitleUpdate(QString)));
    disconnect(this, SIGNAL(windowEmuStart()), window, SLOT(onEmuStart()));
    disconnect(this, SIGNAL(windowEmuStop()), window, SLOT(onEmuStop()));
    disconnect(this, SIGNAL(windowEmuPause(bool)), window, SLOT(onEmuPause(bool)));
    disconnect(this, SIGNAL(windowEmuReset()), window, SLOT(onEmuReset()));
    disconnect(this, SIGNAL(autoScreenSizingChange(int)), window->panel, SLOT(onAutoScreenSizingChanged(int)));
    disconnect(this, SIGNAL(windowFullscreenToggle()), window, SLOT(onFullscreenToggled()));
    disconnect(this, SIGNAL(screenEmphasisToggle()), window, SLOT(onScreenEmphasisToggled()));

    if (window->winHasMenu())
    {
        disconnect(this, SIGNAL(windowLimitFPSChange()), window->actLimitFramerate, SLOT(trigger()));
        disconnect(this, SIGNAL(swapScreensToggle()), window->actScreenSwap, SLOT(trigger()));
    }
}


















// melonPrime
static bool isInGameAndHasInitialized = false;

// 共有キャッシュ定義(エイムのホットパスから再計算を排除するため)
// X軸感度係数(ホットパスで直接参照するため)
static float gAimSensiFactor = 0.01f;          // 初期値(安全側)
// Y軸合成係数(ホットパスで直接参照するため)
static float gAimCombinedY   = 0.013333333f;   // 初期値(安全側)

// 再計算関数(感度キャッシュを即時更新するため)
// localCfgから現在のAimとAimYAxisScaleを取り出し、共有キャッシュへ反映
static inline void recalcAimSensitivityCache(Config::Table& localCfg) {
    // 現在のAim感度値取得(再計算入力の取得のため)
    const int   sens          = localCfg.GetInt("Metroid.Sensitivity.Aim");
    // Y軸倍率取得(合成係数算出のため)
    const float aimYAxisScale = static_cast<float>(localCfg.GetDouble("Metroid.Sensitivity.AimYAxisScale"));
    // X係数更新(掛け算のみで済ませるため)
    gAimSensiFactor = sens * 0.01f;
    // Y係数更新(X係数との積で毎フレームの乗算を1回に集約するため)
    gAimCombinedY   = gAimSensiFactor * aimYAxisScale;
}

/**
 * 感度値変換関数.
 *
 *
 * @param sensiVal 感度テーブルの値(uint32_t, 例: 0x00000999).
 * @return 計算された感度(double).
 */
double sensiValToSensiNum(std::uint32_t sensiVal)
{
    // 基準値定義(sensi=1.0に対応させるため)
    constexpr std::uint32_t BASE_VAL = 0x0999;
    // 増分定義(1.0感度あたりの増分を409に固定するため)
    constexpr std::uint32_t STEP_VAL = 0x0199;

    // 差分計算(BASEとの差分を得るため)
    const std::int64_t diff = static_cast<std::int64_t>(sensiVal) - static_cast<std::int64_t>(BASE_VAL);
    // 感度算出(diffをSTEPで正規化し+1.0のオフセットを加えるため)
    return static_cast<double>(diff) / static_cast<double>(STEP_VAL) + 1.0;
}

/**
 * 感度数値→テーブル値変換関数.
 *
 *
 * @param sensiNum 感度数値(double, 例: 1.0).
 * @return 感度テーブル値(uint16_tに収まる範囲). 範囲外は0x0000～0xFFFFにクリップ.
 */
std::uint16_t sensiNumToSensiVal(double sensiNum)
{
    // 基準値定義(sensi=1.0に対応させるため)
    constexpr std::uint32_t BASE_VAL = 0x0999;
    // 増分定義(1.0感度あたり+0x199=409を加算するため)
    constexpr std::uint32_t STEP_VAL = 0x0199;

    // ステップ数計算(sensiNum=1.0基準からの差をとるため)
    double steps = sensiNum - 1.0;

    // 値算出(BASEにステップ数×増分を足すため)
    double val = static_cast<double>(BASE_VAL) + steps * static_cast<double>(STEP_VAL);

    // 丸めてuint32に変換(安全な整数計算のため)
    std::uint32_t result = static_cast<std::uint32_t>(std::llround(val));

    // 下限クリップ(uint16範囲外を防ぐため)
    if (result > 0xFFFF) {
        result = 0xFFFF;
    }

    // 返却(最終的にuint16_tで返すため)
    return static_cast<std::uint16_t>(result);
}



/**
 * ヘッドフォン設定一度適用関数.
 *
 * @param NDS* nds NDS本体参照.
 * @return bool 書き込み実施有無.
 */



 /**
  * ヘッドフォン設定一度適用関数本体定義.
  *
  *
  * @param NDS* nds NDS本体参照.
  * @param Config::Table& localCfg ローカル設定参照.
  * @param uint32_t kCfgAddr 音声設定アドレス.
  * @param bool& isHeadphoneApplied 一度適用済みフラグ.
  * @return bool 書き込み実施有無.
  */
  // ヘッドフォン設定一度適用関数本体開始(起動時一回だけ反映するため)
bool ApplyHeadphoneOnce(NDS* nds, Config::Table& localCfg, uint32_t kCfgAddr, bool& isHeadphoneApplied)
{
    // 事前条件確認(ヌル参照による異常動作を避けるため)
    if (!nds) {
        // 無効参照検出リターン
        return false;
    }

    // 多重適用回避(すでに処理済みなら即リターンするため)
    if (Q_LIKELY(isHeadphoneApplied)) {
        return false;
    }

    // 設定有効判定(ユーザーがヘッドフォン適用を指定しなければ即リターンするため)
    if (Q_LIKELY(!localCfg.GetBool("Metroid.Apply.Headphone"))) {
        return false;
    }

    // 現在値読出し(差分適用と既成ビット確認のため)
    std::uint8_t oldVal = nds->ARM9Read8(kCfgAddr);

    // ターゲットビット定義(bit4:bit3領域を対象にするため)
    constexpr std::uint8_t kAudioFieldMask = 0x18;

    // 既設定判定(すでにbit4:bit3が11bであれば即リターンするため)
    if ((oldVal & kAudioFieldMask) == kAudioFieldMask) {
        // 適用済みフラグ更新(以降呼び出しで再処理しないため)
        isHeadphoneApplied = true;
        // 書き込み無しリターン
        return false;
    }

    // 新値算出(対象ビットのみ1に立てて他ビットを保持するため)
    std::uint8_t newVal = static_cast<std::uint8_t>(oldVal | kAudioFieldMask);

    // 差分無し確認(理論上newVal == oldValにはならないが安全のため)
    if (newVal == oldVal) {
        isHeadphoneApplied = true;
        return false;
    }

    // 8bit書き込み実行(必要最小限の更新で近接フィールド破壊を避けるため)
    nds->ARM9Write8(kCfgAddr, newVal);

    // 適用済みフラグ更新(以降呼び出しで再処理しないため)
    isHeadphoneApplied = true;

    // 書き込み実施リターン
    return true;
}

/**
 * ハンターライセンス色適用（装飾とランクは保持, ブランクは無視）.
 *
 * @param NDS* nds NDS本体ポインタ.
 * @param Config::Table& localCfg 設定テーブル.
 *   - Metroid.HunterLicense.Color.Apply    : bool  (適用ON/OFF).
 *   - Metroid.HunterLicense.Color.Selected : int   (0=青,1=赤,2=緑).
 * @param std::uint32_t addrRankColor ランクと色の格納アドレス（例: 0x220ECF43）.
 * @return bool 書き込み実施有無.
 */
bool applyLicenseColorStrict(NDS* nds, Config::Table& localCfg, std::uint32_t addrRankColor)
{
    // NDSが無効なら処理しない（安全性確保のため）
    if (!nds) return false;

    // 設定で適用OFFなら処理しない
    if (Q_LIKELY(!localCfg.GetBool("Metroid.HunterLicense.Color.Apply")))
        return false;

    // 関数内enum（外部には露出させない）
    enum LicenseColor : int {
        Blue = 0, // bit7–6 = 00
        Red = 1, // bit7–6 = 01
        Green = 2  // bit7–6 = 10
        // Blank (3) は無視
    };

    // 設定から選択色を取得
    int sel = localCfg.GetInt("Metroid.HunterLicense.Color.Selected");

    // 範囲外（ブランク含む）は無視
    if (sel < Blue || sel > Green) return false;

    // LUT定義（色コードを対応するbitパターンに変換するため）
    constexpr std::uint8_t kColorBitsLUT[3] = {
        0x00, // Blue
        0x40, // Red
        0x80  // Green
    };

    // マスク定義（装飾bitとランク保持のため）
    constexpr std::uint8_t KEEP_MASK = 0x3F; // bit5–0
    constexpr std::uint8_t COLOR_MASK = 0xC0; // bit7–6（参照のみ）

    // 新しい色ビット
    const std::uint8_t desiredColorBits = kColorBitsLUT[sel];

    // 現在の値を読み出し
    const std::uint8_t oldVal = nds->ARM9Read8(addrRankColor);

    // 色のみ差し替え
    const std::uint8_t newVal = static_cast<std::uint8_t>((oldVal & KEEP_MASK) | desiredColorBits);

    // 差分なしなら無視
    if (newVal == oldVal) return false;

    // 書き込み実行
    nds->ARM9Write8(addrRankColor, newVal);
    return true;
}

/**
 * メインハンター適用（フェイバリット/状態は完全維持）
 *
 * @param NDS* nds
 * @param Config::Table& localCfg
 *   - Metroid.Apply.Hunter    : bool  (適用ON/OFF)
 *   - Metroid.Hunter.Selected : int   (0=Samus,1=Kanden,2=Trace,3=Sylux,4=Noxus,5=Spire,6=Weavel)
 * @param std::uint32_t addrMainHunter 例: 0x220ECF40 (8bit)
 * @return bool 書き込み実施有無
 */
bool applySelectedHunterStrict(NDS* nds, Config::Table& localCfg, std::uint32_t addrMainHunter)
{
    if (!nds) return false;
    if (Q_LIKELY(!localCfg.GetBool("Metroid.HunterLicense.Hunter.Apply"))) return false;

    // bit 定義
    //constexpr std::uint8_t FAVORITE_MASK = 0x80; // 参照のみ(保持)
    constexpr std::uint8_t HUNTER_MASK = 0x78; // 置換対象 bit6–3
    //constexpr std::uint8_t STATE_MASK = 0x07; // 参照のみ(保持)

    // 選択ハンター → bit6–3 パターン
    constexpr std::uint8_t kHunterBitsLUT[7] = {
        0x00, // Samus
        0x08, // Kanden
        0x10, // Trace
        0x18, // Sylux
        0x20, // Noxus
        0x28, // Spire
        0x30  // Weavel
    };
    int sel = localCfg.GetInt("Metroid.HunterLicense.Hunter.Selected");
    if (sel < 0) sel = 0;
    if (sel > 6) sel = 6;
    const std::uint8_t desiredHunterBits = static_cast<std::uint8_t>(kHunterBitsLUT[sel] & HUNTER_MASK);

    // 読み取り
    const std::uint8_t oldVal = nds->ARM9Read8(addrMainHunter);

    // ハンター種別のみ差し替え（bit7・bit2–0はそのまま）
    const std::uint8_t newVal = static_cast<std::uint8_t>((oldVal & ~HUNTER_MASK) | desiredHunterBits);

    if (newVal == oldVal) return false;      // 変更なし
    nds->ARM9Write8(addrMainHunter, newVal); // 書き込み実行
    return true;
}


/**
 * DS Name使用設定適用関数.
 *
 *
 * @param NDS* nds NDS本体参照.
 * @param Config::Table& localCfg ローカル設定参照.
 * @param std::uint32_t addrDsNameFlagAndMicVolume DS Name Flag/マイク音量共用アドレス.
 * @return bool 書き込み実施有無.
 */
bool useDsName(NDS* nds, Config::Table& localCfg, std::uint32_t addrDsNameFlagAndMicVolume)
{
    // 事前条件確認(ヌル参照による異常動作を避けるため)
    if (!nds) {
        // 無効参照検出リターン
        return false;
    }

    // 設定有効判定(ユーザーがDSの名前を使う指定をしていなければ即リターンするため)
    if (Q_LIKELY(!localCfg.GetBool("Metroid.Use.Firmware.Name"))) return false;


    // 現在値読み込み(フラグの状態を確認するため)
    std::uint8_t oldVal = nds->ARM9Read8(addrDsNameFlagAndMicVolume);

    // 対象ビットマスク(bit0をフラグとして扱うため)
    constexpr std::uint8_t kFlagMask = 0x01;

    // 新しい値を算出(useDsNameSettingの真偽に応じてビット操作を行うため)
    std::uint8_t newVal;
    
    // 設定がtrue → DS名設定済みフラグをオフに設定
    // (bit0を下ろしてDSの名前を使うようにするため)
    newVal = static_cast<std::uint8_t>(oldVal & ~kFlagMask);

    // 差分確認(値に変化がなければ何もせずリターンするため)
    if (newVal == oldVal) {
        return false;
    }

    // 8bit書き込み実行(必要な場合のみ書き込みを行うため)
    nds->ARM9Write8(addrDsNameFlagAndMicVolume, newVal);

    // 書き込み実施リターン
    return true;
}


/**
 * MPH感度設定反映関数.
 *
 * @param NDS* nds NDS本体参照.
 * @param Config::Table& localCfg ローカル設定参照.
 * @param std::uint32_t addrSensitivity 感度設定アドレス.
 * @return bool 書き込み実施有無.
 */
void ApplyMphSensitivity(NDS* nds, Config::Table& localCfg, std::uint32_t addrSensitivity, std::uint32_t addrInGameSensi, bool isInGameAndHasInitialized)
{
    // 現在の感度数値を設定から取得(ユーザー入力を読むため)
    double mphSensitivity = localCfg.GetDouble("Metroid.Sensitivity.Mph");

    // 感度数値をROMに適用可能な形式へ変換(ゲーム内部値に一致させるため)
    std::uint32_t sensiVal = sensiNumToSensiVal(mphSensitivity);

    // NDSメモリに16bit値を書き込み(ゲーム設定を即時反映するため)
    nds->ARM9Write16(addrSensitivity, static_cast<std::uint16_t>(sensiVal));

    if (isInGameAndHasInitialized) {
        nds->ARM9Write16(addrInGameSensi, static_cast<std::uint16_t>(sensiVal));
    }
}

/**
 * MPHハンター/マップ解除一度適用関数.
 *
 * @param NDS* nds NDS本体参照.
 * @param Config::Table& localCfg ローカル設定参照.
 * @param bool& isUnlockApplied 一度適用済みフラグ.
 * @param std::uint32_t addrUnlock1 書き込みアドレス1.
 * @param std::uint32_t addrUnlock2 書き込みアドレス2.
 * @param std::uint32_t addrUnlock3 書き込みアドレス3.
 * @param std::uint32_t addrUnlock4 書き込みアドレス4.
 * @param std::uint32_t addrUnlock5 書き込みアドレス5.
 * @return bool 書き込み実施有無.
 */
bool ApplyUnlockHuntersMaps(
    NDS* nds,
    Config::Table& localCfg,
    bool& isUnlockApplied,
    std::uint32_t addrUnlock1,
    std::uint32_t addrUnlock2,
    std::uint32_t addrUnlock3,
    std::uint32_t addrUnlock4,
    std::uint32_t addrUnlock5)
{

    // 既適用チェック(多重実行防止のため)
    if (Q_LIKELY(isUnlockApplied)) {
        return false;
    }

    // 設定フラグ確認(ユーザー指定OFFなら処理不要のため)
    if (Q_LIKELY(!localCfg.GetBool("Metroid.Data.Unlock"))) {
        return false;
    }

    // 解除データを書き込み(ゲーム内部のロック解除のため)


    // 全サウンドテストアンロック

    // 現在値読出し(既存フラグ保持のため)
    std::uint8_t cur = nds->ARM9Read8(addrUnlock1);

    // 必要ビットを立てた値を算出(既存値を壊さないため)
    std::uint8_t newVal = static_cast<std::uint8_t>(cur | 0x03);

    // 書き込み実行(解除レジスタの該当ビットを立てるため)
    nds->ARM9Write8(addrUnlock1, newVal);
			
    // 全マップアンロック
    nds->ARM9Write32(addrUnlock2, 0x07FFFFFF);
			
    // 全ハンターアンロック
    nds->ARM9Write8(addrUnlock3, 0x7F);

    // 全ギャラリーアンロック
    nds->ARM9Write32(addrUnlock4, 0xFFFFFFFF);
    nds->ARM9Write8(addrUnlock5, 0xFF);

    // 適用済みフラグ更新(再実行を避けるため)
    isUnlockApplied = true;

    // 書き込み実施結果返却(呼び出し元で処理分岐を可能にするため)
    return true;
}


// CalculatePlayerAddress Function
__attribute__((always_inline, flatten)) inline uint32_t calculatePlayerAddress(uint32_t baseAddress, uint8_t playerPosition, int32_t increment) {
    // If player position is 0, return the base address without modification
    if (playerPosition == 0) {
        return baseAddress;
    }

    // Calculate using 64-bit integers to prevent overflow
    // Use playerPosition as is (no subtraction)
    int64_t result = static_cast<int64_t>(baseAddress) + (static_cast<int64_t>(playerPosition) * increment);

    // Ensure the result is within the 32-bit range
    if (result < 0 || result > UINT32_MAX) {
        return baseAddress;  // Return the original address if out of range
    }

    return static_cast<uint32_t>(result);
}

bool isAltForm;
bool isInGame = false; // MelonPrimeDS
bool isLayoutChangePending = true;       // MelonPrimeDS layout change flag - set true to trigger on first run
bool isSnapTapMode = false;
bool isUnlockHuntersMaps = false;
// グローバル適用フラグ定義(多重適用を避けるため)
bool isHeadphoneApplied = false;


melonDS::u32 addrBaseIsAltForm;
melonDS::u32 addrBaseLoadedSpecialWeapon;
melonDS::u32 addrBaseWeaponChange;
melonDS::u32 addrBaseSelectedWeapon;
melonDS::u32 addrBaseChosenHunter;
melonDS::u32 addrBaseJumpFlag;
melonDS::u32 addrInGame;
melonDS::u32 addrPlayerPos;
melonDS::u32 addrIsInVisorOrMap;
melonDS::u32 addrBaseAimX;
melonDS::u32 addrBaseAimY;
melonDS::u32 addrAimX;
melonDS::u32 addrAimY;
melonDS::u32 addrIsInAdventure;
melonDS::u32 addrIsMapOrUserActionPaused; // for issue in AdventureMode, Aim Stopping when SwitchingWeapon. 
melonDS::u32 addrOperationAndSound; // ToSet headphone. 8bit write
melonDS::u32 addrUnlockMapsHunters; // Sound Test Open, SFX Volum Addr
melonDS::u32 addrUnlockMapsHunters2; // All maps open addr
melonDS::u32 addrUnlockMapsHunters3; // All hunters open addr
melonDS::u32 addrUnlockMapsHunters4; // All gallery open addr
melonDS::u32 addrUnlockMapsHunters5; // All gallery open addr 2
melonDS::u32 addrSensitivity;
melonDS::u32 addrDsNameFlagAndMicVolume;
melonDS::u32 addrMainHunter;
melonDS::u32 addrRankColor;
melonDS::u32 addrBaseInGameSensi;
melonDS::u32 addrInGameSensi;
// melonDS::u32 addrLanguage;
static bool isUnlockMapsHuntersApplied = false;

// ROM detection and address setup (self-contained within this function)
__attribute__((always_inline, flatten)) inline void detectRomAndSetAddresses(EmuInstance* emuInstance) {
    // Define ROM groups
    /*
    enum RomGroup {
        GROUP_US1_1,     // US1.1, US1.1_ENCRYPTED
        GROUP_US1_0,     // US1.0, US1.0_ENCRYPTED
        GROUP_EU1_1,     // EU1.1, EU1.1_ENCRYPTED, EU1_1_BALANCED
        GROUP_EU1_0,     // EU1.0, EU1.0_ENCRYPTED
        GROUP_JP1_0,     // JP1.0, JP1.0_ENCRYPTED
        GROUP_JP1_1,     // JP1.1, JP1.1_ENCRYPTED
        GROUP_KR1_0,     // KR1.0, KR1.0_ENCRYPTED
    };
    */

    // ROM information structure
    struct RomInfo {
        uint32_t checksum;
        const char* name;
        RomGroup group;
    };

    // Mapping of checksums to ROM info (stack-allocated)
    const RomInfo ROM_INFO_TABLE[] = {
        {RomVersions::US1_1,           "US1.1",           GROUP_US1_1},
        {RomVersions::US1_1_ENCRYPTED, "US1.1 ENCRYPTED", GROUP_US1_1},
        {RomVersions::US1_0,           "US1.0",           GROUP_US1_0},
        {RomVersions::US1_0_ENCRYPTED, "US1.0 ENCRYPTED", GROUP_US1_0},
        {RomVersions::EU1_1,           "EU1.1",           GROUP_EU1_1},
        {RomVersions::EU1_1_ENCRYPTED, "EU1.1 ENCRYPTED", GROUP_EU1_1},
        {RomVersions::EU1_1_BALANCED,  "EU1.1 BALANCED",  GROUP_EU1_1},
        {RomVersions::EU1_0,           "EU1.0",           GROUP_EU1_0},
        {RomVersions::EU1_0_ENCRYPTED, "EU1.0 ENCRYPTED", GROUP_EU1_0},
        {RomVersions::JP1_0,           "JP1.0",           GROUP_JP1_0},
        {RomVersions::JP1_0_ENCRYPTED, "JP1.0 ENCRYPTED", GROUP_JP1_0},
        {RomVersions::JP1_1,           "JP1.1",           GROUP_JP1_1},
        {RomVersions::JP1_1_ENCRYPTED, "JP1.1 ENCRYPTED", GROUP_JP1_1},
        {RomVersions::KR1_0,           "KR1.0",           GROUP_KR1_0},
        {RomVersions::KR1_0_ENCRYPTED, "KR1.0 ENCRYPTED", GROUP_KR1_0},
    };

    // Search ROM info from checksum
    const RomInfo* romInfo = nullptr;
    for (const auto& info : ROM_INFO_TABLE) {
        if (globalChecksum == info.checksum) {
            romInfo = &info;
            break;
        }
    }

    // If ROM is unsupported
    if (!romInfo) {
        return;
    }

    // ---- ここで呼ぶ！ ----
    // グループを一度だけローカル変数に保存
    RomGroup detectedGroup = romInfo->group;

    // SetRomGroupFlags(detectedGroup);   // ★ ROMグループのフラグ設定 ★

    // 既存変数への一括設定実施(分岐削減のため)
    // グローバル列挙体の完全修飾指定とキャスト実施(同名列挙体のシャドーイング回避のため)
    detectRomAndSetAddresses_fast(
        // 列挙体の完全修飾(::RomGroup)とint変換後のstatic_cast実施(型不一致解消のため)
        detectedGroup,
        // ChosenHunterアドレス引数受け渡し実施(既存変数の直接利用のため)
        addrBaseChosenHunter,
        // inGameアドレス引数受け渡し実施(既存変数の直接利用のため)
        addrInGame,
        // プレイヤ位置アドレス引数受け渡し実施(既存変数の直接利用のため)
        addrPlayerPos,
        // AltFormアドレス引数受け渡し実施(既存変数の直接利用のため)
        addrBaseIsAltForm,
        // 武器変更アドレス引数受け渡し実施(既存変数の直接利用のため)
        addrBaseWeaponChange,
        // 選択武器アドレス引数受け渡し実施(既存変数の直接利用のため)
        addrBaseSelectedWeapon,
        // AimXアドレス引数受け渡し実施(既存変数の直接利用のため)
        addrBaseAimX,
        // AimYアドレス引数受け渡し実施(既存変数の直接利用のため)
        addrBaseAimY,
        // ADV/Multi判定アドレス引数受け渡し実施(既存変数の直接利用のため)
        addrIsInAdventure,
        // ポーズ判定アドレス引数受け渡し実施(既存変数の直接利用のため)
        addrIsMapOrUserActionPaused,
        addrUnlockMapsHunters,
        addrSensitivity,
        addrMainHunter
    );

    // Addresses calculated from base values
    addrIsInVisorOrMap = addrPlayerPos - 0xABB;
    addrBaseLoadedSpecialWeapon = addrBaseIsAltForm + 0x56;
    addrBaseJumpFlag = addrBaseSelectedWeapon - 0xA;
    addrOperationAndSound = addrUnlockMapsHunters - 0x1;
    addrUnlockMapsHunters2 = addrUnlockMapsHunters + 0x3;
    addrUnlockMapsHunters3 = addrUnlockMapsHunters + 0x7;
    addrUnlockMapsHunters4 = addrUnlockMapsHunters + 0xB;
    addrUnlockMapsHunters5 = addrUnlockMapsHunters + 0xF;
    addrDsNameFlagAndMicVolume = addrUnlockMapsHunters5 + 0x1;
    // addrLanguage = addrUnlockMapsHunters - 0xB1;
    addrBaseInGameSensi = addrSensitivity + 0xB4;
    // addrInGameSensi = addrBaseInGameSensi;
    addrRankColor = addrMainHunter + 0x3;
    isRomDetected = true;

    // ROM detection message
    char message[256];
    sprintf(message, "MPH Rom version detected: %s", romInfo->name);
    emuInstance->osdAddMessage(0, message);

    // ROM判定後の処理

    // フラグリセット(起動後初期適用のため)
    isUnlockMapsHuntersApplied = false;
    isHeadphoneApplied = false;

    // 感度キャッシュ初期化(ホットパス無関与で一度だけ反映するため)
    recalcAimSensitivityCache(emuInstance->getLocalConfig());

    // Apply Aim Adjust setting
    applyAimAdjustSetting(emuInstance->getLocalConfig());
}

































// MelonPrimeDS input handling for joystics
/*
static MelonPrimeXInputFilter* g_xin = nullptr;
static MelonPrimeDirectInputFilter* g_din = nullptr;
*/

/* MelonPrimeDS function goes here */
void EmuThread::run()
{

#ifdef _WIN32
    // RawMouseInput
    //RawInputThread* rawInputThread = new RawInputThread(parent());
    //rawInputThread->start();

// ヘッダ参照(クラス宣言のため)
// #include "MelonPrimeRawInputWinFilter.h"
// アプリケーション参照(QCoreApplicationのため)
#include <QCoreApplication>

// 静的ポインタ定義(単一インスタンス保持のため)
        // static RawInputWinFilter* g_rawFilter = nullptr;

        // どこか一度だけ(例: EmuThread::run の前段やMainWindow生成時)
    if (!g_rawFilter) {
        g_rawFilter = new RawInputWinFilter();
        // emuInstance が生きていて、Raw フィルタが作られた直後あたりで
        /*
        if (g_rawFilter) {
            g_rawFilter->setJoyHotkeyMaskPtr(&emuInstance->joyHotkeyMask);
        }
        */
        qApp->installNativeEventFilter(g_rawFilter);
        // 新: 全HK（Shoot/Zoom 以外も）をRawに登録
        BindMetroidHotkeysFromConfig(g_rawFilter, /*instance*/ emuInstance->getInstanceID());
    }
    /*
    if (!g_xin) {
        g_xin = new MelonPrimeXInputFilter();
        g_xin->autoPickUserIndex();
        g_xin->setDeadzone(7849, 8689, 30);
        MelonPrime_BindMetroidHotkeysFromJoystickConfig(g_xin, emuInstance->getInstanceID());
    }

    // グローバル or EmuThreadメンバ
    if (!g_din) {
        g_din = new MelonPrimeDirectInputFilter();

        // Qt のウィンドウハンドル → HWND
        MainWindow* mw = emuInstance->getMainWindow();
        HWND hwnd = reinterpret_cast<HWND>(mw ? mw->winId() : 0);

        g_din->init(hwnd); // ※あなたの DirectInput フィルタの初期化API名に合わせて
        MelonPrime_BindMetroidHotkeysFromJoystickConfig(g_din, emuInstance->getInstanceID());
    }
    */
#endif


    static const QBitArray& hotkeyMask = emuInstance->hotkeyMask;
    static QBitArray& inputMask = emuInstance->inputMask;
    static const QBitArray& hotkeyPress = emuInstance->hotkeyPress;

// ---- Hotkey boolean macros (WIN: RawInput + XInput / Non-WIN: Qt) ----
#if defined(_WIN32)

    // ジョイだけ版
#define MP_JOY_DOWN(id)      (emuInstance->joyHotkeyMask.testBit((id)))
#define MP_JOY_PRESSED(id)   (emuInstance->joyHotkeyPress.testBit((id)))
#define MP_JOY_RELEASED(id)  (emuInstance->joyHotkeyRelease.testBit((id)))

//#define MP_HK_DOWN(id)     ( ((g_rawFilter && g_rawFilter->hotkeyDown((id)))     || (g_xin && g_xin->hotkeyDown((id)))     || (g_din && g_din->hotkeyDown((id))) ) )
//#define MP_HK_PRESSED(id)  ( ((g_rawFilter && g_rawFilter->hotkeyPressed((id)))  || (g_xin && g_xin->hotkeyPressed((id)))  || (g_din && g_din->hotkeyPressed((id))) ) )
//#define MP_HK_RELEASED(id) ( ((g_rawFilter && g_rawFilter->hotkeyReleased((id))) || (g_xin && g_xin->hotkeyReleased((id))) || (g_din && g_din->hotkeyReleased((id))) ) )

//#define MP_HK_DOWN(id)     ( (g_rawFilter && g_rawFilter->hotkeyDown((id)))   ) 
//#define MP_HK_PRESSED(id)  ( (g_rawFilter && g_rawFilter->hotkeyPressed((id))) )
//#define MP_HK_RELEASED(id) ( (g_rawFilter && g_rawFilter->hotkeyReleased((id))) )

#define MP_HK_DOWN(id)     ( (g_rawFilter && g_rawFilter->hotkeyDown((id)))     || MP_JOY_DOWN((id)) )
#define MP_HK_PRESSED(id)  ( (g_rawFilter && g_rawFilter->hotkeyPressed((id)))  || MP_JOY_PRESSED((id)) )
#define MP_HK_RELEASED(id) ( (g_rawFilter && g_rawFilter->hotkeyReleased((id))) || MP_JOY_RELEASED((id)) )


#else
#define MP_HK_DOWN(id)     ( hotkeyMask.testBit((id)) )
#define MP_HK_PRESSED(id)  ( hotkeyPress.testBit((id)) )
#define MP_HK_RELEASED(id) ( hotkeyRelease.testBit((id)) )
#endif

        // #define TOUCH_IF(PRESS, X, Y) if (hotkeyPress.testBit(PRESS)) { emuInstance->nds->ReleaseScreen(); frameAdvanceTwice(); emuInstance->nds->TouchScreen(X, Y); frameAdvanceTwice(); }

#define TOUCH_IF(PRESS, X, Y)                                         \
    if (MP_HK_PRESSED(PRESS)) {           \
        emuInstance->nds->ReleaseScreen();                            \
        frameAdvanceTwice();                                           \
        emuInstance->nds->TouchScreen((X), (Y));                      \
        frameAdvanceTwice();                                           \
    }

    Config::Table& globalCfg = emuInstance->getGlobalConfig();
    Config::Table& localCfg = emuInstance->getLocalConfig();
    isSnapTapMode = localCfg.GetBool("Metroid.Operation.SnapTap"); // MelonPrimeDS
    u32 mainScreenPos[3];

    //emuInstance->updateConsole();
    // No carts are inserted when melonDS first boots

    mainScreenPos[0] = 0;
    mainScreenPos[1] = 0;
    mainScreenPos[2] = 0;
    autoScreenSizing = 0;

    //videoSettingsDirty = false;

    if (emuInstance->usesOpenGL())
    {
        emuInstance->initOpenGL(0);

        useOpenGL = true;
        videoRenderer = globalCfg.GetInt("3D.Renderer");
    }
    else
    {
        useOpenGL = false;
        videoRenderer = 0;
    }

    //updateRenderer();
    videoSettingsDirty = true;

    u32 nframes = 0;
    double perfCountsSec = 1.0 / SDL_GetPerformanceFrequency();
    double lastTime = SDL_GetPerformanceCounter() * perfCountsSec;
    double frameLimitError = 0.0;
    double lastMeasureTime = lastTime;

    u32 winUpdateCount = 0, winUpdateFreq = 1;
    u8 dsiVolumeLevel = 0x1F;

    char melontitle[100];

    bool fastforward = false;
    bool slowmo = false;
    emuInstance->fastForwardToggled = false;
    emuInstance->slowmoToggled = false;


    // melonPrimeDS

    bool isCursorVisible = true;
    bool wasLastFrameFocused = false;

    /**
     * @brief Function to show or hide the cursor on MelonPrimeDS
     *
     * Controls the mouse cursor visibility state on MelonPrimeDS. Does nothing if
     * the requested state is the same as the current state. When changing cursor
     * visibility, uses Qt::QueuedConnection to safely execute on the UI thread.
     *
     * @param show true to show the cursor, false to hide it
     */
    auto showCursorOnMelonPrimeDS = [&](bool show) __attribute__((always_inline, flatten)) {
        // Do nothing if the requested state is the same as current state (optimization)
        if (show == isCursorVisible) return;

        // Get and verify panel exists
        auto* panel = emuInstance->getMainWindow()->panel;
        if (!panel) return;

        // Use Qt::QueuedConnection to safely execute on the UI thread
        QMetaObject::invokeMethod(panel,
            [panel, show]() {
                // Set cursor visibility (normal ArrowCursor or invisible BlankCursor)
                panel->setCursor(show ? Qt::ArrowCursor : Qt::BlankCursor);
                if (show) {
                    panel->unclip();
                }
                else {
                    panel->clipCursorCenter1px();// Clip Cursor
                }
            },
            Qt::ConnectionType::QueuedConnection
        );

        // Record the state change
        isCursorVisible = show;
    };

    // #define STYLUS_MODE 1 // this is for stylus user. MelonEK

#define INPUT_A 0
#define INPUT_B 1
#define INPUT_SELECT 2
#define INPUT_START 3
#define INPUT_RIGHT 4
#define INPUT_LEFT 5
#define INPUT_UP 6
#define INPUT_DOWN 7
#define INPUT_R 8
#define INPUT_L 9
#define INPUT_X 10
#define INPUT_Y 11

/*
#define FN_INPUT_PRESS(i)   emuInstance->inputMask.setBit(i, false) // No semicolon at the end here
#define FN_INPUT_RELEASE(i) emuInstance->inputMask.setBit(i, true)  // No semicolon at the end here

// Optimized macro definitions - perform direct bit operations instead of using setBit()
// #define FN_INPUT_PRESS(i)   emuInstance->inputMask[i] = false   // Direct assignment for press
// #define FN_INPUT_RELEASE(i) emuInstance->inputMask[i] = true    // Direct assignment for release

    /*
#define PERFORM_TOUCH(x, y) do { \
    emuInstance->nds->ReleaseScreen(); \
    frameAdvanceTwice(); \
    emuInstance->nds->TouchScreen(x, y); \
    frameAdvanceTwice(); \
} while(0)
*/
/*
//
    static const auto PERFORM_TOUCH = [&](int x, int y) __attribute__((always_inline)) {
        emuInstance->nds->ReleaseScreen();
        frameAdvanceTwice();
        emuInstance->nds->TouchScreen(x, y);
        frameAdvanceTwice();
    };
    */

    /**
     * Macro to obtain a 12-bit input state from a QBitArray as a bitmask.
     * Originally defined in EmuInstanceInput.cpp.
     *
     * @param input QBitArray (must have at least 12 bits).
     * @return uint32_t Bitmask containing input state (bits 0–11).
     */
#define GET_INPUT_MASK(inputMask) (                                          \
    (static_cast<uint32_t>((inputMask).testBit(0))  << 0)  |                 \
    (static_cast<uint32_t>((inputMask).testBit(1))  << 1)  |                 \
    (static_cast<uint32_t>((inputMask).testBit(2))  << 2)  |                 \
    (static_cast<uint32_t>((inputMask).testBit(3))  << 3)  |                 \
    (static_cast<uint32_t>((inputMask).testBit(4))  << 4)  |                 \
    (static_cast<uint32_t>((inputMask).testBit(5))  << 5)  |                 \
    (static_cast<uint32_t>((inputMask).testBit(6))  << 6)  |                 \
    (static_cast<uint32_t>((inputMask).testBit(7))  << 7)  |                 \
    (static_cast<uint32_t>((inputMask).testBit(8))  << 8)  |                 \
    (static_cast<uint32_t>((inputMask).testBit(9))  << 9)  |                 \
    (static_cast<uint32_t>((inputMask).testBit(10)) << 10) |                 \
    (static_cast<uint32_t>((inputMask).testBit(11)) << 11)                   \
)

    uint8_t playerPosition;
    const uint16_t incrementOfPlayerAddress = 0xF30;
    const uint8_t incrementOfAimAddr = 0x48;
    uint32_t addrIsAltForm;
    uint32_t addrLoadedSpecialWeapon;
    uint32_t addrChosenHunter;
    uint32_t addrWeaponChange;
    uint32_t addrSelectedWeapon;
    uint32_t addrJumpFlag;

    uint32_t addrHavingWeapons;
    uint32_t addrCurrentWeapon;

    uint32_t addrBoostGauge;
    uint32_t addrIsBoosting;

    uint32_t addrWeaponAmmo;

    // uint32_t isPrimeHunterAddr;


    bool isRoundJustStarted;
    bool isInAdventure;
    bool isSamus;

    bool isWeavel;
    bool isPaused = false; // MelonPrimeDS

    // The QPoint class defines a point in the plane using integer precision. 
    // auto mouseRel = rawInputThread->fetchMouseDelta();
    // QPoint mouseRel;

    // Initialize Adjusted Center 


    // test
    // Lambda function to get adjusted center position based on window geometry and screen layout
#ifndef STYLUS_MODE
    static const auto getAdjustedCenter = [&]()__attribute__((hot, always_inline, flatten)) -> QPoint {
        // Cache static constants outside the function to avoid recomputation
        static constexpr float DEFAULT_ADJUSTMENT = 0.25f;
        static constexpr float HYBRID_RIGHT = 0.333203125f;  // (2133-1280)/2560 = 85/256  = (n * 85) >> 8
        static constexpr float HYBRID_LEFT = 0.166796875f;   // (1280-853)/2560 = 43/256  =  (n * 43) >> 8
        static QPoint adjustedCenter;
        static bool lastFullscreen = false;
        static bool lastSwapped = false;
        static int lastScreenSizing = -1;
        static int lastLayout = -1;

        auto& windowCfg = emuInstance->getMainWindow()->getWindowConfig();

        // Fast access to current settings - get all at once
        int currentLayout = windowCfg.GetInt("ScreenLayout");
        int currentScreenSizing = windowCfg.GetInt("ScreenSizing");
        bool currentSwapped = windowCfg.GetBool("ScreenSwap");
        bool currentFullscreen = emuInstance->getMainWindow()->isFullScreen();

        // Return cached value if settings haven't changed
        if (currentLayout == lastLayout &&
            currentScreenSizing == lastScreenSizing &&
            currentSwapped == lastSwapped &&
            currentFullscreen == lastFullscreen) {
            return adjustedCenter;
        }

        // Update cached settings
        lastLayout = currentLayout;
        lastScreenSizing = currentScreenSizing;
        lastSwapped = currentSwapped;
        lastFullscreen = currentFullscreen;

        // Get display dimensions once
        const QRect& displayRect = emuInstance->getMainWindow()->panel->geometry();
        const int displayWidth = displayRect.width();
        const int displayHeight = displayRect.height();

        // Calculate base center position
        adjustedCenter = emuInstance->getMainWindow()->panel->mapToGlobal(
            QPoint(displayWidth >> 1, displayHeight >> 1)
        );

        // Fast path for special cases
        if (currentScreenSizing == screenSizing_TopOnly) {
            return adjustedCenter;
        }

        if (currentScreenSizing == screenSizing_BotOnly) {
            if (currentFullscreen) {
                // Precompute constants to avoid repeated multiplications
                const float widthAdjust = displayWidth * 0.4f;
                const float heightAdjust = displayHeight * 0.4f;

                adjustedCenter.rx() -= static_cast<int>(widthAdjust);
                adjustedCenter.ry() -= static_cast<int>(heightAdjust);
            }
            return adjustedCenter;
        }

        // Fast path for Hybrid layout with swap
        if (currentLayout == screenLayout_Hybrid && currentSwapped) {
            // Directly compute result for this specific case
            adjustedCenter.rx() += static_cast<int>(displayWidth * HYBRID_RIGHT);
            adjustedCenter.ry() -= static_cast<int>(displayHeight * DEFAULT_ADJUSTMENT);
            return adjustedCenter;
        }

        // For other cases, determine adjustment values
        float xAdjust = 0.0f;
        float yAdjust = 0.0f;

        // Simplified switch with fewer branches
        switch (currentLayout) {
        case screenLayout_Natural:
        case screenLayout_Vertical:
            yAdjust = DEFAULT_ADJUSTMENT;
            break;
        case screenLayout_Horizontal:
            xAdjust = DEFAULT_ADJUSTMENT;
            break;
        case screenLayout_Hybrid:
            // We already handled the swapped case above
            xAdjust = HYBRID_LEFT;
            break;
        }

        // Apply non-zero adjustments only
        if (xAdjust != 0.0f) {
            // Using direct ternary operator instead of swapFactor variable
            adjustedCenter.rx() += static_cast<int>(displayWidth * xAdjust * (currentSwapped ? 1.0f : -1.0f));
        }

        if (yAdjust != 0.0f) {
            // Using direct ternary operator instead of swapFactor variable
            adjustedCenter.ry() += static_cast<int>(displayHeight * yAdjust * (currentSwapped ? 1.0f : -1.0f));
        }

        return adjustedCenter;
    };
#endif


    // processMoveInputFunction{
        /**
 * 移動入力処理 v7 QBitArray操作をSIMD風に最適化.
 *
 *
 * @note x86_64向けに分岐最小化とアクセス回数削減を徹底.
         押しっぱなしでも移動できるようにすること。
 *       snapTapモードじゃないときは、左右キーを同時押しで左右移動をストップしないといけない。上下キーも同様。
 *       通常モードの同時押しキャンセルは LUT によってすでに表現されている」
 *       snapTapの時は左を押しているときに右を押しても右移動できる。上下も同様。
 *
 *       読み取りはtestBitで確定値取得. 書き込みはsetBit/clearBitで確定反映.
 *       snapTapの優先ロジックはビット演算で維持し、水平/垂直の競合は同値判定で分岐レスに処理.
 *       通常モードの同時押しキャンセルは既存LUT表現を厳守.
 *       snapTapでは新規押下が競合時に優先側を上書き保持.
 * .
 */
    static const auto processMoveInput = [&](QBitArray& mask)
        __attribute__((hot, always_inline, flatten))
    {
        alignas(64) static constexpr uint32_t MaskLUT[16] = {
            0x0F0F0F0F, 0x0F0F0F0E, 0x0F0F0E0F, 0x0F0F0F0F,
            0x0F0E0F0F, 0x0F0E0F0E, 0x0F0E0E0F, 0x0F0E0F0F,
            0x0E0F0F0F, 0x0E0F0F0E, 0x0E0F0E0F, 0x0E0F0F0F,
            0x0F0F0F0F, 0x0F0F0F0E, 0x0F0F0E0F, 0x0F0F0F0F
        };

        static uint16_t snapState = 0;

        const uint32_t curr =
            MP_HK_DOWN(HK_MetroidMoveForward) |
            (MP_HK_DOWN(HK_MetroidMoveBack) << 1) |
            (MP_HK_DOWN(HK_MetroidMoveLeft) << 2) |
            (MP_HK_DOWN(HK_MetroidMoveRight) << 3);

        uint32_t finalInput;

        if (Q_LIKELY(!isSnapTapMode)) {
            finalInput = curr;
        }
        else {
            const uint32_t last = snapState & 0xFFu;
            const uint32_t priority = snapState >> 8;
            const uint32_t newPress = curr & ~last;

            const uint32_t conflict =
                (((curr & 0x3u) ^ 0x3u) ? 0u : 0x3u) |
                (((curr & 0xCu) ^ 0xCu) ? 0u : 0xCu);

            const uint32_t updateMask = -((newPress & conflict) != 0u);
            const uint32_t newPriority =
                (priority & ~(conflict & updateMask)) | (newPress & conflict & updateMask);
            const uint32_t activePriority = newPriority & curr;

            snapState = static_cast<uint16_t>((curr & 0xFFu) | ((activePriority & 0xFFu) << 8));
            finalInput = (curr & ~conflict) | (activePriority & conflict);
        }

        // ★ マスク適用の最適化：ループアンロール手動化
        const uint32_t mb = MaskLUT[finalInput];

        // 方向ごとに分岐レス化（CMOVcc命令生成を期待）
        constexpr int inputs[4] = { INPUT_UP, INPUT_DOWN, INPUT_LEFT, INPUT_RIGHT };
        constexpr int shifts[4] = { 0, 8, 16, 24 };

#pragma GCC unroll 4
        for (int i = 0; i < 4; ++i) {
            const bool bit = (mb >> shifts[i]) & 0x1;
            bit ? mask.setBit(inputs[i]) : mask.clearBit(inputs[i]);
        }
    };
    // /processMoveInputFunction }






 

    /**
     * Aim input processing (QCursor-based, structure-preserving, low-latency, drift-prevention version).
     *
     * @note Minimizes hot-path branching and reduces QPoint copying.
     *       Sensitivity recalculation is performed only once via flag monitoring.
     *       AIM_ADJUST is fixed as a safe single-evaluation macro with lightweight ±1 range snapping.
     */
    static const auto processAimInput = [&]() __attribute__((hot, always_inline, flatten)) {
#ifndef STYLUS_MODE
        if(isAimDisabled) {
            return;
		}

// Hot path branch (fast processing when focus is maintained and layout is unchanged)
#if defined(_WIN32)
        // noIf
#else
        // Structure definition for aim processing (to improve cache locality)
        struct alignas(64) {
            // Store center X coordinate (maintain origin for delta calculation)
            int centerX;
            // Store center Y coordinate (maintain origin for delta calculation)
            int centerY;
        } static aimData = { 0, 0 };
        if (Q_LIKELY(!isLayoutChangePending && wasLastFrameFocused))
#endif
        {
            // フォーカス時かつレイアウト変更なしの場合の処理
            int deltaX = 0, deltaY = 0;


#if defined(_WIN32)
            /* ==== Raw Input 経路==== */
            do {
                // emuInstance->osdAddMessage(0, "raw");

                g_rawFilter->fetchMouseDelta(deltaX, deltaY);
            } while (0);


#else
            // Get current mouse coordinates (for delta calculation input)
            const QPoint currentPos = QCursor::pos();
            // Extract X coordinate (early retrieval from QPoint)
            const int posX = currentPos.x();
            // Extract Y coordinate (early retrieval from QPoint)
            const int posY = currentPos.y();

            // Calculate X delta (preserve raw value before scaling)
            deltaX = posX - aimData.centerX;
            // Calculate Y delta (preserve raw value before scaling)
            deltaY = posY - aimData.centerY;
#endif

            // Early exit if no movement (prevent unnecessary processing)
            //if (!(deltaX | deltaY)) return;
            if ((deltaX | deltaY) == 0) return;

            // 係数は共有キャッシュから直接取得(ホットパスでの再計算排除のため)
            float scaledX = deltaX * gAimSensiFactor;
            float scaledY = deltaY * gAimCombinedY;

            AIM_ADJUST(scaledX, scaledY);

            // NDS書き込み
            emuInstance->nds->ARM9Write16(addrAimX, static_cast<int16_t>(scaledX));
            emuInstance->nds->ARM9Write16(addrAimY, static_cast<int16_t>(scaledY));

            // Set aim enable flag (for conditional processing downstream)
            // enableAim = true;


#if defined(_WIN32)

#else
            // Return cursor to center (keep next delta calculation zero-based)
            QCursor::setPos(aimData.centerX, aimData.centerY);
#endif


            // End processing (avoid unnecessary branching)
            return;
        }


#if defined(_WIN32)

#else
        // Recalculate center coordinates (for layout changes and initialization)
        const QPoint center = getAdjustedCenter();
        // Update center X (set origin for next delta calculation)
        aimData.centerX = center.x();
        // Update center Y (set origin for next delta calculation)
        aimData.centerY = center.y();

        // Set initial cursor position (for visual consistency and zeroing delta)
        QCursor::setPos(center);

        // Clear layout change flag (to return to hot path)
        isLayoutChangePending = false;

#endif

#else
        // スタイラス押下分岐(タッチ入力直通処理のため)
        if (Q_LIKELY(emuInstance->isTouching)) {
            // タッチ送出(座標反映のため)
            emuInstance->nds->TouchScreen(emuInstance->touchX, emuInstance->touchY);
        }
        // 非押下分岐(タッチ解放反映のため)
        else {
            // 画面解放(入力状態リセットのため)
            emuInstance->nds->ReleaseScreen();
        }
#endif
    };




    auto frameAdvanceOnce = [&]()  __attribute__((hot, always_inline, flatten)) {
        MPInterface::Get().Process();
#ifdef _WIN32
        
        //if (g_xin) g_xin->update(); // MelonPrimeDS
        //if (g_din) g_din->update(); // MelonPrimeDS
         
#endif
        emuInstance->inputProcess();

        if (emuInstance->hotkeyPressed(HK_FrameLimitToggle)) emit windowLimitFPSChange();

        if (emuInstance->hotkeyPressed(HK_Pause)) emuTogglePause();
        if (emuInstance->hotkeyPressed(HK_Reset)) emuReset();
        if (emuInstance->hotkeyPressed(HK_FrameStep)) emuFrameStep();

        // MelonPrimeDS ホットキー処理部分を修正
        if (emuInstance->hotkeyPressed(HK_FullscreenToggle)) {
            emit windowFullscreenToggle();
            isLayoutChangePending = true;
        }

        if (emuInstance->hotkeyPressed(HK_SwapScreens)) {
            emit swapScreensToggle();
            isLayoutChangePending = true;
        }

        if (emuInstance->hotkeyPressed(HK_SwapScreenEmphasis)) {
            emit screenEmphasisToggle();
            isLayoutChangePending = true;
        }

        // Define minimum sensitivity as a constant to improve readability and optimization
        static constexpr int MIN_SENSITIVITY = 1;

        // Lambda function to update aim sensitivity with low latency
        static const auto updateAimSensitivity = [&](const int change) {
            // Get current sensitivity from config
            int currentSensitivity = localCfg.GetInt("Metroid.Sensitivity.Aim");

            // Calculate new sensitivity value
            int newSensitivity = currentSensitivity + change;

            // Check for minimum value threshold with early return for optimization
            if (newSensitivity < MIN_SENSITIVITY) {
                emuInstance->osdAddMessage(0, "AimSensi cannot be decreased below %d", MIN_SENSITIVITY);
                return;
            }

            // Only process if the value has actually changed
            if (newSensitivity != currentSensitivity) {
                // 設定更新(新しい値の永続化のため)
                localCfg.SetInt("Metroid.Sensitivity.Aim", newSensitivity);
                Config::Save();
                // OSD表示(ユーザー通知のため)
                emuInstance->osdAddMessage(0, "AimSensi Updated: %d->%d", currentSensitivity, newSensitivity);
                // 即時再計算(ホットパスでのポーリング排除のため)
                recalcAimSensitivityCache(localCfg);
            }
            };
        // Optimize hotkey handling with a single expression
        {
            const int sensitivityChange =
                emuInstance->hotkeyReleased(HK_MetroidIngameSensiUp) ? 1 :
                emuInstance->hotkeyReleased(HK_MetroidIngameSensiDown) ? -1 : 0;

            if (sensitivityChange != 0) {
                updateAimSensitivity(sensitivityChange);
            }
        }

        if (emuStatus == emuStatus_Running || emuStatus == emuStatus_FrameStep)
        {
            if (emuStatus == emuStatus_FrameStep) emuStatus = emuStatus_Paused;

            /*
            if (emuInstance->hotkeyPressed(HK_SolarSensorDecrease))
            {
                int level = emuInstance->nds->GBACartSlot.SetInput(GBACart::Input_SolarSensorDown, true);
                if (level != -1)
                {
                    emuInstance->osdAddMessage(0, "Solar sensor level: %d", level);
                }
            }
            if (emuInstance->hotkeyPressed(HK_SolarSensorIncrease))
            {
                int level = emuInstance->nds->GBACartSlot.SetInput(GBACart::Input_SolarSensorUp, true);
                if (level != -1)
                {
                    emuInstance->osdAddMessage(0, "Solar sensor level: %d", level);
                }
            }
            */

            /*
              MelonPrimeDS commentOut
            auto handleDSiInputs = [](EmuInstance* emuInstance, double perfCountsSec) {
                if (emuInstance->nds->ConsoleType == 1)
                {
                    DSi* dsi = static_cast<DSi*>(emuInstance->nds);
                    double currentTime = SDL_GetPerformanceCounter() * perfCountsSec;

                    // Handle power button
                    if (emuInstance->hotkeyDown(HK_PowerButton))
                    {
                        dsi->I2C.GetBPTWL()->SetPowerButtonHeld(currentTime);
                    }
                    else if (emuInstance->hotkeyReleased(HK_PowerButton))
                    {
                        dsi->I2C.GetBPTWL()->SetPowerButtonReleased(currentTime);
                    }

                    // Handle volume buttons
                    if (emuInstance->hotkeyDown(HK_VolumeUp))
                    {
                        dsi->I2C.GetBPTWL()->SetVolumeSwitchHeld(DSi_BPTWL::volumeKey_Up);
                    }
                    else if (emuInstance->hotkeyReleased(HK_VolumeUp))
                    {
                        dsi->I2C.GetBPTWL()->SetVolumeSwitchReleased(DSi_BPTWL::volumeKey_Up);
                    }

                    if (emuInstance->hotkeyDown(HK_VolumeDown))
                    {
                        dsi->I2C.GetBPTWL()->SetVolumeSwitchHeld(DSi_BPTWL::volumeKey_Down);
                    }
                    else if (emuInstance->hotkeyReleased(HK_VolumeDown))
                    {
                        dsi->I2C.GetBPTWL()->SetVolumeSwitchReleased(DSi_BPTWL::volumeKey_Down);
                    }

                    dsi->I2C.GetBPTWL()->ProcessVolumeSwitchInput(currentTime);
                }
                };

            handleDSiInputs(emuInstance, perfCountsSec);

            */

            if (useOpenGL)
                emuInstance->makeCurrentGL();

            // update render settings if needed
            if (videoSettingsDirty)
            {
                emuInstance->renderLock.lock();
                if (useOpenGL)
                {
                    emuInstance->setVSyncGL(true); // is this really needed??
                    videoRenderer = globalCfg.GetInt("3D.Renderer");
                }
#ifdef OGLRENDERER_ENABLED
                else
#endif
                {
                    videoRenderer = 0;
                }

                updateRenderer();

                videoSettingsDirty = false;
                emuInstance->renderLock.unlock();
            }

            /* MelonPrimeDS comment-outed
            // process input and hotkeys
            emuInstance->nds->SetKeyMask(emuInstance->inputMask);
            */


            /* MelonPrimeDS comment-outed
            if (emuInstance->hotkeyPressed(HK_Lid))
            {
                bool lid = !emuInstance->nds->IsLidClosed();
                emuInstance->nds->SetLidClosed(lid);
                emuInstance->osdAddMessage(0, lid ? "Lid closed" : "Lid opened");
            }
            */

            // microphone input
            emuInstance->micProcess();

            // auto screen layout
            {
                mainScreenPos[2] = mainScreenPos[1];
                mainScreenPos[1] = mainScreenPos[0];
                mainScreenPos[0] = emuInstance->nds->PowerControl9 >> 15;

                int guess;
                if (mainScreenPos[0] == mainScreenPos[2] &&
                    mainScreenPos[0] != mainScreenPos[1])
                {
                    // constant flickering, likely displaying 3D on both screens
                    // TODO: when both screens are used for 2D only...???
                    guess = screenSizing_Even;
                }
                else
                {
                    if (mainScreenPos[0] == 1)
                        guess = screenSizing_EmphTop;
                    else
                        guess = screenSizing_EmphBot;
                }

                if (guess != autoScreenSizing)
                {
                    autoScreenSizing = guess;
                    emit autoScreenSizingChange(autoScreenSizing);
                }
            }


            // emulate
            u32 nlines;
            if (emuInstance->nds->GPU.GetRenderer3D().NeedsShaderCompile())
            {
                compileShaders();
                nlines = 1;
            }
            else
            {
                // ここから先は下に行くほど低遅延。RunFrameの直前が最も低遅延。
				// TODO !isFocusedの時の入力排除。RawInputでは排除済み。linux macでは必要。

                // ズーム
                const bool rawZoom = MP_HK_DOWN(HK_MetroidZoom);
                inputMask.setBit(INPUT_R, !rawZoom);

                // 射撃（Shoot/Scanのどちらか）
                const bool rawShoot = MP_HK_DOWN(HK_MetroidShootScan) || MP_HK_DOWN(HK_MetroidScanShoot);
                inputMask.setBit(INPUT_L, !rawShoot);

				// Aim
                processAimInput();
                nlines = emuInstance->nds->RunFrame();
            }

            if (emuInstance->ndsSave)
                emuInstance->ndsSave->CheckFlush();

            if (emuInstance->gbaSave)
                emuInstance->gbaSave->CheckFlush();

            if (emuInstance->firmwareSave)
                emuInstance->firmwareSave->CheckFlush();

            if (!useOpenGL)
            {
                frontBufferLock.lock();
                frontBuffer = emuInstance->nds->GPU.FrontBuffer;
                frontBufferLock.unlock();
            }
            else
            {
                frontBuffer = emuInstance->nds->GPU.FrontBuffer;
                emuInstance->drawScreenGL();
            }

#ifdef MELONCAP
            MelonCap::Update();
#endif // MELONCAP

            winUpdateCount++;
            if (winUpdateCount >= winUpdateFreq && !useOpenGL)
            {
                emit windowUpdate();
                winUpdateCount = 0;
            }
            
            if (emuInstance->hotkeyPressed(HK_FastForwardToggle)) emuInstance->fastForwardToggled = !emuInstance->fastForwardToggled;
            if (emuInstance->hotkeyPressed(HK_SlowMoToggle)) emuInstance->slowmoToggled = !emuInstance->slowmoToggled;

            bool enablefastforward = emuInstance->hotkeyDown(HK_FastForward) | emuInstance->fastForwardToggled;
            bool enableslowmo = emuInstance->hotkeyDown(HK_SlowMo) | emuInstance->slowmoToggled;

            if (useOpenGL)
            {
                // when using OpenGL: when toggling fast-forward or slowmo, change the vsync interval
                if ((enablefastforward || enableslowmo) && !(fastforward || slowmo))
                {
                    emuInstance->setVSyncGL(false);
                }
                else if (!(enablefastforward || enableslowmo) && (fastforward || slowmo))
                {
                    emuInstance->setVSyncGL(true);
                }
            }

            fastforward = enablefastforward;
            slowmo = enableslowmo;

            if (slowmo) emuInstance->curFPS = emuInstance->slowmoFPS;
            else if (fastforward) emuInstance->curFPS = emuInstance->fastForwardFPS;
            else if (!emuInstance->doLimitFPS) emuInstance->curFPS = 1000.0;
            else emuInstance->curFPS = emuInstance->targetFPS;

            if (emuInstance->audioDSiVolumeSync && emuInstance->nds->ConsoleType == 1)
            {
                DSi* dsi = static_cast<DSi*>(emuInstance->nds);
                u8 volumeLevel = dsi->I2C.GetBPTWL()->GetVolumeLevel();
                if (volumeLevel != dsiVolumeLevel)
                {
                    dsiVolumeLevel = volumeLevel;
                    emit syncVolumeLevel();
                }

                emuInstance->audioVolume = volumeLevel * (256.0 / 31.0);
            }

            if (emuInstance->doAudioSync && !(fastforward || slowmo))
                emuInstance->audioSync();

            double frametimeStep = nlines / (emuInstance->curFPS * 263.0);

            if (frametimeStep < 0.001) frametimeStep = 0.001;

            {
                double curtime = SDL_GetPerformanceCounter() * perfCountsSec;

                frameLimitError += frametimeStep - (curtime - lastTime);
                if (frameLimitError < -frametimeStep)
                    frameLimitError = -frametimeStep;
                if (frameLimitError > frametimeStep)
                    frameLimitError = frametimeStep;

                if (round(frameLimitError * 1000.0) > 0.0)
                {
                    SDL_Delay(round(frameLimitError * 1000.0));
                    double timeBeforeSleep = curtime;
                    curtime = SDL_GetPerformanceCounter() * perfCountsSec;
                    frameLimitError -= curtime - timeBeforeSleep;
                }

                lastTime = curtime;
            }

            nframes++;
            if (nframes >= 30)
            {
                double time = SDL_GetPerformanceCounter() * perfCountsSec;
                double dt = time - lastMeasureTime;
                lastMeasureTime = time;

                u32 fps = round(nframes / dt);
                nframes = 0;

                float fpstarget = 1.0/frametimeStep;

                winUpdateFreq = fps / (u32)round(fpstarget);
                if (winUpdateFreq < 1)
                    winUpdateFreq = 1;
                    
                double actualfps = (59.8261 * 263.0) / nlines;
                int inst = emuInstance->instanceID;
                if (inst == 0)
                    sprintf(melontitle, "[%d/%.0f] melonDS " MELONDS_VERSION, fps, actualfps);
                else
                    sprintf(melontitle, "[%d/%.0f] melonDS (%d)", fps, fpstarget, inst+1);
                changeWindowTitle(melontitle);
            }
        }
        else
        {
            // paused
            nframes = 0;
            lastTime = SDL_GetPerformanceCounter() * perfCountsSec;
            lastMeasureTime = lastTime;

            emit windowUpdate();

            int inst = emuInstance->instanceID;
            if (inst == 0)
                sprintf(melontitle, "melonDS " MELONDS_VERSION);
            else
                sprintf(melontitle, "melonDS (%d)", inst+1);
            changeWindowTitle(melontitle);

            SDL_Delay(75);

            if (useOpenGL)
            {
                emuInstance->drawScreenGL();
            }
        }

        handleMessages();

    };


    /*
    auto frameAdvance = [&](int n)  __attribute__((hot, always_inline, flatten)) {
        for (int i = 0; i < n; i++) {
            frameAdvanceOnce();
        }
        };

// Define a frequently used macro to advance 2 frames
#define FRAME_ADVANCE_2 \
    do { \
        frameAdvanceOnce(); \
        frameAdvanceOnce(); \
    } while (0) \
    // Note: Why use do { ... } while (0)?
    // This is the standard safe macro form, allowing it to be treated as a single block in statements such as if-conditions.

    */

    static const auto frameAdvanceTwice = [&]() __attribute__((hot, always_inline, flatten)) {
        frameAdvanceOnce();
        frameAdvanceOnce();
    };



    // Define a lambda function to switch weapons
    static const auto SwitchWeapon = [&](int weaponIndex) __attribute__((hot, always_inline)) {

        // Check for Already equipped
        if (emuInstance->nds->ARM9Read8(addrSelectedWeapon) == weaponIndex) {
            // emuInstance->osdAddMessage(0, "Weapon switch unnecessary: Already equipped");
            return; // Early return if the weapon is already equipped
        }

        if (isInAdventure) {

            // Check isMapOrUserActionPaused, for the issue "If you switch weapons while the map is open, the aiming mechanism may become stuck."
            if (isPaused) {
                return;
            }
            else if (emuInstance->nds->ARM9Read8(addrIsInVisorOrMap) == 0x1) {
                // isInVisor

                // Prevent visual glitches during weapon switching in visor mode
                return;
            }
        }

        // Read the current jump flag value
        uint8_t currentJumpFlags = emuInstance->nds->ARM9Read8(addrJumpFlag);

        // Check if the upper 4 bits are odd (1 or 3)
        // this is for fixing issue: Shooting and transforming become impossible, when changing weapons at high speed while transitioning from transformed to normal form.
        bool isTransforming = currentJumpFlags & 0x10;

        uint8_t jumpFlag = currentJumpFlags & 0x0F;  // Get the lower 4 bits
        //emuInstance->osdAddMessage(0, ("JumpFlag:" + std::string(1, "0123456789ABCDEF"[emuInstance->nds->ARM9Read8(addrJumpFlag) & 0x0F])).c_str());

        bool isRestoreNeeded = false;

        // Check if in alternate form (transformed state)
        isAltForm = emuInstance->nds->ARM9Read8(addrIsAltForm) == 0x02;

        // If not jumping (jumpFlag == 0) and in normal form, temporarily set to jumped state (jumpFlag == 1)
        if (!isTransforming && jumpFlag == 0 && !isAltForm) {
            // Leave the upper 4 bits of currentJumpFlags as they are and set the lower 4 bits to 0x01
            emuInstance->nds->ARM9Write8(addrJumpFlag, (currentJumpFlags & 0xF0) | 0x01);
            isRestoreNeeded = true;
            //emuInstance->osdAddMessage(0, ("JumpFlag:" + std::string(1, "0123456789ABCDEF"[emuInstance->nds->ARM9Read8(addrJumpFlag) & 0x0F])).c_str());
            //emuInstance->osdAddMessage(0, "Done setting jumpFlag.");
        }

        // Leave the upper 4 bits of addrWeaponChange as they are, and set only the lower 4 bits to 1011 (B in hexadecimal)
        emuInstance->nds->ARM9Write8(addrWeaponChange, (emuInstance->nds->ARM9Read8(addrWeaponChange) & 0xF0) | 0x0B); // Only change the lower 4 bits to B

        // Change the weapon
        emuInstance->nds->ARM9Write8(addrSelectedWeapon, weaponIndex);  // Write the address of the corresponding weapon

        // Release the screen (for weapon change)
        emuInstance->nds->ReleaseScreen();

        // Advance frames (for reflection of ReleaseScreen, WeaponChange)
        frameAdvanceTwice();

        // Need Touch after ReleaseScreen for aiming.
#ifndef STYLUS_MODE
        emuInstance->nds->TouchScreen(128, 88);
#else
        if (emuInstance->isTouching) {
            emuInstance->nds->TouchScreen(emuInstance->touchX, emuInstance->touchY);
        }
#endif

        // Advance frames (for reflection of Touch. This is necessary for no jump)
        frameAdvanceTwice();

        // Restore the jump flag to its original value (if necessary)
        if (isRestoreNeeded) {
            currentJumpFlags = emuInstance->nds->ARM9Read8(addrJumpFlag);
            // Create and set a new value by combining the upper 4 bits of currentJumpFlags and the lower 4 bits of jumpFlag
            emuInstance->nds->ARM9Write8(addrJumpFlag, (currentJumpFlags & 0xF0) | jumpFlag);
            //emuInstance->osdAddMessage(0, ("JumpFlag:" + std::string(1, "0123456789ABCDEF"[emuInstance->nds->ARM9Read8(addrJumpFlag) & 0x0F])).c_str());
            //emuInstance->osdAddMessage(0, "Restored jumpFlag.");

        }

    };

    while (emuStatus != emuStatus_Exit) {

        // MelonPrimeDS Functions START

#ifndef STYLUS_MODE
        // Mouse player
        bool isFocused = emuInstance->getMainWindow()->panel->getFocused();
#else
        // isStylus
        bool isFocused = true;
#endif


        if (!isRomDetected) {
            detectRomAndSetAddresses(emuInstance);
        }
        // No "else" here, cuz flag will be changed after detecting.

        if (Q_LIKELY(isRomDetected)) {
            isInGame = emuInstance->nds->ARM9Read16(addrInGame) == 0x0001;

            // Determine whether it is cursor mode in one place
            bool shouldBeCursorMode = !isInGame || (isInAdventure && isPaused);

            if (isInGame && !isInGameAndHasInitialized) {
                // Run once at game start

                // Set the initialization complete flag
                isInGameAndHasInitialized = true;

                // updateRenderer because of using softwareRenderer when not in Game.
                videoRenderer = emuInstance->getGlobalConfig().GetInt("3D.Renderer");
                updateRenderer();

                /* MelonPrimeDS test
                if (vsyncFlag) {
                    emuInstance->osdAddMessage(0, "Vsync is enabled.");
                }
                else {
                    emuInstance->osdAddMessage(0, "Vsync is disabled.");
                }
                */

                // Read the player position
                playerPosition = emuInstance->nds->ARM9Read8(addrPlayerPos);

                // get addresses
                addrIsAltForm = calculatePlayerAddress(addrBaseIsAltForm, playerPosition, incrementOfPlayerAddress);
                addrLoadedSpecialWeapon = calculatePlayerAddress(addrBaseLoadedSpecialWeapon, playerPosition, incrementOfPlayerAddress);
                addrChosenHunter = calculatePlayerAddress(addrBaseChosenHunter, playerPosition, 0x01);
                addrWeaponChange = calculatePlayerAddress(addrBaseWeaponChange, playerPosition, incrementOfPlayerAddress);

                addrSelectedWeapon = calculatePlayerAddress(addrBaseSelectedWeapon, playerPosition, incrementOfPlayerAddress); // 020DCAA3 in JP1.0.
                addrCurrentWeapon = addrSelectedWeapon - 0x1; // 020DCAA2 in JP1.0
                addrHavingWeapons = addrSelectedWeapon + 0x3; // 020DCAA6 in JP1.0

                addrWeaponAmmo = addrSelectedWeapon - 0x383; // 020D720 in JP1.0 current weapon ammo. DC722 is for MissleAmmo. can read both with read32.
                addrJumpFlag = calculatePlayerAddress(addrBaseJumpFlag, playerPosition, incrementOfPlayerAddress);

                // getaddrChosenHunter
                addrChosenHunter = calculatePlayerAddress(addrBaseChosenHunter, playerPosition, 0x01);
                uint8_t hunterID = emuInstance->nds->ARM9Read8(addrChosenHunter); // Perform memory read only once
                isSamus = hunterID == 0x00;
                isWeavel = hunterID == 0x06;

                /*
                Hunter IDs:
                00 - Samus
                01 - Kanden
                02 - Trace
                03 - Sylux
                04 - Noxus
                05 - Spire
                06 - Weavel
                */
                addrInGameSensi = calculatePlayerAddress(addrBaseInGameSensi, playerPosition, 0x04);

                addrBoostGauge = addrIsAltForm + 0x44;
                addrIsBoosting = addrIsAltForm + 0x46;

                // aim addresses
                addrAimX = calculatePlayerAddress(addrBaseAimX, playerPosition, incrementOfAimAddr);
                addrAimY = calculatePlayerAddress(addrBaseAimY, playerPosition, incrementOfAimAddr);


                isInAdventure = emuInstance->nds->ARM9Read8(addrIsInAdventure) == 0x02;

                // isPrimeHunterAddr = addrIsInAdventure + 0xAD; // isPrimeHunter Addr NotPrimeHunter:0xFF, PrimeHunter:0x00 220E9AE9 in JP1.0

                // emuInstance->osdAddMessage(0, "Completed address calculation.");
            }

            if (isFocused) {


                // Calculate for aim 
                // updateMouseRelativeAndRecenterCursor
                // 
                // Handle the case when the window is focused
                // Update mouse relative position and recenter cursor for aim control

				if (Q_LIKELY(isInGame)) { // ここをisInGameAndHasInitializedにしてはいけない
                    // inGame

                    /*
                    * doing this in Screen.cpp
                    if(!wasLastFrameFocused){
                        showCursorOnMelonPrimeDS(false);
                    }
                    */


                    // Move hunter
                    processMoveInput(inputMask);



                    // Jump（B）
                    inputMask.setBit(INPUT_B, !MP_HK_DOWN(HK_MetroidJump));

                    // Alt-form
                    TOUCH_IF(HK_MetroidMorphBall, 231, 167)

                    // Compile-time constants
                    static constexpr uint8_t WEAPON_ORDER[] = { 0, 2, 7, 6, 5, 4, 3, 1, 8 };
                    static constexpr uint16_t WEAPON_MASKS[] = { 0x001, 0x004, 0x080, 0x040, 0x020, 0x010, 0x008, 0x002, 0x100 };
                    static constexpr uint8_t MIN_AMMO[] = { 0, 0x5, 0xA, 0x4, 0x14, 0x5, 0xA, 0xA, 0 }; // TODO カンデンボルトドライバのminAmmoがおかしい？
                    static constexpr uint8_t WEAPON_INDEX_MAP[9] = { 0, 7, 1, 6, 5, 4, 3, 2, 8 };
                    static constexpr uint8_t WEAPON_COUNT = 9;

                    // Hotkey mapping
                    static constexpr struct {
                        int hotkey;
                        uint8_t weapon;
                    } HOTKEY_MAP[] = {
                        { HK_MetroidWeaponBeam, 0 },
                        { HK_MetroidWeaponMissile, 2 },
                        { HK_MetroidWeapon1, 7 },
                        { HK_MetroidWeapon2, 6 },
                        { HK_MetroidWeapon3, 5 },
                        { HK_MetroidWeapon4, 4 },
                        { HK_MetroidWeapon5, 3 },
                        { HK_MetroidWeapon6, 1 },
                        { HK_MetroidWeaponSpecial, 0xFF }
                    };

                    // Main lambda for weapon switching
                    static const auto processWeaponSwitch = [&]() -> bool {
                        bool weaponSwitched = false;

                        // Lambda: Gather hotkey states efficiently
                        static const auto gatherHotkeyStates = [&]() -> uint32_t {
                            uint32_t states = 0;
                            for (size_t i = 0; i < 9; ++i) {
                                states |= static_cast<uint32_t>(MP_HK_PRESSED(HOTKEY_MAP[i].hotkey)) << i;
                            }
                            return states;
                            };


                        // Lambda: Calculate available weapons
                        static const auto getAvailableWeapons = [&]() -> uint16_t {
                            // Batch read weapon data
                            const uint16_t having = emuInstance->nds->ARM9Read16(addrHavingWeapons);
                            const uint32_t ammoData = emuInstance->nds->ARM9Read32(addrWeaponAmmo);
                            const uint16_t missileAmmo = ammoData >> 16;
                            const uint16_t weaponAmmo = ammoData & 0xFFFF;

                            uint16_t available = 0;

                            // Check each weapon
                            for (int i = 0; i < WEAPON_COUNT; ++i) {
                                const uint8_t weapon = WEAPON_ORDER[i];
                                const uint16_t mask = WEAPON_MASKS[i];

                                // Check if owned
                                if (!(having & mask)) continue;

                                // Check ammo requirements
                                bool hasAmmo = true;
                                if (weapon == 2) {
                                    hasAmmo = missileAmmo >= 0xA;
                                }
                                else if (weapon > 2 && weapon < 8) {
                                    uint8_t required = MIN_AMMO[weapon];
                                    if (weapon == 3 && isWeavel) {
                                        required = 0x5;
                                    }
                                    hasAmmo = weaponAmmo >= required;
                                }

                                if (hasAmmo) {
                                    available |= (1u << i);
                                }
                            }

                            return available;
                            };


                        // ホットキー処理：未所持/弾不足/未ロード Special は false で返してフォールバック可にする
                        //（※以前は握りつぶし true 返し→ここを変更）元の雛形参照: :contentReference[oaicite:8]{index=8}
                        static const auto processHotkeys = [&](uint32_t hotkeyStates) -> bool {
                            if (!hotkeyStates) return false;

                            const int firstSet = __builtin_ctz(hotkeyStates);

                            // Special キー（HOTKEY_MAP[8]）
                            if (Q_UNLIKELY(firstSet == 8)) {
                                const uint8_t loaded = emuInstance->nds->ARM9Read8(addrLoadedSpecialWeapon);
                                if (loaded == 0xFF) return false;     // 未ロード → フォールバック
                                SwitchWeapon(loaded);
                                return true;
                            }

                            // 通常武器：available 判定（ダメなら未処理で返す → フォールバック）
                            const uint8_t weaponID = HOTKEY_MAP[firstSet].weapon;
                            const uint16_t available = getAvailableWeapons();
                            const uint8_t  index = WEAPON_INDEX_MAP[weaponID];
                            if (!(available & (1u << index))) return false; // 未所持/弾不足

                            // 切替
                            SwitchWeapon(weaponID);
                            return true;
                            };

                        // Lambda: Find next available weapon
                        static const auto findNextWeapon = [&](uint8_t current, bool forward, uint16_t available) -> int {
                            if (!available) return -1;  // No weapons available

                            uint8_t startIndex = WEAPON_INDEX_MAP[current];
                            uint8_t index = startIndex;

                            // Search for next available weapon
                            for (int attempts = 0; attempts < WEAPON_COUNT; ++attempts) {
                                if (forward) {
                                    index = (index + 1) % WEAPON_COUNT;
                                }
                                else {
                                    index = (index + WEAPON_COUNT - 1) % WEAPON_COUNT;
                                }

                                if (available & (1u << index)) {
                                    return WEAPON_ORDER[index];
                                }
                            }

                            return -1;  // Should not reach here if available != 0
                            };

                        // Lambda: Process wheel and navigation keys
                        static const auto processWheelInput = [&]() -> bool {
                            const int wheelDelta = emuInstance->getMainWindow()->panel->getDelta();
                            const bool nextKey = MP_HK_PRESSED(HK_MetroidWeaponNext);
                            const bool prevKey = MP_HK_PRESSED(HK_MetroidWeaponPrevious);


                            if (!wheelDelta && !nextKey && !prevKey) return false;

                            const bool forward = (wheelDelta < 0) || nextKey;
                            const uint8_t current = emuInstance->nds->ARM9Read8(addrCurrentWeapon);
                            const uint16_t available = getAvailableWeapons();

                            int nextWeapon = findNextWeapon(current, forward, available);
                            if (nextWeapon >= 0) {
                                SwitchWeapon(static_cast<uint8_t>(nextWeapon));
                                return true;
                            }

                            return false;
                            };

                        // Main execution flow
                        const uint32_t hotkeyStates = gatherHotkeyStates();

                        // Try hotkeys first
                        if (processHotkeys(hotkeyStates)) {
                            return true;
                        }

                        // Try wheel/navigation if no hotkey was pressed
                        if (processWheelInput()) {
                            return true;
                        }

                        return false;
                        };

                    // Execute the weapon switch logic
                    bool weaponSwitched = processWeaponSwitch();


                    // Weapon check {
					// TODO モーフボールブーストと併用したい場合は、ここを工夫する必要がある。
                    static bool isWeaponCheckActive = false;
                    //static uint8_t weaponCheckWeaponIndex;

                    if (emuInstance->hotkeyDown(HK_MetroidWeaponCheck)) {
                        if (!isWeaponCheckActive) {
                            // 初回のみ実行
                            isWeaponCheckActive = true;
							// Disable aim during weapon check, this is to prevent weapon selection due to aiming touch input.
                            setAimBlock(AIMBLK_CHECK_WEAPON, true);
                            //weaponCheckWeaponIndex = emuInstance->nds->ARM9Read8(addrSelectedWeapon);
                            emuInstance->nds->ReleaseScreen();
                            frameAdvanceTwice();
                        }

                        // キーが押されている間は継続
                        //emuInstance->nds->ARM9Write8(addrSelectedWeapon, 0x00); // 無選択に見えるようにパワービームにしておく
						// Touch for weapon check
                        emuInstance->nds->TouchScreen(236, 30); // このタッチせいでどうしてもエイム発動してマグモ選択されてしまう問題があった。
						// タッチ反映の為に2フレーム進める
                        frameAdvanceTwice();
                        // still allow movement
                        processMoveInput(inputMask);

                    }
                    else {
                        if (isWeaponCheckActive) {
                            isWeaponCheckActive = false;
                            // キーが離されたときの終了処理
                            emuInstance->nds->ReleaseScreen();
                            // 終了時の武器選択を防ぐ。そのままだとマグモールになってしまう為。
                            //emuInstance->nds->ARM9Write8(addrSelectedWeapon, weaponCheckWeaponIndex);

							// Re-enable aim after weapon check
                            setAimBlock(AIMBLK_CHECK_WEAPON, false);
							// リリース反映の為に2フレーム進める
                            frameAdvanceTwice();
                        }
                    }
                    // } Weapon check


					// Morph ball boost. This should be worked in Adventure mode and Arena mode.
                    // INFO If this function is not used, mouse boosting can only be done once.
                    // This is because it doesn't release from the touch state, which is necessary for aiming. 
                    // There's no way around it.
                    // Morph ball boost（保持型）
                    if (isSamus){
                        if (MP_HK_DOWN(HK_MetroidHoldMorphBallBoost)) {
                            isAltForm = emuInstance->nds->ARM9Read8(addrIsAltForm) == 0x02;
                            if (isAltForm) {
                                uint8_t boostGaugeValue = emuInstance->nds->ARM9Read8(addrBoostGauge);
                                bool isBoosting = emuInstance->nds->ARM9Read8(addrIsBoosting) != 0x00;

                                // boostable when gauge value is 0x05-0x0F(max)
                                bool isBoostGaugeEnough = boostGaugeValue > 0x0A;

                                // just incase
                                setAimBlock(AIMBLK_MORPHBALL_BOOST, true);

                                // release for boost?
                                if(!(emuInstance->hotkeyDown(HK_MetroidWeaponCheck))){ // ここでリリースしてしまうせいで、武器チェックで武器表示と非表示が連発で点滅してしまう問題があった。
                                    emuInstance->nds->ReleaseScreen();
                                }
                                // Boost input determination (true during boost, false during charge)
                                inputMask.setBit(INPUT_R, (!isBoosting && isBoostGaugeEnough));

                                if (isBoosting) {
                                    // touch again for aiming
                                    setAimBlock(AIMBLK_MORPHBALL_BOOST, false);
#ifndef STYLUS_MODE
									// ここでタッチしてしまうと、WeaponCheckで武器選択されてしまう問題がある。
                                    //emuInstance->nds->TouchScreen(128, 88);
#else
                                    if (emuInstance->isTouching) {
                                        emuInstance->nds->TouchScreen(emuInstance->touchX, emuInstance->touchY);
                                    }
#endif
                                }

                            }
                        }
                        else {
                            setAimBlock(AIMBLK_MORPHBALL_BOOST, false);
                        }
                    }

                    if (Q_UNLIKELY(isInAdventure)) {
                        // Adventure Mode Functions

                        // To determine the state of pause or user operation stop (to detect the state of map or action pause)
                        isPaused = emuInstance->nds->ARM9Read8(addrIsMapOrUserActionPaused) == 0x1;

                        // Scan Visor
                        if (MP_HK_PRESSED(HK_MetroidScanVisor)) {
                            emuInstance->nds->ReleaseScreen();
                            frameAdvanceTwice();

                            // emuInstance->osdAddMessage(0, "in visor %d", inVisor);

                            emuInstance->nds->TouchScreen(128, 173);

                            if (emuInstance->nds->ARM9Read8(addrIsInVisorOrMap) == 0x1) {
                                // isInVisor
                                frameAdvanceTwice();
                            }
                            else {
                                for (int i = 0; i < 30; i++) {
                                    // still allow movement whilst we're enabling scan visor
                                    processAimInput();
                                    processMoveInput(inputMask); 

                                    //emuInstance->nds->SetKeyMask(emuInstance->getInputMask());
                                    emuInstance->nds->SetKeyMask(GET_INPUT_MASK(inputMask));
                                    
                                    frameAdvanceOnce();
                                }
                            }

                            emuInstance->nds->ReleaseScreen();
                            frameAdvanceTwice();
                        }

                        TOUCH_IF(HK_MetroidUIOk, 128, 142) // OK (in scans and messages)
                        TOUCH_IF(HK_MetroidUILeft, 71, 141) // Left arrow (in scans and messages)
                        TOUCH_IF(HK_MetroidUIRight, 185, 141) // Right arrow (in scans and messages)
                        TOUCH_IF(HK_MetroidUIYes, 96, 142)  // Enter to Starship
                        TOUCH_IF(HK_MetroidUINo, 160, 142) // No Enter to Starship

                    } // End of Adventure Functions


#ifndef STYLUS_MODE
                    // Touch again for aiming
                    if (!wasLastFrameFocused || !isAimDisabled) {
                        // touch again for aiming
                        // When you return to melonPrimeDS or normal form

                        // emuInstance->osdAddMessage(0,"touching screen for aim");

                        // Changed Y point center(96) to 88, For fixing issue: Alt Tab switches hunter choice.
                        //emuInstance->nds->TouchScreen(128, 96); // required for aiming


                        emuInstance->nds->TouchScreen(128, 88); // required for aiming
                    }
#endif


                    // End of in-game
                }
                else {
                    // !isInGame

                    isInAdventure = false;
                    isAimDisabled = true;

                    if (isInGameAndHasInitialized) {
                        isInGameAndHasInitialized = false;
                    }

                    // Resolve Menu flickering
                    if (videoRenderer != renderer3D_Software) {
                        videoRenderer = renderer3D_Software;
                        updateRenderer();
                    }

                    // L / R for Hunter License (one-frame press)
                    inputMask.setBit(INPUT_L, !MP_HK_PRESSED(HK_MetroidUILeft));
                    inputMask.setBit(INPUT_R, !MP_HK_PRESSED(HK_MetroidUIRight));


                    if (Q_LIKELY(isRomDetected)) {
                        // TODO オープニングムービーの終了後に実行しないとフリーズの恐れあり。

                        // Headphone settings
                        ApplyHeadphoneOnce(emuInstance->nds, localCfg, addrOperationAndSound, isHeadphoneApplied);

                        // Apply MPH sensitivity settings
                        ApplyMphSensitivity(emuInstance->nds, localCfg, addrSensitivity, addrInGameSensi, isInGameAndHasInitialized);
                        
                        // Unlock all Hunters/Maps
                        ApplyUnlockHuntersMaps(
                            emuInstance->nds,
                            localCfg,
                            isUnlockMapsHuntersApplied,
                            addrUnlockMapsHunters,
                            addrUnlockMapsHunters2,
                            addrUnlockMapsHunters3,
                            addrUnlockMapsHunters4,
                            addrUnlockMapsHunters5
                        );

                        // Apply DS name
                        useDsName(emuInstance->nds, localCfg, addrDsNameFlagAndMicVolume);

                        // Apply license Hunter
                        applySelectedHunterStrict(emuInstance->nds, localCfg, addrMainHunter);
                        // Apply license color
                        applyLicenseColorStrict(emuInstance->nds, localCfg, addrRankColor);
                    }

                }

                if (shouldBeCursorMode != isCursorMode) {
                    isCursorMode = shouldBeCursorMode;
                    // カーソルモードの有無をAimブロックに反映
                    setAimBlock(AIMBLK_CURSOR_MODE, isCursorMode);
#ifndef STYLUS_MODE
                    showCursorOnMelonPrimeDS(isCursorMode); 
#endif
                }

                if (isCursorMode) {
                    if (emuInstance->isTouching) {
                        emuInstance->nds->TouchScreen(emuInstance->touchX, emuInstance->touchY);
                    }
                    else {
                        emuInstance->nds->ReleaseScreen();
                    }
                }

                // Start / View Match progress, points / Map(Adventure)
                inputMask.setBit(INPUT_START, !MP_HK_DOWN(HK_MetroidMenu));

                // END of if(isFocused)
            } else {
				// when not focused
#if defined(_WIN32)
                    
                    if (g_rawFilter) g_rawFilter->discardDeltas(); // Call to discard RAW deltas (to eliminate residuals)
                    if (g_rawFilter) g_rawFilter->resetMouseButtons(); // Call to reset mouse button states (to eliminate leftover presses)
                    if (g_rawFilter) g_rawFilter->resetAllKeys(); // Call to reset key press states (to prevent accidental inputs)
                    if (g_rawFilter) g_rawFilter->resetHotkeyEdges(); // Almost essential (prevents false triggers of Pressed/Released right after resuming)
#endif
            }
            
            // Apply input
            // emuInstance->nds->SetKeyMask(emuInstance->getInputMask());
            emuInstance->nds->SetKeyMask(GET_INPUT_MASK(inputMask));

            // record last frame was forcused or not
            wasLastFrameFocused = isFocused;
        } // End of isRomDetected
 

        // MelonPrimeDS Functions END


        frameAdvanceOnce();


    } // End of while (emuStatus != emuStatus_Exit)

}

void EmuThread::sendMessage(Message msg)
{
    msgMutex.lock();
    msgQueue.enqueue(msg);
    msgMutex.unlock();
}

void EmuThread::waitMessage(int num)
{
    if (QThread::currentThread() == this) return;
    msgSemaphore.acquire(num);
}

void EmuThread::waitAllMessages()
{
    if (QThread::currentThread() == this) return;
    while (!msgQueue.empty())
        msgSemaphore.acquire();
}

void EmuThread::handleMessages()
{
    msgMutex.lock();
    while (!msgQueue.empty())
    {
        Message msg = msgQueue.dequeue();
        switch (msg.type)
        {
        case msg_Exit:
            emuStatus = emuStatus_Exit;
            emuPauseStack = emuPauseStackRunning;

            emuInstance->audioDisable();
            MPInterface::Get().End(emuInstance->instanceID);
            break;

        case msg_EmuRun:
            emuStatus = emuStatus_Running;
            emuPauseStack = emuPauseStackRunning;
            emuActive = true;

            emuInstance->audioEnable();
            emit windowEmuStart();
            break;

        case msg_EmuPause:
            emuPauseStack++;
            if (emuPauseStack > emuPauseStackPauseThreshold) break;

            prevEmuStatus = emuStatus;
            emuStatus = emuStatus_Paused;

            if (prevEmuStatus != emuStatus_Paused)
            {
                emuInstance->audioDisable();
                emit windowEmuPause(true);
                emuInstance->osdAddMessage(0, "Paused");
            }
            break;

        case msg_EmuUnpause:
            if (emuPauseStack < emuPauseStackPauseThreshold) break;

            emuPauseStack--;
            if (emuPauseStack >= emuPauseStackPauseThreshold) break;

            emuStatus = prevEmuStatus;

            if (emuStatus != emuStatus_Paused)
            {
                emuInstance->audioEnable();
                emit windowEmuPause(false);
                emuInstance->osdAddMessage(0, "Resumed");

                // MelonPrimeDS {
                // applyVideoSettings Immediately when resumed
                if (isInGame) {
                    // updateRenderer because of using softwareRenderer when not in Game.
                    videoRenderer = emuInstance->getGlobalConfig().GetInt("3D.Renderer");
                    updateRenderer();

                    // Apply MPH sensitivity settings
                    ApplyMphSensitivity(emuInstance->nds, emuInstance->getLocalConfig(), addrSensitivity, addrInGameSensi, isInGameAndHasInitialized);
                }

                /*
                // 感度値取得処理
                uint32_t sensiVal = emuInstance->getNDS()->ARM9Read16(addrSensitivity);
                double sensiNum = sensiValToSensiNum(sensiVal);

                // 感度値表示(ROM情報とは別に独立して表示するため)
                char message[256];
                sprintf(message, "INFO sensitivity of the game itself: 0x%04X(%.5f)", sensiVal, sensiNum);
                emuInstance->osdAddMessage(0, message);
                */

                // reset Settings when unPaused
                isSnapTapMode = emuInstance->getLocalConfig().GetBool("Metroid.Operation.SnapTap"); // SnapTapリセット用
                isUnlockMapsHuntersApplied = false; // Unlockリセット用
                // lastMphSensitivity = std::numeric_limits<double>::quiet_NaN(); // Mph感度リセット用
                isHeadphoneApplied = false; // ヘッドフォンリセット用


                // 再開時に現在の設定値をキャッシュへ即反映（Aim感度）
                recalcAimSensitivityCache(emuInstance->getLocalConfig());

                // Apply Aim Adjust setting
                applyAimAdjustSetting(emuInstance->getLocalConfig());


#ifdef _WIN32
                // VKバインド再適用（ポーズ中に設定が変わっていた場合に反映）
                if (g_rawFilter) {
                    BindMetroidHotkeysFromConfig(g_rawFilter, emuInstance->getInstanceID());
                    // 再バインド直後の誤判定（連続 Pressed/Released）を避ける
                    g_rawFilter->resetHotkeyEdges();
                }
#endif

                // MelonPrimeDS }
            }
            break;

        case msg_EmuStop:
            if (msg.param.value<bool>())
                emuInstance->nds->Stop();
            emuStatus = emuStatus_Paused;
            emuActive = false;

            emuInstance->audioDisable();
            emit windowEmuStop();
            break;

        case msg_EmuFrameStep:
            emuStatus = emuStatus_FrameStep;
            break;

        case msg_EmuReset:
            emuInstance->reset();

            emuStatus = emuStatus_Running;
            emuPauseStack = emuPauseStackRunning;
            emuActive = true;

            emuInstance->audioEnable();
            emit windowEmuReset();
            emuInstance->osdAddMessage(0, "Reset");
            break;

        case msg_InitGL:
            emuInstance->initOpenGL(msg.param.value<int>());
            useOpenGL = true;
            break;

        case msg_DeInitGL:
            emuInstance->deinitOpenGL(msg.param.value<int>());
            if (msg.param.value<int>() == 0)
                useOpenGL = false;
            break;

        case msg_BootROM:
            msgResult = 0;
            if (!emuInstance->loadROM(msg.param.value<QStringList>(), true, msgError))
                break;

            assert(emuInstance->nds != nullptr);
            emuInstance->nds->Start();
            msgResult = 1;
            break;

        case msg_BootFirmware:
            msgResult = 0;
            if (!emuInstance->bootToMenu(msgError))
                break;

            assert(emuInstance->nds != nullptr);
            emuInstance->nds->Start();
            msgResult = 1;
            break;

        case msg_InsertCart:
            msgResult = 0;
            if (!emuInstance->loadROM(msg.param.value<QStringList>(), false, msgError))
                break;

            msgResult = 1;
            break;

        case msg_EjectCart:
            emuInstance->ejectCart();
            break;

        case msg_InsertGBACart:
            msgResult = 0;
            if (!emuInstance->loadGBAROM(msg.param.value<QStringList>(), msgError))
                break;

            msgResult = 1;
            break;

        case msg_InsertGBAAddon:
            msgResult = 0;
            emuInstance->loadGBAAddon(msg.param.value<int>(), msgError);
            msgResult = 1;
            break;

        case msg_EjectGBACart:
            emuInstance->ejectGBACart();
            break;

        case msg_SaveState:
            msgResult = emuInstance->saveState(msg.param.value<QString>().toStdString());
            break;

        case msg_LoadState:
            msgResult = emuInstance->loadState(msg.param.value<QString>().toStdString());
            break;

        case msg_UndoStateLoad:
            emuInstance->undoStateLoad();
            msgResult = 1;
            break;

        case msg_ImportSavefile:
            {
                msgResult = 0;
                auto f = Platform::OpenFile(msg.param.value<QString>().toStdString(), Platform::FileMode::Read);
                if (!f) break;

                u32 len = FileLength(f);

                std::unique_ptr<u8[]> data = std::make_unique<u8[]>(len);
                Platform::FileRewind(f);
                Platform::FileRead(data.get(), len, 1, f);

                assert(emuInstance->nds != nullptr);
                emuInstance->nds->SetNDSSave(data.get(), len);

                CloseFile(f);
                msgResult = 1;
            }
            break;

        case msg_EnableCheats:
            emuInstance->enableCheats(msg.param.value<bool>());
            break;
        }

        msgSemaphore.release();
    }
    msgMutex.unlock();
}

void EmuThread::changeWindowTitle(char* title)
{
    emit windowTitleChange(QString(title));
}

void EmuThread::initContext(int win)
{
    sendMessage({.type = msg_InitGL, .param = win});
    waitMessage();
}

void EmuThread::deinitContext(int win)
{
    sendMessage({.type = msg_DeInitGL, .param = win});
    waitMessage();
}

void EmuThread::emuRun()
{
    sendMessage(msg_EmuRun);
    waitMessage();
}

void EmuThread::emuPause(bool broadcast)
{
    sendMessage(msg_EmuPause);
    waitMessage();

    if (broadcast)
        emuInstance->broadcastCommand(InstCmd_Pause);
}

void EmuThread::emuUnpause(bool broadcast)
{
    sendMessage(msg_EmuUnpause);
    waitMessage();

    if (broadcast)
        emuInstance->broadcastCommand(InstCmd_Unpause);
}

void EmuThread::emuTogglePause(bool broadcast)
{
    if (emuStatus == emuStatus_Paused)
        emuUnpause(broadcast);
    else
        emuPause(broadcast);
}

void EmuThread::emuStop(bool external)
{
    sendMessage({.type = msg_EmuStop, .param = external});
    waitMessage();
}

void EmuThread::emuExit()
{
    sendMessage(msg_Exit);
    waitAllMessages();
}

void EmuThread::emuFrameStep()
{
    if (emuPauseStack < emuPauseStackPauseThreshold)
        sendMessage(msg_EmuPause);
    sendMessage(msg_EmuFrameStep);
    waitAllMessages();
}

void EmuThread::emuReset()
{
    sendMessage(msg_EmuReset);
    waitMessage();
}

bool EmuThread::emuIsRunning()
{
    return emuStatus == emuStatus_Running;
}

bool EmuThread::emuIsActive()
{
    return emuActive;
}

int EmuThread::bootROM(const QStringList& filename, QString& errorstr)
{
    sendMessage({.type = msg_BootROM, .param = filename});
    waitMessage();
    if (!msgResult)
    {
        errorstr = msgError;
        return msgResult;
    }

    sendMessage(msg_EmuRun);
    waitMessage();
    errorstr = "";
    return msgResult;
}

int EmuThread::bootFirmware(QString& errorstr)
{
    sendMessage(msg_BootFirmware);
    waitMessage();
    if (!msgResult)
    {
        errorstr = msgError;
        return msgResult;
    }

    sendMessage(msg_EmuRun);
    waitMessage();
    errorstr = "";
    return msgResult;
}

int EmuThread::insertCart(const QStringList& filename, bool gba, QString& errorstr)
{
    MessageType msgtype = gba ? msg_InsertGBACart : msg_InsertCart;

    sendMessage({.type = msgtype, .param = filename});
    waitMessage();
    errorstr = msgResult ? "" : msgError;
    return msgResult;
}

void EmuThread::ejectCart(bool gba)
{
    sendMessage(gba ? msg_EjectGBACart : msg_EjectCart);
    waitMessage();
}

int EmuThread::insertGBAAddon(int type, QString& errorstr)
{
    sendMessage({.type = msg_InsertGBAAddon, .param = type});
    waitMessage();
    errorstr = msgResult ? "" : msgError;
    return msgResult;
}

int EmuThread::saveState(const QString& filename)
{
    sendMessage({.type = msg_SaveState, .param = filename});
    waitMessage();
    return msgResult;
}

int EmuThread::loadState(const QString& filename)
{
    sendMessage({.type = msg_LoadState, .param = filename});
    waitMessage();
    return msgResult;
}

int EmuThread::undoStateLoad()
{
    sendMessage(msg_UndoStateLoad);
    waitMessage();
    return msgResult;
}

int EmuThread::importSavefile(const QString& filename)
{
    sendMessage(msg_EmuReset);
    sendMessage({.type = msg_ImportSavefile, .param = filename});
    waitMessage(2);
    return msgResult;
}

void EmuThread::enableCheats(bool enable)
{
    sendMessage({.type = msg_EnableCheats, .param = enable});
    waitMessage();
}

void EmuThread::updateRenderer()
{
    // MelonPrimeDS {

    // getVsyncFlag
    bool vsyncFlag = emuInstance->getGlobalConfig().GetBool("Screen.VSync");  // MelonPrimeDS
    // VSync Override
    emuInstance->setVSyncGL(vsyncFlag); // MelonPrimeDS

	// } MelonPrimeDS

    if (videoRenderer != lastVideoRenderer)
    {
        switch (videoRenderer)
        {
            case renderer3D_Software:
                emuInstance->nds->GPU.SetRenderer3D(std::make_unique<SoftRenderer>());
                break;
            case renderer3D_OpenGL:
                emuInstance->nds->GPU.SetRenderer3D(GLRenderer::New());
                break;
            case renderer3D_OpenGLCompute:
                emuInstance->nds->GPU.SetRenderer3D(ComputeRenderer::New());
                break;
            default: __builtin_unreachable();
        }
    }
    lastVideoRenderer = videoRenderer;

    auto& cfg = emuInstance->getGlobalConfig();
    switch (videoRenderer)
    {
        case renderer3D_Software:
            static_cast<SoftRenderer&>(emuInstance->nds->GPU.GetRenderer3D()).SetThreaded(
                    cfg.GetBool("3D.Soft.Threaded"),
                    emuInstance->nds->GPU);
            break;
        case renderer3D_OpenGL:
            static_cast<GLRenderer&>(emuInstance->nds->GPU.GetRenderer3D()).SetRenderSettings(
                    cfg.GetBool("3D.GL.BetterPolygons"),
                    cfg.GetInt("3D.GL.ScaleFactor"));
            break;
        case renderer3D_OpenGLCompute:
            static_cast<ComputeRenderer&>(emuInstance->nds->GPU.GetRenderer3D()).SetRenderSettings(
                    cfg.GetInt("3D.GL.ScaleFactor"),
                    cfg.GetBool("3D.GL.HiresCoordinates"));
            break;
        default: __builtin_unreachable();
    }
    // MelonPrimeDS {
    // VSync Override
    emuInstance->setVSyncGL(vsyncFlag); // MelonPrimeDS

    /* MelonPrimeDS test
    if (vsyncFlag) {
        emuInstance->osdAddMessage(0, "Vsync is enabled.");
    }
    else {
        emuInstance->osdAddMessage(0, "Vsync is disabled.");
    }
    */

    // } MelonPrimeDS
}

void EmuThread::compileShaders()
{
    int currentShader, shadersCount;
    u64 startTime = SDL_GetPerformanceCounter();
    // kind of hacky to look at the wallclock, though it is easier than
    // than disabling vsync
    do
    {
        emuInstance->nds->GPU.GetRenderer3D().ShaderCompileStep(currentShader, shadersCount);
    } while (emuInstance->nds->GPU.GetRenderer3D().NeedsShaderCompile() &&
             (SDL_GetPerformanceCounter() - startTime) * perfCountsSec < 1.0 / 6.0);
    emuInstance->osdAddMessage(0, "Compiling shader %d/%d", currentShader+1, shadersCount);
}
