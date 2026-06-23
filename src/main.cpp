// main.cpp — VulkanApp implementation
#include "app.h"
#include "common.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <cstring>
#include <stdexcept>
#include <set>
#include <algorithm>
#include <cstdint>

// ============================================================================
// Validation layers
// ============================================================================
#ifdef NDEBUG
constexpr bool kEnableValidation = false;
#else
constexpr bool kEnableValidation = true;
#endif

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*) {
    std::cerr << "[VK] " << data->pMessage << std::endl;
    return VK_FALSE;
}

static VkResult CreateDebugUtilsMessengerEXT(
    VkInstance inst, const VkDebugUtilsMessengerCreateInfoEXT* pCreate,
    const VkAllocationCallbacks* pAlloc,
    VkDebugUtilsMessengerEXT* pMessenger) {
    auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(inst, "vkCreateDebugUtilsMessengerEXT");
    return fn ? fn(inst, pCreate, pAlloc, pMessenger) : VK_ERROR_EXTENSION_NOT_PRESENT;
}

static void DestroyDebugUtilsMessengerEXT(
    VkInstance inst, VkDebugUtilsMessengerEXT messenger,
    const VkAllocationCallbacks* pAlloc) {
    auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(inst, "vkDestroyDebugUtilsMessengerEXT");
    if (fn) fn(inst, messenger, pAlloc);
}

static bool checkValidationLayerSupport() {
    uint32_t count;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> available(count);
    vkEnumerateInstanceLayerProperties(&count, available.data());
    for (const char* name : {"VK_LAYER_KHRONOS_validation"}) {
        bool found = false;
        for (auto& l : available)
            if (strcmp(l.layerName, name) == 0) { found = true; break; }
        if (!found) return false;
    }
    return true;
}

// ============================================================================
// Helper: single-use command buffer submission
// ============================================================================
static VkCommandBuffer beginOneShot(VkDevice dev, VkCommandPool pool) {
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(dev, &ai, &cmd);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

static void endOneShot(VkDevice dev, VkCommandPool pool, VkQueue queue, VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(dev, pool, 1, &cmd);
}

// ============================================================================
// Shader loading
// ============================================================================
static VkShaderModule loadShader(VkDevice dev, const std::string& path) {
    auto code = readFile(path);
    return createShaderModule(dev, code);
}

// ============================================================================
// Buffer allocation helper
// ============================================================================
static void createGPUBuffer(VkDevice dev, VkPhysicalDevice phy, VkDeviceSize size,
                              VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                              VkBuffer& buf, VkDeviceMemory& mem) {
    VkBufferCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size = size;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(dev, &ci, nullptr, &buf);

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(dev, buf, &mr);
    VkMemoryAllocateInfo ma{};
    ma.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ma.allocationSize = mr.size;
    ma.memoryTypeIndex = findMemoryType(phy, mr.memoryTypeBits, props);
    vkAllocateMemory(dev, &ma, nullptr, &mem);
    vkBindBufferMemory(dev, buf, mem, 0);
}

// ============================================================================
// Swapchain helper
// ============================================================================
static VkSurfaceFormatKHR chooseSurfaceFormat(VkPhysicalDevice phy, VkSurfaceKHR surface,
                                                bool hdr, bool& needsEncode) {
    uint32_t count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phy, surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(phy, surface, &count, formats.data());

    VkSurfaceFormatKHR chosen = formats[0];  // fallback

    if (!hdr) {
        // SDR: prefer SRGB
        for (auto& f : formats) {
            if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                needsEncode = false;
                return f;
            }
        }
        // Fallback: UNORM + manual encode
        for (auto& f : formats) {
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                needsEncode = true;
                return f;
            }
        }
        needsEncode = (chosen.format != VK_FORMAT_B8G8R8A8_SRGB);
        return chosen;
    } else {
        // HDR: prefer 10-bit + HDR10 PQ
        for (auto& f : formats) {
            if ((f.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 ||
                 f.format == VK_FORMAT_A2R10G10B10_UNORM_PACK32) &&
                f.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT) {
                needsEncode = false;
                return f;
            }
        }
        // Fallback: SDR
        needsEncode = false;
        return chooseSurfaceFormat(phy, surface, false, needsEncode);
    }
}

static VkPresentModeKHR choosePresentMode(VkPhysicalDevice phy, VkSurfaceKHR surface) {
    uint32_t count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(phy, surface, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(phy, surface, &count, modes.data());
    for (auto m : modes)
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
    return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& caps, uint32_t w, uint32_t h) {
    if (caps.currentExtent.width != UINT32_MAX)
        return caps.currentExtent;
    VkExtent2D e = {w, h};
    e.width  = std::clamp(e.width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
    e.height = std::clamp(e.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return e;
}

// ============================================================================
// VulkanApp::~VulkanApp
// ============================================================================
VulkanApp::VulkanApp(const std::string& assetPath) : assetPath_(assetPath) {}

VulkanApp::~VulkanApp() {
    if (core_.device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(core_.device);

    // Quad buffer
    if (quadVB_ != VK_NULL_HANDLE) vkDestroyBuffer(core_.device, quadVB_, nullptr);
    if (quadVBMem_ != VK_NULL_HANDLE) vkFreeMemory(core_.device, quadVBMem_, nullptr);

    // UI textures
    for (auto& p : uiPairs_) {
        if (p.rgb.view) vkDestroyImageView(core_.device, p.rgb.view, nullptr);
        if (p.rgb.img)  vkDestroyImage(core_.device, p.rgb.img, nullptr);
        if (p.rgb.mem)  vkFreeMemory(core_.device, p.rgb.mem, nullptr);
        if (p.alpha.view) vkDestroyImageView(core_.device, p.alpha.view, nullptr);
        if (p.alpha.img)  vkDestroyImage(core_.device, p.alpha.img, nullptr);
        if (p.alpha.mem)  vkFreeMemory(core_.device, p.alpha.mem, nullptr);
    }

    auto cleanupWindow = [&](WindowContext& wc) {
        if (wc.hasImGui) {
            ImGui::SetCurrentContext(wc.imguiCtx);
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext(wc.imguiCtx);
            wc.imguiCtx = nullptr;
        }
        if (wc.imguiDescPool) vkDestroyDescriptorPool(core_.device, wc.imguiDescPool, nullptr);
        for (auto fb : wc.uiFramebuffers)      vkDestroyFramebuffer(core_.device, fb, nullptr);
        for (auto fb : wc.convertFramebuffers) vkDestroyFramebuffer(core_.device, fb, nullptr);
        for (auto fb : wc.imguiFramebuffers)   vkDestroyFramebuffer(core_.device, fb, nullptr);
        if (wc.uiPipeline)       vkDestroyPipeline(core_.device, wc.uiPipeline, nullptr);
        if (wc.uiPipeLayout)     vkDestroyPipelineLayout(core_.device, wc.uiPipeLayout, nullptr);
        if (wc.convertPipeline)  vkDestroyPipeline(core_.device, wc.convertPipeline, nullptr);
        if (wc.convertPipeLayout)vkDestroyPipelineLayout(core_.device, wc.convertPipeLayout, nullptr);
        if (wc.uiDescLayout)     vkDestroyDescriptorSetLayout(core_.device, wc.uiDescLayout, nullptr);
        if (wc.texSampler)       vkDestroySampler(core_.device, wc.texSampler, nullptr);
        if (wc.uiPass)           vkDestroyRenderPass(core_.device, wc.uiPass, nullptr);
        if (wc.convertPass)      vkDestroyRenderPass(core_.device, wc.convertPass, nullptr);
        if (wc.imguiPass)        vkDestroyRenderPass(core_.device, wc.imguiPass, nullptr);
        if (wc.linearView)       vkDestroyImageView(core_.device, wc.linearView, nullptr);
        if (wc.linearImg)        vkDestroyImage(core_.device, wc.linearImg, nullptr);
        if (wc.linearMem)        vkFreeMemory(core_.device, wc.linearMem, nullptr);
        for (auto v : wc.swapchainViews) vkDestroyImageView(core_.device, v, nullptr);
        if (wc.swapchain) vkDestroySwapchainKHR(core_.device, wc.swapchain, nullptr);
        if (wc.surface)   vkDestroySurfaceKHR(core_.instance, wc.surface, nullptr);
        if (wc.window && !glfwWindowShouldClose(wc.window))
            glfwDestroyWindow(wc.window);
        if (!wc.cmdBufs.empty()) {
            vkFreeCommandBuffers(core_.device, wc.cmdPool,
                                 (uint32_t)wc.cmdBufs.size(), wc.cmdBufs.data());
        }
        if (wc.cmdPool) vkDestroyCommandPool(core_.device, wc.cmdPool, nullptr);
        for (auto s : wc.imageAvail) vkDestroySemaphore(core_.device, s, nullptr);
        for (auto s : wc.renderDone) vkDestroySemaphore(core_.device, s, nullptr);
        for (auto f : wc.inFlight)   vkDestroyFence(core_.device, f, nullptr);
    };

    cleanupWindow(sdr_);
    cleanupWindow(hdr_);

    if (core_.uiDescPool)  vkDestroyDescriptorPool(core_.device, core_.uiDescPool, nullptr);
    if (core_.uiDescLayout)vkDestroyDescriptorSetLayout(core_.device, core_.uiDescLayout, nullptr);
    if (core_.texSampler)   vkDestroySampler(core_.device, core_.texSampler, nullptr);
    if (core_.texSamplerLin)vkDestroySampler(core_.device, core_.texSamplerLin, nullptr);
    if (core_.sharedCmdPool)vkDestroyCommandPool(core_.device, core_.sharedCmdPool, nullptr);

    vkDestroyDevice(core_.device, nullptr);
    // Destroy instance (debug messenger was implicitly created; we don't track it)
    vkDestroyInstance(core_.instance, nullptr);
    glfwTerminate();
}

// ============================================================================
// initCore — GLFW, instance, device, samplers, shared descriptor layout
// ============================================================================
void VulkanApp::initCore() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    sdr_.window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT,
                                    "UI Vulkan — SDR", nullptr, nullptr);
    hdr_.window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT,
                                    "UI Vulkan — HDR", nullptr, nullptr);
    if (!sdr_.window || !hdr_.window)
        throw std::runtime_error("GLFW window creation failed");

    // --- Instance ---
    std::vector<const char*> instExts;
    uint32_t glfwExtCount;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    instExts.assign(glfwExts, glfwExts + glfwExtCount);
    if (kEnableValidation) instExts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "UI_Vulkan";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &appInfo;
    ici.enabledExtensionCount = (uint32_t)instExts.size();
    ici.ppEnabledExtensionNames = instExts.data();

    const char* validationLayer = "VK_LAYER_KHRONOS_validation";
    VkDebugUtilsMessengerCreateInfoEXT dbgCI{};
    if (kEnableValidation && checkValidationLayerSupport()) {
        ici.enabledLayerCount = 1;
        ici.ppEnabledLayerNames = &validationLayer;
        dbgCI.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dbgCI.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
                              | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                              | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dbgCI.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                          | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                          | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dbgCI.pfnUserCallback = debugCallback;
        ici.pNext = &dbgCI;
    }

    if (vkCreateInstance(&ici, nullptr, &core_.instance) != VK_SUCCESS)
        throw std::runtime_error("vkCreateInstance failed");

    VkDebugUtilsMessengerEXT dbgMsg;
    if (kEnableValidation && checkValidationLayerSupport())
        CreateDebugUtilsMessengerEXT(core_.instance, &dbgCI, nullptr, &dbgMsg);

    // --- Surfaces ---
    if (glfwCreateWindowSurface(core_.instance, sdr_.window, nullptr, &sdr_.surface) != VK_SUCCESS
     || glfwCreateWindowSurface(core_.instance, hdr_.window, nullptr, &hdr_.surface) != VK_SUCCESS)
        throw std::runtime_error("Surface creation failed");

    // --- Physical device ---
    uint32_t devCount;
    vkEnumeratePhysicalDevices(core_.instance, &devCount, nullptr);
    if (devCount == 0) throw std::runtime_error("No Vulkan physical devices");
    std::vector<VkPhysicalDevice> phys(devCount);
    vkEnumeratePhysicalDevices(core_.instance, &devCount, phys.data());

    core_.physicalDevice = VK_NULL_HANDLE;
    for (auto pd : phys) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(pd, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            core_.physicalDevice = pd;
            break;
        }
    }
    if (core_.physicalDevice == VK_NULL_HANDLE) core_.physicalDevice = phys[0];

    // --- Queue family ---
    uint32_t qfCount;
    vkGetPhysicalDeviceQueueFamilyProperties(core_.physicalDevice, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qfProps(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(core_.physicalDevice, &qfCount, qfProps.data());

    core_.graphicsFamily = ~0u;
    for (uint32_t i = 0; i < qfCount; ++i) {
        if (!(qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
        VkBool32 presentSDR = VK_FALSE, presentHDR = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(core_.physicalDevice, i, sdr_.surface, &presentSDR);
        vkGetPhysicalDeviceSurfaceSupportKHR(core_.physicalDevice, i, hdr_.surface, &presentHDR);
        if (presentSDR && presentHDR) { core_.graphicsFamily = i; break; }
    }
    if (core_.graphicsFamily == ~0u)
        throw std::runtime_error("No suitable graphics+present queue family");

    // --- Logical device ---
    float qp = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = core_.graphicsFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &qp;

    std::vector<const char*> devExts = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkPhysicalDeviceFeatures feats{};

    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.pQueueCreateInfos = &qci;
    dci.queueCreateInfoCount = 1;
    dci.ppEnabledExtensionNames = devExts.data();
    dci.enabledExtensionCount = (uint32_t)devExts.size();
    dci.pEnabledFeatures = &feats;

    if (vkCreateDevice(core_.physicalDevice, &dci, nullptr, &core_.device) != VK_SUCCESS)
        throw std::runtime_error("vkCreateDevice failed");

    vkGetDeviceQueue(core_.device, core_.graphicsFamily, 0, &core_.graphicsQueue);

    // --- Shared command pool ---
    VkCommandPoolCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = core_.graphicsFamily;
    vkCreateCommandPool(core_.device, &cpci, nullptr, &core_.sharedCmdPool);

    // --- Samplers ---
    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter  = VK_FILTER_NEAREST;
    sci.minFilter  = VK_FILTER_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxLod = 1.0f;
    vkCreateSampler(core_.device, &sci, nullptr, &core_.texSampler);

    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    vkCreateSampler(core_.device, &sci, nullptr, &core_.texSamplerLin);

    // --- Shared UI descriptor set layout (texRGB + texAlpha) ---
    VkDescriptorSetLayoutBinding uiBindings[2] = {};
    uiBindings[0].binding = 0;
    uiBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    uiBindings[0].descriptorCount = 1;
    uiBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    uiBindings[1].binding = 1;
    uiBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    uiBindings[1].descriptorCount = 1;
    uiBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dslci{};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 2;
    dslci.pBindings = uiBindings;
    vkCreateDescriptorSetLayout(core_.device, &dslci, nullptr, &core_.uiDescLayout);

    // --- Shared descriptor pool ---
    VkDescriptorPoolSize dps{};
    dps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    dps.descriptorCount = 200;

    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 100;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &dps;
    vkCreateDescriptorPool(core_.device, &dpci, nullptr, &core_.uiDescPool);
}

// ============================================================================
// Quad vertex buffer (shared)
// ============================================================================
static void createQuadBuffer(VulkanCore& core, VkBuffer& buf, VkDeviceMemory& mem) {
    // 6 vertices: 2 triangles covering a unit quad centered at origin
    // pos.xy, uv.xy  (total 16 bytes per vertex)
    // Vulkan viewport: NDC Y=-1 → framebuffer Y=0 (top), NDC Y=+1 → framebuffer Y=height (bottom).
    // So NDC "bottom" (-0.5 in object space) appears at screen-top, needs UV V=0 (top of texture).
    // NDC "top" (+0.5) appears at screen-bottom, needs UV V=1 (bottom of texture).
    struct { float x,y,u,v; } verts[6] = {
        {-0.5f,-0.5f, 0.f,0.f}, { 0.5f,-0.5f, 1.f,0.f}, { 0.5f, 0.5f, 1.f,1.f},
        {-0.5f,-0.5f, 0.f,0.f}, { 0.5f, 0.5f, 1.f,1.f}, {-0.5f, 0.5f, 0.f,1.f},
    };
    VkDeviceSize size = sizeof(verts);

    VkBuffer stag; VkDeviceMemory stagMem;
    createGPUBuffer(core.device, core.physicalDevice, size,
                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    stag, stagMem);
    void* m;
    vkMapMemory(core.device, stagMem, 0, size, 0, &m);
    memcpy(m, verts, size);
    vkUnmapMemory(core.device, stagMem);

    createGPUBuffer(core.device, core.physicalDevice, size,
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buf, mem);

    VkCommandBuffer cmd = beginOneShot(core.device, core.sharedCmdPool);
    VkBufferCopy region{};
    region.size = size;
    vkCmdCopyBuffer(cmd, stag, buf, 1, &region);
    endOneShot(core.device, core.sharedCmdPool, core.graphicsQueue, cmd);

    vkDestroyBuffer(core.device, stag, nullptr);
    vkFreeMemory(core.device, stagMem, nullptr);
}

// ============================================================================
// Window initialization (common parts)
// ============================================================================
static void initWindowSwapchain(WindowContext& wc, VulkanCore& core,
                                  bool hdr, VkFormat desiredFmt,
                                  VkColorSpaceKHR desiredCS) {
    wc.swapchainFmt = desiredFmt;
    wc.swapchainCS  = desiredCS;
    wc.needsSRGBEncode = hdr ? false : (desiredFmt != VK_FORMAT_B8G8R8A8_SRGB);

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(core.physicalDevice, wc.surface, &caps);
    VkExtent2D extent = chooseExtent(caps, WINDOW_WIDTH, WINDOW_HEIGHT);
    wc.swapchainExt = extent;

    VkPresentModeKHR present = choosePresentMode(core.physicalDevice, wc.surface);
    uint32_t imageCount = std::max(caps.minImageCount + 1, 2u);
    if (caps.maxImageCount > 0) imageCount = std::min(imageCount, caps.maxImageCount);

    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = wc.surface;
    sci.minImageCount = imageCount;
    sci.imageFormat = wc.swapchainFmt;
    sci.imageColorSpace = wc.swapchainCS;
    sci.imageExtent = extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = present;
    sci.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(core.device, &sci, nullptr, &wc.swapchain) != VK_SUCCESS)
        throw std::runtime_error("Swapchain creation failed");

    // Swapchain images
    uint32_t scCount;
    vkGetSwapchainImagesKHR(core.device, wc.swapchain, &scCount, nullptr);
    wc.swapchainImages.resize(scCount);
    vkGetSwapchainImagesKHR(core.device, wc.swapchain, &scCount, wc.swapchainImages.data());

    wc.swapchainViews.resize(scCount);
    for (uint32_t i = 0; i < scCount; ++i)
        wc.swapchainViews[i] = createImageView(core.device, wc.swapchainImages[i],
                                                wc.swapchainFmt, VK_IMAGE_ASPECT_COLOR_BIT);

    // Linear intermediate image (R16G16B16A16_SFLOAT)
    createImage(core.device, core.physicalDevice,
                extent.width, extent.height,
                VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                wc.linearImg, wc.linearMem);
    wc.linearView = createImageView(core.device, wc.linearImg,
                                     VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);

    // Transition linear image to shader-read-optimal (done once)
    transitionLayout(core.device, core.sharedCmdPool, core.graphicsQueue,
                     wc.linearImg, VK_FORMAT_R16G16B16A16_SFLOAT,
                     VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

// ============================================================================
// Render passes
// ============================================================================
static void createRenderPasses(WindowContext& wc, VulkanCore& core) {
    // --- UI pass: color attachment = linear intermediate ---
    {
        VkAttachmentDescription att{};
        att.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference ref{};
        ref.attachment = 0;
        ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &ref;

        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = 0;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpci{};
        rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpci.attachmentCount = 1;
        rpci.pAttachments = &att;
        rpci.subpassCount = 1;
        rpci.pSubpasses = &sub;
        rpci.dependencyCount = 1;
        rpci.pDependencies = &dep;
        vkCreateRenderPass(core.device, &rpci, nullptr, &wc.uiPass);
    }

    // --- Convert pass: color attachment = swapchain ---
    {
        VkAttachmentDescription att{};
        att.format = wc.swapchainFmt;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference ref{};
        ref.attachment = 0;
        ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &ref;

        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = 0;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpci{};
        rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpci.attachmentCount = 1;
        rpci.pAttachments = &att;
        rpci.subpassCount = 1;
        rpci.pSubpasses = &sub;
        rpci.dependencyCount = 1;
        rpci.pDependencies = &dep;
        vkCreateRenderPass(core.device, &rpci, nullptr, &wc.convertPass);
    }

    // --- ImGui pass: color attachment = swapchain (LOAD) ---
    {
        VkAttachmentDescription att{};
        att.format = wc.swapchainFmt;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference ref{};
        ref.attachment = 0;
        ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &ref;

        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpci{};
        rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpci.attachmentCount = 1;
        rpci.pAttachments = &att;
        rpci.subpassCount = 1;
        rpci.pSubpasses = &sub;
        rpci.dependencyCount = 1;
        rpci.pDependencies = &dep;
        vkCreateRenderPass(core.device, &rpci, nullptr, &wc.imguiPass);
    }
}

// ============================================================================
// Framebuffers
// ============================================================================
static void createFramebuffers(WindowContext& wc, VulkanCore& core) {
    uint32_t n = (uint32_t)wc.swapchainViews.size();
    VkImageView attachments[1];

    // UI framebuffers (all use same linear view)
    attachments[0] = wc.linearView;
    wc.uiFramebuffers.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        VkFramebufferCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = wc.uiPass;
        fci.attachmentCount = 1;
        fci.pAttachments = attachments;
        fci.width = wc.swapchainExt.width;
        fci.height = wc.swapchainExt.height;
        fci.layers = 1;
        vkCreateFramebuffer(core.device, &fci, nullptr, &wc.uiFramebuffers[i]);
    }

    // Convert & ImGui framebuffers (per swapchain image)
    wc.convertFramebuffers.resize(n);
    wc.imguiFramebuffers.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        attachments[0] = wc.swapchainViews[i];

        VkFramebufferCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = wc.convertPass;
        fci.attachmentCount = 1;
        fci.pAttachments = attachments;
        fci.width = wc.swapchainExt.width;
        fci.height = wc.swapchainExt.height;
        fci.layers = 1;
        vkCreateFramebuffer(core.device, &fci, nullptr, &wc.convertFramebuffers[i]);

        fci.renderPass = wc.imguiPass;
        vkCreateFramebuffer(core.device, &fci, nullptr, &wc.imguiFramebuffers[i]);
    }
}

// ============================================================================
// Convert descriptor set (per-window: reads linear intermediate)
// ============================================================================
static void createConvertDescriptor(WindowContext& wc, VulkanCore& core) {
    // Layout
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dslci{};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 1;
    dslci.pBindings = &binding;
    vkCreateDescriptorSetLayout(core.device, &dslci, nullptr, &wc.uiDescLayout);
}

// ============================================================================
// initSDRWindow
// ============================================================================
void VulkanApp::initSDRWindow() {
    auto& wc = sdr_;
    bool needsEncode;
    VkSurfaceFormatKHR fmt = chooseSurfaceFormat(core_.physicalDevice, wc.surface, false, needsEncode);
    initWindowSwapchain(wc, core_, false, fmt.format, fmt.colorSpace);
    wc.needsSRGBEncode = needsEncode;

    createRenderPasses(wc, core_);
    createConvertDescriptor(wc, core_);
    createFramebuffers(wc, core_);

    // --- Sampler for convert (linear, stored in wc.texSampler) ---
    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxLod = 1.0f;
    vkCreateSampler(core_.device, &sci, nullptr, &wc.texSampler);

    // --- Convert descriptor set (pre-allocated once) ---
    {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = core_.uiDescPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &wc.uiDescLayout;
        vkAllocateDescriptorSets(core_.device, &ai, &wc.convertDescSet);

        VkDescriptorImageInfo ii{};
        ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        ii.imageView = wc.linearView;
        ii.sampler = wc.texSampler;

        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = wc.convertDescSet;
        w.dstBinding = 0;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &ii;
        vkUpdateDescriptorSets(core_.device, 1, &w, 0, nullptr);
    }

    // --- UI pipeline ---
    {
        auto vert = loadShader(core_.device, SHADER_DIR "ui.vert.spv");
        auto frag = loadShader(core_.device, SHADER_DIR "ui.frag.spv");

        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag;
        stages[1].pName = "main";

        VkVertexInputBindingDescription vBind{};
        vBind.binding = 0;
        vBind.stride = 16;  // pos(2f) + uv(2f)
        vBind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription vAttrs[2] = {};
        vAttrs[0].binding = 0;
        vAttrs[0].location = 0;
        vAttrs[0].format = VK_FORMAT_R32G32_SFLOAT;
        vAttrs[0].offset = 0;
        vAttrs[1].binding = 0;
        vAttrs[1].location = 1;
        vAttrs[1].format = VK_FORMAT_R32G32_SFLOAT;
        vAttrs[1].offset = 8;

        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &vBind;
        vi.vertexAttributeDescriptionCount = 2;
        vi.pVertexAttributeDescriptions = vAttrs;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport vp{};
        vp.x = 0; vp.y = 0;
        vp.width  = (float)wc.swapchainExt.width;
        vp.height = (float)wc.swapchainExt.height;
        vp.minDepth = 0.f; vp.maxDepth = 1.f;
        VkRect2D sc{{0,0}, wc.swapchainExt};

        VkPipelineViewportStateCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vs.viewportCount = 1;
        vs.pViewports = &vp;
        vs.scissorCount = 1;
        vs.pScissors = &sc;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.lineWidth = 1.0f;
        rs.cullMode = VK_CULL_MODE_NONE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState cb{};
        cb.blendEnable = VK_FALSE;
        cb.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                          | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = 1;
        cbs.pAttachments = &cb;

        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcRange.offset = 0;
        pcRange.size = 28;

        VkDescriptorSetLayout layouts[1] = {core_.uiDescLayout};
        VkPipelineLayoutCreateInfo plci{};
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount = 1;
        plci.pSetLayouts = layouts;
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges = &pcRange;
        vkCreatePipelineLayout(core_.device, &plci, nullptr, &wc.uiPipeLayout);

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount = 2;
        pci.pStages = stages;
        pci.pVertexInputState = &vi;
        pci.pInputAssemblyState = &ia;
        pci.pViewportState = &vs;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState = &ms;
        pci.pColorBlendState = &cbs;
        pci.layout = wc.uiPipeLayout;
        pci.renderPass = wc.uiPass;
        pci.subpass = 0;
        vkCreateGraphicsPipelines(core_.device, VK_NULL_HANDLE, 1, &pci, nullptr, &wc.uiPipeline);

        vkDestroyShaderModule(core_.device, vert, nullptr);
        vkDestroyShaderModule(core_.device, frag, nullptr);
    }

    // --- Convert pipeline (SDR: srgb_convert) ---
    {
        auto vert = loadShader(core_.device, SHADER_DIR "srgb_convert.vert.spv");
        auto frag = loadShader(core_.device, SHADER_DIR "srgb_convert.frag.spv");

        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag;
        stages[1].pName = "main";

        // No vertex input (fullscreen triangle generated in VS)
        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport vp{};
        vp.x = 0; vp.y = 0;
        vp.width  = (float)wc.swapchainExt.width;
        vp.height = (float)wc.swapchainExt.height;
        vp.minDepth = 0.f; vp.maxDepth = 1.f;
        VkRect2D sc{{0,0}, wc.swapchainExt};

        VkPipelineViewportStateCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vs.viewportCount = 1;
        vs.pViewports = &vp;
        vs.scissorCount = 1;
        vs.pScissors = &sc;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.lineWidth = 1.0f;
        rs.cullMode = VK_CULL_MODE_NONE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState cb{};
        cb.blendEnable = VK_FALSE;
        cb.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                          | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = 1;
        cbs.pAttachments = &cb;

        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pcRange.offset = 0;
        pcRange.size = 4;

        VkDescriptorSetLayout layouts[1] = {wc.uiDescLayout};
        VkPipelineLayoutCreateInfo plci{};
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount = 1;
        plci.pSetLayouts = layouts;
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges = &pcRange;
        vkCreatePipelineLayout(core_.device, &plci, nullptr, &wc.convertPipeLayout);

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount = 2;
        pci.pStages = stages;
        pci.pVertexInputState = &vi;
        pci.pInputAssemblyState = &ia;
        pci.pViewportState = &vs;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState = &ms;
        pci.pColorBlendState = &cbs;
        pci.layout = wc.convertPipeLayout;
        pci.renderPass = wc.convertPass;
        pci.subpass = 0;
        vkCreateGraphicsPipelines(core_.device, VK_NULL_HANDLE, 1, &pci, nullptr, &wc.convertPipeline);

        vkDestroyShaderModule(core_.device, vert, nullptr);
        vkDestroyShaderModule(core_.device, frag, nullptr);
    }

    // --- Command buffers + sync ---
    {
        VkCommandPoolCreateInfo cpci{};
        cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cpci.queueFamilyIndex = core_.graphicsFamily;
        vkCreateCommandPool(core_.device, &cpci, nullptr, &wc.cmdPool);

        uint32_t n = (uint32_t)wc.swapchainViews.size();
        wc.cmdBufs.resize(n);
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = wc.cmdPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = n;
        vkAllocateCommandBuffers(core_.device, &ai, wc.cmdBufs.data());

        wc.imageAvail.resize(MAX_FRAMES_IN_FLIGHT);
        wc.renderDone.resize(MAX_FRAMES_IN_FLIGHT);
        wc.inFlight.resize(MAX_FRAMES_IN_FLIGHT);

        VkSemaphoreCreateInfo sci{};
        sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            vkCreateSemaphore(core_.device, &sci, nullptr, &wc.imageAvail[i]);
            vkCreateSemaphore(core_.device, &sci, nullptr, &wc.renderDone[i]);
            vkCreateFence(core_.device, &fci, nullptr, &wc.inFlight[i]);
        }
    }
}

// ============================================================================
// initHDRWindow
// ============================================================================
void VulkanApp::initHDRWindow() {
    auto& wc = hdr_;
    bool needsEncode;
    // Try HDR; fallback is handled inside chooseSurfaceFormat
    VkSurfaceFormatKHR fmt = chooseSurfaceFormat(core_.physicalDevice, wc.surface, true, needsEncode);
    core_.hdrSupported = (fmt.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT);
    initWindowSwapchain(wc, core_, true, fmt.format, fmt.colorSpace);

    createRenderPasses(wc, core_);
    createConvertDescriptor(wc, core_);
    createFramebuffers(wc, core_);

    // Sampler for convert
    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxLod = 1.0f;
    vkCreateSampler(core_.device, &sci, nullptr, &wc.texSampler);

    // --- Convert descriptor set (pre-allocated once) ---
    {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = core_.uiDescPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &wc.uiDescLayout;
        vkAllocateDescriptorSets(core_.device, &ai, &wc.convertDescSet);

        VkDescriptorImageInfo ii{};
        ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        ii.imageView = wc.linearView;
        ii.sampler = wc.texSampler;

        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = wc.convertDescSet;
        w.dstBinding = 0;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &ii;
        vkUpdateDescriptorSets(core_.device, 1, &w, 0, nullptr);
    }

    // --- UI pipeline (same as SDR) ---
    {
        auto vert = loadShader(core_.device, SHADER_DIR "ui.vert.spv");
        auto frag = loadShader(core_.device, SHADER_DIR "ui.frag.spv");

        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag;
        stages[1].pName = "main";

        VkVertexInputBindingDescription vBind{};
        vBind.binding = 0;
        vBind.stride = 16;
        vBind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription vAttrs[2] = {};
        vAttrs[0].binding = 0; vAttrs[0].location = 0;
        vAttrs[0].format = VK_FORMAT_R32G32_SFLOAT; vAttrs[0].offset = 0;
        vAttrs[1].binding = 0; vAttrs[1].location = 1;
        vAttrs[1].format = VK_FORMAT_R32G32_SFLOAT; vAttrs[1].offset = 8;

        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &vBind;
        vi.vertexAttributeDescriptionCount = 2;
        vi.pVertexAttributeDescriptions = vAttrs;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport vp{};
        vp.x = 0; vp.y = 0;
        vp.width  = (float)wc.swapchainExt.width;
        vp.height = (float)wc.swapchainExt.height;
        vp.minDepth = 0.f; vp.maxDepth = 1.f;
        VkRect2D sc{{0,0}, wc.swapchainExt};

        VkPipelineViewportStateCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vs.viewportCount = 1; vs.pViewports = &vp;
        vs.scissorCount = 1; vs.pScissors = &sc;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.lineWidth = 1.0f; rs.cullMode = VK_CULL_MODE_NONE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState cb{};
        cb.blendEnable = VK_FALSE;
        cb.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                          | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = 1; cbs.pAttachments = &cb;

        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcRange.offset = 0; pcRange.size = 28;

        VkDescriptorSetLayout layouts[1] = {core_.uiDescLayout};
        VkPipelineLayoutCreateInfo plci{};
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount = 1; plci.pSetLayouts = layouts;
        plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcRange;
        vkCreatePipelineLayout(core_.device, &plci, nullptr, &wc.uiPipeLayout);

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount = 2; pci.pStages = stages;
        pci.pVertexInputState = &vi; pci.pInputAssemblyState = &ia;
        pci.pViewportState = &vs; pci.pRasterizationState = &rs;
        pci.pMultisampleState = &ms; pci.pColorBlendState = &cbs;
        pci.layout = wc.uiPipeLayout;
        pci.renderPass = wc.uiPass; pci.subpass = 0;
        vkCreateGraphicsPipelines(core_.device, VK_NULL_HANDLE, 1, &pci, nullptr, &wc.uiPipeline);

        vkDestroyShaderModule(core_.device, vert, nullptr);
        vkDestroyShaderModule(core_.device, frag, nullptr);
    }

    // --- Convert pipeline (HDR: pq_convert or srgb_convert fallback) ---
    {
        auto vert = loadShader(core_.device, SHADER_DIR "srgb_convert.vert.spv");
        auto frag = loadShader(core_.device, core_.hdrSupported
                               ? SHADER_DIR "pq_convert.frag.spv"
                               : SHADER_DIR "srgb_convert.frag.spv");

        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert; stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag; stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport vp{};
        vp.x = 0; vp.y = 0;
        vp.width  = (float)wc.swapchainExt.width;
        vp.height = (float)wc.swapchainExt.height;
        vp.minDepth = 0.f; vp.maxDepth = 1.f;
        VkRect2D sc{{0,0}, wc.swapchainExt};

        VkPipelineViewportStateCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vs.viewportCount = 1; vs.pViewports = &vp;
        vs.scissorCount = 1; vs.pScissors = &sc;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.lineWidth = 1.0f; rs.cullMode = VK_CULL_MODE_NONE;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState cb{};
        cb.blendEnable = VK_FALSE;
        cb.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                          | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = 1; cbs.pAttachments = &cb;

        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pcRange.offset = 0;
        pcRange.size = core_.hdrSupported ? 8u : 4u;

        VkDescriptorSetLayout layouts[1] = {wc.uiDescLayout};
        VkPipelineLayoutCreateInfo plci{};
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount = 1; plci.pSetLayouts = layouts;
        plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcRange;
        vkCreatePipelineLayout(core_.device, &plci, nullptr, &wc.convertPipeLayout);

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount = 2; pci.pStages = stages;
        pci.pVertexInputState = &vi; pci.pInputAssemblyState = &ia;
        pci.pViewportState = &vs; pci.pRasterizationState = &rs;
        pci.pMultisampleState = &ms; pci.pColorBlendState = &cbs;
        pci.layout = wc.convertPipeLayout;
        pci.renderPass = wc.convertPass; pci.subpass = 0;
        vkCreateGraphicsPipelines(core_.device, VK_NULL_HANDLE, 1, &pci, nullptr, &wc.convertPipeline);

        vkDestroyShaderModule(core_.device, vert, nullptr);
        vkDestroyShaderModule(core_.device, frag, nullptr);
    }

    // --- Command buffers + sync ---
    {
        VkCommandPoolCreateInfo cpci{};
        cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cpci.queueFamilyIndex = core_.graphicsFamily;
        vkCreateCommandPool(core_.device, &cpci, nullptr, &wc.cmdPool);

        uint32_t n = (uint32_t)wc.swapchainViews.size();
        wc.cmdBufs.resize(n);
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = wc.cmdPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = n;
        vkAllocateCommandBuffers(core_.device, &ai, wc.cmdBufs.data());

        wc.imageAvail.resize(MAX_FRAMES_IN_FLIGHT);
        wc.renderDone.resize(MAX_FRAMES_IN_FLIGHT);
        wc.inFlight.resize(MAX_FRAMES_IN_FLIGHT);

        VkSemaphoreCreateInfo ssci{};
        ssci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            vkCreateSemaphore(core_.device, &ssci, nullptr, &wc.imageAvail[i]);
            vkCreateSemaphore(core_.device, &ssci, nullptr, &wc.renderDone[i]);
            vkCreateFence(core_.device, &fci, nullptr, &wc.inFlight[i]);
        }
    }
}

// ============================================================================
// initImGui
// ============================================================================
static void checkVk(VkResult r) {
    if (r != VK_SUCCESS) std::cerr << "[ImGui VK] error " << r << std::endl;
}

void VulkanApp::initImGui(WindowContext& wc, bool first) {
    wc.imguiCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(wc.imguiCtx);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    ImGui_ImplGlfw_InitForVulkan(wc.window, true);

    // ImGui descriptor pool (needs COMBINED_IMAGE_SAMPLER + SAMPLER)
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10},
        {VK_DESCRIPTOR_TYPE_SAMPLER,                2},
    };
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpci.maxSets = 12;
    dpci.poolSizeCount = 2;
    dpci.pPoolSizes = poolSizes;
    vkCreateDescriptorPool(core_.device, &dpci, nullptr, &wc.imguiDescPool);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_3;
    initInfo.Instance = core_.instance;
    initInfo.PhysicalDevice = core_.physicalDevice;
    initInfo.Device = core_.device;
    initInfo.QueueFamily = core_.graphicsFamily;
    initInfo.Queue = core_.graphicsQueue;
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.DescriptorPool = wc.imguiDescPool;
    initInfo.PipelineInfoMain.RenderPass = wc.imguiPass;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.MinImageCount = (uint32_t)wc.swapchainViews.size();
    initInfo.ImageCount = (uint32_t)wc.swapchainViews.size();
    initInfo.Allocator = nullptr;
    initInfo.CheckVkResultFn = checkVk;

    ImGui_ImplVulkan_Init(&initInfo);
    // Fonts are auto-created on first NewFrame() in the docking branch

    wc.hasImGui = true;
}

// ============================================================================
// Record UI pass
// ============================================================================
static float alignCenter(int texSize, int windowSize) {
    return ((float)windowSize - (float)texSize) * 0.5f;
}

void VulkanApp::recordUIPass(WindowContext& wc, VkCommandBuffer cmd, uint32_t imageIdx,
                              float bgLinear, float alpha, float uiLum) {
    if (uiPairs_.empty()) return;

    const auto& ui = uiPairs_[currentUI_];
    if (ui.uiDescSet == VK_NULL_HANDLE) return;

    // Calculate quad offset/scale to center the UI in the window (NDC)
    // Quad in object space: [-0.5, 0.5].  After scale+offset in NDC:
    //   scale.x = 2 * uiW / winW  so quad spans [-uiW/winW, +uiW/winW]
    //   offset = (0,0) for centering
    float pcData[7] = {
        0.0f,                                            // offset.x (NDC, centered)
        0.0f,                                            // offset.y (NDC, centered)
        2.0f * (float)ui.width  / (float)wc.swapchainExt.width,   // scale.x
        2.0f * (float)ui.height / (float)wc.swapchainExt.height,  // scale.y
        alpha,                // uiAlphaMultiplier
        bgLinear,             // bgLinear
        uiLum,                // uiLumMult
    };

    VkClearValue clearVal{};
    clearVal.color = {{bgLinear, bgLinear, bgLinear, 1.0f}};

    VkRenderPassBeginInfo rpbi{};
    rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass = wc.uiPass;
    rpbi.framebuffer = wc.uiFramebuffers[imageIdx];
    rpbi.renderArea = {{0,0}, wc.swapchainExt};
    rpbi.clearValueCount = 1;
    rpbi.pClearValues = &clearVal;

    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, wc.uiPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             wc.uiPipeLayout, 0, 1, &ui.uiDescSet, 0, nullptr);

    // Push constants (offset=0, size=28)
    vkCmdPushConstants(cmd, wc.uiPipeLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, 28, pcData);

    VkDeviceSize offsets[1] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, &quadVB_, offsets);
    vkCmdDraw(cmd, 6, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

// ============================================================================
// Record convert pass
// ============================================================================
void VulkanApp::recordConvertPass(WindowContext& wc, VkCommandBuffer cmd, uint32_t imageIdx) {
    if (wc.convertDescSet == VK_NULL_HANDLE) return;

    VkClearValue clearVal{};
    clearVal.color = {{0.f, 0.f, 0.f, 0.f}};

    VkRenderPassBeginInfo rpbi{};
    rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass = wc.convertPass;
    rpbi.framebuffer = wc.convertFramebuffers[imageIdx];
    rpbi.renderArea = {{0,0}, wc.swapchainExt};
    rpbi.clearValueCount = 1;
    rpbi.pClearValues = &clearVal;

    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, wc.convertPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             wc.convertPipeLayout, 0, 1, &wc.convertDescSet, 0, nullptr);

    if (&wc == &sdr_) {
        // SDR: push uSRGBEncode
        float encode = wc.needsSRGBEncode ? 1.0f : 0.0f;
        vkCmdPushConstants(cmd, wc.convertPipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &encode);
    } else {
        // HDR
        if (core_.hdrSupported) {
            float pc[2] = {maxNit_, 1.0f};
            vkCmdPushConstants(cmd, wc.convertPipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 8, pc);
        } else {
            float encode = 1.0f;  // encode to sRGB on SDR fallback window
            vkCmdPushConstants(cmd, wc.convertPipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &encode);
        }
    }

    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

// ============================================================================
// Record ImGui pass
// ============================================================================
void VulkanApp::recordImGuiPass(WindowContext& wc, VkCommandBuffer cmd, uint32_t imageIdx) {
    if (!wc.hasImGui) return;

    VkRenderPassBeginInfo rpbi{};
    rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass = wc.imguiPass;
    rpbi.framebuffer = wc.imguiFramebuffers[imageIdx];
    rpbi.renderArea = {{0,0}, wc.swapchainExt};
    rpbi.clearValueCount = 0;
    rpbi.pClearValues = nullptr;

    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    ImGui::SetCurrentContext(wc.imguiCtx);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

    vkCmdEndRenderPass(cmd);
}

// ============================================================================
// ImGui UIs
// ============================================================================
void VulkanApp::sdrImGui() {
    ImGui::SetCurrentContext(sdr_.imguiCtx);
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
    ImGui::Begin("SDR Controls", nullptr,
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
    if (!uiPairs_.empty()) {
        ImGui::Text("UI: %s", uiPairs_[currentUI_].name.c_str());
        ImGui::Text("Size: %dx%d", uiPairs_[currentUI_].width, uiPairs_[currentUI_].height);
        ImGui::Text("Alpha avg: %.3f", uiPairs_[currentUI_].alphaAvg);
    }
    if (ImGui::Button("< Prev")) { currentUI_ = (currentUI_ + uiPairs_.size() - 1) % uiPairs_.size(); }
    ImGui::SameLine();
    if (ImGui::Button("Next >")) { currentUI_ = (currentUI_ + 1) % uiPairs_.size(); }
    ImGui::SliderFloat("Alpha", &sdrAlpha_, 0.1f, 1.0f, "%.1f");
    ImGui::End();

    ImGui::Render();
}

void VulkanApp::hdrImGui() {
    ImGui::SetCurrentContext(hdr_.imguiCtx);
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
    ImGui::Begin("HDR Controls", nullptr,
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
    if (!uiPairs_.empty()) {
        ImGui::Text("UI: %s", uiPairs_[currentUI_].name.c_str());
        ImGui::Text("Size: %dx%d", uiPairs_[currentUI_].width, uiPairs_[currentUI_].height);
        ImGui::Text("Avg Luminance: %.3f nit", uiPairs_[currentUI_].lumAvg * 500.0f);
    }
    if (ImGui::Button("< Prev")) { currentUI_ = (currentUI_ + uiPairs_.size() - 1) % uiPairs_.size(); }
    ImGui::SameLine();
    if (ImGui::Button("Next >")) { currentUI_ = (currentUI_ + 1) % uiPairs_.size(); }

    ImGui::SliderFloat("Max Nit", &maxNit_, 100.0f, 2000.0f, "%.0f");
    ImGui::SliderFloat("BG Nit",  &bgNit_,  0.0f, maxNit_, "%.0f");

    // PQ diagnostic: show actual 10-bit code value after clamp
    {
        float clampedNit = std::min(bgNit_, maxNit_);
        float y = clampedNit / 10000.0f;
        float yPow = powf(y, 2610.0f/16384.0f);
        float num = 3424.0f/4096.0f + (2413.0f/128.0f) * yPow;
        float den = 1.0f + (2392.0f/128.0f) * yPow;
        float pqVal = powf(num/den, 2523.0f/32.0f);
        int code10 = (int)(pqVal * 1023.0f);
        ImGui::Text("BG PQ: %d/1023  (%.0f nit → clamped %.0f)", code10, bgNit_, clampedNit);
        if (clampedNit < bgNit_)
            ImGui::TextColored(ImVec4(1,0.5f,0,1), "  ^ clamped by MaxNit");
    }

    // Lock: preserve uiLumNit * effAlpha product so visual brightness stays constant
    if (locked_) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
        if (ImGui::Button("Unlock")) { locked_ = false; }
        ImGui::PopStyleColor();
        if (effAlpha_ > 0.001f)
            uiLumNit_ = lumLock_ / effAlpha_;
        else
            uiLumNit_ = lumLock_;
    } else {
        if (ImGui::Button("Lock")) {
            locked_ = true;
            lumLock_ = uiLumNit_ * effAlpha_;
        }
    }
    ImGui::SameLine();
    ImGui::Text(locked_ ? "LOCKED" : "unlocked");
    ImGui::SliderFloat("UI Lum Nit", &uiLumNit_, 0.0f, 1000.0f, "%.0f");
    ImGui::SliderFloat("Eff. Alpha", &effAlpha_, 0.0f, 1.0f);
    ImGui::End();

    ImGui::Render();
}

// ============================================================================
// drawSDRFrame / drawHDRFrame
// ============================================================================
void VulkanApp::drawSDRFrame() {
    auto& wc = sdr_;
    if (glfwWindowShouldClose(wc.window)) return;

    vkWaitForFences(core_.device, 1, &wc.inFlight[wc.currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIdx;
    VkResult r = vkAcquireNextImageKHR(core_.device, wc.swapchain, UINT64_MAX,
                                        wc.imageAvail[wc.currentFrame],
                                        VK_NULL_HANDLE, &imageIdx);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
        return;
    }

    vkResetFences(core_.device, 1, &wc.inFlight[wc.currentFrame]);

    VkCommandBuffer cmd = wc.cmdBufs[imageIdx];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &bi);

    recordUIPass(wc, cmd, imageIdx, BG_GRAY, sdrAlpha_, 1.0f);
    recordConvertPass(wc, cmd, imageIdx);
    recordImGuiPass(wc, cmd, imageIdx);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &wc.imageAvail[wc.currentFrame];
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    si.pWaitDstStageMask = waitStages;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &wc.renderDone[wc.currentFrame];

    vkQueueSubmit(core_.graphicsQueue, 1, &si, wc.inFlight[wc.currentFrame]);

    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &wc.renderDone[wc.currentFrame];
    pi.swapchainCount = 1;
    pi.pSwapchains = &wc.swapchain;
    pi.pImageIndices = &imageIdx;

    vkQueuePresentKHR(core_.graphicsQueue, &pi);

    wc.currentFrame = (wc.currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanApp::drawHDRFrame() {
    auto& wc = hdr_;
    if (glfwWindowShouldClose(wc.window)) return;

    vkWaitForFences(core_.device, 1, &wc.inFlight[wc.currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIdx;
    VkResult r = vkAcquireNextImageKHR(core_.device, wc.swapchain, UINT64_MAX,
                                        wc.imageAvail[wc.currentFrame],
                                        VK_NULL_HANDLE, &imageIdx);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) return;

    vkResetFences(core_.device, 1, &wc.inFlight[wc.currentFrame]);

    VkCommandBuffer cmd = wc.cmdBufs[imageIdx];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &bi);

    // HDR: bgLinear = bgNit / 500.0, uiLumMult = uiLumNit / (lumAvg * 500)
    float bgLinear = bgNit_ / 500.0f;
    float uiLumMult = 1.0f;
    if (!uiPairs_.empty()) {
        float avgLum = uiPairs_[currentUI_].lumAvg;
        if (avgLum > 0.0001f) uiLumMult = uiLumNit_ / (avgLum * 500.0f);
    }

    recordUIPass(wc, cmd, imageIdx, bgLinear, effAlpha_, uiLumMult);
    recordConvertPass(wc, cmd, imageIdx);
    recordImGuiPass(wc, cmd, imageIdx);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &wc.imageAvail[wc.currentFrame];
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    si.pWaitDstStageMask = waitStages;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &wc.renderDone[wc.currentFrame];

    vkQueueSubmit(core_.graphicsQueue, 1, &si, wc.inFlight[wc.currentFrame]);

    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &wc.renderDone[wc.currentFrame];
    pi.swapchainCount = 1;
    pi.pSwapchains = &wc.swapchain;
    pi.pImageIndices = &imageIdx;

    vkQueuePresentKHR(core_.graphicsQueue, &pi);

    wc.currentFrame = (wc.currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

// ============================================================================
// run
// ============================================================================
void VulkanApp::run() {
    initCore();

    loadAssets(core_, uiPairs_, assetPath_);
    if (uiPairs_.empty()) std::cerr << "[WARN] No UI assets loaded from " << assetPath_ << std::endl;

    // Pre-allocate UI descriptor sets (one per pair)
    for (auto& p : uiPairs_) {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = core_.uiDescPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &core_.uiDescLayout;
        vkAllocateDescriptorSets(core_.device, &ai, &p.uiDescSet);

        VkDescriptorImageInfo rgbInfo{};
        rgbInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        rgbInfo.imageView = p.rgb.view;
        rgbInfo.sampler = core_.texSampler;

        VkDescriptorImageInfo alphaInfo{};
        alphaInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        alphaInfo.imageView = p.alpha.view;
        alphaInfo.sampler = core_.texSampler;

        VkWriteDescriptorSet writes[2] = {};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = p.uiDescSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &rgbInfo;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = p.uiDescSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &alphaInfo;
        vkUpdateDescriptorSets(core_.device, 2, writes, 0, nullptr);
    }

    createQuadBuffer(core_, quadVB_, quadVBMem_);

    initSDRWindow();
    initHDRWindow();

    initImGui(sdr_, true);
    initImGui(hdr_, false);

    // Give SDR window initial focus, then show both
    glfwFocusWindow(sdr_.window);
    if (core_.hdrSupported) glfwShowWindow(hdr_.window);

    if (!core_.hdrSupported) {
        glfwHideWindow(hdr_.window);
        std::cout << "[INFO] HDR not supported — HDR window hidden." << std::endl;
    }
    std::cout << "[INFO] Entering main loop." << std::endl;

    while (!glfwWindowShouldClose(sdr_.window)) {
        glfwPollEvents();

        sdrImGui();
        drawSDRFrame();

        if (core_.hdrSupported) {
            hdrImGui();
            drawHDRFrame();
        }
    }

    vkDeviceWaitIdle(core_.device);
    std::cout << "[INFO] Shutting down." << std::endl;
}

// ============================================================================
// main
// ============================================================================
int main() {
    try {
        VulkanApp app(ASSET_DIR);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "[FATAL] " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
