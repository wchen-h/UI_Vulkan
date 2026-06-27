#pragma once
#include "common.h"
#include <string>
#include <vector>

class HDRApp {
public:
    HDRApp(const std::string& assetPath);
    ~HDRApp();
    void run();

private:
    void init();
    void drawFrame();
    void hdrImGui();
    void recordConvertPass(VkCommandBuffer cmd, uint32_t imageIdx);
    float computeAvgMixedNit();

    VulkanCore core_;
    WindowContext wc_;
    std::string assetPath_;
    std::vector<UIPair> uiPairs_;
    int currentUI_ = 0;
    VkBuffer quadVB_ = VK_NULL_HANDLE;
    VkDeviceMemory quadVBMem_ = VK_NULL_HANDLE;
    int maxNit_  = 4000, bgNit_ = 500;
    float effAlpha_ = 1.0f;
    float yScale_    = 1.0f;
    float cbcrScale_ = 1.0f;
};