#ifdef _WIN32
// ヘッダ読込(クラス宣言とQt/Win32型の可視化のため)
#include "MelonPrimeRawInputWinFilter.h"

// 匿名名前空間定義(内部定数の隠蔽のため)
namespace {
    // マウスボタンフラグ集合定義(usButtonFlags判定のため)
    static constexpr USHORT kAllMouseBtnMask =
        0x0001 | 0x0002 | // LeftDown/LeftUp
        0x0004 | 0x0008 | // RightDown/RightUp
        0x0010 | 0x0020 | // MiddleDown/MiddleUp
        0x0040 | 0x0080 | // X1Down/X1Up
        0x0100 | 0x0200;  // X2Down/X2Up

    // VK→マウスIndex簡易LUT定義(互換維持のため)
    static constexpr uint8_t kMouseButtonLUT[8] = {
        0xFF, // 0 unused
        0,    // VK_LBUTTON
        1,    // VK_RBUTTON
        0xFF, // 3
        2,    // VK_MBUTTON
        3,    // VK_XBUTTON1
        4,    // VK_XBUTTON2
        0xFF  // 7
    };
}

/**
 * 構築子本体.
 *
 *
 * @brief RawInput登録と内部状態初期化を行う.
 */
RawInputWinFilter::RawInputWinFilter()
{
    // デバイス記述構築(マウス登録のため)
    m_rid[0] = { 0x01, 0x02, 0, nullptr };
    // デバイス記述構築(キーボード登録のため)
    m_rid[1] = { 0x01, 0x06, 0, nullptr };
    // デバイス登録実行(入力受理のため)
    RegisterRawInputDevices(m_rid, 2, sizeof(RAWINPUTDEVICE));

    // 互換配列初期化(既存I/F維持のため)
    for (auto& a : m_vkDownCompat) a.store(0, std::memory_order_relaxed);
    // 互換配列初期化(既存I/F維持のため)
    for (auto& b : m_mbCompat)     b.store(0, std::memory_order_relaxed);

    // エッジ配列初期化(立上り立下り検出のため)
    for (auto& w : m_hkPrev) w.store(0, std::memory_order_relaxed);

    // 前計算マスク初期化(安全側運用のため)
    std::memset(m_hkMask.data(), 0, sizeof(m_hkMask));
}

/**
 * 破棄子本体.
 *
 *
 * @brief RawInput登録解除を行う.
 */
RawInputWinFilter::~RawInputWinFilter()
{
    // 解除指定構築(マウス解除のため)
    m_rid[0].dwFlags = RIDEV_REMOVE; m_rid[0].hwndTarget = nullptr;
    // 解除指定構築(キーボード解除のため)
    m_rid[1].dwFlags = RIDEV_REMOVE; m_rid[1].hwndTarget = nullptr;
    // デバイス解除実行(後処理完了のため)
    RegisterRawInputDevices(m_rid, 2, sizeof(RAWINPUTDEVICE));
}

/**
 * ネイティブイベントフィルタ本体.
 *
 *
 * @param eventType イベント種別文字列.
 * @param message Win32 MSGポインタ.
 * @param result 予約領域.
 * @return bool 継続可否.
 */
bool RawInputWinFilter::nativeEventFilter(const QByteArray& /*eventType*/, void* message, qintptr* /*result*/)
{
    // MSG取得処理(安全側運用のため)
    MSG* msg = static_cast<MSG*>(message);
    // Null/種別検査処理(早期離脱のため)
    if (!msg || msg->message != WM_INPUT) return false;

    // バッファ長設定処理(単一取得固定長のため)
    UINT size = sizeof(m_rawBuf);
    // RAWINPUT取得処理(単一呼び出しのため)
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(msg->lParam),
        RID_INPUT, m_rawBuf, &size,
        sizeof(RAWINPUTHEADER)) == (UINT)-1) {
        // 失敗離脱処理(堅牢性確保のため)
        return false;
    }

    // RAWINPUT参照化処理(アクセス効率化のため)
    RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(m_rawBuf);
    // タイプ抽出処理(分岐準備のため)
    const DWORD type = raw->header.dwType;

    // マウス経路処理(頻度最適化のため)
    if (type == RIM_TYPEMOUSE) {
        // マウスデータ参照処理(可読性向上のため)
        const RAWMOUSE& m = raw->data.mouse;

        // 相対移動抽出処理(整数変換のため)
        const LONG dx_ = m.lLastX;
        // 相対移動抽出処理(整数変換のため)
        const LONG dy_ = m.lLastY;
        // 非ゼロ検査処理(原子RMW回避のため)
        if ((dx_ | dy_) != 0) {
            // 旧値読取処理(CAS準備のため)
            uint64_t oldv = m_dxyPack.load(std::memory_order_relaxed);
            // 反復更新処理(競合時再試行のため)
            for (;;) {
                // 下位抽出処理(32bit分離のため)
                uint32_t lo = (uint32_t)(oldv & 0xFFFFFFFFu);
                // 上位抽出処理(32bit分離のため)
                uint32_t hi = (uint32_t)(oldv >> 32);
                // 下位加算処理(キャリー遮断のため)
                lo += (uint32_t)(int32_t)dx_;
                // 上位加算処理(キャリー遮断のため)
                hi += (uint32_t)(int32_t)dy_;
                // 新値組立処理(再パックのため)
                const uint64_t newv = (uint64_t)lo | ((uint64_t)hi << 32);
                // CAS適用処理(原子1回更新のため)
                if (m_dxyPack.compare_exchange_weak(
                    oldv, newv,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
                    // 更新成功離脱処理(ループ終了のため)
                    break;
                }
                // 失敗再試行処理(oldv更新済みのため)
            }
        }


        // ボタンフラグ抽出処理(差分更新のため)
        const USHORT f = m.usButtonFlags;
        // 早期離脱検査処理(分岐最小化のため)
        if ((f & kAllMouseBtnMask) == 0) return false;

        // ダウンマスク構築処理(5bit生成のため)
        const uint8_t downMask =
            ((f & 0x0001) ? 0x01 : 0) | ((f & 0x0004) ? 0x02 : 0) |
            ((f & 0x0010) ? 0x04 : 0) | ((f & 0x0040) ? 0x08 : 0) |
            ((f & 0x0100) ? 0x10 : 0);

        // アップマスク構築処理(5bit生成のため)
        const uint8_t upMask =
            ((f & 0x0002) ? 0x01 : 0) | ((f & 0x0008) ? 0x02 : 0) |
            ((f & 0x0020) ? 0x04 : 0) | ((f & 0x0080) ? 0x08 : 0) |
            ((f & 0x0200) ? 0x10 : 0);

        // 現在値取得処理(差分計算のため)
        const uint8_t cur = m_state.mouseButtons.load(std::memory_order_relaxed);
        // 次状態算出処理(一括更新のため)
        const uint8_t nxt = (uint8_t)((cur | downMask) & (uint8_t)~upMask);
        // 状態書換処理(可視化更新のため)
        m_state.mouseButtons.store(nxt, std::memory_order_relaxed);

        // 互換配列差分生成処理(ストア最小化のため)
        const uint8_t chg = (uint8_t)(cur ^ nxt);
        // 差分適用処理(必要箇所のみ更新のため)
        if (chg) {
            // 左ボタン更新処理(差分適用のため)
            if (chg & 0x01) m_mbCompat[0].store((nxt & 0x01) ? 1u : 0u, std::memory_order_relaxed);
            // 右ボタン更新処理(差分適用のため)
            if (chg & 0x02) m_mbCompat[1].store((nxt & 0x02) ? 1u : 0u, std::memory_order_relaxed);
            // 中ボタン更新処理(差分適用のため)
            if (chg & 0x04) m_mbCompat[2].store((nxt & 0x04) ? 1u : 0u, std::memory_order_relaxed);
            // X1ボタン更新処理(差分適用のため)
            if (chg & 0x08) m_mbCompat[3].store((nxt & 0x08) ? 1u : 0u, std::memory_order_relaxed);
            // X2ボタン更新処理(差分適用のため)
            if (chg & 0x10) m_mbCompat[4].store((nxt & 0x10) ? 1u : 0u, std::memory_order_relaxed);
        }

        // 継続復帰処理(Qt側継続のため)
        return false;
    }
    // キーボード経路処理(正規化最適化のため)
    else if (type == RIM_TYPEKEYBOARD) {
        // キーボードデータ参照処理(可読性向上のため)
        const RAWKEYBOARD& kb = raw->data.keyboard;
        // 仮想キー取得処理(正規化準備のため)
        UINT vk = kb.VKey;
        // フラグ抽出処理(押下/解放判定のため)
        const USHORT flags = kb.Flags;
        // 解放判定生成処理(状態反転のため)
        const bool isUp = (flags & RI_KEY_BREAK) != 0;

        // Shift正規化処理(システムコール回避のため)
        if (vk == VK_SHIFT) {
            // MakeCode参照処理(左右識別のため)
            const USHORT sc = kb.MakeCode;
            // 左右判定処理(0x2A=LShift,0x36=RShiftのため)
            vk = (sc == 0x2A) ? VK_LSHIFT :
                (sc == 0x36) ? VK_RSHIFT : VK_SHIFT;
        }
        // Control正規化処理(E0ビット活用のため)
        else if (vk == VK_CONTROL) {
            vk = (flags & RI_KEY_E0) ? VK_RCONTROL : VK_LCONTROL;
        }
        // Alt正規化処理(E0ビット活用のため)
        else if (vk == VK_MENU) {
            vk = (flags & RI_KEY_E0) ? VK_RMENU : VK_LMENU;
        }
        // それ以外維持処理(副作用回避のため)
        else {
        }

        // VKビット更新処理(原子1回更新のため)
        setVkBit(vk, !isUp);

        // 互換配列更新処理(既存I/F維持のため)
        if (vk < m_vkDownCompat.size())
            m_vkDownCompat[vk].store(!isUp, std::memory_order_relaxed);

        // 継続復帰処理(Qt側継続のため)
        return false;
    }

    // その他無視復帰処理(安定運用のため)
    return false;
}

/**
 * 相対デルタ取得本体.
 *
 *
 * @param outDx X相対量.
 * @param outDy Y相対量.
 */
void RawInputWinFilter::fetchMouseDelta(int& outDx, int& outDy)
{
    // パック交換処理(原子1回読みのため)
    const uint64_t v = m_dxyPack.exchange(0, std::memory_order_relaxed);
    // X解凍処理(下位32bit抽出のため)
    outDx = (int32_t)(v & 0xFFFFFFFFu);
    // Y解凍処理(上位32bit抽出のため)
    outDy = (int32_t)(v >> 32);
}

/**
 * 相対デルタ破棄本体.
 *
 *
 * @brief 未読相対デルタをゼロ化する.
 */
void RawInputWinFilter::discardDeltas()
{
    // パックゼロ化処理(破棄のため)
    (void)m_dxyPack.exchange(0, std::memory_order_relaxed);
}

/**
 * 全キーリセット本体.
 *
 *
 * @brief VK配列と互換配列を未押下化する.
 */
void RawInputWinFilter::resetAllKeys()
{
    // VKゼロ化処理(64bit×4のため)
    for (auto& w : m_state.vkDown) w.store(0, std::memory_order_relaxed);
    // 互換配列ゼロ化処理(整合性確保のため)
    for (auto& a : m_vkDownCompat) a.store(0, std::memory_order_relaxed);
}

/**
 * 全マウスボタンリセット本体.
 *
 *
 * @brief 内部ビットと互換配列を未押下化する.
 */
void RawInputWinFilter::resetMouseButtons()
{
    // 内部ゼロ化処理(整合性確保のため)
    m_state.mouseButtons.store(0, std::memory_order_relaxed);
    // 互換配列ゼロ化処理(整合性確保のため)
    for (auto& b : m_mbCompat) b.store(0, std::memory_order_relaxed);
}

/**
 * ホットキーエッジリセット本体.
 *
 *
 * @brief 押下/解放エッジの前回状態をゼロ化する.
 */
void RawInputWinFilter::resetHotkeyEdges()
{
    // 配列ゼロ化処理(再同期のため)
    for (auto& w : m_hkPrev) w.store(0, std::memory_order_relaxed);
}

/**
 * マスク構築ヘルパ本体.
 *
 *
 * @param m マスク参照.
 * @param vk 仮想キー.
 */
FORCE_INLINE void RawInputWinFilter::addVkToMask(HotkeyMask& m, UINT vk) noexcept
{
    // マウス特例判定処理(LUT活用のため)
    if (vk < 8u) {
        // マウスIndex取得処理(簡易LUT使用のため)
        const uint8_t b = kMouseButtonLUT[vk];
        // 正当性検査処理(安全側運用のため)
        if (b < 5u) { m.mouseMask |= (1u << b); m.hasMask = 1; }
        // 早期復帰処理(不要分岐回避のため)
        return;
    }
    // VK範囲検査処理(256未満のため)
    if (vk < 256u) {
        // インデックス算出処理(64bit境界のため)
        const uint32_t idx = vk >> 6;
        // ビット算出処理(低コスト更新のため)
        const uint64_t bit = 1ULL << (vk & 63u);
        // マスク設定処理(前計算充填のため)
        m.vkMask[idx] |= bit;
        // 有効化設定処理(分岐短絡のため)
        m.hasMask = 1;
    }
}

/**
 * ホットキー登録本体.
 *
 *
 * @param hk ホットキーID.
 * @param vks 仮想キー配列.
 */
void RawInputWinFilter::setHotkeyVks(int hk, const std::vector<UINT>& vks)
{
    // 高速パス検査処理(範囲内判定のため)
    if ((unsigned)hk < (unsigned)kMaxHotkeyId) {
        // 参照取得処理(可読性向上のため)
        HotkeyMask& m = m_hkMask[hk];
        // 構造体初期化処理(AVX非依存のため)
        std::memset(&m, 0, sizeof(m));
        // 上限制約設定処理(実用範囲のため)
        const size_t n = (vks.size() > 8) ? 8 : vks.size();
        // 追加反復処理(前計算充填のため)
        for (size_t i = 0; i < n; ++i) addVkToMask(m, vks[i]);
        // 早期復帰処理(不要分岐回避のため)
        return;
    }
    // フォールバック格納処理(柔軟性確保のため)
    m_hkToVk[hk] = vks;
}

/**
 * ホットキー押下中判定本体.
 *
 *
 * @param hk ホットキーID.
 * @return bool 押下中.
 */
bool RawInputWinFilter::hotkeyDown(int hk) const noexcept
{
    // 高速パス検査処理(前計算活用のため)
    if ((unsigned)hk < (unsigned)kMaxHotkeyId) {
        // 参照取得処理(可読性向上のため)
        const HotkeyMask& m = m_hkMask[hk];
        // 有効検査処理(早期離脱のため)
        if (m.hasMask) {
            // VK命中検査処理(OR集約のため)
            const bool vkHit =
                ((m_state.vkDown[0].load(std::memory_order_relaxed) & m.vkMask[0]) |
                    (m_state.vkDown[1].load(std::memory_order_relaxed) & m.vkMask[1]) |
                    (m_state.vkDown[2].load(std::memory_order_relaxed) & m.vkMask[2]) |
                    (m_state.vkDown[3].load(std::memory_order_relaxed) & m.vkMask[3])) != 0ULL;

            // マウス命中検査処理(AND照合のため)
            const bool mouseHit =
                (m_state.mouseButtons.load(std::memory_order_relaxed) & m.mouseMask) != 0u;

            // 短絡評価復帰処理(合致判定のため)
            if (vkHit | mouseHit) return true;
        }
    }

    // フォールバック検査処理(柔軟性確保のため)
    auto it = m_hkToVk.find(hk);
    // 未登録検査処理(早期離脱のため)
    if (it != m_hkToVk.end()) {
        // 逐次照合処理(簡易互換のため)
        for (UINT vk : it->second) {
            // マウス特例判定処理(互換維持のため)
            if (vk < 8u) {
                // LUT参照処理(低分岐のため)
                const uint8_t b = kMouseButtonLUT[vk];
                // 押下判定復帰処理(短絡評価のため)
                if (b < 5u && getMouseButton((int)b)) return true;
            }
            // VK押下検査処理(短絡評価のため)
            else if (getVkState(vk)) {
                // 命中復帰処理(逐次終了のため)
                return true;
            }
        }
    }
    // 不一致復帰処理(デフォルト否定のため)
    return false;
}

/**
 * ホットキー立上り検出本体.
 *
 *
 * @param hk ホットキーID.
 * @return bool 立上り.
 */
bool RawInputWinFilter::hotkeyPressed(int hk) noexcept
{
    // 現在状態取得処理(整合性確保のため)
    const bool now = hotkeyDown(hk);

    // 高速パス検査処理(前計算活用のため)
    if ((unsigned)hk < (unsigned)kMaxHotkeyId) {
        // インデックス算出処理(64bit境界のため)
        const size_t idx = ((unsigned)hk) >> 6;
        // ビット算出処理(単一RMWのため)
        const uint64_t bit = 1ULL << (hk & 63);
        // 立上り検出処理(OR旧値比較のため)
        if (now) {
            // 旧値取得処理(差分評価のため)
            const uint64_t prev = m_hkPrev[idx].fetch_or(bit, std::memory_order_acq_rel);
            // 立上り復帰処理(差分真判定のため)
            return (prev & bit) == 0;
        }
        else {
            // 立下り反映処理(AND旧値比較のため)
            (void)m_hkPrev[idx].fetch_and(~bit, std::memory_order_acq_rel);
            // 不一致復帰処理(常時否定のため)
            return false;
        }
    }

    // フォールバック配列定義(簡易保持のため)
    static std::array<std::atomic<uint8_t>, 1024> s{};
    // インデックス算出処理(循環領域のため)
    const size_t i = ((unsigned)hk) & 1023;
    // 旧値交換処理(単一RMWのため)
    const uint8_t prev = s[i].exchange(now ? 1u : 0u, std::memory_order_acq_rel);
    // 立上り復帰処理(差分判定のため)
    return now && !prev;
}

/**
 * ホットキー立下り検出本体.
 *
 *
 * @param hk ホットキーID.
 * @return bool 立下り.
 */
bool RawInputWinFilter::hotkeyReleased(int hk) noexcept
{
    // 現在状態取得処理(整合性確保のため)
    const bool now = hotkeyDown(hk);

    // 高速パス検査処理(前計算活用のため)
    if ((unsigned)hk < (unsigned)kMaxHotkeyId) {
        // インデックス算出処理(64bit境界のため)
        const size_t idx = ((unsigned)hk) >> 6;
        // ビット算出処理(単一RMWのため)
        const uint64_t bit = 1ULL << (hk & 63);
        // 立下り検出処理(AND旧値比較のため)
        if (!now) {
            // 旧値取得処理(差分評価のため)
            const uint64_t prev = m_hkPrev[idx].fetch_and(~bit, std::memory_order_acq_rel);
            // 立下り復帰処理(差分真判定のため)
            return (prev & bit) != 0;
        }
        else {
            // 押下反映処理(ORで印加のため)
            (void)m_hkPrev[idx].fetch_or(bit, std::memory_order_acq_rel);
            // 不一致復帰処理(常時否定のため)
            return false;
        }
    }

    // フォールバック配列定義(簡易保持のため)
    static std::array<std::atomic<uint8_t>, 1024> s{};
    // インデックス算出処理(循環領域のため)
    const size_t i = ((unsigned)hk) & 1023;
    // 旧値交換処理(単一RMWのため)
    const uint8_t prev = s[i].exchange(now ? 1u : 0u, std::memory_order_acq_rel);
    // 立下り復帰処理(差分判定のため)
    return (!now) && prev;
}

/**
 * VK状態設定ヘルパ本体.
 *
 *
 * @param vk 仮想キー.
 * @param down 押下状態.
 */
FORCE_INLINE void RawInputWinFilter::setVkBit(UINT vk, bool down) noexcept
{
    // 範囲検査処理(安全側運用のため)
    if (vk >= 256u) return;
    // ワード参照取得処理(64bit境界のため)
    auto& word = m_state.vkDown[vk >> 6];
    // ビット算出処理(低コスト更新のため)
    const uint64_t bit = 1ULL << (vk & 63u);
    // 条件更新処理(原子1回選択のため)
    if (down) (void)word.fetch_or(bit, std::memory_order_relaxed);
    else      (void)word.fetch_and(~bit, std::memory_order_relaxed);
}

/**
 * VK状態取得ヘルパ本体.
 *
 *
 * @param vk 仮想キー.
 * @return bool 押下中.
 */
FORCE_INLINE bool RawInputWinFilter::getVkState(UINT vk) const noexcept
{
    // 範囲検査処理(安全側運用のため)
    if (vk >= 256u) return false;
    // ワード読取処理(低コスト参照のため)
    const uint64_t w = m_state.vkDown[vk >> 6].load(std::memory_order_relaxed);
    // ビット検査復帰処理(押下判定のため)
    return (w & (1ULL << (vk & 63u))) != 0ULL;
}

/**
 * マウスボタン取得ヘルパ本体.
 *
 *
 * @param b ボタンIndex.
 * @return bool 押下中.
 */
FORCE_INLINE bool RawInputWinFilter::getMouseButton(int b) const noexcept
{
    // 範囲検査処理(安全側運用のため)
    if ((unsigned)b >= 5u) return false;
    // 現在値取得処理(低コスト参照のため)
    const uint8_t bits = m_state.mouseButtons.load(std::memory_order_relaxed);
    // ビット検査復帰処理(押下判定のため)
    return (bits & (1u << b)) != 0u;
}

#endif // _WIN32
