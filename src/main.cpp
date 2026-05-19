// UI_Vulkan — SDR UI 渲染对比工具
// Vulkan 1.3 + Dear ImGui + GLFW3
// 功能：单 UI 居中绘制、alpha 混合、参数调节
//
// 渲染管线：
//   1. 线性中间缓冲 (R16G16B16A16_SFLOAT)：背景 + UI 混合
//   2. sRGB 编码后写 swapchain (R8G8B8A8_SRGB)
//   3. ImGui 叠加控件

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

// Dear ImGui
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

// Image loading
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <set>
#include <map>

namespace fs = std::filesystem;

// ============================================================================
// 常量
// ============================================================================
constexpr int   WINDOW_WIDTH  = 2620;  // SDR(1310) + HDR(1310)
constexpr int   WINDOW_HEIGHT = 1498;
constexpr float BG_GRAY       = 0.18f;   // 18% 灰度 (linear 域)

const std::string ASSET_PATH = ASSET_DIR;

// 验证层
const std::vector<const char*> VALIDATION_LAYERS = {
    "VK_LAYER_KHRONOS_validation"
};
#ifdef NDEBUG
constexpr bool ENABLE_VALIDATION = false;
#else
constexpr bool ENABLE_VALIDATION = true;
#endif

// ============================================================================
// Vulkan 辅助函数
// ============================================================================
static std::vector<char> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open: " + path);
    size_t size = file.tellg();
    std::vector<char> buf(size);
    file.seekg(0);
    file.read(buf.data(), size);
    return buf;
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

static uint32_t findMemoryType(VkPhysicalDevice phy, uint32_t typeFilter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phy, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((typeFilter & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    throw std::runtime_error("No suitable memory type");
}

// ============================================================================
// VulkanApp — 主渲染类
// ============================================================================
class VulkanApp {
public:
    VulkanApp();
    ~VulkanApp();
    void run();

private:
    // 初始化步骤
    void initGLFW();
    void initVulkan();
    void initImGui();
    void loadAssets();
    void createRenderPasses();
    void createFramebuffers();
    void createPipelines();
    void createSyncObjects();
    void createCommandPool();

    // 每帧
    void drawFrame();
    void recordUIPass(VkCommandBuffer cmd, uint32_t imageIdx);
    void recordSRGBPass(VkCommandBuffer cmd, uint32_t imageIdx);
    void recordImGuiPass(VkCommandBuffer cmd, uint32_t imageIdx);

    // 资源创建
    void createImage(uint32_t w, uint32_t h, VkFormat fmt,
                     VkImageUsageFlags usage, VkImage& img, VkDeviceMemory& mem);
    VkImageView createImageView(VkImage img, VkFormat fmt, VkImageAspectFlags aspect);
    void uploadTexture(int texWidth, int texHeight, VkFormat fmt,
                       const void* data, VkImage& img, VkDeviceMemory& mem, VkImageView& view);
    void transitionLayout(VkImage img, VkFormat fmt,
                          VkImageLayout oldL, VkImageLayout newL);

    // 工具
    VkCommandBuffer beginSingleCmd();
    void endSingleCmd(VkCommandBuffer cmd);
    void updateUIDescriptor(int idx);

    // ========== Vulkan 对象 ==========
    GLFWwindow*       window_       = nullptr;
    VkInstance        instance_     = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_ = VK_NULL_HANDLE;
    VkSurfaceKHR      surface_      = VK_NULL_HANDLE;
    VkPhysicalDevice  physicalDev_  = VK_NULL_HANDLE;
    VkDevice          device_       = VK_NULL_HANDLE;

    VkQueue           graphicsQueue_ = VK_NULL_HANDLE;
    uint32_t          graphicsFamily_ = 0;

    VkSwapchainKHR    swapchain_     = VK_NULL_HANDLE;
    VkFormat          swapchainFmt_    = VK_FORMAT_B8G8R8A8_UNORM;
    VkColorSpaceKHR   swapchainCS_     = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    bool              needsSRGBEncode_ = true;  // 如果 swapchain 非 sRGB 格式，shader 做软编码
    VkExtent2D        swapchainExt_  = {};
    std::vector<VkImage>     swapchainImages_;
    std::vector<VkImageView> swapchainViews_;

    // 线性中间缓冲
    VkImage        linearImg_  = VK_NULL_HANDLE;
    VkDeviceMemory linearMem_  = VK_NULL_HANDLE;
    VkImageView    linearView_ = VK_NULL_HANDLE;
    VkFormat       linearFmt_  = VK_FORMAT_R16G16B16A16_SFLOAT;

    // 渲染通道
    VkRenderPass   uiRenderPass_    = VK_NULL_HANDLE;  // → linearImg
    VkRenderPass   srgbRenderPass_  = VK_NULL_HANDLE;  // → swapchain
    VkRenderPass   imguiRenderPass_ = VK_NULL_HANDLE;  // → swapchain (overlay)

    std::vector<VkFramebuffer> uiFramebuffers_;
    std::vector<VkFramebuffer> srgbFramebuffers_;
    std::vector<VkFramebuffer> imguiFramebuffers_;

    // 管线
    VkPipelineLayout uiPipeLayout_   = VK_NULL_HANDLE;
    VkPipeline       uiPipeline_     = VK_NULL_HANDLE;
    VkPipelineLayout srgbPipeLayout_ = VK_NULL_HANDLE;
    VkPipeline       srgbPipeline_   = VK_NULL_HANDLE;

    // 描述符
    VkDescriptorPool      descPool_      = VK_NULL_HANDLE;
    VkDescriptorSetLayout uiDescLayout_  = VK_NULL_HANDLE;
    VkDescriptorSetLayout srgbDescLayout_= VK_NULL_HANDLE;
    VkDescriptorSet       srgbDescSet_   = VK_NULL_HANDLE;
    VkSampler             sampler_       = VK_NULL_HANDLE;  // linear (sRGB pass)
    VkSampler             samplerNear_   = VK_NULL_HANDLE;  // nearest (UI tex)

    VkCommandPool    cmdPool_     = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> cmdBufs_;

    // 同步
    std::vector<VkSemaphore> imageAvail_;
    std::vector<VkSemaphore> uiDone_;
    std::vector<VkFence>     inFlight_;
    uint32_t currentFrame_ = 0;
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    // ========== UI 数据 ==========
    struct UITexture {
        VkImage        img  = VK_NULL_HANDLE;
        VkDeviceMemory mem  = VK_NULL_HANDLE;
        VkImageView    view = VK_NULL_HANDLE;
        int width = 0, height = 0;
    };
    struct UIPair {
        std::string name;
        UITexture rgb;
        UITexture alpha;
        int width, height;
    };
    std::vector<UIPair> uiPairs_;
    int  currentUI_  = 0;
    float uiAlpha_   = 1.0f;

    // 每个 UI 的独立描述符集（不同的纹理）
    std::vector<VkDescriptorSet> uiDescSets_;

    // ========== HDR 数据 ==========
    bool hdrSupported_ = false;               // 设备+显示器支持 HDR 输出
    VkPipelineLayout pqPipeLayout_ = VK_NULL_HANDLE;
    VkPipeline       pqPipeline_   = VK_NULL_HANDLE;
    VkDescriptorSetLayout pqDescLayout_ = VK_NULL_HANDLE;
    VkDescriptorSet       pqDescSet_    = VK_NULL_HANDLE;

    float maxDisplayNit_    = 1000.0f;        // 显示器峰值 nit (用户可改)
    float bgNit_            = 500.0f;         // 背景亮度 nit
    float uiLumNit_         = 500.0f;         // UI 亮度 nit (slider 调节)
    float effAlpha_         = 1.0f;           // 有效不透明度
    bool  phase1Locked_     = false;          // Phase1 基准已锁定?
    float lumResLocked_     = 0.0f;           // 锁定时的 Lum_res

    // UI 平均属性 (每对预计算)
    std::vector<float> uiAlphaAvg_;           // avg(texture alpha)
    std::vector<float> uiLumAvg_;             // avg(linear luminance, alpha-weighted)

    // UI 顶点 quad (unit square)
    VkBuffer        quadVB_ = VK_NULL_HANDLE;
    VkDeviceMemory  quadVBMem_ = VK_NULL_HANDLE;

    // ImGui
    VkDescriptorPool imguiDescPool_ = VK_NULL_HANDLE;
};

// ============================================================================
// 构造 / 析构
// ============================================================================
VulkanApp::VulkanApp() {
    initGLFW();
    initVulkan();
    createCommandPool();     // 纹理上传和 ImGui 字体都需要
    loadAssets();            // 加载 UI 纹理（需要 cmdPool）
    createRenderPasses();    // imguiRenderPass_ 等
    initImGui();             // 依赖 imguiRenderPass_ + swapchain
    createFramebuffers();
    createPipelines();
    createSyncObjects();
}

VulkanApp::~VulkanApp() {
    vkDeviceWaitIdle(device_);

    // ImGui
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (imguiDescPool_) vkDestroyDescriptorPool(device_, imguiDescPool_, nullptr);

    // 描述符
    for (auto& ds : uiDescSets_) vkFreeDescriptorSets(device_, descPool_, 1, &ds);
    if (srgbDescSet_ != VK_NULL_HANDLE) vkFreeDescriptorSets(device_, descPool_, 1, &srgbDescSet_);
    if (descPool_)       vkDestroyDescriptorPool(device_, descPool_, nullptr);
    if (uiDescLayout_)   vkDestroyDescriptorSetLayout(device_, uiDescLayout_, nullptr);
    if (srgbDescLayout_) vkDestroyDescriptorSetLayout(device_, srgbDescLayout_, nullptr);
    if (pqPipeline_)    vkDestroyPipeline(device_, pqPipeline_, nullptr);
    if (pqPipeLayout_)  vkDestroyPipelineLayout(device_, pqPipeLayout_, nullptr);
    if (pqDescLayout_)  vkDestroyDescriptorSetLayout(device_, pqDescLayout_, nullptr);
    if (sampler_)     vkDestroySampler(device_, sampler_, nullptr);
    if (samplerNear_) vkDestroySampler(device_, samplerNear_, nullptr);

    // 管线
    if (uiPipeline_)   vkDestroyPipeline(device_, uiPipeline_, nullptr);
    if (uiPipeLayout_) vkDestroyPipelineLayout(device_, uiPipeLayout_, nullptr);
    if (srgbPipeline_) vkDestroyPipeline(device_, srgbPipeline_, nullptr);
    if (srgbPipeLayout_) vkDestroyPipelineLayout(device_, srgbPipeLayout_, nullptr);

    // 帧缓冲
    for (auto fb : uiFramebuffers_)    vkDestroyFramebuffer(device_, fb, nullptr);
    for (auto fb : srgbFramebuffers_)  vkDestroyFramebuffer(device_, fb, nullptr);
    for (auto fb : imguiFramebuffers_) vkDestroyFramebuffer(device_, fb, nullptr);

    // 交换链
    for (auto v : swapchainViews_) vkDestroyImageView(device_, v, nullptr);
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);

    // 同步
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (imageAvail_[i]) vkDestroySemaphore(device_, imageAvail_[i], nullptr);
        if (uiDone_[i])     vkDestroySemaphore(device_, uiDone_[i], nullptr);
        if (inFlight_[i])   vkDestroyFence(device_, inFlight_[i], nullptr);
    }

    // 纹理
    for (auto& p : uiPairs_) {
        if (p.rgb.view) vkDestroyImageView(device_, p.rgb.view, nullptr);
        if (p.rgb.img)  vkDestroyImage(device_, p.rgb.img, nullptr);
        if (p.rgb.mem)  vkFreeMemory(device_, p.rgb.mem, nullptr);
        if (p.alpha.view) vkDestroyImageView(device_, p.alpha.view, nullptr);
        if (p.alpha.img)  vkDestroyImage(device_, p.alpha.img, nullptr);
        if (p.alpha.mem)  vkFreeMemory(device_, p.alpha.mem, nullptr);
    }
    if (linearView_) vkDestroyImageView(device_, linearView_, nullptr);
    if (linearImg_)  vkDestroyImage(device_, linearImg_, nullptr);
    if (linearMem_)  vkFreeMemory(device_, linearMem_, nullptr);
    if (quadVB_)     vkDestroyBuffer(device_, quadVB_, nullptr);
    if (quadVBMem_)  vkFreeMemory(device_, quadVBMem_, nullptr);

    // 命令
    if (cmdPool_) vkDestroyCommandPool(device_, cmdPool_, nullptr);

    if (device_)   vkDestroyDevice(device_, nullptr);
    if (surface_)  vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (debug_) {
        auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT");
        if (fn) fn(instance_, debug_, nullptr);
    }
    if (instance_) vkDestroyInstance(instance_, nullptr);
    if (window_)   glfwDestroyWindow(window_);
    glfwTerminate();
}

// ============================================================================
// 1. GLFW 窗口
// ============================================================================
void VulkanApp::initGLFW() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window_ = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Vulkan SDR UI Render", nullptr, nullptr);
    glfwSetInputMode(window_, GLFW_STICKY_KEYS, GLFW_TRUE);
}

// ============================================================================
// 2. Vulkan 实例 + 设备 + 交换链
// ============================================================================
void VulkanApp::initVulkan() {
    // --- 实例 ---
    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = "UI_Vulkan";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName        = "NoEngine";
    appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_3;

    // 扩展
    uint32_t glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    std::vector<const char*> exts(glfwExts, glfwExts + glfwExtCount);
    if (ENABLE_VALIDATION) exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    VkInstanceCreateInfo instCI{};
    instCI.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instCI.pApplicationInfo = &appInfo;
    instCI.enabledExtensionCount   = static_cast<uint32_t>(exts.size());
    instCI.ppEnabledExtensionNames = exts.data();
    if (ENABLE_VALIDATION) {
        instCI.enabledLayerCount   = static_cast<uint32_t>(VALIDATION_LAYERS.size());
        instCI.ppEnabledLayerNames = VALIDATION_LAYERS.data();
    }
    vkCreateInstance(&instCI, nullptr, &instance_);

    // 调试回调
    if (ENABLE_VALIDATION) {
        VkDebugUtilsMessengerCreateInfoEXT dbgCI{};
        dbgCI.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dbgCI.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                              | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dbgCI.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                          | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                          | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dbgCI.pfnUserCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT,
                                   VkDebugUtilsMessageTypeFlagsEXT,
                                   const VkDebugUtilsMessengerCallbackDataEXT* pData,
                                   void*) -> VkBool32 {
            std::cerr << "[VK] " << pData->pMessage << std::endl;
            return VK_FALSE;
        };
        auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT");
        if (fn) fn(instance_, &dbgCI, nullptr, &debug_);
    }

    // --- 表面 ---
    glfwCreateWindowSurface(instance_, window_, nullptr, &surface_);

    // --- 物理设备 ---
    uint32_t devCount = 0;
    vkEnumeratePhysicalDevices(instance_, &devCount, nullptr);
    std::vector<VkPhysicalDevice> phys(devCount);
    vkEnumeratePhysicalDevices(instance_, &devCount, phys.data());
    physicalDev_ = phys[0]; // 选第一个
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physicalDev_, &props);
        std::cout << "[GPU] " << props.deviceName << " (Vulkan "
                  << VK_API_VERSION_MAJOR(props.apiVersion) << "."
                  << VK_API_VERSION_MINOR(props.apiVersion) << "."
                  << VK_API_VERSION_PATCH(props.apiVersion) << ")" << std::endl;
    }

    // 队列族
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDev_, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qfProps(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDev_, &qfCount, qfProps.data());
    for (uint32_t i = 0; i < qfCount; ++i) {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDev_, i, surface_, &present);
        if (qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT && present) {
            graphicsFamily_ = i;
            break;
        }
    }

    // --- 逻辑设备 ---
    float qp = 1.0f;
    VkDeviceQueueCreateInfo qCI{};
    qCI.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qCI.queueFamilyIndex = graphicsFamily_;
    qCI.queueCount       = 1;
    qCI.pQueuePriorities = &qp;

    // 查询设备支持的扩展，按需启用
    uint32_t devExtCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDev_, nullptr, &devExtCount, nullptr);
    std::vector<VkExtensionProperties> availExts(devExtCount);
    vkEnumerateDeviceExtensionProperties(physicalDev_, nullptr, &devExtCount, availExts.data());

    auto hasExt = [&](const char* name) {
        for (auto& e : availExts)
            if (strcmp(e.extensionName, name) == 0) return true;
        return false;
    };

    std::vector<const char*> devExts = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };
    if (hasExt(VK_EXT_HDR_METADATA_EXTENSION_NAME)) {
        devExts.push_back(VK_EXT_HDR_METADATA_EXTENSION_NAME);
        std::cout << "[VK] HDR metadata extension supported" << std::endl;
    } else {
        std::cout << "[VK] HDR metadata extension NOT supported — SDR-only mode"
                  << std::endl;
    }

    VkPhysicalDeviceVulkan13Features feat13{};
    feat13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

    VkDeviceCreateInfo devCI{};
    devCI.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    devCI.pNext                   = &feat13;
    devCI.queueCreateInfoCount    = 1;
    devCI.pQueueCreateInfos       = &qCI;
    devCI.enabledExtensionCount   = static_cast<uint32_t>(devExts.size());
    devCI.ppEnabledExtensionNames = devExts.data();
    vkCreateDevice(physicalDev_, &devCI, nullptr, &device_);
    vkGetDeviceQueue(device_, graphicsFamily_, 0, &graphicsQueue_);

    // --- 交换链 ---
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDev_, surface_, &caps);
    swapchainExt_ = caps.currentExtent;
    if (swapchainExt_.width == 0xFFFFFFFF) {
        swapchainExt_.width  = WINDOW_WIDTH;
        swapchainExt_.height = WINDOW_HEIGHT;
    }

    // 查询表面支持的格式
    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDev_, surface_, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> diagFmts(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDev_, surface_, &fmtCount, diagFmts.data());
    std::cout << "[DIAG] Surface formats (" << fmtCount << "):" << std::endl;
    for (auto& sf : diagFmts) {
        const char* fmtName = "?";
        switch (sf.format) {
            case VK_FORMAT_B8G8R8A8_SRGB: fmtName = "B8G8R8A8_SRGB"; break;
            case VK_FORMAT_B8G8R8A8_UNORM: fmtName = "B8G8R8A8_UNORM"; break;
            case VK_FORMAT_R8G8B8A8_SRGB: fmtName = "R8G8B8A8_SRGB"; break;
            case VK_FORMAT_R8G8B8A8_UNORM: fmtName = "R8G8B8A8_UNORM"; break;
            case VK_FORMAT_A2B10G10R10_UNORM_PACK32: fmtName = "A2B10G10R10_UNORM"; break;
            case VK_FORMAT_A2R10G10B10_UNORM_PACK32: fmtName = "A2R10G10B10_UNORM"; break;
            case VK_FORMAT_R16G16B16A16_SFLOAT: fmtName = "R16G16B16A16_SFLOAT"; break;
        }
        const char* csName = "?";
        switch (sf.colorSpace) {
            case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR: csName = "SRGB_NONLINEAR"; break;
            case VK_COLOR_SPACE_HDR10_ST2084_EXT: csName = "HDR10_ST2084"; break;
            case VK_COLOR_SPACE_HDR10_HLG_EXT: csName = "HDR10_HLG"; break;
            case VK_COLOR_SPACE_BT709_NONLINEAR_EXT: csName = "BT709_NONLINEAR"; break;
            case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT: csName = "EXT_SRGB_LINEAR"; break;
        }
        std::cout << "  " << sf.format << " (" << fmtName << ") + " 
                  << sf.colorSpace << " (" << csName << ")" << std::endl;
    }

    // 查询表面支持的格式
    fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDev_, surface_, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> surfaceFmts(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDev_, surface_, &fmtCount, surfaceFmts.data());

    // 优先选 sRGB，回退到 UNORM（着色器已做 sRGB 编码）
    swapchainFmt_ = VK_FORMAT_UNDEFINED;
    swapchainCS_  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    for (auto& sf : surfaceFmts) {
        if (sf.format == VK_FORMAT_B8G8R8A8_SRGB
            && sf.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            swapchainFmt_ = sf.format;
            swapchainCS_  = sf.colorSpace;
            break;
        }
    }
    if (swapchainFmt_ == VK_FORMAT_UNDEFINED) {
        for (auto& sf : surfaceFmts) {
            if (sf.format == VK_FORMAT_B8G8R8A8_UNORM
                && sf.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                swapchainFmt_ = sf.format;
                swapchainCS_  = sf.colorSpace;
                break;
            }
        }
    }
    if (swapchainFmt_ == VK_FORMAT_UNDEFINED) {
        // 最后手段：用第一个支持的格式
        swapchainFmt_ = surfaceFmts[0].format;
        swapchainCS_  = surfaceFmts[0].colorSpace;
    }
    // 仅在 UNORM 时 shader 软编码；SRGB 格式由硬件编码
    needsSRGBEncode_ = (swapchainFmt_ != VK_FORMAT_B8G8R8A8_SRGB
                     && swapchainFmt_ != VK_FORMAT_R8G8B8A8_SRGB);
    // HDR 能力检测
    hdrSupported_ = (hasExt(VK_EXT_HDR_METADATA_EXTENSION_NAME));
    for (auto& sf : surfaceFmts) {
        if (sf.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT
         && (sf.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32
          || sf.format == VK_FORMAT_A2R10G10B10_UNORM_PACK32)) {
            hdrSupported_ = true;
            break;
        }
    }
    std::cout << "[VK] HDR output: " << (hdrSupported_ ? "YES" : "NO")
              << " (need VK_EXT_hdr_metadata + ST2084 surface)" << std::endl;

    std::cout << "[VK] Swapchain format: " << swapchainFmt_
              << (needsSRGBEncode_ ? " (sw sRGB)" : " (hw sRGB)")
              << ", colorspace: " << swapchainCS_ << std::endl;

    // minImageCount: maxImageCount==0 表示无上限
    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR swCI{};
    swCI.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swCI.surface          = surface_;
    swCI.minImageCount    = imageCount;
    swCI.imageFormat      = swapchainFmt_;
    swCI.imageColorSpace  = swapchainCS_;
    swCI.imageExtent      = swapchainExt_;
    swCI.imageArrayLayers = 1;
    swCI.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swCI.preTransform     = caps.currentTransform;
    swCI.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swCI.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
    swCI.clipped          = VK_TRUE;
    vkCreateSwapchainKHR(device_, &swCI, nullptr, &swapchain_);

    uint32_t imgCount = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &imgCount, nullptr);
    swapchainImages_.resize(imgCount);
    swapchainViews_.resize(imgCount);
    vkGetSwapchainImagesKHR(device_, swapchain_, &imgCount, swapchainImages_.data());
    for (uint32_t i = 0; i < imgCount; ++i)
        swapchainViews_[i] = createImageView(swapchainImages_[i], swapchainFmt_,
                                             VK_IMAGE_ASPECT_COLOR_BIT);
}

// ============================================================================
// 3. 图片资源加载
// ============================================================================
void VulkanApp::loadAssets() {
    using namespace fs;
    std::map<std::string, std::string> rgbMap, alphaMap;

    std::cout << "[ASSET] Scanning " << ASSET_PATH << std::endl;
    for (auto& entry : fs::directory_iterator(ASSET_PATH)) {
        std::string fn = entry.path().filename().string();
        if (fn.size() < 4 || fn.substr(fn.size() - 4) != ".png") continue;

        // 解析命名: {id}_{type}_{x}_{y}.png
        auto u1 = fn.find('_');
        auto u2 = fn.find('_', u1 + 1);
        if (u1 == std::string::npos || u2 == std::string::npos) continue;
        std::string type = fn.substr(u1 + 1, u2 - u1 - 1);
        std::string key  = fn.substr(0, u1) + fn.substr(u2); // "1_148_476.png"

        if (type == "rgb")   rgbMap[key]   = fn;
        if (type == "alpha") alphaMap[key] = fn;
    }

    std::cout << "[ASSET] Found " << rgbMap.size() << " RGB, "
              << alphaMap.size() << " Alpha" << std::endl;

    for (auto& [key, rgbFn] : rgbMap) {
        if (!alphaMap.count(key)) continue;
        std::string alphaFn = alphaMap[key];

        std::string rgbPath   = ASSET_PATH + "/" + rgbFn;
        std::string alphaPath = ASSET_PATH + "/" + alphaFn;

        int wR, hR, wA, hA, ch;
        stbi_uc* rgbPx   = stbi_load(rgbPath.c_str(),   &wR, &hR, &ch, 4);
        stbi_uc* alphaPx = stbi_load(alphaPath.c_str(), &wA, &hA, &ch, 1);
        if (!rgbPx || !alphaPx) {
            std::cerr << "[WARN] Failed to load: " << rgbFn << std::endl;
            if (rgbPx) stbi_image_free(rgbPx);
            if (alphaPx) stbi_image_free(alphaPx);
            continue;
        }

        UIPair pair;
        pair.name   = key;
        pair.width  = wR;
        pair.height = hR;

        uploadTexture(wR, hR, VK_FORMAT_R8G8B8A8_SRGB, rgbPx,
                      pair.rgb.img, pair.rgb.mem, pair.rgb.view);
        uploadTexture(wA, hA, VK_FORMAT_R8_UNORM, alphaPx,
                      pair.alpha.img, pair.alpha.mem, pair.alpha.view);

        // 预计算 UI 平均 alpha 和亮度 (HDR 联动需要)
        {
            float sumAlpha = 0.0f, sumLum = 0.0f;
            int countAlpha = 0;
            for (int i = 0; i < wR * hR; ++i) {
                uint8_t a8 = alphaPx[i];
                if (a8 == 0) continue;
                float a = a8 / 255.0f;
                float r = rgbPx[i*4+0] / 255.0f, g = rgbPx[i*4+1] / 255.0f, b = rgbPx[i*4+2] / 255.0f;
                // sRGB→linear (approx)
                auto s2l = [](float c){ return c <= 0.04045f ? c/12.92f : powf((c+0.055f)/1.055f, 2.4f); };
                float lum = 0.2126f * s2l(r) + 0.7152f * s2l(g) + 0.0722f * s2l(b);
                sumAlpha += a;
                sumLum   += lum * a;
                countAlpha++;
            }
            uiAlphaAvg_.push_back(countAlpha > 0 ? sumAlpha / (wR * hR) : 0.001f);
            uiLumAvg_.push_back(sumAlpha > 0 ? sumLum / sumAlpha : 0.01f);
            std::cout << "[AVG] " << key << " alpha_avg=" << uiAlphaAvg_.back()
                      << " lum_avg=" << uiLumAvg_.back() << std::endl;
        }

        stbi_image_free(rgbPx);
        stbi_image_free(alphaPx);

        uiPairs_.push_back(std::move(pair));
    }

    std::cout << "[ASSET] Loaded " << uiPairs_.size() << " UI pairs." << std::endl;

    // UI 顶点 quad (unit square, bottom-left origin, UV matches)
    // pos(0,0)=bottom-left → uv(0,1) texture bottom-left
    // pos(1,1)=top-right   → uv(1,0) texture top-right
    float quad[] = {
        0,0, 0,1,  1,0, 1,1,  1,1, 1,0,
        0,0, 0,1,  1,1, 1,0,  0,1, 0,0,
    };

    VkBufferCreateInfo vbCI{};
    vbCI.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vbCI.size        = sizeof(quad);
    vbCI.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vbCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(device_, &vbCI, nullptr, &quadVB_);

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device_, quadVB_, &mr);
    VkMemoryAllocateInfo ma{};
    ma.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ma.allocationSize  = mr.size;
    ma.memoryTypeIndex = findMemoryType(physicalDev_, mr.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                                      | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(device_, &ma, nullptr, &quadVBMem_);
    vkBindBufferMemory(device_, quadVB_, quadVBMem_, 0);
    void* mapped;
    vkMapMemory(device_, quadVBMem_, 0, sizeof(quad), 0, &mapped);
    memcpy(mapped, quad, sizeof(quad));
    vkUnmapMemory(device_, quadVBMem_);
}

// ============================================================================
// 纹理上传
// ============================================================================
void VulkanApp::uploadTexture(int w, int h, VkFormat fmt, const void* data,
                               VkImage& img, VkDeviceMemory& mem, VkImageView& view) {
    VkDeviceSize size = w * h * (fmt == VK_FORMAT_R8_UNORM ? 1 : 4);

    // Staging buffer
    VkBuffer stage;
    VkDeviceMemory stageMem;
    {
        VkBufferCreateInfo ci{};
        ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ci.size        = size;
        ci.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(device_, &ci, nullptr, &stage);
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(device_, stage, &mr);
        VkMemoryAllocateInfo ma{};
        ma.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ma.allocationSize = mr.size;
        ma.memoryTypeIndex = findMemoryType(physicalDev_, mr.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                                          | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(device_, &ma, nullptr, &stageMem);
        vkBindBufferMemory(device_, stage, stageMem, 0);
        void* mapped;
        vkMapMemory(device_, stageMem, 0, size, 0, &mapped);
        memcpy(mapped, data, size);
        vkUnmapMemory(device_, stageMem);
    }

    // Image
    createImage(w, h, fmt,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                img, mem);
    transitionLayout(img, fmt, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Copy
    VkCommandBuffer cmd = beginSingleCmd();
    VkBufferImageCopy region{};
    region.bufferOffset      = 0;
    region.bufferRowLength   = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent       = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
    vkCmdCopyBufferToImage(cmd, stage, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    endSingleCmd(cmd);

    transitionLayout(img, fmt, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    view = createImageView(img, fmt, VK_IMAGE_ASPECT_COLOR_BIT);

    // Cleanup staging
    vkDestroyBuffer(device_, stage, nullptr);
    vkFreeMemory(device_, stageMem, nullptr);
}

void VulkanApp::createImage(uint32_t w, uint32_t h, VkFormat fmt,
                             VkImageUsageFlags usage, VkImage& img, VkDeviceMemory& mem) {
    VkImageCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType     = VK_IMAGE_TYPE_2D;
    ci.extent        = {w, h, 1};
    ci.mipLevels     = 1;
    ci.arrayLayers   = 1;
    ci.format        = fmt;
    ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ci.usage         = usage;
    ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    vkCreateImage(device_, &ci, nullptr, &img);

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(device_, img, &mr);
    VkMemoryAllocateInfo ma{};
    ma.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ma.allocationSize = mr.size;
    ma.memoryTypeIndex = findMemoryType(physicalDev_, mr.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(device_, &ma, nullptr, &mem);
    vkBindImageMemory(device_, img, mem, 0);
}

VkImageView VulkanApp::createImageView(VkImage img, VkFormat fmt, VkImageAspectFlags aspect) {
    VkImageViewCreateInfo ci{};
    ci.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ci.image      = img;
    ci.viewType   = VK_IMAGE_VIEW_TYPE_2D;
    ci.format     = fmt;
    ci.subresourceRange = {aspect, 0, 1, 0, 1};
    VkImageView view;
    vkCreateImageView(device_, &ci, nullptr, &view);
    return view;
}

void VulkanApp::transitionLayout(VkImage img, VkFormat fmt,
                                  VkImageLayout oldL, VkImageLayout newL) {
    VkCommandBuffer cmd = beginSingleCmd();
    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = oldL;
    barrier.newLayout           = newL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = img;
    barrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkPipelineStageFlags srcStage, dstStage;
    if (oldL == VK_IMAGE_LAYOUT_UNDEFINED && newL == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldL == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
               && newL == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        srcStage = dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    endSingleCmd(cmd);
}

// ============================================================================
// 辅助命令
// ============================================================================
VkCommandBuffer VulkanApp::beginSingleCmd() {
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandPool        = cmdPool_;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device_, &ai, &cmd);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

void VulkanApp::endSingleCmd(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    vkQueueSubmit(graphicsQueue_, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue_);
    vkFreeCommandBuffers(device_, cmdPool_, 1, &cmd);
}

// ============================================================================
// 渲染通道
// ============================================================================
void VulkanApp::createRenderPasses() {
    // --- Pass 1: UI 绘制 → 线性中间缓冲 ---
    {
        VkAttachmentDescription colorAtt{};
        colorAtt.format         = linearFmt_;
        colorAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
        colorAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // manual clear via vkCmdClearAttachments
        colorAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAtt.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription sub{};
        sub.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments    = &colorRef;

        VkRenderPassCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = 1;
        ci.pAttachments    = &colorAtt;
        ci.subpassCount    = 1;
        ci.pSubpasses      = &sub;
        vkCreateRenderPass(device_, &ci, nullptr, &uiRenderPass_);
    }

    // --- Pass 2: linear → sRGB 转换 → swapchain ---
    {
        VkAttachmentDescription colorAtt{};
        colorAtt.format         = swapchainFmt_;
        colorAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
        colorAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAtt.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription sub{};
        sub.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments    = &colorRef;

        VkRenderPassCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = 1;
        ci.pAttachments    = &colorAtt;
        ci.subpassCount    = 1;
        ci.pSubpasses      = &sub;
        vkCreateRenderPass(device_, &ci, nullptr, &srgbRenderPass_);
    }
    

    // --- Pass 3: ImGui 叠加 → swapchain ---
    {
        VkAttachmentDescription colorAtt{};
        colorAtt.format         = swapchainFmt_;
        colorAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
        colorAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;    // 保留 sRGB pass 内容
        colorAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAtt.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription sub{};
        sub.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments    = &colorRef;

        VkRenderPassCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = 1;
        ci.pAttachments    = &colorAtt;
        ci.subpassCount    = 1;
        ci.pSubpasses      = &sub;
        vkCreateRenderPass(device_, &ci, nullptr, &imguiRenderPass_);
    }
}

// ============================================================================
// 帧缓冲
// ============================================================================
void VulkanApp::createFramebuffers() {
    // 创建线性中间缓冲
    createImage(swapchainExt_.width, swapchainExt_.height, linearFmt_,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                linearImg_, linearMem_);
    linearView_ = createImageView(linearImg_, linearFmt_, VK_IMAGE_ASPECT_COLOR_BIT);

    uint32_t count = static_cast<uint32_t>(swapchainViews_.size());
    uiFramebuffers_.resize(count);
    srgbFramebuffers_.resize(count);
    imguiFramebuffers_.resize(count);

    for (uint32_t i = 0; i < count; ++i) {
        // UI pass → linearImg
        {
            VkFramebufferCreateInfo ci{};
            ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            ci.renderPass      = uiRenderPass_;
            ci.attachmentCount = 1;
            ci.pAttachments    = &linearView_;
            ci.width           = swapchainExt_.width;
            ci.height          = swapchainExt_.height;
            ci.layers          = 1;
            vkCreateFramebuffer(device_, &ci, nullptr, &uiFramebuffers_[i]);
        }
        // sRGB pass → swapchain
        {
            VkFramebufferCreateInfo ci{};
            ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            ci.renderPass      = srgbRenderPass_;
            ci.attachmentCount = 1;
            ci.pAttachments    = &swapchainViews_[i];
            ci.width           = swapchainExt_.width;
            ci.height          = swapchainExt_.height;
            ci.layers          = 1;
            vkCreateFramebuffer(device_, &ci, nullptr, &srgbFramebuffers_[i]);
        }
        // ImGui pass → swapchain
        {
            VkFramebufferCreateInfo ci{};
            ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            ci.renderPass      = imguiRenderPass_;
            ci.attachmentCount = 1;
            ci.pAttachments    = &swapchainViews_[i];
            ci.width           = swapchainExt_.width;
            ci.height          = swapchainExt_.height;
            ci.layers          = 1;
            vkCreateFramebuffer(device_, &ci, nullptr, &imguiFramebuffers_[i]);
        }
    }
}

// ============================================================================
// 管线
// ============================================================================
void VulkanApp::createPipelines() {
    // 采样器
    VkSamplerCreateInfo sampCI{};
    sampCI.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampCI.magFilter    = VK_FILTER_LINEAR;
    sampCI.minFilter    = VK_FILTER_LINEAR;
    sampCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(device_, &sampCI, nullptr, &sampler_);

    // Nearest 采样器 → UI 贴图 1:1 渲染不糊
    sampCI.magFilter = VK_FILTER_NEAREST;
    sampCI.minFilter = VK_FILTER_NEAREST;
    vkCreateSampler(device_, &sampCI, nullptr, &samplerNear_);

    // --- 描述符布局：UI ---
    {
        VkDescriptorSetLayoutBinding bindings[2] = {};
        bindings[0].binding         = 0;
        bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1] = bindings[0];
        bindings[1].binding = 1;

        VkDescriptorSetLayoutCreateInfo ci{};
        ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ci.bindingCount = 2;
        ci.pBindings    = bindings;
        vkCreateDescriptorSetLayout(device_, &ci, nullptr, &uiDescLayout_);
    }

    // --- 描述符布局：sRGB 转换 ---
    {
        VkDescriptorSetLayoutBinding bind{};
        bind.binding         = 0;
        bind.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bind.descriptorCount = 1;
        bind.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo ci{};
        ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ci.bindingCount = 1;
        ci.pBindings    = &bind;
        vkCreateDescriptorSetLayout(device_, &ci, nullptr, &srgbDescLayout_);
    }

    // 描述符池
    {
        VkDescriptorPoolSize sizes[] = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              static_cast<uint32_t>(uiPairs_.size() * 2 + 2) }  // UI*2 + sRGB + PQ
        };
        VkDescriptorPoolCreateInfo ci{};
        ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        ci.maxSets       = static_cast<uint32_t>(uiPairs_.size() + 2);  // +1 srgb +1 pq
        ci.poolSizeCount = 1;
        ci.pPoolSizes    = sizes;
        vkCreateDescriptorPool(device_, &ci, nullptr, &descPool_);
    }

    // 为每个 UI pair 分配独立的描述符集
    uiDescSets_.resize(uiPairs_.size());
    std::vector<VkDescriptorSetLayout> layouts(uiPairs_.size(), uiDescLayout_);
    {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = descPool_;
        ai.descriptorSetCount = static_cast<uint32_t>(uiPairs_.size());
        ai.pSetLayouts        = layouts.data();
        vkAllocateDescriptorSets(device_, &ai, uiDescSets_.data());
    }
    for (size_t i = 0; i < uiPairs_.size(); ++i) {
        VkDescriptorImageInfo imgs[2] = {};
        imgs[0].sampler     = samplerNear_;   // UI 贴图用 nearest，1:1 不糊
        imgs[0].imageView   = uiPairs_[i].rgb.view;
        imgs[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imgs[1].sampler     = samplerNear_;
        imgs[1].imageView   = uiPairs_[i].alpha.view;
        imgs[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet writes[2] = {};
        writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet          = uiDescSets_[i];
        writes[0].dstBinding      = 0;
        writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo      = &imgs[0];
        writes[1] = writes[0];
        writes[1].dstBinding = 1;
        writes[1].pImageInfo = &imgs[1];
        vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);
    }

    // sRGB 转换描述符集
    {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = descPool_;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &srgbDescLayout_;
        vkAllocateDescriptorSets(device_, &ai, &srgbDescSet_);

        VkDescriptorImageInfo imgInfo{};
        imgInfo.sampler     = sampler_;
        imgInfo.imageView   = linearView_;
        imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = srgbDescSet_;
        write.dstBinding      = 0;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo      = &imgInfo;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }

    // 着色器
    auto uiVertCode   = readFile(std::string(SHADER_DIR) + "ui.vert.spv");
    auto uiFragCode   = readFile(std::string(SHADER_DIR) + "ui.frag.spv");
    auto srgbVertCode = readFile(std::string(SHADER_DIR) + "srgb_convert.vert.spv");
    auto srgbFragCode = readFile(std::string(SHADER_DIR) + "srgb_convert.frag.spv");

    auto uiVertMod   = createShaderModule(device_, uiVertCode);
    auto uiFragMod   = createShaderModule(device_, uiFragCode);
    auto srgbVertMod = createShaderModule(device_, srgbVertCode);
    auto srgbFragMod = createShaderModule(device_, srgbFragCode);

    // Push constant ranges
    VkPushConstantRange uiPCR{};
    uiPCR.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    uiPCR.offset     = 0;
    uiPCR.size       = 32; // vec2 offset + vec2 scale + float alpha

    // --- UI 管线 ---
    {
        VkPipelineLayoutCreateInfo plCI{};
        plCI.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plCI.setLayoutCount = 1;
        plCI.pSetLayouts    = &uiDescLayout_;
        plCI.pushConstantRangeCount = 1;
        plCI.pPushConstantRanges    = &uiPCR;
        vkCreatePipelineLayout(device_, &plCI, nullptr, &uiPipeLayout_);

        VkPipelineShaderStageCreateInfo stages[] = {
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_VERTEX_BIT,   uiVertMod, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_FRAGMENT_BIT, uiFragMod, "main", nullptr},
        };

        VkVertexInputBindingDescription vtxBind{};
        vtxBind.binding   = 0;
        vtxBind.stride    = 4 * sizeof(float);  // pos.xy + uv.xy
        vtxBind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription vtxAttrs[2]{};
        vtxAttrs[0].location = 0;
        vtxAttrs[0].binding  = 0;
        vtxAttrs[0].format   = VK_FORMAT_R32G32_SFLOAT;
        vtxAttrs[0].offset   = 0;
        vtxAttrs[1].location = 1;
        vtxAttrs[1].binding  = 0;
        vtxAttrs[1].format   = VK_FORMAT_R32G32_SFLOAT;
        vtxAttrs[1].offset   = 2 * sizeof(float);

        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount   = 1;
        vi.pVertexBindingDescriptions      = &vtxBind;
        vi.vertexAttributeDescriptionCount = 2;
        vi.pVertexAttributeDescriptions    = vtxAttrs;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport vp{};
        vp.x = 0; vp.y = 0;
        vp.width  = (float)swapchainExt_.width;
        vp.height = (float)swapchainExt_.height;
        vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
        VkRect2D scissor{{0,0}, swapchainExt_};

        VkPipelineViewportStateCreateInfo vs{};
        vs.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vs.viewportCount = 1;
        vs.pViewports    = &vp;
        vs.scissorCount  = 1;
        vs.pScissors     = &scissor;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.lineWidth   = 1.0f;
        rs.cullMode    = VK_CULL_MODE_NONE;
        rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState cbAtt{};
        cbAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                             | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1;
        cb.pAttachments    = &cbAtt;

        VkGraphicsPipelineCreateInfo pi{};
        pi.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount          = 2;
        pi.pStages             = stages;
        pi.pVertexInputState   = &vi;
        pi.pInputAssemblyState = &ia;
        pi.pViewportState      = &vs;
        pi.pRasterizationState = &rs;
        pi.pMultisampleState   = &ms;
        pi.pColorBlendState    = &cb;
        VkDynamicState uidyn[] = { VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo uidynCI{};
        uidynCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        uidynCI.dynamicStateCount = 1;
        uidynCI.pDynamicStates = uidyn;
        pi.pDynamicState = &uidynCI;
        pi.layout              = uiPipeLayout_;
        pi.renderPass          = uiRenderPass_;
        pi.subpass             = 0;
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &uiPipeline_);
    }

    // --- sRGB 管线 ---
    {
        VkPushConstantRange srgbPCR{};
        srgbPCR.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        srgbPCR.offset     = 0;
        srgbPCR.size       = sizeof(float);  // uSRGBEncode

        VkPipelineLayoutCreateInfo plCI{};
        plCI.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plCI.setLayoutCount = 1;
        plCI.pSetLayouts    = &srgbDescLayout_;
        plCI.pushConstantRangeCount = 1;
        plCI.pPushConstantRanges    = &srgbPCR;
        vkCreatePipelineLayout(device_, &plCI, nullptr, &srgbPipeLayout_);

        VkPipelineShaderStageCreateInfo stages[] = {
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_VERTEX_BIT,   srgbVertMod, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_FRAGMENT_BIT, srgbFragMod, "main", nullptr},
        };

        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport vp{0,0,(float)swapchainExt_.width,(float)swapchainExt_.height,0,1};
        VkRect2D sc{{0,0}, swapchainExt_};
        VkPipelineViewportStateCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vs.viewportCount=1; vs.pViewports=&vp;
        vs.scissorCount=1;  vs.pScissors=&sc;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.lineWidth=1; rs.cullMode=VK_CULL_MODE_NONE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState cbAtt{};
        cbAtt.colorWriteMask = 0xF;

        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount=1; cb.pAttachments=&cbAtt;

        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount=2; pi.pStages=stages;
        pi.pVertexInputState=&vi;   pi.pInputAssemblyState=&ia;
        pi.pViewportState=&vs;      pi.pRasterizationState=&rs;
        pi.pMultisampleState=&ms;   pi.pColorBlendState=&cb;
        VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynCI{};
        dynCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynCI.dynamicStateCount = 1;
        dynCI.pDynamicStates = dynStates;
        pi.pDynamicState = &dynCI;
        pi.layout=srgbPipeLayout_;  pi.renderPass=srgbRenderPass_; pi.subpass=0;
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &srgbPipeline_);
    }

    // --- PQ (HDR) 管线 ---
    auto pqFragCode = readFile(std::string(SHADER_DIR) + "pq_convert.frag.spv");
    auto pqFragMod  = createShaderModule(device_, pqFragCode);

    {
        VkDescriptorSetLayoutBinding bind{};
        bind.binding         = 0;
        bind.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bind.descriptorCount = 1;
        bind.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ci.bindingCount = 1;
        ci.pBindings = &bind;
        vkCreateDescriptorSetLayout(device_, &ci, nullptr, &pqDescLayout_);
    }
    {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = descPool_;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &pqDescLayout_;
        vkAllocateDescriptorSets(device_, &ai, &pqDescSet_);
        VkDescriptorImageInfo imgInfo{};
        imgInfo.sampler = sampler_;
        imgInfo.imageView = linearView_;
        imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = pqDescSet_;
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imgInfo;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }
    {
        VkPushConstantRange pqPCR{};
        pqPCR.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pqPCR.offset = 0;
        pqPCR.size = 32;  // 8 floats

        VkPipelineLayoutCreateInfo plCI{};
        plCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plCI.setLayoutCount = 1;
        plCI.pSetLayouts = &pqDescLayout_;
        plCI.pushConstantRangeCount = 1;
        plCI.pPushConstantRanges = &pqPCR;
        vkCreatePipelineLayout(device_, &plCI, nullptr, &pqPipeLayout_);

        VkPipelineShaderStageCreateInfo stages[] = {
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_VERTEX_BIT, srgbVertMod, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_FRAGMENT_BIT, pqFragMod, "main", nullptr},
        };
        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport vp{0,0,(float)swapchainExt_.width,(float)swapchainExt_.height,0,1};
        VkRect2D sc{{0,0},swapchainExt_};
        VkPipelineViewportStateCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vs.viewportCount=1; vs.pViewports=&vp;
        vs.scissorCount=1; vs.pScissors=&sc;
        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.lineWidth=1; rs.cullMode=VK_CULL_MODE_NONE;
        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState cbAtt{};
        cbAtt.colorWriteMask = 0xF;
        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount=1; cb.pAttachments=&cbAtt;
        VkGraphicsPipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount=2; pi.pStages=stages;
        pi.pVertexInputState=&vi; pi.pInputAssemblyState=&ia;
        pi.pViewportState=&vs; pi.pRasterizationState=&rs;
        pi.pMultisampleState=&ms; pi.pColorBlendState=&cb;
        VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynCI{};
        dynCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynCI.dynamicStateCount = 1;
        dynCI.pDynamicStates = dynStates;
        pi.pDynamicState = &dynCI;
        pi.layout=pqPipeLayout_; pi.renderPass=srgbRenderPass_; pi.subpass=0;
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &pqPipeline_);
    }

    // 销毁着色器模块
    vkDestroyShaderModule(device_, uiVertMod, nullptr);
    vkDestroyShaderModule(device_, uiFragMod, nullptr);
    vkDestroyShaderModule(device_, srgbVertMod, nullptr);
    vkDestroyShaderModule(device_, srgbFragMod, nullptr);
    vkDestroyShaderModule(device_, pqFragMod, nullptr);
}

// ============================================================================
// ImGui 初始化
// ============================================================================
void VulkanApp::initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    ImGui_ImplGlfw_InitForVulkan(window_, true);

    // 描述符池给 ImGui
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100},
        {VK_DESCRIPTOR_TYPE_SAMPLER, 100},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100},
    };
    VkDescriptorPoolCreateInfo dpCI{};
    dpCI.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpCI.maxSets       = 100;
    dpCI.poolSizeCount = 4;
    dpCI.pPoolSizes    = poolSizes;
    vkCreateDescriptorPool(device_, &dpCI, nullptr, &imguiDescPool_);

    ImGui_ImplVulkan_InitInfo vi{};
    vi.ApiVersion      = VK_API_VERSION_1_3;
    vi.Instance        = instance_;
    vi.PhysicalDevice  = physicalDev_;
    vi.Device          = device_;
    vi.QueueFamily     = graphicsFamily_;
    vi.Queue           = graphicsQueue_;
    vi.PipelineCache   = VK_NULL_HANDLE;
    vi.DescriptorPool  = imguiDescPool_;
    uint32_t imgCount = static_cast<uint32_t>(swapchainImages_.size());
    vi.MinImageCount   = imgCount;
    vi.ImageCount      = imgCount;
    vi.PipelineInfoMain.RenderPass  = imguiRenderPass_;
    vi.PipelineInfoMain.Subpass     = 0;
    vi.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    vi.UseDynamicRendering          = false;  // 使用传统 render pass，不走 dynamic rendering
    ImGui_ImplVulkan_Init(&vi);
    ImGui_ImplVulkan_CreateMainPipeline(&vi.PipelineInfoMain);
    ImGui::StyleColorsDark();
}

// ============================================================================
// 同步对象 + 命令池
// ============================================================================
void VulkanApp::createSyncObjects() {
    imageAvail_.resize(MAX_FRAMES_IN_FLIGHT);
    uiDone_.resize(MAX_FRAMES_IN_FLIGHT);
    inFlight_.resize(MAX_FRAMES_IN_FLIGHT);
    VkSemaphoreCreateInfo semCI{};
    semCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceCI{};
    fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkCreateSemaphore(device_, &semCI, nullptr, &imageAvail_[i]);
        vkCreateSemaphore(device_, &semCI, nullptr, &uiDone_[i]);
        vkCreateFence(device_, &fenceCI, nullptr, &inFlight_[i]);
    }
}

void VulkanApp::createCommandPool() {
    VkCommandPoolCreateInfo ci{};
    ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.queueFamilyIndex = graphicsFamily_;
    ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCreateCommandPool(device_, &ci, nullptr, &cmdPool_);

    cmdBufs_.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = cmdPool_;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = static_cast<uint32_t>(cmdBufs_.size());
    vkAllocateCommandBuffers(device_, &ai, cmdBufs_.data());
}

// ============================================================================
// 主循环
// ============================================================================
void VulkanApp::run() {
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();

        // --- ImGui 控件 ---
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::Begin("SDR UI Controls", nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("UI: %d/%zu — %s", currentUI_ + 1, uiPairs_.size(),
                    uiPairs_[currentUI_].name.c_str());
        ImGui::Text("Size: %dx%d", uiPairs_[currentUI_].width,
                    uiPairs_[currentUI_].height);

        if (ImGui::Button("< Prev")) {
            currentUI_ = (currentUI_ - 1 + uiPairs_.size()) % uiPairs_.size();
            uiLumNit_ = std::max(uiLumAvg_[currentUI_], 0.01f) * 500.0f;
            phase1Locked_ = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Next >")) {
            currentUI_ = (currentUI_ + 1) % uiPairs_.size();
            uiLumNit_ = std::max(uiLumAvg_[currentUI_], 0.01f) * 500.0f;
            phase1Locked_ = false;
        }
        ImGui::Spacing();

        ImGui::SliderFloat("UI Alpha", &uiAlpha_, 0.1f, 1.0f, "%.1f",
                           ImGuiSliderFlags_AlwaysClamp);
        ImGui::End();

        // === HDR 控制面板 (右下) ===
        if (hdrSupported_ || true) {  // 总是显示，方便观察参数
            ImGui::SetNextWindowPos(ImVec2(WINDOW_WIDTH/2.0f + 10, 10), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(620, 400), ImGuiCond_FirstUseEver);
            ImGui::Begin("HDR Controls", nullptr,
                         ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("HDR: %s", hdrSupported_ ? "ACTIVE" : "UNAVAILABLE (grey)");
            ImGui::Separator();

            // Max Display Nit
            ImGui::PushItemWidth(120);
            ImGui::InputFloat("Max Display Nit", &maxDisplayNit_, 100.0f, 1000.0f, "%.0f");
            if (maxDisplayNit_ < 100.0f) maxDisplayNit_ = 100.0f;
            ImGui::PopItemWidth();

            // Background Nit
            ImGui::PushItemWidth(500);
            ImGui::SliderFloat("BG Nit", &bgNit_, 0.0f, maxDisplayNit_, "%.0f",
                               ImGuiSliderFlags_AlwaysClamp);
            ImGui::PopItemWidth();

            // UI Luminance Nit
            ImGui::PushItemWidth(500);
            if (ImGui::SliderFloat("UI Lum Nit", &uiLumNit_, 0.0f, 4000.0f, "%.0f",
                                   ImGuiSliderFlags_AlwaysClamp)) {
                phase1Locked_ = false;  // 亮度变了，解锁 Phase1
            }
            ImGui::PopItemWidth();

            // Effective Alpha
            ImGui::PushItemWidth(500);
            if (ImGui::SliderFloat("Eff Alpha", &effAlpha_, 0.01f, 1.0f, "%.2f",
                                   ImGuiSliderFlags_AlwaysClamp)) {
                // Phase 2 自动联动
                if (phase1Locked_ && uiPairs_.size() && uiAlphaAvg_[currentUI_] > 0.001f) {
                    float aa = std::max(uiAlphaAvg_[currentUI_], 0.001f);
                    float la = std::max(uiLumAvg_[currentUI_], 0.01f);
                    uiLumNit_ = (lumResLocked_ - (1.0f - effAlpha_) * bgNit_)
                              / (effAlpha_ * aa * la);
                    if (uiLumNit_ < 0) uiLumNit_ = 0;
                    if (uiLumNit_ > 4000) uiLumNit_ = 4000;
                }
            }
            ImGui::PopItemWidth();

            // Phase 1 lock button
            if (ImGui::Button("Lock Brightness (Phase 1)")) {
                phase1Locked_ = true;
                float aa = std::max(uiAlphaAvg_[currentUI_], 0.001f);
                float la = std::max(uiLumAvg_[currentUI_], 0.01f);
                lumResLocked_ = (1.0f - aa) * bgNit_ + aa * uiLumNit_ * la;
            }
            ImGui::SameLine();
            if (ImGui::Button("Unlock")) phase1Locked_ = false;
            ImGui::Text("Locked: %s  LumRes: %.0f", phase1Locked_ ? "YES" : "no", lumResLocked_);

            // 显示计算的 ratio
            float aa = std::max(uiAlphaAvg_[currentUI_], 0.001f);
            float la = std::max(uiLumAvg_[currentUI_], 0.01f);
            ImGui::Text("UI_alpha_ratio = %.2f  |  UI_lum_ratio = %.2f",
                        effAlpha_ / aa, uiLumNit_ / la);

            ImGui::End();
        }

        ImGui::Render();

        // --- 绘制 ---
        drawFrame();
    }
    vkDeviceWaitIdle(device_);
}

void VulkanApp::drawFrame() {
    vkWaitForFences(device_, 1, &inFlight_[currentFrame_], VK_TRUE, UINT64_MAX);
    vkResetFences(device_, 1, &inFlight_[currentFrame_]);

    uint32_t imageIndex;
    vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                          imageAvail_[currentFrame_], VK_NULL_HANDLE, &imageIndex);

    vkResetCommandBuffer(cmdBufs_[currentFrame_], 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmdBufs_[currentFrame_], &beginInfo);

    recordUIPass(cmdBufs_[currentFrame_], imageIndex);
    recordSRGBPass(cmdBufs_[currentFrame_], imageIndex);
    recordImGuiPass(cmdBufs_[currentFrame_], imageIndex);

    vkEndCommandBuffer(cmdBufs_[currentFrame_]);

    VkSemaphore waitSems[] = { imageAvail_[currentFrame_] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore sigSems[] = { uiDone_[currentFrame_] };

    VkSubmitInfo si{};
    si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount   = 1;
    si.pWaitSemaphores      = waitSems;
    si.pWaitDstStageMask    = waitStages;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &cmdBufs_[currentFrame_];
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = sigSems;
    vkQueueSubmit(graphicsQueue_, 1, &si, inFlight_[currentFrame_]);

    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = sigSems;
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &swapchain_;
    pi.pImageIndices      = &imageIndex;
    vkQueuePresentKHR(graphicsQueue_, &pi);

    currentFrame_ = (currentFrame_ + 1) % MAX_FRAMES_IN_FLIGHT;
}

// ============================================================================
// 绘制录制
// ============================================================================
void VulkanApp::recordUIPass(VkCommandBuffer cmd, uint32_t imageIdx) {
    int uiW = uiPairs_[currentUI_].width;
    int uiH = uiPairs_[currentUI_].height;
    float fracX = (float)uiW / swapchainExt_.width;
    float fracY = (float)uiH / swapchainExt_.height;
    float offsetX = -fracX;
    float offsetY =  fracY;
    float scaleX  = fracX * 2.0f;
    float scaleY  = -fracY * 2.0f;

    // Push constants struct: 32 bytes
    // bytes 0-15: vertex (offset+scale)
    // bytes 16-19: uiAlpha
    // bytes 20-23: bgLinear
    struct PC { float ox, oy, sx, sy, alpha, bgLinear; float pad[2]; };
    PC pc{};
    pc.ox = offsetX; pc.oy = offsetY;
    pc.sx = scaleX;  pc.sy = scaleY;
    pc.alpha = uiAlpha_;

    // Clear + draw: use DONT_CARE since we clear manually with scissor
    VkRenderPassBeginInfo rp{};
    rp.sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass  = uiRenderPass_;
    rp.framebuffer = uiFramebuffers_[imageIdx];
    rp.renderArea  = {{0,0}, swapchainExt_};
    rp.clearValueCount = 0;    // manual clear via vkCmdClearAttachments
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, uiPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            uiPipeLayout_, 0, 1, &uiDescSets_[currentUI_], 0, nullptr);

    // === 左半：SDR 背景 0.18 ===
    {
        VkRect2D scissor{ {0,0}, {swapchainExt_.width/2, swapchainExt_.height} };
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        pc.ox = -0.5f - fracX;  // center UI in left half (NDC center at -0.5)
        VkClearAttachment clearAtt{};
        clearAtt.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        clearAtt.clearValue.color = {{ BG_GRAY, BG_GRAY, BG_GRAY, 1.0f }};
        VkClearRect cr{};
        cr.rect = {{0, 0}, {swapchainExt_.width/2, swapchainExt_.height}};
        cr.layerCount = 1;
        vkCmdClearAttachments(cmd, 1, &clearAtt, 1, &cr);

        pc.bgLinear = BG_GRAY;  // 0.18 SDR
        vkCmdPushConstants(cmd, uiPipeLayout_,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        VkDeviceSize vbOff = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &quadVB_, &vbOff);
        vkCmdDraw(cmd, 6, 1, 0, 0);
    }

    // === 右半：HDR 背景 BG_nit/500 ===
    {
        VkRect2D scissor{ {(int32_t)swapchainExt_.width/2, 0}, {swapchainExt_.width/2, swapchainExt_.height} };
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        pc.ox = 0.5f - fracX;  // center UI in right half (NDC center at +0.5)
        float hdrBg = bgNit_ / 500.0f;
        VkClearAttachment clearAtt{};
        clearAtt.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        clearAtt.clearValue.color = {{ hdrBg, hdrBg, hdrBg, 1.0f }};
        VkClearRect cr{};
        cr.rect = {{(int32_t)swapchainExt_.width/2, 0}, {swapchainExt_.width/2, swapchainExt_.height}};
        cr.layerCount = 1;
        vkCmdClearAttachments(cmd, 1, &clearAtt, 1, &cr);

        pc.bgLinear = hdrBg;
        vkCmdPushConstants(cmd, uiPipeLayout_,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        VkDeviceSize vbOff = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &quadVB_, &vbOff);
        vkCmdDraw(cmd, 6, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
}

void VulkanApp::recordSRGBPass(VkCommandBuffer cmd, uint32_t imageIdx) {
    // 屏障：确保 UI pass 写入的线性中间缓冲对后续 shader 读取可见
    VkImageMemoryBarrier linearBarrier{};
    linearBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    linearBarrier.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    linearBarrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    linearBarrier.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    linearBarrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    linearBarrier.image               = linearImg_;
    linearBarrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &linearBarrier);

    VkRenderPassBeginInfo rp{};
    rp.sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass  = srgbRenderPass_;
    rp.framebuffer = srgbFramebuffers_[imageIdx];
    rp.renderArea  = {{0,0}, swapchainExt_};
    rp.clearValueCount = 0;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    int halfW = (int)swapchainExt_.width / 2;

    // === 左半：SDR (sRGB 编码) ===
    {
        VkRect2D scissor{ {0,0}, {(uint32_t)halfW, swapchainExt_.height} };
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, srgbPipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                srgbPipeLayout_, 0, 1, &srgbDescSet_, 0, nullptr);
        float encodeFlag = needsSRGBEncode_ ? 1.0f : 0.0f;
        vkCmdPushConstants(cmd, srgbPipeLayout_, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(float), &encodeFlag);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    // === 右半：HDR (PQ 编码) 或 降级灰色 ===
    {
        VkRect2D scissor{ {halfW,0}, {(uint32_t)halfW, swapchainExt_.height} };
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pqPipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pqPipeLayout_, 0, 1, &pqDescSet_, 0, nullptr);

        // UI quad bounds in NDC
        int uiW = uiPairs_.size() ? uiPairs_[currentUI_].width : 0;
        int uiH = uiPairs_.size() ? uiPairs_[currentUI_].height : 0;
        float fx = (float)uiW / swapchainExt_.width;
        float fy = (float)uiH / swapchainExt_.height;

        struct PQPC {
            float wp, bg, maxNit, hdrOk;
            float uiL, uiR, uiB, uiT;
        } pq;
        pq.wp    = 500.0f;
        pq.bg    = bgNit_;
        pq.maxNit = maxDisplayNit_;
        pq.hdrOk = hdrSupported_ ? 1.0f : 0.0f;
        pq.uiL   =  0.5f - fx;  pq.uiR = 0.5f + fx;  // NDC right-half center
        pq.uiB   = -fy;         pq.uiT = fy;          // NDC bottom/top (same)
        static int pqFrame = 0;
        if (++pqFrame <= 3) std::cout << "[PQ] frame " << pqFrame
            << " bg=" << pq.bg << " maxNit=" << pq.maxNit << " hdrOk=" << pq.hdrOk
            << " uiBounds=[" << pq.uiL << "," << pq.uiR << "," << pq.uiB << "," << pq.uiT << "]"
            << std::endl;
        vkCmdPushConstants(cmd, pqPipeLayout_, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pq), &pq);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
}

void VulkanApp::recordImGuiPass(VkCommandBuffer cmd, uint32_t imageIdx) {
    // 屏障：确保 sRGB pass 写入的 swapchain 对后续 ImGui pass 读取可见
    VkImageMemoryBarrier swapchainBarrier{};
    swapchainBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    swapchainBarrier.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    swapchainBarrier.dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                         | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    swapchainBarrier.oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    swapchainBarrier.newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    swapchainBarrier.image               = swapchainImages_[imageIdx];
    swapchainBarrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0, 0, nullptr, 0, nullptr, 1, &swapchainBarrier);

    VkRenderPassBeginInfo rp{};
    rp.sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass  = imguiRenderPass_;
    rp.framebuffer = imguiFramebuffers_[imageIdx];
    rp.renderArea  = {{0,0}, swapchainExt_};
    rp.clearValueCount = 0;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

    vkCmdEndRenderPass(cmd);
}

// ============================================================================
// 入口
// ============================================================================
int main() {
    try {
        VulkanApp app;
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "[FATAL] " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
