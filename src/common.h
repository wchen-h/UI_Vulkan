#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <cmath>

struct ImGuiContext;

constexpr int   WINDOW_WIDTH  = 655;
constexpr int   WINDOW_HEIGHT = 749;
constexpr float BG_GRAY       = 0.18f;
constexpr int   MAX_FRAMES_IN_FLIGHT = 2;
constexpr float UI_REFERENCE_DPI = 96.0f;
constexpr float PAPER_WHITE_NIT = 350.0f;

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
    float       alphaAvg = 0.0f;
    float       lumAvg   = 0.0f;
    VkDescriptorSet uiDescSet = VK_NULL_HANDLE;
    std::vector<uint8_t> rawRGBA;   // sRGB RGBA, 4 bytes/pixel (for CPU-side pipeline simulation)
    std::vector<uint8_t> rawAlpha; // alpha, 1 byte/pixel
};

struct WindowContext {
    GLFWwindow*     window    = nullptr;
    VkSurfaceKHR    surface   = VK_NULL_HANDLE;
    VkSwapchainKHR  swapchain = VK_NULL_HANDLE;
    VkFormat        swapchainFmt    = VK_FORMAT_B8G8R8A8_UNORM;
    VkColorSpaceKHR swapchainCS     = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkExtent2D      swapchainExt    = {};
    bool            needsSRGBEncode = true;
    int             physWidth_mm  = 0;
    int             physHeight_mm = 0;
    float           pxPerMm       = 0.0f;   // monitor pixel density (px per mm)
    bool            framebufferResized = false;

    std::vector<VkImage>     swapchainImages;
    std::vector<VkImageView> swapchainViews;

    VkImage        linearImg  = VK_NULL_HANDLE;
    VkDeviceMemory linearMem  = VK_NULL_HANDLE;
    VkImageView    linearView = VK_NULL_HANDLE;

    VkRenderPass   uiPass      = VK_NULL_HANDLE;
    VkRenderPass   convertPass = VK_NULL_HANDLE;
    VkRenderPass   imguiPass   = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> uiFramebuffers;
    std::vector<VkFramebuffer> convertFramebuffers;
    std::vector<VkFramebuffer> imguiFramebuffers;

    VkPipelineLayout uiPipeLayout     = VK_NULL_HANDLE;
    VkPipeline       uiPipeline       = VK_NULL_HANDLE;
    VkPipelineLayout convertPipeLayout = VK_NULL_HANDLE;
    VkPipeline       convertPipeline  = VK_NULL_HANDLE;

    VkDescriptorSetLayout convertDescLayout = VK_NULL_HANDLE;
    VkSampler             convertSampler    = VK_NULL_HANDLE;
    VkDescriptorSet       convertDescSet    = VK_NULL_HANDLE;

    std::vector<VkSemaphore> imageAvail;
    std::vector<VkSemaphore> renderDone;
    std::vector<VkFence>     inFlight;
    uint32_t currentFrame = 0;

    VkCommandPool              cmdPool;
    std::vector<VkCommandBuffer> cmdBufs;

    VkDescriptorPool imguiDescPool = VK_NULL_HANDLE;
    ImGuiContext*    imguiCtx = nullptr;
    bool             hasImGui = false;
};

struct VulkanCore {
    VkInstance       instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice         device = VK_NULL_HANDLE;
    VkQueue          graphicsQueue = VK_NULL_HANDLE;
    uint32_t         graphicsFamily = 0;
    VkCommandPool    sharedCmdPool = VK_NULL_HANDLE;
    VkDescriptorPool uiDescPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout uiDescLayout = VK_NULL_HANDLE;
    VkSampler        texSampler     = VK_NULL_HANDLE;
    bool             hdrSupported = false;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
};