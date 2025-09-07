#version 450

// Input from vertex shader
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

// Output
layout(location = 0) out vec4 outColor;

// Uniform buffer for ShaderToy-style uniforms
layout(std140, binding = 0) uniform ShaderToyUBO {
    float iTime;
    vec2 iResolution;
    vec2 iMouse;
    vec4 lightOn;
    vec4 lightRadius;
};

//////////////////////////////////////
// Combine distance field functions //
//////////////////////////////////////

float smoothMerge(float d1, float d2, float k)
{
    float h = clamp(0.5 + 0.5*(d2 - d1)/k, 0.0, 1.0);
    return mix(d2, d1, h) - k * h * (1.0-h);
}

float merge(float d1, float d2)
{
    return min(d1, d2);
}

//////////////////////////////
// Rotation and translation //
//////////////////////////////

vec2 translate(vec2 p, vec2 t)
{
    return p - t;
}

//////////////////////////////
// Distance field functions //
//////////////////////////////

float circleDist(vec2 p, float radius)
{
    return length(p) - radius;
}

///////////////////////
// Masks for drawing //
///////////////////////

float fillMask(float dist)
{
    return clamp(-dist, 0.0, 1.0);
}

float innerBorderMask(float dist, float width)
{
    float alpha1 = clamp(dist + width, 0.0, 1.0);
    float alpha2 = clamp(dist, 0.0, 1.0);
    return alpha1 - alpha2;
}

///////////////
// The scene //
///////////////

float sceneDist(vec2 p)
{
    // Animation parameters
    float time = iTime * 2.0;  // Speed of animation
    float cycle = 6.0;         // Duration of one complete cycle
    float t = mod(time, cycle) / cycle;  // Normalized time [0,1] for one cycle
    
    // Sphere parameters
    float sphereRadius = 30.0;
    vec2 centerScreen = iResolution.xy / 2.0;
    
    // Calculate positions for the two spheres
    // They start far apart and move toward each other
    float maxSeparation = 200.0;
    float currentSeparation;
    
    if (t < 0.7) {
        // Approaching phase (70% of cycle)
        float approachT = t / 0.7;
        currentSeparation = maxSeparation * (1.0 - smoothstep(0.0, 1.0, approachT));
    } else {
        // Separating phase (30% of cycle) - quick reset
        float separateT = (t - 0.7) / 0.3;
        currentSeparation = maxSeparation * smoothstep(0.0, 1.0, separateT);
    }
    
    vec2 sphere1Pos = centerScreen + vec2(-currentSeparation * 0.5, 0.0);
    vec2 sphere2Pos = centerScreen + vec2(currentSeparation * 0.5, 0.0);
    
    // Calculate sphere distances
    float sphere1 = circleDist(translate(p, sphere1Pos), sphereRadius);
    float sphere2 = circleDist(translate(p, sphere2Pos), sphereRadius);
    
    // Merge the spheres with smooth blending when they get close
    float blendRadius = 60.0;  // Radius at which smooth merging starts
    float dist = currentSeparation;
    
    if (dist < blendRadius) {
        // Use smooth merge when spheres are close
        float k = 20.0 * (1.0 - dist / blendRadius);  // Blend strength increases as they get closer
        return smoothMerge(sphere1, sphere2, k);
    } else {
        // Use regular merge when spheres are far apart
        return merge(sphere1, sphere2);
    }
}

//////////////////////
// Shadow and light //
//////////////////////

float shadow(vec2 p, vec2 pos, float radius)
{
    vec2 dir = normalize(pos - p);
    float dl = length(p - pos);
    
    // fraction of light visible, starts at one radius (second half added in the end);
    float lf = radius * dl;
    
    // distance traveled
    float dt = 0.01;

    for (int i = 0; i < 64; ++i)
    {			
        // distance to scene at current position
        float sd = sceneDist(p + dir * dt);

        // early out when this ray is guaranteed to be full shadow
        if (sd < -radius) 
            return 0.0;
        
        // width of cone-overlap at light
        // 0 in center, so 50% overlap: add one radius outside of loop to get total coverage
        // should be '(sd / dt) * dl', but '*dl' outside of loop
        lf = min(lf, sd / dt);
        
        // move ahead
        dt += max(1.0, abs(sd));
        if (dt > dl) break;
    }

    // multiply by dl to get the real projected overlap (moved out of loop)
    // add one radius, before between -radius and + radius
    // normalize to 1 ( / 2*radius)
    lf = clamp((lf*dl + radius) / (2.0 * radius), 0.0, 1.0);
    lf = smoothstep(0.0, 1.0, lf);
    return lf;
}

vec4 drawLight(vec2 p, vec2 pos, vec4 color, float dist, float range, float radius)
{
    // distance to light
    float ld = length(p - pos);
    
    // out of range
    if (ld > range) return vec4(0.0);
    
    // shadow and falloff
    float shad = shadow(p, pos, radius);
    float fall = (range - ld)/range;
    fall *= fall;
    float source = fillMask(circleDist(p - pos, radius));
    return (shad * fall + source) * color;
}

float luminance(vec4 col)
{
    return 0.2126 * col.r + 0.7152 * col.g + 0.0722 * col.b;
}

void setLuminance(inout vec4 col, float lum)
{
    lum /= luminance(col);
    col *= lum;
}

float AO(vec2 p, float dist, float radius, float intensity)
{
    float a = clamp(dist / radius, 0.0, 1.0) - 1.0;
    return 1.0 - (pow(abs(a), 5.0) + 1.0) * intensity + (1.0 - intensity);
}

/////////////////
// The program //
/////////////////

void main() 
{
    // Convert from Vulkan fragment coordinates to ShaderToy coordinates
    vec2 fragCoord = gl_FragCoord.xy;
    vec2 p = fragCoord;
    vec2 c = iResolution.xy / 2.0;
    
    float dist = sceneDist(p);
    
    // Three light setup same as sdf2d.frag
    vec2 light1Pos = iMouse.xy;
    vec4 light1Col = vec4(0.75, 1.0, 0.5, 1.0);
    setLuminance(light1Col, 0.4);
    
    vec2 light2Pos = vec2(iResolution.x * (sin(iTime + 3.1415) + 1.2) / 2.4, 175.0);
    vec4 light2Col = vec4(1.0, 0.75, 0.5, 1.0);
    setLuminance(light2Col, 0.5);
    
    vec2 light3Pos = vec2(iResolution.x * (sin(iTime) + 1.2) / 2.4, 340.0);
    vec4 light3Col = vec4(0.5, 0.75, 1.0, 1.0);
    setLuminance(light3Col, 0.6);
    
    // gradient
    vec4 col = vec4(0.5, 0.5, 0.5, 1.0) * (1.0 - length(c - p)/iResolution.x);
    // grid
    col *= clamp(min(mod(p.y, 10.0), mod(p.x, 10.0)), 0.9, 1.0);
    // ambient occlusion
    col *= AO(p, dist, 40.0, 0.4);
    
    // light (range auto-calculated from radius; 25.0 keeps previous defaults)
    float r2range = 25.0;
    col += lightOn.x * drawLight(p, light1Pos, light1Col, dist, lightRadius.x * r2range, lightRadius.x);
    col += lightOn.y * drawLight(p, light2Pos, light2Col, dist, lightRadius.y * r2range, lightRadius.y);
    col += lightOn.z * drawLight(p, light3Pos, light3Col, dist, lightRadius.z * r2range, lightRadius.z);
    
    // Fill the spheres with orange color
    col = mix(col, vec4(1.0, 0.6, 0.2, 1.0), fillMask(dist));
    
    // Add shape outline
    col = mix(col, vec4(0.1, 0.1, 0.1, 1.0), innerBorderMask(dist, 1.5));

    outColor = clamp(col, 0.0, 1.0);
}