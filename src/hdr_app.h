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
    void computeLocalAvgI();
    static float pqEncInline(float nit);
    static float pqDecInline(float pq);

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
    float fgIScale_  = 1.0f;
    float bgIScale_  = 1.0f;
    float ctCpScale_ = 1.0f;
    float avgMixedNit_ = 0.0f;
    UITexture bgTexture_;
    float bgAvgNit_ = 0.0f;
    std::vector<uint8_t> bgRawRGBA_;
    int bgWidth_ = 0, bgHeight_ = 0;
    float localAvgI_ = 0.0f;
    VkBuffer readbackBuf_ = VK_NULL_HANDLE;
    VkDeviceMemory readbackMem_ = VK_NULL_HANDLE;
};