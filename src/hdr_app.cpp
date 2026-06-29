#include "hdr_app.h"
#include "vulkan_util.h"
#include "texture.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <algorithm>
#include <cmath>

HDRApp::HDRApp(const std::string& assetPath) : assetPath_(assetPath) {}

HDRApp::~HDRApp() {
    if (core_.device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(core_.device);

    if (quadVB_ != VK_NULL_HANDLE) vkDestroyBuffer(core_.device, quadVB_, nullptr);
    if (quadVBMem_ != VK_NULL_HANDLE) vkFreeMemory(core_.device, quadVBMem_, nullptr);
    if (readbackBuf_ != VK_NULL_HANDLE) vkDestroyBuffer(core_.device, readbackBuf_, nullptr);
    if (readbackMem_ != VK_NULL_HANDLE) vkFreeMemory(core_.device, readbackMem_, nullptr);

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
    if (core_.sharedCmdPool)vkDestroyCommandPool(core_.device, core_.sharedCmdPool, nullptr);

    vkDestroyDevice(core_.device, nullptr);
    if (core_.debugMessenger)
        DestroyDebugUtilsMessengerEXT(core_.instance, core_.debugMessenger, nullptr);
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
    createUIPipeline(wc_, core_, quadVB_, "hdr_ui.frag");
    createConvertPipeline(wc_, core_,
                          SHADER_DIR "srgb_convert.vert.spv",
                          SHADER_DIR "pq_convert.frag.spv", 4);
    createCmdBuffersAndSync(wc_, core_);
    initImGuiForWindow(wc_, core_);

    // Readback buffer for average luminance computation (host-visible)
    {
        VkDeviceSize readbackSize = (VkDeviceSize)wc_.swapchainExt.width * wc_.swapchainExt.height * 8; // R16G16B16A16 = 8 bytes/pixel
        createGPUBuffer(core_.device, core_.physicalDevice, readbackSize,
                        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        readbackBuf_, readbackMem_);
    }

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

    float pc = (float)maxNit_;
    vkCmdPushConstants(cmd, wc_.convertPipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &pc);

    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

float HDRApp::computeAvgMixedNit() {
    if (uiPairs_.empty()) return 0.0f;
    const auto& ui = uiPairs_[currentUI_];
    if (ui.rawRGBA.empty() || ui.rawAlpha.empty()) return 0.0f;

    float bgNitF = std::min((float)bgNit_, (float)maxNit_);

    auto s2l = [](float c) -> float {
        return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
    };
    auto pqEnc = [](float nit) -> float {
        float y = nit / 10000.0f;
        if (y <= 0.0f) return 0.0f;
        float yPow = powf(y, 2610.0f / 16384.0f);
        float num = 3424.0f / 4096.0f + (2413.0f / 128.0f) * yPow;
        float den = 1.0f + (2392.0f / 128.0f) * yPow;
        return powf(num / den, 2523.0f / 32.0f);
    };
    auto pqDec = [](float v) -> float {
        if (v <= 0.0f) return 0.0f;
        float vp = powf(v, 32.0f / 2523.0f);
        float num = std::max(vp - 3424.0f / 4096.0f, 0.0f);
        float den = (2413.0f / 128.0f) - (2392.0f / 128.0f) * vp;
        if (den <= 0.0f) return 10000.0f;
        return 10000.0f * powf(num / den, 16384.0f / 2610.0f);
    };

    double totalNit = 0.0;
    int count = 0;

    for (int i = 0; i < ui.width * ui.height; ++i) {
        uint8_t a8 = ui.rawAlpha[i];
        if (a8 == 0) continue;

        float a = a8 / 255.0f;
        float r = ui.rawRGBA[i * 4] / 255.0f;
        float g = ui.rawRGBA[i * 4 + 1] / 255.0f;
        float b = ui.rawRGBA[i * 4 + 2] / 255.0f;

        float rl = s2l(r), gl = s2l(g), bl = s2l(b);
        float rN709 = rl * 350.0f, gN709 = gl * 350.0f, bN709 = bl * 350.0f;

        float rN = 0.627404f * rN709 + 0.329283f * gN709 + 0.043313f * bN709;
        float gN = 0.069097f * rN709 + 0.919540f * gN709 + 0.011362f * bN709;
        float bN = 0.016391f * rN709 + 0.088013f * gN709 + 0.895595f * bN709;

        float effA = a * effAlpha_;
        float invA = 1.0f - effA;
        rN = rN * effA + bgNitF * invA;
        gN = gN * effA + bgNitF * invA;
        bN = bN * effA + bgNitF * invA;

        float r10 = pqEnc(rN) * 1023.0f;
        float g10 = pqEnc(gN) * 1023.0f;
        float b10 = pqEnc(bN) * 1023.0f;

        float Y = 0.2627f * r10 + 0.6780f * g10 + 0.0593f * b10;
        float Cb = (-0.1396f * r10 - 0.3604f * g10 + 0.5000f * b10) + 512.0f;
        float Cr = (0.5000f * r10 - 0.4598f * g10 - 0.0402f * b10) + 512.0f;

        Y = Y * yScale_;
        Cb = 512.0f + (Cb - 512.0f) * cbcrScale_;
        Cr = 512.0f + (Cr - 512.0f) * cbcrScale_;

        Y = std::clamp(Y, 0.0f, 1023.0f);
        Cb = std::clamp(Cb, 0.0f, 1023.0f);
        Cr = std::clamp(Cr, 0.0f, 1023.0f);

        float R = Y + 1.4746f * (Cr - 512.0f);
        float B = Y + 1.8814f * (Cb - 512.0f);
        float G = (Y - 0.2627f * R - 0.0593f * B) / 0.6780f;
        R = std::clamp(R, 0.0f, 1023.0f);
        G = std::clamp(G, 0.0f, 1023.0f);
        B = std::clamp(B, 0.0f, 1023.0f);

        float rNit = pqDec(R / 1023.0f);
        float gNit = pqDec(G / 1023.0f);
        float bNit = pqDec(B / 1023.0f);

        float lum = 0.2627f * rNit + 0.6780f * gNit + 0.0593f * bNit;
        totalNit += lum;
        count++;
    }

    return count > 0 ? (float)(totalNit / count) : 0.0f;
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
        ImGui::Text("Avg Luminance (raw): %.1f nit", uiPairs_[currentUI_].lumAvg * PAPER_WHITE_NIT);
        ImGui::Text("Avg Mixed Luminance: %.1f nit", avgMixedNit_);
    }
    if (ImGui::Button("< Prev")) { currentUI_ = (currentUI_ + uiPairs_.size() - 1) % uiPairs_.size(); }
    ImGui::SameLine();
    if (ImGui::Button("Next >")) { currentUI_ = (currentUI_ + 1) % uiPairs_.size(); }

    ImGui::DragInt("Max Nit", &maxNit_, 1.0f, 100, 4000);
    ImGui::DragInt("BG Nit",  &bgNit_,  1.0f, 0, maxNit_);

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

    ImGui::Separator();
    ImGui::SliderFloat("Eff. Alpha", &effAlpha_, 0.0f, 2.0f);
    ImGui::DragFloat("Y-Scale", &yScale_, 0.01f, 0.0f, 10.0f, "%.3f");
    ImGui::DragFloat("CbCr-Scale", &cbcrScale_, 0.001f, 0.0f, 3.0f, "%.3f");
    ImGui::PopItemWidth();
    ImGui::End();

    ImGui::Render();
}

void HDRApp::drawFrame() {
    auto& wc = wc_;
    if (glfwWindowShouldClose(wc.window)) return;

    vkWaitForFences(core_.device, 1, &wc.inFlight[wc.currentFrame], VK_TRUE, UINT64_MAX);

    // Read back previous frame's rendered result and compute average mixed luminance
    if (readbackBuf_ != VK_NULL_HANDLE && !uiPairs_.empty()) {
        const auto& ui = uiPairs_[currentUI_];
        if (!ui.rawRGBA.empty() && !ui.rawAlpha.empty()) {
            VkDeviceSize bufSize = (VkDeviceSize)wc.swapchainExt.width * wc.swapchainExt.height * 8;
            void* mapped = nullptr;
            if (vkMapMemory(core_.device, readbackMem_, 0, bufSize, 0, &mapped) == VK_SUCCESS) {
                // Intermediate is R16G16B16A16_SFLOAT = 4×half = 8 bytes/pixel
                // hdr_ui.frag outputs PQ code values [0,1] in RGB (alpha=1.0)
                const uint16_t* px = static_cast<const uint16_t*>(mapped);
                int w = wc.swapchainExt.width;
                int h = wc.swapchainExt.height;

                auto halfToFloat = [](uint16_t h) -> float {
                    // IEEE 754 half float -> float
                    int sign = (h >> 15) & 1;
                    int exp  = (h >> 10) & 0x1f;
                    int mant = h & 0x3ff;
                    float val;
                    if (exp == 0)      val = mant * 5.9604645e-08f;
                    else if (exp == 31) val = mant ? 0.0f : 0.0f;
                    else                val = ldexpf(1.0f + mant / 1024.0f, exp - 15);
                    return sign ? -val : val;
                };
                auto pqDec = [](float v) -> float {
                    if (v <= 0.0f) return 0.0f;
                    float vp = powf(v, 32.0f / 2523.0f);
                    float num = std::max(vp - 3424.0f / 4096.0f, 0.0f);
                    float den = (2413.0f / 128.0f) - (2392.0f / 128.0f) * vp;
                    if (den <= 0.0f) return 10000.0f;
                    return 10000.0f * powf(num / den, 16384.0f / 2610.0f);
                };

                double totalNit = 0.0;
                int count = 0;

                // The UI quad is centered in the window; compute its pixel range
                float scaleX, scaleY;
                if (wc.pxPerMm > 0.0f) {
                    float physW = (float)ui.width * 25.4f / UI_REFERENCE_DPI;
                    float physH = (float)ui.height * 25.4f / UI_REFERENCE_DPI;
                    float winPhysW = (float)w / wc.pxPerMm;
                    float winPhysH = (float)h / wc.pxPerMm;
                    scaleX = physW / winPhysW;
                    scaleY = physH / winPhysH;
                } else {
                    scaleX = (float)ui.width / (float)w;
                    scaleY = (float)ui.height / (float)h;
                }
                int quadW = (int)((float)w * scaleX);
                int quadH = (int)((float)h * scaleY);
                int quadX0 = (w - quadW) / 2;
                int quadY0 = (h - quadH) / 2;

                for (int y = 0; y < quadH && y < ui.height; ++y) {
                    for (int x = 0; x < quadW && x < ui.width; ++x) {
                        int uiIdx = y * ui.width + x;
                        if (ui.rawAlpha[uiIdx] == 0) continue;

                        int pxIdx = ((quadY0 + y) * w + (quadX0 + x)) * 4; // R16G16B16A16 = 4 halfs
                        float rPQ = halfToFloat(px[pxIdx + 0]);
                        float gPQ = halfToFloat(px[pxIdx + 1]);
                        float bPQ = halfToFloat(px[pxIdx + 2]);

                        float rNit = pqDec(rPQ);
                        float gNit = pqDec(gPQ);
                        float bNit = pqDec(bPQ);

                        float lum = 0.2627f * rNit + 0.6780f * gNit + 0.0593f * bNit;
                        totalNit += lum;
                        count++;
                    }
                }

                vkUnmapMemory(core_.device, readbackMem_);
                avgMixedNit_ = count > 0 ? (float)(totalNit / count) : 0.0f;
            }
        }
    }

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

    // HDR: pass bgNit (for shader mixing) + PQ(bgNit) (for clear color)
    float bgNitF = (float)bgNit_;
    float y = std::min(bgNitF, (float)maxNit_) / 10000.0f;
    float yPow = powf(y, 2610.0f/16384.0f);
    float num = 3424.0f/4096.0f + (2413.0f/128.0f) * yPow;
    float den = 1.0f + (2392.0f/128.0f) * yPow;
    float pqBg = powf(num/den, 2523.0f/32.0f);
    recordUIPass(wc, cmd, imageIdx, uiPairs_, currentUI_, quadVB_, bgNitF, effAlpha_, yScale_, cbcrScale_, pqBg);

    // Copy linear intermediate to readback buffer for average luminance computation
    if (readbackBuf_ != VK_NULL_HANDLE) {
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = wc.linearImg;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = wc.swapchainExt.width;
        region.bufferImageHeight = wc.swapchainExt.height;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {wc.swapchainExt.width, wc.swapchainExt.height, 1};
        vkCmdCopyImageToBuffer(cmd, wc.linearImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               readbackBuf_, 1, &region);

        // Barrier back to SHADER_READ_ONLY for convert pass
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    }

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

void HDRApp::run() {
    init();
    std::cout << "[INFO] HDR entering main loop." << std::endl;

    while (!glfwWindowShouldClose(wc_.window)) {
        glfwPollEvents();
        if (wc_.framebufferResized) { wc_.framebufferResized = false; recreateSwapchain(wc_, core_); }
        hdrImGui();
        drawFrame();
    }

    vkDeviceWaitIdle(core_.device);
    std::cout << "[INFO] HDR shutting down." << std::endl;
}