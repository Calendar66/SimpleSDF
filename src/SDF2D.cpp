/*
 * @Author       : Calendar66 calendarsunday@163.com
 * @Date         : 2025-08-19 00:06:55
 * @Description  : 
 * @FilePath     : SDF2D.cpp
 * @LastEditTime : 2025-08-20 17:07:42
 * @LastEditors  : Calendar66 calendarsunday@163.com
 * @Version      : V1.0.0
 * Copyright 2025 CalendarSUNDAY, All Rights Reserved. 
 * 2025-04-25 00:06:55
 */
#include "SDF2D.hpp"

#include <EasyVulkan/Builders/BufferBuilder.hpp>
#include <EasyVulkan/Builders/CommandBufferBuilder.hpp>
#include <EasyVulkan/Builders/FramebufferBuilder.hpp>
#include <EasyVulkan/Builders/GraphicsPipelineBuilder.hpp>
#include <EasyVulkan/Builders/RenderPassBuilder.hpp>
#include <EasyVulkan/Builders/ShaderModuleBuilder.hpp>
#include <EasyVulkan/Builders/ImageBuilder.hpp>
#include <EasyVulkan/Builders/DescriptorSetBuilder.hpp>
#include <EasyVulkan/Builders/ComputePipelineBuilder.hpp>
#include <EasyVulkan/Builders/SamplerBuilder.hpp>
#include <EasyVulkan/Core/ImGuiManager.hpp>
#include "imgui.h"

#include <array>
#include <iostream>
#include <chrono>
#include <thread>
#include <GLFW/glfw3.h>
 




void SDF2D::run() {
    auto initStart = std::chrono::high_resolution_clock::now();
    initVulkan();
    auto initEnd = std::chrono::high_resolution_clock::now();
    
    // Print initialization timing
    auto initDuration = std::chrono::duration<double, std::milli>(initEnd - initStart).count();
    std::cout << "\nVulkan Initialization Statistics:\n";
    std::cout << "Total Init Time: " << initDuration << " ms\n";
    std::cout << "----------------------------------------\n";

    mainLoop();
}
bool SDF2D::initVulkan() {
    initVulkanPC(); 
    return true;
}

void SDF2D::initVulkanPC() {
    // Get primary monitor resolution for full screen
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }
    
    GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);
    windowWidth = mode->width;
    windowHeight = mode->height;
    
    glfwTerminate(); // Will be reinitialized by VulkanContext
    
    // Create the Vulkan context (with validation layer = true)
    context = std::make_unique<ev::VulkanContext>(true);

    // Enable device features if needed
    VkPhysicalDeviceFeatures features{};
    features.fragmentStoresAndAtomics = VK_TRUE; 
    features.sampleRateShading       = VK_TRUE;
    context->setDeviceFeatures(features);
    context->setInstanceExtensions({"VK_KHR_get_physical_device_properties2"});

    // Enable ImGui and initialize the context. This will create a GLFW window of given size
    context->enableImGui();
    // and set up everything needed in Vulkan up to swapchain creation.
    context->initialize(windowWidth, windowHeight);

    // Grab convenience pointers
    device           = context->getDevice();
    resourceManager  = context->getResourceManager();
    cmdPoolManager   = context->getCommandPoolManager();
    swapchainManager = context->getSwapchainManager();
    syncManager      = context->getSynchronizationManager();

    // Configure the swapchain usage/format
    swapchainManager->setPreferredColorSpace(VK_COLOR_SPACE_PASS_THROUGH_EXT); // or VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
    swapchainManager->setImageUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

    swapchainManager->createSwapchain();

    // Create our main render pass
    createRenderPass();

    // Create FBs that directly wrap the swapchain images
    createFramebuffers();

    // Initialize ImGui with our render pass
    if (auto* imgui = context->getImGuiManager()) {
        imgui->initialize(
            renderPass,
            static_cast<uint32_t>(swapchainManager->getSwapchainImageViews().size()),
            VK_SAMPLE_COUNT_1_BIT);
            imgui->enableResourceMonitor(true);
    }

    // Initialize timing
    startTime = std::chrono::high_resolution_clock::now();

    // Create triangle vertex buffer
    createVertexBuffer();

    // Create ShaderToy SDF uniform buffer and descriptors
    createUniformBuffer();
    createDescriptorSetLayout();
    createDescriptorSets();

    // Create triangle rendering pipeline (now with descriptor sets)
    createPipeline();

    // Allocate command buffers (recorded each frame to include ImGui)
    createCommandBuffers();

    // Setup mouse input
    setupMouseCallback();

    // Setup frame synchronization (triple buffering)
    syncManager->createFrameSynchronization(frameNum);
}


void SDF2D::mainLoop() {
    int frameCount = 0;
    double totalTime = 0.0;
    std::vector<double> frameTimes;  // Store individual frame times
    
    while (!glfwWindowShouldClose(device->getWindow())) {
        auto frameStart = std::chrono::high_resolution_clock::now();
        
        glfwPollEvents();
        drawFrame();
        
        frameCount++;
        auto frameEnd = std::chrono::high_resolution_clock::now();
        auto frameDuration = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
        
        // Store frame time
        frameTimes.push_back(frameDuration);
        
        // Only add to total time after first two frames
        if (frameCount > 2) {
            totalTime += frameDuration;
        }
        
        // Print statistics every framePauseInterval frames
        if (frameCount % framePauseInterval == 0 && frameCount > 2) {
            double averageFrameTime = totalTime / (frameCount - 2);  // Exclude first two frames
            double fps = 1000.0 / averageFrameTime;
            
            // Current frame time
            double currentFrameTime = frameTimes.back();
            
            std::cout << "Frame Statistics:\n";
            std::cout << "Current Frame Time: " << currentFrameTime << " ms\n";
            std::cout << "Average Frame Time (excluding first 2 frames): " << averageFrameTime << " ms\n";
            std::cout << "Average FPS: " << fps << "\n";
            std::cout << "Total Frames: " << frameCount << "\n";
            std::cout << "----------------------------------------\n";
        }
    }

    vkDeviceWaitIdle(device->getLogicalDevice());
}

/* -------------------------------------------------------------------------- */
/*                                 Render Pass                                */
/* -------------------------------------------------------------------------- */
void SDF2D::createRenderPass() {
    // Create a simple render pass for triangle rendering
    auto  renderPassBuilder = resourceManager->createRenderPass();
    // One color attachment matching the swapchain format:
    renderPassBuilder.addColorAttachment(
        swapchainManager->getSwapchainImageFormat(),
        VK_SAMPLE_COUNT_1_BIT,
        VK_ATTACHMENT_LOAD_OP_CLEAR,     // Clear at start
        VK_ATTACHMENT_STORE_OP_STORE,    // Store color so we can see it
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    // Single subpass
    renderPassBuilder.beginSubpass()
           .addColorReference(0)
           .endSubpass();

    renderPass = renderPassBuilder.build("triangle-render-pass");
}

/* -------------------------------------------------------------------------- */
/*                           Swapchain Framebuffers                           */
/* -------------------------------------------------------------------------- */
void SDF2D::createFramebuffers() {
    const auto& swapchainImageViews = swapchainManager->getSwapchainImageViews();
    const auto& swapchainExtent = swapchainManager->getSwapchainExtent();
    
    framebuffers.resize(swapchainImageViews.size());
    
    for (size_t i = 0; i < swapchainImageViews.size(); i++) {
        auto framebufferBuilder = resourceManager->createFramebuffer();
        framebuffers[i] = framebufferBuilder
            .addAttachment(swapchainImageViews[i])
            .setDimensions(swapchainExtent.width, swapchainExtent.height)
            .build(renderPass, "triangle-framebuffer-" + std::to_string(i));
    }
}

/* -------------------------------------------------------------------------- */
/*                              Vertex Buffers                                */
/* -------------------------------------------------------------------------- */
void SDF2D::createVertexBuffer() {
    // Fullscreen rectangle using triangle strip (4 vertices)
    const std::vector<TriangleVertex> vertices = {
        {{-1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},  // Bottom left
        {{ 1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},  // Bottom right
        {{-1.0f,  1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},  // Top left
        {{ 1.0f,  1.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, 0.0f}}   // Top right
    };

    auto  builder = resourceManager->createBuffer();
    triangleVertexBuffer =
        builder.setSize(sizeof(vertices[0]) * vertices.size())
               .setUsage(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
               .setMemoryProperties(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
               .buildAndInitialize(vertices.data(),
                                   sizeof(vertices[0]) * vertices.size(),
                                   "triangle-vertex-buffer");
}

/* -------------------------------------------------------------------------- */
/*                            Triangle Pipeline Setup                          */
/* -------------------------------------------------------------------------- */
void SDF2D::createPipeline() {
    // Create vertex shader module
    std::string vertShaderPath = "shaders/triangle.vert.spv";
    auto vertShader = resourceManager->createShaderModule()
                          .loadFromFile(vertShaderPath)
                          .build("triangle-vertex-shader");

    // Create fragment shader module
    std::string fragShaderPath = "shaders/sdf2dCircleRect.frag.spv";
    auto fragShader = resourceManager->createShaderModule()
                          .loadFromFile(fragShaderPath)
                          .build("sdf2dCircle-fragment-shader");
    
    // Define vertex input binding and attributes for TriangleVertex
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(TriangleVertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};
    
    // Position attribute
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(TriangleVertex, pos);
    
    // Color attribute  
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(TriangleVertex, color);

    // Texture coordinate attribute
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(TriangleVertex, texCoord);

    // Create a basic pipeline (this would need actual shaders in a real implementation)
    auto pipelineBuilder = resourceManager->createGraphicsPipeline();
    trianglePipeline = pipelineBuilder
            // Add shader stages
            .addShaderStage(VK_SHADER_STAGE_VERTEX_BIT, vertShader)
            .addShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragShader)
            // Configure vertex input
            .setVertexInputState(bindingDescription, std::vector<VkVertexInputAttributeDescription>(attributeDescriptions.begin(), attributeDescriptions.end()))
            .setInputAssemblyState(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP)
            .setDynamicState({VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR})
            // Ensure the triangle is not accidentally culled and depth test is disabled (no depth attachment)
            .setDepthStencilState(VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS)
            // Configure blending
            .setColorBlendState(
                {VkPipelineColorBlendAttachmentState{VK_FALSE, // blendEnable (no blending for channel extraction)
                                                     VK_BLEND_FACTOR_ONE,  // srcColorBlendFactor
                                                     VK_BLEND_FACTOR_ZERO, // dstColorBlendFactor
                                                     VK_BLEND_OP_ADD,      // colorBlendOp
                                                     VK_BLEND_FACTOR_ONE,  // srcAlphaBlendFactor
                                                     VK_BLEND_FACTOR_ZERO, // dstAlphaBlendFactor
                                                     VK_BLEND_OP_ADD,      // alphaBlendOp
                                                     VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT}})
            // Set render pass
            .setRenderPass(renderPass, 0)
            // Set descriptor set layout
            .setDescriptorSetLayouts({descriptorSetLayout}) // Include our uniform buffer layout
            // Build the pipeline
            .build("sdf-pipeline");

    trianglePipelineLayout = pipelineBuilder.getPipelineLayout();
}

/* -------------------------------------------------------------------------- */
/*                         Triangle Command Buffers                           */
/* -------------------------------------------------------------------------- */
void SDF2D::createCommandBuffers() {
    // Create a command pool for these buffers
    if (commandPool == VK_NULL_HANDLE) {
        commandPool = cmdPoolManager->createCommandPool(
            device->getGraphicsQueueFamily(),
            VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    }
    
    commandBuffers.clear();

    // Allocate as many command buffers as there are swapchain images
    auto builder = resourceManager->createCommandBuffer();
    commandBuffers = builder.setCommandPool(commandPool)
                            .setCount(swapchainManager->getSwapchainImageViews().size())
                            .buildMultiple();
}

void SDF2D::recordCommandBuffer(uint32_t imageIndex) {
    VkCommandBuffer cmd = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkClearValue clearColor = {{{1.0f, 1.0f, 1.0f, 1.0f}}};
    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = renderPass;
    rpInfo.framebuffer = framebuffers[imageIndex];
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = swapchainManager->getSwapchainExtent();
    rpInfo.clearValueCount = 1;
    rpInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    // SDF rendering content
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, trianglePipeline);
    VkExtent2D extent = swapchainManager->getSwapchainExtent();
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    
    // Bind descriptor set for uniforms
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, trianglePipelineLayout,
                           0, 1, &descriptorSets[imageIndex], 0, nullptr);
    
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, &triangleVertexBuffer, offsets);
    vkCmdDraw(cmd, 4, 1, 0, 0);

    // ImGui content
    if (auto* imgui = context->getImGuiManager()) {
        imgui->beginFrame();
        // Test UI
        ImGui::Begin("ImGui Test");
        ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("Hello, ImGui from EasyVulkan!");
        ImGui::Separator();
        ImGui::Text("Display: %dx%d", extent.width, extent.height);
        ImGui::Text("DeltaTime: %.3f ms (%.1f FPS)", io.DeltaTime * 1000.0f, io.Framerate);

        ImGui::Separator();
        ImGui::Text("Lights");
        ImGui::Checkbox("Light 1 On", &lightEnabled[0]);
        ImGui::SliderFloat("Light 1 Radius", &lightRadii[0], 0.0f, 50.0f, "%.1f");
        ImGui::SliderFloat("Light 1 X", &light1PositionX, 0.0f, static_cast<float>(extent.width), "%.1f");
        ImGui::SliderFloat("Light 1 Y", &light1PositionY, 0.0f, static_cast<float>(extent.height), "%.1f");
        ImGui::Checkbox("Light 2 On", &lightEnabled[1]);
        ImGui::SliderFloat("Light 2 Radius", &lightRadii[1], 0.0f, 50.0f, "%.1f");
        ImGui::Checkbox("Light 3 On", &lightEnabled[2]);
        ImGui::SliderFloat("Light 3 Radius", &lightRadii[2], 0.0f, 50.0f, "%.1f");
        ImGui::Separator();
        ImGui::Text("Circle (Mouse Controlled)");
        ImGui::Text("Mouse Position: (%.1f, %.1f)", mouseX, mouseY);
        ImGui::Text("Ball Position: (%.1f, %.1f)", ballX, ballY);
        ImGui::SliderFloat("Mouse Sensitivity", &mouseSensitivity, 0.1f, 5.0f, "%.1f");
        ImGui::End();
        imgui->endFrame();
        imgui->record(cmd);
    }

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
}

/* -------------------------------------------------------------------------- */
/*                                Draw Frame                                  */
/* -------------------------------------------------------------------------- */
void SDF2D::drawFrame() {
    // Wait for previous frame
    VkFence inFlightFence = syncManager->getInFlightFence(currentFrame);
    vkWaitForFences(device->getLogicalDevice(), 1, &inFlightFence, VK_TRUE, UINT64_MAX);

    // Acquire next swapchain image
    uint32_t imageIndex = swapchainManager->acquireNextImage(
        syncManager->getImageAvailableSemaphore(currentFrame));

    // Reset fence for next frame and update uniforms
    vkResetFences(device->getLogicalDevice(), 1, &inFlightFence);
    updateUniformBuffer(imageIndex);
    
    // Re-record command buffer for this image
    vkResetCommandBuffer(commandBuffers[imageIndex], 0);
    recordCommandBuffer(imageIndex);

    // Submit command buffer
    VkSemaphore waitSemaphores[] = {
        syncManager->getImageAvailableSemaphore(currentFrame)};
    VkPipelineStageFlags waitStages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

    VkSemaphore signalSemaphores[] = {
        syncManager->getRenderFinishedSemaphore(currentFrame)};

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[imageIndex];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(device->getGraphicsQueue(), 1, &submitInfo, inFlightFence) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit command buffer!");
    }

    // Present the image
    swapchainManager->presentImage(
        imageIndex, syncManager->getRenderFinishedSemaphore(currentFrame));

    currentFrame = (currentFrame + 1) % frameNum;
}


/* -------------------------------------------------------------------------- */
/*                          ShaderToy SDF Methods                            */
/* -------------------------------------------------------------------------- */
void SDF2D::createUniformBuffer() {
    // Create uniform buffer using ResourceUtils to get VMA allocation handle
    uniformBuffer = ev::ResourceUtils::createBuffer(
        device,
        sizeof(ShaderToyUniforms),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &uniformBufferAllocation
    );
}

void SDF2D::createDescriptorSetLayout() {
    // Use EasyVulkan's DescriptorSetBuilder to create layout
    auto builder = resourceManager->createDescriptorSet();
    builder.addBinding(
        0,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        1,
        VK_SHADER_STAGE_FRAGMENT_BIT
    );
    descriptorSetLayout = builder.createLayout("sdf2d_descriptor_layout");
}

void SDF2D::createDescriptorSets() {
    // Allocate one descriptor set per swapchain image via EasyVulkan's builder
    size_t imageCount = swapchainManager->getSwapchainImages().size();
    descriptorSets.resize(imageCount);

    for (size_t i = 0; i < imageCount; ++i) {
        auto builder = resourceManager->createDescriptorSet();
        builder
            .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
            .addBufferDescriptor(0, uniformBuffer, 0, sizeof(ShaderToyUniforms), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

        descriptorSets[i] = builder.build(
            descriptorSetLayout,
            std::string("sdf2d_descriptor_set_") + std::to_string(i)
        );
    }
}

void SDF2D::updateUniformBuffer(uint32_t imageIndex) {
    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>
                 (currentTime - startTime).count();

    // Get actual swapchain dimensions
    VkExtent2D extent = swapchainManager->getSwapchainExtent();
    
    ShaderToyUniforms ubo{};
    ubo.iTime = time;
    ubo.iResolution[0] = static_cast<float>(extent.width);
    ubo.iResolution[1] = static_cast<float>(extent.height);
    // Ball position for circle movement (with sensitivity applied)
    ubo.iMouse[0] = ballX;
    ubo.iMouse[1] = ballY;
    
    // Light 1 position from ImGui sliders
    ubo.lightPos[0] = light1PositionX;
    ubo.lightPos[1] = light1PositionY;

    // Light toggles and radii
    ubo.lightOn[0] = lightEnabled[0] ? 1.0f : 0.0f;
    ubo.lightOn[1] = lightEnabled[1] ? 1.0f : 0.0f;
    ubo.lightOn[2] = lightEnabled[2] ? 1.0f : 0.0f;
    ubo.lightOn[3] = 0.0f; // padding

    ubo.lightRadius[0] = lightRadii[0];
    ubo.lightRadius[1] = lightRadii[1];
    ubo.lightRadius[2] = lightRadii[2];
    ubo.lightRadius[3] = 0.0f; // padding

    // Upload data to the mapped uniform buffer using ResourceUtils
    ev::ResourceUtils::uploadDataToMappedBuffer(
        uniformBuffer,
        device,
        &uniformBufferAllocation,
        &ubo,
        sizeof(ubo),
        0
    );
}

void SDF2D::setupMouseCallback() {
#if !defined(__OHOS__)
    // Setup mouse callback for PC/GLFW
    glfwSetWindowUserPointer(device->getWindow(), this);
    
    // Initialize ball position to screen center
    VkExtent2D extent = swapchainManager->getSwapchainExtent();
    ballX = static_cast<float>(extent.width) * 0.5f;
    ballY = static_cast<float>(extent.height) * 0.5f;
    
    glfwSetCursorPosCallback(device->getWindow(), [](GLFWwindow* window, double xpos, double ypos) {
        SDF2D* app = reinterpret_cast<SDF2D*>(glfwGetWindowUserPointer(window));
        
        // Store raw mouse position
        app->mouseX = static_cast<float>(xpos);
        app->mouseY = static_cast<float>(ypos);
        
        // Calculate screen center
        VkExtent2D extent = app->swapchainManager->getSwapchainExtent();
        
        // Calculate mouse delta from center
        float deltaX = app->mouseX;
        float deltaY = app->mouseY;
        
        // Apply sensitivity and update ball position
        app->ballX = deltaX * app->mouseSensitivity;
        app->ballY = deltaY * app->mouseSensitivity;
        
        // Clamp ball position to screen bounds
        app->ballX = std::max(0.0f, std::min(static_cast<float>(extent.width), app->ballX));
        app->ballY = std::max(0.0f, std::min(static_cast<float>(extent.height), app->ballY));
    });
#endif
}

SDF2D::~SDF2D() {
    // Simple cleanup - most resources are managed by EasyVulkan's ResourceManager
    if (device && device->getLogicalDevice()) {
        vkDeviceWaitIdle(device->getLogicalDevice());

        // Destroy the UBO created via ResourceUtils (not tracked by ResourceManager)
        if (uniformBuffer != VK_NULL_HANDLE && uniformBufferAllocation != VK_NULL_HANDLE) {
            vmaDestroyBuffer(device->getAllocator(), uniformBuffer, uniformBufferAllocation);
            uniformBuffer = VK_NULL_HANDLE;
            uniformBufferAllocation = VK_NULL_HANDLE;
        }

        // Do not manually destroy descriptor resources created via ResourceManager builders.
        // They are tracked and released by ResourceManager during context cleanup.
    }
}

