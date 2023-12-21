#include <vulkan/vulkan.h>
#include <stdlib.h>

#include "imgui_impl_vulkan.h"

struct TextureData
{
    VkDescriptorSet DS;         // Descriptor set: this is what you'll pass to Image()
    int             Width;
    int             Height;
    int             Channels;

    // Need to keep track of these to properly cleanup
    VkImageView     ImageView;
    VkImage         Image;
    VkDeviceMemory  ImageMemory;
    VkSampler       Sampler;
    VkBuffer        UploadBuffer;
    VkDeviceMemory  UploadBufferMemory;

    void* map;

    TextureData() { memset(this, 0, sizeof(*this)); }
};

class VulkanInterface {

 public:
    VkAllocationCallbacks*   g_Allocator = NULL;
    VkInstance               g_Instance = VK_NULL_HANDLE;
    VkPhysicalDevice         g_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice                 g_Device = VK_NULL_HANDLE;
    uint32_t                 g_QueueFamily = (uint32_t)-1;
    VkQueue                  g_Queue = VK_NULL_HANDLE;
    VkDebugReportCallbackEXT g_DebugReport = VK_NULL_HANDLE;
    VkPipelineCache          g_PipelineCache = VK_NULL_HANDLE;
    VkDescriptorPool         g_DescriptorPool = VK_NULL_HANDLE;

    ImGui_ImplVulkanH_Window g_MainWindowData;
    int                      g_MinImageCount = 2;
    bool                     g_SwapChainRebuild = false;

public:
    ImGui_ImplVulkan_InitInfo makeInfo();
    void SetupVulkan(const char** extensions, uint32_t extensions_count);
    void SetupVulkanWindow(ImGui_ImplVulkanH_Window* wd, VkSurfaceKHR surface, int width, int height);
    void CleanupVulkan();
    void CleanupVulkanWindow();
    void FrameRender(ImGui_ImplVulkanH_Window* wd, ImDrawData* draw_data);
    void FramePresent(ImGui_ImplVulkanH_Window* wd);
    uint32_t findMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties);
    bool LoadTextureFromData(TextureData* tex_data, void* image_data, int width, int height);
    void RemoveTexture(TextureData* tex_data);
    void UpdateTexture(TextureData* tex_data, void* data, int image_size);

};

static void check_vk_result(VkResult err);
