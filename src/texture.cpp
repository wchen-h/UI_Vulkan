// texture.cpp — image creation, texture upload, UI asset loading
#include "common.h"
#include <cstring>
#include <cmath>
#include <filesystem>
#include <map>
#include <algorithm>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace fs = std::filesystem;

// sRGB to linear approx
static float srgb2lin(float c) {
    return c <= 0.04045f ? c/12.92f : powf((c+0.055f)/1.055f, 2.4f);
}

VkImageView createImageView(VkDevice dev, VkImage img, VkFormat fmt,
                             VkImageAspectFlags aspect) {
    VkImageViewCreateInfo ci{};
    ci.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ci.image      = img;
    ci.viewType   = VK_IMAGE_VIEW_TYPE_2D;
    ci.format     = fmt;
    ci.subresourceRange = {aspect, 0, 1, 0, 1};
    VkImageView view;
    vkCreateImageView(dev, &ci, nullptr, &view);
    return view;
}

void createImage(VkDevice dev, VkPhysicalDevice phy,
                 uint32_t w, uint32_t h, VkFormat fmt,
                 VkImageUsageFlags usage, VkImage& img, VkDeviceMemory& mem) {
    VkImageCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType     = VK_IMAGE_TYPE_2D;
    ci.extent        = {w, h, 1};
    ci.mipLevels     = 1;
    ci.arrayLayers   = 1;
    ci.format        = fmt;
    ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ci.usage         = usage;
    ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    vkCreateImage(dev, &ci, nullptr, &img);

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(dev, img, &mr);
    VkMemoryAllocateInfo ma{};
    ma.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ma.allocationSize  = mr.size;
    ma.memoryTypeIndex = findMemoryType(phy, mr.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(dev, &ma, nullptr, &mem);
    vkBindImageMemory(dev, img, mem, 0);
}

void transitionLayout(VkDevice dev, VkCommandPool cmdPool, VkQueue queue,
                      VkImage img, VkFormat fmt,
                      VkImageLayout oldL, VkImageLayout newL) {
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandPool = cmdPool;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(dev, &ai, &cmd);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = oldL;
    barrier.newLayout           = newL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = img;
    barrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkPipelineStageFlags srcS, dstS;
    if (oldL == VK_IMAGE_LAYOUT_UNDEFINED && newL == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcS = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstS = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldL == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newL == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcS = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstS = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else { srcS = dstS = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT; }

    vkCmdPipelineBarrier(cmd, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(dev, cmdPool, 1, &cmd);
}

static void uploadTexture(VulkanCore& core, int w, int h, VkFormat fmt,
                           const void* data, VkImage& img, VkDeviceMemory& mem,
                           VkImageView& view) {
    VkDeviceSize size = w * h * (fmt == VK_FORMAT_R8_UNORM ? 1 : 4);

    VkBuffer stag; VkDeviceMemory stagM;
    {
        VkBufferCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ci.size = size;
        ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        vkCreateBuffer(core.device, &ci, nullptr, &stag);
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(core.device, stag, &mr);
        VkMemoryAllocateInfo ma{};
        ma.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ma.allocationSize = mr.size;
        ma.memoryTypeIndex = findMemoryType(core.physicalDevice, mr.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(core.device, &ma, nullptr, &stagM);
        vkBindBufferMemory(core.device, stag, stagM, 0);
        void* m;
        vkMapMemory(core.device, stagM, 0, size, 0, &m);
        memcpy(m, data, size);
        vkUnmapMemory(core.device, stagM);
    }

    createImage(core.device, core.physicalDevice, w, h, fmt,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, img, mem);
    transitionLayout(core.device, core.sharedCmdPool, core.graphicsQueue,
                     img, fmt, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Copy
    {
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandPool = core.sharedCmdPool;
        ai.commandBufferCount = 1;
        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(core.device, &ai, &cmd);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {(uint32_t)w, (uint32_t)h, 1};
        vkCmdCopyBufferToImage(cmd, stag, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        vkEndCommandBuffer(cmd);
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        vkQueueSubmit(core.graphicsQueue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(core.graphicsQueue);
        vkFreeCommandBuffers(core.device, core.sharedCmdPool, 1, &cmd);
    }

    transitionLayout(core.device, core.sharedCmdPool, core.graphicsQueue,
                     img, fmt, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    view = createImageView(core.device, img, fmt, VK_IMAGE_ASPECT_COLOR_BIT);

    vkDestroyBuffer(core.device, stag, nullptr);
    vkFreeMemory(core.device, stagM, nullptr);
}

// ---- Asset loading ----
void loadAssets(VulkanCore& core, std::vector<UIPair>& uiPairs,
                const std::string& assetPath) {
    std::map<std::string,std::string> rgbMap, alphaMap;
    std::cout << "[ASSET] Scanning " << assetPath << std::endl;
    for (auto& e : fs::directory_iterator(assetPath)) {
        std::string fn = e.path().filename().string();
        if (fn.size() < 4 || fn.substr(fn.size()-4) != ".png") continue;
        auto u1 = fn.find('_'), u2 = fn.find('_', u1+1);
        if (u1 == std::string::npos || u2 == std::string::npos) continue;
        std::string type = fn.substr(u1+1, u2-u1-1);
        std::string key  = fn.substr(0,u1) + fn.substr(u2);
        if (type == "rgb") rgbMap[key] = fn;
        if (type == "alpha") alphaMap[key] = fn;
    }

    for (auto& [key, rgbFn] : rgbMap) {
        if (!alphaMap.count(key)) continue;
        std::string rp = assetPath+"/"+rgbFn, ap = assetPath+"/"+alphaMap[key];
        int wr,hr,wa,ha,ch;
        stbi_uc* rpix = stbi_load(rp.c_str(), &wr,&hr,&ch,4);
        stbi_uc* apix = stbi_load(ap.c_str(), &wa,&ha,&ch,1);
        if (!rpix || !apix) { if(rpix)stbi_image_free(rpix); if(apix)stbi_image_free(apix); continue; }

        UIPair p; p.name=key; p.width=wr; p.height=hr;
        uploadTexture(core, wr,hr,VK_FORMAT_R8G8B8A8_SRGB,rpix, p.rgb.img,p.rgb.mem,p.rgb.view);
        uploadTexture(core, wa,ha,VK_FORMAT_R8_UNORM,       apix, p.alpha.img,p.alpha.mem,p.alpha.view);

        // Precompute averages
        float sa=0,sl=0; int ca=0;
        for (int i=0;i<wr*hr;++i){
            uint8_t a8=apix[i]; if(a8==0)continue;
            float a=a8/255.0f, r=rpix[i*4]/255.0f, g=rpix[i*4+1]/255.0f, b=rpix[i*4+2]/255.0f;
            float lum=0.2126f*srgb2lin(r)+0.7152f*srgb2lin(g)+0.0722f*srgb2lin(b);
            sa+=a; sl+=lum*a; ca++;
        }
        p.alphaAvg = ca>0 ? sa/(wr*hr) : 0.001f;
        p.lumAvg   = sa>0 ? sl/sa : 0.01f;
        std::cout << "[AVG] " << key << " a=" << p.alphaAvg << " L=" << p.lumAvg << std::endl;

        stbi_image_free(rpix); stbi_image_free(apix);
        uiPairs.push_back(std::move(p));
    }
    std::cout << "[ASSET] Loaded " << uiPairs.size() << " pairs." << std::endl;
}
