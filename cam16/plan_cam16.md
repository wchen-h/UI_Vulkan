# CAM16 感知模型 UI 亮度提亮与色彩一致性研究计划

## 总体目标

在 UI_Vulkan 工程的 HDR 管线中,用 CAM16 色彩感知模型实现:提高感知亮度 Q(乘以 k)的同时保持色相 h 和饱和度 s 不变,从目标 (Q', h', s') 反解出新的 PQ 域 RGB 值,替代当前的 Y-Scale/CbCr-Scale 方案。

---

## 步骤 0:分支管理

- 从 `chromaScale_YUV` 新建 `chromaScale_cam16` 分支
- 保留 hdr_ui.frag 的管线骨架(BT.2020 混合→PQ),替换 YCbCr 调节段为 CAM16 段

---

## 步骤 1:CAM16 模型调研

### 1.1 获取论文公式定义

**输入**:论文 "Comprehensive color solutions: CAM16, CAT16 and CAM16-UCS" (Li et al., 2017)
**输出**:`cam16/cam16_formulas.md`,包含以下公式的逐字抄录:

1. CAT16 色度适应矩阵(3×3)
2. 正向变换(XYZ → 感知属性):全部中间变量和输出公式(J, C, h, Q, M, s)
3. 逆向变换(感知属性 → XYZ):完整反推步骤
4. Q 与 J 的关系式、s 与 (C, Q) 的关系式

**执行方式**:
- webfetch 获取论文(尝试 DOI、journal 页面)
- 如获取不到,请用户手动下载 PDF
- 逐公式抄录,标注论文出处
- 用 colour-science/colour Python 源码交叉验证

### 1.2 获取 colour-science 实现参考

**输入**:github.com/colour-science/colour 的 CAM16 实现
**输出**:在 `cam16/cam16_formulas.md` 中补充源码文件路径和函数名

---

## 步骤 2:方案验证与数学推导

### 2.1 思路合理性检验

**输入**:`cam16/cam16_formulas.md`
**输出**:`cam16/cam16_derivation.md`,第一节"思路检验"

检验内容:
1. 正向映射确定性:RGB + 观看环境 → (Q, h, s) 是否唯一
2. 逆向可解性:(Q', h', s') → RGB 是否可解(Q'→J', s'→C' 桥接,再用标准逆向)
3. 解的物理可行性:超出色域如何处理

### 2.2 数学推导

**输入**:`cam16/cam16_formulas.md` + 思路检验结论
**输出**:`cam16/cam16_derivation.md` 后续章节

推导内容:
1. 正向公式链:RGB_PQ → PQ_decode → nit → XYZ → (Q, h, s) via CAM16
2. 逆向公式链:(Q', h', s') → (J', C', h') → XYZ' → nit' → PQ_encode → RGB_PQ'
3. 观看环境参数确定(L_A, Y_b, surround, XYZ_w)
4. 色域外处理策略

---

## 步骤 3:代码实现方案设计

**输入**:`cam16/cam16_derivation.md`、现有 UI_Vulkan 代码
**输出**:`cam16/cam16_implementation_design.md`

设计内容:
1. 实现位置选择(Shader / CPU+LUT / CPU 逐像素)
2. 管线改动点(基于 hdr_ui.frag)
3. CAM16 实现模块(cam16.h / cam16.cpp)
4. ImGui 交互(Q-Scale 滑条)
5. 复用现有代码清单

---

## 步骤 4:代码实现与文档

### 4.1 实现

**输入**:`cam16/cam16_implementation_design.md`
**输出**:可编译运行的代码

实现顺序:
1. cam16.h / cam16.cpp:CAM16 正向 + 逆向
2. 交叉验证:用 colour-science Python 对比 C++ 结果
3. 集成到 hdr_app.cpp
4. ImGui:Q-Scale 滑条
5. 编译验证

### 4.2 输出文档

**输入**:实现完成的代码
**输出**:`cam16/cam16_algorithm_spec.md`

内容:
1. 算法说明书(输入输出、公式链、参数、色域处理、性能)
2. Code Review 提示(关键验证点、已知局限、测试用例)

---

## 执行顺序

```
步骤 0 (分支) → 步骤 1 (调研) → 步骤 2 (推导) → 步骤 3 (设计) → 步骤 4 (实现)
```

## 诚实约束

1. 步骤 1:公式从论文/源码逐字抄录,不凭记忆
2. 步骤 2:推导只基于步骤 1 的公式,无法推导的标注"需要进一步验证"
3. 步骤 3-4:实现只基于步骤 2 的推导
4. CAM16 的 C++ 实现用 colour-science Python 库做数值对比
