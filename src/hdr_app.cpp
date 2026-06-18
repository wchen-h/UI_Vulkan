#include "hdr_app.h"
#include "vulkan_util.h"
#include "texture.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <algorithm>

HDRApp::HDRApp(const std::string& assetPath) : assetPath_(assetPath) {}

HDRApp::~HDRApp() {
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

    cleanupWindow(wc_, core_);

    if (core_.uiDescPool)  vkDestroyDescriptorPool(core_.device, core_.uiDescPool, nullptr);
    if (core_.uiDescLayout)vkDestroyDescriptorSetLayout(core_.device, core_.uiDescLayout, nullptr);
    if (core_.texSampler)   vkDestroySampler(core_.device, core_.texSampler, nullptr);
    if (core_.texSamplerLin)vkDestroySampler(core_.device, core_.texSamplerLin, nullptr);
    if (core_.sharedCmdPool)vkDestroyCommandPool(core_.device, core_.sharedCmdPool, nullptr);

    vkDestroyDevice(core_.device, nullptr);
    vkDestroyInstance(core_.instance, nullptr);
    glfwTerminate();
}

void HDRApp::init() {
    initVulkanCore(core_, wc_, "UI Vulkan \u2014 HDR", true);

    VkSurfaceFormatKHR fmt = chooseHDRSurfaceFormat(core_.physicalDevice, wc_.surface);
    wc_.swapchainFmt = fmt.format;
    wc_.swapchainCS  = fmt.colorSpace;
    wc_.needsSRGBEncode = false;
    core_.hdrSupported = true;

    loadAssets(core_, uiPairs_, assetPath_);
    if (uiPairs_.empty()) std::cerr << "[WARN] No UI assets loaded from " << assetPath_ << std::endl;

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

        VkWriteDescriptorSet writes[2] = {};
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
        vkUpdateDescriptorSets(core_.device, 2, writes, 0, nullptr);
    }

    createQuadBuffer(core_, quadVB_, quadVBMem_);

    initWindowSwapchain(wc_, core_);
    createRenderPasses(wc_, core_.device);

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
    createUIPipeline(wc_, core_, quadVB_);
    createConvertPipeline(wc_, core_,
                          SHADER_DIR "srgb_convert.vert.spv",
                          SHADER_DIR "pq_convert.frag.spv", 8);
    createCmdBuffersAndSync(wc_, core_);
    initImGuiForWindow(wc_, core_);

    std::cout << "[INFO] HDR window ready. Format: PQ+ST.2084" << std::endl;
}

void HDRApp::recordConvertPass(VkCommandBuffer cmd, uint32_t imageIdx) {
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
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             wc_.convertPipeLayout, 0, 1, &wc_.convertDescSet, 0, nullptr);

    float pc[2] = {(float)maxNit_, 1.0f};
    vkCmdPushConstants(cmd, wc_.convertPipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 8, pc);

    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

void HDRApp::hdrImGui() {
    ImGui::SetCurrentContext(wc_.imguiCtx);
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(800, 400), ImGuiCond_Once);
    ImGui::Begin("HDR Controls", nullptr, ImGuiWindowFlags_NoCollapse);
    ImGui::PushItemWidth(300);
    if (!uiPairs_.empty()) {
        ImGui::Text("UI: %s", uiPairs_[currentUI_].name.c_str());
        ImGui::Text("Size: %dx%d", uiPairs_[currentUI_].width, uiPairs_[currentUI_].height);
        ImGui::Text("Avg Luminance: %.3f nit", uiPairs_[currentUI_].lumAvg * 500.0f);
    }
    if (ImGui::Button("< Prev")) { currentUI_ = (currentUI_ + uiPairs_.size() - 1) % uiPairs_.size(); }
    ImGui::SameLine();
    if (ImGui::Button("Next >")) { currentUI_ = (currentUI_ + 1) % uiPairs_.size(); }

    ImGui::DragInt("Max Nit", &maxNit_, 1.0f, 100, 2000);
    ImGui::DragInt("BG Nit",  &bgNit_,  1.0f, 0, 1000);

    {
float bgNitF = (float)bgNit_;
    float maxNitF = (float)maxNit_;
    float clampedNit = std::min(bgNitF, maxNitF);
    float y = clampedNit / 10000.0f;
    float yPow = powf(y, 2610.0f/16384.0f);
    float num = 3424.0f/4096.0f + (2413.0f/128.0f) * yPow;
    float den = 1.0f + (2392.0f/128.0f) * yPow;
    float pqVal = powf(num/den, 2523.0f/32.0f);
    int code10 = (int)(pqVal * 1023.0f);
    ImGui::Text("BG PQ: %d/1023  (%d nit -> clamped %.0f)", code10, bgNit_, clampedNit);
    if (clampedNit < bgNitF)
        ImGui::TextColored(ImVec4(1,0.5f,0,1), "  ^ clamped by MaxNit");
    }

    if (locked_) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
        if (ImGui::Button("Unlock")) { locked_ = false; }
        ImGui::PopStyleColor();
        float effAlphaF = effAlpha_;
        if (effAlphaF > 0.001f)
            uiLumNit_ = (int)((lumLock_ - bgNit_ * (1.0f - effAlphaF)) / effAlphaF);
        else
            uiLumNit_ = (int)lumLock_;
    } else {
        if (ImGui::Button("Lock")) {
            locked_ = true;
            lumLock_ = (float)uiLumNit_ * effAlpha_ + (float)bgNit_ * (1.0f - effAlpha_);
        }
    }
    ImGui::SameLine();
    ImGui::Text(locked_ ? "LOCKED" : "unlocked");
    ImGui::DragInt("UI Lum Nit", &uiLumNit_, 1.0f, 0, 1000);
    ImGui::SliderFloat("Eff. Alpha", &effAlpha_, 0.0f, 1.0f);
    ImGui::DragFloat("Chroma Scale", &chromaScale_, 0.001f, 0.0f, 2.0f, "%.3f");
    ImGui::PopItemWidth();
    ImGui::End();

    ImGui::Render();
}

void HDRApp::drawFrame() {
    auto& wc = wc_;
    if (glfwWindowShouldClose(wc.window)) return;

    vkWaitForFences(core_.device, 1, &wc.inFlight[wc.currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIdx;
    VkResult r = vkAcquireNextImageKHR(core_.device, wc.swapchain, UINT64_MAX,
                                        wc.imageAvail[wc.currentFrame],
                                        VK_NULL_HANDLE, &imageIdx);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) return;

    vkResetFences(core_.device, 1, &wc.inFlight[wc.currentFrame]);

    VkCommandBuffer cmd = wc.cmdBufs[imageIdx];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &bi);

    float bgLinear = (float)bgNit_ / PAPER_WHITE_NIT;
    float uiLumMult = 1.0f;
    if (!uiPairs_.empty()) {
        float avgLum = uiPairs_[currentUI_].lumAvg;
        if (avgLum > 0.0001f) uiLumMult = (float)uiLumNit_ / (avgLum * PAPER_WHITE_NIT);
    }

    recordUIPass(wc, cmd, imageIdx, uiPairs_, currentUI_, quadVB_, bgLinear, effAlpha_, uiLumMult, chromaScale_);
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

    vkQueuePresentKHR(core_.graphicsQueue, &pi);

    wc.currentFrame = (wc.currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void HDRApp::run() {
    init();
    std::cout << "[INFO] HDR entering main loop." << std::endl;

    while (!glfwWindowShouldClose(wc_.window)) {
        glfwPollEvents();
        hdrImGui();
        drawFrame();
    }

    vkDeviceWaitIdle(core_.device);
    std::cout << "[INFO] HDR shutting down." << std::endl;
}