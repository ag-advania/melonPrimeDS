#pragma once

// MELONPRIME_VULKAN_R25_PIPELINE_CACHE_V1
// Owner-separated, device/driver/ABI-keyed persistent Vulkan pipeline cache.
// Cache load/save failures are deliberately non-fatal to renderer startup.

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

#include <volk.h>

#include "Platform.h"
#include "VulkanReferencePortVersion.h"
#include "types.h"

namespace melonDS
{

enum class VulkanPipelineCacheOwner : u32
{
    Renderer3D = 1,
    Compositor = 2,
    Presenter = 3,
};

class VulkanPersistentPipelineCache
{
public:
    VulkanPersistentPipelineCache() = default;
    ~VulkanPersistentPipelineCache() = default;

    VulkanPersistentPipelineCache(const VulkanPersistentPipelineCache&) = delete;
    VulkanPersistentPipelineCache& operator=(const VulkanPersistentPipelineCache&) = delete;

    bool Create(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        VulkanPipelineCacheOwner owner,
        const char* ownerName,
        const char* fileName,
        u32 pipelineAbiVersion,
        u32 descriptorIndexingMode,
        u32 textureSamplingPath)
    {
        Destroy();

        PhysicalDevice = physicalDevice;
        Device = device;
        OwnerName = ownerName != nullptr ? ownerName : "unknown";
        CacheFileName = fileName != nullptr ? fileName : "";
        CacheFilePath = CacheFileName.empty()
            ? std::string{}
            : Platform::GetLocalFilePath(CacheFileName);

        if (PhysicalDevice == VK_NULL_HANDLE || Device == VK_NULL_HANDLE)
            return false;

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(PhysicalDevice, &properties);

        ExpectedHeader = {};
        std::memcpy(ExpectedHeader.Magic, kMagic.data(), kMagic.size());
        ExpectedHeader.HeaderVersion = MelonPrime::Vulkan::kPipelineCacheDiskFormatVersion;
        ExpectedHeader.Owner = static_cast<u32>(owner);
        ExpectedHeader.PipelineAbiVersion = pipelineAbiVersion;
        ExpectedHeader.DescriptorIndexingMode = descriptorIndexingMode;
        ExpectedHeader.TextureSamplingPath = textureSamplingPath;
        ExpectedHeader.VendorId = properties.vendorID;
        ExpectedHeader.DeviceId = properties.deviceID;
        ExpectedHeader.DriverVersion = properties.driverVersion;
        ExpectedHeader.ApiVersion = properties.apiVersion;
        std::memcpy(
            ExpectedHeader.PipelineCacheUuid,
            properties.pipelineCacheUUID,
            VK_UUID_SIZE);
        ExpectedHeader.CoreReferenceHash =
            HashString(MelonPrime::Vulkan::kSapphireCoreCommit);
        ExpectedHeader.FrontendReferenceHash =
            HashString(MelonPrime::Vulkan::kSapphireFrontendTag);
        ExpectedHeader.ShaderManifestHash =
            HashString(MelonPrime::Vulkan::kSapphireShaderManifestIdentity);

        std::vector<u8> initialData;
        LoadInitialData(initialData);

        VkPipelineCacheCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        createInfo.initialDataSize = initialData.size();
        createInfo.pInitialData = initialData.empty() ? nullptr : initialData.data();

        VkResult result =
            vkCreatePipelineCache(Device, &createInfo, nullptr, &Cache);
        if (result != VK_SUCCESS && !initialData.empty())
        {
            Platform::Log(
                Platform::LogLevel::Warn,
                "VulkanPipelineCache[%s]: cached data rejected (%d); retrying empty",
                OwnerName.c_str(),
                static_cast<int>(result));
            createInfo.initialDataSize = 0;
            createInfo.pInitialData = nullptr;
            result =
                vkCreatePipelineCache(Device, &createInfo, nullptr, &Cache);
        }

        if (result != VK_SUCCESS)
        {
            Cache = VK_NULL_HANDLE;
            Platform::Log(
                Platform::LogLevel::Warn,
                "VulkanPipelineCache[%s]: cache creation failed (%d); continuing uncached",
                OwnerName.c_str(),
                static_cast<int>(result));
            return false;
        }

        Platform::Log(
            Platform::LogLevel::Info,
            "VulkanPipelineCache[%s]: ready key=%08x:%08x driver=%08x abi=%u descriptor=%u texture=%u loaded=%llu",
            OwnerName.c_str(),
            ExpectedHeader.VendorId,
            ExpectedHeader.DeviceId,
            ExpectedHeader.DriverVersion,
            ExpectedHeader.PipelineAbiVersion,
            ExpectedHeader.DescriptorIndexingMode,
            ExpectedHeader.TextureSamplingPath,
            static_cast<unsigned long long>(initialData.size()));
        return true;
    }

    [[nodiscard]] VkPipelineCache Get() const noexcept
    {
        return Cache;
    }

    void SaveAndDestroy(bool deviceLost)
    {
        if (!deviceLost)
            Save();
        Destroy();
    }

private:
#pragma pack(push, 1)
    struct DiskHeader
    {
        char Magic[8]{};
        u32 HeaderVersion = 0;
        u32 Owner = 0;
        u32 PipelineAbiVersion = 0;
        u32 DescriptorIndexingMode = 0;
        u32 TextureSamplingPath = 0;
        u32 VendorId = 0;
        u32 DeviceId = 0;
        u32 DriverVersion = 0;
        u32 ApiVersion = 0;
        u8 PipelineCacheUuid[VK_UUID_SIZE]{};
        u64 CoreReferenceHash = 0;
        u64 FrontendReferenceHash = 0;
        u64 ShaderManifestHash = 0;
        u64 PayloadHash = 0;
        u64 PayloadSize = 0;
    };
#pragma pack(pop)

    static constexpr std::array<char, 8> kMagic = {
        'M', 'P', 'V', 'K', 'P', 'C', '2', '5'
    };
    static constexpr u64 kMaximumPayloadSize =
        256ull * 1024ull * 1024ull;

    static u64 HashBytes(const void* data, size_t size)
    {
        constexpr u64 offsetBasis = 14695981039346656037ull;
        constexpr u64 prime = 1099511628211ull;

        u64 hash = offsetBasis;
        const auto* bytes = static_cast<const u8*>(data);
        for (size_t i = 0; i < size; i++)
        {
            hash ^= static_cast<u64>(bytes[i]);
            hash *= prime;
        }
        return hash;
    }

    static u64 HashString(const char* value)
    {
        if (value == nullptr)
            return HashBytes("", 0);
        return HashBytes(value, std::strlen(value));
    }

    bool HeaderMatches(const DiskHeader& header) const
    {
        return std::memcmp(header.Magic, ExpectedHeader.Magic, kMagic.size()) == 0
            && header.HeaderVersion == ExpectedHeader.HeaderVersion
            && header.Owner == ExpectedHeader.Owner
            && header.PipelineAbiVersion == ExpectedHeader.PipelineAbiVersion
            && header.DescriptorIndexingMode == ExpectedHeader.DescriptorIndexingMode
            && header.TextureSamplingPath == ExpectedHeader.TextureSamplingPath
            && header.VendorId == ExpectedHeader.VendorId
            && header.DeviceId == ExpectedHeader.DeviceId
            && header.DriverVersion == ExpectedHeader.DriverVersion
            && header.ApiVersion == ExpectedHeader.ApiVersion
            && std::memcmp(
                header.PipelineCacheUuid,
                ExpectedHeader.PipelineCacheUuid,
                VK_UUID_SIZE) == 0
            && header.CoreReferenceHash == ExpectedHeader.CoreReferenceHash
            && header.FrontendReferenceHash == ExpectedHeader.FrontendReferenceHash
            && header.ShaderManifestHash == ExpectedHeader.ShaderManifestHash;
    }

    void LoadInitialData(std::vector<u8>& initialData)
    {
        initialData.clear();
        if (CacheFilePath.empty())
            return;

        Platform::FileHandle* file =
            Platform::OpenFile(CacheFilePath, Platform::FileMode::Read);
        if (file == nullptr)
            return;

        const u64 fileSize = Platform::FileLength(file);
        if (fileSize < sizeof(DiskHeader)
            || fileSize > sizeof(DiskHeader) + kMaximumPayloadSize)
        {
            Platform::CloseFile(file);
            Platform::Log(
                Platform::LogLevel::Warn,
                "VulkanPipelineCache[%s]: ignored invalid file size %llu",
                OwnerName.c_str(),
                static_cast<unsigned long long>(fileSize));
            return;
        }

        DiskHeader header{};
        const bool headerRead =
            Platform::FileRead(&header, 1, sizeof(header), file) == sizeof(header);
        const u64 actualPayloadSize = fileSize - sizeof(DiskHeader);
        if (!headerRead
            || !HeaderMatches(header)
            || header.PayloadSize != actualPayloadSize
            || header.PayloadSize == 0
            || header.PayloadSize > kMaximumPayloadSize)
        {
            Platform::CloseFile(file);
            Platform::Log(
                Platform::LogLevel::Info,
                "VulkanPipelineCache[%s]: cache key/header mismatch; ignoring old cache",
                OwnerName.c_str());
            return;
        }

        initialData.resize(static_cast<size_t>(header.PayloadSize));
        const bool payloadRead =
            Platform::FileRead(
                initialData.data(),
                1,
                header.PayloadSize,
                file) == header.PayloadSize;
        Platform::CloseFile(file);

        if (!payloadRead
            || HashBytes(initialData.data(), initialData.size())
                != header.PayloadHash)
        {
            initialData.clear();
            Platform::Log(
                Platform::LogLevel::Warn,
                "VulkanPipelineCache[%s]: cache payload is truncated/corrupt; ignoring",
                OwnerName.c_str());
        }
    }

    static bool AtomicReplace(
        const std::filesystem::path& temporaryPath,
        const std::filesystem::path& finalPath)
    {
#if defined(_WIN32)
        return MoveFileExW(
            temporaryPath.c_str(),
            finalPath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        return std::rename(
            temporaryPath.c_str(),
            finalPath.c_str()) == 0;
#endif
    }

    bool AcquireSaveLock(
        const std::filesystem::path& lockPath,
        std::error_code& error)
    {
        error.clear();
        if (std::filesystem::create_directory(lockPath, error))
            return true;

        error.clear();
        if (!std::filesystem::exists(lockPath, error))
            return false;

        // A crashed writer must not disable cache saves forever.
        const auto lastWrite = std::filesystem::last_write_time(lockPath, error);
        if (error)
            return false;
        const auto age =
            std::filesystem::file_time_type::clock::now() - lastWrite;
        if (age <= std::chrono::minutes(10))
            return false;

        std::filesystem::remove_all(lockPath, error);
        error.clear();
        return std::filesystem::create_directory(lockPath, error);
    }

    void Save()
    {
        if (Device == VK_NULL_HANDLE
            || Cache == VK_NULL_HANDLE
            || CacheFilePath.empty())
        {
            return;
        }

        size_t payloadSize = 0;
        if (vkGetPipelineCacheData(
                Device, Cache, &payloadSize, nullptr) != VK_SUCCESS
            || payloadSize == 0
            || payloadSize > kMaximumPayloadSize)
        {
            return;
        }

        std::vector<u8> payload(payloadSize);
        if (vkGetPipelineCacheData(
                Device, Cache, &payloadSize, payload.data()) != VK_SUCCESS
            || payloadSize == 0)
        {
            return;
        }
        payload.resize(payloadSize);

        const std::filesystem::path finalPath =
            std::filesystem::u8path(CacheFilePath);
        const std::filesystem::path lockPath =
            std::filesystem::u8path(CacheFilePath + ".lock");

        std::error_code error;
        if (!AcquireSaveLock(lockPath, error))
        {
            Platform::Log(
                Platform::LogLevel::Info,
                "VulkanPipelineCache[%s]: another process owns the save lock; skipping",
                OwnerName.c_str());
            return;
        }

        struct LockCleanup
        {
            std::filesystem::path Path;
            ~LockCleanup()
            {
                std::error_code ignored;
                std::filesystem::remove_all(Path, ignored);
            }
        } lockCleanup{lockPath};

        const auto nonce =
            std::chrono::steady_clock::now().time_since_epoch().count()
            ^ static_cast<long long>(
                std::hash<std::thread::id>{}(std::this_thread::get_id()));
        const std::filesystem::path temporaryPath =
            std::filesystem::u8path(
                CacheFilePath + ".tmp." + std::to_string(nonce));

        DiskHeader header = ExpectedHeader;
        header.PayloadSize = static_cast<u64>(payload.size());
        header.PayloadHash = HashBytes(payload.data(), payload.size());

        Platform::FileHandle* file =
            Platform::OpenFile(
                temporaryPath.u8string(),
                Platform::FileMode::Write);
        if (file == nullptr)
        {
            Platform::Log(
                Platform::LogLevel::Warn,
                "VulkanPipelineCache[%s]: failed to create temporary cache file",
                OwnerName.c_str());
            return;
        }

        const bool headerWritten =
            Platform::FileWrite(&header, 1, sizeof(header), file)
                == sizeof(header);
        const bool payloadWritten =
            Platform::FileWrite(
                payload.data(),
                1,
                payload.size(),
                file) == payload.size();
        const bool flushed = Platform::FileFlush(file);
        const bool closed = Platform::CloseFile(file);

        if (!headerWritten || !payloadWritten || !flushed || !closed)
        {
            std::filesystem::remove(temporaryPath, error);
            Platform::Log(
                Platform::LogLevel::Warn,
                "VulkanPipelineCache[%s]: incomplete temporary cache write",
                OwnerName.c_str());
            return;
        }

        if (!AtomicReplace(temporaryPath, finalPath))
        {
            std::filesystem::remove(temporaryPath, error);
            Platform::Log(
                Platform::LogLevel::Warn,
                "VulkanPipelineCache[%s]: atomic cache replacement failed",
                OwnerName.c_str());
            return;
        }

        Platform::Log(
            Platform::LogLevel::Info,
            "VulkanPipelineCache[%s]: saved %llu bytes",
            OwnerName.c_str(),
            static_cast<unsigned long long>(payload.size()));
    }

    void Destroy()
    {
        if (Device != VK_NULL_HANDLE && Cache != VK_NULL_HANDLE)
            vkDestroyPipelineCache(Device, Cache, nullptr);

        Cache = VK_NULL_HANDLE;
        PhysicalDevice = VK_NULL_HANDLE;
        Device = VK_NULL_HANDLE;
        ExpectedHeader = {};
        OwnerName.clear();
        CacheFileName.clear();
        CacheFilePath.clear();
    }

private:
    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    VkDevice Device = VK_NULL_HANDLE;
    VkPipelineCache Cache = VK_NULL_HANDLE;
    DiskHeader ExpectedHeader{};
    std::string OwnerName;
    std::string CacheFileName;
    std::string CacheFilePath;
};

} // namespace melonDS
