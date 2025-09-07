#version 450

// 输入：从顶点着色器传入的变量（这里未使用，但保留以保持接口一致）
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

// 输出：最终的像素颜色
layout(location = 0) out vec4 outColor;

// UBO (Uniform Buffer Object)，用于从CPU传递全局变量给Shader，类似ShaderToy的内置变量
layout(std140, binding = 0) uniform ShaderToyUBO {
  float iTime;        // 时间，秒
  vec2 iResolution;   // 视口分辨率，像素
  vec2 iMouse;        // 鼠标位置，像素
  int iFrame;         // 当前帧数
  ivec4 enableLights; // x,y,z,w 对应启用光源 1..4（1 启用，0 关闭）
};

// --- Inigo Quilez 的 3D SDF 函数库 ---
// 源码来自 https://www.iquilezles.org/articles/distfunctions/
// 这里进行了少量适配

// AA 是抗锯齿(Anti-Aliasing)的采样等级。>1会启用超级采样。
#define AA 1
// 定义一个整型的0，用于循环
#define ZERO 0

// --- 数学辅助函数 ---
float dot2(in vec2 v) { return dot(v, v); } // 计算vec2的点积平方(v.x*v.x + v.y*v.y)
float dot2(in vec3 v) { return dot(v, v); } // 计算vec3的点积平方
float ndot(in vec2 a, in vec2 b) { return a.x * b.x - a.y * b.y; }

// --- SDF (有向距离函数) 几何体定义 ---
// 每个函数输入一个3D点 `p`，返回该点到几何体表面的最短距离

float sdPlane(vec3 p) { return p.y; } // 平面
float sdSphere(vec3 p, float s) { return length(p) - s; } // 球体
float sdBox(vec3 p, vec3 b) { // 立方体
  vec3 d = abs(p) - b;
  return min(max(d.x, max(d.y, d.z)), 0.0) + length(max(d, 0.0));
}
float sdBoxFrame(vec3 p, vec3 b, float e) { // 立方体线框
  p = abs(p) - b;
  vec3 q = abs(p + e) - e;
  return min(min(length(max(vec3(p.x, q.y, q.z), 0.0)) +
                     min(max(p.x, max(q.y, q.z)), 0.0),
                 length(max(vec3(q.x, p.y, q.z), 0.0)) +
                     min(max(q.x, max(p.y, q.z)), 0.0)),
             length(max(vec3(q.x, q.y, p.z), 0.0)) +
                 min(max(q.x, max(q.y, p.z)), 0.0));
}
float sdEllipsoid(in vec3 p, in vec3 r) { // 椭球体
  float k0 = length(p / r);
  float k1 = length(p / (r * r));
  return k0 * (k0 - 1.0) / k1;
}
float sdTorus(vec3 p, vec2 t) { // 圆环
  return length(vec2(length(p.xz) - t.x, p.y)) - t.y;
}
float sdCappedTorus(in vec3 p, in vec2 sc, in float ra, in float rb) { // 带帽圆环
  p.x = abs(p.x);
  float k = (sc.y * p.x > sc.x * p.y) ? dot(p.xy, sc) : length(p.xy);
  return sqrt(dot(p, p) + ra * ra - 2.0 * ra * k) - rb;
}
float sdHexPrism(vec3 p, vec2 h) { // 六棱柱
  const vec3 k = vec3(-0.8660254, 0.5, 0.57735);
  p = abs(p);
  p.xy -= 2.0 * min(dot(k.xy, p.xy), 0.0) * k.xy;
  vec2 d = vec2(length(p.xy - vec2(clamp(p.x, -k.z * h.x, k.z * h.x), h.x)) *
                    sign(p.y - h.x),
                p.z - h.y);
  return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}
float sdOctogonPrism(in vec3 p, in float r, float h) { // 八棱柱
  const vec3 k = vec3(-0.9238795325, 0.3826834323, 0.4142135623);
  p = abs(p);
  p.xy -= 2.0 * min(dot(vec2(k.x, k.y), p.xy), 0.0) * vec2(k.x, k.y);
  p.xy -= 2.0 * min(dot(vec2(-k.x, k.y), p.xy), 0.0) * vec2(-k.x, k.y);
  p.xy -= vec2(clamp(p.x, -k.z * r, k.z * r), r);
  vec2 d = vec2(length(p.xy) * sign(p.y), p.z - h);
  return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}
float sdCapsule(vec3 p, vec3 a, vec3 b, float r) { // 胶囊体
  vec3 pa = p - a, ba = b - a;
  float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
  return length(pa - ba * h) - r;
}
float sdRoundCone(in vec3 p, in float r1, float r2, float h) { // 圆角圆锥
  vec2 q = vec2(length(p.xz), p.y);
  float b = (r1 - r2) / h;
  float a = sqrt(1.0 - b * b);
  float k = dot(q, vec2(-b, a));
  if (k < 0.0)
    return length(q) - r1;
  if (k > a * h)
    return length(q - vec2(0.0, h)) - r2;
  return dot(q, vec2(a, b)) - r1;
}
float sdRoundCone(vec3 p, vec3 a, vec3 b, float r1, float r2) { // 任意朝向的圆角圆锥
  vec3 ba = b - a;
  float l2 = dot(ba, ba);
  float rr = r1 - r2;
  float a2 = l2 - rr * rr;
  float il2 = 1.0 / l2;
  vec3 pa = p - a;
  float y = dot(pa, ba);
  float z = y - l2;
  float x2 = dot2(pa * l2 - ba * y);
  float y2 = y * y * l2;
  float z2 = z * z * l2;
  float k = sign(rr) * rr * rr * x2;
  if (sign(z) * a2 * z2 > k)
    return sqrt(x2 + z2) * il2 - r2;
  if (sign(y) * a2 * y2 < k)
    return sqrt(x2 + y2) * il2 - r1;
  return (sqrt(x2 * a2 * il2) + y * rr) * il2 - r1;
}
float sdTriPrism(vec3 p, vec2 h) { // 三棱柱
  const float k = sqrt(3.0);
  h.x *= 0.5 * k;
  p.xy /= h.x;
  p.x = abs(p.x) - 1.0;
  p.y = p.y + 1.0 / k;
  if (p.x + k * p.y > 0.0)
    p.xy = vec2(p.x - k * p.y, -k * p.x - p.y) / 2.0;
  p.x -= clamp(p.x, -2.0, 0.0);
  float d1 = length(p.xy) * sign(-p.y) * h.x;
  float d2 = abs(p.z) - h.y;
  return length(max(vec2(d1, d2), 0.0)) + min(max(d1, d2), 0.0);
}
float sdCylinder(vec3 p, vec2 h) { // 圆柱
  vec2 d = abs(vec2(length(p.xz), p.y)) - h;
  return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}
float sdCylinder(vec3 p, vec3 a, vec3 b, float r) { // 任意朝向的圆柱
  vec3 pa = p - a, ba = b - a;
  float baba = dot(ba, ba);
  float paba = dot(pa, ba);
  float x = length(pa * baba - ba * paba) - r * baba;
  float y = abs(paba - baba * 0.5) - baba * 0.5;
  float x2 = x * x;
  float y2 = y * y * baba;
  float d = (max(x, y) < 0.0)
                ? -min(x2, y2)
                : (((x > 0.0) ? x2 : 0.0) + ((y > 0.0) ? y2 : 0.0));
  return sign(d) * sqrt(abs(d)) / baba;
}
float sdCone(in vec3 p, in vec2 c, float h) { // 圆锥
  vec2 q = h * vec2(c.x, -c.y) / c.y;
  vec2 w = vec2(length(p.xz), p.y);
  vec2 a = w - q * clamp(dot(w, q) / dot(q, q), 0.0, 1.0);
  vec2 b = w - q * vec2(clamp(w.x / q.x, 0.0, 1.0), 1.0);
  float k = sign(q.y);
  float d = min(dot(a, a), dot(b, b));
  float s = max(k * (w.x * q.y - w.y * q.x), k * (w.y - q.y));
  return sqrt(d) * sign(s);
}
float sdCappedCone(in vec3 p, in float h, in float r1, in float r2) { // 带帽圆锥 (圆台)
  vec2 q = vec2(length(p.xz), p.y);
  vec2 k1 = vec2(r2, h);
  vec2 k2 = vec2(r2 - r1, 2.0 * h);
  vec2 ca = vec2(q.x - min(q.x, (q.y < 0.0) ? r1 : r2), abs(q.y) - h);
  vec2 cb = q - k1 + k2 * clamp(dot(k1 - q, k2) / dot2(k2), 0.0, 1.0);
  float s = (cb.x < 0.0 && ca.y < 0.0) ? -1.0 : 1.0;
  return s * sqrt(min(dot2(ca), dot2(cb)));
}
float sdCappedCone(vec3 p, vec3 a, vec3 b, float ra, float rb) { // 任意朝向的带帽圆锥
  float rba = rb - ra;
  float baba = dot(b - a, b - a);
  float papa = dot(p - a, p - a);
  float paba = dot(p - a, b - a) / baba;
  float x = sqrt(papa - paba * paba * baba);
  float cax = max(0.0, x - ((paba < 0.5) ? ra : rb));
  float cay = abs(paba - 0.5) - 0.5;
  float k = rba * rba + baba;
  float f = clamp((rba * (x - ra) + paba * baba) / k, 0.0, 1.0);
  float cbx = x - ra - f * rba;
  float cby = paba - f;
  float s = (cbx < 0.0 && cay < 0.0) ? -1.0 : 1.0;
  return s *
         sqrt(min(cax * cax + cay * cay * baba, cbx * cbx + cby * cby * baba));
}
float sdSolidAngle(vec3 pos, vec2 c, float ra) { // 立体角
  vec2 p = vec2(length(pos.xz), pos.y);
  float l = length(p) - ra;
  float m = length(p - c * clamp(dot(p, c), 0.0, ra));
  return max(l, m * sign(c.y * p.x - c.x * p.y));
}
float sdOctahedron(vec3 p, float s) { // 八面体
  p = abs(p);
  float m = p.x + p.y + p.z - s;
  vec3 q;
  if (3.0 * p.x < m)
    q = p.xyz;
  else if (3.0 * p.y < m)
    q = p.yzx;
  else if (3.0 * p.z < m)
    q = p.zxy;
  else
    return m * 0.57735027;
  float k = clamp(0.5 * (q.z - q.y + s), 0.0, s);
  return length(vec3(q.x, q.y - s + k, q.z - k));
}
float sdPyramid(in vec3 p, in float h) { // 四棱锥
  float m2 = h * h + 0.25;
  p.xz = abs(p.xz);
  p.xz = (p.z > p.x) ? p.zx : p.xz;
  p.xz -= 0.5;
  vec3 q = vec3(p.z, h * p.y - 0.5 * p.x, h * p.x + 0.5 * p.y);
  float s = max(-q.x, 0.0);
  float t = clamp((q.y - 0.5 * p.z) / (m2 + 0.25), 0.0, 1.0);
  float a = m2 * (q.x + s) * (q.x + s) + q.y * q.y;
  float b =
      m2 * (q.x + 0.5 * t) * (q.x + 0.5 * t) + (q.y - m2 * t) * (q.y - m2 * t);
  float d2 = min(q.y, -q.x * m2 - q.y * 0.5) > 0.0 ? 0.0 : min(a, b);
  return sqrt((d2 + q.z * q.z) / m2) * sign(max(q.z, -p.y));
}
float sdRhombus(vec3 p, float la, float lb, float h, float ra) { // 菱形
  p = abs(p);
  vec2 b = vec2(la, lb);
  float f = clamp((ndot(b, b - 2.0 * p.xz)) / dot(b, b), -1.0, 1.0);
  vec2 q = vec2(length(p.xz - 0.5 * b * vec2(1.0 - f, 1.0 + f)) *
                        sign(p.x * b.y + p.z * b.x - b.x * b.y) -
                    ra,
                p.y - h);
  return min(max(q.x, q.y), 0.0) + length(max(q, 0.0));
}
float sdHorseshoe(in vec3 p, in vec2 c, in float r, in float le, vec2 w) { // 马蹄形
  p.x = abs(p.x);
  float l = length(p.xy);
  p.xy = mat2(-c.x, c.y, c.y, c.x) * p.xy;
  p.xy = vec2((p.y > 0.0 || p.x > 0.0) ? p.x : l * sign(-c.x),
              (p.x > 0.0) ? p.y : l);
  p.xy = vec2(p.x, abs(p.y - r)) - vec2(le, 0.0);
  vec2 q = vec2(length(max(p.xy, 0.0)) + min(0.0, max(p.x, p.y)), p.z);
  vec2 d = abs(q) - w;
  return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}
float sdU(in vec3 p, in float r, in float le, vec2 w) { // U形
  p.x = (p.y > 0.0) ? abs(p.x) : length(p.xy);
  p.x = abs(p.x - r);
  p.y = p.y - le;
  float k = max(p.x, p.y);
  vec2 q = vec2((k < 0.0) ? -k : length(max(p.xy, 0.0)), abs(p.z)) - w;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
}

// --- 场景构建 ---

// SDF操作：并集(Union)。返回两个SDF结果中距离更近的那个。
// d1和d2的.x是距离，.y是材质ID
vec2 opU(vec2 d1, vec2 d2) { return (d1.x < d2.x) ? d1 : d2; }

// map函数：定义整个3D场景。
// 输入一个空间点pos，返回场景中离此点最近的物体的(距离, 材质ID)。
// 这是所有SDF渲染的核心，通过组合不同的SDF几何体来“搭建”场景。
vec2 map(in vec3 pos) {
  // res的x是距离，y是材质ID。初始时，我们假定场景中只有y=0的平面。
  vec2 res = vec2(pos.y, 0.0); 

  // 下面的代码通过检查包围盒(sdBox)来剔除无关计算，提高效率
  // 如果点pos在某个大区域内，才计算该区域内的所有小物体
  
  // 第一列物体
  if (sdBox(pos - vec3(-2.0, 0.3, 0.25), vec3(0.3, 0.3, 1.0)) < res.x) {
    res = opU(res, vec2(sdSphere(pos - vec3(-2.0, 0.25, 0.0), 0.25), 26.9));
    res = opU(res, vec2(sdRhombus((pos - vec3(-2.0, 0.25, 1.0)).xzy, 0.15, 0.25,
                                  0.04, 0.08),
                        17.0));
  }
  // 第二列物体
  if (sdBox(pos - vec3(0.0, 0.3, -1.0), vec3(0.35, 0.3, 2.5)) < res.x) {
    res = opU(res,
              vec2(sdCappedTorus((pos - vec3(0.0, 0.30, 1.0)) * vec3(1, -1, 1),
                                 vec2(0.866025, -0.5), 0.25, 0.05),
                   25.0));
    res = opU(res, vec2(sdBoxFrame(pos - vec3(0.0, 0.25, 0.0),
                                   vec3(0.3, 0.25, 0.2), 0.025),
                        16.9));
    res =
        opU(res, vec2(sdCone(pos - vec3(0.0, 0.45, -1.0), vec2(0.6, 0.8), 0.45),
                      55.0));
    res = opU(res,
              vec2(sdCappedCone(pos - vec3(0.0, 0.25, -2.0), 0.25, 0.25, 0.1),
                   13.67));
    res = opU(res, vec2(sdSolidAngle(pos - vec3(0.0, 0.00, -3.0),
                                     vec2(3, 4) / 5.0, 0.4),
                        49.13));
  }
  // ... 其他列物体的定义 ...
  if (sdBox(pos - vec3(1.0, 0.3, -1.0), vec3(0.35, 0.3, 2.5)) < res.x) {
    res = opU(
        res,
        vec2(sdTorus((pos - vec3(1.0, 0.30, 1.0)).xzy, vec2(0.25, 0.05)), 7.1));
    res = opU(res, vec2(sdBox(pos - vec3(1.0, 0.25, 0.0), vec3(0.3, 0.25, 0.1)),
                        3.0));
    res = opU(res,
              vec2(sdCapsule(pos - vec3(1.0, 0.00, -1.0), vec3(-0.1, 0.1, -0.1),
                             vec3(0.2, 0.4, 0.2), 0.1),
                   31.9));
    res =
        opU(res, vec2(sdCylinder(pos - vec3(1.0, 0.25, -2.0), vec2(0.15, 0.25)),
                      8.0));
    res = opU(res, vec2(sdHexPrism(pos - vec3(1.0, 0.2, -3.0), vec2(0.2, 0.05)),
                        18.4));
  }
  if (sdBox(pos - vec3(-1.0, 0.35, -1.0), vec3(0.35, 0.35, 2.5)) < res.x) {
    res = opU(res, vec2(sdPyramid(pos - vec3(-1.0, -0.6, -3.0), 1.0), 13.56));
    res =
        opU(res, vec2(sdOctahedron(pos - vec3(-1.0, 0.15, -2.0), 0.35), 23.56));
    res =
        opU(res, vec2(sdTriPrism(pos - vec3(-1.0, 0.15, -1.0), vec2(0.3, 0.05)),
                      43.5));
    res = opU(res, vec2(sdEllipsoid(pos - vec3(-1.0, 0.25, 0.0),
                                    vec3(0.2, 0.25, 0.05)),
                        43.17));
    res = opU(res, vec2(sdHorseshoe(pos - vec3(-1.0, 0.25, 1.0),
                                    vec2(cos(1.3), sin(1.3)), 0.2, 0.3,
                                    vec2(0.03, 0.08)),
                        11.5));
  }
  if (sdBox(pos - vec3(2.0, 0.3, -1.0), vec3(0.35, 0.3, 2.5)) < res.x) {
    res = opU(
        res, vec2(sdOctogonPrism(pos - vec3(2.0, 0.2, -3.0), 0.2, 0.05), 51.8));
    res = opU(res,
              vec2(sdCylinder(pos - vec3(2.0, 0.14, -2.0), vec3(0.1, -0.1, 0.0),
                              vec3(-0.2, 0.35, 0.1), 0.08),
                   31.2));
    res = opU(
        res, vec2(sdCappedCone(pos - vec3(2.0, 0.09, -1.0), vec3(0.1, 0.0, 0.0),
                               vec3(-0.2, 0.40, 0.1), 0.15, 0.05),
                  46.1));
    res = opU(res,
              vec2(sdRoundCone(pos - vec3(2.0, 0.15, 0.0), vec3(0.1, 0.0, 0.0),
                               vec3(-0.1, 0.35, 0.1), 0.15, 0.05),
                   51.7));
    res = opU(res, vec2(sdRoundCone(pos - vec3(2.0, 0.20, 1.0), 0.2, 0.1, 0.3),
                        37.0));
  }
  return res;
}

// --- 渲染核心函数 ---

// 计算射线与一个AABB包围盒的相交距离
vec2 iBox(in vec3 ro, in vec3 rd, in vec3 rad) {
  vec3 m = 1.0 / rd;
  vec3 n = m * ro;
  vec3 k = abs(m) * rad;
  vec3 t1 = -n - k;
  vec3 t2 = -n + k;
  // 返回相交的最近距离和最远距离
  return vec2(max(max(t1.x, t1.y), t1.z), min(min(t2.x, t2.y), t2.z));
}

// Raycast (光线步进) 函数
// 沿着射线 ro-rd 前进，寻找与场景的交点
// 返回 (交点距离t, 材质ID)
vec2 raycast(in vec3 ro, in vec3 rd) {
  vec2 res = vec2(-1.0, -1.0); // 默认未命中
  float tmin = 1.0; // 最近的渲染距离
  float tmax = 20.0; // 最远的渲染距离

  // 首先检查与地平面的交点
  float tp1 = (0.0 - ro.y) / rd.y;
  if (tp1 > 0.0) {
    tmax = min(tmax, tp1);
    res = vec2(tp1, 1.0); // 材质ID为1.0代表地面
  }
  
  // 检查与场景物体的总包围盒的交点，这是一个优化
  // 只有当射线穿过这个大盒子时，才进行详细的步进计算
  vec2 tb = iBox(ro - vec3(0.0, 0.4, -0.5), rd, vec3(2.5, 0.41, 3.0));
  if (tb.x < tb.y && tb.y > 0.0 && tb.x < tmax) {
    tmin = max(tb.x, tmin); // 更新步进的起始距离
    tmax = min(tb.y, tmax); // 更新步进的结束距离
    
    // 光线步进主循环
    float t = tmin;
    for (int i = 0; i < 70 && t < tmax; i++) {
      // 在当前位置调用map函数，获取到场景的最近距离h
      vec2 h = map(ro + rd * t);
      // 如果距离h小到一个阈值，就认为射线击中了表面
      if (abs(h.x) < (0.0001 * t)) {
        res = vec2(t, h.y); // 记录距离t和材质ID
        break;
      }
      // Sphere Tracing: 沿着射线前进h.x的距离
      // 这是安全的，因为SDF保证了h.x内没有物体
      t += h.x;
    }
  }
  return res;
}

// 计算软阴影
// 从物体表面`ro`向光源`rd`发射一条射线，检查途中是否有遮挡
float calcSoftshadow(in vec3 ro, in vec3 rd, in float mint, in float tmax) {
  // ... 此处也检查了与地面的交点作为优化 ...
  float tp = (0.8 - ro.y) / rd.y;
  if (tp > 0.0)
    tmax = min(tmax, tp);

  float res = 1.0; // 1.0代表完全照亮，0.0代表全黑
  float t = mint;
  // 步进循环，但步长较小，检查遮挡
  for (int i = ZERO; i < 24; i++) {
    float h = map(ro + rd * t).x;
    // 使用一个公式根据距离h来计算阴影的柔和程度
    float s = clamp(8.0 * h / t, 0.0, 1.0);
    res = min(res, s); // 取最暗的阴影值
    t += clamp(h, 0.01, 0.2); // 限制步长
    if (res < 0.004 || t > tmax)
      break;
  }
  res = clamp(res, 0.0, 1.0);
  // 使用一个平滑函数让阴影边缘更自然
  return res * res * (3.0 - 2.0 * res);
}

// 计算法线向量
// 法线是SDF的梯度，可以通过在pos周围极小的邻域内采样4次map函数来估算
vec3 calcNormal(in vec3 pos) {
  vec3 n = vec3(0.0);
  for (int i = ZERO; i < 4; i++) {
    vec3 e = 0.5773 *
             (2.0 * vec3((((i + 3) >> 1) & 1), ((i >> 1) & 1), (i & 1)) - 1.0);
    n += e * map(pos + 0.0005 * e).x;
  }
  return normalize(n);
}

// 计算环境光遮蔽 (Ambient Occlusion)
// 通过在法线方向上进行几次短距离步进，检查周围是否有物体遮挡，来模拟角落更暗的效果
float calcAO(in vec3 pos, in vec3 nor) {
  float occ = 0.0;
  float sca = 1.0;
  for (int i = ZERO; i < 5; i++) {
    float h = 0.01 + 0.12 * float(i) / 4.0; // 步进距离越来越长
    float d = map(pos + h * nor).x; // 获取该点的距离
    occ += (h - d) * sca;
    sca *= 0.95;
    if (occ > 0.35)
      break;
  }
  return clamp(1.0 - 3.0 * occ, 0.0, 1.0) * (0.5 + 0.5 * nor.y);
}

// 带有梯度的程序化棋盘格纹理
// 用于地面，dpdx和dpdy是像素在世界空间的偏导数，用于抗锯齿
float checkersGradBox(in vec2 p, in vec2 dpdx, in vec2 dpdy) {
  vec2 w = abs(dpdx) + abs(dpdy) + 0.001;
  vec2 i = 2.0 *
           (abs(fract((p - 0.5 * w) * 0.5) - 0.5) -
            abs(fract((p + 0.5 * w) * 0.5) - 0.5)) /
           w;
  return 0.5 - 0.5 * i.x * i.y;
}

// 设置相机矩阵
// 输入相机位置ro，目标位置ta，和相机倾斜角度cr
// 输出一个3x3矩阵，可以将相机空间的向量转换到世界空间
mat3 setCamera(in vec3 ro, in vec3 ta, float cr) {
  vec3 cw = normalize(ta - ro); // Z轴：前向向量
  vec3 cp = vec3(sin(cr), cos(cr), 0.0); // 辅助向量，用于定义“上”方向
  vec3 cu = normalize(cross(cw, cp)); // X轴：右向向量
  vec3 cv = (cross(cu, cw)); // Y轴：上向向量
  return mat3(cu, cv, cw); // 返回这个坐标系的变换矩阵
}

// 主渲染函数
// 根据射线与场景的交点信息，计算光照、阴影、AO等，最终得到像素颜色
vec3 render(in vec3 ro, in vec3 rd, in vec3 rdx, in vec3 rdy) {
  // 背景色（天空）
  vec3 col = vec3(0.7, 0.7, 0.9) - max(rd.y, 0.0) * 0.3;
  // 执行光线步进
  vec2 res = raycast(ro, rd);
  float t = res.x; // 交点距离
  float m = res.y; // 材质ID
  
  // 如果 m > -0.5，说明射线击中了物体
  if (m > -0.5) {
    vec3 pos = ro + t * rd; // 计算世界空间交点坐标
    // 如果材质ID<1.5是地面，否则是其他物体，需要计算法线
    vec3 nor = (m < 1.5) ? vec3(0.0, 1.0, 0.0) : calcNormal(pos);
    vec3 ref = reflect(rd, nor); // 反射向量
    
    // 根据材质ID赋予基础颜色
    col = 0.2 + 0.2 * sin(m * 2.0 + vec3(0.0, 1.0, 2.0));
    float ks = 1.0; // 高光系数
    
    // 如果是地面，应用棋盘格纹理
    if (m < 1.5) {
      vec3 dpdx = ro.y * (rd / rd.y - rdx / rdx.y);
      vec3 dpdy = ro.y * (rd / rd.y - rdy / rdy.y);
      float f = checkersGradBox(3.0 * pos.xz, 3.0 * dpdx.xz, 3.0 * dpdy.xz);
      col = 0.15 + f * vec3(0.05);
      ks = 0.4;
    }
    
    // 计算AO
    float occ = calcAO(pos, nor);
    
    // 光照计算
    vec3 lin = vec3(0.0); // 初始化光照累加器
    float l1 = (enableLights.x != 0) ? 1.0 : 0.0;
    float l2 = (enableLights.y != 0) ? 1.0 : 0.0;
    float l3 = (enableLights.z != 0) ? 1.0 : 0.0;
    float l4 = (enableLights.w != 0) ? 1.0 : 0.0;
    
    // 光源1: 主光源 (像太阳)
    if (l1 > 0.0) {
      vec3 lig = normalize(vec3(-0.5, 0.4, -0.6)); // 光源方向
      vec3 hal = normalize(lig - rd); // 半程向量
      float dif = clamp(dot(nor, lig), 0.0, 1.0); // 漫反射
      dif *= calcSoftshadow(pos, lig, 0.02, 2.5); // 乘以软阴影
      float spe = pow(clamp(dot(nor, hal), 0.0, 1.0), 16.0); // 镜面反射 (高光)
      spe *= dif;
      spe *= 0.04 + 0.96 * pow(clamp(1.0 - dot(hal, lig), 0.0, 1.0), 5.0);
      lin += l1 * (col * 2.20 * dif * vec3(1.30, 1.00, 0.70)); // 累加漫反射光
      lin += l1 * (5.00 * spe * vec3(1.30, 1.00, 0.70) * ks);   // 累加高光
    }
    
    // 光源2: 天空光/环境光
    if (l2 > 0.0) {
      float dif = sqrt(clamp(0.5 + 0.5 * nor.y, 0.0, 1.0));
      dif *= occ;
      float spe = smoothstep(-0.2, 0.2, ref.y);
      spe *= dif;
      spe *= 0.04 + 0.96 * pow(clamp(1.0 + dot(nor, rd), 0.0, 1.0), 5.0);
      spe *= calcSoftshadow(pos, ref, 0.02, 2.5);
      lin += l2 * (col * 0.60 * dif * vec3(0.40, 0.60, 1.15));
      lin += l2 * (2.00 * spe * vec3(0.40, 0.60, 1.30) * ks);
    }
    
    // 光源3: 另一个补光
    if (l3 > 0.0) {
      float dif = clamp(dot(nor, normalize(vec3(0.5, 0.0, 0.6))), 0.0, 1.0) *
                  clamp(1.0 - pos.y, 0.0, 1.0);
      dif *= occ;
      lin += l3 * (col * 0.55 * dif * vec3(0.25, 0.25, 0.25));
    }
    
    // 光源4: 边缘光/菲涅尔效应
    if (l4 > 0.0) {
      float dif = pow(clamp(1.0 + dot(nor, rd), 0.0, 1.0), 2.0);
      dif *= occ;
      lin += l4 * (col * 0.25 * dif * vec3(1.00, 1.00, 1.00));
    }
    
    col = lin; // 最终颜色是所有光照的总和
    
    // 模拟雾效：根据距离t混合背景色
    col = mix(col, vec3(0.7, 0.7, 0.9), 1.0 - exp(-0.0001 * t * t * t));
  }
  
  // 返回最终颜色，限制在[0,1]范围
  return vec3(clamp(col, 0.0, 1.0));
}

// 着色器主函数，每个像素执行一次
void main() {
  vec2 fragCoord = gl_FragCoord.xy;
  fragCoord.y = iResolution.y - fragCoord.y; 
  
  // 鼠标位置归一化（但不影响相机）
  vec2 mo = iMouse.xy / max(iResolution.xy, vec2(1.0));
  // 时间，用于动画
  float time = 32.0 + iTime * 1.5;

  // --- 摄像机设置 ---
  // 目标点 (Look-at Target)
  vec3 ta = vec3(0.25, -0.75, -0.75); 
  // 相机位置 (Ray Origin)，围绕目标点ta做圆周运动（不随鼠标变化）
  vec3 ro = ta + vec3(4.5 * cos(0.1 * time), 2.2,
                      4.5 * sin(0.1 * time));
  
  // 计算相机坐标系矩阵
  mat3 ca = setCamera(ro, ta, 0.0);

  vec3 tot = vec3(0.0); // 用于抗锯齿的颜色累加器

  // 如果定义了抗锯齿(AA>1)，则进行超级采样
  #if AA > 1
  for (int m = ZERO; m < AA; m++)
    for (int n = ZERO; n < AA; n++) {
      // 在一个像素内进行子像素偏移
      vec2 o = vec2(float(m), float(n)) / float(AA) - 0.5;
      // 将带有偏移的像素坐标归一化到NDC(Normalized Device Coordinates)
      vec2 p = (2.0 * (fragCoord + o) - iResolution.xy) / iResolution.y;
  #else
    // --- 从屏幕2D坐标生成3D射线 ---
    // 1. 将屏幕像素坐标`fragCoord` (如 [0, 1920]) 转换到归一化的屏幕空间`p` (范围大致为[-aspect, aspect] x [-1, 1])
    // 这是连接2D屏幕和3D世界的桥梁
    vec2 p = (2.0 * fragCoord - iResolution.xy) / iResolution.y;
  #endif
      // 2. 定义虚拟屏幕到摄像机的距离（焦距），控制FOV
      const float fl = 2.5;
      
      // 3. 构建射线方向
      //    - vec3(p, fl): 在相机坐标系下，从原点指向虚拟屏幕上(p.x, p.y, -fl)的点。
      //    - normalize(...): 将其归一化为单位向量。
      //    - ca * ...: 使用相机矩阵，将这个方向从相机空间变换到世界空间。
      // 这就是最终得到的、从相机出发穿过当前像素的3D射线方向`rd`
      vec3 rd = ca * normalize(vec3(p, fl));

      // 计算相邻像素的射线方向，用于计算偏导数，给地面纹理做抗锯齿
      vec2 px = (2.0 * (fragCoord + vec2(1.0, 0.0)) - iResolution.xy) / iResolution.y;
      vec2 py = (2.0 * (fragCoord + vec2(0.0, 1.0)) - iResolution.xy) / iResolution.y;
      vec3 rdx = ca * normalize(vec3(px, fl));
      vec3 rdy = ca * normalize(vec3(py, fl));
      
      // 调用渲染函数，传入相机位置(ro)和射线方向(rd)，得到颜色
      vec3 col = render(ro, rd, rdx, rdy);
      
      // 进行伽马校正
      col = pow(col, vec3(0.4545));
      
      // 累加颜色
      tot += col;
  #if AA > 1
    }
  // 如果开启了抗锯齿，则取平均值
  tot /= float(AA * AA);
  #endif
  
  // 输出最终颜色
  outColor = vec4(tot, 1.0);
}