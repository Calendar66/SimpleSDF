/*
 * @Author       : Calendar66 calendarsunday@163.com
 * @Date         : 2025-08-19 00:06:55
 * @Description  : 
 * @FilePath     : SDF2D.hpp
 * @LastEditTime : 2025-08-19 20:11:00
 * @LastEditors  : Calendar66 calendarsunday@163.com
 * @Version      : V1.0.0
 * Copyright 2025 CalendarSUNDAY, All Rights Reserved. 
 * 2025-04-25 00:07:13
 */
#pragma once

#include <EasyVulkan/Core/VulkanContext.hpp>
#include <EasyVulkan/Core/VulkanDevice.hpp>
#include <EasyVulkan/Core/ResourceManager.hpp>
#include <EasyVulkan/Core/CommandPoolManager.hpp>
#include <EasyVulkan/Core/SwapchainManager.hpp>
#include <EasyVulkan/Core/SynchronizationManager.hpp>
#include <memory>
#include <vector>
#include <string_view>
#include <string>
#include <cmath>

#include <EasyVulkan/DataStructures.hpp>
#include <EasyVulkan/Utils/ResourceUtils.hpp>
#include <EasyVulkan/Utils/CommandUtils.hpp>



struct TriangleVertex {
    float pos[2];
    float color[3];
    float texCoord[2];
};

struct ShaderToyUniforms {
    alignas(16) float iTime;
    alignas(8) float iResolution[2];
    alignas(8) float iMouse[2];           // Mouse position for circle
    alignas(8) float lightPos[2];         // Light 1 position
    alignas(16) float lightOn[4];
    alignas(16) float lightRadius[4];
};

class SDF2D {
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
    
    
    /**
     * @brief Destructor to clean up resources
     */
    ~SDF2D();
    
private:
    /* -------------------------------------------------------------------------- */
    /*                                  Settings                                  */
    /* -------------------------------------------------------------------------- */
#if defined(__OHOS__)
    static constexpr int frameNum = 4;
#else
    static constexpr int frameNum = 3;
#endif

    int framePauseInterval = 10;

    /* -------------------------------------------------------------------------- */
    /*                                App necessary                               */
    /* -------------------------------------------------------------------------- */
    // Triangle renderer dimensions (will be set to full screen)
    int windowWidth = 1920;  // Default, will be updated to actual screen size
    int windowHeight = 1080; // Default, will be updated to actual screen size

    uint32_t currentFrame = 0;
    std::unique_ptr<ev::VulkanContext> context;
    ev::VulkanDevice* device = nullptr;
    ev::ResourceManager* resourceManager = nullptr;
    ev::CommandPoolManager* cmdPoolManager = nullptr;
    ev::SwapchainManager* swapchainManager = nullptr;
    ev::SynchronizationManager* syncManager = nullptr;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    
    VkBuffer triangleVertexBuffer = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;  // Recorded for each swapchain image
    std::vector<VkFramebuffer> framebuffers;

    // ShaderToy SDF uniforms
    VkBuffer uniformBuffer = VK_NULL_HANDLE;
    VmaAllocation uniformBufferAllocation = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    
    // Timing and input
    std::chrono::high_resolution_clock::time_point startTime;
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    float mouseSensitivity = 1.0f;
    float ballX = 0.0f;
    float ballY = 0.0f;

    // Light controls (UI state)
    bool  lightEnabled[3] = {true, true, false};
    float lightRadii[3]   = {60.0f, 80.0f, 12.0f};
    // range is auto-derived in shader: range = radius * 25.0
    // Light 1 position controlled via ImGui (in pixel coordinates)
    float light1PositionX = 400.0f;
    float light1PositionY = 300.0f;

    /* -------------------------------------------------------------------------- */
    /*                                  Methods                                   */
    /* -------------------------------------------------------------------------- */
    void createRenderPass();
    void createFramebuffers();
    void createVertexBuffer();
    void createPipeline();
    void createCommandBuffers();
    void recordCommandBuffer(uint32_t imageIndex);
    void drawFrame();
    
    // ShaderToy SDF methods
    void createUniformBuffer();
    void createDescriptorSetLayout();
    void createDescriptorSets();
    void updateUniformBuffer(uint32_t imageIndex);
    void setupMouseCallback();

    /* -------------------------------------------------------------------------- */
    /*                                 Gfx Related                                */
    /* -------------------------------------------------------------------------- */
    VkRenderPass renderPass = VK_NULL_HANDLE;
    
    // Triangle rendering pipeline
    VkPipelineLayout trianglePipelineLayout = VK_NULL_HANDLE;
    VkPipeline trianglePipeline = VK_NULL_HANDLE;

};