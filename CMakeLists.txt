cmake_minimum_required(VERSION 3.16)

cmake_policy(VERSION 3.15)
if (POLICY CMP0076)
    cmake_policy(SET CMP0076 NEW)
endif()

set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
set(CMAKE_USER_MAKE_RULES_OVERRIDE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DefaultBuildFlags.cmake")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum OS X deployment version")

# --- MelonPrime Vulkan backend master build gate ----------------------------
# This resolution happens before project()/vcpkg so an inactive Vulkan build
# cannot acquire Vulkan manifest features as a configure-time side effect.
option(MELONPRIME_ENABLE_VULKAN
    "Build MelonPrime Vulkan renderers and presenter"
    OFF)
option(MELONPRIME_FORCE_DISABLE_VULKAN
    "Completely exclude all MelonPrime Vulkan code and dependencies"
    OFF)
option(MELONPRIME_ENABLE_VULKAN_MOLTENVK
    "Allow the Vulkan backend through MoltenVK on macOS"
    OFF)

set(MELONPRIME_VULKAN_ACTIVE OFF)
if (MELONPRIME_ENABLE_VULKAN AND NOT MELONPRIME_FORCE_DISABLE_VULKAN)
    set(MELONPRIME_VULKAN_ACTIVE ON)
endif()

option(MELONPRIME_ENABLE_SANITIZERS
    "Enable AddressSanitizer and UndefinedBehaviorSanitizer (Linux/clang/gcc)"
    OFF)
option(MELONPRIME_ENABLE_MSAN
    "Enable MemorySanitizer (requires fully instrumented dependency tree)"
    OFF)

option(USE_VCPKG "Use vcpkg for dependency packages" OFF)
if (USE_VCPKG)
	if (MELONPRIME_VULKAN_ACTIVE)
		list(APPEND VCPKG_MANIFEST_FEATURES vulkan)
	endif()
	include(ConfigureVcpkg)
endif()

project(melonDS
    VERSION 1.1
    DESCRIPTION "DS emulator, sorta"
    HOMEPAGE_URL "https://melonds.kuribo64.net"
    LANGUAGES C CXX)

if (MELONPRIME_VULKAN_ACTIVE AND APPLE AND NOT MELONPRIME_ENABLE_VULKAN_MOLTENVK)
    message(FATAL_ERROR
        "Vulkan on macOS requires MELONPRIME_ENABLE_VULKAN_MOLTENVK=ON")
endif()

include(CheckSymbolExists)
include(CheckLibraryExists)
include(CMakeDependentOption)
include(CheckIPOSupported)

include(SetupCCache)
include(Sanitizers)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

function(detect_architecture symbol arch)
    if (NOT DEFINED ARCHITECTURE)
        set(CMAKE_REQUIRED_QUIET 1)
        check_symbol_exists("${symbol}" "" ARCHITECTURE_${arch})
        unset(CMAKE_REQUIRED_QUIET)

        # The output variable needs to be unique across invocations otherwise
        # CMake's crazy scope rules will keep it defined
        if (ARCHITECTURE_${arch})
            set(ARCHITECTURE "${arch}" PARENT_SCOPE)
            set(ARCHITECTURE_${arch} 1 PARENT_SCOPE)
            add_definitions(-DARCHITECTURE_${arch}=1)
        endif()
    endif()
endfunction()

detect_architecture("__x86_64__" x86_64)
detect_architecture("__i386__" x86)
detect_architecture("__arm__" ARM)
detect_architecture("__aarch64__" ARM64)

cmake_dependent_option(ENABLE_JIT "Enable JIT recompiler" ON
    "ARCHITECTURE STREQUAL x86_64 OR ARCHITECTURE STREQUAL ARM64;NOT ARCHITECTURE STREQUAL x86_64 OR NOT APPLE" OFF)
cmake_dependent_option(ENABLE_JIT_PROFILING "Enable JIT profiling with VTune" OFF "ENABLE_JIT" OFF)
option(ENABLE_OGLRENDERER "Enable OpenGL renderer" ON)

check_ipo_supported(RESULT IPO_SUPPORTED)
cmake_dependent_option(ENABLE_LTO_RELEASE "Enable link-time optimizations for release builds" ON "IPO_SUPPORTED" OFF)
cmake_dependent_option(ENABLE_LTO "Enable link-time optimizations" OFF "IPO_SUPPORTED" OFF)

if (ENABLE_LTO_RELEASE)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

if (ENABLE_LTO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if (NOT APPLE)
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -s")
endif()

if (WIN32)
    option(BUILD_STATIC "Statically link dependencies" OFF)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX)
    if (NOT MINGW)
        add_definitions(-D__WIN32__ -D_CRT_SECURE_NO_WARNINGS -D_USE_MATH_DEFINES)
        if (BUILD_STATIC)
            set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        endif()
    elseif (BUILD_STATIC AND MINGW)
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
    endif()
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(ENABLE_GDBSTUB "Enable GDB stub" ON)
if (ENABLE_GDBSTUB)
    add_definitions(-DGDBSTUB_ENABLED)
endif()

option(BUILD_QT_SDL "Build Qt/SDL frontend" ON)

if (MELONPRIME_ENABLE_MSAN)
    add_compile_options(-fsanitize=memory -fsanitize-memory-track-origins=2 -fno-omit-frame-pointer -g3)
    add_link_options(-fsanitize=memory)
elseif (MELONPRIME_ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g3)
    add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(src)

if (BUILD_QT_SDL)
    add_subdirectory(src/frontend/qt_sdl)
endif()
