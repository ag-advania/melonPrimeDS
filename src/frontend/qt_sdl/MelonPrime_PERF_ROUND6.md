# MelonPrime パフォーマンス改善提案: Round 6

**対象:** 現行コードベース（Round 5 + P-32 適用済み）  
**観点:** フレームループ・入力パイプラインの syscall 削減 / ホットパスの命令数削減  
**前提:** リファクタリング統合ドキュメントの監査結果に基づく

---

## 総括: 5 つの提案 (P-35 はリバート)

| ID | 種別 | 推定効果 | リスク | 状態 | 対象ファイル |
|---|---|---|---|---|---|
| P-33 | syscall 削減 | ~500–2000 cyc/frame | 低 | ✅ 適用 | `EmuThread.cpp`, `MelonPrime.cpp`, `MelonPrime.h` |
| P-34 | syscall 削減 | ~300–600 cyc/frame | 低 | ✅ 適用 | `EmuInstanceInput.cpp` |
| P-35 | 冗長 syscall 除去 | ~200–500 cyc/frame | **高** | ❌ リバート | `MelonPrimeRawInputWinFilter.cpp` |
| P-36 | ホットパス改善 | ~5–15 cyc/frame | 極低 | ✅ 適用 | `MelonPrimeInGame.cpp` |
| P-37 | ホットパス改善 | ~3–8 cyc/frame | 極低 | ✅ 適用 | `MelonPrimeRawInputState.cpp` |

---

## P-33: PrePollRawInput 除去 (P-20 完了)

### 背景

リファクタリング統合ドキュメントのセクション 9.12 / 11.4 にて:

> P-20 は Round 5 文書の到達点であり、現コードでは未完了

P-19 により `HiddenWndProc` が WM_INPUT 到着時に即座に `processRawInput()` を実行するため、
`PrePollRawInput()` の `processRawInputBatched()` + `drainPendingMessages()` は冗長である。

### 現状の問題

フレームごとに以下の冗長な syscall が発生:

```
EmuThread main loop:
  melonPrime->PrePollRawInput()      ← ここ (冗長)
    → m_rawFilter->Poll()
      → processRawInputBatched()     ← GetRawInputBuffer syscall (P-19 により殆ど空)
      → drainPendingMessages()       ← processRawInputBatched + PeekMessage ループ

frameAdvanceOnce:
  melonPrime->PrePollRawInput()      ← ここも (冗長)
    → 同上
```

**合計: フレームあたり 2 回の冗長 `Poll()`** = 4 × `GetRawInputBuffer` + 2 × `PeekMessage` ループ。

### 変更箇所

#### EmuThread.cpp (メインループ側, ~L500–503)

```cpp
// 変更前:
#ifdef MELONPRIME_DS
        // P-14: Pre-drain raw input before SDL's message pump (inputProcess).
        // P-19: HiddenWndProc also captures any WM_INPUT dispatched by SDL.
        melonPrime->PrePollRawInput();
#endif
        emuInstance->inputProcess();

// 変更後:
        emuInstance->inputProcess();
        // P-33: PrePollRawInput removed. P-19 (HiddenWndProc) captures all
        // WM_INPUT on dispatch. SDL's PeekMessage cannot lose data anymore.
```

#### EmuThread.cpp (frameAdvanceOnce 内, ~L253–257)

```cpp
// 変更前:
        // P-14: Pre-drain raw input before SDL's message pump.
        melonPrime->PrePollRawInput();
        // P-15: Late-Poll Joystick — refresh SDL state after Sleep.
        emuInstance->inputRefreshJoystickState();

// 変更後:
        // P-33: PrePollRawInput removed — P-19 handles all WM_INPUT capture.
        // P-15: Late-Poll Joystick — refresh SDL state after Sleep.
        emuInstance->inputRefreshJoystickState();
```

#### MelonPrime.h — `PrePollRawInput()` の deprecation

```cpp
// 変更前:
    // P-14: Pre-drain raw input buffer before SDL's message pump.
    // Belt-and-suspenders with P-19 (HiddenWndProc processRawInput).
    void PrePollRawInput();

// 変更後:
    // P-14/P-33: PrePollRawInput retired. P-19 (HiddenWndProc) captures
    // all WM_INPUT at dispatch time, making pre-drain unnecessary.
    // Kept as empty inline for source compatibility; remove in future cleanup.
    void PrePollRawInput() {}
```

#### MelonPrime.cpp — 実装除去

`MelonPrimeCore::PrePollRawInput()` の実装（L225–232）を削除。
ヘッダー側が空インラインになるため、リンクエラーなし。

### 効果

- `GetRawInputBuffer` syscall: 4 回/frame → 2 回/frame（`PollAndSnapshot` 内のみ）
- `PeekMessage` ループ: 2 回/frame → 0 回（`DeferredDrain` のみ残留）
- 推定削減: **500–2000 cyc/frame**（8kHz マウス環境で顕著）

### 安全性

P-19 により、SDL が `PeekMessage` で WM_INPUT を dispatch しても:
1. `HiddenWndProc` が `processRawInput(HRAWINPUT)` で即時キャプチャ
2. `PollAndSnapshot` 内の `processRawInputBatched()` が残りを回収
3. `DeferredDrain` が描画後にキューを清掃

三重の防御が成立しており、`PrePollRawInput` 除去は安全。

---

## P-34: inputProcess() のジョイスティック不在時 SDL スキップ

### 背景

P-23 は `inputRefreshJoystickState()` 内で no-joystick 高速パスを実装したが、
`inputProcess()` 自体は **毎フレーム無条件** で以下を実行する:

```cpp
SDL_LockMutex(joyMutex.get());   // syscall
SDL_JoystickUpdate();             // SDL 内部処理
// ... joystick detach check ...
SDL_UnlockMutex(joyMutex.get()); // syscall
```

KB+M 専用プレイヤーでは、この 3 syscall は完全に無駄である。

### 変更箇所

#### EmuInstanceInput.cpp `inputProcess()` MelonPrime ブロック (~L529–561)

```cpp
// 変更前:
    SDL_LockMutex(joyMutex.get());
    SDL_JoystickUpdate();

    if (joystick)
    {
        if (!SDL_JoystickGetAttached(joystick))
        {
            SDL_JoystickClose(joystick);
            joystick = nullptr;
        }
    }
    if (!joystick && (SDL_NumJoysticks() > 0))
    {
        openJoystick();
    }

    SDL_UnlockMutex(joyMutex.get());

// 変更後:
    // P-34: Skip SDL mutex/update when no joystick connected.
    // Config dialog joystick reads are handled by the throttled check.
    // This saves 2 mutex syscalls + SDL_JoystickUpdate per frame for KB+M.
    if (LIKELY(!joystick)) {
        // Throttled attachment check (~once per second at 60fps).
        // SDL_JoystickUpdate is needed for SDL_NumJoysticks to reflect changes.
        static uint8_t s_inputJoyCheck = 0;
        if (UNLIKELY(++s_inputJoyCheck >= 60)) {
            s_inputJoyCheck = 0;
            SDL_LockMutex(joyMutex.get());
            SDL_JoystickUpdate();
            if (SDL_NumJoysticks() > 0) {
                openJoystick();
            }
            SDL_UnlockMutex(joyMutex.get());
        }
    }
    else {
        SDL_LockMutex(joyMutex.get());
        SDL_JoystickUpdate();

        if (!SDL_JoystickGetAttached(joystick))
        {
            SDL_JoystickClose(joystick);
            joystick = nullptr;
        }

        SDL_UnlockMutex(joyMutex.get());
    }
```

### 効果

- KB+M プレイヤー: **2 mutex syscall + SDL_JoystickUpdate を 59/60 フレームでスキップ**
- 推定削減: **300–600 cyc/frame**（Windows mutex acquire/release は各 ~100–200 cyc）

### 注意事項

- `static uint8_t s_inputJoyCheck` はシングルインスタンス前提。マルチインスタンス対応が必要な場合はメンバ変数化する。
- Config ダイアログでのジョイスティック読み取り:
  emu 一時停止中は `inputProcess` が高頻度で呼ばれないため、throttle の影響は無視できる。

---

## P-35: drainPendingMessages 内の冗長 processRawInputBatched 除去 — ❌ リバート

### 提案内容

`DeferredDrain()` が呼ぶ `drainPendingMessages()` 内の `processRawInputBatched()` (GetRawInputBuffer) を除去し、PeekMessage ループのみの `drainMessagesOnly()` に置き換える。

### リバート理由: stuck keys 再発

FIX-1 で文書化された **shared-buffer semantics** が原因。GetRawInputBuffer と GetRawInputData は Windows 内部で raw input バッファを共有しており、以下のシーケンスでデータロスが発生する:

```
PollAndSnapshot:
  processRawInputBatched()  → GetRawInputBuffer がバッファ上の raw data を消費

[drawScreen 中に新規 WM_INPUT 到着]

DeferredDrain (P-35 適用後 — drainMessagesOnly のみ):
  PeekMessage(PM_REMOVE)   → WM_INPUT を dispatch
  → HiddenWndProc          → processRawInput(HRAWINPUT)
  → GetRawInputData(hRaw)  → 共有バッファは PollAndSnapshot の
                               GetRawInputBuffer で既に消費済み → 失敗!
                            → key-up イベントがロスト → stuck key
```

元の `drainPendingMessages` では `processRawInputBatched` が PeekMessage の **前** に走るため、GetRawInputBuffer が新しいデータを先に確保し、後続の PeekMessage → GetRawInputData が失敗しても影響がなかった。

### 教訓

- `drainPendingMessages` 内の `processRawInputBatched` は「冗長な syscall」に見えるが、**shared-buffer に対するセーフティネットとして必須**
- P-14 / FIX-1 が「belt-and-suspenders」と呼んでいた設計は、まさにこの問題を防ぐためだった
- P-19 (HiddenWndProc 即時 processRawInput) は single-event の救出には有効だが、shared-buffer の消費タイミング問題は解決しない

---

## P-36: HandleInGameLogic の NDS ポインタ再取得除去

### 背景

`HandleInGameLogic()` の冒頭で `auto* const nds = emuInstance->getNDS()` を取得するが、
内部で呼ばれる cold パス（`HandleRareMorph`, `HandleRareWeaponCheckStart` 等）が
それぞれ独自に `emuInstance->getNDS()` を再呼び出ししている。

### 変更箇所

#### MelonPrimeInGame.cpp

cold パス関数に `nds` パラメータを追加して再取得を除去。ただし **cold パスなので効果は微小**。
代わりにホットパスに焦点を当てる。

より重要な最適化: **`HandleMorphBallBoost` の早期脱出改善**

```cpp
// 変更前:
HOT_FUNCTION bool MelonPrimeCore::HandleMorphBallBoost()
{
    if (!m_flags.test(StateFlags::BIT_IS_SAMUS)) return false;

    if (IsDown(IB_MORPH_BOOST)) {
        // ... full boost logic ...
    }
    else {
        if (UNLIKELY(m_aimBlockBits & AIMBLK_MORPHBALL_BOOST)) {
            SetAimBlockBranchless(AIMBLK_MORPHBALL_BOOST, false);
        }
    }
    return false;
}

// 変更後:
HOT_FUNCTION bool MelonPrimeCore::HandleMorphBallBoost()
{
    // P-36: Combined early exit — skip both samus check AND boost-down check
    // in a single branch. ~70%+ of frames hit this (non-Samus or no boost key).
    if (LIKELY(!m_flags.test(StateFlags::BIT_IS_SAMUS) || !IsDown(IB_MORPH_BOOST))) {
        // Guard redundant store - 99%+ of frames this bit is already 0.
        if (UNLIKELY(m_aimBlockBits & AIMBLK_MORPHBALL_BOOST)) {
            SetAimBlockBranchless(AIMBLK_MORPHBALL_BOOST, false);
        }
        return false;
    }

    // Samus + boost key held — full logic (rare path)
    const bool isAltForm = (*m_ptrs.isAltForm) == 0x02;
    m_flags.assign(StateFlags::BIT_IS_ALT_FORM, isAltForm);

    if (isAltForm) {
        const uint8_t boostGauge = *m_ptrs.boostGauge;
        const bool isBoosting = (*m_ptrs.isBoosting) != 0x00;
        const bool gaugeEnough = boostGauge > 0x0A;

        SetAimBlockBranchless(AIMBLK_MORPHBALL_BOOST, true);

        if (!IsDown(IB_WEAPON_CHECK)) {
            emuInstance->getNDS()->ReleaseScreen();
        }

        InputSetBranchless(INPUT_R, !isBoosting && gaugeEnough);

        if (isBoosting) {
            SetAimBlockBranchless(AIMBLK_MORPHBALL_BOOST, false);
        }
        return true;
    }

    return false;
}
```

### 効果

- 非サムスキャラ使用時: `BIT_IS_SAMUS` テストのみで即 return（変更なし）
- サムスだがブースト未押下: 元は `BIT_IS_SAMUS` チェック → `IsDown` チェック → else ブランチ。変更後は 1 分岐で到達
- ブランチ予測の改善: `LIKELY` ヒントにより fast path をフォールスルーに配置
- 推定削減: **5–15 cyc/frame**（分岐予測ミス回避が主）

---

## P-37: processRawInputBatched のコミットフェーズ最適化

### 背景

`processRawInputBatched` のコミットフェーズ:

```cpp
if (localAccX) {
    const int64_t cur = m_accumMouseX.load(std::memory_order_relaxed);
    m_accumMouseX.store(cur + localAccX, std::memory_order_relaxed);
}
if (localAccY) {
    const int64_t cur = m_accumMouseY.load(std::memory_order_relaxed);
    m_accumMouseY.store(cur + localAccY, std::memory_order_relaxed);
}
```

8kHz マウスでは `localAccX` と `localAccY` は **ほぼ常に非ゼロ**（133 events/frame で移動なしは稀）。
しかし、ゼロチェックの分岐自体は予測がよく当たるため、悪影響は少ない。

より重要な最適化は **fetch_add ではなく load+store を維持しつつ、X/Y を結合すること**:

### 変更箇所

```cpp
// 変更前:
    if (localAccX) {
        const int64_t cur = m_accumMouseX.load(std::memory_order_relaxed);
        m_accumMouseX.store(cur + localAccX, std::memory_order_relaxed);
    }
    if (localAccY) {
        const int64_t cur = m_accumMouseY.load(std::memory_order_relaxed);
        m_accumMouseY.store(cur + localAccY, std::memory_order_relaxed);
    }

// 変更後:
    // P-37: Combined nonzero check reduces branch count.
    // With 8kHz mouse, (localAccX | localAccY) is nonzero on ~99% of frames.
    if (localAccX | localAccY) {
        const int64_t curX = m_accumMouseX.load(std::memory_order_relaxed);
        const int64_t curY = m_accumMouseY.load(std::memory_order_relaxed);
        if (localAccX) m_accumMouseX.store(curX + localAccX, std::memory_order_relaxed);
        if (localAccY) m_accumMouseY.store(curY + localAccY, std::memory_order_relaxed);
    }
```

### 効果

- マウス移動なしフレーム: 2 分岐 → 1 分岐
- 片軸のみ移動: load の分離により、不要な store をスキップ（変化なし）
- 推定削減: **3–8 cyc/frame**（微小だが 0 コスト変更）

---

## 適用優先順位

| 優先度 | 提案 | 理由 |
|---|---|---|
| 1 | **P-33** | 最大効果。設計ドキュメントで既に到達点として定義済み。最もリスクが低い |
| 2 | **P-34** | KB+M プレイヤーへの直接的な恩恵。SDL overhead の定常的除去 |
| — | ~~P-35~~ | ❌ **リバート済み。** shared-buffer semantics により stuck keys 再発。drainPendingMessages 内の processRawInputBatched は必須のセーフティネット |
| 3 | **P-36** | ホットパスの分岐改善。コードの明瞭性も向上 |
| 4 | **P-37** | 微小改善。リスクゼロだが効果も最小 |

---

## 合計推定効果 (P-35 リバート後)

| 環境 | 削減 (cyc/frame) | 備考 |
|---|---|---|
| 8kHz マウス + KB+M | ~800–2600 | P-33 + P-34 が主要 |
| 通常マウス + KB+M | ~300–1000 | syscall 削減の効果が相対的に大 |
| ジョイスティック使用 | ~500–2000 | P-34 の効果なし、他は同等 |

---

## 未検討・今後の課題

| 項目 | 理由 |
|---|---|
| `DeferredDrain` の P-26b 再検討 | 8kHz 環境では毎フレーム PeekMessage ループ自体がコスト。P-19 により drain 頻度を下げても安全な可能性がある。ただし P-26b が以前撤回された経緯もあり、慎重に検証が必要 |
| `DeferredDrain` 内の `processRawInputBatched` 除去 (P-35 再挑戦) | **shared-buffer semantics (FIX-1) が解消されない限り不可。** GetRawInputBuffer と GetRawInputData のバッファ共有を断つには、processRawInput (single-event) 側で全データを確実に処理する保証が必要だが、Windows の raw input 内部実装に依存するため現時点では不可能 |
| `FrameAdvanceDefault` の `usesOpenGL()` キャッシュ | 元文書で却下済み（コールドパス）。再検討の必要なし |
| `processRawInputBatched` の TLS バッファ | 16KB thread_local static は TLS アクセスコストがあるが、スタック上 16KB は `__chkstk` と L1 圧迫で却下済み。現状維持が妥当 |
| `EmuThread::compileShaders` の `perfCountsSec` | メンバ変数に昇格すれば浮動小数点除算を 1 回節約できるが、シェーダーコンパイル時のみの cold パス |
