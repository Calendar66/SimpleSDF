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
#define APPIMPLEMENTATION 3  // SDFCornell - Cornell box (default)
```

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
│   └── SDFCornell.cpp     # Cornell box with RSM
├── include/               # Header files
├── shaders/               # GLSL shader sources
│   ├── sdf2d.frag        # 2D SDF fragment shader
│   ├── sdf3d.frag        # 3D ray marching shader
│   ├── sdf_practice.frag # Cornell box main shader
│   └── rsm_light.frag    # RSM light pass shader
├── assets/               # Texture resources
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
