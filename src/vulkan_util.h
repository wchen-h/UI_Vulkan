#pragma once
#include "common.h"

#ifdef NDEBUG
constexpr bool kEnableValidation = false;
#else
constexpr bool kEnableValidation = true;
#endif

uint32_t findMemoryType(VkPhysicalDevice phy, uint32_t typeFilter, VkMemoryPropertyFlags props);
VkShaderModule createShaderModule(VkDevice dev, const std::vector<char>& code);
std::vector<char> readFile(const std::string& path);
VkShaderModule loadShader(VkDevice dev, const std::string& path);

bool checkValidationLayerSupport();
VkResult CreateDebugUtilsMessengerEXT(VkInstance inst, const VkDebugUtilsMessengerCreateInfoEXT* pCreate,
                                       const VkAllocationCallbacks* pAlloc, VkDebugUtilsMessengerEXT* pMessenger);
void DestroyDebugUtilsMessengerEXT(VkInstance inst, VkDebugUtilsMessengerEXT messenger,
                                    const VkAllocationCallbacks* pAlloc);

VkCommandBuffer beginOneShot(VkDevice dev, VkCommandPool pool);
void endOneShot(VkDevice dev, VkCommandPool pool, VkQueue queue, VkCommandBuffer cmd);
void createGPUBuffer(VkDevice dev, VkPhysicalDevice phy, VkDeviceSize size,
                     VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                     VkBuffer& buf, VkDeviceMemory& mem);

VkSurfaceFormatKHR chooseSDRSurfaceFormat(VkPhysicalDevice phy, VkSurfaceKHR surface, bool& needsEncode);
VkSurfaceFormatKHR chooseHDRSurfaceFormat(VkPhysicalDevice phy, VkSurfaceKHR surface);
VkPresentModeKHR choosePresentMode(VkPhysicalDevice phy, VkSurfaceKHR surface);
VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& caps, uint32_t w, uint32_t h);

void initVulkanCore(VulkanCore& core, WindowContext& wc, const char* windowTitle, bool hdr);
void initWindowSwapchain(WindowContext& wc, VulkanCore& core);
void recreateSwapchain(WindowContext& wc, VulkanCore& core);
void createRenderPasses(WindowContext& wc, VkDevice dev);
void createFramebuffers(WindowContext& wc, VkDevice dev);
void createConvertDescriptor(WindowContext& wc, VulkanCore& core);
void createUIPipeline(WindowContext& wc, VulkanCore& core, VkBuffer quadVB,
                      const char* fragShader);
void createConvertPipeline(WindowContext& wc, VulkanCore& core,
                           const char* vertPath, const char* fragPath, uint32_t pcSize);
void createCmdBuffersAndSync(WindowContext& wc, VulkanCore& core);
void initImGuiForWindow(WindowContext& wc, VulkanCore& core);
void createQuadBuffer(VulkanCore& core, VkBuffer& buf, VkDeviceMemory& mem);

void recordUIPass(WindowContext& wc, VkCommandBuffer cmd, uint32_t imageIdx,
                   const std::vector<UIPair>& uiPairs, int currentUI,
                   VkBuffer quadVB, float bgMultiplierI,
                   float fgAlpha, float bgAlpha,
                   float fgIScale, float bgIScale,
                   float ctCpScale);
void recordImGuiPass(WindowContext& wc, VkCommandBuffer cmd, uint32_t imageIdx);
void cleanupWindow(WindowContext& wc, VulkanCore& core);