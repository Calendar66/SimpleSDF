
# SDF Mesh


## 一、动机与背景

实时渲染（例如 UE5 的 Lumen）把性能与真实感推到了新的高度。其中，SDF（有向距离场）因求交高效、表达统一，被广泛用于描述与追踪几何体。

但传统 SDF 多依赖解析式（球、盒等），遇到复杂形状就会受限。受 Lumen 启发，我们把任意网格（Mesh）预烘焙成 SDF，在光线步进（Ray Marching）中像采样贴图一样使用它。

这正是本文的 **SDF Mesh**：先把模型的距离场写入三维纹理（3D/Volume Texture），渲染时直接采样，得到任意点到表面的距离。接下来结合工程代码，讲清数据组织、采样映射、缩放与性能要点。

## 二、理解SDF数据结构

在我们深入代码之前，首先需要理解SDF Mesh的数据是如何组织的。这通常是一个自定义的二进制或文本文件，包含了重建SDF场景所需的所有元数据。以我们项目中使用的 `.sdf` 文件为例，其结构非常直观：

**文件示例：** `assets/car.sdf`

```
27 17 15
-0.13 -0.08 -0.04
0.01
...
-0.015
-0.005
0.005
0.015
...
```

各行含义如下：

1.  **第1行：体素分辨率 (Resolution)**
    `27 17 15` 表示这个SDF数据场在X, Y, Z三个轴向上被划分成了 $27 \\times 17 \\times 15$ 个网格单元（Voxel）。分辨率越高，细节越丰富，但文件也越大。

2.  **第2行：空间原点 (Origin)**
    `-0.13 -0.08 -0.04` 定义了这个三维网格在模型局部空间中的起始坐标。它像是整个数据场的“锚点”。

3.  **第3行：体素尺寸 (Voxel Size / Precision)**
    `0.01` 定义了每个小格子的边长。这个值至关重要，它将无单位的格子索引与真实的空间尺度关联起来。

4.  **第4行及以后：距离场数据 (Distance Field Data)**
    从这里开始，文件逐行列出每个网格顶点的SDF值。这些浮点数遵循SDF的经典定义：

      * **负数**：该点位于物体**内部**。
      * **正数**：该点位于物体**外部**。
      * **零**：该点恰好位于物体**表面**。

在加载阶段，我们会解析这个文件，将元数据（分辨率、原点、尺寸）保存起来，并将所有的距离数据上传到GPU，形成一个3D纹理，也就是我们Shader中将要采样的 `sampler3D` 对象。

补充说明：分辨率与 `cellSize` 决定了体素网格的物理尺度；`origin` 表示该网格在模型局部空间中的起点坐标。当前实现主要以包围盒 `meshHalfSize` 为基准做归一化映射；若需要引入 `origin`，可在着色器中先对 `local` 做平移，再进行 `local / halfSize` 的归一化。

## 三、从空间点到SDF采样

接下来一个核心问题是：在 Shader 中，给定任意一个世界空间点 $p$，如何从三维纹理中查询对应的 SDF 值？

这个核心任务由 `sdfMeshShape` 函数完成。它的职责明确：接收一个世界空间点 $p$ ，返回该点到SDF网格物体表面的最短有向距离。

### 3.1 核心函数 `sdfMeshShape`

```glsl
// 采样SDF网格并将其约束在包围盒内
float sdfMeshShape(vec3 p) {
    // 步骤 1: 坐标系转换 (世界空间 -> 模型局部空间)
    mat3 rot = rotateX(u.sphereRotation.x) * rotateY(u.sphereRotation.y) * rotateZ(u.sphereRotation.z);
    vec3 local = transpose(rot) * p;

    // 步骤 2: 计算点到包围盒的距离
    vec3 halfSize = u.meshHalfSize.xyz;
    float boxSdf = sdBox(local, halfSize);

    // 步骤 3: 将局部空间坐标映射为3D纹理坐标 (UVW)
    vec3 uvw = clamp(local / max(halfSize, vec3(1e-5)) * 0.5 + 0.5, 0.0, 1.0);
    
    // 步骤 4: 从3D纹理中采样SDF值并与包围盒组合
    float scaleMin = u.sdfParams0.x;
    float hasTex = u.sdfParams0.y;
    float dMesh = boxSdf;
    if (hasTex > 0.5) {
        float dTex = texture(sdfTex, uvw).r; // 采样
        dMesh = max(dTex * scaleMin, boxSdf); // 组合
    }
    return dMesh;
}
```

### 3.2 算法步骤详解

#### 步骤 1: 坐标系转换 (世界空间 $\rightarrow$ 模型局部空间)

SDF纹理是在模型的**局部空间（Model Space）**下生成的，它不关心模型在世界中的位置和旋转。因此，采样的第一步，就是将世界空间的查询点 $p$ 转换回模型的局部空间。

```glsl
mat3 rot = rotateX(u.sphereRotation.x) * rotateY(u.sphereRotation.y) * rotateZ(u.sphereRotation.z);
vec3 local = transpose(rot) * p;
```

这里直接使用旋转矩阵的转置。对正交矩阵（包含旋转）而言，逆矩阵等于转置矩阵。这样就能快速抵消世界空间下的旋转，得到局部空间坐标 `local`。

#### 步骤 2: 计算包围盒SDF

3D纹理本身是无边界的，我们需要一个“容器”来约束它。一个简单的轴对齐包围盒（AABB）是理想的选择。

```glsl
vec3 halfSize = u.meshHalfSize.xyz;
float boxSdf = sdBox(local, halfSize);
```

`boxSdf` 计算了局部点 `local` 到这个包围盒的距离。它有两个作用：

1.  **基础形状**：如果SDF纹理未启用，物体就表现为一个简单的盒子。
2.  **裁剪边界**：确保最终的SDF形状不会超出这个盒子的范围。

#### 步骤 3: 局部空间坐标到纹理坐标(UVW)的映射

这是整个流程的枢纽。我们需要将局部空间坐标 `local` 转换成用于纹理采样的、范围在 $[0, 1]$ 内的UVW坐标。

```glsl
vec3 uvw = clamp(local / max(halfSize, vec3(1e-5)) * 0.5 + 0.5, 0.0, 1.0);
```

这行代码做了三件事：

1.  **归一化**: `local / halfSize`

      - 将局部坐标从 $[-halfSize, +halfSize]$ 范围映射到 $[-1, 1]$ 范围。
      - 数学上：$P_{norm} = (\frac{local.x}{halfSize.x}, \frac{local.y}{halfSize.y}, \frac{local.z}{halfSize.z})$

2.  **重映射**: `... * 0.5 + 0.5`

      - 将 $[-1, 1]$ 范围线性映射到 $[0, 1]$ 的标准纹理坐标范围。
      - 数学上：$P_{uvw} = P_{norm} \times 0.5 + 0.5$

3.  **钳制**: `clamp(..., 0.0, 1.0)`

      - 一个安全措施，确保任何因浮点误差或处于盒子外部的点所计算出的坐标都能被强制拉回有效的 $[0, 1]$ 范围，避免采样越界。

#### 步骤 4: 采样、缩放与组合

最后一步，我们使用计算出的 `uvw` 坐标进行采样，并与包围盒进行组合。

```glsl
float dTex = texture(sdfTex, uvw).r;
dMesh = max(dTex * scaleMin, boxSdf);
```

这里有两个关键点：

  - **距离缩放 (`dTex * scaleMin`)**: 3D纹理中存储的SDF值 `dTex` 通常是归一化的。`scaleMin` 参数作为一个从CPU传入的缩放因子，负责将这个无单位的相对距离值，转换为与场景尺度匹配的真实世界距离。这使得同一个SDF资产可以在场景中以不同的大小复用。我们将在后文详细探讨这一点。

  - **几何组合 (`max(A, B)`)**: 在SDF的布尔运算中，`max(sdfA, sdfB)` 代表两个形状的**交集 (Intersection)**。这里的 `max(scaledDistance, boxSdf)` 意味着最终的形状是“SDF纹理定义的形状”与“包围盒定义的形状”的交集。这巧妙地将复杂的SDF几何体**裁剪**并约束在了包围盒的内部。

## 四、性能优化与思考

在步进循环中，`sdfMeshShape` 会被反复调用，代价敏感。一个低成本的改进是：**只有当查询点落在包围盒内部时，才进行 3D 纹理采样。**

### 4.1 冗余计算分析

若点在盒子外（`boxSdf > 0`），最短距离就是到盒面的距离。此时再去计算 `uvw` 并采样 3D 纹理基本无助于结果，只会浪费带宽与算力。

### 4.2 引入分支进行优化

我们可以通过一个简单的条件分支来避免这种冗余：

**优化后的 `sdfMeshShape` 函数:**

```glsl
float sdfMeshShape_Optimized(vec3 p) {
    // ... (步骤1: 坐标转换)
    vec3 local = ...;
    
    // ... (步骤2: 计算包围盒SDF)
    vec3 halfSize = u.meshHalfSize.xyz;
    float boxSdf = sdBox(local, halfSize);

    // 【核心优化】
    // 如果点在包围盒外部，提前返回，避免不必要的计算
    if (boxSdf > 0.0) {
        return boxSdf;
    }

    // --- 仅当点在包围盒内部或表面时，才执行以下代码 ---
    if (u.sdfParams0.y > 0.5) { // hasTex
        vec3 uvw = clamp(local / max(halfSize, vec3(1e-5)) * 0.5 + 0.5, 0.0, 1.0);
        float dTex = texture(sdfTex, uvw).r;
        return max(dTex * u.sdfParams0.x, boxSdf); // u.sdfParams0.x is scaleMin
    }

    return boxSdf;
}
```

### 4.3 分支的利弊权衡 (The Trade-off)

在GPU编程中，引入分支需要警惕**线程束发散 (Warp Divergence)**。

  - **优点**: 当SDF物体在屏幕上占比较小时，绝大多数像素/光线都会在 `if (boxSdf > 0.0)` 处被提前剔除，极大地节省了纹理带宽和计算开销，性能提升显著。
  - **缺点**: 当大量像素/光线恰好落在包围盒的边界时，一个线程束（Warp）内的线程可能会进入不同的分支路径，导致硬件需要串行执行两个分支，反而会降低性能。

不过，在光线步进这类应用中，光线从远处逼近物体，绝大多数步进点都处于物体之外。因此，这种**提前退出（Early Exit）**的优化策略通常是利大于弊的。

## 五、自由三维缩放：通过 meshHalfSize 控制

在本项目中，SDF 网格的“大小”和“非均匀缩放”由一个简单直观的包围盒参数 `u.meshHalfSize.xyz` 控制。它表示模型局部空间中包围盒在 X、Y、Z 方向上的“半长度”（Half Size）。

回看前文 Shader 的核心采样映射：

```glsl
vec3 halfSize = u.meshHalfSize.xyz;
vec3 uvw = clamp(local / max(halfSize, vec3(1e-5)) * 0.5 + 0.5, 0.0, 1.0);
```

这意味着：

- **设定半尺寸**：若希望最终渲染物体的世界尺寸为 \(L \times H \times W\)，则将 `u.meshHalfSize` 设为 \((L/2, H/2, W/2)\)。
- **非均匀缩放天然支持**：`halfSize.x/y/z` 可以分别不同，从而在三个轴向上独立缩放。
- **数值稳定性**：使用 `max(halfSize, 1e-5)` 与 UI 侧的最小值钳制，避免除零与极小尺寸导致的不稳定。

### 5.1 运行时如何驱动 meshHalfSize（UI 与 UBO）

界面侧通过 ImGui 提供了一个三维尺寸滑条（长度/高度/宽度），直接对应到 `boxSizeLWH`：

```cpp
ImGui::Text("Box Size (L/W/H)");
// Length->X, Width->Z, Height->Y
ImGui::SliderFloat3("L/W/H", boxSizeLWH, 0.2f, 6.0f, "%.2f");
if (ImGui::Button("Reset Size")) { boxSizeLWH[0]=2.4f; boxSizeLWH[1]=2.4f; boxSizeLWH[2]=2.4f; }
```

随后在更新 UBO 的阶段将其转换为 `meshHalfSize`（注意最小值钳制与轴向映射）：

```cpp
// Use the user-controlled box as the bounding box for the SDF mesh
float halfX = std::max(0.05f, boxSizeLWH[0] * 0.5f); // Length -> X
float halfY = std::max(0.05f, boxSizeLWH[1] * 0.5f); // Height -> Y
float halfZ = std::max(0.05f, boxSizeLWH[2] * 0.5f); // Width  -> Z
u.meshHalfSize[0] = halfX;
u.meshHalfSize[1] = halfY;
u.meshHalfSize[2] = halfZ;
u.meshHalfSize[3] = 0.0f;
```

由此，Shader 中 `local / halfSize` 的归一化会把 SDF 网格“挤压/拉伸”到这个包围盒内，从而实现可视上对物体的自由三维缩放。

### 5.2 与距离缩放（scaleMin）的关系

当进行非均匀缩放时，为了保证光线步进的稳定性与保守性，CPU 侧会根据“原始 SDF 体素物理尺寸”和“目标包围盒尺寸”的比值，计算一个最小轴向缩放 `scaleMin` 并传入 `u.sdfParams0.x`：

```cpp
float origLenX = sdfData.width  * sdfData.cellSize;
float origLenY = sdfData.height * sdfData.cellSize;
float origLenZ = sdfData.depth  * sdfData.cellSize;
float boxLenX  = 2.0f * halfX;
float boxLenY  = 2.0f * halfY;
float boxLenZ  = 2.0f * halfZ;
float sx = (origLenX > 1e-6f) ? (boxLenX / origLenX) : 1.0f;
float sy = (origLenY > 1e-6f) ? (boxLenY / origLenY) : 1.0f;
float sz = (origLenZ > 1e-6f) ? (boxLenZ / origLenZ) : 1.0f;
float scaleMin = std::min(sx, std::min(sy, sz));

u.sdfParams0[0] = scaleMin; // 距离缩放（用于 Shader 中 dTex * scaleMin）
u.sdfParams0[1] = (sdfMeshTextureView != VK_NULL_HANDLE) ? 1.0f : 0.0f; // hasTex
```

- **直观理解**：`meshHalfSize` 决定“几何体外形/体积”的缩放；`scaleMin` 决定“光线步进距离”的保守缩放。非均匀缩放下我们选取最小轴向比例，避免步长过大导致穿透。
- **Shader 侧组合**：

```glsl
float dTex = texture(sdfTex, uvw).r;
float dMesh = max(dTex * u.sdfParams0.x /*scaleMin*/, boxSdf);
```

这保证了即便进行了强烈的非均匀拉伸/压缩，步进仍然稳定。

### 5.3 快速上手与实践建议

- **想要放大/缩小整体**：三个滑条同步增减即可；想要某一方向拉伸，单独调整该轴对应的数值。
- **数值边界**：保持每个半轴不小于 ~0.05，可避免极端情况下的数值不稳定；Shader 已额外使用 `1e-5` 防卫。
- **坐标系约定**：文中 `Length->X`，`Height->Y`，`Width->Z`。保持 UI 与期望轴向一致，避免视觉/交互混淆。

---

## 六、距离缩放
在采样得到SDF Mesh中距离值后，还需要进行缩放处理，即： `dTex * scaleMin` ：

1.  **归一化存储的本质**: SDF纹理资产在制作时，其内部的距离值被归一化到了一个标准范围（如 $[0,1]$）。这使得资产本身与具体尺寸解耦，便于管理和复用。

2.  **匹配场景尺度**: 我们的渲染场景工作在具体的“世界单位”下。光线步进的步长、碰撞检测的阈值，都依赖于SDF函数返回的真实距离。`scaleMin` 将纹理中无单位的相对距离，转换为场景中有意义的绝对距离。

## 七、结语

通过将复杂的几何体预计算为SDF 3D纹理，我们成功地打破了传统解析式SDF的局限，为光线步进的世界打开了渲染任意模型的大门。从坐标变换、边界定义，到核心的纹理坐标映射与采样，每一步都体现了计算机图形学中空间与数据巧妙结合的智慧。

更重要的是，通过对算法的深入分析，我们发现了利用包围盒进行提前剔除的优化空间，并在实践中权衡了GPU分支带来的利弊。这提醒我们，在追求更高真实感的同时，对性能的极致探索同样是图形程序员永恒的课题。希望本文能为你在这条探索之路上提供一些有价值的参考。