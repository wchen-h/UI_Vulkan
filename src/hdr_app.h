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

    VulkanCore core_;
    WindowContext wc_;
    std::string assetPath_;
    std::vector<UIPair> uiPairs_;
    int currentUI_ = 0;
    VkBuffer quadVB_ = VK_NULL_HANDLE;
    VkDeviceMemory quadVBMem_ = VK_NULL_HANDLE;
    int maxNit_  = 4000, bgNit_ = 500;
    float uiLumNit_ = 500.0f;
    float effAlpha_ = 1.0f;
    float chromaScale_ = 1.0f;
    bool  locked_  = false;
    float lumLock_ = 0.0f;
    // Debug: center pixel clamped nit values (R,G,B)
    float dbgClampedNit_[3] = {0,0,0};
    // Readback buffer for GPU→CPU pixel read
    VkBuffer readbackBuf_ = VK_NULL_HANDLE;
    VkDeviceMemory readbackMem_ = VK_NULL_HANDLE;
};