// common.h — shared types, constants, and per-window Vulkan state
#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <cmath>

struct ImGuiContext;

// ============================================================================
// Constants
// ============================================================================
constexpr int   WINDOW_WIDTH  = 1310;
constexpr int   WINDOW_HEIGHT = 1498;
constexpr float BG_GRAY       = 0.18f;
constexpr int   MAX_FRAMES_IN_FLIGHT = 2;

// ============================================================================
// UI texture pair (one UI element)
// ============================================================================
struct UITexture {
    VkImage        img  = VK_NULL_HANDLE;
    VkDeviceMemory mem  = VK_NULL_HANDLE;
    VkImageView    view = VK_NULL_HANDLE;
    int width = 0, height = 0;
};

struct UIPair {
    std::string name;
    UITexture   rgb;
    UITexture   alpha;
    int         width  = 0;
    int         height = 0;
    float       alphaAvg = 0.0f;   // precomputed average alpha
    float       lumAvg   = 0.0f;   // precomputed avg linear luminance
    VkDescriptorSet uiDescSet = VK_NULL_HANDLE;  // pre-allocated per-pair
};

// ============================================================================
// Per-window Vulkan state
// ============================================================================
struct WindowContext {
    GLFWwindow*     window    = nullptr;
    VkSurfaceKHR    surface   = VK_NULL_HANDLE;
    VkSwapchainKHR  swapchain = VK_NULL_HANDLE;
    VkFormat        swapchainFmt    = VK_FORMAT_B8G8R8A8_UNORM;
    VkColorSpaceKHR swapchainCS     = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkExtent2D      swapchainExt    = {};
    bool            needsSRGBEncode = true;

    std::vector<VkImage>     swapchainImages;
    std::vector<VkImageView> swapchainViews;

    // Linear intermediate
    VkImage        linearImg  = VK_NULL_HANDLE;
    VkDeviceMemory linearMem  = VK_NULL_HANDLE;
    VkImageView    linearView = VK_NULL_HANDLE;

    // Render passes
    VkRenderPass   uiPass      = VK_NULL_HANDLE;
    VkRenderPass   convertPass = VK_NULL_HANDLE;
    VkRenderPass   imguiPass   = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> uiFramebuffers;
    std::vector<VkFramebuffer> convertFramebuffers;
    std::vector<VkFramebuffer> imguiFramebuffers;

    // Pipelines
    VkPipelineLayout uiPipeLayout     = VK_NULL_HANDLE;
    VkPipeline       uiPipeline       = VK_NULL_HANDLE;
    VkPipelineLayout convertPipeLayout = VK_NULL_HANDLE;
    VkPipeline       convertPipeline  = VK_NULL_HANDLE;

    // Descriptors
    VkDescriptorSetLayout uiDescLayout = VK_NULL_HANDLE;
    VkSampler             texSampler   = VK_NULL_HANDLE;

    // Sync
    std::vector<VkSemaphore> imageAvail;
    std::vector<VkSemaphore> renderDone;
    std::vector<VkFence>     inFlight;
    uint32_t currentFrame = 0;

    // Command
    VkCommandPool              cmdPool;
    std::vector<VkCommandBuffer> cmdBufs;

    // ImGui
    VkDescriptorPool imguiDescPool = VK_NULL_HANDLE;
    ImGuiContext*    imguiCtx = nullptr;
    bool             hasImGui = false;

    // Pre-allocated convert descriptor set
    VkDescriptorSet  convertDescSet = VK_NULL_HANDLE;
};

// ============================================================================
// Shared Vulkan core
// ============================================================================
struct VulkanCore {
    VkInstance       instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice         device = VK_NULL_HANDLE;
    VkQueue          graphicsQueue = VK_NULL_HANDLE;
    uint32_t         graphicsFamily = 0;
    VkCommandPool    sharedCmdPool = VK_NULL_HANDLE;
    VkDescriptorPool uiDescPool = VK_NULL_HANDLE;         // pool for UI descriptors
    VkDescriptorSetLayout uiDescLayout = VK_NULL_HANDLE;  // layout for UI textures
    VkSampler        texSampler     = VK_NULL_HANDLE;     // nearest (UI)
    VkSampler        texSamplerLin  = VK_NULL_HANDLE;     // linear (intermediate)

    bool hdrSupported = false;
};

// ============================================================================
// Utility
// ============================================================================
static uint32_t findMemoryType(VkPhysicalDevice phy, uint32_t typeFilter,
                                VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phy, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((typeFilter & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    throw std::runtime_error("No suitable memory type");
}

static VkShaderModule createShaderModule(VkDevice dev, const std::vector<char>& code) {
    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    ci.pCode    = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule mod;
    vkCreateShaderModule(dev, &ci, nullptr, &mod);
    return mod;
}

static std::vector<char> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open: " + path);
    size_t size = file.tellg();
    std::vector<char> buf(size);
    file.seekg(0);
    file.read(buf.data(), size);
    return buf;
}

VkImageView createImageView(VkDevice dev, VkImage img, VkFormat fmt,
                             VkImageAspectFlags aspect);

void createImage(VkDevice dev, VkPhysicalDevice phy,
                 uint32_t w, uint32_t h, VkFormat fmt,
                 VkImageUsageFlags usage, VkImage& img, VkDeviceMemory& mem);

void transitionLayout(VkDevice dev, VkCommandPool cmdPool, VkQueue queue,
                      VkImage img, VkFormat fmt,
                      VkImageLayout oldL, VkImageLayout newL);

void loadAssets(struct VulkanCore& core, std::vector<UIPair>& uiPairs,
                const std::string& assetPath);