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
    vec2 iMouse;        // Mouse position for circle
    vec2 lightPos;      // Light 1 position
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

vec2 rotateCW(vec2 p, float a)
{
    mat2 m = mat2(cos(a), -sin(a), sin(a), cos(a));
    return p * m;
}

//////////////////////////////
// Distance field functions //
//////////////////////////////

float circleDist(vec2 p, float radius)
{
    return length(p) - radius;
}

float boxDist(vec2 p, vec2 size, float radius)
{
    size -= vec2(radius);
    vec2 d = abs(p) - size;
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - radius;
}

vec2 flamePerturbation(vec2 p, vec2 rectPos, vec2 rectSize)
{
    // vec2 relativeP = p - rectPos;
    // float rectDist = boxDist(relativeP, rectSize, 5.0);
    
    // // Only apply perturbation near the edges (distance < 80 for flame spread)
    // float edgeInfluence = smoothstep(80.0, 0.0, rectDist);
    
    // // Calculate height from rectangle bottom for upward flame flow
    // float heightFromBottom = (relativeP.y + rectSize.y) / (rectSize.y * 2.0);
    // heightFromBottom = clamp(heightFromBottom, 0.0, 1.0);
    
    // // Flame flows more upward as height increases
    // float upwardBias = heightFromBottom * 2.0 + 0.5;
    
    // // Fast flickering time for flame dynamics
    // float time = iTime * 2.5;
    // vec2 noiseCoord = p * 0.004;
    
    // vec2 flameDistortion = vec2(0.0);
    
    // // Large flame tongues with strong upward bias
    // float largeFlamex = sin(noiseCoord.x * 3.0 + time * 1.2) * cos(noiseCoord.y * 2.0 + time * 0.8);
    // float largeFlamey = cos(noiseCoord.x * 2.5 + time * 1.5) * sin(noiseCoord.y * 3.5 + time * 1.1) * upwardBias;
    // flameDistortion += vec2(largeFlamex, largeFlamey) * 35.0;
    
    // // Medium flickering flames
    // float medFlamex = sin(noiseCoord.x * 8.0 + time * 2.0) * cos(noiseCoord.y * 6.0 + time * 1.8);
    // float medFlamey = cos(noiseCoord.x * 7.0 + time * 2.3) * sin(noiseCoord.y * 9.0 + time * 2.1) * upwardBias;
    // flameDistortion += vec2(medFlamex, medFlamey) * 20.0;
    
    // // Fine flame detail with rapid flickering
    // float fineFlamex = sin(noiseCoord.x * 20.0 + time * 4.0) * cos(noiseCoord.y * 18.0 + time * 3.5);
    // float fineFlamey = cos(noiseCoord.x * 19.0 + time * 4.2) * sin(noiseCoord.y * 22.0 + time * 4.8) * upwardBias;
    // flameDistortion += vec2(fineFlamex, fineFlamey) * 10.0;
    
    // // Additional vertical stretching for flame effect
    // flameDistortion.y *= 1.5;
    
    // // Apply falloff based on distance from rectangle edge with flame-specific curve
    // float flameFalloff = exp(-rectDist * 0.015) * (1.0 + heightFromBottom * 0.8);
    
    // return p + flameDistortion * edgeInfluence * flameFalloff;
    return p;
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
    float time = iTime * 1.5;  // Speed of animation
    float cycle = 8.0;         // Duration of one complete cycle
    float t = mod(time, cycle) / cycle;  // Normalized time [0,1] for one cycle
    
    // Shape parameters
    float circleRadius = 35.0;
    vec2 rectSize = vec2(80.0, 250.0);
    vec2 centerScreen = iResolution.xy / 2.0;
    
    // Rectangle stays fixed at center of screen
    vec2 rectPos = centerScreen;
    
    // Circle follows mouse position
    vec2 circlePos = iMouse.xy * 2.0;
    
    // Calculate shape distances
    float circle = circleDist(translate(p, circlePos), circleRadius);
    
    // Rectangle with flame perturbation effect on edges
    vec2 perturbedP = flamePerturbation(p, rectPos, rectSize);
    float rectangle = boxDist(translate(perturbedP, rectPos), rectSize, 5.0); // Rounded corners
    
    // Merge the shapes with smooth blending when boundaries get close
    float blendRadius = 200.0;  // Distance between boundaries at which smooth merging starts
    
    // Calculate the distance between the boundaries (not centers)
    // This is the minimum distance between the surfaces of the two shapes
    float boundaryDist = max(circle, rectangle);  // Distance from point to closest boundary
    
    // Alternative approach: calculate actual boundary-to-boundary distance
    // We need to find how close the surfaces are to each other
    float surfaceDistance = circle + rectangle;  // Sum gives approximate boundary separation
    
    if (surfaceDistance < blendRadius) {
        // Use smooth merge when boundaries are close
        float k = 100.0 * (1.0 - surfaceDistance / blendRadius);  // Blend strength increases as boundaries get closer
        return smoothMerge(circle, rectangle, k);
    } else {
        // Use regular merge when boundaries are far apart
        return merge(circle, rectangle);
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
    
    // Beautiful three light setup with warm/cool contrast
    vec2 light1Pos = lightPos.xy;  // Use slider-controlled position
    vec4 light1Col = vec4(1.0, 0.8, 0.3, 1.0);  // Warm golden light
    setLuminance(light1Col, 0.6);
    
    vec2 light2Pos = vec2(iResolution.x * (sin(iTime + 3.1415) + 1.2) / 2.4, 175.0);
    vec4 light2Col = vec4(0.3, 0.7, 1.0, 1.0);  // Cool blue light
    setLuminance(light2Col, 0.7);
    
    vec2 light3Pos = vec2(iResolution.x * (sin(iTime) + 1.2) / 2.4, 340.0);
    vec4 light3Col = vec4(1.0, 0.4, 0.6, 1.0);  // Magenta accent light
    setLuminance(light3Col, 0.5);
    
    // Beautiful dark gradient background
    float gradientFactor = 1.0 - length(c - p) / (iResolution.x * 0.8);
    vec4 col = mix(
        vec4(0.08, 0.12, 0.2, 1.0),   // Dark blue-grey at edges
        vec4(0.15, 0.18, 0.25, 1.0),  // Lighter blue-grey at center
        gradientFactor
    );
    
    // Subtle grid pattern
    float gridSize = 20.0;
    float gridStrength = 0.95;
    col *= clamp(min(
        mod(p.y, gridSize) / gridSize,
        mod(p.x, gridSize) / gridSize
    ), gridStrength, 1.0);
    // ambient occlusion
    col *= AO(p, dist, 40.0, 0.4);
    
    // light (range auto-calculated from radius; 25.0 keeps previous defaults)
    float r2range = 25.0;
    col += lightOn.x * drawLight(p, light1Pos, light1Col, dist, lightRadius.x * r2range, lightRadius.x);
    col += lightOn.y * drawLight(p, light2Pos, light2Col, dist, lightRadius.y * r2range, lightRadius.y);
    col += lightOn.z * drawLight(p, light3Pos, light3Col, dist, lightRadius.z * r2range, lightRadius.z);
    
    // Fill shapes with beautiful gradient colors
    // Create a dynamic color based on position and time for more interest
    float colorPhase = sin(iTime * 0.5) * 0.5 + 0.5;
    vec4 shapeColor = mix(
        vec4(0.2, 0.8, 1.0, 1.0),     // Bright cyan
        vec4(0.8, 0.3, 1.0, 1.0),     // Purple-magenta
        colorPhase
    );
    
    // Add some spatial variation to the color
    float spatialVariation = sin(p.x * 0.01) * sin(p.y * 0.01) * 0.3 + 0.7;
    shapeColor.rgb *= spatialVariation;
    
    col = mix(col, shapeColor, fillMask(dist));
    
    // Add elegant shape outline with subtle glow
    vec4 outlineColor = vec4(0.9, 0.9, 1.0, 1.0) * 0.8;  // Soft white-blue outline
    col = mix(col, outlineColor, innerBorderMask(dist, 2.0));

    outColor = clamp(col, 0.0, 1.0);
}