#include "vulkan_util.h"
#include "texture.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <cstring>
#include <stdexcept>
#include <set>
#include <algorithm>
#include <cstdint>

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    std::cerr << "[VK] " << data->pMessage << std::endl;
    return VK_FALSE;
}

bool checkValidationLayerSupport() {
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

VkResult CreateDebugUtilsMessengerEXT(
    VkInstance inst, const VkDebugUtilsMessengerCreateInfoEXT* pCreate,
    const VkAllocationCallbacks* pAlloc, VkDebugUtilsMessengerEXT* pMessenger) {
    auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(inst, "vkCreateDebugUtilsMessengerEXT");
    return fn ? fn(inst, pCreate, pAlloc, pMessenger) : VK_ERROR_EXTENSION_NOT_PRESENT;
}

void DestroyDebugUtilsMessengerEXT(
    VkInstance inst, VkDebugUtilsMessengerEXT messenger,
    const VkAllocationCallbacks* pAlloc) {
    auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(inst, "vkDestroyDebugUtilsMessengerEXT");
    if (fn) fn(inst, messenger, pAlloc);
}

uint32_t findMemoryType(VkPhysicalDevice phy, uint32_t typeFilter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phy, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((typeFilter & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    throw std::runtime_error("No suitable memory type");
}

VkShaderModule createShaderModule(VkDevice dev, const std::vector<char>& code) {
    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    ci.pCode    = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule mod;
    vkCreateShaderModule(dev, &ci, nullptr, &mod);
    return mod;
}

std::vector<char> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open: " + path);
    size_t size = file.tellg();
    std::vector<char> buf(size);
    file.seekg(0);
    file.read(buf.data(), size);
    return buf;
}

VkShaderModule loadShader(VkDevice dev, const std::string& path) {
    auto code = readFile(path);
    return createShaderModule(dev, code);
}

VkCommandBuffer beginOneShot(VkDevice dev, VkCommandPool pool) {
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

void endOneShot(VkDevice dev, VkCommandPool pool, VkQueue queue, VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(dev, pool, 1, &cmd);
}

void createGPUBuffer(VkDevice dev, VkPhysicalDevice phy, VkDeviceSize size,
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

VkSurfaceFormatKHR chooseSDRSurfaceFormat(VkPhysicalDevice phy, VkSurfaceKHR surface, bool& needsEncode) {
    uint32_t count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phy, surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(phy, surface, &count, formats.data());
    for (auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            needsEncode = false;
            return f;
        }
    }
    for (auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            needsEncode = true;
            return f;
        }
    }
    needsEncode = (formats[0].format != VK_FORMAT_B8G8R8A8_SRGB);
    return formats[0];
}

VkSurfaceFormatKHR chooseHDRSurfaceFormat(VkPhysicalDevice phy, VkSurfaceKHR surface) {
    uint32_t count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phy, surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(phy, surface, &count, formats.data());
    for (auto& f : formats) {
        if ((f.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 ||
             f.format == VK_FORMAT_A2R10G10B10_UNORM_PACK32) &&
            f.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT) {
            return f;
        }
    }
    throw std::runtime_error("No HDR surface format (HDR10 ST2084) found on this display");
}

VkPresentModeKHR choosePresentMode(VkPhysicalDevice phy, VkSurfaceKHR surface) {
    uint32_t count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(phy, surface, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(phy, surface, &count, modes.data());
    for (auto m : modes)
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& caps, uint32_t w, uint32_t h) {
    if (caps.currentExtent.width != UINT32_MAX) return caps.currentExtent;
    VkExtent2D e = {w, h};
    e.width  = std::clamp(e.width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
    e.height = std::clamp(e.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return e;
}

void initVulkanCore(VulkanCore& core, WindowContext& wc, const char* windowTitle, bool hdr) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    glfwWindowHint(GLFW_RED_BITS, mode->redBits);
    glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
    glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
    glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
    wc.window = glfwCreateWindow(mode->width, mode->height, windowTitle, monitor, nullptr);
    if (!wc.window) throw std::runtime_error("GLFW window creation failed");
    glfwGetMonitorPhysicalSize(monitor, &wc.physWidth_mm, &wc.physHeight_mm);

    std::vector<const char*> instExts;
    uint32_t glfwExtCount;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    instExts.assign(glfwExts, glfwExts + glfwExtCount);
    if (kEnableValidation) instExts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (hdr) instExts.push_back(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = windowTitle;
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

    if (vkCreateInstance(&ici, nullptr, &core.instance) != VK_SUCCESS)
        throw std::runtime_error("vkCreateInstance failed");

    VkDebugUtilsMessengerEXT dbgMsg;
    if (kEnableValidation && checkValidationLayerSupport())
        CreateDebugUtilsMessengerEXT(core.instance, &dbgCI, nullptr, &dbgMsg);

    if (glfwCreateWindowSurface(core.instance, wc.window, nullptr, &wc.surface) != VK_SUCCESS)
        throw std::runtime_error("Surface creation failed");

    uint32_t devCount;
    vkEnumeratePhysicalDevices(core.instance, &devCount, nullptr);
    if (devCount == 0) throw std::runtime_error("No Vulkan physical devices");
    std::vector<VkPhysicalDevice> phys(devCount);
    vkEnumeratePhysicalDevices(core.instance, &devCount, phys.data());

std::vector<const char*> devExts;

    auto checkDevExtSupport = [](VkPhysicalDevice pd, const std::vector<const char*>& reqExts) {
        uint32_t extCount;
        vkEnumerateDeviceExtensionProperties(pd, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> availExts(extCount);
        vkEnumerateDeviceExtensionProperties(pd, nullptr, &extCount, availExts.data());
        for (auto& req : reqExts) {
            bool found = false;
            for (auto& av : availExts) if (strcmp(req, av.extensionName) == 0) { found = true; break; }
            if (!found) return false;
        }
        return true;
    };

    auto hasDevExt = [](VkPhysicalDevice pd, const char* extName) {
        uint32_t extCount;
        vkEnumerateDeviceExtensionProperties(pd, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> availExts(extCount);
        vkEnumerateDeviceExtensionProperties(pd, nullptr, &extCount, availExts.data());
        for (auto& av : availExts) if (strcmp(extName, av.extensionName) == 0) return true;
        return false;
    };

    auto hasHDRSurfaceFmt = [](VkPhysicalDevice pd, VkSurfaceKHR surface) {
        uint32_t fmtCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface, &fmtCount, nullptr);
        std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface, &fmtCount, fmts.data());
        for (auto& f : fmts)
            if (f.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT) return true;
        return false;
    };

    auto hasPresentSupport = [](VkPhysicalDevice pd, VkSurfaceKHR surface) {
        uint32_t qfc;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qfc, nullptr);
        for (uint32_t i = 0; i < qfc; ++i) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface, &present);
            if (present) return true;
        }
        return false;
    };

    for (auto pd : phys) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(pd, &props);
        std::cout << "[DIAG] GPU: " << props.deviceName << std::endl;
        std::cout << "[DIAG]   VK_EXT_swapchain_colorspace: " << (hasDevExt(pd, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME) ? "YES" : "NO") << std::endl;
        std::cout << "[DIAG]   HDR10 ST2084 surface format: " << (hasHDRSurfaceFmt(pd, wc.surface) ? "YES" : "NO") << std::endl;
        std::cout << "[DIAG]   Can present to surface: " << (hasPresentSupport(pd, wc.surface) ? "YES" : "NO") << std::endl;
    }

    core.physicalDevice = VK_NULL_HANDLE;
    for (auto pd : phys) {
        if (hdr && !hasHDRSurfaceFmt(pd, wc.surface)) continue;
        if (!hasPresentSupport(pd, wc.surface)) continue;
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(pd, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            core.physicalDevice = pd;
            break;
        }
        if (core.physicalDevice == VK_NULL_HANDLE) core.physicalDevice = pd;
    }
    if (core.physicalDevice == VK_NULL_HANDLE)
        throw std::runtime_error("No physical device supports HDR10 ST2084 and can present to surface. Please run UI_Vulkan_SDR on SDR displays.");

    devExts = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    if (hdr && hasDevExt(core.physicalDevice, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME))
        devExts.push_back(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);

    uint32_t qfCount;
    vkGetPhysicalDeviceQueueFamilyProperties(core.physicalDevice, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qfProps(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(core.physicalDevice, &qfCount, qfProps.data());

    core.graphicsFamily = ~0u;
    for (uint32_t i = 0; i < qfCount; ++i) {
        if (!(qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(core.physicalDevice, i, wc.surface, &present);
        if (present) { core.graphicsFamily = i; break; }
    }
    if (core.graphicsFamily == ~0u)
        throw std::runtime_error("No suitable graphics+present queue family");

    float qp = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = core.graphicsFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &qp;

    VkPhysicalDeviceFeatures feats{};

    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.pQueueCreateInfos = &qci;
    dci.queueCreateInfoCount = 1;
    dci.ppEnabledExtensionNames = devExts.data();
    dci.enabledExtensionCount = (uint32_t)devExts.size();
    dci.pEnabledFeatures = &feats;

    if (vkCreateDevice(core.physicalDevice, &dci, nullptr, &core.device) != VK_SUCCESS)
        throw std::runtime_error("vkCreateDevice failed");

    vkGetDeviceQueue(core.device, core.graphicsFamily, 0, &core.graphicsQueue);

    VkCommandPoolCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = core.graphicsFamily;
    vkCreateCommandPool(core.device, &cpci, nullptr, &core.sharedCmdPool);

    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter  = VK_FILTER_NEAREST;
    sci.minFilter  = VK_FILTER_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxLod = 1.0f;
    vkCreateSampler(core.device, &sci, nullptr, &core.texSampler);

    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    vkCreateSampler(core.device, &sci, nullptr, &core.texSamplerLin);

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
    vkCreateDescriptorSetLayout(core.device, &dslci, nullptr, &core.uiDescLayout);

    VkDescriptorPoolSize dps{};
    dps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    dps.descriptorCount = 200;
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 100;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &dps;
    vkCreateDescriptorPool(core.device, &dpci, nullptr, &core.uiDescPool);
}

void initWindowSwapchain(WindowContext& wc, VulkanCore& core) {
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

    uint32_t scCount;
    vkGetSwapchainImagesKHR(core.device, wc.swapchain, &scCount, nullptr);
    wc.swapchainImages.resize(scCount);
    vkGetSwapchainImagesKHR(core.device, wc.swapchain, &scCount, wc.swapchainImages.data());

    wc.swapchainViews.resize(scCount);
    for (uint32_t i = 0; i < scCount; ++i)
        wc.swapchainViews[i] = createImageView(core.device, wc.swapchainImages[i],
                                                wc.swapchainFmt, VK_IMAGE_ASPECT_COLOR_BIT);

    createImage(core.device, core.physicalDevice, extent.width, extent.height,
                VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                wc.linearImg, wc.linearMem);
    wc.linearView = createImageView(core.device, wc.linearImg,
                                     VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT);

    transitionLayout(core.device, core.sharedCmdPool, core.graphicsQueue,
                     wc.linearImg, VK_FORMAT_R16G16B16A16_SFLOAT,
                     VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void createRenderPasses(WindowContext& wc, VkDevice dev) {
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
        vkCreateRenderPass(dev, &rpci, nullptr, &wc.uiPass);
    }

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
        vkCreateRenderPass(dev, &rpci, nullptr, &wc.convertPass);
    }

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
        vkCreateRenderPass(dev, &rpci, nullptr, &wc.imguiPass);
    }
}

void createFramebuffers(WindowContext& wc, VkDevice dev) {
    uint32_t n = (uint32_t)wc.swapchainViews.size();
    VkImageView attachments[1];

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
        vkCreateFramebuffer(dev, &fci, nullptr, &wc.uiFramebuffers[i]);
    }

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
        vkCreateFramebuffer(dev, &fci, nullptr, &wc.convertFramebuffers[i]);

        fci.renderPass = wc.imguiPass;
        vkCreateFramebuffer(dev, &fci, nullptr, &wc.imguiFramebuffers[i]);
    }
}

void createConvertDescriptor(WindowContext& wc, VulkanCore& core) {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dslci{};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 1;
    dslci.pBindings = &binding;
    vkCreateDescriptorSetLayout(core.device, &dslci, nullptr, &wc.convertDescLayout);

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = core.uiDescPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &wc.convertDescLayout;
    vkAllocateDescriptorSets(core.device, &ai, &wc.convertDescSet);

    VkDescriptorImageInfo ii{};
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ii.imageView = wc.linearView;
    ii.sampler = wc.convertSampler;

    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = wc.convertDescSet;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &ii;
    vkUpdateDescriptorSets(core.device, 1, &w, 0, nullptr);
}

void createUIPipeline(WindowContext& wc, VulkanCore& core, VkBuffer quadVB) {
    auto vert = loadShader(core.device, SHADER_DIR "ui.vert.spv");
    auto frag = loadShader(core.device, SHADER_DIR "ui.frag.spv");

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag; stages[1].pName = "main";

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

    VkDescriptorSetLayout layouts[1] = {core.uiDescLayout};
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1; plci.pSetLayouts = layouts;
    plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcRange;
    vkCreatePipelineLayout(core.device, &plci, nullptr, &wc.uiPipeLayout);

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount = 2; pci.pStages = stages;
    pci.pVertexInputState = &vi; pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vs; pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms; pci.pColorBlendState = &cbs;
    pci.layout = wc.uiPipeLayout;
    pci.renderPass = wc.uiPass; pci.subpass = 0;
    vkCreateGraphicsPipelines(core.device, VK_NULL_HANDLE, 1, &pci, nullptr, &wc.uiPipeline);

    vkDestroyShaderModule(core.device, vert, nullptr);
    vkDestroyShaderModule(core.device, frag, nullptr);
}

void createConvertPipeline(WindowContext& wc, VulkanCore& core,
                           const char* vertPath, const char* fragPath, uint32_t pcSize) {
    auto vert = loadShader(core.device, vertPath);
    auto frag = loadShader(core.device, fragPath);

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
    pcRange.offset = 0; pcRange.size = pcSize;

    VkDescriptorSetLayout layouts[1] = {wc.convertDescLayout};
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1; plci.pSetLayouts = layouts;
    plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcRange;
    vkCreatePipelineLayout(core.device, &plci, nullptr, &wc.convertPipeLayout);

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount = 2; pci.pStages = stages;
    pci.pVertexInputState = &vi; pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vs; pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms; pci.pColorBlendState = &cbs;
    pci.layout = wc.convertPipeLayout;
    pci.renderPass = wc.convertPass; pci.subpass = 0;
    vkCreateGraphicsPipelines(core.device, VK_NULL_HANDLE, 1, &pci, nullptr, &wc.convertPipeline);

    vkDestroyShaderModule(core.device, vert, nullptr);
    vkDestroyShaderModule(core.device, frag, nullptr);
}

void createCmdBuffersAndSync(WindowContext& wc, VulkanCore& core) {
    VkCommandPoolCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = core.graphicsFamily;
    vkCreateCommandPool(core.device, &cpci, nullptr, &wc.cmdPool);

    uint32_t n = (uint32_t)wc.swapchainViews.size();
    wc.cmdBufs.resize(n);
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = wc.cmdPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = n;
    vkAllocateCommandBuffers(core.device, &ai, wc.cmdBufs.data());

    wc.imageAvail.resize(MAX_FRAMES_IN_FLIGHT);
    wc.renderDone.resize(MAX_FRAMES_IN_FLIGHT);
    wc.inFlight.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkCreateSemaphore(core.device, &sci, nullptr, &wc.imageAvail[i]);
        vkCreateSemaphore(core.device, &sci, nullptr, &wc.renderDone[i]);
        vkCreateFence(core.device, &fci, nullptr, &wc.inFlight[i]);
    }
}

static void checkVk(VkResult r) {
    if (r != VK_SUCCESS) std::cerr << "[ImGui VK] error " << r << std::endl;
}

void initImGuiForWindow(WindowContext& wc, VulkanCore& core) {
    wc.imguiCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(wc.imguiCtx);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    ImGui_ImplGlfw_InitForVulkan(wc.window, true);

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
    vkCreateDescriptorPool(core.device, &dpci, nullptr, &wc.imguiDescPool);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_3;
    initInfo.Instance = core.instance;
    initInfo.PhysicalDevice = core.physicalDevice;
    initInfo.Device = core.device;
    initInfo.QueueFamily = core.graphicsFamily;
    initInfo.Queue = core.graphicsQueue;
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
    wc.hasImGui = true;
}

void createQuadBuffer(VulkanCore& core, VkBuffer& buf, VkDeviceMemory& mem) {
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
    VkBufferCopy region{}; region.size = size;
    vkCmdCopyBuffer(cmd, stag, buf, 1, &region);
    endOneShot(core.device, core.sharedCmdPool, core.graphicsQueue, cmd);

    vkDestroyBuffer(core.device, stag, nullptr);
    vkFreeMemory(core.device, stagMem, nullptr);
}

void recordUIPass(WindowContext& wc, VkCommandBuffer cmd, uint32_t imageIdx,
                  const std::vector<UIPair>& uiPairs, int currentUI,
                  VkBuffer quadVB, float bgLinear, float alpha, float uiLum) {
    if (uiPairs.empty()) return;
    const auto& ui = uiPairs[currentUI];
    if (ui.uiDescSet == VK_NULL_HANDLE) return;

    float scaleX, scaleY;
    if (wc.physWidth_mm > 0 && wc.physHeight_mm > 0) {
        float physW = UI_PHYSICAL_WIDTH_MM;
        float physH = UI_PHYSICAL_WIDTH_MM * (float)ui.height / (float)ui.width;
        scaleX = 2.0f * physW / (float)wc.physWidth_mm;
        scaleY = 2.0f * physH / (float)wc.physHeight_mm;
    } else {
        scaleX = 2.0f * (float)ui.width  / (float)wc.swapchainExt.width;
        scaleY = 2.0f * (float)ui.height / (float)wc.swapchainExt.height;
    }
    float pcData[7] = {
        0.0f, 0.0f,
        scaleX, scaleY,
        alpha, bgLinear, uiLum,
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
    vkCmdPushConstants(cmd, wc.uiPipeLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, 28, pcData);

    VkDeviceSize offsets[1] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, &quadVB, offsets);
    vkCmdDraw(cmd, 6, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

void recordImGuiPass(WindowContext& wc, VkCommandBuffer cmd, uint32_t imageIdx) {
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

void cleanupWindow(WindowContext& wc, VulkanCore& core) {
    if (wc.hasImGui) {
        ImGui::SetCurrentContext(wc.imguiCtx);
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext(wc.imguiCtx);
        wc.imguiCtx = nullptr;
    }
    if (wc.imguiDescPool) vkDestroyDescriptorPool(core.device, wc.imguiDescPool, nullptr);
    for (auto fb : wc.uiFramebuffers)      vkDestroyFramebuffer(core.device, fb, nullptr);
    for (auto fb : wc.convertFramebuffers) vkDestroyFramebuffer(core.device, fb, nullptr);
    for (auto fb : wc.imguiFramebuffers)   vkDestroyFramebuffer(core.device, fb, nullptr);
    if (wc.uiPipeline)       vkDestroyPipeline(core.device, wc.uiPipeline, nullptr);
    if (wc.uiPipeLayout)     vkDestroyPipelineLayout(core.device, wc.uiPipeLayout, nullptr);
    if (wc.convertPipeline)  vkDestroyPipeline(core.device, wc.convertPipeline, nullptr);
    if (wc.convertPipeLayout)vkDestroyPipelineLayout(core.device, wc.convertPipeLayout, nullptr);
    if (wc.convertDescLayout)vkDestroyDescriptorSetLayout(core.device, wc.convertDescLayout, nullptr);
    if (wc.convertSampler)   vkDestroySampler(core.device, wc.convertSampler, nullptr);
    if (wc.uiPass)           vkDestroyRenderPass(core.device, wc.uiPass, nullptr);
    if (wc.convertPass)      vkDestroyRenderPass(core.device, wc.convertPass, nullptr);
    if (wc.imguiPass)        vkDestroyRenderPass(core.device, wc.imguiPass, nullptr);
    if (wc.linearView)       vkDestroyImageView(core.device, wc.linearView, nullptr);
    if (wc.linearImg)        vkDestroyImage(core.device, wc.linearImg, nullptr);
    if (wc.linearMem)        vkFreeMemory(core.device, wc.linearMem, nullptr);
    for (auto v : wc.swapchainViews) vkDestroyImageView(core.device, v, nullptr);
    if (wc.swapchain) vkDestroySwapchainKHR(core.device, wc.swapchain, nullptr);
    if (wc.surface)   vkDestroySurfaceKHR(core.instance, wc.surface, nullptr);
    if (wc.window && !glfwWindowShouldClose(wc.window))
        glfwDestroyWindow(wc.window);
    if (!wc.cmdBufs.empty()) {
        vkFreeCommandBuffers(core.device, wc.cmdPool,
                             (uint32_t)wc.cmdBufs.size(), wc.cmdBufs.data());
    }
    if (wc.cmdPool) vkDestroyCommandPool(core.device, wc.cmdPool, nullptr);
    for (auto s : wc.imageAvail) vkDestroySemaphore(core.device, s, nullptr);
    for (auto s : wc.renderDone) vkDestroySemaphore(core.device, s, nullptr);
    for (auto f : wc.inFlight)   vkDestroyFence(core.device, f, nullptr);
}