#pragma once
#include "common.h"
#include <string>
#include <vector>

class SDRApp {
public:
    SDRApp(const std::string& assetPath);
    ~SDRApp();
    void run();

private:
    void init();
    void drawFrame();
    void sdrImGui();
    void recordConvertPass(VkCommandBuffer cmd, uint32_t imageIdx);

    VulkanCore core_;
    WindowContext wc_;
    std::string assetPath_;
    std::vector<UIPair> uiPairs_;
    int currentUI_ = 0;
    VkBuffer quadVB_ = VK_NULL_HANDLE;
    VkDeviceMemory quadVBMem_ = VK_NULL_HANDLE;
    float sdrAlpha_ = 1.0f;
};