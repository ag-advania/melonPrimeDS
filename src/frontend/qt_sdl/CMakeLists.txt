include(CMakeDependentOption)

include(FixInterfaceIncludes)

set(MELONPRIME_WINDOWS_ONLY_SOURCES
    MelonPrimeRawInputWinFilter.cpp
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
    MelonPrimeLocalization.cpp
    MelonPrimeHudRender.cpp
    MelonPrimeHudConfigOnScreenEdit.cpp
    MelonPrimePatchAspectRatio.cpp
    MelonPrimePatchOsdColor.cpp
    MelonPrimePatchNoHud.cpp
    MelonPrimePatchNoDoubleTapJump.cpp
    MelonPrimePatchFixWifi.cpp
    MelonPrimePatchUseFirmwareLanguage.cpp
    MelonPrimePatchShowHeadshotOnline.cpp
    MelonPrimePatchShowEnemyHpMeterOnline.cpp
    MelonPrimePatchDisableDoubleDamageMultiplier.cpp
    MelonPrimePatchNoPickingUpSpecificItems.cpp
    MelonPrimePatchShadowFreezeRuntimeHook.cpp
    MelonPrimePatchFixNoxusBladePersistence.cpp
    MelonPrimePatchInstantAimFollow.cpp
    MelonPrimePatchExpandStageMatrix.cpp
    MelonPrimePatchLowHpWarning.cpp
    MelonPrimePatchRegistry.cpp
    MelonPrimeArm9Hook.cpp


)

option(USE_QT6 "Use Qt 6 instead of Qt 5" ON)
option(MELONPRIME_ENABLE_DEVELOPER_FEATURES "Enable MelonPrime developer-only settings and patches" OFF)

if (USE_QT6)
    find_package(Qt6 COMPONENTS Core Gui Widgets Network Multimedia OpenGL OpenGLWidgets Svg REQUIRED)
    if (WIN32 AND MINGW AND TARGET WrapRt::WrapRt)
        # Qt's FindWrapRt can pick up MSYS /usr/lib/librt.a during a MinGW
        # configure, but Windows targets do not need librt and cannot link it.
        set_property(TARGET WrapRt::WrapRt PROPERTY INTERFACE_LINK_LIBRARIES "")
    endif()
    set(QT_LINK_LIBS Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Multimedia Qt6::OpenGL Qt6::OpenGLWidgets)
else()
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

if (MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    target_compile_definitions(melonDS PRIVATE MELONPRIME_ENABLE_DEVELOPER_FEATURES)
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

    if (ENABLE_WAYLAND)
        # we only need ECM for Wayland
        # so we only require it from here
        find_package(ECM REQUIRED NO_MODULE)
        list(APPEND CMAKE_MODULE_PATH "${ECM_MODULE_PATH}")

        find_package(Wayland REQUIRED Client)

        target_compile_definitions(melonDS PRIVATE WAYLAND_ENABLED)

        target_sources(melonDS PRIVATE
            ../duckstation/gl/context_egl_wayland.cpp
        )
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
                COMMAND ${CODESIGN_EXECUTABLE} --force --sign - --deep "$<TARGET_BUNDLE_DIR:melonDS>"
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
