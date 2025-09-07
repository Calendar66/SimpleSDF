#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outPosition; // world position
layout(location = 1) out vec4 outNormal;   // world normal (xyz)
layout(location = 2) out vec4 outFlux;     // flux (rgb)

layout(std140, binding = 0) uniform ShaderToyUBO {
  float iTime;
  vec2  iResolution;
  vec2  iMouse;
  int   iFrame;
  ivec4 enableLights;
  vec4  lightDir;           // xyz dir (from scene to light), w = intensity
  vec4  lightRight;         // xyz basis
  vec4  lightUp;            // xyz basis
  vec4  lightOrigin;        // origin of light camera
  vec4  lightOrthoHalfSize; // xy half extent
  vec4  rsmResolution;      // xy
  vec4  rsmParams;          // not used here
};

// SDF helpers (trimmed)
float sdPlane(vec3 p) { return p.y; }
float sdSphere(vec3 p, float s) { return length(p) - s; }
float sdBox(vec3 p, vec3 b) { vec3 d = abs(p) - b; return min(max(d.x, max(d.y, d.z)), 0.0) + length(max(d, 0.0)); }

vec2 opU(vec2 d1, vec2 d2) { return (d1.x < d2.x) ? d1 : d2; }

// Scene map (copy the same as in sdf3d.frag, trimmed to main pieces)
vec2 map(in vec3 pos) {
  vec2 res = vec2(pos.y, 0.0);
  if (sdBox(pos - vec3(-2.0, 0.3, 0.25), vec3(0.3, 0.3, 1.0)) < res.x) {
    res = opU(res, vec2(sdSphere(pos - vec3(-2.0, 0.25, 0.0), 0.25), 26.9));
  }
  if (sdBox(pos - vec3(0.0, 0.3, -1.0), vec3(0.35, 0.3, 2.5)) < res.x) {
    res = opU(res, vec2(sdBox(pos - vec3(0.0, 0.25, 0.0), vec3(0.3, 0.25, 0.2)), 16.9));
  }
  if (sdBox(pos - vec3(1.0, 0.3, -1.0), vec3(0.35, 0.3, 2.5)) < res.x) {
    res = opU(res, vec2(sdSphere(pos - vec3(1.0, 0.25, 0.0), 0.25), 7.1));
  }
  return res;
}

vec2 iBox(in vec3 ro, in vec3 rd, in vec3 rad) {
  vec3 m = 1.0 / rd;
  vec3 n = m * ro;
  vec3 k = abs(m) * rad;
  vec3 t1 = -n - k;
  vec3 t2 = -n + k;
  return vec2(max(max(t1.x, t1.y), t1.z), min(min(t2.x, t2.y), t2.z));
}

vec2 raycast(in vec3 ro, in vec3 rd) {
  vec2 res = vec2(-1.0, -1.0);
  float tmin = 0.0;
  float tmax = 30.0;
  float tp1 = (0.0 - ro.y) / rd.y;
  if (tp1 > 0.0) { tmax = min(tmax, tp1); res = vec2(tp1, 1.0); }
  vec2 tb = iBox(ro - vec3(0.0, 0.4, -0.5), rd, vec3(2.5, 0.41, 3.0));
  if (tb.x < tb.y && tb.y > 0.0 && tb.x < tmax) {
    tmin = max(tb.x, tmin);
    tmax = min(tb.y, tmax);
    float t = tmin;
    for (int i = 0; i < 64 && t < tmax; i++) {
      vec2 h = map(ro + rd * t);
      if (abs(h.x) < (0.0005 * t)) { res = vec2(t, h.y); break; }
      t += h.x;
    }
  }
  return res;
}

vec3 calcNormal(in vec3 pos) {
  vec3 n = vec3(0.0);
  for (int i = 0; i < 4; i++) {
    vec3 e = 0.5773 * (2.0 * vec3(((i+3)>>1)&1, (i>>1)&1, i&1) - 1.0);
    n += e * map(pos + 0.0005 * e).x;
  }
  return normalize(n);
}

void main() {
  // Build orthographic light ray for each pixel
  vec2 uv = fragTexCoord * 2.0 - 1.0;
  uv *= lightOrthoHalfSize.xy / lightOrthoHalfSize.yy; // keep aspect simple
  vec3 ro = lightOrigin.xyz + lightRight.xyz * uv.x + lightUp.xyz * uv.y;
  vec3 rd = normalize(lightDir.xyz);

  vec2 hit = raycast(ro, rd);
  if (hit.x < 0.0) {
    outPosition = vec4(0.0);
    outNormal = vec4(0.0);
    outFlux = vec4(0.0);
    return;
  }
  vec3 pos = ro + rd * hit.x;
  vec3 nor = (hit.y < 1.5) ? vec3(0,1,0) : calcNormal(pos);

  float nDotL = max(dot(nor, rd), 0.0);
  vec3 flux = vec3(1.3, 1.0, 0.7) * lightDir.w * nDotL;

  outPosition = vec4(pos, 1.0);
  outNormal = vec4(nor, 0.0);
  outFlux = vec4(flux, 1.0);
}


