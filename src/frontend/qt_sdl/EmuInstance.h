/*
    Copyright 2016-2026 melonDS team

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

#ifndef EMUINSTANCE_H
#define EMUINSTANCE_H

#include <SDL2/SDL.h>

#include "Platform.h"
#include "main.h"
#include "NDS.h"
#include "EmuThread.h"
#include "Window.h"
#include "Config.h"
#include "SaveManager.h"
#ifdef MELONPRIME_DS
#include <atomic>
#include <cstdint>
#include "MelonPrimeMouseButton.h"
#include "MelonPrimeJoystickDevice.h"
namespace MelonPrime { class MelonPrimeCore; }
#endif // MELONPRIME_DS

const int kMaxWindows = 4;

enum
{
    HK_Lid = 0,
    HK_Mic,
    HK_Pause,
    HK_Reset,
    HK_FastForward,
    HK_FrameLimitToggle,
    HK_FullscreenToggle,
    HK_SwapScreens,
    HK_SwapScreenEmphasis,
    HK_SolarSensorDecrease,
    HK_SolarSensorIncrease,
    HK_FrameStep,
    HK_PowerButton,
    HK_VolumeUp,
    HK_VolumeDown,
    HK_AudioMuteToggle,
    HK_SlowMo,
    HK_FastForwardToggle,
    HK_SlowMoToggle,
    HK_GuitarGripGreen,
    HK_GuitarGripRed,
    HK_GuitarGripYellow,
    HK_GuitarGripBlue,

#ifdef MELONPRIME_DS
    HK_MetroidMoveForward,
    HK_MetroidMoveBack,
    HK_MetroidMoveLeft,
    HK_MetroidMoveRight,
    HK_MetroidJump,
    HK_MetroidMorphBall,
    HK_MetroidZoom,
    HK_MetroidHoldMorphBallBoost,
    HK_MetroidScanVisor,
    HK_MetroidUILeft,
    HK_MetroidUIRight,
    HK_MetroidUIOk,
    HK_MetroidUIYes,
    HK_MetroidUINo,
    HK_MetroidShootScan,
    HK_MetroidScanShoot,
    HK_MetroidWeaponBeam,
    HK_MetroidWeaponMissile,
    HK_MetroidWeaponSpecial,
    HK_MetroidWeaponNext,
    HK_MetroidWeaponPrevious,
    HK_MetroidWeapon1,
    HK_MetroidWeapon2,
    HK_MetroidWeapon3,
    HK_MetroidWeapon4,
    HK_MetroidWeapon5,
    HK_MetroidWeapon6,
    HK_MetroidWeaponCheck,
    HK_MetroidMenu,
    HK_MetroidIngameSensiUp,
    HK_MetroidIngameSensiDown,
    // Secondary next/prev aliases (OR'd into IB_WEAPON_NEXT/PREV at project time).
    // Appended after the contiguous Beam..Previous group so InputProjection bit
    // packing stays valid.
    HK_MetroidWeaponNextSecondary,
    HK_MetroidWeaponPreviousSecondary,
    // Appended to preserve every existing hotkey ID. Runtime projection selects
    // this binding only while Stylus Mode is enabled.
    HK_MetroidScanShootStylus,
    // Appended to preserve every existing hotkey ID. In Stylus Mode this
    // binding drives touch contact; outside Stylus Mode it is ignored.
    HK_MetroidStylusTouch,
#endif // MELONPRIME_DS

    // HK_MAX should be last item.
    HK_MAX
};

enum
{
    micInputType_Silence,
    micInputType_External,
    micInputType_Noise,
    micInputType_Wav,
    micInputType_MAX,
};

enum
{
    renderer3D_Software = 0,
#ifdef OGLRENDERER_ENABLED
    renderer3D_OpenGL,
    renderer3D_OpenGLCompute,
#endif
#if defined(MELONPRIME_ENABLE_METAL)
    renderer3D_Metal,
    renderer3D_MetalCompute,
#endif
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    renderer3D_Vulkan,
#endif
#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
    renderer3D_DX12,
#endif
    // MELONPRIME_METAL_COMPUTE_UI_V1
    renderer3D_Max,
};

#ifdef MELONPRIME_DS
namespace MelonPrime {

// Published by EmuThread at the cold renderer/configuration boundary. Native
// presenters consume this one immutable-at-read snapshot instead of querying
// Config::Table on every frame. `configuredRenderer` remains the user's
// requested value; `activeRenderer` is the renderer that actually survived
// construction/fallback.
struct PresentationConfigSnapshot
{
    bool vsync = false;
    int configuredRenderer = renderer3D_Software;
    int activeRenderer = renderer3D_Software;
    std::uint32_t revision = 0;
};

} // namespace MelonPrime
#endif // MELONPRIME_DS

class EmuInstance
{
#ifdef MELONPRIME_DS
    friend class MelonPrime::MelonPrimeCore;
#endif // MELONPRIME_DS

public:
    EmuInstance(int inst);
    ~EmuInstance();

#ifdef MELONPRIME_DS
    void onMousePress(QMouseEvent* event);
    void onMouseRelease(QMouseEvent* event);
    [[nodiscard]] bool hotkeyUsesKeyboardKey(int hotkeyId, int qtKey) const;
    [[nodiscard]] bool hotkeyUsesMouseButton(int hotkeyId, Qt::MouseButton button) const;
    [[nodiscard]] uint8_t mouseRecoveryEligibleMask() const noexcept
    {
        return m_mouseRecoveryEligibleMask;
    }
    void syncMouseHotkeysFromQtButtons(Qt::MouseButtons physical);
    // One-frame virtual press for MelonPrime::InputKey::MouseWheelUp/Down bindings.
    void onMouseWheel(int delta);
    [[nodiscard]] uint64_t wheelHotkeyMaskForDelta(int delta) const noexcept
    {
        return delta > 0
            ? wheelUpHotkeyMask.load(std::memory_order_acquire)
            : wheelDownHotkeyMask.load(std::memory_order_acquire);
    }
#endif // MELONPRIME_DS

    int getInstanceID() { return instanceID; }
    int getConsoleType() { return consoleType; }
    EmuThread* getEmuThread() { return emuThread; }
    melonDS::NDS* getNDS() { return nds; }

    MainWindow* getMainWindow() { return mainWindow; }
    int getNumWindows() { return numWindows; }
    MainWindow* getWindow(int id) { return windowList[id]; }

    void doOnAllWindows(std::function<void(MainWindow*)> func, int exclude = -1);
    void saveEnabledWindows();

    Config::Table& getGlobalConfig() { return globalCfg; }
    Config::Table& getLocalConfig() { return localCfg; }

    void broadcastCommand(int cmd, QVariant param = QVariant());
    void handleCommand(int cmd, QVariant& param);

    std::string instanceFileSuffix();

    void createWindow(int id = -1);
    void deleteWindow(int id, bool close);
    void deleteAllWindows();

    void osdAddMessage(unsigned int color, const char* fmt, ...);

    bool emuIsActive();
    void emuStop(melonDS::Platform::StopReason reason);

    bool usesOpenGL();
    void initOpenGL(int win);
    void deinitOpenGL(int win);
    void setVSyncGL(bool vsync);
    void makeCurrentGL();
    void releaseGL();

    void drawScreen();
#ifdef MELONPRIME_DS
    void publishPresentationConfig(
        bool vsync, int configuredRenderer, int activeRenderer) noexcept;
    [[nodiscard]] MelonPrime::PresentationConfigSnapshot
    getPresentationConfigSnapshot() const noexcept;
    void invalidateRendererOutput();
#if defined(MELONPRIME_ENABLE_VULKAN)
    void beginVulkanLowLatencyFrame(
        int reflexMode,
        bool antiLag2Enabled,
        bool normalSpeed,
        melonDS::u64 targetFrameIntervalNs,
        melonDS::u64 logicalFrameId);
    void markVulkanReflexInputSample();
    void markVulkanReflexSimulationStart();
    void markVulkanReflexSimulationEnd();
    void finishVulkanLowLatencyFrame();
#endif
#endif

    // return: empty string = setup OK, non-empty = error message
    QString verifySetup();

    bool updateConsole() noexcept;

    void enableCheats(bool enable);
    melonDS::ARCodeFile* getCheatFile();

    void romIcon(const melonDS::u8 (&data)[512],
                 const melonDS::u16 (&palette)[16],
                 melonDS::u32 (&iconRef)[32*32]);
    void animatedROMIcon(const melonDS::u8 (&data)[8][512],
                         const melonDS::u16 (&palette)[8][16],
                         const melonDS::u16 (&sequence)[64],
                         melonDS::u32 (&animatedIconRef)[64][32*32],
                         std::vector<int> &animatedSequenceRef);

    static const char* buttonNames[12];
    static const char* hotkeyNames[HK_MAX];

    void inputInit();
    void inputDeInit();
    void inputLoadConfig();
    void inputRumbleStart(melonDS::u32 len_ms);
    void inputRumbleStop();

    bool inputHotkeyDown(int id) { return hotkeyDown(id); }
    float inputMotionQuery(melonDS::Platform::MotionQueryType type);

    void setJoystick(int id);
    int getJoystickID() { return joystickID; }
#ifdef MELONPRIME_DS
    // Cold configuration APIs own the device lock and expose no SDL handle or
    // external locking protocol to the Qt mapping widgets.
    bool pollJoystickMapping(int oldMapping, const int* axesRest,
                             int& outMapping);
    void captureJoystickAxisRest(int* axesRest, int count);
#else
    SDL_Joystick* getJoystick() {
        return joystick;
    }
    std::shared_ptr<SDL_mutex> getJoyMutex() { return joyMutex; }
#endif

    void touchScreen(int x, int y);
    void releaseScreen();

    // mic start/stop control from core
    void micStart();
    void micStop();
    int micReadInput(melonDS::s16* data, int maxlength);

    // Renderer lifetime fence shared by the GUI and emulation threads.
    //
    // Owner/protected data: EmuInstance owns the NDS object and its renderer;
    // writers take this lock while replacing either. ScreenPanelNative takes
    // it while copying borrowed RendererOutput CPU pointers. It currently
    // remains held through QPainter + Software HUD composition to preserve the
    // established lifetime contract until measurement proves a safe win from
    // narrowing it.
    //
    // Allowed callers: GUI thread (console replacement and native paint) and
    // EmuThread (renderer transition). Lock order in native paint is
    // renderLock -> ScreenPanelNative::bufferLock; no caller may acquire
    // renderLock while holding bufferLock. Renderer transitions are cold;
    // native paint is presentation-cadence and instrumented by
    // NativePaintPerf in developer builds.
    QMutex renderLock;

private:
    static int lastSep(const std::string& path);
    std::string getAssetPath(bool gba, const std::string& configpath, const std::string& ext, const std::string& file);

    QString verifyDSBIOS();
    QString verifyDSiBIOS();
    QString verifyDSFirmware();
    QString verifyDSiFirmware();
    QString verifyDSiNAND(bool isoptional);

    std::string getEffectiveFirmwareSavePath();
    void initFirmwareSaveManager() noexcept;
    std::string getSavestateName(int slot);
    bool savestateExists(int slot);
    bool loadState(const std::string& filename);
    bool saveState(const std::string& filename);
    void undoStateLoad();
    void unloadCheats();
    void loadCheats();
    std::unique_ptr<melonDS::ARM9BIOSImage> loadARM9BIOS() noexcept;
    std::unique_ptr<melonDS::ARM7BIOSImage> loadARM7BIOS() noexcept;
    std::unique_ptr<melonDS::DSiBIOSImage> loadDSiARM9BIOS() noexcept;
    std::unique_ptr<melonDS::DSiBIOSImage> loadDSiARM7BIOS() noexcept;
    melonDS::Firmware generateFirmware(int type) noexcept;
    std::optional<melonDS::Firmware> loadFirmware(int type) noexcept;
    std::optional<melonDS::DSi_NAND::NANDImage> loadNAND(const std::array<melonDS::u8, melonDS::DSiBIOSSize>& arm7ibios) noexcept;
    std::optional<melonDS::FATStorageArgs> getSDCardArgs(const std::string& key) noexcept;
    std::optional<melonDS::FATStorage> loadSDCard(const std::string& key) noexcept;
    void setBatteryLevels();
    void reset();
    bool bootToMenu(QString& errorstr);
    melonDS::u32 decompressROM(const melonDS::u8* inContent, const melonDS::u32 inSize, std::unique_ptr<melonDS::u8[]>& outContent);
    void clearBackupState();
    std::pair<std::unique_ptr<melonDS::Firmware>, std::string> generateDefaultFirmware();
    bool parseMacAddress(void* data);
    void customizeFirmware(melonDS::Firmware& firmware, bool overridesettings) noexcept;

    bool loadROMData(const QStringList& filepath, std::unique_ptr<melonDS::u8[]>& filedata, melonDS::u32& filelen, std::string& basepath, std::string& romname) noexcept;
    QString getSavErrorString(std::string& filepath, bool gba);
    bool loadROM(QStringList filepath, bool reset, QString& errorstr);
    void ejectCart();
    bool cartInserted();
    QString cartLabel();

    bool loadGBAROM(QStringList filepath, QString& errorstr);
    void loadGBAAddon(int type, QString& errorstr);
    void ejectGBACart();
    bool gbaCartInserted();
    QString gbaAddonName(int addon);
    QString gbaCartLabel();

    void audioInit();
    void audioDeInit();
    void audioEnable();
    void audioDisable();
    void updateAudioMuteByWindowFocus();
    void toggleAudioMute();
    void updateFastForwardMute(bool fastForward);
    void audioSync();
    void audioUpdateSettings();

    void micOpen();
    void micClose();
    void micLoadWav(const std::string& name);
    void setupMicInputData();

    int audioGetNumSamplesOut(int outlen);
    static void audioCallback(void* data, Uint8* stream, int len);

    int micGetNumSamplesIn(int inlen);
    void micResample(melonDS::s16* inbuf, int inlen);
    static void micCallback(void* data, Uint8* stream, int len);

    void onKeyPress(QKeyEvent* event);
    void onKeyRelease(QKeyEvent* event);

#ifdef MELONPRIME_DS
    melonDS::u32 getInputMask();
#endif // MELONPRIME_DS

    void keyReleaseAll();

    // joyMutex must be held. These are the sole device lifetime writers.
    void openJoystick();
    void closeJoystick();
    void setJoystickLocked(int id);
    bool joystickButtonDown(int val);

    void inputProcess(bool guestFrameWillRun);

#ifdef MELONPRIME_DS
    // Guest-frame late poll. Global emulator hotkey edges remain owned by
    // inputProcess(); this publishes a separate MelonPrime gameplay snapshot.
    void inputRefreshJoystickState(bool commitGameplayEdges);
#endif

#ifdef MELONPRIME_DS
    bool hotkeyDown(int id) { return (hotkeyMask >> id) & 1; }
    bool hotkeyPressed(int id) { return (hotkeyPress >> id) & 1; }
    bool hotkeyReleased(int id) { return (hotkeyRelease >> id) & 1; }
#else
    bool hotkeyDown(int id) { return hotkeyMask & (1 << id); }
    bool hotkeyPressed(int id) { return hotkeyPress & (1 << id); }
    bool hotkeyReleased(int id) { return hotkeyRelease & (1 << id); }
#endif // MELONPRIME_DS

    void loadRTCData();
    void saveRTCData();
    void setDateTime();
    void syncRTC();

    bool deleting;

    int instanceID;

    EmuThread* emuThread;

    MainWindow* mainWindow;
    MainWindow* windowList[kMaxWindows];
    int numWindows;

    Config::Table globalCfg;
    Config::Table localCfg;

    int consoleType;
    melonDS::NDS* nds;

    int cartType;
    std::string baseROMDir;
    std::string baseROMName;
    std::string baseAssetName;
    bool changeCart;
    std::unique_ptr<melonDS::NDSCart::CartCommon> nextCart;

    int gbaCartType;
    std::string baseGBAROMDir;
    std::string baseGBAROMName;
    std::string baseGBAAssetName;
    bool changeGBACart;
    std::unique_ptr<melonDS::GBACart::CartCommon> nextGBACart;

    // HACK
public:
    std::unique_ptr<SaveManager> ndsSave;
    std::unique_ptr<SaveManager> gbaSave;
    std::unique_ptr<SaveManager> firmwareSave;

#ifdef MELONPRIME_DS
    std::atomic_bool doLimitFPS{true};
#else
    bool doLimitFPS;
#endif
    double curFPS;
    double targetFPS;
    double fastForwardFPS;
    double slowmoFPS;
    bool fastForwardToggled;
    bool slowmoToggled;
#ifdef MELONPRIME_DS
    std::atomic_bool doAudioSync{false};
#else
    bool doAudioSync;
#endif
private:

    std::unique_ptr<melonDS::Savestate> backupState;
    bool savestateLoaded;

    std::unique_ptr<melonDS::ARCodeFile> cheatFile;
    bool cheatsOn;

#ifdef MELONPRIME_DS
    // syncRTC() throttle: DS RTC ticks at 1Hz, so syncing to host time at
    // 60Hz is wasteful. Skip 59 of 60 calls to avoid the per-frame
    // QDateTime::currentDateTime() syscall + toml lookup + RTC write.
    int rtcSyncSkipFrames = 0;
#endif

    SDL_AudioDeviceID audioDevice;
    int audioFreq;
    int audioBufSize;
    float audioSampleFrac;
    bool audioMutedToggle;
    bool audioMutedByFastForward;
    bool audioMutedByWindowFocus;
    SDL_cond* audioSyncCond;
    SDL_mutex* audioSyncLock;

    int mpAudioMode;

    bool micStarted;

    SDL_AudioDeviceID micDevice;
    int micFreq;
    int micBufSize;
    float micSampleFrac;

    melonDS::s16 micExtBuffer[4096];
    melonDS::u32 micExtBufferWritePos;
    melonDS::u32 micExtBufferCount;

    melonDS::u32 micWavLength;
    melonDS::s16* micWavBuffer;

    melonDS::s16* micBuffer;
    melonDS::u32 micBufferLength;
    melonDS::u32 micBufferReadPos;

    SDL_mutex* micLock;

    //int audioInterp;
    int audioVolume;
    bool audioDSiVolumeSync;
    int micInputType;
    std::string micDeviceName;
    std::string micWavPath;

    int keyMapping[12];
    int joyMapping[12];
    int hkKeyMapping[HK_MAX];
    int hkJoyMapping[HK_MAX];

    int joystickID;
#ifdef MELONPRIME_DS
    MelonPrime::MelonPrimeJoystickDevice joystickDevice;
#else
    SDL_Joystick* joystick;
#endif
#ifdef MELONPRIME_DS
    // Lock-free presence hint only. SDL_Joystick* lifetime and every
    // dereference remain serialized by joyMutex.
    std::atomic_bool joystickPresent{false};
#endif
#ifndef MELONPRIME_DS
    SDL_GameController* controller;
    bool hasAccelerometer = false;
    bool hasGyroscope = false;
    bool hasRumble = false;
    bool isRumbling = false;
#endif

    std::shared_ptr<SDL_mutex> joyMutex;

#ifdef MELONPRIME_DS
    struct LateJoystickSnapshot
    {
        uint16_t inputMask = 0xFFF;
        uint64_t hotkeyHeld = 0;
        uint64_t hotkeyPressed = 0;
    };

    using JoystickSourceKind = MelonPrime::JoystickSourceKind;
    using JoystickPhysicalSource = MelonPrime::JoystickPhysicalSource;

    struct JoystickFanoutRule
    {
        uint8_t sourceIndex = 0;
        uint8_t predicate = 0;
        uint16_t inputBits = 0;
        uint64_t hotkeyBits = 0;
    };

    static constexpr int kMaxJoystickCompiledEntries = 2 * (HK_MAX + 12);

    struct JoystickBindingProgram
    {
        JoystickPhysicalSource sources[kMaxJoystickCompiledEntries]{};
        JoystickFanoutRule rules[kMaxJoystickCompiledEntries]{};
        uint8_t sourceCount = 0;
        uint8_t ruleCount = 0;
    };

    struct JoystickPhysicalSnapshot
    {
        // Deliberately has no default initializer. sampleJoystickPhysicalLocked
        // writes every element in [0, sourceCount), and fanout is asserted to
        // reference only that initialized range. This avoids a maximum-size
        // stack clear on every active-controller guest frame.
        int32_t sourceValue[kMaxJoystickCompiledEntries];
        uint8_t sourceCount;
    };

    struct JoystickProjectedState
    {
        uint16_t inputMask;
        uint64_t hotkeyMask;
    };

    struct MouseButtonBindingMask
    {
        uint16_t inputBits = 0;
        uint64_t hotkeyBits = 0;
        uint64_t globalCommandBits = 0;
        uint64_t gameplayBits = 0;
    };

    [[nodiscard]] JoystickBindingProgram compileJoystickBindingProgram() const;
    void publishJoystickBindingProgramLocked(
        const JoystickBindingProgram& program);
    void activateJoystickBindingProgramLocked();
    void rebuildMouseButtonBindingMasks();
    void resetJoystickConsumerState();
    bool consumeJoystickResetPending();
    void probeJoystickConnection();
    bool sampleJoystickPhysicalLocked(
        JoystickPhysicalSnapshot& snapshot
#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
        , Uint64* updateTicks, MelonPrime::SdlProcessTiming* processTiming
#endif
    );
    bool sampleJoystickPhysical(JoystickPhysicalSnapshot& snapshot);
    [[nodiscard]] JoystickProjectedState projectJoystickPhysicalSnapshot(
        const JoystickPhysicalSnapshot& snapshot) const;
    void projectJoystickCommandState(const JoystickProjectedState& projected);
    void projectJoystickGameplayState(
        const JoystickProjectedState& projected, bool commitGameplayEdges);
    void refreshJoystickCommandState();

    // OPT: QBitArray -> native integers.
    // QBitArray involves heap allocation, reference counting, byte-level iteration,
    // and bounds checking per operation. With only 12 input bits and ~53 hotkey bits,
    // uint16_t / uint64_t provide single-instruction bitwise ops with zero overhead.
    // Estimated saving: ~1400-2400 cyc/frame in inputProcess() alone.
    // Qt publishes keyboard/mouse state from the GUI thread while the
    // emulation thread consumes it. Keep the published masks atomic; the
    // joystick and combined masks remain emulation-thread-owned.
    std::atomic<uint16_t> keyInputMask{0xFFF};
    uint16_t inputMask;

    std::atomic<uint64_t> keyHotkeyMask{0};
    uint64_t hotkeyMask, lastHotkeyMask;
    uint64_t hotkeyPress, hotkeyRelease;
    uint64_t controllerCommandHotkeyMask = 0;
    bool controllerCommandSnapshotValid = false;
    bool controllerCommandNeedsBaseline = true;
    LateJoystickSnapshot lateJoystick{};
    uint64_t previousLateJoystickHotkeyMask = 0;
    bool lateJoystickNeedsBaseline = true;
    // GUI/config/device-lifetime writers publish only a reset request. The
    // EmuThread remains the sole writer of every gameplay-derived mask above.
    std::atomic_bool joystickGameplayResetPending{false};
    uint8_t joystickLifecycleCheckCounter = 0;
    // Config/UI writes and EmuThread activation are serialized by the
    // per-instance device mutex; active is immutable throughout sampling and
    // lock-free projection.
    JoystickBindingProgram pendingJoystickBindingProgram{};
    JoystickBindingProgram activeJoystickBindingProgram{};
    uint32_t joystickBindingProgramGeneration = 0;
    uint32_t activeJoystickBindingProgramGeneration = 0;
    MouseButtonBindingMask mouseButtonMasks[
        MelonPrime::kSupportedMouseButtonCount]{};
    // Cold config projection: only buttons mapped to a DS input or hotkey
    // need macOS lost-release recovery during mouse movement.
    uint8_t m_mouseRecoveryEligibleMask = 0;
    static constexpr uint64_t kGlobalCommandHotkeyMask =
        (1ULL << HK_GuitarGripGreen) - 1ULL;
    static constexpr uint64_t kGameplayHotkeyMask =
        ~((1ULL << HK_MetroidMoveForward) - 1ULL);
    // Global command and gameplay presses have different consumers. A Pause
    // tap may complete between outer polls and must still be claimed once.
    std::atomic<uint64_t> qtGlobalCommandPressPending{0};
    std::atomic<uint64_t> qtGameplayPressPending{0};
    // Wheel is an impulse, not a held Qt key. Preserve one inputProcess()
    // level for down-state consumers without writing keyHotkeyMask.
    std::atomic<uint64_t> qtWheelLevelPulsePending{0};
    // Cold inputLoadConfig() projection. Both the Qt producer and the Windows
    // Raw Input consumer avoid scanning HK_MAX on every wheel pulse.
    std::atomic<uint64_t> wheelUpHotkeyMask{0};
    std::atomic<uint64_t> wheelDownHotkeyMask{0};

    // Packed acquire/release publication keeps VSync, configured renderer,
    // actual renderer, and their revision coherent with one atomic load in
    // the presenter hot path. Bits: 0=VSync, 8..15=configured, 16..23=active,
    // 32..63=revision.
    std::atomic<std::uint64_t> presentationConfigBits{0};
#else
    melonDS::u32 keyInputMask, joyInputMask;
    melonDS::u32 keyHotkeyMask, joyHotkeyMask;
    melonDS::u32 hotkeyMask, lastHotkeyMask;
    melonDS::u32 hotkeyPress, hotkeyRelease;

    melonDS::u32 inputMask;
#endif // MELONPRIME_DS

    bool isTouching;
    melonDS::u16 touchX, touchY;

    friend class EmuThread;
    friend class MainWindow;
};

#endif //EMUINSTANCE_H
