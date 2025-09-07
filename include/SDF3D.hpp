/*
 * @Author       : Calendar66 calendarsunday@163.com
 * @Date         : 2025-08-20 20:00:00
 * @Description  : 3D SDF demo (ray-marched) built similarly to SDF2D
 * @FilePath     : SDF3D.hpp
 * @Version      : V1.0.0
 * Copyright 2025 CalendarSUNDAY, All Rights Reserved.
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

struct SDF3DVertex {
    float pos[2];
    float color[3];
    float texCoord[2];
};

struct ShaderToy3DUniforms {
    alignas(16) float iTime;
    alignas(8)  float iResolution[2];
    alignas(8)  float iMouse[2];
    alignas(4)  int   iFrame;
    alignas(16) int   enableLights[4]; // 1 to enable, 0 to disable for lights 1..4
};

class SDF3D {
public:
#if defined(__OHOS__)
    void initVulkanOHOS(OHNativeWindow* window);
    bool initVulkan(OHNativeWindow* window);
#else
    void initVulkanPC();
    bool initVulkan();
    void run();
#endif
    void mainLoop();
    ~SDF3D();

private:
#if defined(__OHOS__)
    static constexpr int frameNum = 4;
#else
    static constexpr int frameNum = 3;
#endif

    int framePauseInterval = 10;

    // Core context
    std::unique_ptr<ev::VulkanContext> context;
    ev::VulkanDevice* device = nullptr;
    ev::ResourceManager* resourceManager = nullptr;
    ev::CommandPoolManager* cmdPoolManager = nullptr;
    ev::SwapchainManager* swapchainManager = nullptr;
    ev::SynchronizationManager* syncManager = nullptr;

    uint32_t currentFrame = 0;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    // Buffers and render targets
    VkBuffer fullscreenVertexBuffer = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;
    std::vector<VkFramebuffer> framebuffers;

    // ShaderToy-like UBO
    VkBuffer uniformBuffer = VK_NULL_HANDLE;
    VmaAllocation uniformBufferAllocation = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;

    // Pipeline
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;

    // Timing and input
    std::chrono::high_resolution_clock::time_point startTime;
    int frameCounter = 0;
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    float mouseSensitivity = 1.0f;

    // Light toggles (default enabled)
    bool enableLight1 = true;
    bool enableLight2 = true;
    bool enableLight3 = true;
    bool enableLight4 = true;

    // Methods
    void createRenderPass();
    void createFramebuffers();
    void createVertexBuffer();
    void createPipeline();
    void createCommandBuffers();
    void recordCommandBuffer(uint32_t imageIndex);
    void drawFrame();

    void createUniformBuffer();
    void createDescriptorSetLayout();
    void createDescriptorSets();
    void updateUniformBuffer(uint32_t imageIndex);
    void setupMouseCallback();
};

