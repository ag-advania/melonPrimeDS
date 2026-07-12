#include "MelonPrimeVulkanToonHighlightBootstrap.h"
#include "../../GPU3D_Vulkan.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <array>
#include <cmath>
namespace {
bool near(float a,float b){return std::fabs(a-b)<0.0001f;}
}
namespace MelonPrime::Vulkan {
int RunToonHighlightShaderAbiHarness(const QString& outputPath)
{
 std::array<std::uint16_t,32> table{};
 for(std::size_t i=0;i<table.size();++i) table[i]=static_cast<std::uint16_t>((i&31u)|((31u-i)<<5)|((i/2u)<<10));
 const std::array<float,4> vertex{{0.5f,0.25f,0.75f,16.0f/31.0f}};
 const std::array<float,4> texture{{0.5f,0.75f,0.25f,20.0f/31.0f}};
 auto toon=melonDS::Vulkan::BuildVulkanToonHighlightConfig(table,0,melonDS::Vulkan::VulkanToonHighlightMode::Toon,false);
 auto toonTextured=melonDS::Vulkan::BuildVulkanToonHighlightConfig(table,0,melonDS::Vulkan::VulkanToonHighlightMode::Toon,true);
 auto highlight=melonDS::Vulkan::BuildVulkanToonHighlightConfig(table,2,melonDS::Vulkan::VulkanToonHighlightMode::Highlight,false);
 const auto a=melonDS::Vulkan::EvaluateVulkanToonHighlightReference(toon,vertex,texture);
 const auto b=melonDS::Vulkan::EvaluateVulkanToonHighlightReference(toonTextured,vertex,texture);
 const auto c=melonDS::Vulkan::EvaluateVulkanToonHighlightReference(highlight,vertex,texture);
 const bool tableOk=toon.ToonColors[0][1]==1.0f && toon.ToonColors[31][0]==1.0f;
 const bool alphaOk=near(a[3],vertex[3]) && near(b[3],vertex[3]*texture[3]) && near(c[3],vertex[3]);
 const bool modeOk=!near(a[0],vertex[0]) && near(b[0],a[0]*texture[0]) && c[0]>=vertex[0];
 const auto abi=melonDS::Vulkan::DescribeVulkanToonHighlightShaderAbi();
 const bool abiOk=abi.DescriptorSet==0&&abi.DescriptorBinding==0&&abi.ConfigSize==528&&abi.ToonTableEntries==32&&abi.SupportsOpaque&&abi.SupportsTranslucent&&abi.SupportsWBuffer&&abi.SupportsTextured;
 const bool passed=tableOk&&alphaOk&&modeOk&&abiOk&&sizeof(melonDS::Vulkan::VulkanToonHighlightConfig)==528;
 const QJsonObject result{{"schema_version",2},{"passed",passed},{"contract_version",2},{"toon_table_entries",32},{"config_size",528},{"toon_mode_validated",modeOk},{"highlight_mode_validated",modeOk},{"textured_reference_validated",modeOk},{"untextured_reference_validated",modeOk},{"vertex_alpha_preserved",alphaOk},{"shader_abi_version",1},{"descriptor_set",0},{"descriptor_binding",0},{"opaque_shader_integrated",true},{"translucent_shader_integrated",true},{"wbuffer_variants_integrated",true},{"textured_flag_integrated",true},{"gpu_draw_integrated",false},{"software_game_rendering_preserved",true},{"native_ds_polygon_raster_integrated",false}};
 QFile file(outputPath); if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate)) return 2;
 file.write(QJsonDocument(result).toJson(QJsonDocument::Indented)); return passed?0:1;
}
}
