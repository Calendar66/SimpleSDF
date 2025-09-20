/*
 * @Author       : Calendar66 calendarsunday@163.com
 * @Date         : 2025-09-14 15:03:49
 * @Description  : 
 * @FilePath     : SDFMesh.hpp
 * @LastEditTime : 2025-09-19 21:10:06
 * @LastEditors  : Calendar66 calendarsunday@163.com
 * @Version      : V1.0.0
 * Copyright 2025 CalendarSUNDAY, All Rights Reserved. 
 * 2025-09-18 15:03:49
 */

#pragma once

#include <EasyVulkan/Core/VulkanContext.hpp>
#include <EasyVulkan/Core/VulkanDevice.hpp>
#include <EasyVulkan/Core/ResourceManager.hpp>
#include <EasyVulkan/Core/CommandPoolManager.hpp>
#include <EasyVulkan/Core/SwapchainManager.hpp>
#include <EasyVulkan/Core/SynchronizationManager.hpp>
#include <EasyVulkan/DataStructures.hpp>
#include <EasyVulkan/Utils/ResourceUtils.hpp>

#include <memory>
#include <vector>
#include <chrono>
#include <string>

struct SDFMeshVertex {
    float pos[2];
    float color[3];
    float texCoord[2];
};

// std140-compatible layout mirroring shaders/sdf_practice.frag
struct SDFMeshUniforms {
    alignas(16) float iTime;
    alignas(8)  float iResolution[2];
    alignas(8)  float iMouse[2];
    alignas(4)  int   iFrame;

    alignas(16) float sphereRotation[4]; // xyz angles (rad), w = animation time

    alignas(16) float sphereColor[4];      // RGB sphere color

    alignas(16) int   enableLights[4];     // x=key, y=fill, z=rim, w=env
    alignas(16) float lightDir[4];         // xyz dir, w intensity
    alignas(16) float lightColors[3][4];   // rgb + alpha(intensity)
    alignas(16) float ambientColor[4];     // rgb + alpha(strength)

    alignas(16) float shadowParams[4];     // x=quality, y=intensity, z=blueTint, w=metallic

    // RSM / light camera parameters
    alignas(16) float lightRight[4];        // xyz right basis of light camera
    alignas(16) float lightUp[4];           // xyz up basis of light camera
    alignas(16) float lightOrigin[4];       // origin of light camera
    alignas(16) float lightOrthoHalfSize[4];// xy half size of ortho frustum
    alignas(16) float rsmResolution[4];     // xy: RSM texture size
    alignas(16) float rsmParams[4];         // x=radius, y=samples, z=enableIndirectLighting(>0.5), w=enableRSM(>0.5)
    alignas(16) float indirectParams[4];    // x=indirectIntensity, y/z/w reserved
    
    // Debug controls
    alignas(16) float debugParams[4];       // x=showRSMOnly (>0.5), y/z/w reserved

    // PBR parameters
    alignas(16) float pbrParams[4];         // x=enablePBR(>0.5), y=globalRoughness, z=globalMetallic, w=reserved
    alignas(8) float roughnessValues[2];   // per-material roughness: [0]=sphere1, [1]=sphere2  
    alignas(8) float metallicValues[2];    // per-material metallic: [0]=sphere1, [1]=sphere2
    alignas(16) float baseColorFactors[4];  // global color tinting factors: RGB + intensity
    
    // SDF mesh mapping params
    // meshHalfSize: half extents of the bounding box for the SDF mesh (in world units)
    // sdfParams0: x = min world scale among axes (for safe sphere tracing), y = hasSDFTex (>0.5), z/w reserved
    alignas(16) float meshHalfSize[4];
    alignas(16) float sdfParams0[4];
};

class SDFMesh {
public:
#if !defined(__OHOS__)
    // Select which monitor to use when sizing/placing the window (0-based index)
    static constexpr int kMonitorIndex = 1;
#endif
#if defined(__OHOS__)
    void initVulkanOHOS(OHNativeWindow* window);
    bool initVulkan(OHNativeWindow* window);
#else
    void initVulkanPC();
    bool initVulkan();
    void run();
#endif
    void mainLoop();
    ~SDFMesh();

private:
#if defined(__OHOS__)
    static constexpr int frameNum = 4;
#else
    static constexpr int frameNum = 3;
#endif

    // Core context
    std::unique_ptr<ev::VulkanContext> context;
    ev::VulkanDevice* device = nullptr;
    ev::ResourceManager* resourceManager = nullptr;
    ev::CommandPoolManager* cmdPoolManager = nullptr;
    ev::SwapchainManager* swapchainManager = nullptr;
    ev::SynchronizationManager* syncManager = nullptr;

    uint32_t currentFrame = 0;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    // Rendering resources
    VkBuffer fullscreenVertexBuffer = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;
    std::vector<VkFramebuffer> framebuffers;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    // UBO and descriptors
    VkBuffer uniformBuffer = VK_NULL_HANDLE;
    VmaAllocation uniformBufferAllocation = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;

    // Timing and inputs
    std::chrono::high_resolution_clock::time_point startTime;
    int frameCounter = 0;
    float mouseX = 0.0f;
    float mouseY = 0.0f;

    // UI state
    float rotationEuler[3] = {0.0f, 0.0f, 0.0f};
    float rotationAnimSpeed = 0.6f;
    float virtualStick[2] = {0.0f, 0.0f}; // -1..1 range

    float sphereColor[3] = {0.3f, 0.7f, 1.0f}; // RGB solid color for spheres

    // Box size controls (full size L/W/H in world units)
    // Mapping: Length->X, Height->Y, Width->Z
    float boxSizeLWH[3] = {2.4f, 2.4f, 2.4f};

    // Lighting
    bool  enableKey = true;
    bool  enableFill = true;
    bool  enableRim = true;
    bool  enableEnv = true;
    float keyIntensity = 1.2f;
    float ambientStrength = 0.25f;
    float blueTint = 1.0f;
    float shadowQuality = 1.0f;
    float shadowIntensity = 0.9f;
    float metallic = 0.6f;
    
    // Light direction angles (in radians)
    float lightElevation = 0.8f;  // Elevation angle (pitch) 
    float lightAzimuth = -0.7f;   // Azimuth angle (yaw)
    
    // Light orthographic projection size control
    float lightOrthoHalfSize[2] = {8.0f, 8.0f}; // X and Y half-size of orthographic frustum

    // RSM controls and resources
    bool  enableRSM = false;
    bool  enableIndirectLighting = true;  // Enable indirect lighting when RSM is enabled
    bool  enableImportanceSampling = true; // Enable adaptive importance sampling for RSM
    uint32_t rsmWidth = 1024;
    uint32_t rsmHeight = 1024;
    int rsmResolutionIndex = 1; // 0:512, 1:1024, 2:2048, 3:4096
    bool rsmRecreatePending = false;
    uint32_t rsmPendingSize = 1024;
    float indirectIntensity = 1.0f; // Physically-based scale for indirect lighting

    // Debug/visualization
    bool showRSMOnly = false;
    bool showIndirectOnly = false;  // New debug mode: show only indirect lighting

    // PBR controls
    bool enablePBR = false;
    float globalRoughness = 0.5f;
    float globalMetallic = 0.0f;
    float sphere1Roughness = 0.4f;  // Box roughness
    float sphere1Metallic = 0.1f;   // Box metallic
    float baseColorIntensity = 1.0f;

    VkRenderPass rsmRenderPass = VK_NULL_HANDLE;
    VkFramebuffer rsmFramebuffer = VK_NULL_HANDLE;
    VkPipeline rsmPipeline = VK_NULL_HANDLE;
    VkPipelineLayout rsmPipelineLayout = VK_NULL_HANDLE;

    VkImage rsmPositionImage = VK_NULL_HANDLE;
    VkImage rsmNormalImage = VK_NULL_HANDLE;
    VkImage rsmFluxImage = VK_NULL_HANDLE;
    VmaAllocation rsmPositionAlloc = VK_NULL_HANDLE;
    VmaAllocation rsmNormalAlloc = VK_NULL_HANDLE;
    VmaAllocation rsmFluxAlloc = VK_NULL_HANDLE;
    VkImageView rsmPositionView = VK_NULL_HANDLE;
    VkImageView rsmNormalView = VK_NULL_HANDLE;
    VkImageView rsmFluxView = VK_NULL_HANDLE;
    VkSampler rsmSampler = VK_NULL_HANDLE;

    // SDF Mesh resources
    VkImage sdfMeshTexture = VK_NULL_HANDLE;
    VmaAllocation sdfMeshTextureAlloc = VK_NULL_HANDLE;
    VkImageView sdfMeshTextureView = VK_NULL_HANDLE;
    VkSampler sdfMeshSampler = VK_NULL_HANDLE;
    
    // SDF data storage
    struct SDFData {
        uint32_t width, height, depth;  // Grid dimensions
        float originX, originY, originZ; // Starting point in world space
        float cellSize;                  // Size of each voxel
        std::vector<float> distances;   // Distance field values
        bool isLoaded = false;
    } sdfData;

    // SDF file management
    std::vector<std::string> availableSDFFiles;
    int currentSDFIndex = 0;
    bool sdfSwitchPending = false;
    std::string pendingSDFFile;

    void createRenderPass();
    void createFramebuffers();
    void createRSMPassResources();
    void recreateRSMResources(uint32_t newSize);
    void createVertexBuffer();
    void createPipeline();
    void createRSMPipeline();
    void createCommandBuffers();
    void recordCommandBuffer(uint32_t imageIndex);
    void drawFrame();

    void createUniformBuffer();
    void createDescriptorSetLayout();
    void createDescriptorSets();
    void updateUniformBuffer(uint32_t imageIndex);
    void setupMouseCallback();

    bool loadSDFFile(const std::string& filepath);
    void createSDFTexture();
    void switchSDFFile(const std::string& filepath);
};
