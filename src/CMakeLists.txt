set (CMAKE_CXX_STANDARD 17)

include(FixInterfaceIncludes)

add_library(core STATIC
    ARCodeFile.cpp
    ARDatabaseDAT.cpp
    AREngine.cpp
    ARM.cpp
    ARM_InstrTable.h
    ARMInterpreter.cpp
    ARMInterpreter_ALU.cpp
    ARMInterpreter_Branch.cpp
    ARMInterpreter_LoadStore.cpp
    CP15.cpp
    CRC32.cpp
    DMA.cpp
    DMA_Timings.h
    DMA_Timings.cpp
    DSi.cpp
    DSi_AES.cpp
    DSi_Camera.cpp
    DSi_DSP.cpp
    DSi_I2C.cpp
    DSi_I2S.cpp
    DSi_NAND.cpp
    DSi_NDMA.cpp
    DSi_NWifi.cpp
    DSi_SD.cpp
    DSi_SPI_TSC.cpp
    FATIO.cpp
    FATStorage.cpp
    FIFO.h
    GBACart.cpp
    GBACartMotionPak.cpp
    GPU.cpp
    GPU_Soft.cpp
    SapphireGPU2DSoftAccess.cpp
    SapphireGPU2DStructuredVulkan.cpp
    GPU2D.cpp
    GPU2D_Soft.cpp
    GPU3D.cpp
    GPU3D_Soft.cpp
    GPU3D_Texcache.cpp
    GPU3D_Texcache.h
    Mic.cpp
    NDS.cpp
    NDSCart.cpp
    Platform.h
    ROMList.h
    ROMList.cpp
    FreeBIOS.h
    FreeBIOS.cpp
    RTC.cpp
    Savestate.cpp
    SPI.cpp
    SPI_Firmware.cpp
    SPU.cpp
    types.h
    Utils.cpp
    Utils.h
    Wifi.cpp
    WifiAP.cpp

    DSP_HLE/UcodeBase.cpp
    DSP_HLE/AACUcode.cpp
    DSP_HLE/G711Ucode.cpp
    DSP_HLE/GraphicsUcode.cpp

    NDSCart/CartCommon.cpp
    NDSCart/CartRetail.cpp
    NDSCart/CartRetailNAND.cpp
    NDSCart/CartRetailIR.cpp
    NDSCart/CartRetailBT.cpp
    NDSCart/CartSD.cpp
    NDSCart/CartHomebrew.cpp
    NDSCart/CartR4.cpp

    fatfs/ff.c
    fatfs/ffsystem.c
    fatfs/ffunicode.c
    fatfs/ffconf.h

    sha1/sha1.c
    tiny-AES-c/aes.c
    xxhash/xxhash.c

    blip-buf/blip_buf.c)

if (ENABLE_GDBSTUB)
    message(NOTICE "Enabling GDB stub")
    target_sources(core PRIVATE
        debug/GdbStub.cpp
        debug/GdbProto.cpp
        debug/GdbCmds.cpp
    )
endif()

if (ENABLE_OGLRENDERER)
    add_subdirectory(OpenGL_shaders)
    target_sources(core PRIVATE
        GPU_OpenGL.cpp
        GPU2D_OpenGL.cpp
        GPU3D_OpenGL.cpp
        GPU3D_Compute.cpp
        GPU3D_TexcacheOpenGL.cpp
        GPU3D_TexcacheOpenGL.h
        OpenGLSupport.cpp)

    target_compile_definitions(core PUBLIC OGLRENDERER_ENABLED)
endif()

if (MELONPRIME_VULKAN_ACTIVE)
    # MELONPRIME_VULKAN_REFERENCE_PORT_V0_V5_V1
    find_package(volk CONFIG REQUIRED)
    find_package(VulkanMemoryAllocator CONFIG REQUIRED)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    find_program(MELONPRIME_GLSLANG_VALIDATOR
        NAMES glslangValidator glslangValidator.exe
        HINTS
            "$ENV{VULKAN_SDK}/Bin"
            "$ENV{VULKAN_SDK}/bin"
            "C:/msys64/mingw64/bin")
    if (NOT MELONPRIME_GLSLANG_VALIDATOR)
        message(FATAL_ERROR
            "Vulkan shader generation requires glslangValidator. "
            "Install glslang or set MELONPRIME_GLSLANG_VALIDATOR.")
    endif()

    set(MELONPRIME_SAPPHIRE_SHADER_OUTPUT_DIR
        "${CMAKE_BINARY_DIR}/generated/sapphire")
    set(MELONPRIME_SAPPHIRE_SHADER_HEADERS
        GPU3D_Vulkan_BinCombinedShaderData.h
        GPU3D_Vulkan_CalculateWorkOffsetsShaderData.h
        GPU3D_Vulkan_CaptureLineExportShaderData.h
        GPU3D_Vulkan_DepthBlendShaderData.h
        GPU3D_Vulkan_FinalPassShaderData.h
        GPU3D_Vulkan_InterpSpansShaderData.h
        GPU3D_Vulkan_SortWorkShaderData.h
        GPU3D_Vulkan_TriRasterBaseShaderData.h
        GPU3D_Vulkan_TriRasterCompatShaderData.h
        GPU3D_Vulkan_TriRasterShaderData.h
        GPU3D_Vulkan_GraphicsClearShaderData.h
        GPU3D_Vulkan_GraphicsEdgeFogShaderData.h
        GPU3D_Vulkan_GraphicsEdgeShaderData.h
        GPU3D_Vulkan_GraphicsFinalShaderVertexData.h
        GPU3D_Vulkan_GraphicsFogShaderData.h
        GPU3D_Vulkan_GraphicsNoColorShaderData.h
        GPU3D_Vulkan_GraphicsRasterShaderVertexData.h
        GPU3D_Vulkan_GraphicsRasterShaderFragmentData.h
        GPU3D_Vulkan_GraphicsRasterNoFragDepthShaderFragmentData.h
        GPU3D_Vulkan_GraphicsRasterNoFragDepthDirectShaderFragmentData.h
        GPU3D_Vulkan_GraphicsRasterNoFragDepthDirectFastModulateShaderFragmentData.h
        GPU3D_Vulkan_GraphicsRasterNoFragDepthDirectFastModulateToonShaderFragmentData.h
        GPU3D_Vulkan_GraphicsRasterNoFragDepthDirectFastModulatePlainShaderFragmentData.h
        GPU3D_Vulkan_GraphicsRasterNoFragDepthDirectFastModulateOpaqueAlphaToonShaderFragmentData.h
        GPU3D_Vulkan_GraphicsRasterNoFragDepthDirectFastModulateOpaqueAlphaPlainShaderFragmentData.h
        VulkanAccumulate3dShaderData.h
        VulkanCompositorShaderData.h
        VulkanSurfacePresenterVertexShaderData.h
        VulkanSurfacePresenterFragmentShaderData.h)
    list(TRANSFORM MELONPRIME_SAPPHIRE_SHADER_HEADERS
        PREPEND "${MELONPRIME_SAPPHIRE_SHADER_OUTPUT_DIR}/")

    set(MELONPRIME_SAPPHIRE_SHADER_SOURCES
        GPU3D_Vulkan_BinCombinedShader.comp
        GPU3D_Vulkan_CalculateWorkOffsetsShader.comp
        GPU3D_Vulkan_CaptureLineExportShader.comp
        GPU3D_Vulkan_DepthBlendShader.comp
        GPU3D_Vulkan_FinalPassShader.comp
        GPU3D_Vulkan_InterpSpansShader.comp
        GPU3D_Vulkan_SortWorkShader.comp
        GPU3D_Vulkan_TriRasterBaseShader.comp
        GPU3D_Vulkan_TriRasterCompatShader.comp
        GPU3D_Vulkan_TriRasterShader.comp
        GPU3D_Vulkan_GraphicsClearShader.frag
        GPU3D_Vulkan_GraphicsEdgeFogShader.frag
        GPU3D_Vulkan_GraphicsEdgeShader.frag
        GPU3D_Vulkan_GraphicsFinalShader.vert
        GPU3D_Vulkan_GraphicsFogShader.frag
        GPU3D_Vulkan_GraphicsNoColorShader.frag
        GPU3D_Vulkan_GraphicsRasterShader.vert
        GPU3D_Vulkan_GraphicsRasterShader.frag
        VulkanStructuredControlAbi.inc
        frontend/qt_sdl/VulkanReference/VulkanAccumulate3dShader.comp
        frontend/qt_sdl/VulkanReference/VulkanCompositorShader.comp
        frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.vert
        frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.frag)
    list(TRANSFORM MELONPRIME_SAPPHIRE_SHADER_SOURCES
        PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
    set(MELONPRIME_SAPPHIRE_SHADER_STAMP
        "${MELONPRIME_SAPPHIRE_SHADER_OUTPUT_DIR}/generation.stamp")

    add_custom_command(
        OUTPUT "${MELONPRIME_SAPPHIRE_SHADER_STAMP}"
        BYPRODUCTS ${MELONPRIME_SAPPHIRE_SHADER_HEADERS}
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tools/vulkan/generate_sapphire_spirv.py"
            --manifest "${CMAKE_SOURCE_DIR}/tools/vulkan/sapphire_shader_manifest.json"
            --output-root "${MELONPRIME_SAPPHIRE_SHADER_OUTPUT_DIR}"
            --compiler "${MELONPRIME_GLSLANG_VALIDATOR}"
        COMMAND "${CMAKE_COMMAND}" -E touch
            "${MELONPRIME_SAPPHIRE_SHADER_STAMP}"
        DEPENDS
            "${CMAKE_SOURCE_DIR}/tools/vulkan/generate_sapphire_spirv.py"
            "${CMAKE_SOURCE_DIR}/tools/vulkan/sapphire_shader_manifest.json"
            ${MELONPRIME_SAPPHIRE_SHADER_SOURCES}
        COMMENT "Generating Sapphire Vulkan SPIR-V headers"
        VERBATIM)
    add_custom_target(melonprime_sapphire_shaders
        DEPENDS "${MELONPRIME_SAPPHIRE_SHADER_STAMP}")
    add_dependencies(core melonprime_sapphire_shaders)
    target_include_directories(core PRIVATE "${CMAKE_BINARY_DIR}/generated")

    target_sources(core PRIVATE
        GPU_Vulkan.cpp
        GPU_Vulkan.h
        VulkanContext.cpp
        VulkanContext.h
        VulkanDesktopCompat.cpp
        VulkanDesktopCompat.h
        GPU3D_AcceleratedFrontend.cpp
        GPU3D_AcceleratedFrontend.h
        GPU3D_Vulkan.cpp
        GPU3D_Vulkan.h
        GPU3D_TexcacheVulkan.cpp
        GPU3D_TexcacheVulkan.h
        VulkanPerfStats.h)
    target_compile_definitions(core PUBLIC MELONPRIME_ENABLE_VULKAN=1)
    target_compile_definitions(core PRIVATE VK_NO_PROTOTYPES=1)
    if (WIN32)
        target_compile_definitions(core PRIVATE VK_USE_PLATFORM_WIN32_KHR=1)
    endif()
    target_compile_definitions(core PRIVATE
        VMA_STATIC_VULKAN_FUNCTIONS=0
        VMA_DYNAMIC_VULKAN_FUNCTIONS=1)
    target_link_libraries(core PRIVATE
        volk::volk
        volk::volk_headers
        GPUOpen::VulkanMemoryAllocator)
endif()

if (ENABLE_JIT)
    enable_language(ASM)

    target_sources(core PRIVATE
        ARM_InstrInfo.cpp

        ARMJIT.cpp
        ARMJIT_Memory.cpp
        ARMJIT_Global.cpp

        dolphin/CommonFuncs.cpp)
    
    if (WIN32)
        # Required for memory mapping-related functions introduced in Windows 8
        target_compile_definitions(core PRIVATE -D_WIN32_WINNT=_WIN32_WINNT_WIN8)
        if (NOT MINGW)
            target_link_libraries(core PRIVATE onecore)
        endif()
    endif()

    if (CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
        target_link_options(core PUBLIC -Wl,-z,wxneeded)
    endif()

    if (ARCHITECTURE STREQUAL x86_64)
        target_sources(core PRIVATE
            dolphin/x64ABI.cpp
            dolphin/x64CPUDetect.cpp
            dolphin/x64Emitter.cpp

            ARMJIT_x64/ARMJIT_Compiler.cpp
            ARMJIT_x64/ARMJIT_ALU.cpp
            ARMJIT_x64/ARMJIT_LoadStore.cpp
            ARMJIT_x64/ARMJIT_Branch.cpp

            ARMJIT_x64/ARMJIT_Linkage.S)
    endif()
    if (ARCHITECTURE STREQUAL ARM64)
        target_sources(core PRIVATE
            dolphin/Arm64Emitter.cpp
            dolphin/MathUtil.cpp

            ARMJIT_A64/ARMJIT_Compiler.cpp
            ARMJIT_A64/ARMJIT_ALU.cpp
            ARMJIT_A64/ARMJIT_LoadStore.cpp
            ARMJIT_A64/ARMJIT_Branch.cpp

            ARMJIT_A64/ARMJIT_Linkage.S)
    endif()
endif()

target_include_directories(core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(core PUBLIC MELONPRIME_DS)

set(MELONDS_VERSION_SUFFIX "$ENV{MELONDS_VERSION_SUFFIX}" CACHE STRING "Suffix to add to displayed melonDS version")
option(MELONDS_EMBED_BUILD_INFO "Embed detailed build info into the binary" OFF)
set(MELONDS_GIT_BRANCH "$ENV{MELONDS_GIT_BRANCH}" CACHE STRING "The Git branch used for this build")
set(MELONDS_GIT_HASH "$ENV{MELONDS_GIT_HASH}" CACHE STRING "The hash of the Git commit")
set(MELONDS_BUILD_PROVIDER "$ENV{MELONDS_BUILD_PROVIDER}" CACHE STRING "The name of the provider of this build")

if (MELONDS_EMBED_BUILD_INFO)
    target_compile_definitions(core PUBLIC MELONDS_EMBED_BUILD_INFO)
    if (NOT MELONDS_GIT_BRANCH OR NOT MELONDS_GIT_HASH OR NOT MELONDS_BUILD_PROVIDER)
        message(FATAL_ERROR "When embedding build information, all fields must be filled out. See src/CMakeLists.txt.")
    endif()
endif()

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/version.h")
target_sources(core PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/version.h")
target_include_directories(core PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")

set(BUILD_SHARED_LIBS OFF)
add_subdirectory(teakra EXCLUDE_FROM_ALL)
# Workaround for building teakra with -O0 on Windows either failing or hanging forever
target_compile_options(teakra PRIVATE "$<$<CONFIG:DEBUG>:-Og>")
target_link_libraries(core PRIVATE teakra)

if (NOT MSVC)
    # MSVC has its own compiler flag syntax; if we ever support it,
    # be sure to add equivalent flags here.

    target_compile_options(core PUBLIC -fwrapv)

    target_compile_options(core PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-Wno-invalid-offsetof>")
    # These warnings are excessive, and are only triggered in the ARMJIT code
    # (which is fundamentally non-portable, so this is fine)
endif()

# Fix xxhash compile errors when using -Og
# See https://github.com/Cyan4973/xxHash/blob/136cc1f8fe4d5ea62a7c16c8424d4fa5158f6d68/README.md?plain=1#L159-L163
target_compile_definitions(core PRIVATE $<$<CONFIG:DEBUG>:XXH_NO_INLINE_HINTS=1>)

find_library(m MATH_LIBRARY)

if (MATH_LIBRARY)
    target_link_libraries(core PRIVATE ${MATH_LIBRARY})
endif()

if (ENABLE_JIT)
    target_compile_definitions(core PUBLIC JIT_ENABLED)

    if (ENABLE_JIT_PROFILING)
        include(../cmake/FindVTune.cmake)
        add_definitions(-DJIT_PROFILING_ENABLED)
    endif()
endif()

if (WIN32)
    target_link_libraries(core PRIVATE ole32 comctl32 wsock32 ws2_32)

    if (NOT MINGW)
        find_path(DIRENT_INCLUDE_DIRS "dirent.h")
        target_include_directories(core PUBLIC ${DIRENT_INCLUDE_DIRS})
    endif()
elseif(NOT APPLE AND NOT HAIKU)
    check_library_exists(rt shm_open "" NEED_LIBRT)
    if (NEED_LIBRT)
        target_link_libraries(core PRIVATE rt)
    endif()
elseif(HAIKU)
    target_link_libraries(core PRIVATE network)
endif()

if (ENABLE_JIT_PROFILING)
    target_include_directories(core PRIVATE "${VTUNE_INCLUDE_DIR}")
    target_link_libraries(core PRIVATE "${VTUNE_LIBRARY}")
endif()

#if(CMAKE_BUILD_TYPE MATCHES "Debug")
#  set(
#    CMAKE_C_FLAGS
#    "${CMAKE_C_FLAGS} -fsanitize=undefined -fsanitize=address"
#  )
#  target_link_options(core
#    BEFORE PUBLIC -fsanitize=undefined PUBLIC -fsanitize=address
#  )
#endif()
