# 问题与解决总结

## 软件问题

### 1. CMake include 路径不传播导致 `vulkan/vulkan.h` 找不到

**现象**：Windows 编译报错 `C1083: "vulkan/vulkan.h" no such file or directory`

**原因**：`ui_vulkan_common` 的 `target_include_directories` 和 `target_link_libraries` 均为 PRIVATE，Vulkan 头文件路径不传播到依赖它的 SDR/HDR 可执行文件

**解决**：将 `ui_vulkan_common` 的 include directories 改为 PUBLIC，link `Vulkan::Vulkan` 改为 PUBLIC，让依赖目标自动继承路径（commit `5b22254`, `875c0af`）

---

### 2. `VK_EXT_swapchain_colorspace` 扩展导致 `vkCreateDevice` 失败

**现象**：RTX 6000 + HDR 显示器上 `UI_Vulkan_HDR.exe` 报 `vkCreateDevice failed`

**原因**：代码硬性请求 `VK_EXT_swapchain_colorspace` 作为设备扩展，但 NVIDIA Windows 驱动不暴露该扩展。NVIDIA 通过 OS DXGI HDR 机制直接提供 HDR10 ST2084 surface format，不需要此扩展

**解决**：改为仅在该扩展可用时才请求它；物理设备选择改为基于 HDR surface format 是否存在而非扩展是否存在（commit `8f2f51a`）

---

### 3. 物理设备选择不考虑 surface presentation 支持

**现象**：双 GPU 系统（集显 + 独显）中，HDR 显示器接在集显上时 `vkCreateDevice` 失败

**原因**：代码优先选独显，但独显无法 present 到连接在集显上的 surface

**解决**：在选物理设备时同时检查 surface presentation 支持和 HDR surface format（commit `b09103e`, `c6eb420`, `8f2f51a`）

---

### 4. SDR 与 HDR 背景亮度不匹配

**现象**：同一台 HDR 显示器上，SDR 窗口 18% 灰背景（0.18 linear）远暗于 HDR 窗口 BG Nit=90 的背景

**原因**：SDR 的 bgLinear=0.18 是相对值（占显示器 paper white 的 18%），实际亮度取决于 paper white。HDR 的 PQ 是绝对亮度编码（90 nit 就是 90 nit）。两者只在 paper white=500 nit 时匹配，但实际 paper white 通常远低于 500 nit

**解决**：改为使用固定 PAPER_WHITE_NIT=350 nit 作为统一参考。SDR bgLinear=0.18（18%×350=63 nit），HDR bgNit 设为 63 nit 即可匹配。SDR 显示器必须以最大亮度运行（commit `7268417`, `2b664bc`）

---

### 5. Windows SDR 亮度滑块非线性映射

**现象**：Windows HDR 设置中的 "SDR 内容亮度" 滑块值 42 并不等于 42%×1200nit=504nit。实际 paper white ≈ 261nit（通过视觉匹配 PQ(47nit) ≈ SDR 0.18 灰测出）

**原因**：Windows SDR 亮度滑块使用感知线性映射（非线性物理映射），用户无法得知精确的 paper white nit 值

**解决**：放弃主观校准方案（Paper White 滑动条），改为在 SDR 显示器最大亮度下进行测试，硬编码 paperWhite=350nit（commit `80260ad`, `7268417`）

---

### 6. 窗口过大，SDR/HDR 窗口大小不一致

**现象**：原窗口 1310×1498 过大；不同分辨率显示器上窗口和 UI 物理尺寸不一致

**解决**：窗口改为全屏模式（使用主显示器原生分辨率），UI 渲染基于物理尺寸（参考 DPI=96），两台显示器上 UI 物理大小一致（commit `806690b`, `b714132`, `7ada6d6`）

---

### 7. SDR 窗口不应在 HDR 显示器上运行

**现象**：HDR 显示器（ProArt PA32UCX-P）设为 HDR_PQ_Rec2020 模式时，SDR 内容饱和度偏低、灰色偏绿

**原因**：Windows 将 SDR sRGB 内容映射到 Rec2020 色域，导致色彩失真

**解决**：SDR.exe 应运行在 SDR 显示器（ThinkVision T27q-20）上，HDR.exe 运行在 HDR 显示器上（部署指导，非代码修改）

---

### 8. ImGui 滑动条精度不足

**现象**：SliderFloat 无法以 1 nit 为最小单位调节

**解决**：nit 相关控件改为 DragInt（1 nit/像素拖拽速度，Ctrl+点击可输入精确值），面板扩大到 800×400（commit `5cdd578`, `6c9d6a0`）

---

### 9. ImGui 控件标签文字被遮挡

**现象**：`PushItemWidth(-1)` 使控件占满窗口宽度，标签文字被挤出可见区域

**解决**：改为 `PushItemWidth(300)` 固定控件宽度，面板扩大到 800×400（commit `6c9d6a0`）

---

### 10. PQ 编码正确性验证

**现象**：初步怀疑 PQ(90nit) 显示亮度约为预期的 2 倍

**原因**：非 PQ 编码问题，而是对 SDR paper white 的错误假设（假设为 504nit，实际约 261nit）

**验证**：PQ(1000nit) 在 ProArt 上接近峰值亮度，PQ 编码本身正确

---

## 硬件问题

### 1. 两台显示器灰色色差（ProArt 偏绿）

**现象**：ProArt PA32UCX-P 在 HDR/Rec2020 模式下 90 nit 灰色偏绿，ThinkVision T27q-20 灰色正常

**原因**：两台显示器白点和色域校准不一致。ProArt 在 Rec2020 模式下使用不同色域，灰色在 90 nit 处有色彩偏移

**解决**：需对 ProArt 进行硬件校准（Asus Lightroom/Calman，校准到 D65 白点 + sRGB 色域）。当前未校准，测试中只关注亮度感知差异，不关注色彩差异

---

### 2. ThinkVision T27q-20 OSD 亮度控制灰化不可选

**现象**：显示器 OSD 中亮度选项灰色不可选中

**原因**：DDC/CI 启用后 OSD 亮度被锁定，或处于 sRGB 等预设色温模式

**解决**：在 OSD 中关闭 DDC/CI 或切换到 Custom/Standard 色温模式后解锁亮度控制，设为 100%（最大亮度 350 nit）。也可保持 DDC/CI 开启用 `ddcutil setvcp 10 100` 设置

---

### 3. ThinkVision T27q-20 无法显示 500 nit 白色

**现象**：显示器最大亮度 350 nit，无法显示 paperWhite=500nit 的白色

**解决**：所有测试改为在 350 nit paper white 下进行（PAPER_WHITE_NIT=350）。SDR 背景亮度 = 63 nit，HDR BG Nit 设为 63 nit 匹配

---

### 4. 同时对比效应影响 HDR 灰色主观亮度

**现象**：HDR 窗口非全屏时，周围 SDR 内容亮度变化会影响 HDR 窗口内灰色的主观感知亮度（SDR 调亮 → HDR 灰色视觉上变暗）

**原因**：同时对比效应（simultaneous contrast），人眼感知亮度受周围环境亮度影响

**解决**：SDR 和 HDR 窗口均全屏显示，消除周围内容干扰（commit `b714132`）
