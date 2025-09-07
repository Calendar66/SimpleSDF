#version 450

// 输入：从顶点着色器传入的片段颜色和纹理坐标
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

// 输出：最终计算出的像素颜色
layout(location = 0) out vec4 outColor;

// Uniforms：从CPU传入的全局变量，用于控制渲染效果
layout(binding = 0) uniform SDFCornellUniforms {
    float iTime;          // 当前时间，用于动画
    vec2 iResolution;     // 屏幕分辨率
    vec2 iMouse;          // 鼠标位置
    int iFrame;           // 当前帧数
    
    // 球体旋转控制
    vec4 sphereRotation;  // xyz = 旋转角度, w = 动画时间
    
    // 球体颜色
    vec4 sphereColor;     // RGB 球体颜色
    
    // 光照控制
    ivec4 enableLights;   // 1表示启用, 0表示禁用 (x=主光源, y=填充光, z=边缘光, w=环境反射)
    vec4 lightDir;        // xyz = 方向, w = 强度
    vec4 lightColors[3];  // 3个光源的颜色
    vec4 ambientColor;    // RGBA 环境光
    
    // 阴影和材质设置
    vec4 shadowParams;    // x=阴影质量, y=阴影强度, z=蓝色色调, w=金属度
    
    // RSM/light camera additions
    vec4 lightRight;        // xyz basis
    vec4 lightUp;           // xyz basis
    vec4 lightOrigin;       // origin of light camera
    vec4 lightOrthoHalfSize;// xy half size
    vec4 rsmResolution;     // xy
    vec4 rsmParams;         // x radius, y samples, z enableIndirectLighting (>0.5), w enableRSM (>0.5)
    vec4 indirectParams;    // x indirect intensity, y/z/w reserved
    vec4 debugParams;       // x showRSMOnly (>0.5)
    
    // PBR parameters
    vec4 pbrParams;         // x=enablePBR(>0.5), y=globalRoughness, z=globalMetallic, w=reserved
    vec2 roughnessValues;   // per-material roughness: [0]=sphere1, [1]=sphere2  
    vec2 metallicValues;    // per-material metallic: [0]=sphere1, [1]=sphere2
    vec4 baseColorFactors;  // global color tinting factors: RGB + intensity
} u;

// RSM textures
layout(binding = 1) uniform sampler2D rsmPositionTex;
layout(binding = 2) uniform sampler2D rsmNormalTex;
layout(binding = 3) uniform sampler2D rsmFluxTex;

// Flower texture
layout(binding = 4) uniform sampler2D flowerTex;

// --- 常量定义 ---
const float PI = 3.14159265359;
const float MAX_DIST = 100.0;     // 光线行进的最大距离
const int MAX_STEPS = 128;        // 光线行进的最大步数
const float SURF_DIST = 0.006;    // 判断光线是否击中物体表面的最小距离阈值

// --- SDF (Signed Distance Function - 有向距离场) 函数 ---
// SDF的核心思想是：对于空间中的任意一点，函数返回该点到场景中最近物体表面的距离。
// 如果点在物体外部，距离为正；如果在内部，距离为负。

// 球体的SDF
float sphereSDF(vec3 p, float r) {
    // 计算点p到原点的距离，再减去半径r。
    // 结果 > 0: 点在球外，值为到球面的距离
    // 结果 = 0: 点在球面上
    // 结果 < 0: 点在球内，值为到球面的距离的负数
    return length(p) - r;
}

// --- 辅助函数 ---

// 绕X轴旋转的矩阵
mat3 rotateX(float a) {
    float s = sin(a), c = cos(a);
    return mat3(1, 0, 0, 0, c, -s, 0, s, c);
}

// 绕Y轴旋转的矩阵
mat3 rotateY(float a) {
    float s = sin(a), c = cos(a);
    return mat3(c, 0, s, 0, 1, 0, -s, 0, c);
}

// 绕Z轴旋转的矩阵
mat3 rotateZ(float a) {
    float s = sin(a), c = cos(a);
    return mat3(c, -s, 0, s, c, 0, 0, 0, 1);
}


// --- 场景SDF ---
// 这个函数通过组合多个SDF来定义整个场景的几何形状。
float sceneSDF(vec3 p) {
    // 计算绕z轴的旋转，让两个球体围绕中心旋转
    float rotationAngle = u.iTime * 0.5; // 旋转速度
    mat3 zRotation = rotateZ(rotationAngle);
    
    // 第一个球体 - 位于右侧，围绕z轴旋转
    vec3 sphere1Pos = zRotation * vec3(2.0, 0.0, 0.0); // 距离中心2个单位
    vec3 sphere1P = p - sphere1Pos;
    // 对第一个球体应用局部旋转
    mat3 rotation1 = rotateX(u.sphereRotation.x) * rotateY(u.sphereRotation.y) * rotateZ(u.sphereRotation.z);
    sphere1P = rotation1 * sphere1P;
    float sphere1 = sphereSDF(sphere1P, 1.0);
    
    // 第二个球体 - 位于左侧，围绕z轴旋转
    vec3 sphere2Pos = zRotation * vec3(-2.0, 0.0, 0.0); // 距离中心2个单位，相反方向
    vec3 sphere2P = p - sphere2Pos;
    // 对第二个球体应用局部旋转
    mat3 rotation2 = rotateX(u.sphereRotation.x) * rotateY(u.sphereRotation.y) * rotateZ(u.sphereRotation.z);
    sphere2P = rotation2 * sphere2P;
    float sphere2 = sphereSDF(sphere2P, 1.0);
    
    // 合并两个球体
    float spheres = min(sphere1, sphere2);
    
    // 定义地面 (一个y=-4.5的平面)
    float ground = p.y + 4.5;
    
    // 定义墙壁和天花板，形成一个房间
    float leftWall = p.x + 5.0;
    float rightWall = -p.x + 5.0;
    float backWall = p.z + 2.0;
    float ceiling = -p.y + 4.5;
    
    // 使用min操作合并墙壁和地面。SDF的min操作相当于几何体的并集。
    float walls = min(min(leftWall, rightWall), min(backWall, ceiling));
    walls = min(walls, ground);
    
    // 再次使用min操作，将球体和房间合并。最终返回点p到整个场景最近表面的距离。
    return min(spheres, walls);
}

// 计算球面UV坐标
vec2 getSphereUV(vec3 p) {
    vec3 d = normalize(p);
    float u = 0.5 + atan(d.z, d.x) / (2.0 * PI);
    float v = 0.5 - asin(d.y) / PI;
    return vec2(u, v);
}

// 获取指定点的材质ID
int getMaterial(vec3 p) {
    // 计算绕z轴的旋转，让两个球体围绕中心旋转
    float rotationAngle = u.iTime * 0.5; // 旋转速度
    mat3 zRotation = rotateZ(rotationAngle);
    
    // 第一个球体 - 位于右侧，围绕z轴旋转
    vec3 sphere1Pos = zRotation * vec3(2.0, 0.0, 0.0);
    vec3 sphere1P = p - sphere1Pos;
    mat3 rotation1 = rotateX(u.sphereRotation.x) * rotateY(u.sphereRotation.y) * rotateZ(u.sphereRotation.z);
    sphere1P = rotation1 * sphere1P;
    float sphere1 = sphereSDF(sphere1P, 1.0);
    
    // 第二个球体 - 位于左侧，围绕z轴旋转
    vec3 sphere2Pos = zRotation * vec3(-2.0, 0.0, 0.0);
    vec3 sphere2P = p - sphere2Pos;
    mat3 rotation2 = rotateX(u.sphereRotation.x) * rotateY(u.sphereRotation.y) * rotateZ(u.sphereRotation.z);
    sphere2P = rotation2 * sphere2P;
    float sphere2 = sphereSDF(sphere2P, 1.0);
    
    float spheres = min(sphere1, sphere2);
    
    // 定义各个墙面
    float ground = p.y + 4.5;      // 地面 (底部)
    float leftWall = p.x + 5.0;    // 左墙
    float rightWall = -p.x + 5.0;  // 右墙
    float backWall = p.z + 2.0;    // 后墙
    float ceiling = -p.y + 4.5;    // 天花板 (顶部)
    
    // 检查是否是球体
    float walls = min(min(leftWall, rightWall), min(backWall, ceiling));
    walls = min(walls, ground);
    
    if (spheres < walls) {
        // Distinguish between the two spheres
        if (sphere1 <= sphere2) {
            return 1; // Sphere1 (right side) - textured with flower
        } else {
            return 7; // Sphere2 (left side) - solid color
        }
    }
    
    // 确定是哪面墙 - 找到距离最小的墙面
    float minDist = min(min(min(min(ground, leftWall), rightWall), backWall), ceiling);
    
    if (ground == minDist) {
        return 2; // 地面 (底部) - 深蓝色
    } else if (ceiling == minDist) {
        return 3; // 天花板 (顶部) - 浅蓝色  
    } else if (leftWall == minDist) {
        return 4; // 左墙 - 绿色
    } else if (rightWall == minDist) {
        return 5; // 右墙 - 红色
    } else if (backWall == minDist) {
        return 6; // 后墙 - 紫色
    }
    
    return 2; // 默认墙面材质
}


// --- 核心渲染算法 ---

// 计算表面法线 (Normal)
// 法线是垂直于物体表面的向量，对于光照计算至关重要。
// 这里的算法通过采样SDF在当前点p周围的几个点，来估算SDF的梯度，梯度方向即为法线方向。
vec3 getNormal(vec3 p) {
    const float h = 0.001; // 一个很小的偏移量
    const vec2 k = vec2(1, -1);
    return normalize(k.xyy * sceneSDF(p + k.xyy * h) +
                     k.yyx * sceneSDF(p + k.yyx * h) +
                     k.yxy * sceneSDF(p + k.yxy * h) +
                     k.xxx * sceneSDF(p + k.xxx * h));
}

// 光线步进 (Ray Marching)
// 这是SDF渲染的核心算法。它从相机位置(ro)沿着光线方向(rd)前进，直到击中物体或超出最大距离。
// 算法流程：
// 1. 在当前位置p，计算到场景的距离d = sceneSDF(p)。
// 2. 这个距离d保证了我们可以安全地沿着光线方向前进d的距离，而不会穿过任何物体。
// 3. 更新光线行进的总距离dO，并移动到新的位置。
// 4. 重复1-3步，直到距离d足够小（表示击中表面）或超出最大步数/距离。
float rayMarch(vec3 ro, vec3 rd) {
    float dO = 0.0; // 光线已经行进的总距离
    for (int i = 0; i < MAX_STEPS; i++) {
        vec3 p = ro + rd * dO; // 当前光线位置
        float dS = sceneSDF(p); // 计算到场景的距离
        if (abs(dS) < SURF_DIST || dO > MAX_DIST) break; // 如果足够近或太远，则停止
        dO += max(dS, 0.003); // 沿光线方向前进dS的距离，设置一个最小步长防止在平面上停滞
    }
    return dO;
}

// 计算软阴影
// 算法类似于光线步进，但是从物体表面点(ro)向光源方向(rd)步进。
// 在每一步，检查离场景的距离h。如果h很小，说明被遮挡了。
// 通过 `k * h / t` 计算遮挡程度，t是当前步进距离。离遮挡物越近(h小)，或者遮挡物离表面点越近(t小)，阴影就越暗。
// 最终将所有步的最小遮挡值作为结果，形成软阴影效果。
float softShadow(vec3 ro, vec3 rd, float mint, float maxt, float k) {
    float res = 1.0; // 结果，1.0代表完全亮，0.0代表完全黑
    float t = mint;
    for (int i = 0; i < 64; i++) {
        float h = sceneSDF(ro + rd * t);
        if (h < 0.0008) return 0.0; // 完全被遮挡
        res = min(res, k * h / t); // k控制阴影柔和度
        t += clamp(h, 0.002, 0.05); // 步进，clamp避免步长过大或过小
        if (res < 0.004 || t > maxt)
            break;
    }
    return clamp(res, 0.0, 1.0);
}

// RSM-based shadow test using position buffer as a depth substitute
float rsmShadow(vec3 p, vec3 n) {
    // Project point to light ortho plane
    vec3 rel = p - u.lightOrigin.xyz;
    vec2 base = vec2(dot(rel, u.lightRight.xyz) / max(u.lightOrthoHalfSize.x, 1e-4),
                     dot(rel, u.lightUp.xyz)    / max(u.lightOrthoHalfSize.y, 1e-4));
    vec2 uv = base * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
        return 1.0; // outside light frustum -> treat as lit
    }
    // Light forward = from light to scene
    // If RSM was rendered with rd = u.lightDir (toward light), depth axis points opposite
    vec3 Ld = normalize(u.lightDir.xyz);
    float tSurface = dot(rel, Ld);
    // Receiver-plane depth bias to reduce self-shadowing on curved surfaces
    float slope = 1.0 - max(dot(n, Ld), 0.0);
    // PCF-like 8 taps with configurable radius (in texels)
    vec2 texel = 1.0 / max(u.rsmResolution.xy, vec2(1.0));
    float radius = max(u.rsmParams.x, 0.5);
    vec2 offs[8] = vec2[8](
        vec2( 0.0,  0.0), vec2( 1.0,  0.0), vec2(-1.0,  0.0), vec2(0.0,  1.0),
        vec2( 0.7,  0.7), vec2(-0.7,  0.7), vec2(0.7, -0.7), vec2(-0.7, -0.7)
    );
    float sum = 0.0;
    for (int i = 0; i < 8; ++i) {
        vec2 duv = offs[i] * radius * texel;
        vec3 vplPos = texture(rsmPositionTex, clamp(uv + duv, 0.0, 1.0)).xyz;
        float tRsm = dot(vplPos - u.lightOrigin.xyz, Ld);
        float bias = 0.02 + 0.10 * slope;
        float visible = (tRsm + bias < tSurface) ? 0.0 : 1.0; // biased comparison
        sum += visible;
    }
    return sum / 8.0;
}

// --- PBR Helper Functions ---

// Fresnel-Schlick approximation
vec3 fresnel_schlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// GGX/Trowbridge-Reitz Normal Distribution Function
float distribution_ggx(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return num / max(denom, 0.0001);
}

// Schlick-GGX Geometry Function
float geometry_schlick_ggx(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return num / max(denom, 0.0001);
}

// Smith's method for Geometry Function
float geometry_smith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = geometry_schlick_ggx(NdotV, roughness);
    float ggx1 = geometry_schlick_ggx(NdotL, roughness);
    
    return ggx1 * ggx2;
}

// Cook-Torrance BRDF
vec3 cook_torrance_brdf(vec3 albedo, vec3 N, vec3 V, vec3 L, float roughness, float metallic) {
    vec3 H = normalize(V + L);
    
    // Calculate base reflectivity (F0)
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);
    
    // Calculate PBR components
    float NDF = distribution_ggx(N, H, roughness);
    float G = geometry_smith(N, V, L, roughness);
    vec3 F = fresnel_schlick(max(dot(H, V), 0.0), F0);
    
    // Calculate specular component
    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;
    
    // Calculate diffuse component using energy conservation
    vec3 kS = F; // Specular contribution
    vec3 kD = vec3(1.0) - kS; // Diffuse contribution
    kD *= 1.0 - metallic; // Metals don't have diffuse
    
    float NdotL = max(dot(N, L), 0.0);
    
    // Enhanced Lambertian diffuse with additional light boost for dielectrics
    vec3 diffuse = kD * albedo / PI;
    
    // Add extra diffuse boost for non-metallic materials to improve visibility
    if (metallic < 0.5) {
        diffuse *= 1.5; // Boost diffuse for dielectrics
    }
    
    return (diffuse + specular) * NdotL;
}

// 光照计算
// 支持传统渲染和基于物理的渲染（PBR）双模式
vec3 getLight(vec3 p, vec3 n, vec3 viewDir, vec3 albedo, int matId) {
    vec3 l = normalize(-u.lightDir.xyz); // 主光源方向
    vec3 r = reflect(-l, n); // 反射光方向
    
    // 菲涅尔效应：视角与法线夹角越大，反射越强（常见于水面、玻璃等）
    float fresnel = 1.0;
    if (matId == 1) { // 只对球体应用
        fresnel = pow(1.0 - max(0.0, dot(viewDir, n)), 2.0);
    }
    
    // Increase base ambient for PBR mode to improve visibility
    float ambientMultiplier = (u.pbrParams.x > 0.5) ? 0.8 : 0.3;
    vec3 finalColor = vec3(0.12, 0.15, 0.20) * ambientMultiplier;
    
    // Calculate shadow first (used by both direct and indirect lighting)
    float shadow = 1.0;
    if (u.enableLights.x == 1) {
        if (u.rsmParams.w > 0.5) {
            shadow = rsmShadow(p + n * 0.05, n);
        } else {
            shadow = softShadow(p + n * 0.07, l, 0.07, 6.0, 6.0 * u.shadowParams.x);
        }
        shadow = mix(0.2, 1.0, shadow * u.shadowParams.y); // 应用阴影强度
    }
    
    // Get material-specific PBR properties
    float roughness = u.pbrParams.y; // Default to global roughness
    float metallic = u.pbrParams.z;  // Default to global metallic
    
    // Override with material-specific values for spheres
    if (matId == 1) { // Sphere1 (textured)
        roughness = u.roughnessValues.x;
        metallic = u.metallicValues.x;
    } else if (matId == 7) { // Sphere2 (solid color)
        roughness = u.roughnessValues.y;
        metallic = u.metallicValues.y;
    }
    
    // Apply global base color factors for PBR (but ensure minimum brightness)
    vec3 pbrAlbedo = albedo;
    if (u.pbrParams.x > 0.5) { // PBR enabled
        // Apply base color factors more conservatively to avoid over-darkening
        vec3 colorFactor = max(u.baseColorFactors.rgb * u.baseColorFactors.a, vec3(0.3));
        pbrAlbedo *= colorFactor;
    }
    
    // 1. 主光源 (Key Light) - Direct Lighting
    if (u.enableLights.x == 1) {
        vec3 lightColor = u.lightColors[0].rgb * u.lightColors[0].a;
        
        if (u.pbrParams.x > 0.5) {
            // === PBR LIGHTING ===
            vec3 brdf = cook_torrance_brdf(pbrAlbedo, n, viewDir, l, roughness, metallic);
            // Increase PBR lighting intensity to compensate for energy conservation
            float pbrIntensity = 3.0; // Boost PBR lighting to match traditional appearance
            finalColor += brdf * lightColor * shadow * u.lightDir.w * pbrIntensity;
        } else {
            // === TRADITIONAL LIGHTING ===
            float ndotl = max(0.0, dot(n, l));
            float diff = ndotl; // 漫反射强度
            
            // 模拟GGX高光，金属度(w)越高，高光越集中
            float rough = clamp(1.0 - u.shadowParams.w, 0.05, 0.95);
            float specPower = mix(16.0, 64.0, u.shadowParams.w);
            float spec = pow(max(0.0, dot(viewDir, r)), specPower) * (1.0 - rough); // 高光强度
            
            finalColor += (diff * albedo + spec * lightColor) * lightColor * shadow * u.lightDir.w;
        }
    }
    
    // RSM Indirect Lighting (separate from direct lighting)
    if (u.rsmParams.w > 0.5 && u.rsmParams.z > 0.5) {
        float radius = max(u.rsmParams.x, 1.0);
        int samples = int(max(u.rsmParams.y, 1.0));
        vec3 rel = p - u.lightOrigin.xyz;
        vec2 base = vec2(dot(rel, u.lightRight.xyz) / max(u.lightOrthoHalfSize.x, 1e-4),
                         dot(rel, u.lightUp.xyz)    / max(u.lightOrthoHalfSize.y, 1e-4));
        vec2 baseUV = base * 0.5 + 0.5;
        vec3 bounce = vec3(0.0);
        int validSamples = 0;
        
        // Choose sampling strategy based on importance sampling toggle
        if (u.debugParams[1] > 0.5) {
            // === IMPORTANCE SAMPLING - Three-Phase Adaptive Strategy ===
            
            // Phase 1: Coarse Analysis Pass (8 samples)
            vec2 coarseOffs[8] = vec2[8](
                vec2( 0.0,  0.0), vec2( 1.0,  0.0), vec2(-1.0,  0.0), vec2( 0.0,  1.0),
                vec2( 0.0, -1.0), vec2( 0.7,  0.7), vec2(-0.7,  0.7), vec2( 0.7, -0.7)
            );
            
            float maxImportance = 0.0;
            vec2 bestRegion = vec2(0.0);
            
            for (int i = 0; i < 8; ++i) {
                vec2 duv = coarseOffs[i] * radius * 0.5 / max(u.rsmResolution.xy, vec2(1.0));
                vec2 uv = clamp(baseUV + duv, 0.0, 1.0);
                
                vec3 vplPos = texture(rsmPositionTex, uv).xyz;
                vec3 vplNor = texture(rsmNormalTex, uv).xyz;
                vec3 flux = texture(rsmFluxTex, uv).xyz;
                
                if (length(vplPos) < 0.1) continue;
                
                vec3 wi = normalize(vplPos - p);
                float cos1 = max(dot(n, wi), 0.0);
                float cos2 = max(dot(vplNor, -wi), 0.0);
                float fluxMag = length(flux);
                
                float importance = cos1 * cos2 * fluxMag;
                if (importance > maxImportance) {
                    maxImportance = importance;
                    bestRegion = duv;
                }
            }
            
            // Phase 2: Focused Dense Sampling (20 samples)
            vec2 denseOffs[20] = vec2[20](
                vec2( 0.0,  0.0), vec2( 0.3,  0.0), vec2(-0.3,  0.0), vec2( 0.0,  0.3), vec2( 0.0, -0.3),
                vec2( 0.2,  0.2), vec2(-0.2,  0.2), vec2( 0.2, -0.2), vec2(-0.2, -0.2),
                vec2( 0.5,  0.0), vec2(-0.5,  0.0), vec2( 0.0,  0.5), vec2( 0.0, -0.5),
                vec2( 0.4,  0.4), vec2(-0.4,  0.4), vec2( 0.4, -0.4), vec2(-0.4, -0.4),
                vec2( 0.6,  0.2), vec2(-0.6, -0.2), vec2( 0.2, -0.6)
            );
            
            for (int i = 0; i < 20; ++i) {
                vec2 localOffset = denseOffs[i] * 0.3;
                vec2 duv = (bestRegion + localOffset * radius / max(u.rsmResolution.xy, vec2(1.0)));
                vec2 uv = clamp(baseUV + duv, 0.0, 1.0);
                
                vec3 vplPos = texture(rsmPositionTex, uv).xyz;
                vec3 vplNor = normalize(texture(rsmNormalTex, uv).xyz);
                vec3 flux = texture(rsmFluxTex, uv).xyz;
                
                if (length(vplPos) < 0.1) continue;
                
                vec3 wi = vplPos - p;
                float dist = length(wi);
                if (dist < 0.05) continue;
                wi = normalize(wi);
                
                float cos1 = max(dot(n, wi), 0.0);
                float cos2 = max(dot(vplNor, -wi), 0.0);
                
                if (cos1 < 0.05 || cos2 < 0.05) continue;

                // Enhanced VPL contribution with geometric consistency check
                float enhancedFalloff = 1.0 / max(dist * dist + 0.5, 1e-3);
                vec3 brdf = albedo / 3.14159;
                
                // Geometric consistency check to reduce seam artifacts
                vec3 vplToReceiver = normalize(p - vplPos);
                float normalConsistency = dot(normalize(n), normalize(vplNor));
                
                // Reduce contribution from VPLs that create harsh geometric discontinuities
                float geometricDamping = 1.0;
                if (abs(normalConsistency) > 0.8) { // Nearly parallel or anti-parallel surfaces
                    geometricDamping *= 0.3; // Strong reduction for seam-like configurations
                } else if (abs(normalConsistency) > 0.6) {
                    geometricDamping *= 0.6; // Moderate reduction
                }
                
                // Apply material-based enhancement for spheres but reduce for walls
                float materialBoost = (matId == 1 || matId == 7) ? 2.0 : 0.7; // Reduced wall contribution
                bounce += brdf * flux * (cos1 * cos2) * enhancedFalloff * materialBoost * geometricDamping;
                validSamples++;
            }
            
            // Phase 3: Coverage Sampling (4 samples)
            vec2 coverageOffs[4] = vec2[4](
                vec2(-0.7, -0.7), vec2( 0.8,  0.3), vec2(-0.3,  0.8), vec2( 0.9, -0.4)
            );
            
            for (int i = 0; i < 4; ++i) {
                vec2 duv = coverageOffs[i] * radius / max(u.rsmResolution.xy, vec2(1.0));
                vec2 uv = clamp(baseUV + duv, 0.0, 1.0);
                
                vec3 vplPos = texture(rsmPositionTex, uv).xyz;
                vec3 vplNor = normalize(texture(rsmNormalTex, uv).xyz);
                vec3 flux = texture(rsmFluxTex, uv).xyz;
                
                if (length(vplPos) < 0.1) continue;
                
                vec3 wi = vplPos - p;
                float dist = length(wi);
                if (dist < 0.05) continue;
                wi = normalize(wi);
                
                float cos1 = max(dot(n, wi), 0.0);
                float cos2 = max(dot(vplNor, -wi), 0.0);
                
                if (cos1 < 0.05 || cos2 < 0.05) continue;

                // Enhanced falloff for coverage samples with seam reduction
                float enhancedFalloff = 1.0 / max(dist * dist + 0.5, 1e-3);
                vec3 brdf = albedo / 3.14159;
                
                // Geometric consistency check for coverage samples
                float normalConsistency = dot(normalize(n), normalize(vplNor));
                float geometricDamping = 1.0;
                if (abs(normalConsistency) > 0.8) {
                    geometricDamping *= 0.3;
                } else if (abs(normalConsistency) > 0.6) {
                    geometricDamping *= 0.6;
                }
                
                float materialBoost = (matId == 1 || matId == 7) ? 2.0 : 0.7;
                bounce += brdf * flux * (cos1 * cos2) * enhancedFalloff * materialBoost * geometricDamping;
                validSamples++;
            }
            
            // Normalize by the number of contributing samples and scale by user intensity
            if (validSamples > 0) {
                finalColor += (u.indirectParams.x) * (bounce / float(validSamples));
            }
        } else {
            // === UNIFORM SAMPLING - Original Strategy ===
            
            // Uniform sampling pattern with 32 points
            vec2 offs[32] = vec2[32](
                vec2(0.0, 0.0),     // Center - 1
                vec2(0.3, 0.0), vec2(-0.3, 0.0), vec2(0.0, 0.3), vec2(0.0, -0.3),         // Ring 1 cardinal - 5
                vec2(0.2, 0.2), vec2(-0.2, 0.2), vec2(0.2, -0.2), vec2(-0.2, -0.2),       // Ring 1 diagonal - 9
                vec2(0.6, 0.0), vec2(-0.6, 0.0), vec2(0.0, 0.6), vec2(0.0, -0.6),         // Ring 2 cardinal - 13
                vec2(0.45, 0.45), vec2(-0.45, 0.45), vec2(0.45, -0.45), vec2(-0.45, -0.45), // Ring 2 diagonal - 17
                vec2(1.0, 0.0), vec2(-1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, -1.0),         // Ring 3 cardinal - 21
                vec2(0.7, 0.7), vec2(-0.7, 0.7), vec2(0.7, -0.7), vec2(-0.7, -0.7),       // Ring 3 diagonal - 25
                vec2(0.5, 0.2), vec2(-0.5, 0.2), vec2(0.2, 0.5), vec2(-0.2, 0.5),         // Intermediate 1 - 29
                vec2(0.8, 0.3), vec2(-0.8, -0.3), vec2(0.3, -0.8)                         // Final 3 - 32
            );
            
            int N = min(samples, 32);
            int totalValid = 0;
            
            for (int i = 0; i < N; ++i) {
                // Add some randomization to reduce banding
                float random = fract(sin(dot(p.xz + float(i), vec2(12.9898, 78.233))) * 43758.5453);
                vec2 jitter = vec2(fract(random * 43758.5453), fract(random * 23421.6319)) * 2.0 - 1.0;
                vec2 duv = (offs[i] + jitter * 0.05) * radius / max(u.rsmResolution.xy, vec2(1.0));
                vec2 uv = clamp(baseUV + duv, 0.0, 1.0);
                
                vec3 vplPos = texture(rsmPositionTex, uv).xyz;
                vec3 vplNor = normalize(texture(rsmNormalTex, uv).xyz);
                vec3 flux   = texture(rsmFluxTex, uv).xyz;
                
                // Skip invalid VPLs (background)
                if (length(vplPos) < 0.1) continue;
                
                vec3 wi = vplPos - p;
                float dist = length(wi);
                if (dist < 0.05) continue; // Skip self-illumination
                wi = normalize(wi);
                
                float cos1 = max(dot(n, wi), 0.0);
                float cos2 = max(dot(vplNor, -wi), 0.0);
                
                // Skip VPLs facing away from receiver
                if (cos1 < 0.05 || cos2 < 0.05) continue;
                
                // Enhanced falloff for uniform sampling with seam artifact reduction
                float enhancedFalloff = 1.0 / max(dist * dist + 0.5, 1e-3);
                vec3 brdf = albedo / 3.14159;
                
                // Geometric consistency check to prevent seam over-illumination
                float normalConsistency = dot(normalize(n), normalize(vplNor));
                float geometricDamping = 1.0;
                if (abs(normalConsistency) > 0.8) {
                    geometricDamping *= 0.3; // Strong reduction for seam-like configurations
                } else if (abs(normalConsistency) > 0.6) {
                    geometricDamping *= 0.6;
                }
                
                float materialBoost = (matId == 1 || matId == 7) ? 2.0 : 0.7;
                bounce += brdf * flux * (cos1 * cos2) * enhancedFalloff * materialBoost * geometricDamping;
                totalValid++;
            }
            
            // Normalize and apply uniform-sampled indirect lighting
            if (totalValid > 0) {
                finalColor += (u.indirectParams.x) * (bounce / float(totalValid));
            }
        }
    }
    
    // 2. 填充光 (Fill Light)，用于照亮暗部
    if (u.enableLights.y == 1) {
        vec3 fillDir = normalize(vec3(0.4, 0.3, 0.7)); // 填充光方向
        vec3 fillColor = u.lightColors[1].rgb * u.lightColors[1].a;
        
        if (u.pbrParams.x > 0.5) {
            // PBR fill light with increased intensity
            vec3 fillBrdf = cook_torrance_brdf(pbrAlbedo, n, viewDir, fillDir, roughness, metallic);
            finalColor += fillBrdf * fillColor * 0.6; // Increased from 0.2 to 0.6
        } else {
            // Traditional fill light
            float fillDiff = max(0.0, dot(n, fillDir)) * 0.2;
            finalColor += fillDiff * albedo * fillColor;
        }
    }
    
    // 3. 边缘光 (Rim Light)，用于勾勒物体轮廓
    if (u.enableLights.z == 1) {
        float rim = 1.0 - max(0.0, dot(viewDir, n));
        rim = pow(rim, 3.0);
        vec3 rimColor = u.lightColors[2].rgb * u.lightColors[2].a * 0.8;
        
        if (u.pbrParams.x > 0.5) {
            // For PBR, rim lighting is more subtle and affects fresnel
            vec3 F0 = vec3(0.04);
            F0 = mix(F0, pbrAlbedo, metallic);
            vec3 rimFresnel = fresnel_schlick(1.0 - rim, F0);
            finalColor += rim * rimFresnel * rimColor;
        } else {
            // Traditional rim light
            finalColor += rim * rimColor;
        }
    }
    
    // 4. 模拟环境反射
    if (u.enableLights.w == 1) {
        vec3 envReflect = reflect(-viewDir, n);
        vec3 envColor = vec3(0.95, 0.85, 0.6); // 温暖的金黄色反射
        
        if (u.pbrParams.x > 0.5) {
            // === PBR ENVIRONMENT REFLECTION ===
            // Calculate F0 based on metallic workflow
            vec3 F0 = vec3(0.04);
            F0 = mix(F0, pbrAlbedo, metallic);
            
            // Use PBR Fresnel calculation
            float cosTheta = max(dot(-viewDir, n), 0.0);
            vec3 envFresnel = fresnel_schlick(cosTheta, F0);
            
            // Environment reflection strength based on reflection vector
            float envAmount = max(0.0, envReflect.y);
            
            // Apply PBR-based environment reflection with increased intensity
            finalColor += envAmount * envFresnel * envColor * 0.8; // Increased from 0.4
        } else {
            // === TRADITIONAL ENVIRONMENT REFLECTION ===
            float envAmount = max(0.0, envReflect.y) * fresnel;
            finalColor += envAmount * envColor * 0.3;
        }
    }
    
    // 应用全局蓝色色调
    finalColor *= mix(vec3(1.0), vec3(0.7, 0.85, 1.0), u.shadowParams.z);
    
    return finalColor;
}

// --- 主函数 ---
void main() {
    // 1. 屏幕坐标(UV)转换：将[0,1]的纹理坐标转换为[-1,1]的规范化设备坐标，并校正宽高比
    vec2 uv = (fragTexCoord - 0.5) * 2.0;
    uv.x *= u.iResolution.x / u.iResolution.y;
    
    // 2. 相机设置
    vec3 ro = vec3(0.0, 0.0, 5.0);     // 相机位置 (Ray Origin)
    vec3 target = vec3(0.0, 0.0, 0.0); // 目标点
    vec3 up = vec3(0.0, 1.0, 0.0);     // 上方向
    
    // 计算相机坐标系的基向量
    vec3 cw = normalize(target - ro); // 前方 (w)
    vec3 cu = normalize(cross(cw, up)); // 右方 (u)
    vec3 cv = normalize(cross(cu, cw)); // 上方 (v)
    
    // 3. 计算光线方向 (Ray Direction)
    // 根据UV坐标和相机基向量，计算出从相机发出、穿过当前像素的光线方向
    // 1.2 * cw 控制了视野(FOV)，值越大FOV越小
    vec3 rd = normalize(uv.x * cu + uv.y * cv + 1.2 * cw);
    
    // 4. 执行光线步进，获取到场景的距离d
    float d = rayMarch(ro, rd);
    
    // 5. 根据距离d计算颜色
    vec3 color = vec3(0.85, 0.9, 0.95); // 浅灰蓝色背景，更接近图片的柔和背景
    
    if (d < MAX_DIST) { // 如果光线击中了物体
        vec3 p = ro + rd * d;       // 计算交点坐标
        vec3 n = getNormal(p);      // 计算交点法线
        int matId = getMaterial(p); // 获取材质ID
        
        // 根据材质ID获取物体基础色(Albedo)
        vec3 albedo;
        if (matId == 1) { // 球体1 - 带花朵纹理
            // Calculate sphere UV coordinates for texture mapping
            // Need to transform back to sphere-local coordinates
            float rotationAngle = u.iTime * 0.5;
            mat3 zRotation = rotateZ(rotationAngle);
            vec3 sphere1Pos = zRotation * vec3(2.0, 0.0, 0.0);
            vec3 sphere1P = p - sphere1Pos;
            mat3 rotation1 = rotateX(u.sphereRotation.x) * rotateY(u.sphereRotation.y) * rotateZ(u.sphereRotation.z);
            sphere1P = rotation1 * sphere1P;
            vec2 sphereUV = getSphereUV(sphere1P);
            albedo = texture(flowerTex, sphereUV).rgb;
        } else if (matId == 7) { // 球体2 - 纯色
            albedo = u.sphereColor.rgb;
        } else if (matId == 2) { // 地面 (底部) - 深蓝灰色
            albedo = vec3(0.3, 0.4, 0.6);
        } else if (matId == 3) { // 天花板 (顶部) - 浅蓝灰色
            albedo = vec3(0.7, 0.8, 0.9);
        } else if (matId == 4) { // 左墙 - 温暖绿色
            albedo = vec3(0.4, 0.7, 0.5);
        } else if (matId == 5) { // 右墙 - 温暖红色
            albedo = vec3(0.7, 0.4, 0.4);
        } else if (matId == 6) { // 后墙 - 温暖紫色
            albedo = vec3(0.6, 0.4, 0.7);
        } else { // 默认墙面
            vec3 baseColor = vec3(0.92, 0.94, 0.98); // 明亮的浅色
            baseColor += n * 0.001; // 根据法线增加一些颜色变化
            albedo = baseColor * u.shadowParams.z;
        }
        
        // 调用光照函数计算最终颜色
        color = getLight(p, n, -rd, albedo, matId);
        
        // 添加距离雾效，增加场景深度感
        float fog = 1.0 - exp(-d * 0.08);
        color = mix(color, vec3(0.85, 0.9, 0.95), fog * 0.15); // 使用明亮的背景色作为雾效
    }
    
    // 6. 后期处理
    // Removed manual gamma correction to avoid double gamma with sRGB swapchain
    // 跳过Gamma校正，当前使用sRGB输入+sRGB纹理+sRGB交换链的组合
    // color = pow(color, vec3(0.75)); // 更强的Gamma校正，提亮整体
    color = mix(color, color * vec3(1.05, 1.02, 0.95), 0.12); // 增加温暖的黄色色调
    
    // Debug visualization: show RSM buffers instead of final render when enabled
    if (u.debugParams.x > 0.5) {
        // Visualize: position (xyz) as color, normal, and flux
        // Pack into RGB channels to help debugging. Here prefer flux as primary.
        vec3 flux = texture(rsmFluxTex, fragTexCoord).rgb;
        vec3 normalVis = texture(rsmNormalTex, fragTexCoord).xyz * 0.5 + 0.5;
        vec3 posVis = texture(rsmPositionTex, fragTexCoord).xyz * 0.05 + 0.5;
        // Compose a quick tri-view by weighting
        vec3 debugColor = mix(posVis, normalVis, 0.3);
        debugColor = mix(debugColor, flux, 0.6);
        outColor = vec4(debugColor, 1.0);
        return;
    }
    
    // Debug visualization: show only indirect lighting when enabled
    if (u.debugParams.z > 0.5) {
        if (d < MAX_DIST) { // If light ray hit an object
            vec3 p = ro + rd * d;
            vec3 n = getNormal(p);
            int matId = getMaterial(p);
            
            // Get albedo for the surface
            vec3 albedo;
            if (matId == 1) {
                float rotationAngle = u.iTime * 0.5;
                mat3 zRotation = rotateZ(rotationAngle);
                vec3 sphere1Pos = zRotation * vec3(2.0, 0.0, 0.0);
                vec3 sphere1P = p - sphere1Pos;
                mat3 rotation1 = rotateX(u.sphereRotation.x) * rotateY(u.sphereRotation.y) * rotateZ(u.sphereRotation.z);
                sphere1P = rotation1 * sphere1P;
                vec2 sphereUV = getSphereUV(sphere1P);
                albedo = texture(flowerTex, sphereUV).rgb;
            } else if (matId == 7) {
                albedo = u.sphereColor.rgb;
            } else if (matId == 2) {
                albedo = vec3(0.3, 0.4, 0.6);
            } else if (matId == 3) {
                albedo = vec3(0.7, 0.8, 0.9);
            } else if (matId == 4) {
                albedo = vec3(0.4, 0.7, 0.5);
            } else if (matId == 5) {
                albedo = vec3(0.7, 0.4, 0.4);
            } else if (matId == 6) {
                albedo = vec3(0.6, 0.4, 0.7);
            } else {
                albedo = vec3(0.92, 0.94, 0.98) * u.shadowParams.z;
            }
            
            // Only show indirect lighting from RSM
            vec3 indirectOnly = vec3(0.0);
            if (u.rsmParams.w > 0.5 && u.rsmParams.z > 0.5) {
                // Calculate only the indirect lighting portion
                float radius = max(u.rsmParams.x, 1.0);
                vec3 rel = p - u.lightOrigin.xyz;
                vec2 base = vec2(dot(rel, u.lightRight.xyz) / max(u.lightOrthoHalfSize.x, 1e-4),
                                 dot(rel, u.lightUp.xyz)    / max(u.lightOrthoHalfSize.y, 1e-4));
                vec2 baseUV = base * 0.5 + 0.5;
                vec3 bounce = vec3(0.0);
                int validSamples = 0;
                
                // Use same sampling strategy as in getLight function but only return indirect contribution
                if (u.debugParams[1] > 0.5) {
                    // Importance sampling approach (simplified version)
                    vec2 offs[16] = vec2[16](
                        vec2(0.0, 0.0), vec2(0.3, 0.0), vec2(-0.3, 0.0), vec2(0.0, 0.3),
                        vec2(0.0, -0.3), vec2(0.2, 0.2), vec2(-0.2, 0.2), vec2(0.2, -0.2),
                        vec2(-0.2, -0.2), vec2(0.5, 0.0), vec2(-0.5, 0.0), vec2(0.0, 0.5),
                        vec2(0.0, -0.5), vec2(0.4, 0.4), vec2(-0.4, 0.4), vec2(0.4, -0.4)
                    );
                    
                    for (int i = 0; i < 16; ++i) {
                        vec2 duv = offs[i] * radius / max(u.rsmResolution.xy, vec2(1.0));
                        vec2 uv = clamp(baseUV + duv, 0.0, 1.0);
                        
                        vec3 vplPos = texture(rsmPositionTex, uv).xyz;
                        vec3 vplNor = normalize(texture(rsmNormalTex, uv).xyz);
                        vec3 flux = texture(rsmFluxTex, uv).xyz;
                        
                        if (length(vplPos) < 0.1) continue;
                        
                        vec3 wi = vplPos - p;
                        float dist = length(wi);
                        if (dist < 0.05) continue;
                        wi = normalize(wi);
                        
                        float cos1 = max(dot(n, wi), 0.0);
                        float cos2 = max(dot(vplNor, -wi), 0.0);
                        
                        if (cos1 < 0.05 || cos2 < 0.05) continue;
                        
                        // Enhanced falloff for debug mode with seam artifact reduction
                        float enhancedFalloff = 1.0 / max(dist * dist + 0.5, 1e-3);
                        vec3 brdf = albedo / 3.14159;
                        
                        // Geometric damping to prevent seam over-illumination
                        float normalConsistency = dot(normalize(n), normalize(vplNor));
                        float geometricDamping = 1.0;
                        if (abs(normalConsistency) > 0.8) {
                            geometricDamping *= 0.3;
                        } else if (abs(normalConsistency) > 0.6) {
                            geometricDamping *= 0.6;
                        }
                        
                        float materialBoost = (matId == 1 || matId == 7) ? 2.0 : 0.7;
                        bounce += brdf * flux * (cos1 * cos2) * enhancedFalloff * materialBoost * geometricDamping;
                        validSamples++;
                    }
                } else {
                    // Uniform sampling approach
                    vec2 offs[16] = vec2[16](
                        vec2(0.0, 0.0), vec2(0.3, 0.0), vec2(-0.3, 0.0), vec2(0.0, 0.3),
                        vec2(0.0, -0.3), vec2(0.2, 0.2), vec2(-0.2, 0.2), vec2(0.2, -0.2),
                        vec2(-0.2, -0.2), vec2(0.6, 0.0), vec2(-0.6, 0.0), vec2(0.0, 0.6),
                        vec2(0.0, -0.6), vec2(0.45, 0.45), vec2(-0.45, 0.45), vec2(0.45, -0.45)
                    );
                    
                    for (int i = 0; i < 16; ++i) {
                        vec2 duv = offs[i] * radius / max(u.rsmResolution.xy, vec2(1.0));
                        vec2 uv = clamp(baseUV + duv, 0.0, 1.0);
                        
                        vec3 vplPos = texture(rsmPositionTex, uv).xyz;
                        vec3 vplNor = normalize(texture(rsmNormalTex, uv).xyz);
                        vec3 flux = texture(rsmFluxTex, uv).xyz;
                        
                        if (length(vplPos) < 0.1) continue;
                        
                        vec3 wi = vplPos - p;
                        float dist = length(wi);
                        if (dist < 0.05) continue;
                        wi = normalize(wi);
                        
                        float cos1 = max(dot(n, wi), 0.0);
                        float cos2 = max(dot(vplNor, -wi), 0.0);
                        
                        if (cos1 < 0.05 || cos2 < 0.05) continue;
                        
                        // Enhanced falloff for debug mode with seam reduction
                        float enhancedFalloff = 1.0 / max(dist * dist + 0.5, 1e-3);
                        vec3 brdf = albedo / 3.14159;
                        
                        // Geometric damping for debug uniform sampling
                        float normalConsistency = dot(normalize(n), normalize(vplNor));
                        float geometricDamping = 1.0;
                        if (abs(normalConsistency) > 0.8) {
                            geometricDamping *= 0.3;
                        } else if (abs(normalConsistency) > 0.6) {
                            geometricDamping *= 0.6;
                        }
                        
                        float materialBoost = (matId == 1 || matId == 7) ? 2.0 : 0.7;
                        bounce += brdf * flux * (cos1 * cos2) * enhancedFalloff * materialBoost * geometricDamping;
                        validSamples++;
                    }
                }
                
                if (validSamples > 0) {
                    indirectOnly = (u.indirectParams.x) * (bounce / float(validSamples));
                }
            }
            
            // Show only indirect lighting on black background
            outColor = vec4(indirectOnly, 1.0);
            return;
        } else {
            // Background - show as black when showing indirect only
            outColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
    }

    // 7. 输出最终颜色
    outColor = vec4(color, 1.0);
}
