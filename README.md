# SimpleSDF - Advanced Signed Distance Field Rendering Framework

A comprehensive Vulkan-based SDF (Signed Distance Field) rendering framework showcasing advanced computer graphics techniques including soft shadows, ray marching, and physically-based rendering with global illumination.

## 🎯 Features

This project demonstrates three distinct SDF rendering approaches:

### 🟦 2D SDF Scene
- **Soft shadows** with multiple interactive light sources
- **Blending functions**: merge, subtract, intersect, smooth merge
- **Geometric primitives**: circles, boxes, triangles, lines, semi-circles
- **Real-time interaction** via mouse controls
- **Ambient occlusion** and gradient backgrounds

### 🟨 3D SDF Scene  
- The 3D scene is entirely based on Inigo Quilez’s ShaderToy work; this project simply reimplements it using Vulkan.
- **ShaderToy-compatible** structure for easy experimentation
- **Multiple configurable light sources** with real-time toggles
- **Advanced 3D SDF compositions** and transformations

### 🟩 Cornell Box Scene (Default)
- **RSM (Reflective Shadow Maps)** for realistic indirect lighting
- **Three-stage importance sampling** during VPL (Virtual Point Light) sampling
- **Optional PBR (Physically Based Rendering)** with material controls
- **Advanced lighting models** with comprehensive real-time controls
- **Debug visualization modes** for RSM analysis

### 🟪 SDF Mesh (Mesh → 3D Volume SDF)
- **Arbitrary mesh to SDF** baked into a 3D texture asset (`.sdf`)
- **Efficient ray marching** by sampling `sampler3D` for signed distance
- **Non-uniform scaling** through a bounding box (`u.meshHalfSize`)
- **Safe step scaling** using CPU-computed `scaleMin`
- **Early-exit optimization** using box SDF to skip texture fetches

## 🎥 Demonstrations

### 2D SDF Scene
![2D Scene Demo](https://github.com/Calendar66/SimpleSDF/raw/main/video/2D.gif)

*Interactive 2D signed distance fields with soft shadows, multiple blending operations, and real-time mouse-controlled lighting.*

### 3D SDF Scene  
![3D Scene Demo](https://github.com/Calendar66/SimpleSDF/raw/main/video/3D.gif)

*Ray-marched 3D signed distance fields with advanced lighting and material effects.The 3D scene is entirely from Inigo Quilez’s ShaderToy work; this project reimplements it with Vulkan.*

### Cornell Box Scene
![Cornell Scene Demo](https://github.com/Calendar66/SimpleSDF/raw/main/video/Cornell.gif)

*Advanced Cornell box implementation featuring RSM-based global illumination, importance sampling, and optional PBR materials.*

### SDF Mesh Scene
![SDF Mesh Demo](https://github.com/Calendar66/SimpleSDF/raw/main/video/Mesh.gif)

*Arbitrary mesh baked to a 3D SDF texture and ray-marched with non-uniform scaling and early-exit optimization.*

## 🚀 Quick Start

### Prerequisites

- **CMake 3.16.5+**
- **C++20 compatible compiler**
- **Vulkan SDK** with `glslangValidator`
- **Platform dependencies**: GLFW, VMA, ImGui (provided via EasyVulkan)

### Building

#### Standard Build (All Scenes)
```bash
# Create build directory and compile
mkdir -p build && cd build
cmake ..
make

# Run the application
./SDF
```

#### Xcode Build (macOS)
```bash
cd buildXCodes
xcodebuild -project SimpleSDF.xcodeproj -scheme SDF -configuration Debug

# Run from build output
cd Debug
./SDF
```

### Windows Build
EasyVulkan has good cross-platform characteristics.

## 🎮 Usage

### Scene Selection
The active scene is controlled by the `APPIMPLEMENTATION` preprocessor macro in `src/main.cpp`:

```cpp
#define APPIMPLEMENTATION 1  // SDF2D - 2D demonstrations
#define APPIMPLEMENTATION 2  // SDF3D - 3D ray marching  
#define APPIMPLEMENTATION 3  // SDFCornell - Cornell box
#define APPIMPLEMENTATION 4  // SDFMesh - Mesh SDF (default)
```

Default is 4 (SDFMesh) in the current codebase.

### Controls

#### 2D Scene Controls
- **Mouse**: Interactive light source positioning
- **ImGui Panel**: 
  - Toggle individual lights on/off
  - Adjust light radii and intensities
  - Real-time parameter modifications

#### 3D Scene Controls  
- **Mouse**: Camera orientation control
- **ImGui Panel**:
  - Enable/disable individual light sources
  - Adjust rendering parameters
  - Animation controls

#### Cornell Box Controls
- **Mouse**: Scene navigation and interaction
- **Comprehensive ImGui Interface**:
  - **Lighting**: Key, fill, rim, and environment light controls
  - **Materials**: PBR parameters, roughness, metallic values
  - **RSM Settings**: Resolution, sampling parameters, indirect lighting
  - **Debug Modes**: RSM visualization, shadow analysis
  - **Animation**: Sphere rotation and movement controls

#### SDF Mesh Controls
- **Transform**:
  - Euler rotation sliders, virtual joystick, animation speed, reset buttons
  - Arcball rotation widget with roll control and degree readouts
  - Box size sliders `L/W/H` (non-uniform scaling) with Reset Size
  - Box color picker
- **SDF Asset**:
  - SDF file selector (combo box) and runtime reload
  - HUD: `Dimensions (W×H×D)` and `Cell Size`
- **Lighting & RSM**:
  - Light toggles: Key/Fill/Rim/Env; Key intensity
  - Enable RSM, indirect lighting, importance sampling, indirect intensity
  - RSM resolution selector; Show RSM Only / Show Indirect Only
  - Interactive main light direction control with Reset Light
  - Ambient, shadow quality/intensity, metallic, blue tint
- **PBR**:
  - Enable PBR; global roughness/metallic; base color intensity
  - Per-object roughness/metallic; Reset PBR Settings
- **Light Projection**:
  - Light ortho half-size sliders with reset

## 🏗️ Technical Architecture

### Core Framework
- **EasyVulkan Wrapper**: Simplified Vulkan API with builder patterns
- **Resource Management**: Automatic cleanup and memory management via VMA
- **Cross-Platform Support**: Windows, macOS, Linux, with mobile (OHOS) compatibility

### Rendering Pipeline
- **Automatic Shader Compilation**: GLSL to SPIR-V via `glslangValidator`
- **Uniform Buffer Management**: ShaderToy-compatible parameter structures  
- **Command Buffer Optimization**: Efficient GPU command recording
- **ImGui Integration**: Real-time parameter adjustment and debugging

### SDF Mesh Pipeline
- **Asset format**: `.sdf` files contain resolution `(W H D)`, origin `(x y z)`, voxel size, and per-voxel signed distances (neg: inside, pos: outside).
- **CPU loading**: Parse metadata and distance values; create/upload a 3D texture for sampling.
- **Uniforms/UBO**:
  - `u.meshHalfSize.xyz`: non-uniform geometry scale via bounding box half-lengths
  - `u.sdfParams0.x`: `scaleMin` (conservative distance scale for safe sphere tracing)
  - `u.sdfParams0.y`: `hasTex` flag
- **Shader sampling**:
  - World → model: rotate using the inverse (transpose) of the world rotation
  - Bounding box SDF: `sdBox(local, halfSize)` to constrain the shape
  - UVW mapping: `local / halfSize → [-1,1] → [0,1]` with clamp
  - Combine: `max(texture3D * scaleMin, boxSdf)`
- **Optimization**: Early exit when `boxSdf > 0` to skip unnecessary 3D texture fetches.
- **Integration**: Works with the existing RSM light pass and optional PBR shading.

### Advanced Features
- **RSM Implementation**: Multi-pass rendering for global illumination
- **Importance Sampling**: Three-stage VPL sampling with adaptive weighting
- **PBR Materials**: Physically-based shading with metallic-roughness workflow
- **Soft Shadows**: Distance field-based shadow computation
- **Ray Marching**: Efficient SDF traversal and lighting

## 📁 Project Structure

```
SimpleSDF/
├── src/                    # Application source code
│   ├── main.cpp           # Entry point with scene selection
│   ├── SDF2D.cpp          # 2D SDF implementation
│   ├── SDF3D.cpp          # 3D ray marching implementation
│   ├── SDFCornell.cpp     # Cornell box with RSM
│   └── SDFMesh.cpp        # Mesh SDF scene implementation
├── include/               # Header files
│   └── SDFMesh.hpp        # Mesh SDF scene interface
├── shaders/               # GLSL shader sources
│   ├── sdf2d.frag        # 2D SDF fragment shader
│   ├── sdf3d.frag        # 3D ray marching shader
│   ├── sdf_practice.frag # Cornell box main shader
│   ├── rsm_light.frag    # RSM light pass shader
│   ├── sdf_mesh_main.frag# Mesh SDF main shader
│   └── sdf_mesh_rsm.frag # Mesh SDF pass for RSM
├── assets/               # Texture resources
│   └── sdf/              # Prebaked SDF volumes (.sdf)
├── video/                # Demonstration videos
├── thirdParty/           # External dependencies
│   └── EasyVulkan/       # Vulkan wrapper library
└── build/                # Build output directory
```

## 🎨 SDF Techniques Demonstrated

### Distance Field Operations
- **Primitive SDFs**: Spheres, boxes, planes, torus
- **Boolean Operations**: Union, intersection, subtraction
- **Smooth Blending**: Smooth min/max operations
- **Domain Repetition**: Infinite geometric patterns

### Advanced Rendering
- **Soft Shadows**: Distance-field based shadow computation
- **Ambient Occlusion**: Screen-space and distance-field AO
- **Global Illumination**: RSM-based indirect lighting
- **Material Systems**: PBR and custom material models

### Optimization Techniques
- **Ray Marching**: Sphere tracing with adaptive step sizing
- **Early Ray Termination**: Efficient traversal optimizations
- **Importance Sampling**: Adaptive VPL distribution
- **GPU Memory Management**: Efficient resource utilization

### SDF Mesh Techniques
- **Mesh → Volume SDF**: Bake arbitrary meshes into 3D textures and sample in shaders
- **Bounding Box Clipping**: `sdBox` limits shape; intersection via `max()`
- **Early Exit**: Skip 3D texture sampling when outside the box (`boxSdf > 0`)
- **Non-uniform Scaling**: `u.meshHalfSize.xyz` controls geometry scale per axis
- **Conservative Distance Scaling**: CPU-computed `scaleMin` ensures safe marching
- **UVW Mapping**: `local / halfSize` normalized to `[0,1]` with clamp
- **Runtime Asset Switching**: Select `.sdf` files at runtime via UI

## 🔧 Dependencies

### Core Libraries
- **EasyVulkan**: Custom Vulkan abstraction layer
- **Vulkan SDK**: Graphics API and validation layers
- **GLFW**: Window management and input handling
- **ImGui**: Immediate mode GUI for controls
- **VMA**: Vulkan Memory Allocator
- **STB**: Image loading utilities

### Build Tools
- **CMake**: Cross-platform build system
- **glslangValidator**: SPIR-V shader compilation
- **Platform Compilers**: GCC, Clang, MSVC, or Xcode

## 📚 References and Inspiration

- **Inigo Quilez**: 3D SDF techniques and ShaderToy innovations
- **Shadertoy.com**: Community-driven shader development
- **Real-Time Rendering**: Advanced graphics programming techniques
- **PBR Guide**: Physically-based rendering principles

## 🤝 Contributing

Feel free to experiment with the SDF techniques, add new scenes, or improve the rendering pipeline. The modular architecture makes it easy to extend with new demonstrations.

## 📄 License

Copyright 2025 CalendarSUNDAY, All Rights Reserved.

---

*Built with Vulkan for high-performance graphics rendering and real-time SDF visualization.*
