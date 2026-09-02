include(CMakeDependentOption)

include(FixInterfaceIncludes)

set(MELONPRIME_WINDOWS_ONLY_SOURCES
    MelonPrimeRawInputWinFilter.cpp
    MelonPrimeRawHotkeyVkMapping.cpp
    MelonPrimeRawHotkeyVkBinding.cpp
    MelonPrimeRawInputState.cpp
    MelonPrimeRawWinInternal.cpp
)

set(MELONPRIME_MACOS_ONLY_SOURCES
    MelonPrimeRawInputMacFilter.mm
)

set(MELONPRIME_LINUX_ONLY_SOURCES
    MelonPrimeRawInputLinuxFilter.cpp
)

set(SOURCES_QT_SDL
    main.cpp
    main_shaders.h
    Screen.cpp
    Window.cpp
    EmuInstance.cpp
    EmuInstanceAudio.cpp
    EmuInstanceInput.cpp
    MelonPrimeJoystickDevice.cpp
    EmuThread.cpp
    CheatImportDialog.cpp
    CheatsDialog.cpp
    Config.cpp
    DateTimeDialog.cpp
    EmuSettingsDialog.cpp
    PowerManagement/PowerManagementDialog.cpp
    PowerManagement/resources/battery.qrc
    InputConfig/InputConfigDialog.cpp
    InputConfig/MapButton.h
    InputConfig/resources/ds.qrc
    VideoSettingsDialog.cpp
    CameraSettingsDialog.cpp
    AudioSettingsDialog.cpp
    FirmwareSettingsDialog.cpp
    PathSettingsDialog.cpp
    MPSettingsDialog.cpp
    WifiSettingsDialog.cpp
    InterfaceSettingsDialog.cpp
    ROMInfoDialog.cpp
    RAMInfoDialog.cpp
    TitleManagerDialog.cpp
    OSD_shaders.h
    font.h
    Platform.cpp
    Platform_AAC.cpp
    QPathInput.h
    SaveManager.cpp
    CameraManager.cpp
    AboutDialog.cpp
    AboutDialog.h
    AboutDialog.ui
    ARCodeQDecl.h

    ArchiveUtil.h
    ArchiveUtil.cpp

    ../ScreenLayout.cpp
    ../mic_blow.h

    ../glad/glad.c
    ../duckstation/gl/context.cpp

    ${CMAKE_SOURCE_DIR}/res/melon.qrc

    CLI.h
    CLI.cpp

    LANDialog.cpp
    NetplayDialog.cpp
    #rawinput/rawinput_common.c
    #RawInputThread.cpp
    ${MELONPRIME_WINDOWS_ONLY_SOURCES}
    ${MELONPRIME_MACOS_ONLY_SOURCES}
    ${MELONPRIME_LINUX_ONLY_SOURCES}

    MelonPrimeGameRomDetect.cpp
    MelonPrimeInGame.cpp
    MelonPrimeGameInput.cpp
    MelonPrimeGameSettings.cpp
    MelonPrimeGameWeapon.cpp
    MelonPrimeDamageNotifyPurple.cpp

    InputConfig/MelonPrimeInputConfig.cpp
    InputConfig/MelonPrimeInputConfigConfig.cpp
    InputConfig/MelonPrimeInputConfigPreview.cpp

    MelonPrime.cpp
    MelonPrimeLifecycle.cpp
    MelonPrimeInstanceDiagnostics.cpp
    MelonPrimeRuntimeConfig.cpp
    MelonPrimeVideoBackend.cpp
    MelonPrimeScreenCursorPolicy.cpp
    MelonPrimeHudEditorFormLayout.cpp
    MelonPrimeHudEditorFormBuilder.cpp
    MelonPrimePatchLifecycle.cpp
    MelonPrimeLocalization.cpp
    MelonPrimeLocalization/MelonPrimeLanguageRegistry.cpp
    MelonPrimeLocalization/MelonPrimeTranslationCatalog.cpp
    MelonPrimeLocalization/MelonPrimeTranslationDynamic.cpp
    MelonPrimeLocalization/MelonPrimeWidgetLocalizer.cpp
    MelonPrimeLocalization/MelonPrimeSplashLocalization.cpp
    MelonPrimeHudRender.cpp
    MelonPrimeColorDialogPrefs.cpp
    MelonPrimeHudConfigOnScreenEdit.cpp
    MelonPrimePatchAspectRatio.cpp
    MelonPrimePatchOsdColor.cpp
    MelonPrimePatchNoHud.cpp
    MelonPrimePatchNoDoubleTapJump.cpp
    MelonPrimePatchFixWifi.cpp
    MelonPrimeWifiReconnectFix.cpp
    MelonPrimePatchUseFirmwareLanguage.cpp
    MelonPrimePatchShowHeadshotOnline.cpp
    MelonPrimePatchShowEnemyHpMeterOnline.cpp
    MelonPrimePatchDisableDoubleDamageMultiplier.cpp
    MelonPrimePatchNoPickingUpSpecificItems.cpp
    MelonPrimePatchTouchScreenAimOnly.cpp
    MelonPrimePatchShadowFreezeRuntimeHook.cpp
    MelonPrimePatchFixNoxusBladePersistence.cpp
    MelonPrimePatchFpsCameraLock.cpp
    MelonPrimePatchExpandStageMatrix.cpp
    MelonPrimePatchLowHpWarning.cpp
    MelonPrimePatchAimSmoothing.cpp
    MelonPrimePatchRegistry.cpp
    MelonPrimeArm9Hook.cpp

)

# All three implementation shards include ui_MelonPrimeInputConfig.h.  On
# parallel MinGW incremental builds AUTOUIC can refresh that generated header
# after Make has already judged the objects up to date, leaving old UI source
# text paired with a newly compiled localization catalog.  Depend on the .ui
# input directly so text, styles, and layout changes always rebuild the users.
set_source_files_properties(
    InputConfig/MelonPrimeInputConfig.cpp
    InputConfig/MelonPrimeInputConfigConfig.cpp
    InputConfig/MelonPrimeInputConfigPreview.cpp
    PROPERTIES OBJECT_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/InputConfig/MelonPrimeInputConfig.ui")

option(USE_QT6 "Use Qt 6 instead of Qt 5" ON)
option(MELONPRIME_REQUIRE_QT6_5
    "Fail configure unless the selected Qt is Qt 6.5 or newer"
    OFF)
option(MELONPRIME_ENABLE_DEVELOPER_FEATURES "Enable MelonPrime developer-only settings and patches" OFF)

option(MELONPRIME_ENABLE_RAW_INPUT_PERF_TELEMETRY
    "Compile Windows Raw Input performance telemetry"
    OFF)

option(MELONPRIME_ENABLE_INPUT_DEBUG_TELEMETRY
    "Compile platform input debug telemetry"
    OFF)

if (MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    list(APPEND SOURCES_QT_SDL
        ${CMAKE_SOURCE_DIR}/tools/testing/gpu2d-native-recorder-purity.cpp)
endif()

# Renderer performance telemetry is a separate, shared compile-time gate. It
# is intentionally independent of developer features: a measurement build can
# enable only the renderer instrumentation, while every normal/release build
# compiles the observer out of both the core and Qt/SDL targets.
option(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY
    "Compile Vulkan/DX12 renderer performance telemetry instrumentation"
    OFF)

# Detailed Vulkan allocation telemetry is diagnostic-only. Memory safety uses
# a separate minimal reservation counter in the admission state, so ordinary
# Release/shipping allocations do not update peak/total/bucket statistics.
option(MELONPRIME_ENABLE_GPU_MEMORY_TELEMETRY
    "Compile detailed Vulkan GPU memory allocation telemetry"
    OFF)

function(melonprime_apply_renderer_perf_telemetry_definition target)
    if (MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
        target_compile_definitions(${target} PRIVATE
            MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY=1)
    endif()
endfunction()

# Per-frame CSV capture for Vulkan present-pacing A/B measurement.
#
# Deliberately separate from MELONPRIME_ENABLE_DEVELOPER_FEATURES. Developer
# features make the pacer request every present stage the surface supports,
# which changes driver work and timing-queue pressure -- so a developer build is
# not a valid thing to measure a shipping build's latency with. This option adds
# logging only and must never alter presentation behaviour.
option(MELONPRIME_ENABLE_VULKAN_LATENCY_CAPTURE
    "Record per-frame Vulkan present pacing timings to CSV for A/B measurement"
    OFF)

# --- MelonPrime Vulkan backend build switch ---------------------------------
# This is a complete build gate. When inactive, no Vulkan source, include path,
# compile definition, or loader dependency is added to either target.
option(MELONPRIME_ENABLE_VULKAN
    "Build the MelonPrime Vulkan renderer and native presenter"
    ON)
option(MELONPRIME_FORCE_DISABLE_VULKAN
    "Completely exclude all MelonPrime Vulkan code"
    OFF)
option(MELONPRIME_BUNDLE_MOLTENVK
    "Bundle the MoltenVK runtime into macOS Vulkan app bundles"
    ON)
set(MELONPRIME_MOLTENVK_DYLIB "" CACHE FILEPATH
    "Path to the MoltenVK runtime dylib embedded in macOS Vulkan builds")
set(MELONPRIME_MOLTENVK_LICENSE "" CACHE FILEPATH
    "Path to the MoltenVK license file copied into macOS app-bundle notices")

set(MELONPRIME_VULKAN_ACTIVE OFF)
if (MELONPRIME_FORCE_DISABLE_VULKAN)
    set(MELONPRIME_VULKAN_ACTIVE OFF)
elseif (MELONPRIME_ENABLE_VULKAN)
    # Only the headers are needed at build time: the loader (or MoltenVK on
    # macOS) is opened with dlopen()/LoadLibrary() at runtime by
    # VulkanDispatch, so no import library is linked here.
    #
    # HINTS cover the LunarG SDK on every platform, Homebrew's
    # vulkan-headers on macOS, and the package prefixes used by the BSD CI
    # images. CMake does not search all of those prefixes by default.
    find_path(MELONPRIME_VULKAN_INCLUDE_DIR
        NAMES vulkan/vulkan.h
        HINTS
            ENV VULKAN_SDK
            /opt/homebrew
            /usr/local
            /usr/pkg
            /usr/X11R6
        PATH_SUFFIXES include
        DOC "Directory containing the Vulkan headers")
    if (MELONPRIME_VULKAN_INCLUDE_DIR)
        set(MELONPRIME_VULKAN_ACTIVE ON)
    else()
        message(WARNING
            "Vulkan headers were not found; MelonPrime Vulkan is excluded from this build")
    endif()
endif()

# --- MelonPrime native Metal backend build switch ----------------------------
# MELONPRIME_METAL_DEFAULT_ON_MAC_V1
# Native Metal is part of normal macOS builds. Both disable paths below are
# hard build gates: when inactive, Metal sources, compile definitions and
# Metal/QuartzCore framework links are all excluded by MELONPRIME_METAL_ACTIVE.
set(_MELONPRIME_ENABLE_METAL_DEFAULT OFF)
if (APPLE)
    set(_MELONPRIME_ENABLE_METAL_DEFAULT ON)
endif()
option(MELONPRIME_ENABLE_METAL
    "Build MelonPrime native Metal renderers on macOS"
    ${_MELONPRIME_ENABLE_METAL_DEFAULT})
unset(_MELONPRIME_ENABLE_METAL_DEFAULT)

option(MELONPRIME_FORCE_DISABLE_METAL
    "Completely exclude all MelonPrime Metal code and framework links"
    OFF)

set(MELONPRIME_METAL_ACTIVE OFF)
if (MELONPRIME_FORCE_DISABLE_METAL)
    set(MELONPRIME_METAL_ACTIVE OFF)
elseif (MELONPRIME_ENABLE_METAL)
    if (NOT APPLE)
        message(FATAL_ERROR "MELONPRIME_ENABLE_METAL is only supported on Apple platforms")
    endif()
    set(MELONPRIME_METAL_ACTIVE ON)
endif()

# --- MelonPrime DirectX 12 backend build switch ------------------------------
# Windows-only, and a hard build gate like the Vulkan/Metal switches: when
# inactive, no DX12 source, compile definition or library is added to either
# target, so the OpenGL/Software/Vulkan/Metal paths are untouched and non-Windows
# builds never see any of it.
#
# d3d12.dll / dxgi.dll / d3dcompiler_47.dll are opened with LoadLibrary() at
# runtime by DX12Context, so only the SDK headers are needed at build time.
set(_MELONPRIME_ENABLE_DX12_DEFAULT OFF)
if (WIN32)
    set(_MELONPRIME_ENABLE_DX12_DEFAULT ON)
endif()
option(MELONPRIME_ENABLE_DX12
    "Build the MelonPrime DirectX 12 renderer"
    ${_MELONPRIME_ENABLE_DX12_DEFAULT})
unset(_MELONPRIME_ENABLE_DX12_DEFAULT)

option(MELONPRIME_FORCE_DISABLE_DX12
    "Completely exclude all MelonPrime DirectX 12 code"
    OFF)

set(MELONPRIME_DX12_ACTIVE OFF)
if (MELONPRIME_FORCE_DISABLE_DX12)
    set(MELONPRIME_DX12_ACTIVE OFF)
elseif (MELONPRIME_ENABLE_DX12)
    if (NOT WIN32)
        message(FATAL_ERROR "MELONPRIME_ENABLE_DX12 is only supported on Windows")
    endif()

    include(CheckIncludeFileCXX)
    check_include_file_cxx(d3d12.h MELONPRIME_HAVE_D3D12_H)
    check_include_file_cxx(dxgi1_6.h MELONPRIME_HAVE_DXGI1_6_H)
    check_include_file_cxx(d3dcompiler.h MELONPRIME_HAVE_D3DCOMPILER_H)

    if (MELONPRIME_HAVE_D3D12_H AND MELONPRIME_HAVE_DXGI1_6_H AND MELONPRIME_HAVE_D3DCOMPILER_H)
        set(MELONPRIME_DX12_ACTIVE ON)
    else()
        message(WARNING
            "Direct3D 12 headers were not found; MelonPrime DX12 is excluded from this build")
    endif()
endif()

if (USE_QT6)
    find_package(Qt6 COMPONENTS Core Gui Widgets Network Multimedia OpenGL OpenGLWidgets Svg REQUIRED)
    set(_melonprime_qt6_version "${Qt6_VERSION}")
    if (NOT _melonprime_qt6_version)
        set(_melonprime_qt6_version "${Qt6Core_VERSION}")
    endif()
    message(STATUS "MelonPrime Qt6 version: ${_melonprime_qt6_version}")
    if (MELONPRIME_REQUIRE_QT6_5)
        if (NOT _melonprime_qt6_version)
            message(FATAL_ERROR
                "MELONPRIME_REQUIRE_QT6_5 is ON, but CMake did not report a Qt6 version")
        endif()
        if (_melonprime_qt6_version VERSION_LESS "6.5.0")
            message(FATAL_ERROR
                "MELONPRIME_REQUIRE_QT6_5 is ON, but Qt ${_melonprime_qt6_version} was found; Qt 6.5+ is required")
        endif()
    endif()
    if (WIN32 AND MINGW AND TARGET WrapRt::WrapRt)
        # Qt's FindWrapRt can pick up MSYS /usr/lib/librt.a during a MinGW
        # configure, but Windows targets do not need librt and cannot link it.
        set_property(TARGET WrapRt::WrapRt PROPERTY INTERFACE_LINK_LIBRARIES "")
    endif()
    set(QT_LINK_LIBS Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Multimedia Qt6::OpenGL Qt6::OpenGLWidgets)

    if (NOT WIN32 AND NOT APPLE)
        # Screen.cpp needs the private QPA qplatformnativeinterface.h header:
        # unconditionally for X11 on Qt < 6.5, and for native Wayland wl_surface
        # access (MelonPrime's Wayland pointer lock / GL context creation) on any
        # Qt6 version when the Wayland EGL backend is enabled. Some distro Qt6
        # packages (e.g. Arch/CachyOS) only expose that header under a
        # version-suffixed private include path that isn't reachable without
        # this target, even though the header file itself is installed.
        find_package(Qt6GuiPrivate QUIET)
        if (TARGET Qt6::GuiPrivate)
            list(APPEND QT_LINK_LIBS Qt6::GuiPrivate)
        endif()
    endif()
else()
    if (MELONPRIME_REQUIRE_QT6_5)
        message(FATAL_ERROR
            "MELONPRIME_REQUIRE_QT6_5 is ON, but USE_QT6 is OFF; Qt 6.5+ is required")
    endif()
    find_package(Qt5 COMPONENTS Core Gui Widgets Network Multimedia Svg REQUIRED)
    set(QT_LINK_LIBS Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network Qt5::Multimedia)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

if (BUILD_STATIC)
    set(PKG_CONFIG_ARGN "--static")
endif()

# MSYS2 pkg-config can shadow vcpkg and return a libarchive that omits zlib
# from the final link line on static MinGW builds (CI: build-windows.yml).
if (USE_VCPKG AND WIN32 AND MINGW)
    set(_vcpkg_pc "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/lib/pkgconfig")
    if (IS_DIRECTORY "${_vcpkg_pc}")
        set(ENV{PKG_CONFIG_PATH} "${_vcpkg_pc}${CMAKE_LIST_SEPARATOR}$ENV{PKG_CONFIG_PATH}")
    endif()
    set(_vcpkg_pkgconf "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/tools/pkgconf/pkgconf.exe")
    if (EXISTS "${_vcpkg_pkgconf}")
        set(PKG_CONFIG_EXECUTABLE "${_vcpkg_pkgconf}" CACHE FILEPATH "pkg-config executable" FORCE)
    endif()
endif()

# CMake's FindPkgConfig resolves the `-lm` dependency from the static SDL2
# and faad2 pkg-config files through find_library().  In an MSYS2 shell that
# can select MSYS's incompatible /usr/lib/libm.a even though the compiler is
# MinGW. Seed both cache entries from the active MinGW toolchain before the
# imported pkg-config targets are created.
if (WIN32 AND MINGW)
    get_filename_component(_melonprime_mingw_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_melonprime_mingw_root "${_melonprime_mingw_bin}" DIRECTORY)
    set(_melonprime_mingw_libm "${_melonprime_mingw_root}/lib/libm.a")
    if (EXISTS "${_melonprime_mingw_libm}")
        set(pkgcfg_lib_SDL2_m "${_melonprime_mingw_libm}" CACHE FILEPATH
            "MinGW math library used by SDL2" FORCE)
        set(pkgcfg_lib_Faad_m "${_melonprime_mingw_libm}" CACHE FILEPATH
            "MinGW math library used by faad2" FORCE)
    endif()
    unset(_melonprime_mingw_libm)
    unset(_melonprime_mingw_root)
    unset(_melonprime_mingw_bin)
endif()

pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(LibArchive REQUIRED IMPORTED_TARGET libarchive)
pkg_check_modules(Zstd REQUIRED IMPORTED_TARGET libzstd)
pkg_check_modules(Faad REQUIRED IMPORTED_TARGET faad2)
if (WIN32 AND MINGW AND BUILD_STATIC AND USE_VCPKG)
    pkg_check_modules(LibZ REQUIRED IMPORTED_TARGET zlib)
endif()

fix_interface_includes(PkgConfig::SDL2 PkgConfig::LibArchive)

add_compile_definitions(ARCHIVE_SUPPORT_ENABLED)
add_compile_definitions(MELONPRIME_DS) # MelonPrimeDS
add_compile_definitions(MELONPRIME_CUSTOM_HUD ) # MelonPrimeDS

if (WIN32)
    # No Windows RawInput source exclusions are needed here.
    list(REMOVE_ITEM SOURCES_QT_SDL
        ${MELONPRIME_MACOS_ONLY_SOURCES}
        ${MELONPRIME_LINUX_ONLY_SOURCES}
    )
elseif (APPLE)
    # Disable the Windows-only raw input sources on macOS.
    list(REMOVE_ITEM SOURCES_QT_SDL
        #rawinput/rawinput_common.c
        #RawInputThread.cpp
        ${MELONPRIME_WINDOWS_ONLY_SOURCES}
        ${MELONPRIME_LINUX_ONLY_SOURCES}
    )
else()
    # Linux has its own XInput2 RawMotion path. Disable the Windows and macOS
    # native input sources; Wayland/non-X11 falls back to the Qt/QCursor path.
    list(REMOVE_ITEM SOURCES_QT_SDL
        #rawinput/rawinput_common.c
        #RawInputThread.cpp
        ${MELONPRIME_WINDOWS_ONLY_SOURCES}
        ${MELONPRIME_MACOS_ONLY_SOURCES}
    )
endif()

add_executable(melonDS ${SOURCES_QT_SDL})

# Geometry-level regression coverage for the production translated-label
# helper and the same adaptive panel-width arithmetic used by Screen.cpp.
# This is a Qt Widgets test, so it catches overlap/clipping that a source audit
# cannot see. The custom target runs it with Qt's headless platform plugin.
add_executable(melonprime_hud_classic_layout_tests EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/tools/testing/hud-classic-layout-tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MelonPrimeHudEditorFormLayout.cpp)
target_include_directories(melonprime_hud_classic_layout_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(melonprime_hud_classic_layout_tests PRIVATE
    MELONPRIME_DS
    MELONPRIME_CUSTOM_HUD)
target_link_libraries(melonprime_hud_classic_layout_tests PRIVATE ${QT_LINK_LIBS})
set_target_properties(melonprime_hud_classic_layout_tests PROPERTIES
    WIN32_EXECUTABLE OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
if (WIN32)
    # The static Windows Qt package exposes qwindows but does not ship the
    # offscreen platform plugin. The test never shows an interactive window,
    # so the normal Windows platform plugin is sufficient on the CI runner.
    set(MELONPRIME_HUD_QPA_PLATFORM windows)
elseif (APPLE)
    # The macOS Qt package uses the native Cocoa integration on the hosted
    # runner; selecting it avoids depending on an optional offscreen plugin.
    set(MELONPRIME_HUD_QPA_PLATFORM cocoa)
else()
    set(MELONPRIME_HUD_QPA_PLATFORM offscreen)
endif()
add_custom_target(melonprime_hud_classic_layout_check ALL
    COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=${MELONPRIME_HUD_QPA_PLATFORM}
        $<TARGET_FILE:melonprime_hud_classic_layout_tests>
    DEPENDS melonprime_hud_classic_layout_tests
    COMMENT "Running Classic On-Screen Edit Qt geometry tests..."
    VERBATIM)

if (WIN32)
    # Exercise the production Qt-identity -> VK compiler directly. Source
    # token audits cannot prove that left/right modifier identities remain
    # disjoint at runtime.
    add_executable(melonprime_raw_hotkey_vk_mapping_tests EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tools/testing/raw-hotkey-vk-mapping-tests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/MelonPrimeRawHotkeyVkMapping.cpp)
    target_include_directories(melonprime_raw_hotkey_vk_mapping_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../..)
    target_compile_definitions(melonprime_raw_hotkey_vk_mapping_tests PRIVATE
        MELONPRIME_DS)
    target_link_libraries(melonprime_raw_hotkey_vk_mapping_tests PRIVATE ${QT_LINK_LIBS})
    set_target_properties(melonprime_raw_hotkey_vk_mapping_tests PROPERTIES
        WIN32_EXECUTABLE OFF
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    add_custom_target(melonprime_raw_hotkey_vk_mapping_check ALL
        COMMAND $<TARGET_FILE:melonprime_raw_hotkey_vk_mapping_tests>
        DEPENDS melonprime_raw_hotkey_vk_mapping_tests
        COMMENT "Running Windows Raw hotkey VK mapping tests..."
        VERBATIM)

    # Exercise the production HiddenWndProc recovery classifier with a fake
    # GetRawInputData boundary. This keeps pure-motion and wheel-only dispatch
    # off the recovery mailbox while retaining failure and stateful-event
    # recovery without requiring a real device or message queue.
    add_executable(melonprime_raw_recovery_hint_tests EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tools/testing/raw-recovery-hint-tests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/MelonPrimeRawInputState.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/MelonPrimeRawWinInternal.cpp)
    target_include_directories(melonprime_raw_recovery_hint_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../..)
    target_compile_definitions(melonprime_raw_recovery_hint_tests PRIVATE
        MELONPRIME_DS)
    target_link_libraries(melonprime_raw_recovery_hint_tests PRIVATE ${QT_LINK_LIBS})
    set_target_properties(melonprime_raw_recovery_hint_tests PROPERTIES
        WIN32_EXECUTABLE OFF
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    add_custom_target(melonprime_raw_recovery_hint_check ALL
        COMMAND $<TARGET_FILE:melonprime_raw_recovery_hint_tests>
        DEPENDS melonprime_raw_recovery_hint_tests
        COMMENT "Running Windows Raw recovery hint tests..."
        VERBATIM)
endif()

# MelonPrimeDS: keep the CMake target name "melonDS" (so all the target_* calls
# below stay untouched), but emit the binary as "melonPrimeDS" (melonPrimeDS.exe
# on Windows). The Apple block further down overrides OUTPUT_NAME for the bundle.
set_target_properties(melonDS PROPERTIES OUTPUT_NAME melonPrimeDS)

# MelonPrimeDS: the build timestamp shown in the title comes from the compiler's
# __DATE__/__TIME__ (build-machine local time), but C/C++ has no macro for the
# UTC offset. Compute it here from the difference between local and UTC wall
# clocks and pass it as a compile definition (e.g. "GMT+9"). The value is stable
# for a given timezone, so it does not churn incremental builds.
string(TIMESTAMP _mp_loc_h "%H")
string(TIMESTAMP _mp_loc_m "%M")
string(TIMESTAMP _mp_utc_h "%H" UTC)
string(TIMESTAMP _mp_utc_m "%M" UTC)
foreach(_v _mp_loc_h _mp_loc_m _mp_utc_h _mp_utc_m)
    string(REGEX REPLACE "^0([0-9])" "\\1" ${_v} "${${_v}}")  # strip single leading zero for math()
endforeach()
math(EXPR _mp_off_min "(${_mp_loc_h} - ${_mp_utc_h}) * 60 + (${_mp_loc_m} - ${_mp_utc_m})")
# Pick the representative offset within a day (real offsets are -12h..+14h).
if(_mp_off_min GREATER 720)
    math(EXPR _mp_off_min "${_mp_off_min} - 1440")
elseif(_mp_off_min LESS_EQUAL -720)
    math(EXPR _mp_off_min "${_mp_off_min} + 1440")
endif()
set(_mp_tz_sign "+")
if(_mp_off_min LESS 0)
    set(_mp_tz_sign "-")
    math(EXPR _mp_off_min "0 - ${_mp_off_min}")
endif()
math(EXPR _mp_tz_h "${_mp_off_min} / 60")
math(EXPR _mp_tz_m "${_mp_off_min} % 60")
if(_mp_tz_m EQUAL 0)
    set(_mp_build_tz "GMT${_mp_tz_sign}${_mp_tz_h}")
else()
    if(_mp_tz_m LESS 10)
        set(_mp_tz_m "0${_mp_tz_m}")
    endif()
    set(_mp_build_tz "GMT${_mp_tz_sign}${_mp_tz_h}:${_mp_tz_m}")
endif()
target_compile_definitions(melonDS PRIVATE MELONPRIMEDS_BUILD_TZ="${_mp_build_tz}")

if (MELONDS_GIT_HASH)
    set(_mp_git_sha "${MELONDS_GIT_HASH}")
else()
    set(_mp_git_sha "unknown")
endif()
if (MELONDS_GIT_BRANCH)
    set(_mp_git_branch "${MELONDS_GIT_BRANCH}")
else()
    set(_mp_git_branch "unknown")
endif()
if (MELONDS_BUILD_PROVIDER)
    set(_mp_build_provider "${MELONDS_BUILD_PROVIDER}")
else()
    set(_mp_build_provider "unknown")
endif()
if (CMAKE_BUILD_TYPE)
    set(_mp_build_type "${CMAKE_BUILD_TYPE}")
else()
    set(_mp_build_type "unknown")
endif()
set(_mp_renderer_perf_gate 0)
if (MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    set(_mp_renderer_perf_gate 1)
endif()
set(_mp_vulkan_latency_gate 0)
if (MELONPRIME_VULKAN_ACTIVE AND MELONPRIME_ENABLE_VULKAN_LATENCY_CAPTURE)
    set(_mp_vulkan_latency_gate 1)
endif()
set(_mp_gpu_memory_gate 0)
if (MELONPRIME_ENABLE_GPU_MEMORY_TELEMETRY)
    set(_mp_gpu_memory_gate 1)
endif()
set(_mp_developer_gate 0)
if (MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    set(_mp_developer_gate 1)
endif()
set(_mp_vulkan_backend_gate 0)
if (MELONPRIME_VULKAN_ACTIVE)
    set(_mp_vulkan_backend_gate 1)
endif()
set(_mp_dx12_backend_gate 0)
if (MELONPRIME_DX12_ACTIVE)
    set(_mp_dx12_backend_gate 1)
endif()
target_compile_definitions(melonDS PRIVATE
    MELONPRIMEDS_GIT_SHA="${_mp_git_sha}"
    MELONPRIMEDS_GIT_BRANCH="${_mp_git_branch}"
    MELONPRIMEDS_GIT_DIRTY="${MELONDS_GIT_DIRTY}"
    MELONPRIMEDS_BUILD_PROVIDER="${_mp_build_provider}"
    MELONPRIMEDS_BUILD_TYPE="${_mp_build_type}"
    MELONPRIMEDS_BUILD_RENDERER_PERF_TELEMETRY=${_mp_renderer_perf_gate}
    MELONPRIMEDS_BUILD_VULKAN_LATENCY_CAPTURE=${_mp_vulkan_latency_gate}
    MELONPRIMEDS_BUILD_GPU_MEMORY_TELEMETRY=${_mp_gpu_memory_gate}
    MELONPRIMEDS_BUILD_DEVELOPER_FEATURES=${_mp_developer_gate}
    MELONPRIMEDS_BUILD_VULKAN=${_mp_vulkan_backend_gate}
    MELONPRIMEDS_BUILD_DX12=${_mp_dx12_backend_gate})

if (MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    target_compile_definitions(core PRIVATE
        MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY=1)
    target_compile_definitions(melonDS PRIVATE
        MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY=1)
    message(STATUS
        "MelonPrime renderer performance telemetry: COMPILED IN. "
        "Set MELONPRIME_PERF=1 at runtime to enable collection.")
else()
    message(STATUS
        "MelonPrime renderer performance telemetry: COMPILED OUT")
endif()

if (WIN32 AND MELONPRIME_ENABLE_RAW_INPUT_PERF_TELEMETRY)
    target_compile_definitions(core PRIVATE
        MELONPRIME_ENABLE_RAW_INPUT_PERF_TELEMETRY=1)
    target_compile_definitions(melonDS PRIVATE
        MELONPRIME_ENABLE_RAW_INPUT_PERF_TELEMETRY=1)
    message(STATUS
        "MelonPrime Raw Input performance telemetry: COMPILED IN. "
        "Set MELONPRIME_RAW_INPUT_PERF=1 at runtime to enable collection.")
else()
    message(STATUS
        "MelonPrime Raw Input performance telemetry: COMPILED OUT")
endif()

if (MELONPRIME_ENABLE_INPUT_DEBUG_TELEMETRY)
    target_compile_definitions(core PRIVATE
        MELONPRIME_ENABLE_INPUT_DEBUG_TELEMETRY=1)
    target_compile_definitions(melonDS PRIVATE
        MELONPRIME_ENABLE_INPUT_DEBUG_TELEMETRY=1)
    message(STATUS
        "MelonPrime input debug telemetry: COMPILED IN. "
        "Set MELONPRIME_INPUT_DEBUG=1 at runtime to enable collection.")
else()
    message(STATUS
        "MelonPrime input debug telemetry: COMPILED OUT")
endif()

if (MELONPRIME_ENABLE_GPU_MEMORY_TELEMETRY)
    target_compile_definitions(core PRIVATE
        MELONPRIME_ENABLE_GPU_MEMORY_TELEMETRY=1)
    target_compile_definitions(melonDS PRIVATE
        MELONPRIME_ENABLE_GPU_MEMORY_TELEMETRY=1)
    message(STATUS
        "MelonPrime GPU memory telemetry: COMPILED IN (diagnostic build)")
else()
    message(STATUS
        "MelonPrime GPU memory telemetry: COMPILED OUT")
endif()

# Backend-neutral structured input upload planning. A unit wrongly considered
# clean leaves last frame's 2D content on screen with no error anywhere, so the
# extraction is checked against a transcription of the pre-refactor computation.
add_executable(melonprime_wayland_fixed_delta_tests EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/tools/testing/wayland-fixed-delta-tests.cpp)
target_include_directories(melonprime_wayland_fixed_delta_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(melonprime_wayland_fixed_delta_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
add_custom_target(melonprime_wayland_fixed_delta_check ALL
    COMMAND $<TARGET_FILE:melonprime_wayland_fixed_delta_tests>
    DEPENDS melonprime_wayland_fixed_delta_tests
    COMMENT "Running Wayland fixed-point delta parity tests..."
    VERBATIM)

# The Linux XI2 reset mailbox is a lifecycle fence, not a per-event signal.
# Keep its bounded-chunk ordering model executable on every build host; the
# model has no X11 dependency and therefore also protects cross-platform CI.
add_executable(melonprime_linux_reset_fence_tests EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/tools/testing/linux-reset-fence-tests.cpp)
set_target_properties(melonprime_linux_reset_fence_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
add_custom_target(melonprime_linux_reset_fence_check ALL
    COMMAND $<TARGET_FILE:melonprime_linux_reset_fence_tests>
    DEPENDS melonprime_linux_reset_fence_tests
    COMMENT "Running Linux XI2 reset-fence state-model tests..."
    VERBATIM)

add_executable(melonprime_structured_upload_plan_tests EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/tools/testing/structured-upload-plan-tests.cpp)
target_include_directories(melonprime_structured_upload_plan_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_compile_definitions(melonprime_structured_upload_plan_tests PRIVATE
    MELONPRIME_DS)
set_target_properties(melonprime_structured_upload_plan_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
add_custom_target(melonprime_structured_upload_plan_check ALL
    COMMAND $<TARGET_FILE:melonprime_structured_upload_plan_tests>
    DEPENDS melonprime_structured_upload_plan_tests
    COMMENT "Running structured input upload plan tests..."
    VERBATIM)

# Backend-neutral native Display Capture provenance. A wrong "yes" here hands
# the emulated program VRAM from the wrong frame rather than crashing, and both
# backends now share the rule, so it is checked directly.
add_executable(melonprime_capture_provenance_tests EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/tools/testing/capture-provenance-tests.cpp
    ${CMAKE_SOURCE_DIR}/src/CaptureProvenanceState.cpp)
target_include_directories(melonprime_capture_provenance_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_compile_definitions(melonprime_capture_provenance_tests PRIVATE
    MELONPRIME_DS)
set_target_properties(melonprime_capture_provenance_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
add_custom_target(melonprime_capture_provenance_check ALL
    COMMAND $<TARGET_FILE:melonprime_capture_provenance_tests>
    DEPENDS melonprime_capture_provenance_tests
    COMMENT "Running native capture provenance tests..."
    VERBATIM)

# Backend-neutral presentation slot ring. Both compositors publish through it,
# and a mistake in the protocol shows a stale or half-composed frame rather
# than crashing -- so the rules are checked here, without a GPU.
add_executable(melonprime_renderer_output_ring_tests EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/tools/testing/renderer-output-ring-tests.cpp
    ${CMAKE_SOURCE_DIR}/src/RendererOutputRing.cpp)
target_include_directories(melonprime_renderer_output_ring_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_compile_definitions(melonprime_renderer_output_ring_tests PRIVATE
    MELONPRIME_DS)
set_target_properties(melonprime_renderer_output_ring_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
add_custom_target(melonprime_renderer_output_ring_check ALL
    COMMAND $<TARGET_FILE:melonprime_renderer_output_ring_tests>
    DEPENDS melonprime_renderer_output_ring_tests
    COMMENT "Running renderer output ring protocol tests..."
    VERBATIM)

# Backend-neutral GPU2D frame publication policy. Not gated on developer
# features and not gated on a backend: both the Vulkan and the DX12 renderer
# run this decision tree on every frame, and the test compares it against a
# transcription of the pre-extraction inline branches over the full input
# cross-product.
add_executable(melonprime_gpu2d_frame_policy_tests EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/tools/testing/gpu2d-frame-policy-tests.cpp)
target_include_directories(melonprime_gpu2d_frame_policy_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_compile_definitions(melonprime_gpu2d_frame_policy_tests PRIVATE
    MELONPRIME_DS)
set_target_properties(melonprime_gpu2d_frame_policy_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
add_custom_target(melonprime_gpu2d_frame_policy_check ALL
    COMMAND $<TARGET_FILE:melonprime_gpu2d_frame_policy_tests>
    DEPENDS melonprime_gpu2d_frame_policy_tests
    COMMENT "Running GPU2D frame publication policy tests..."
    VERBATIM)

# Stage Matrix uses a strict loaded-state guard before writing guest RAM. The
# callback seam drives the production templated implementation from fake RAM,
# covering delayed partial loads, permanent mismatches, all seven ROM groups,
# and lifecycle invalidation without requiring a running emulator or display.
add_executable(melonprime_stage_matrix_validation_tests EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/tools/testing/stage-matrix-validation-tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MelonPrimePatchExpandStageMatrix.cpp)
target_include_directories(melonprime_stage_matrix_validation_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_compile_definitions(melonprime_stage_matrix_validation_tests PRIVATE
    MELONPRIME_DS
    MELONPRIME_STAGE_MATRIX_TESTING=1)
target_link_libraries(melonprime_stage_matrix_validation_tests PRIVATE core)
set_target_properties(melonprime_stage_matrix_validation_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
add_custom_target(melonprime_stage_matrix_validation_check ALL
    COMMAND $<TARGET_FILE:melonprime_stage_matrix_validation_tests>
    DEPENDS melonprime_stage_matrix_validation_tests
    COMMENT "Running Stage Matrix loaded-state validation tests..."
    VERBATIM)

# The high-resolution crosshair centre reproduces MPH's Q12 projection up to
# the raw Q32 quotient. The module is free of Qt and melonDS so the arithmetic
# can be checked directly against a literal transcription of the ROM chain.
add_executable(melonprime_crosshair_projection_tests EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/tools/testing/crosshair-projection-tests.cpp)
target_include_directories(melonprime_crosshair_projection_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(melonprime_crosshair_projection_tests PRIVATE
    MELONPRIME_DS
    MELONPRIME_CUSTOM_HUD)
set_target_properties(melonprime_crosshair_projection_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
add_custom_target(melonprime_crosshair_projection_check ALL
    COMMAND $<TARGET_FILE:melonprime_crosshair_projection_tests>
    DEPENDS melonprime_crosshair_projection_tests
    COMMENT "Running high-resolution crosshair projection tests..."
    VERBATIM)

if (MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    target_compile_definitions(melonDS PRIVATE MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    target_compile_definitions(core PRIVATE MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    add_executable(melonprime_structured_capture_dependency_tests EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tools/testing/structured-capture-dependency-tests.cpp)
    target_include_directories(melonprime_structured_capture_dependency_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../..)
    target_compile_definitions(melonprime_structured_capture_dependency_tests PRIVATE
        MELONPRIME_DS)
    set_target_properties(melonprime_structured_capture_dependency_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    add_custom_target(melonprime_structured_capture_dependency_check ALL
        COMMAND $<TARGET_FILE:melonprime_structured_capture_dependency_tests>
        DEPENDS melonprime_structured_capture_dependency_tests
        COMMENT "Running CaptureSidecar dependency classifier tests..."
        VERBATIM)
    # Renderer-switch stress driver. Added to the target only in a developer
    # configure, so a release build cannot contain it even by accident -- the
    # header's own #ifdef is the second line of defence, not the first.
    target_sources(melonDS PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/MelonPrimeRendererSwitchStress.cpp)
endif()

if (MELONPRIME_VULKAN_ACTIVE AND MELONPRIME_ENABLE_VULKAN_LATENCY_CAPTURE)
    # Both targets: the capture object lives in core next to the pacer, and the
    # presenter in melonDS drives its marker points.
    target_compile_definitions(melonDS PRIVATE MELONPRIME_VULKAN_LATENCY_CAPTURE=1)
    target_compile_definitions(core PRIVATE MELONPRIME_VULKAN_LATENCY_CAPTURE=1)
    message(STATUS
        "MelonPrime: Vulkan latency capture ENABLED. This build writes a per-frame "
        "CSV and is intended for A/B measurement only.")
endif()

if (MELONPRIME_VULKAN_ACTIVE)
    target_compile_definitions(melonDS PRIVATE
        MELONPRIME_ENABLE_VULKAN=1
        VK_NO_PROTOTYPES=1)
    target_compile_definitions(core PUBLIC
        MELONPRIME_ENABLE_VULKAN=1
        VK_NO_PROTOTYPES=1)
    target_compile_definitions(core PRIVATE
        $<$<CONFIG:Debug>:MELONDS_VULKAN_ENABLE_VALIDATION=1>)
    target_include_directories(melonDS PRIVATE "${MELONPRIME_VULKAN_INCLUDE_DIR}")
    target_include_directories(core PRIVATE "${MELONPRIME_VULKAN_INCLUDE_DIR}")

    # New Vulkan backend sources are added here as the clean-room rewrite lands.
    #
    # These live in src/ and belong to the core target, like the DX12 and Metal
    # backends: they are compiled into the emulator core, not the Qt frontend.
    # The Vulkan loader is never linked -- every entry point is resolved at
    # runtime by VulkanLoader -- so no library is added alongside them.
    target_sources(core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../VulkanCommon.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../VulkanLoader.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../VulkanFeatureProbe.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../VulkanContext.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../VulkanDevice.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../VulkanMemory.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../VulkanDescriptors.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../VulkanCaptureBridge.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../VulkanGpu2DComposer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../VulkanPipelineCache.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../VulkanSync.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../VulkanNvidiaReflex.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../VulkanAmdAntiLag.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../VulkanPresentPacer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../VulkanPresentLatencyCapture.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../GPU3D_TexcacheVulkan.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../GPU3D_Vulkan.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../GPU_Vulkan.cpp)

    # Pregenerated SPIR-V for the compute rasterizer. The .comp/.glsl sources
    # under GPU3D_Vulkan_shaders/ are inputs to the offline generator
    # (tools/vulkan/compile-shaders.py) and are deliberately NOT listed here:
    # the SPIR-V words are committed, so the build has no glslang dependency.
    # Blobs are split one translation unit per stage because a single header
    # would be ~8 MB that every consumer would re-parse.
    set(MELONPRIME_VULKAN_SHADER_DIR
        ${CMAKE_CURRENT_SOURCE_DIR}/../../GPU3D_Vulkan_shaders/generated)
    # Frontend-side Vulkan sources: the feature check the settings dialog and
    # the renderer factory consult, the per-platform WSI surface adapters, the
    # swapchain/present path and the Qt screen panel. These belong to the
    # melonDS (Qt frontend) target, not to core: they include QtWidgets and the
    # MelonPrime HUD/OSD surfaces, which core must never see.
    #
    # The presenter's SPIR-V lives in MelonPrimeVulkanPresentShaders/ as a
    # committed generated header (tools/vulkan/compile-present-shaders.py), so
    # the .vert/.frag sources are deliberately NOT listed as build inputs.
    target_sources(melonDS PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/MelonPrimeVulkanFeatureCheck.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/MelonPrimeVulkanSurfaceCommon.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/MelonPrimeVulkanSurfaceLifecycle.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/MelonPrimeVulkanPresenterTimeout.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/MelonPrimeVulkanLatencyController.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/MelonPrimeVulkanPresenter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/MelonPrimeScreenVulkan.cpp)

    # Execute the target-time presentation arithmetic on every Vulkan build.
    # VulkanPresentTimingModel holds no Vulkan objects on purpose, so sequence
    # accounting, baseline rebasing, staleness and overflow are validated with
    # no driver, no GPU and no window system -- the parts of target-time pacing
    # that a static audit cannot see and that runtime testing on one machine
    # would only exercise for that machine's display.
    add_executable(melonprime_vulkan_present_timing_tests EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tools/testing/vulkan-present-timing-tests.cpp)
    target_include_directories(melonprime_vulkan_present_timing_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
        "${MELONPRIME_VULKAN_INCLUDE_DIR}")
    target_compile_definitions(melonprime_vulkan_present_timing_tests PRIVATE
        MELONPRIME_DS
        MELONPRIME_ENABLE_VULKAN=1)
    melonprime_apply_renderer_perf_telemetry_definition(
        melonprime_vulkan_present_timing_tests)
    set_target_properties(melonprime_vulkan_present_timing_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    add_custom_target(melonprime_vulkan_present_timing_check ALL
        COMMAND $<TARGET_FILE:melonprime_vulkan_present_timing_tests>
        DEPENDS melonprime_vulkan_present_timing_tests
        COMMENT "Running Vulkan present timing model tests..."
        VERBATIM)

    add_executable(melonprime_vulkan_memory_admission_tests EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tools/testing/vulkan-memory-admission-tests.cpp)
    target_include_directories(melonprime_vulkan_memory_admission_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
        "${MELONPRIME_VULKAN_INCLUDE_DIR}")
    target_compile_definitions(melonprime_vulkan_memory_admission_tests PRIVATE
        MELONPRIME_DS
        MELONPRIME_ENABLE_VULKAN=1
        VK_NO_PROTOTYPES=1)
    set_target_properties(melonprime_vulkan_memory_admission_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    add_custom_target(melonprime_vulkan_memory_admission_check ALL
        COMMAND $<TARGET_FILE:melonprime_vulkan_memory_admission_tests>
        DEPENDS melonprime_vulkan_memory_admission_tests
        COMMENT "Running Vulkan memory admission model tests..."
        VERBATIM)

    add_executable(melonprime_vulkan_memory_telemetry_tests EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tools/testing/vulkan-memory-telemetry-tests.cpp)
    target_include_directories(melonprime_vulkan_memory_telemetry_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
        "${MELONPRIME_VULKAN_INCLUDE_DIR}")
    target_compile_definitions(melonprime_vulkan_memory_telemetry_tests PRIVATE
        MELONPRIME_DS
        MELONPRIME_ENABLE_VULKAN=1
        MELONPRIME_ENABLE_GPU_MEMORY_TELEMETRY=1
        VK_NO_PROTOTYPES=1)
    set_target_properties(melonprime_vulkan_memory_telemetry_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    add_custom_target(melonprime_vulkan_memory_telemetry_check ALL
        COMMAND $<TARGET_FILE:melonprime_vulkan_memory_telemetry_tests>
        DEPENDS melonprime_vulkan_memory_telemetry_tests
        COMMENT "Running Vulkan memory telemetry model tests..."
        VERBATIM)

    # Exercise the production deferred-destroy queue with a pure frame-tag
    # model and an API-level fake allocation dispatch. This fixes the
    # distinction between the next scheduler frame and the last frame that may
    # still reference a reset texcache image, and verifies the forced-OOM retry
    # frees old image/view/memory objects before the retry allocation.
    add_executable(melonprime_vulkan_frame_retire_tests EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tools/testing/vulkan-frame-retire-tests.cpp)
    target_include_directories(melonprime_vulkan_frame_retire_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
        "${MELONPRIME_VULKAN_INCLUDE_DIR}")
    target_compile_definitions(melonprime_vulkan_frame_retire_tests PRIVATE
        MELONPRIME_DS
        MELONPRIME_ENABLE_VULKAN=1
        VK_NO_PROTOTYPES=1)
    # FrameRing's test seam reads the production class layout. Keep the model
    # test's compile-time telemetry gate identical to core so measurement
    # builds cannot create an ODR/layout mismatch with the linked library.
    melonprime_apply_renderer_perf_telemetry_definition(
        melonprime_vulkan_frame_retire_tests)
    target_link_libraries(melonprime_vulkan_frame_retire_tests PRIVATE core)
    set_target_properties(melonprime_vulkan_frame_retire_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    add_custom_target(melonprime_vulkan_frame_retire_check ALL
        COMMAND $<TARGET_FILE:melonprime_vulkan_frame_retire_tests>
        DEPENDS melonprime_vulkan_frame_retire_tests
        COMMENT "Running Vulkan deferred-destroy frame-retire model tests..."
        VERBATIM)

    add_executable(melonprime_vulkan_queue_sharing_experiment_tests EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tools/testing/vulkan-queue-sharing-experiment-tests.cpp)
    target_include_directories(melonprime_vulkan_queue_sharing_experiment_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
        "${MELONPRIME_VULKAN_INCLUDE_DIR}")
    target_compile_definitions(melonprime_vulkan_queue_sharing_experiment_tests PRIVATE
        MELONPRIME_DS
        MELONPRIME_ENABLE_VULKAN=1
        VK_NO_PROTOTYPES=1)
    set_target_properties(melonprime_vulkan_queue_sharing_experiment_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    add_custom_target(melonprime_vulkan_queue_sharing_experiment_check ALL
        COMMAND $<TARGET_FILE:melonprime_vulkan_queue_sharing_experiment_tests>
        DEPENDS melonprime_vulkan_queue_sharing_experiment_tests
        COMMENT "Running Vulkan split-queue experiment policy tests..."
        VERBATIM)

    add_executable(melonprime_vulkan_linux_surface_lifecycle_tests
        ${CMAKE_SOURCE_DIR}/tools/testing/vulkan-linux-surface-lifecycle-tests.cpp)
    set_target_properties(melonprime_vulkan_linux_surface_lifecycle_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    add_custom_target(melonprime_vulkan_linux_surface_lifecycle_check ALL
        COMMAND $<TARGET_FILE:melonprime_vulkan_linux_surface_lifecycle_tests>
        DEPENDS melonprime_vulkan_linux_surface_lifecycle_tests
        COMMENT "Running Vulkan Linux surface lifecycle model tests..."
        VERBATIM)

    # Exercise the production lifecycle class directly. The Qt host remains a
    # thin Linux event adapter; this target proves the same synchronized state
    # machine used by the application, including the post-Show generation
    # handoff and the native-destruction barrier, without requiring a display.
    add_executable(melonprime_vulkan_surface_lifecycle_direct_tests
        ${CMAKE_SOURCE_DIR}/tools/testing/vulkan-linux-surface-lifecycle-direct-tests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/MelonPrimeVulkanSurfaceLifecycle.cpp)
    target_include_directories(melonprime_vulkan_surface_lifecycle_direct_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
        "${MELONPRIME_VULKAN_INCLUDE_DIR}")
    target_compile_definitions(melonprime_vulkan_surface_lifecycle_direct_tests PRIVATE
        MELONPRIME_DS
        MELONPRIME_ENABLE_VULKAN=1
        VK_NO_PROTOTYPES=1)
    target_link_libraries(melonprime_vulkan_surface_lifecycle_direct_tests PRIVATE
        Threads::Threads)
    set_target_properties(melonprime_vulkan_surface_lifecycle_direct_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    add_custom_target(melonprime_vulkan_surface_lifecycle_direct_check ALL
        COMMAND $<TARGET_FILE:melonprime_vulkan_surface_lifecycle_direct_tests>
        DEPENDS melonprime_vulkan_surface_lifecycle_direct_tests
        COMMENT "Running production Vulkan surface lifecycle tests..."
        VERBATIM)

    # Keep the acquire experiment environment contract separate from the
    # production image-fence watchdog. This is pure timeout policy code, so it
    # can be tested without a Vulkan device or a window system.
    add_executable(melonprime_vulkan_presenter_timeout_tests
        ${CMAKE_SOURCE_DIR}/tools/testing/vulkan-presenter-timeout-tests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/MelonPrimeVulkanPresenterTimeout.cpp)
    target_include_directories(melonprime_vulkan_presenter_timeout_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
        "${MELONPRIME_VULKAN_INCLUDE_DIR}")
    target_compile_definitions(melonprime_vulkan_presenter_timeout_tests PRIVATE
        MELONPRIME_DS
        MELONPRIME_ENABLE_VULKAN=1
        MELONPRIME_VULKAN_PRESENTER_TIMEOUT_TESTING=1)
    set_target_properties(melonprime_vulkan_presenter_timeout_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    add_custom_target(melonprime_vulkan_presenter_timeout_check ALL
        COMMAND $<TARGET_FILE:melonprime_vulkan_presenter_timeout_tests>
        DEPENDS melonprime_vulkan_presenter_timeout_tests
        COMMENT "Running Vulkan presenter timeout policy tests..."
        VERBATIM)

    # Execute the production pacer against an API-level fake dispatch.  The
    # test compiles VulkanPresentPacer.cpp directly so every fake call reaches
    # the same state machine as the emulator; core supplies VulkanDevice and
    # common Vulkan utility symbols without extracting a second pacer object.
    add_executable(melonprime_vulkan_present_pacer_dispatch_tests
        ${CMAKE_SOURCE_DIR}/tools/testing/vulkan-present-pacer-dispatch-tests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../VulkanPresentPacer.cpp)
    target_include_directories(melonprime_vulkan_present_pacer_dispatch_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
        "${MELONPRIME_VULKAN_INCLUDE_DIR}")
    target_compile_definitions(melonprime_vulkan_present_pacer_dispatch_tests PRIVATE
        MELONPRIME_DS
        MELONPRIME_ENABLE_VULKAN=1
        MELONPRIME_VULKAN_PRESENT_PACER_TESTING=1)
    melonprime_apply_renderer_perf_telemetry_definition(
        melonprime_vulkan_present_pacer_dispatch_tests)
    target_link_libraries(melonprime_vulkan_present_pacer_dispatch_tests PRIVATE core)
    set_target_properties(melonprime_vulkan_present_pacer_dispatch_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    add_custom_target(melonprime_vulkan_present_pacer_dispatch_check ALL
        COMMAND $<TARGET_FILE:melonprime_vulkan_present_pacer_dispatch_tests>
        DEPENDS melonprime_vulkan_present_pacer_dispatch_tests
        COMMENT "Running Vulkan present pacer fake-dispatch tests..."
        VERBATIM)

    if (CMAKE_SYSTEM_NAME MATCHES "FreeBSD|NetBSD|OpenBSD")
        # Exercise the production VulkanLoader::Library::Open() path against
        # the installed runtime. A successful file-exists probe is not enough:
        # this target must resolve the loader's global entry points through the
        # same dlopen() implementation used by melonPrimeDS.
        add_executable(melonprime_vulkan_loader_open_tests
            ${CMAKE_SOURCE_DIR}/tools/testing/vulkan-loader-open-tests.cpp)
        target_include_directories(melonprime_vulkan_loader_open_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../..
            "${MELONPRIME_VULKAN_INCLUDE_DIR}")
        target_compile_definitions(melonprime_vulkan_loader_open_tests PRIVATE
            MELONPRIME_DS
            MELONPRIME_ENABLE_VULKAN=1)
        melonprime_apply_renderer_perf_telemetry_definition(
            melonprime_vulkan_loader_open_tests)
        target_link_libraries(melonprime_vulkan_loader_open_tests PRIVATE
            core
            ${CMAKE_DL_LIBS})
        set_target_properties(melonprime_vulkan_loader_open_tests PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
        add_custom_target(melonprime_vulkan_loader_open_check
            COMMAND $<TARGET_FILE:melonprime_vulkan_loader_open_tests>
            DEPENDS melonprime_vulkan_loader_open_tests
            COMMENT "Running production Vulkan loader open check..."
            VERBATIM)
    endif()

    if (MELONPRIME_ENABLE_DEVELOPER_FEATURES)
        # Exercise the production renderer-selection code in both forced
        # success and sticky-failure states. The feature-check functions are
        # deterministic test doubles here; the process-level failure seam and
        # switch stress driver cover the real renderer/panel lifetime path.
        add_executable(melonprime_vulkan_renderer_fallback_tests
            ${CMAKE_SOURCE_DIR}/tools/testing/vulkan-renderer-fallback-tests.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/MelonPrimeVideoBackend.cpp)
        target_include_directories(melonprime_vulkan_renderer_fallback_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/..
            ${CMAKE_CURRENT_SOURCE_DIR}/../..
            ${CMAKE_CURRENT_SOURCE_DIR}/../../net
            ${SDL2_INCLUDE_DIRS}
            "${MELONPRIME_VULKAN_INCLUDE_DIR}")
        target_compile_definitions(melonprime_vulkan_renderer_fallback_tests PRIVATE
            MELONPRIME_DS
            MELONPRIME_ENABLE_VULKAN=1
            MELONPRIME_ENABLE_DEVELOPER_FEATURES=1
            OGLRENDERER_ENABLED
            SDL_MAIN_HANDLED
            VK_NO_PROTOTYPES=1)
        melonprime_apply_renderer_perf_telemetry_definition(
            melonprime_vulkan_renderer_fallback_tests)
        target_link_libraries(melonprime_vulkan_renderer_fallback_tests PRIVATE
            ${QT_LINK_LIBS})
        set_target_properties(melonprime_vulkan_renderer_fallback_tests PROPERTIES
            WIN32_EXECUTABLE OFF
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
        add_custom_target(melonprime_vulkan_renderer_fallback_check ALL
            COMMAND $<TARGET_FILE:melonprime_vulkan_renderer_fallback_tests>
            DEPENDS melonprime_vulkan_renderer_fallback_tests
            COMMENT "Running Vulkan renderer fallback regression tests..."
            VERBATIM)
    endif()

    # One platform adapter per target OS. Each is additionally guarded by its
    # own #if, so an accidental double-add cannot produce duplicate symbols.
    if (WIN32)
        target_sources(melonDS PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/MelonPrimeVulkanSurfaceWin32.cpp)
    elseif (APPLE)
        target_sources(melonDS PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/MelonPrimeVulkanSurfaceMacOS.mm)
    elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_sources(melonDS PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/MelonPrimeVulkanSurfaceLinux.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/MelonPrimeVulkanSurfaceHostLinux.cpp)
    elseif (CMAKE_SYSTEM_NAME STREQUAL "FreeBSD"
        OR CMAKE_SYSTEM_NAME STREQUAL "NetBSD"
        OR CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
        target_sources(melonDS PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/MelonPrimeVulkanSurfaceBSD.cpp)
    else()
        # Unknown Unix-like systems: no supported WSI extension in this build.
        # The stub fails loudly at runtime rather than failing to compile.
        target_sources(melonDS PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/MelonPrimeVulkanSurfaceStub.cpp)
    endif()

    target_sources(core PRIVATE
        ${MELONPRIME_VULKAN_SHADER_DIR}/GPU3D_Vulkan_ShaderModules.cpp
        ${MELONPRIME_VULKAN_SHADER_DIR}/GPU3D_Vulkan_ShaderBlobs_BinCombined.cpp
        ${MELONPRIME_VULKAN_SHADER_DIR}/GPU3D_Vulkan_ShaderBlobs_CalculateWorkOffsets.cpp
        ${MELONPRIME_VULKAN_SHADER_DIR}/GPU3D_Vulkan_ShaderBlobs_CaptureSidecar.cpp
        ${MELONPRIME_VULKAN_SHADER_DIR}/GPU3D_Vulkan_ShaderBlobs_ClearCoarseBinMask.cpp
        ${MELONPRIME_VULKAN_SHADER_DIR}/GPU3D_Vulkan_ShaderBlobs_ClearIndirectWorkCount.cpp
        ${MELONPRIME_VULKAN_SHADER_DIR}/GPU3D_Vulkan_ShaderBlobs_Compositor.cpp
        ${MELONPRIME_VULKAN_SHADER_DIR}/GPU3D_Vulkan_ShaderBlobs_CorrectCoverage.cpp
        ${MELONPRIME_VULKAN_SHADER_DIR}/GPU3D_Vulkan_ShaderBlobs_DepthBlend.cpp
        ${MELONPRIME_VULKAN_SHADER_DIR}/GPU3D_Vulkan_ShaderBlobs_FinalPass.cpp
        ${MELONPRIME_VULKAN_SHADER_DIR}/GPU3D_Vulkan_ShaderBlobs_GPU2DNative.cpp
        ${MELONPRIME_VULKAN_SHADER_DIR}/GPU3D_Vulkan_ShaderBlobs_InterpSpans.cpp
        ${MELONPRIME_VULKAN_SHADER_DIR}/GPU3D_Vulkan_ShaderBlobs_Rasterise.cpp
        ${MELONPRIME_VULKAN_SHADER_DIR}/GPU3D_Vulkan_ShaderBlobs_Resolve.cpp
        ${MELONPRIME_VULKAN_SHADER_DIR}/GPU3D_Vulkan_ShaderBlobs_SortWork.cpp)
    target_include_directories(core PRIVATE "${MELONPRIME_VULKAN_SHADER_DIR}")

    if (APPLE)
        # Vulkan on macOS is MoltenVK, which can only present to a
        # CAMetalLayer. The adapter that owns that layer is Objective-C++, so
        # it is compiled here (with ARC, like every other MelonPrime .mm) and
        # links the AppKit/QuartzCore frameworks it needs.
        #
        # This is independent of MELONPRIME_METAL_ACTIVE: the Vulkan and Metal
        # renderers coexist, each with its own screen panel, presentation
        # layer, and video setting. Building one does not require the other.
        find_library(MELONPRIME_VULKAN_QUARTZCORE_LIB QuartzCore)
        find_library(MELONPRIME_VULKAN_APPKIT_LIB AppKit)
        find_library(MELONPRIME_VULKAN_METAL_LIB Metal)
        if (NOT MELONPRIME_VULKAN_QUARTZCORE_LIB
            OR NOT MELONPRIME_VULKAN_APPKIT_LIB
            OR NOT MELONPRIME_VULKAN_METAL_LIB)
            message(FATAL_ERROR
                "MelonPrime Vulkan on macOS requires the QuartzCore, AppKit and Metal frameworks")
        endif()
        target_link_libraries(melonDS PRIVATE
            ${MELONPRIME_VULKAN_QUARTZCORE_LIB}
            ${MELONPRIME_VULKAN_APPKIT_LIB}
            ${MELONPRIME_VULKAN_METAL_LIB})
        # MelonPrimeVulkanSurfaceMacOS.mm is the CAMetalLayer adapter; it is
        # added to the target above, next to the other platform adapters. ARC is
        # required because it holds Objective-C objects.
        set_source_files_properties(
            ${CMAKE_CURRENT_SOURCE_DIR}/MelonPrimeVulkanSurfaceMacOS.mm
            PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    endif()

    message(STATUS
        "MelonPrime Vulkan backend: enabled (headers: ${MELONPRIME_VULKAN_INCLUDE_DIR})")
else()
    message(STATUS "MelonPrime Vulkan backend: disabled")
endif()

if (MELONPRIME_DX12_ACTIVE)
    target_compile_definitions(melonDS PRIVATE MELONPRIME_ENABLE_DX12=1)
    target_compile_definitions(core PUBLIC MELONPRIME_ENABLE_DX12=1)

    if (MELONPRIME_ENABLE_DEVELOPER_FEATURES)
        # Debug layer on developer builds only. GPU-based validation stays
        # gated on a debug build inside DX12Context.cpp because it costs an
        # order of magnitude in frame time.
        target_compile_definitions(core PRIVATE MELONPRIME_DX12_ENABLE_DEBUG_LAYER=1)
    endif()

    target_sources(core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../DX12RuntimeLoader.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../DX12Context.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../DX12ShaderCompiler.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../DX12ResourceFactory.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../DX12DescriptorRing.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../DX12CommandContext.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../DX12UploadRing.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../DX12AmdAntiLag2.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../DX12IntelXeLL.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../DX12LowLatencyController.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../DX12NvidiaReflex.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../DX12CaptureBridge.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../DX12Gpu2DComposer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../DX12PipelineRepository.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../GPU3D_TexcacheDX12.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../GPU3D_DX12.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../GPU_DX12.cpp)
    target_sources(melonDS PRIVATE
        MelonPrimeDX12FeatureCheck.cpp
        MelonPrimeDX12SurfacePresenter.cpp)

    # Execute the production XeLL state machine against an injected fake API
    # on every Windows DX12 build. This validates successful and failing driver
    # paths without requiring Intel Arc hardware or loading Intel's runtime.
    add_executable(melonprime_xell_state_machine_tests EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tools/testing/xell-state-machine-tests.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../DX12IntelXeLL.cpp)
    target_include_directories(melonprime_xell_state_machine_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../..)
    target_compile_definitions(melonprime_xell_state_machine_tests PRIVATE
        MELONPRIME_DS
        MELONPRIME_ENABLE_DX12=1
        MELONPRIME_ENABLE_DEVELOPER_FEATURES=1)
    set_target_properties(melonprime_xell_state_machine_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    add_custom_target(melonprime_xell_state_machine_check ALL
        COMMAND $<TARGET_FILE:melonprime_xell_state_machine_tests>
        DEPENDS melonprime_xell_state_machine_tests
        COMMENT "Running Intel XeLL fake API state-machine tests..."
        VERBATIM)

    add_executable(melonprime_dx12_memory_admission_tests EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/tools/testing/dx12-memory-admission-tests.cpp)
    target_include_directories(melonprime_dx12_memory_admission_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../..)
    target_compile_definitions(melonprime_dx12_memory_admission_tests PRIVATE
        MELONPRIME_DS
        MELONPRIME_ENABLE_DX12=1)
    set_target_properties(melonprime_dx12_memory_admission_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    add_custom_target(melonprime_dx12_memory_admission_check ALL
        COMMAND $<TARGET_FILE:melonprime_dx12_memory_admission_tests>
        DEPENDS melonprime_dx12_memory_admission_tests
        COMMENT "Running DX12 memory admission model tests..."
        VERBATIM)

    set(MELONPRIME_XELL_RUNTIME_DIR
        "${CMAKE_SOURCE_DIR}/res/third_party/intel-xell")
    set(MELONPRIME_XELL_RUNTIME
        "${MELONPRIME_XELL_RUNTIME_DIR}/libxell.dll")
    set(MELONPRIME_XELL_LICENSE
        "${MELONPRIME_XELL_RUNTIME_DIR}/LICENSE.txt")
    set(MELONPRIME_XELL_THIRD_PARTY
        "${MELONPRIME_XELL_RUNTIME_DIR}/third-party-programs.txt")
    foreach(XELL_FILE
        "${MELONPRIME_XELL_RUNTIME}"
        "${MELONPRIME_XELL_LICENSE}"
        "${MELONPRIME_XELL_THIRD_PARTY}")
        if (NOT EXISTS "${XELL_FILE}")
            message(FATAL_ERROR
                "MelonPrime Intel XeLL runtime file is missing: ${XELL_FILE}")
        endif()
    endforeach()
    file(SHA256 "${MELONPRIME_XELL_RUNTIME}" MELONPRIME_XELL_RUNTIME_SHA256)
    if (NOT MELONPRIME_XELL_RUNTIME_SHA256 STREQUAL
        "d2030dcd694fda8f2ec7e044b13e6db8f0b56d4ba9113a5efad334e3f3ded8c7")
        message(FATAL_ERROR
            "Intel XeLL runtime hash does not match pinned XeSS SDK 3.0.2")
    endif()

    add_custom_command(TARGET melonDS POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MELONPRIME_XELL_RUNTIME}"
            "$<TARGET_FILE_DIR:melonDS>/libxell.dll"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:melonDS>/ThirdPartyNotices"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MELONPRIME_XELL_LICENSE}"
            "$<TARGET_FILE_DIR:melonDS>/ThirdPartyNotices/Intel-XeSS-SDK-LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MELONPRIME_XELL_THIRD_PARTY}"
            "$<TARGET_FILE_DIR:melonDS>/ThirdPartyNotices/Intel-XeSS-SDK-third-party-programs.txt"
        COMMENT "Bundling Intel XeLL runtime and notices..."
        VERBATIM)

    # Only the GUID symbols are linked; every DX12/DXGI/D3DCompile entry point
    # is resolved with GetProcAddress at runtime.
    target_link_libraries(core PUBLIC dxguid)
    target_link_libraries(melonDS PRIVATE dxguid)

    message(STATUS "MelonPrime DirectX 12 backend: enabled")
else()
    message(STATUS "MelonPrime DirectX 12 backend: disabled")
endif()

if (MELONPRIME_METAL_ACTIVE)
    target_compile_definitions(melonDS PRIVATE MELONPRIME_ENABLE_METAL=1)
    target_compile_definitions(core PUBLIC MELONPRIME_ENABLE_METAL=1)
    target_sources(core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../GPU_Metal.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/../../GPU2D_Metal.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/../../GPU3D_Metal.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/../../GPU3D_MetalCompute.mm
    )

    # Native Metal presenter, raster renderer and Metal Compute renderer.
    # Every Metal-specific source/definition/framework remains inside this
    # MELONPRIME_METAL_ACTIVE gate so either disable option is a complete kill.
    find_library(METAL_LIB Metal)
    if (NOT METAL_LIB)
        message(FATAL_ERROR "MELONPRIME_ENABLE_METAL requires the Metal framework")
    endif()
    find_library(QUARTZCORE_LIB_METAL QuartzCore)
    if (NOT QUARTZCORE_LIB_METAL)
        message(FATAL_ERROR "MELONPRIME_ENABLE_METAL requires the QuartzCore framework")
    endif()
    target_link_libraries(melonDS PRIVATE ${METAL_LIB} ${QUARTZCORE_LIB_METAL})

    target_sources(melonDS PRIVATE
        MelonPrimeMetalFeatureCheck.mm
        MelonPrimeScreenMetal.mm
    )
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../GPU_Metal.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/../../GPU2D_Metal.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/../../GPU3D_Metal.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/../../GPU3D_MetalCompute.mm
        MelonPrimeMetalFeatureCheck.mm
        MelonPrimeScreenMetal.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
    )
endif()

add_subdirectory("../../net"
    ${CMAKE_BINARY_DIR}/net)

target_link_libraries(melonDS PRIVATE net-utils)

if (WIN32)
    target_link_libraries(melonDS PUBLIC opengl32)


    target_sources(melonDS PRIVATE
        ../duckstation/gl/context_wgl.cpp

        ../glad/glad_wgl.c
    )

    if (MINGW AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        set_property(TARGET melonDS PROPERTY AUTORCC_OPTIONS "--no-zstd")
    endif()

    if (BUILD_STATIC)
        find_package(SDL2 CONFIG COMPONENTS SDL2main)
        if (TARGET SDL2::SDL2main)
            target_link_libraries(melonDS PRIVATE SDL2::SDL2main)
        endif()
    endif()
elseif (APPLE)
    if (NOT USE_QT6)
        find_library(COCOA_LIB Cocoa)
        target_link_libraries(melonDS PRIVATE ${COCOA_LIB})
    endif()
    find_library(OPENGL_LIB OpenGL)
    target_link_libraries(melonDS PRIVATE ${OPENGL_LIB})
    # MelonPrimeDS: GCMouse/IOHID raw mouse input + CGWarp cursor recenter
    # (MelonPrimeRawInputMacFilter.mm, compiled with ARC)
    find_library(IOKIT_LIB IOKit)
    find_library(COREFOUNDATION_LIB CoreFoundation)
    find_library(COREGRAPHICS_LIB CoreGraphics)
    find_library(GAMECONTROLLER_LIB GameController)
    target_link_libraries(melonDS PRIVATE
        ${IOKIT_LIB} ${COREFOUNDATION_LIB} ${COREGRAPHICS_LIB} ${GAMECONTROLLER_LIB})
    set_source_files_properties(
        MelonPrimeRawInputMacFilter.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
    )
    target_sources(melonDS PRIVATE
        ../duckstation/gl/context_agl.mm
    )
    set_source_files_properties(
        ../duckstation/gl/context_agl.mm
        PROPERTIES COMPILE_OPTIONS "-Wno-deprecated-declarations"
    )
else()
    find_package(X11 REQUIRED COMPONENTS Xi)
    find_package(EGL REQUIRED)

    option(ENABLE_WAYLAND "Enable Wayland support" ON)

    # MelonPrimeDS Linux window/input backend policy.
    #
    # ENABLE_WAYLAND controls the optional Wayland EGL backend. X11 remains the
    # baseline Linux backend, so missing Wayland development packages must not
    # make an otherwise valid Linux build fail.
    set(MELONPRIME_WAYLAND_ACTIVE OFF)
    set(MELONPRIME_WAYLAND_POINTER_LOCK_ACTIVE OFF)

    set(MELONPRIME_WAYLAND_POINTER_LOCK "AUTO" CACHE STRING
        "Native Wayland mouse lock: AUTO, ON, or OFF")
    set_property(CACHE MELONPRIME_WAYLAND_POINTER_LOCK
        PROPERTY STRINGS AUTO ON OFF)
    string(TOUPPER "${MELONPRIME_WAYLAND_POINTER_LOCK}"
        MELONPRIME_WAYLAND_POINTER_LOCK_MODE)
    if (NOT MELONPRIME_WAYLAND_POINTER_LOCK_MODE MATCHES "^(AUTO|ON|OFF)$")
        message(FATAL_ERROR
            "MELONPRIME_WAYLAND_POINTER_LOCK must be AUTO, ON, or OFF")
    endif()

    if (ENABLE_WAYLAND)
        # PkgConfig is already a required project dependency. Using the standard
        # wayland-client .pc file avoids requiring ECM solely for Wayland.
        pkg_check_modules(WaylandClient QUIET IMPORTED_TARGET wayland-client)
        if (TARGET PkgConfig::WaylandClient)
            set(MELONPRIME_WAYLAND_ACTIVE ON)
            target_link_libraries(melonDS PRIVATE PkgConfig::WaylandClient)
            target_compile_definitions(melonDS PRIVATE WAYLAND_ENABLED)
            target_sources(melonDS PRIVATE
                ../duckstation/gl/context_egl_wayland.cpp
            )
            message(STATUS "Linux Wayland EGL backend: enabled")
        else()
            message(WARNING
                "wayland-client development files were not found; "
                "continuing with the X11 backend only. "
                "Pass -DENABLE_WAYLAND=OFF to request X11-only explicitly.")
        endif()
    else()
        message(STATUS "Linux Wayland EGL backend: disabled (X11-only build)")
    endif()

    if (MELONPRIME_WAYLAND_POINTER_LOCK_MODE STREQUAL "ON"
        AND NOT MELONPRIME_WAYLAND_ACTIVE)
        message(FATAL_ERROR
            "MELONPRIME_WAYLAND_POINTER_LOCK=ON requires ENABLE_WAYLAND and "
            "the wayland-client development package")
    endif()

    if (MELONPRIME_WAYLAND_ACTIVE
        AND NOT MELONPRIME_WAYLAND_POINTER_LOCK_MODE STREQUAL "OFF")
        # The protocol generator and XML descriptions are optional in AUTO
        # mode. ON mode intentionally fails when the explicitly requested
        # feature cannot be built.
        find_program(MELONPRIME_WAYLAND_SCANNER_EXECUTABLE
            NAMES wayland-scanner)
        pkg_get_variable(MELONPRIME_WAYLAND_PROTOCOLS_DATADIR
            wayland-protocols pkgdatadir)

        set(_mp_wayland_pointer_lock_missing "")
        if (NOT MELONPRIME_WAYLAND_SCANNER_EXECUTABLE)
            list(APPEND _mp_wayland_pointer_lock_missing "wayland-scanner")
        endif()
        if (NOT MELONPRIME_WAYLAND_PROTOCOLS_DATADIR)
            list(APPEND _mp_wayland_pointer_lock_missing "wayland-protocols")
        else()
            set(MELONPRIME_RELATIVE_POINTER_XML
                "${MELONPRIME_WAYLAND_PROTOCOLS_DATADIR}/unstable/relative-pointer/relative-pointer-unstable-v1.xml")
            set(MELONPRIME_POINTER_CONSTRAINTS_XML
                "${MELONPRIME_WAYLAND_PROTOCOLS_DATADIR}/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml")

            if (NOT EXISTS "${MELONPRIME_RELATIVE_POINTER_XML}")
                list(APPEND _mp_wayland_pointer_lock_missing
                    "relative-pointer-unstable-v1.xml")
            endif()
            if (NOT EXISTS "${MELONPRIME_POINTER_CONSTRAINTS_XML}")
                list(APPEND _mp_wayland_pointer_lock_missing
                    "pointer-constraints-unstable-v1.xml")
            endif()
        endif()

        if (_mp_wayland_pointer_lock_missing)
            list(JOIN _mp_wayland_pointer_lock_missing ", "
                _mp_wayland_pointer_lock_missing_text)
            if (MELONPRIME_WAYLAND_POINTER_LOCK_MODE STREQUAL "ON")
                message(FATAL_ERROR
                    "Native Wayland mouse lock was explicitly required, but "
                    "these dependencies are missing: "
                    "${_mp_wayland_pointer_lock_missing_text}")
            else()
                message(STATUS
                    "Native Wayland mouse lock: disabled in AUTO mode "
                    "(missing: ${_mp_wayland_pointer_lock_missing_text}); "
                    "the build will use the X11/Qt fallback path")
            endif()
        else()
            set(MELONPRIME_WAYLAND_GEN_DIR
                "${CMAKE_CURRENT_BINARY_DIR}/melonprime-wayland-protocols")
            file(MAKE_DIRECTORY "${MELONPRIME_WAYLAND_GEN_DIR}")

            set(MELONPRIME_RELATIVE_POINTER_HEADER
                "${MELONPRIME_WAYLAND_GEN_DIR}/relative-pointer-unstable-v1-client-protocol.h")
            set(MELONPRIME_RELATIVE_POINTER_CODE
                "${MELONPRIME_WAYLAND_GEN_DIR}/relative-pointer-unstable-v1-protocol.c")
            set(MELONPRIME_POINTER_CONSTRAINTS_HEADER
                "${MELONPRIME_WAYLAND_GEN_DIR}/pointer-constraints-unstable-v1-client-protocol.h")
            set(MELONPRIME_POINTER_CONSTRAINTS_CODE
                "${MELONPRIME_WAYLAND_GEN_DIR}/pointer-constraints-unstable-v1-protocol.c")

            add_custom_command(
                OUTPUT
                    "${MELONPRIME_RELATIVE_POINTER_HEADER}"
                    "${MELONPRIME_RELATIVE_POINTER_CODE}"
                COMMAND "${MELONPRIME_WAYLAND_SCANNER_EXECUTABLE}"
                    client-header
                    "${MELONPRIME_RELATIVE_POINTER_XML}"
                    "${MELONPRIME_RELATIVE_POINTER_HEADER}"
                COMMAND "${MELONPRIME_WAYLAND_SCANNER_EXECUTABLE}"
                    private-code
                    "${MELONPRIME_RELATIVE_POINTER_XML}"
                    "${MELONPRIME_RELATIVE_POINTER_CODE}"
                DEPENDS "${MELONPRIME_RELATIVE_POINTER_XML}"
                VERBATIM)

            add_custom_command(
                OUTPUT
                    "${MELONPRIME_POINTER_CONSTRAINTS_HEADER}"
                    "${MELONPRIME_POINTER_CONSTRAINTS_CODE}"
                COMMAND "${MELONPRIME_WAYLAND_SCANNER_EXECUTABLE}"
                    client-header
                    "${MELONPRIME_POINTER_CONSTRAINTS_XML}"
                    "${MELONPRIME_POINTER_CONSTRAINTS_HEADER}"
                COMMAND "${MELONPRIME_WAYLAND_SCANNER_EXECUTABLE}"
                    private-code
                    "${MELONPRIME_POINTER_CONSTRAINTS_XML}"
                    "${MELONPRIME_POINTER_CONSTRAINTS_CODE}"
                DEPENDS "${MELONPRIME_POINTER_CONSTRAINTS_XML}"
                VERBATIM)

            target_sources(melonDS PRIVATE
                MelonPrimeWaylandPointerLock.cpp
                "${MELONPRIME_RELATIVE_POINTER_HEADER}"
                "${MELONPRIME_RELATIVE_POINTER_CODE}"
                "${MELONPRIME_POINTER_CONSTRAINTS_HEADER}"
                "${MELONPRIME_POINTER_CONSTRAINTS_CODE}")
            target_include_directories(melonDS PRIVATE
                "${MELONPRIME_WAYLAND_GEN_DIR}")
            target_compile_definitions(melonDS PRIVATE
                MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK=1)

            set(MELONPRIME_WAYLAND_POINTER_LOCK_ACTIVE ON)
            message(STATUS "Native Wayland mouse lock: enabled")
        endif()
    elseif (MELONPRIME_WAYLAND_POINTER_LOCK_MODE STREQUAL "OFF")
        message(STATUS "Native Wayland mouse lock: disabled by option")
    endif()

    target_sources(melonDS PRIVATE
        ../duckstation/gl/context_egl.cpp
        ../duckstation/gl/context_egl_x11.cpp
        ../duckstation/gl/context_glx.cpp
        ../duckstation/gl/x11_window.cpp

        ../glad/glad_egl.c
        ../glad/glad_glx.c  
    )
    target_link_libraries(melonDS PRIVATE "${X11_LIBRARIES}" "${X11_Xi_LIB}" "${EGL_LIBRARIES}")
    target_include_directories(melonDS PRIVATE "${X11_INCLUDE_DIR}" "${X11_Xi_INCLUDE_PATH}")
    add_compile_definitions(QAPPLICATION_CLASS=QApplication)
endif()


if (BUILD_STATIC)
    qt_import_plugins(melonDS INCLUDE Qt::QSvgPlugin)
    if (WIN32 AND USE_QT6)
        qt_import_plugins(melonDS INCLUDE Qt::QModernWindowsStylePlugin)
    endif()
    if (USE_VCPKG AND UNIX AND NOT APPLE)
        pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)
        target_link_libraries(melonDS PRIVATE PkgConfig::ALSA)
    elseif (NOT APPLE)
        target_link_options(melonDS PRIVATE -static)
    endif()
endif()

target_include_directories(melonDS PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/..")

if (USE_QT6)
    target_include_directories(melonDS PUBLIC ${Qt6Gui_PRIVATE_INCLUDE_DIRS})
else()
    target_include_directories(melonDS PUBLIC ${Qt5Gui_PRIVATE_INCLUDE_DIRS})
endif()
target_link_libraries(melonDS PRIVATE core)
target_link_libraries(melonDS PRIVATE PkgConfig::SDL2 PkgConfig::LibArchive PkgConfig::Zstd PkgConfig::Faad)
if (TARGET PkgConfig::LibZ)
    target_link_libraries(melonDS PRIVATE PkgConfig::LibZ)
endif()
target_link_libraries(melonDS PRIVATE ${QT_LINK_LIBS} ${CMAKE_DL_LIBS})

if (WIN32)
    option(PORTABLE "Make a portable build that looks for its configuration in the current directory" ON)

    if (PORTABLE)
        target_compile_definitions(melonDS PRIVATE WIN32_PORTABLE)
    endif()

    string(REPLACE . , MELON_RC_VERSION ${melonDS_VERSION})
    configure_file("${CMAKE_SOURCE_DIR}/res/melon.rc.in" "${CMAKE_BINARY_DIR}/res/melon.rc")
    target_sources(melonDS PUBLIC "${CMAKE_BINARY_DIR}/res/melon.rc")
    target_include_directories(melonDS PRIVATE "${CMAKE_BINARY_DIR}/res")

    if (${ARCHITECTURE} STREQUAL x86_64)
        set(WIN32_ARCHITECTURE amd64)
    elseif (${ARCHITECTURE} STREQUAL x86)
        set(WIN32_ARCHITECTURE x86)
    elseif (${ARCHITECTURE} MATCHES "ARM(64)?")
        string(TOLOWER "${ARCHITECTURE}" WIN32_ARCHITECTURE)
    endif()


    target_link_libraries(melonDS PRIVATE ws2_32 iphlpapi)
    set_target_properties(melonDS PROPERTIES WIN32_EXECUTABLE $<IF:$<CONFIG:Debug>,OFF,ON>)

    if (MINGW)
        configure_file("${CMAKE_SOURCE_DIR}/res/xp.manifest.in" "${CMAKE_BINARY_DIR}/res/xp.manifest")
    else()
        # Work around faad2 unconditionally linking libm even on Windows
        if (USE_VCPKG)
            add_library(m STATIC)
            target_link_libraries(melonDS PUBLIC m)
        endif()
    endif()
endif()

if (APPLE)
    # Copy icon into the bundle
    set(RESOURCE_FILES "${CMAKE_SOURCE_DIR}/res/melon.icns")
    target_sources(melonDS PUBLIC "${RESOURCE_FILES}")

    set_target_properties(melonDS PROPERTIES
        MACOSX_BUNDLE true
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/res/melon.plist.in
        OUTPUT_NAME melonPrimeDS  # MelonPrimeDS: bundle/binary name
        RESOURCE "${RESOURCE_FILES}")


    option(MACOS_BUNDLE_LIBS "Bundle libraries with the app on macOS" OFF)
    option(MACOS_BUILD_DMG "Build DMG image of the macOS application bundle" OFF)
    option(MACOS_ADHOC_SIGN_BUNDLE "Ad-hoc sign the macOS app bundle after build" ON)

    if (MELONPRIME_VULKAN_ACTIVE AND MELONPRIME_BUNDLE_MOLTENVK)
        if (NOT MELONPRIME_MOLTENVK_DYLIB OR NOT EXISTS "${MELONPRIME_MOLTENVK_DYLIB}")
            message(FATAL_ERROR
                "macOS Vulkan builds require MELONPRIME_MOLTENVK_DYLIB to point "
                "at a pinned libMoltenVK.dylib runtime for app-bundle deployment. "
                "Set MELONPRIME_FORCE_DISABLE_VULKAN=ON for a Metal-only build.")
        endif()

        add_custom_command(TARGET melonDS POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_DIR:melonDS>/Contents/Frameworks"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MELONPRIME_MOLTENVK_DYLIB}"
                "$<TARGET_BUNDLE_DIR:melonDS>/Contents/Frameworks/libMoltenVK.dylib"
            COMMENT "Bundling MoltenVK runtime into macOS app bundle..."
            VERBATIM)

        if (MELONPRIME_MOLTENVK_LICENSE)
            if (NOT EXISTS "${MELONPRIME_MOLTENVK_LICENSE}")
                message(FATAL_ERROR
                    "MELONPRIME_MOLTENVK_LICENSE was set but does not exist: "
                    "${MELONPRIME_MOLTENVK_LICENSE}")
            endif()
            add_custom_command(TARGET melonDS POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_BUNDLE_DIR:melonDS>/Contents/Resources/ThirdPartyNotices"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${MELONPRIME_MOLTENVK_LICENSE}"
                    "$<TARGET_BUNDLE_DIR:melonDS>/Contents/Resources/ThirdPartyNotices/MoltenVK-LICENSE.txt"
                COMMENT "Bundling MoltenVK license notice..."
                VERBATIM)
        endif()
    endif()

    if (MACOS_BUNDLE_LIBS)
        if (MACOS_BUILD_DMG)
            set(DMGARG "--dmg")
        endif()
        add_custom_command(TARGET melonDS POST_BUILD
            COMMAND ${CMAKE_SOURCE_DIR}/tools/mac-libs.rb ${DMGARG} ${CMAKE_BINARY_DIR}
            COMMENT "Bundling macOS libraries...")
    endif()

    if (MACOS_ADHOC_SIGN_BUNDLE)
        find_program(CODESIGN_EXECUTABLE codesign)
        if (CODESIGN_EXECUTABLE)
            add_custom_command(TARGET melonDS POST_BUILD
                COMMAND ${CODESIGN_EXECUTABLE} --force --sign - --deep --timestamp=none "$<TARGET_BUNDLE_DIR:melonDS>"
                COMMENT "Ad-hoc signing macOS app bundle...")
        else()
            message(WARNING "codesign not found; macOS app bundle will not be ad-hoc signed")
        endif()
    endif()
endif()

if (UNIX AND NOT APPLE)
    foreach(SIZE 16 32 48 64 128 256)
        install(FILES ${CMAKE_SOURCE_DIR}/res/icon/melon_${SIZE}x${SIZE}.png
            DESTINATION ${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/${SIZE}x${SIZE}/apps
            RENAME net.kuribo64.melonDS.png)
    endforeach()

    install(FILES ${CMAKE_SOURCE_DIR}/res/net.kuribo64.melonDS.desktop DESTINATION ${CMAKE_INSTALL_PREFIX}/share/applications)
    install(TARGETS melonDS BUNDLE DESTINATION ${CMAKE_BINARY_DIR} RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)

    if (NOT USE_QT6)
        set_target_properties(melonDS PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION OFF
            INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF)
    endif()
elseif(APPLE)
    install(TARGETS melonDS BUNDLE DESTINATION "${CMAKE_INSTALL_PREFIX}/Applications")
endif()

if (ENABLE_OGLRENDERER)
    set(MELONDS_GL_HEADER \"frontend/glad/glad.h\" CACHE STRING "Path to a header that contains OpenGL function and type declarations.")

    target_compile_definitions(melonDS PUBLIC OGLRENDERER_ENABLED)
    target_compile_definitions(melonDS PUBLIC MELONDS_GL_HEADER=${MELONDS_GL_HEADER})
    target_compile_definitions(core PUBLIC MELONDS_GL_HEADER=${MELONDS_GL_HEADER})
endif()
