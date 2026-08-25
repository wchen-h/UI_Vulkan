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
    void computeLocalAvgNit();
    void switchBackground();
    void updateBGDescriptorSets();

    VulkanCore core_;
    WindowContext wc_;
    std::string assetPath_;
    std::vector<UIPair> uiPairs_;
    int currentUI_ = 0;
    VkBuffer quadVB_ = VK_NULL_HANDLE;
    VkDeviceMemory quadVBMem_ = VK_NULL_HANDLE;
    int maxNit_  = 4000, bgNit_ = 124;
    float fgAlpha_   = 1.0f;
    float bgAlpha_   = 1.0f;
    float fgYScale_  = 1.0f;
    float cbcrScale_ = 1.0f;
    int   blendingMode_ = 0;  // 0=linear alpha blend, 1=sRGB alpha blend
    float avgMixedNit_ = 0.0f;
    std::vector<UITexture> bgTextures_;
    std::vector<std::vector<uint8_t>> bgRawList_;
    std::vector<int> bgWList_, bgHList_;
    std::vector<std::string> bgNames_;
    int currentBG_ = 0;
    float localAvgNit_ = 0.0f;
    VkBuffer readbackBuf_ = VK_NULL_HANDLE;
    VkDeviceMemory readbackMem_ = VK_NULL_HANDLE;
};