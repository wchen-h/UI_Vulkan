#pragma once
#include "common.h"

VkImageView createImageView(VkDevice dev, VkImage img, VkFormat fmt, VkImageAspectFlags aspect);
void createImage(VkDevice dev, VkPhysicalDevice phy, uint32_t w, uint32_t h, VkFormat fmt,
                 VkImageUsageFlags usage, VkImage& img, VkDeviceMemory& mem);
void transitionLayout(VkDevice dev, VkCommandPool cmdPool, VkQueue queue,
                      VkImage img, VkFormat fmt, VkImageLayout oldL, VkImageLayout newL);
void loadAssets(VulkanCore& core, std::vector<UIPair>& uiPairs, const std::string& assetPath);
UITexture loadBackgroundTexture(VulkanCore& core, const std::string& path, float& avgNit, bool hdr,
                                std::vector<uint8_t>& rawRGBA);