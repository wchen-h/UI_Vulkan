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
    void computeLocalAvgNit();

    VulkanCore core_;
    WindowContext wc_;
    std::string assetPath_;
    std::vector<UIPair> uiPairs_;
    int currentUI_ = 0;
    VkBuffer quadVB_ = VK_NULL_HANDLE;
    VkDeviceMemory quadVBMem_ = VK_NULL_HANDLE;
    float sdrFgAlpha_ = 1.0f;
    float sdrBgAlpha_ = 1.0f;
    UITexture bgTexture_;
    float bgAvgNit_ = 0.0f;
    float bgMultiplier_ = 0.0f;
    std::vector<uint8_t> bgRawRGBA_;
    int bgWidth_ = 0, bgHeight_ = 0;
    float localAvgNit_ = 0.0f;
};