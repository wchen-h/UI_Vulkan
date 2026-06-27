#pragma once
#include "common.h"
#include "cam16.h"
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
    float qScale_     = 1.0f;
    float avgMixedNit_ = 0.0f;
    int   outOfGamutCount_ = 0;
    VkBuffer readbackBuf_ = VK_NULL_HANDLE;
    VkDeviceMemory readbackMem_ = VK_NULL_HANDLE;
    // CAM16 staging: CPU-computed adjusted PQ values, copied to swapchain
    VkBuffer cam16StagingBuf_ = VK_NULL_HANDLE;
    VkDeviceMemory cam16StagingMem_ = VK_NULL_HANDLE;
    bool cam16Dirty_ = true;
};