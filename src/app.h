// app.h — VulkanApp dual-window application
#pragma once
#include "common.h"
#include <vector>
#include <string>

class VulkanApp {
public:
    VulkanApp(const std::string& assetPath);
    ~VulkanApp();
    void run();

private:
    void initCore();
    void initSDRWindow();
    void initHDRWindow();
    void initImGui(WindowContext& wc, bool first);
    void drawSDRFrame();
    void drawHDRFrame();
    void recordUIPass(WindowContext& wc, VkCommandBuffer cmd, uint32_t imageIdx,
                      float bgLinear, float alpha, float uiLum);
    void recordConvertPass(WindowContext& wc, VkCommandBuffer cmd, uint32_t imageIdx);
    void recordImGuiPass(WindowContext& wc, VkCommandBuffer cmd, uint32_t imageIdx);
    void sdrImGui();
    void hdrImGui();

    VulkanCore core_;
    std::string assetPath_;

    WindowContext sdr_, hdr_;

    // Shared resources
    std::vector<UIPair> uiPairs_;
    int currentUI_ = 0;
    VkBuffer quadVB_ = VK_NULL_HANDLE;
    VkDeviceMemory quadVBMem_ = VK_NULL_HANDLE;

    // SDR controls
    float sdrAlpha_ = 1.0f;

    // HDR controls
    float maxNit_  = 4000.0f, bgNit_ = 500.0f, uiLumNit_ = 500.0f, effAlpha_ = 1.0f;
    bool  locked_  = false;
    float lumLock_ = 0.0f;
};
