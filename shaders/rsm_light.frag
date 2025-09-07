#version 450

// G-Buffer Pass for Reflective Shadow Map (RSM)
// 目的：从光源视角渲染场景，生成三个G-Buffer纹理：
//      1. 世界空间位置 (World Position)
//      2. 世界空间法线 (World Normal)
//      3. 辐射通量 (Radiant Flux)，用于间接光照

// --- 输入变量 ---
layout(location = 1) in vec2 fragTexCoord; // 渲染全屏四边形的纹理坐标，范围[0, 1]

// --- 输出变量 (Multiple Render Targets - MRT) ---
// 这些变量将分别写入到不同的纹理附件中
layout(location = 0) out vec4 outPosition; // 输出 G-Buffer 0: 世界空间位置 (xyz) + 有效标记 (w)
layout(location = 1) out vec4 outNormal;   // 输出 G-Buffer 1: 世界空间法线 (xyz)
layout(location = 2) out vec4 outFlux;     // 输出 G-Buffer 2: 辐射通量 (rgb)，即从该点反射的光能

// --- Uniforms ---
// 从CPU端传入的统一变量，包含了场景、动画和光源的所有信息
layout(binding = 0) uniform SDFCornellUniforms {
    float iTime;              // 当前时间，用于动画
    vec2  iResolution;        // 渲染目标的分辨率
    vec2  iMouse;             // 鼠标位置
    int   iFrame;             // 当前帧数
    vec4  sphereRotation;     // 球体的旋转（欧拉角）
    vec4  sphereColor;        // 球体的颜色
    ivec4 enableLights;       // (未使用)
    vec4  lightDir;           // 光源方向 (xyz) 和强度 (w)
    vec4  lightColors[3];     // 光源颜色 (rgb) 和强度 (a)
    vec4  ambientColor;       // 环境光颜色
    vec4  shadowParams;       // 阴影参数
    vec4  lightRight;         // 光源相机的 "right" 向量
    vec4  lightUp;            // 光源相机的 "up" 向量
    vec4  lightOrigin;        // 光源相机的原点
    vec4  lightOrthoHalfSize; // 光源正交投影视锥体的一半大小 (width/2, height/2)
    vec4  rsmResolution;      // RSM 纹理的分辨率
    vec4  rsmParams;          // RSM 相关参数
    vec4  debugParams;        // 调试参数 (x=showRSMOnly)
} u;

// --- 常量 ---
const float MAX_DIST = 100.0;    // 光线步进的最大距离
const int   MAX_STEPS = 128;     // 光线步进的最大步数
const float SURF_DIST = 0.006;   // 判断是否击中物体表面的距离阈值

// --- 辅助函数 ---
// X轴旋转矩阵
mat3 rotateX(float a) {
    float s = sin(a), c = cos(a);
    return mat3(1, 0, 0, 0, c, -s, 0, s, c);
}
// Y轴旋转矩阵
mat3 rotateY(float a) {
    float s = sin(a), c = cos(a);
    return mat3(c, 0, s, 0, 1, 0, -s, 0, c);
}
// Z轴旋转矩阵
mat3 rotateZ(float a) {
    float s = sin(a), c = cos(a);
    return mat3(c, -s, 0, s, c, 0, 0, 0, 1);
}

// 球体的SDF（Signed Distance Function）
// 返回点p到半径为r的球体表面的最短距离
float sphereSDF(vec3 p, float r) { return length(p) - r; }

// 整个场景的SDF
// 通过组合不同形状的SDF来构建复杂场景
float sceneSDF(vec3 p) {
    // 基于时间创建Z轴旋转动画
    float rotationAngle = u.iTime * 0.5;
    mat3 zRotation = rotateZ(rotationAngle);

    // 第一个球体
    vec3 sphere1Pos = zRotation * vec3(2.0, 0.0, 0.0);
    vec3 sphere1P = p - sphere1Pos;
    mat3 rotation1 = rotateX(u.sphereRotation.x) * rotateY(u.sphereRotation.y) * rotateZ(u.sphereRotation.z);
    sphere1P = rotation1 * sphere1P; // 应用球体自身的旋转
    float sphere1 = sphereSDF(sphere1P, 1.0);

    // 第二个球体
    vec3 sphere2Pos = zRotation * vec3(-2.0, 0.0, 0.0);
    vec3 sphere2P = p - sphere2Pos;
    mat3 rotation2 = rotateX(u.sphereRotation.x) * rotateY(u.sphereRotation.y) * rotateZ(u.sphereRotation.z);
    sphere2P = rotation2 * sphere2P; // 应用球体自身的旋转
    float sphere2 = sphereSDF(sphere2P, 1.0);

    // 使用min操作合并两个球体
    float spheres = min(sphere1, sphere2);

    // 构建一个盒子作为房间（康奈尔盒）
    float ground  = p.y + 4.5;    // 地面
    float leftWall = p.x + 5.0;   // 左墙
    float rightWall = -p.x + 5.0; // 右墙
    float backWall = p.z + 2.0;   // 后墙
    float ceiling  = -p.y + 4.5;  // 天花板
    float walls = min(min(leftWall, rightWall), min(backWall, ceiling));
    walls = min(walls, ground);

    // 最终将球体和墙壁合并，返回到场景表面的最短距离
    return min(spheres, walls);
}

// 计算SDF在点p处的法线
// 原理：通过采样p点周围极小范围内的SDF值来估算SDF场的梯度，梯度方向即为法线方向
vec3 getNormal(vec3 p) {
    const float h = 0.001;
    const vec2 k = vec2(1, -1);
    return normalize(k.xyy * sceneSDF(p + k.xyy * h) +
                     k.yyx * sceneSDF(p + k.yyx * h) +
                     k.yxy * sceneSDF(p + k.yxy * h) +
                     k.xxx * sceneSDF(p + k.xxx * h));
}

// 光线步进函数
// 从ro点沿着rd方向前进，返回与场景的交点距离
float rayMarch(vec3 ro, vec3 rd) {
    float dO = 0.0; // 从ro开始的总行进距离
    for (int i = 0; i < MAX_STEPS; i++) {
        vec3 p = ro + rd * dO; // 当前射线上的点
        float dS = sceneSDF(p); // 当前点到场景表面的最短距离
        // 如果距离足够近，或者超过了最大距离，则停止步进
        if (abs(dS) < SURF_DIST || dO > MAX_DIST) break;
        // 安全步进策略：每次前进的距离是dS，确保不会穿过物体
        // 使用max(dS, 0.003)是为了避免在某些情况下步进过小导致性能问题
        dO += max(dS, 0.003);
    }
    return dO;
}

// 根据世界坐标p获取该点的材质反照率（Albedo）
// 这段逻辑必须和sceneSDF保持一致，以正确判断p点属于哪个物体
vec3 getMaterialAlbedo(vec3 p) {
    // 重新计算SDF值来确定哪个表面最近
    float rotationAngle = u.iTime * 0.5;
    mat3 zRotation = rotateZ(rotationAngle);
    
    vec3 sphere1Pos = zRotation * vec3(2.0, 0.0, 0.0);
    vec3 sphere1P = p - sphere1Pos;
    mat3 rotation1 = rotateX(u.sphereRotation.x) * rotateY(u.sphereRotation.y) * rotateZ(u.sphereRotation.z);
    sphere1P = rotation1 * sphere1P;
    float sphere1 = sphereSDF(sphere1P, 1.0);
    
    vec3 sphere2Pos = zRotation * vec3(-2.0, 0.0, 0.0);
    vec3 sphere2P = p - sphere2Pos;
    mat3 rotation2 = rotateX(u.sphereRotation.x) * rotateY(u.sphereRotation.y) * rotateZ(u.sphereRotation.z);
    sphere2P = rotation2 * sphere2P;
    float sphere2 = sphereSDF(sphere2P, 1.0);
    
    float spheres = min(sphere1, sphere2);
    
    float ground = p.y + 4.5;
    float leftWall = p.x + 5.0;
    float rightWall = -p.x + 5.0;
    float backWall = p.z + 2.0;
    float ceiling = -p.y + 4.5;
    
    float walls = min(min(leftWall, rightWall), min(backWall, ceiling));
    walls = min(walls, ground);
    
    // 如果球体更近，返回球体的颜色
    if (spheres < walls) {
        return u.sphereColor.rgb;
    } 
    
    // 否则，判断是哪面墙并返回对应的颜色
    float minDist = min(min(min(min(ground, leftWall), rightWall), backWall), ceiling);
    
    if (ground == minDist)    return vec3(0.3, 0.4, 0.6); // 地面 - 深蓝灰色
    if (ceiling == minDist)   return vec3(0.7, 0.8, 0.9); // 天花板 - 浅蓝灰色
    if (leftWall == minDist)  return vec3(0.4, 0.7, 0.5); // 左墙 - 暖绿色
    if (rightWall == minDist) return vec3(0.7, 0.4, 0.4); // 右墙 - 暖红色
    if (backWall == minDist)  return vec3(0.6, 0.4, 0.7); // 后墙 - 暖紫色
    
    return vec3(0.8); // 默认颜色
}

void main() {
  // --- 1. 构建光源相机的正交投影射线 ---
  // 将纹理坐标从 [0, 1] 映射到 [-1, 1] 的标准设备坐标 (NDC)
  vec2 uv = fragTexCoord * 2.0 - 1.0;
  // 计算射线原点 (ro)。这是正交投影的关键：
  // 每个像素的射线原点都位于一个由 lightRight 和 lightUp 定义的平面上，
  // 从而产生一组平行的射线。
  vec3 ro = u.lightOrigin.xyz
          + u.lightRight.xyz * (uv.x * u.lightOrthoHalfSize.x)
          + u.lightUp.xyz    * (uv.y * u.lightOrthoHalfSize.y);
  // 射线方向 (rd) 对所有像素都是相同的，即光源方向。
  vec3 rd = normalize(u.lightDir.xyz);

  // --- 2. 执行光线步进，找到与场景的交点 ---
  float d = rayMarch(ro, rd);
  
  // 如果距离超过最大值，说明射线没有击中任何物体
  if (d >= MAX_DIST) {
    // 写入无效数据（全0），以便在后续处理中可以忽略这些像素
    outPosition = vec4(0.0);
    outNormal = vec4(0.0);
    outFlux = vec4(0.0);
    return; // 提前退出
  }

  // --- 3. 计算击中点的几何属性 ---
  // 根据射线原点、方向和行进距离计算精确的世界坐标
  vec3 pos = ro + rd * d;
  // 计算该点的法线
  vec3 nor = getNormal(pos);
  // 计算兰伯特光照因子。注意光源方向是-rd
  float nDotL = max(dot(nor, -rd), 0.0);
  
  // --- 4. 计算辐射通量 (Radiant Flux) ---
  // 这是RSM的核心步骤，计算从该点反射出去的光能
  
  // 获取该点的材质反照率（基础颜色）
  vec3 albedo = getMaterialAlbedo(pos);
  // 获取光源颜色并乘以强度
  vec3 lightColor = u.lightColors[0].rgb * u.lightColors[0].a;
  // 计算通量：Flux = Albedo * LightColor * LightIntensity * NdotL
  // u.lightDir.w 通常也用作一个全局光强因子
  // 最后的 * 2.0 是一个增益系数，用于增强间接光照的效果，可以按需调整
  vec3 flux = albedo * lightColor * u.lightDir.w * nDotL * 2.0; 

  // --- 5. 将计算结果写入 G-Buffer ---
  // 将世界坐标写入第一个渲染目标
  outPosition = vec4(pos, 1.0); // w=1.0 表示这是一个有效的击中点
  // 将法线写入第二个渲染目标
  outNormal = vec4(nor, 0.0);
  // 将辐射通量写入第三个渲染目标
  outFlux = vec4(flux, 1.0);
}