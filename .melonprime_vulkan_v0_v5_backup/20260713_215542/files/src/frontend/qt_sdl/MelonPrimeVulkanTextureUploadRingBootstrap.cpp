#include "MelonPrimeVulkanTextureUploadRingBootstrap.h"

#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSurface>
#include <QString>
#include <QVulkanDeviceFunctions>
#include <QVulkanFunctions>
#include <QVulkanInstance>
#include <QWindow>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include "GPU3D_TexcacheVulkan.h"
#include "MelonPrimeVulkanFeatureCheck.h"
#include "MelonPrimeVulkanInstanceHost.h"
#include "Platform.h"
#include "main.h"

namespace MelonPrime::Vulkan
{
namespace
{

using melonDS::Vulkan::AlignVulkanTextureUploadOffset;
using melonDS::Vulkan::ReserveVulkanTextureUpload;
using melonDS::Vulkan::RetireVulkanTextureUploads;
using melonDS::Vulkan::ValidateVulkanTextureUploadFlushRange;
using melonDS::Vulkan::VulkanTextureUploadReservation;
using melonDS::Vulkan::VulkanTextureUploadRingConfig;
using melonDS::Vulkan::VulkanTextureUploadRingState;

constexpr std::uint32_t kSubmissionCount = 4;

struct BufferResource
{
    VkBuffer Buffer = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkDeviceSize Size = 0;
    VkMemoryPropertyFlags MemoryFlags = 0;
};

struct UploadSample
{
    std::uint64_t Serial = 0;
    std::uint64_t RingOffset = 0;
    std::uint64_t Size = 0;
    std::uint64_t FlushOffset = 0;
    std::uint64_t FlushSize = 0;
    bool Wrapped = false;
    bool Matched = false;
};

struct ProbeResult
{
    bool Passed = false;
    bool PersistentMappingEstablished = false;
    bool FlushRangesValidated = false;
    bool FlushCallsSubmitted = false;
    bool InitialOverlapRejected = false;
    bool FenceRetirementIntegrated = false;
    bool WrappedAfterRetirement = false;
    bool RetiredSpaceReused = false;
    bool DeviceLocalBufferUsed = false;
    bool ReadbackCompleted = false;
    bool PayloadMatched = false;
    bool NonCoherentMemorySelected = false;
    std::uint64_t RingCapacity = 0;
    std::uint64_t CopyAlignment = 0;
    std::uint64_t NonCoherentAtomSize = 0;
    std::uint64_t ReservationCount = 0;
    std::uint64_t WrapCount = 0;
    std::uint64_t ReuseCount = 0;
    std::uint64_t RejectedOverlapCount = 0;
    std::string FailureStage;
    VkResult FailureResult = VK_SUCCESS;
    std::vector<UploadSample> Samples;
};

std::uint64_t GreatestCommonDivisor(std::uint64_t left, std::uint64_t right)
{
    while (right != 0u)
    {
        const std::uint64_t remainder = left % right;
        left = right;
        right = remainder;
    }
    return left;
}

std::uint64_t LeastCommonMultiple(std::uint64_t left, std::uint64_t right)
{
    left = std::max<std::uint64_t>(left, 1u);
    right = std::max<std::uint64_t>(right, 1u);
    return (left / GreatestCommonDivisor(left, right)) * right;
}

std::vector<std::uint8_t> MakePayload(std::size_t size, std::uint8_t seed)
{
    std::vector<std::uint8_t> bytes(size);
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::uint8_t>(seed + (i * 37u + i / 3u) % 211u);
    return bytes;
}

class TextureUploadRingProbe
{
public:
    TextureUploadRingProbe(std::shared_ptr<DeviceContext> context, QWindow* window)
        : Context(std::move(context)), Window(window)
    {
        if (Context)
        {
            Device = Context->device();
            Functions = Context->functions();
        }
    }

    ~TextureUploadRingProbe() { Destroy(); }

    ProbeResult Run()
    {
        ProbeResult result;
        if (!CreateResources(result) || !Execute(result) || !Readback(result))
        {
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            return result;
        }
        result.ReservationCount = Ring.ReservationCount;
        result.WrapCount = Ring.WrapCount;
        result.ReuseCount = Ring.ReuseCount;
        result.RejectedOverlapCount = Ring.RejectedOverlapCount;
        result.Passed = result.PersistentMappingEstablished &&
            result.FlushRangesValidated && result.FlushCallsSubmitted &&
            result.InitialOverlapRejected && result.FenceRetirementIntegrated &&
            result.WrappedAfterRetirement && result.RetiredSpaceReused &&
            result.DeviceLocalBufferUsed && result.ReadbackCompleted &&
            result.PayloadMatched && Ring.ReservationCount == kSubmissionCount &&
            Ring.WrapCount == 1u && Ring.ReuseCount == 1u &&
            Ring.RejectedOverlapCount == 1u;
        return result;
    }

private:
    bool Fail(const char* stage, VkResult result)
    {
        FailureStage = stage;
        FailureResult = result;
        return false;
    }

    std::uint32_t FindMemoryType(
        std::uint32_t bits,
        VkMemoryPropertyFlags required,
        VkMemoryPropertyFlags preferred,
        bool preferNonCoherent,
        VkMemoryPropertyFlags& selectedFlags) const
    {
        VkPhysicalDeviceMemoryProperties properties{};
        Window->vulkanInstance()->functions()->vkGetPhysicalDeviceMemoryProperties(
            Context->physicalDevice(), &properties);
        auto matches = [&](std::uint32_t index, bool strictPreferred,
                           bool requireNonCoherent)
        {
            const auto flags = properties.memoryTypes[index].propertyFlags;
            if ((bits & (1u << index)) == 0 || (flags & required) != required)
                return false;
            if (strictPreferred && (flags & preferred) != preferred)
                return false;
            if (requireNonCoherent &&
                (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0)
                return false;
            return true;
        };
        for (int pass = 0; pass < 4; ++pass)
        {
            const bool strictPreferred = pass < 2;
            const bool requireNonCoherent = preferNonCoherent && (pass % 2 == 0);
            for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i)
            {
                if (matches(i, strictPreferred, requireNonCoherent))
                {
                    selectedFlags = properties.memoryTypes[i].propertyFlags;
                    return i;
                }
            }
        }
        return std::numeric_limits<std::uint32_t>::max();
    }

    bool CreateBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags required,
        VkMemoryPropertyFlags preferred,
        bool preferNonCoherent,
        BufferResource& resource)
    {
        resource.Size = size;
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkResult vr = Functions->vkCreateBuffer(Device, &info, nullptr, &resource.Buffer);
        if (vr != VK_SUCCESS) return Fail("vkCreateBuffer", vr);
        VkMemoryRequirements requirements{};
        Functions->vkGetBufferMemoryRequirements(Device, resource.Buffer, &requirements);
        VkMemoryPropertyFlags selectedFlags = 0;
        const std::uint32_t type = FindMemoryType(
            requirements.memoryTypeBits, required, preferred,
            preferNonCoherent, selectedFlags);
        if (type == std::numeric_limits<std::uint32_t>::max())
            return Fail("buffer memory type", VK_ERROR_FEATURE_NOT_PRESENT);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = type;
        vr = Functions->vkAllocateMemory(Device, &allocation, nullptr, &resource.Memory);
        if (vr != VK_SUCCESS) return Fail("vkAllocateMemory(buffer)", vr);
        vr = Functions->vkBindBufferMemory(Device, resource.Buffer, resource.Memory, 0);
        if (vr != VK_SUCCESS) return Fail("vkBindBufferMemory", vr);
        resource.MemoryFlags = selectedFlags;
        return true;
    }

    bool CreateResources(ProbeResult& result)
    {
        VkPhysicalDeviceProperties properties{};
        Window->vulkanInstance()->functions()->vkGetPhysicalDeviceProperties(
            Context->physicalDevice(), &properties);
        AtomSize = std::max<VkDeviceSize>(
            1u, properties.limits.nonCoherentAtomSize);
        CopyAlignment = std::max<VkDeviceSize>(
            16u, properties.limits.optimalBufferCopyOffsetAlignment);
        Quantum = LeastCommonMultiple(AtomSize, CopyAlignment);
        RingCapacity = Quantum * 8u;
        DestinationStride = Quantum * 3u;
        DestinationSize = DestinationStride * kSubmissionCount;
        Ring.Reset({RingCapacity, Quantum, AtomSize});

        if (!CreateBuffer(RingCapacity,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                0, true, Staging))
            return false;
        VkResult vr = Functions->vkMapMemory(
            Device, Staging.Memory, 0, Staging.Size, 0, &MappedStaging);
        if (vr != VK_SUCCESS) return Fail("vkMapMemory(persistent staging)", vr);
        result.PersistentMappingEstablished = MappedStaging != nullptr;
        result.NonCoherentMemorySelected =
            (Staging.MemoryFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0;

        if (!CreateBuffer(DestinationSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                0, false, DeviceLocal))
            return false;
        result.DeviceLocalBufferUsed =
            (DeviceLocal.MemoryFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
        if (!CreateBuffer(DestinationSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                false, ReadbackBuffer))
            return false;

        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex = Context->featureInfo().graphicsQueueFamily;
        vr = Functions->vkCreateCommandPool(Device, &pool, nullptr, &CommandPool);
        if (vr != VK_SUCCESS) return Fail("vkCreateCommandPool", vr);
        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate.commandPool = CommandPool;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = kSubmissionCount;
        vr = Functions->vkAllocateCommandBuffers(Device, &allocate, CommandBuffers.data());
        if (vr != VK_SUCCESS) return Fail("vkAllocateCommandBuffers", vr);
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        for (auto& fence : Fences)
        {
            vr = Functions->vkCreateFence(Device, &fenceInfo, nullptr, &fence);
            if (vr != VK_SUCCESS) return Fail("vkCreateFence", vr);
        }
        result.RingCapacity = RingCapacity;
        result.CopyAlignment = Quantum;
        result.NonCoherentAtomSize = AtomSize;
        return true;
    }

    bool WriteAndFlush(
        const VulkanTextureUploadReservation& reservation,
        const std::vector<std::uint8_t>& payload,
        ProbeResult& result)
    {
        if (!ValidateVulkanTextureUploadFlushRange(Ring, reservation))
            return Fail("invalid non-coherent flush range", VK_ERROR_INITIALIZATION_FAILED);
        result.FlushRangesValidated = true;
        std::memcpy(static_cast<std::uint8_t*>(MappedStaging) + reservation.Offset,
            payload.data(), payload.size());
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = Staging.Memory;
        range.offset = reservation.FlushOffset;
        range.size = reservation.FlushSize;
        const VkResult vr = Functions->vkFlushMappedMemoryRanges(Device, 1, &range);
        if (vr != VK_SUCCESS) return Fail("vkFlushMappedMemoryRanges", vr);
        result.FlushCallsSubmitted = true;
        return true;
    }

    bool SubmitCopy(
        std::uint32_t index,
        const VulkanTextureUploadReservation& reservation)
    {
        VkCommandBuffer command = CommandBuffers[index];
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VkResult vr = Functions->vkBeginCommandBuffer(command, &begin);
        if (vr != VK_SUCCESS) return Fail("vkBeginCommandBuffer", vr);
        VkBufferCopy upload{};
        upload.srcOffset = reservation.Offset;
        upload.dstOffset = DestinationStride * index;
        upload.size = reservation.Size;
        Functions->vkCmdCopyBuffer(command, Staging.Buffer, DeviceLocal.Buffer, 1, &upload);
        VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = DeviceLocal.Buffer;
        barrier.offset = upload.dstOffset;
        barrier.size = upload.size;
        Functions->vkCmdPipelineBarrier(command,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 1, &barrier, 0, nullptr);
        VkBufferCopy readback = upload;
        readback.srcOffset = upload.dstOffset;
        readback.dstOffset = upload.dstOffset;
        Functions->vkCmdCopyBuffer(
            command, DeviceLocal.Buffer, ReadbackBuffer.Buffer, 1, &readback);
        VkBufferMemoryBarrier hostBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        hostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        hostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostBarrier.buffer = ReadbackBuffer.Buffer;
        hostBarrier.offset = readback.dstOffset;
        hostBarrier.size = readback.size;
        Functions->vkCmdPipelineBarrier(command,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 0, nullptr, 1, &hostBarrier, 0, nullptr);
        vr = Functions->vkEndCommandBuffer(command);
        if (vr != VK_SUCCESS) return Fail("vkEndCommandBuffer", vr);
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        vr = Functions->vkQueueSubmit(Context->graphicsQueue(), 1, &submit, Fences[index]);
        return vr == VK_SUCCESS ? true : Fail("vkQueueSubmit", vr);
    }

    bool Execute(ProbeResult& result)
    {
        const std::array<std::uint64_t, kSubmissionCount> sizes{{
            Quantum + 7u,
            Quantum + 11u,
            Quantum * 2u + 13u,
            Quantum + 5u,
        }};
        for (std::uint32_t i = 0; i < 3u; ++i)
        {
            VulkanTextureUploadReservation reservation;
            std::string reason;
            if (!ReserveVulkanTextureUpload(
                    Ring, sizes[i], i + 1u, reservation, &reason))
            {
                FailureStage = reason;
                return false;
            }
            Reservations[i] = reservation;
            Payloads[i] = MakePayload(static_cast<std::size_t>(sizes[i]),
                static_cast<std::uint8_t>(0x20u + i * 0x31u));
            if (!WriteAndFlush(reservation, Payloads[i], result) ||
                !SubmitCopy(i, reservation))
                return false;
        }

        VulkanTextureUploadReservation blocked;
        std::string blockedReason;
        if (ReserveVulkanTextureUpload(
                Ring, sizes[3], 4u, blocked, &blockedReason))
            return Fail("overlap reservation unexpectedly succeeded", VK_ERROR_INITIALIZATION_FAILED);
        result.InitialOverlapRejected =
            blockedReason == "upload ring overlaps an unretired submission";

        VkResult vr = Functions->vkWaitForFences(
            Device, 1, &Fences[0], VK_TRUE, UINT64_MAX);
        if (vr != VK_SUCCESS) return Fail("vkWaitForFences(first serial)", vr);
        RetireVulkanTextureUploads(Ring, 1u);
        result.FenceRetirementIntegrated = Ring.LastRetiredSerial == 1u;

        std::string reason;
        if (!ReserveVulkanTextureUpload(
                Ring, sizes[3], 4u, Reservations[3], &reason))
        {
            FailureStage = reason;
            return false;
        }
        result.WrappedAfterRetirement = Reservations[3].Wrapped;
        result.RetiredSpaceReused = Reservations[3].ReusedRetiredSpace;
        Payloads[3] = MakePayload(static_cast<std::size_t>(sizes[3]), 0xB7u);
        if (!WriteAndFlush(Reservations[3], Payloads[3], result) ||
            !SubmitCopy(3u, Reservations[3]))
            return false;

        vr = Functions->vkWaitForFences(
            Device, kSubmissionCount, Fences.data(), VK_TRUE, UINT64_MAX);
        if (vr != VK_SUCCESS) return Fail("vkWaitForFences(all serials)", vr);
        RetireVulkanTextureUploads(Ring, 4u);
        return true;
    }

    bool Readback(ProbeResult& result)
    {
        void* mapped = nullptr;
        VkResult vr = Functions->vkMapMemory(
            Device, ReadbackBuffer.Memory, 0, ReadbackBuffer.Size, 0, &mapped);
        if (vr != VK_SUCCESS) return Fail("vkMapMemory(readback)", vr);
        if ((ReadbackBuffer.MemoryFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
        {
            VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            range.memory = ReadbackBuffer.Memory;
            range.offset = 0;
            range.size = VK_WHOLE_SIZE;
            vr = Functions->vkInvalidateMappedMemoryRanges(Device, 1, &range);
            if (vr != VK_SUCCESS)
            {
                Functions->vkUnmapMemory(Device, ReadbackBuffer.Memory);
                return Fail("vkInvalidateMappedMemoryRanges", vr);
            }
        }
        result.PayloadMatched = true;
        for (std::uint32_t i = 0; i < kSubmissionCount; ++i)
        {
            UploadSample sample;
            sample.Serial = Reservations[i].SubmissionSerial;
            sample.RingOffset = Reservations[i].Offset;
            sample.Size = Reservations[i].Size;
            sample.FlushOffset = Reservations[i].FlushOffset;
            sample.FlushSize = Reservations[i].FlushSize;
            sample.Wrapped = Reservations[i].Wrapped;
            const auto* actual = static_cast<const std::uint8_t*>(mapped) +
                DestinationStride * i;
            sample.Matched = std::memcmp(
                actual, Payloads[i].data(), Payloads[i].size()) == 0;
            result.PayloadMatched = result.PayloadMatched && sample.Matched;
            result.Samples.push_back(sample);
        }
        Functions->vkUnmapMemory(Device, ReadbackBuffer.Memory);
        result.ReadbackCompleted = true;
        return true;
    }

    void DestroyBuffer(BufferResource& resource)
    {
        if (resource.Buffer) Functions->vkDestroyBuffer(Device, resource.Buffer, nullptr);
        if (resource.Memory) Functions->vkFreeMemory(Device, resource.Memory, nullptr);
        resource = {};
    }

    void Destroy()
    {
        if (!Functions || !Device) return;
        Functions->vkDeviceWaitIdle(Device);
        if (MappedStaging && Staging.Memory)
            Functions->vkUnmapMemory(Device, Staging.Memory);
        MappedStaging = nullptr;
        for (auto fence : Fences)
            if (fence) Functions->vkDestroyFence(Device, fence, nullptr);
        if (CommandPool) Functions->vkDestroyCommandPool(Device, CommandPool, nullptr);
        DestroyBuffer(ReadbackBuffer);
        DestroyBuffer(DeviceLocal);
        DestroyBuffer(Staging);
    }

    std::shared_ptr<DeviceContext> Context;
    QWindow* Window = nullptr;
    VkDevice Device = VK_NULL_HANDLE;
    QVulkanDeviceFunctions* Functions = nullptr;
    BufferResource Staging;
    BufferResource DeviceLocal;
    BufferResource ReadbackBuffer;
    void* MappedStaging = nullptr;
    VkDeviceSize AtomSize = 1;
    VkDeviceSize CopyAlignment = 16;
    VkDeviceSize Quantum = 16;
    VkDeviceSize RingCapacity = 0;
    VkDeviceSize DestinationStride = 0;
    VkDeviceSize DestinationSize = 0;
    VulkanTextureUploadRingState Ring;
    std::array<VulkanTextureUploadReservation, kSubmissionCount> Reservations{};
    std::array<std::vector<std::uint8_t>, kSubmissionCount> Payloads{};
    VkCommandPool CommandPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, kSubmissionCount> CommandBuffers{};
    std::array<VkFence, kSubmissionCount> Fences{};
    std::string FailureStage;
    VkResult FailureResult = VK_SUCCESS;
};

QJsonObject SampleJson(const UploadSample& sample)
{
    return {
        {"serial", static_cast<double>(sample.Serial)},
        {"ring_offset", static_cast<double>(sample.RingOffset)},
        {"size", static_cast<double>(sample.Size)},
        {"flush_offset", static_cast<double>(sample.FlushOffset)},
        {"flush_size", static_cast<double>(sample.FlushSize)},
        {"wrapped", sample.Wrapped},
        {"matched", sample.Matched},
    };
}

QJsonObject ProbeJson(const ProbeResult& result)
{
    QJsonArray samples;
    for (const auto& sample : result.Samples) samples.append(SampleJson(sample));
    return {
        {"passed", result.Passed},
        {"persistent_mapping_established", result.PersistentMappingEstablished},
        {"flush_ranges_validated", result.FlushRangesValidated},
        {"flush_calls_submitted", result.FlushCallsSubmitted},
        {"initial_overlap_rejected", result.InitialOverlapRejected},
        {"fence_retirement_integrated", result.FenceRetirementIntegrated},
        {"wrapped_after_retirement", result.WrappedAfterRetirement},
        {"retired_space_reused", result.RetiredSpaceReused},
        {"device_local_buffer_used", result.DeviceLocalBufferUsed},
        {"readback_completed", result.ReadbackCompleted},
        {"payload_matched", result.PayloadMatched},
        {"non_coherent_memory_selected", result.NonCoherentMemorySelected},
        {"ring_capacity", static_cast<double>(result.RingCapacity)},
        {"copy_alignment", static_cast<double>(result.CopyAlignment)},
        {"non_coherent_atom_size", static_cast<double>(result.NonCoherentAtomSize)},
        {"reservation_count", static_cast<double>(result.ReservationCount)},
        {"wrap_count", static_cast<double>(result.WrapCount)},
        {"reuse_count", static_cast<double>(result.ReuseCount)},
        {"rejected_overlap_count", static_cast<double>(result.RejectedOverlapCount)},
        {"failure_stage", QString::fromStdString(result.FailureStage)},
        {"vk_result", static_cast<int>(result.FailureResult)},
        {"samples", samples},
    };
}

} // namespace

int RunTextureUploadRingHarness(const QString& outputPath, int iterations)
{
    if (iterations <= 0) iterations = 1;
    FeatureInfo lastInfo;
    ProbeResult lastResult;
    QJsonArray results;
    int completed = 0;
    auto& host = static_cast<MelonApplication*>(qApp)->vulkanInstanceHost();
    if (!host.ensureCreated())
    {
        lastResult.FailureStage = host.unavailableReason();
    }
    else
    {
        for (int iteration = 0; iteration < iterations; ++iteration)
        {
            QWindow window;
            window.setSurfaceType(QSurface::VulkanSurface);
            window.setVulkanInstance(&host.instance());
            window.resize(1, 1);
            window.create();
            auto context = CreateDeviceContext(&window, lastInfo);
            if (!context)
            {
                lastResult.FailureStage = lastInfo.unavailableReason;
                window.destroy();
                break;
            }
            {
                TextureUploadRingProbe probe(context, &window);
                lastResult = probe.Run();
            }
            context.reset();
            window.destroy();
            results.append(ProbeJson(lastResult));
            if (!lastResult.Passed) break;
            ++completed;
        }
    }

    const bool passed = completed == iterations;
    melonDS::Platform::Log(
        passed ? melonDS::Platform::LogLevel::Info : melonDS::Platform::LogLevel::Error,
        passed ? "[MelonPrime] Vulkan texture upload ring passed: iterations=%d\n" :
                 "[MelonPrime] Vulkan texture upload ring failed: iterations=%d\n",
        completed);
    const QJsonObject output{
        {"schema_version", 1},
        {"passed", passed},
        {"contract_version", static_cast<int>(
            melonDS::Vulkan::kTextureUploadRingContractVersion)},
        {"requested_iterations", iterations},
        {"completed_iterations", completed},
        {"submission_count", static_cast<int>(kSubmissionCount)},
        {"persistent_mapping_established", lastResult.PersistentMappingEstablished},
        {"flush_ranges_validated", lastResult.FlushRangesValidated},
        {"flush_calls_submitted", lastResult.FlushCallsSubmitted},
        {"non_coherent_memory_selected", lastResult.NonCoherentMemorySelected},
        {"initial_overlap_rejected", lastResult.InitialOverlapRejected},
        {"fence_retirement_integrated", lastResult.FenceRetirementIntegrated},
        {"wrapped_after_retirement", lastResult.WrappedAfterRetirement},
        {"retired_space_reused", lastResult.RetiredSpaceReused},
        {"device_local_buffer_used", lastResult.DeviceLocalBufferUsed},
        {"payload_matched", lastResult.PayloadMatched},
        {"reservation_count", static_cast<double>(lastResult.ReservationCount)},
        {"wrap_count", static_cast<double>(lastResult.WrapCount)},
        {"reuse_count", static_cast<double>(lastResult.ReuseCount)},
        {"rejected_overlap_count", static_cast<double>(lastResult.RejectedOverlapCount)},
        {"texture_decode_integrated", true},
        {"gpu_upload_ring_integrated", true},
        {"timeline_semaphore_integrated", false},
        {"capture_texture_integrated", false},
        {"savestate_integrated", false},
        {"native_ds_polygon_raster_integrated", false},
        {"software_game_rendering_preserved", true},
        {"failure_stage", QString::fromStdString(lastResult.FailureStage)},
        {"vk_result", static_cast<int>(lastResult.FailureResult)},
        {"iterations", results},
    };
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 2;
    file.write(QJsonDocument(output).toJson(QJsonDocument::Indented));
    return passed ? 0 : 1;
}

} // namespace MelonPrime::Vulkan
