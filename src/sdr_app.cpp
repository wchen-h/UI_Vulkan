#include "sdr_app.h"
#include "vulkan_util.h"
#include "texture.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

SDRApp::SDRApp(const std::string& assetPath) : assetPath_(assetPath) {}

SDRApp::~SDRApp() {
    if (core_.device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(core_.device);

    if (quadVB_ != VK_NULL_HANDLE) vkDestroyBuffer(core_.device, quadVB_, nullptr);
    if (quadVBMem_ != VK_NULL_HANDLE) vkFreeMemory(core_.device, quadVBMem_, nullptr);

    for (auto& p : uiPairs_) {
        if (p.rgb.view) vkDestroyImageView(core_.device, p.rgb.view, nullptr);
        if (p.rgb.img)  vkDestroyImage(core_.device, p.rgb.img, nullptr);
        if (p.rgb.mem)  vkFreeMemory(core_.device, p.rgb.mem, nullptr);
        if (p.alpha.view) vkDestroyImageView(core_.device, p.alpha.view, nullptr);
        if (p.alpha.img)  vkDestroyImage(core_.device, p.alpha.img, nullptr);
        if (p.alpha.mem)  vkFreeMemory(core_.device, p.alpha.mem, nullptr);
    }

    for (auto& bg : bgTextures_) {
        if (bg.view) vkDestroyImageView(core_.device, bg.view, nullptr);
        if (bg.img)  vkDestroyImage(core_.device, bg.img, nullptr);
        if (bg.mem)  vkFreeMemory(core_.device, bg.mem, nullptr);
    }

    cleanupWindow(wc_, core_);

    if (core_.uiDescPool)  vkDestroyDescriptorPool(core_.device, core_.uiDescPool, nullptr);
    if (core_.uiDescLayout)vkDestroyDescriptorSetLayout(core_.device, core_.uiDescLayout, nullptr);
    if (core_.texSampler)   vkDestroySampler(core_.device, core_.texSampler, nullptr);
    if (core_.sharedCmdPool)vkDestroyCommandPool(core_.device, core_.sharedCmdPool, nullptr);

    vkDestroyDevice(core_.device, nullptr);
    if (core_.debugMessenger)
        DestroyDebugUtilsMessengerEXT(core_.instance, core_.debugMessenger, nullptr);
    vkDestroyInstance(core_.instance, nullptr);
    glfwTerminate();
}

void SDRApp::init() {
    initVulkanCore(core_, wc_, "UI Vulkan \u2014 SDR", false);

    bool needsEncode;
    VkSurfaceFormatKHR fmt = chooseSDRSurfaceFormat(core_.physicalDevice, wc_.surface, needsEncode);
    wc_.swapchainFmt = fmt.format;
    wc_.swapchainCS  = fmt.colorSpace;
    wc_.needsSRGBEncode = needsEncode;

    loadAssets(core_, uiPairs_, assetPath_);
    if (uiPairs_.empty()) std::cerr << "[WARN] No UI assets loaded from " << assetPath_ << std::endl;

    // Load all *_rotate.png backgrounds + white
    {
        std::cout << "[BG] Scanning: " << BG_IMAGE_DIR << std::endl;
        std::vector<std::string> bgFiles;
        try {
            namespace fs = std::filesystem;
            for (auto& e : fs::directory_iterator(BG_IMAGE_DIR)) {
                std::string fn = e.path().filename().string();
                std::cout << "[BG]   found: " << fn << " (len=" << fn.size() << ")" << std::endl;
                if (fn.find("_rotate.png") != std::string::npos)
                    bgFiles.push_back(fn);
            }
        } catch (const std::exception& ex) {
            std::cerr << "[BG] ERROR scanning directory: " << ex.what() << std::endl;
        }
        std::sort(bgFiles.begin(), bgFiles.end());

        for (auto& fn : bgFiles) {
            float avgNit;
            std::vector<uint8_t> raw;
            std::string fullPath = std::string(BG_IMAGE_DIR) + "/" + fn;
            UITexture tex = loadBackgroundTexture(core_, fullPath, avgNit, false, raw);
            if (tex.view == VK_NULL_HANDLE) continue;
            bgTextures_.push_back(tex);
            bgRawList_.push_back(std::move(raw));
            bgWList_.push_back(tex.width);
            bgHList_.push_back(tex.height);
            bgNames_.push_back(fn);
        }

        uint8_t whitePix[4] = {255, 255, 255, 255};
        std::vector<uint8_t> whiteRaw;
        UITexture whiteTex = createSolidTexture(core_, 64, 64, whitePix, whiteRaw);
        bgTextures_.push_back(whiteTex);
        bgRawList_.push_back(std::move(whiteRaw));
        bgWList_.push_back(64);
        bgHList_.push_back(64);
        bgNames_.push_back("White");
        currentBG_ = 0;
    }

    for (auto& p : uiPairs_) {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = core_.uiDescPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &core_.uiDescLayout;
        vkAllocateDescriptorSets(core_.device, &ai, &p.uiDescSet);

        VkDescriptorImageInfo rgbInfo{};
        rgbInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        rgbInfo.imageView = p.rgb.view;
        rgbInfo.sampler = core_.texSampler;

        VkDescriptorImageInfo alphaInfo{};
        alphaInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        alphaInfo.imageView = p.alpha.view;
        alphaInfo.sampler = core_.texSampler;

        VkDescriptorImageInfo bgInfo{};
        bgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bgInfo.imageView = bgTextures_[currentBG_].view;
        bgInfo.sampler = core_.texSampler;

        VkWriteDescriptorSet writes[3] = {};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = p.uiDescSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &rgbInfo;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = p.uiDescSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &alphaInfo;
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = p.uiDescSet;
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].pImageInfo = &bgInfo;
        vkUpdateDescriptorSets(core_.device, 3, writes, 0, nullptr);
    }

    createQuadBuffer(core_, quadVB_, quadVBMem_);

    initWindowSwapchain(wc_, core_);
    createRenderPasses(wc_, core_.device);
    computeLocalAvgNit();
    updateBGDescriptorSets();

    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxLod = 1.0f;
    vkCreateSampler(core_.device, &sci, nullptr, &wc_.convertSampler);

    createConvertDescriptor(wc_, core_);
    createFramebuffers(wc_, core_.device);
    createUIPipeline(wc_, core_, quadVB_, "sdr_ui.frag");
    createConvertPipeline(wc_, core_,
                          SHADER_DIR "srgb_convert.vert.spv",
                          SHADER_DIR "srgb_convert.frag.spv", 4);
    createCmdBuffersAndSync(wc_, core_);
    initImGuiForWindow(wc_, core_);

    std::cout << "[INFO] SDR window ready. Format: "
              << (wc_.needsSRGBEncode ? "UNORM+manual sRGB" : "SRGB+hw encode") << std::endl;
}

void SDRApp::recordConvertPass(VkCommandBuffer cmd, uint32_t imageIdx) {
    if (wc_.convertDescSet == VK_NULL_HANDLE) return;

    VkClearValue clearVal{};
    clearVal.color = {{0.f, 0.f, 0.f, 0.f}};

    VkRenderPassBeginInfo rpbi{};
    rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass = wc_.convertPass;
    rpbi.framebuffer = wc_.convertFramebuffers[imageIdx];
    rpbi.renderArea = {{0,0}, wc_.swapchainExt};
    rpbi.clearValueCount = 1;
    rpbi.pClearValues = &clearVal;

    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, wc_.convertPipeline);
    VkViewport vp{};
    vp.x = 0; vp.y = 0;
    vp.width  = (float)wc_.swapchainExt.width;
    vp.height = (float)wc_.swapchainExt.height;
    vp.minDepth = 0.f; vp.maxDepth = 1.f;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0,0}, wc_.swapchainExt};
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             wc_.convertPipeLayout, 0, 1, &wc_.convertDescSet, 0, nullptr);

    float encode = wc_.needsSRGBEncode ? 1.0f : 0.0f;
    vkCmdPushConstants(cmd, wc_.convertPipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &encode);

    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

void SDRApp::sdrImGui() {
    ImGui::SetCurrentContext(wc_.imguiCtx);
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(800, 400), ImGuiCond_Once);
    ImGui::Begin("SDR Controls", nullptr, ImGuiWindowFlags_NoCollapse);
    ImGui::PushItemWidth(300);
    if (!uiPairs_.empty()) {
        ImGui::Text("UI: %s", uiPairs_[currentUI_].name.c_str());
        ImGui::Text("Size: %dx%d", uiPairs_[currentUI_].width, uiPairs_[currentUI_].height);
        ImGui::Text("Alpha avg: %.3f", uiPairs_[currentUI_].alphaAvg);
        ImGui::Text("Local: %.1f nit  Multiplier: %.4f", localAvgNit_, bgMultiplier_);
    }
    if (ImGui::Button("< Prev")) { currentUI_ = (currentUI_ + uiPairs_.size() - 1) % uiPairs_.size(); computeLocalAvgNit(); }
    ImGui::SameLine();
    if (ImGui::Button("Next >")) { currentUI_ = (currentUI_ + 1) % uiPairs_.size(); computeLocalAvgNit(); }
    ImGui::SameLine();
    if (ImGui::Button("Next BG")) { switchBackground(); }
    if (!bgNames_.empty()) {
        ImGui::Text("BG: %s (%d/%d)", bgNames_[currentBG_].c_str(), currentBG_+1, (int)bgTextures_.size());
    }
    ImGui::SliderFloat("FG Alpha", &sdrFgAlpha_, 0.1f, 1.0f, "%.1f");
    ImGui::SliderFloat("BG Alpha", &sdrBgAlpha_, 0.1f, 1.0f, "%.1f");
    ImGui::RadioButton("Linear Blend", &blendingMode_, 0); ImGui::SameLine();
    ImGui::RadioButton("sRGB Blend", &blendingMode_, 1);
    ImGui::PopItemWidth();
    ImGui::End();

    ImGui::Render();
}

void SDRApp::updateBGDescriptorSets() {
    if (bgTextures_.empty()) return;
    for (auto& p : uiPairs_) {
        VkDescriptorImageInfo bgInfo{};
        bgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bgInfo.imageView = bgTextures_[currentBG_].view;
        bgInfo.sampler = core_.texSampler;
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = p.uiDescSet;
        write.dstBinding = 2;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &bgInfo;
        vkUpdateDescriptorSets(core_.device, 1, &write, 0, nullptr);
    }
}

void SDRApp::switchBackground() {
    if (bgTextures_.empty()) return;
    currentBG_ = (currentBG_ + 1) % bgTextures_.size();
    updateBGDescriptorSets();
    computeLocalAvgNit();
}

void SDRApp::computeLocalAvgNit() {
    if (uiPairs_.empty() || bgTextures_.empty()) return;
    const auto& raw = bgRawList_[currentBG_];
    int bgW = bgWList_[currentBG_];
    int bgH = bgHList_[currentBG_];
    if (raw.empty() || bgW == 0 || bgH == 0) return;
    const auto& ui = uiPairs_[currentUI_];

    float fracX, fracY;
    if (wc_.pxPerMm > 0.0f) {
        float physW = (float)ui.width  * 25.4f / UI_REFERENCE_DPI;
        float physH = (float)ui.height * 25.4f / UI_REFERENCE_DPI;
        float winPhysW = (float)wc_.swapchainExt.width  / wc_.pxPerMm;
        float winPhysH = (float)wc_.swapchainExt.height / wc_.pxPerMm;
        fracX = physW / winPhysW;
        fracY = physH / winPhysH;
    } else {
        fracX = (float)ui.width  / (float)wc_.swapchainExt.width;
        fracY = (float)ui.height / (float)wc_.swapchainExt.height;
    }

    float loX = std::max(0.0f, 0.5f - fracX);
    float hiX = std::min(1.0f, 0.5f + fracX);
    float loY = std::max(0.0f, 0.5f - fracY);
    float hiY = std::min(1.0f, 0.5f + fracY);

    int bx0 = (int)(loX * bgW);
    int bx1 = (int)(hiX * bgW);
    int by0 = (int)(loY * bgH);
    int by1 = (int)(hiY * bgH);
    if (bx0 >= bx1) { bx0 = 0; bx1 = bgW; }
    if (by0 >= by1) { by0 = 0; by1 = bgH; }
    if (bx1 > bgW) bx1 = bgW;
    if (by1 > bgH) by1 = bgH;

    auto s2l = [](float c) -> float {
        return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
    };

    double totalNit = 0.0;
    int count = 0;
    for (int y = by0; y < by1; ++y) {
        for (int x = bx0; x < bx1; ++x) {
            int idx = (y * bgW + x) * 4;
            float r = raw[idx]     / 255.0f;
            float g = raw[idx + 1] / 255.0f;
            float b = raw[idx + 2] / 255.0f;
            float rl = s2l(r), gl = s2l(g), bl = s2l(b);
            float Y = 0.2126f * rl + 0.7152f * gl + 0.0722f * bl;
            totalNit += Y * PAPER_WHITE_NIT;
            count++;
        }
    }
    localAvgNit_ = count > 0 ? (float)(totalNit / count) : 0.0f;
    bgMultiplier_ = 1.0f;
    std::cout << "[LocalAvg] UI=" << currentUI_ << " region=[" << (bx1-bx0) << "x" << (by1-by0)
              << "] localAvg=" << localAvgNit_ << " multiplier=" << bgMultiplier_ << std::endl;
}

void SDRApp::drawFrame() {
    auto& wc = wc_;
    if (glfwWindowShouldClose(wc.window)) return;

    vkWaitForFences(core_.device, 1, &wc.inFlight[wc.currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIdx;
    VkResult r = vkAcquireNextImageKHR(core_.device, wc.swapchain, UINT64_MAX,
                                        wc.imageAvail[wc.currentFrame],
                                        VK_NULL_HANDLE, &imageIdx);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapchain(wc, core_); return; }
    // VK_SUBOPTIMAL_KHR: image acquired, render this frame then recreate next frame.

    vkResetFences(core_.device, 1, &wc.inFlight[wc.currentFrame]);

    VkCommandBuffer cmd = wc.cmdBufs[imageIdx];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &bi);

    recordUIPass(wc, cmd, imageIdx, uiPairs_, currentUI_, quadVB_, bgMultiplier_,
                 sdrFgAlpha_, sdrBgAlpha_, 1.0f, (float)blendingMode_, 1.0f);
    recordConvertPass(cmd, imageIdx);
    recordImGuiPass(wc, cmd, imageIdx);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &wc.imageAvail[wc.currentFrame];
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    si.pWaitDstStageMask = waitStages;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &wc.renderDone[wc.currentFrame];

    vkQueueSubmit(core_.graphicsQueue, 1, &si, wc.inFlight[wc.currentFrame]);

    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &wc.renderDone[wc.currentFrame];
    pi.swapchainCount = 1;
    pi.pSwapchains = &wc.swapchain;
    pi.pImageIndices = &imageIdx;

    VkResult pr = vkQueuePresentKHR(core_.graphicsQueue, &pi);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) recreateSwapchain(wc, core_);

    wc.currentFrame = (wc.currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void SDRApp::run() {
    init();
    std::cout << "[INFO] SDR entering main loop." << std::endl;

    while (!glfwWindowShouldClose(wc_.window)) {
        glfwPollEvents();
        if (wc_.framebufferResized) { wc_.framebufferResized = false; recreateSwapchain(wc_, core_); computeLocalAvgNit(); }
        sdrImGui();
        drawFrame();
    }

    vkDeviceWaitIdle(core_.device);
    std::cout << "[INFO] SDR shutting down." << std::endl;
}