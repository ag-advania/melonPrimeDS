#pragma once
#include <string>
#include <utility>
#include <vector>
#include <volk.h>
#include "types.h"
namespace MelonDSAndroid { class VulkanRetroArchFilterChain {public:VulkanRetroArchFilterChain()=default;~VulkanRetroArchFilterChain()=default;VulkanRetroArchFilterChain(const VulkanRetroArchFilterChain&)=delete;VulkanRetroArchFilterChain&operator=(const VulkanRetroArchFilterChain&)=delete;VulkanRetroArchFilterChain(VulkanRetroArchFilterChain&&)noexcept=default;VulkanRetroArchFilterChain&operator=(VulkanRetroArchFilterChain&&)noexcept=default;void shutdown(){}bool configure(const std::string&,melonDS::u32,melonDS::u32,melonDS::u32,melonDS::u32,const std::vector<std::pair<std::string,float>>&){return false;}bool recordFrame(VkCommandBuffer,VkImage,VkImage,melonDS::u64,bool,melonDS::u32){return false;}const std::string&getPresetPath()const{return empty;}melonDS::u32 getSourceWidth()const{return 0;}melonDS::u32 getSourceHeight()const{return 0;}melonDS::u32 getOutputWidth()const{return 0;}melonDS::u32 getOutputHeight()const{return 0;}const std::vector<std::pair<std::string,float>>&getParameterOverrides()const{return params;}private:std::string empty;std::vector<std::pair<std::string,float>>params;}; }
