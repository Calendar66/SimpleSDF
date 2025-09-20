/*
 * @Author       : Calendar66 calendarsunday@163.com
 * @Date         : 2025-09-14 15:03:49
 * @Description  : 
 * @FilePath     : SDFMesh.cpp
 * @LastEditTime : 2025-09-19 21:15:12
 * @LastEditors  : Calendar66 calendarsunday@163.com
 * @Version      : V1.0.0
 * Copyright 2025 CalendarSUNDAY, All Rights Reserved. 
 * 2025-09-18 15:03:49
 */


 #include "SDFMesh.hpp"

 #include <EasyVulkan/Builders/BufferBuilder.hpp>
 #include <EasyVulkan/Builders/CommandBufferBuilder.hpp>
 #include <EasyVulkan/Builders/FramebufferBuilder.hpp>
 #include <EasyVulkan/Builders/GraphicsPipelineBuilder.hpp>
 #include <EasyVulkan/Builders/RenderPassBuilder.hpp>
 #include <EasyVulkan/Builders/ShaderModuleBuilder.hpp>
 #include <EasyVulkan/Builders/DescriptorSetBuilder.hpp>
 #include <EasyVulkan/Builders/ImageBuilder.hpp>
 #include <EasyVulkan/Builders/SamplerBuilder.hpp>
 #include <EasyVulkan/Core/ImGuiManager.hpp>
 #include <EasyVulkan/Utils/ResourceUtils.hpp>
 #include "imgui.h"
 
 #include <array>
 #include <vector>
 #include <cmath>
 #include <stdexcept>
 #include <fstream>
 #include <sstream>
 #include <iostream>
 #include <GLFW/glfw3.h>
 
 void SDFMesh::run() {
     initVulkan();
     mainLoop();
 }
 
 bool SDFMesh::initVulkan() {
     initVulkanPC();
     return true;
 }
 
 void SDFMesh::initVulkanPC() {
     if (!glfwInit()) {
         throw std::runtime_error("Failed to initialize GLFW");
     }
     int monitorCount = 0;
     GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
     GLFWmonitor* chosenMonitor = nullptr;
     if (monitors && monitorCount > 0) {
 #if !defined(__OHOS__)
         int index = kMonitorIndex;
 #else
         int index = 0;
 #endif
         if (index >= 0 && index < monitorCount) {
             chosenMonitor = monitors[index];
         }
     }
     if (!chosenMonitor) {
         chosenMonitor = glfwGetPrimaryMonitor();
     }
 
     const GLFWvidmode* mode = glfwGetVideoMode(chosenMonitor);
     int windowWidth = mode ? mode->width : 1280;
     int windowHeight = mode ? mode->height : 720;
 
     // Query monitor work area to position window on the selected monitor (windowed fullscreen)
     int workX = 0, workY = 0, workW = windowWidth, workH = windowHeight;
     glfwGetMonitorWorkarea(chosenMonitor, &workX, &workY, &workW, &workH);
     // We only needed GLFW here for monitor info; EasyVulkan will create the actual window
     glfwTerminate();
 
     context = std::make_unique<ev::VulkanContext>(true);
     VkPhysicalDeviceFeatures features{};
     features.fragmentStoresAndAtomics = VK_TRUE;
     features.sampleRateShading = VK_TRUE;
     context->setDeviceFeatures(features);
     context->setInstanceExtensions({"VK_KHR_get_physical_device_properties2"});
     context->enableImGui();
     context->initialize(windowWidth, windowHeight);
 
     device = context->getDevice();
     resourceManager = context->getResourceManager();
     cmdPoolManager = context->getCommandPoolManager();
     swapchainManager = context->getSwapchainManager();
     syncManager = context->getSynchronizationManager();
 
     swapchainManager->setPreferredColorSpace(VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
     swapchainManager->setImageUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
     swapchainManager->createSwapchain();
 
     createRenderPass();
     createFramebuffers();
 
     // Create resources for RSM offscreen pass
     createRSMPassResources();

    // Initialize available SDF files list
    availableSDFFiles = {"car.sdf", "carBig.sdf", "Halloween.sdf", "mask.sdf"};
    
    // Find current SDF file index (default to mask.sdf)
    std::string defaultFile = "mask.sdf";
    currentSDFIndex = 0;
    for (size_t i = 0; i < availableSDFFiles.size(); ++i) {
        if (availableSDFFiles[i] == defaultFile) {
            currentSDFIndex = static_cast<int>(i);
            break;
        }
    }
    
    // Load SDF mesh and create 3D texture before creating descriptor sets
    // Use existing bounding box; shader will fit SDF into the box
    if (loadSDFFile("assets/sdf/" + availableSDFFiles[currentSDFIndex])) {
        createSDFTexture();
    }
 
     if (auto* imgui = context->getImGuiManager()) {
         imgui->initialize(
             renderPass,
             static_cast<uint32_t>(swapchainManager->getSwapchainImageViews().size()),
             VK_SAMPLE_COUNT_1_BIT);
         imgui->enableResourceMonitor(true);
     }
 
     startTime = std::chrono::high_resolution_clock::now();
 
     createVertexBuffer();
    createUniformBuffer();
     createDescriptorSetLayout();
     createDescriptorSets();
     createPipeline();
     createRSMPipeline();
     createCommandBuffers();
     setupMouseCallback();
     syncManager->createFrameSynchronization(frameNum);
 }
 
 void SDFMesh::createRenderPass() {
     auto builder = resourceManager->createRenderPass();
     builder.addColorAttachment(
         swapchainManager->getSwapchainImageFormat(),
         VK_SAMPLE_COUNT_1_BIT,
         VK_ATTACHMENT_LOAD_OP_CLEAR,
         VK_ATTACHMENT_STORE_OP_STORE,
         VK_IMAGE_LAYOUT_UNDEFINED,
         VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
     builder.beginSubpass().addColorReference(0).endSubpass();
     renderPass = builder.build("SDFMesh-render-pass");
 }
 
 void SDFMesh::createFramebuffers() {
     const auto& views = swapchainManager->getSwapchainImageViews();
     const auto& extent = swapchainManager->getSwapchainExtent();
     framebuffers.resize(views.size());
     for (size_t i = 0; i < views.size(); ++i) {
         auto fb = resourceManager->createFramebuffer();
         framebuffers[i] = fb.addAttachment(views[i])
                            .setDimensions(extent.width, extent.height)
                            .build(renderPass, "SDFMesh-fb-" + std::to_string(i));
     }
 }
 
 void SDFMesh::createRSMPassResources() {
     // Create RSM images (position, normal, flux)
     auto imgBuilder = resourceManager->createImage();
     auto createAttachment = [&](const char* name, VkImage& image, VmaAllocation& alloc, VkImageView& view){
         ev::ImageInfo info = imgBuilder
             .setFormat(VK_FORMAT_R16G16B16A16_SFLOAT)
             .setExtent(rsmWidth, rsmHeight)
             .setUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
             .build(name, &alloc);
         image = info.image;
         view = info.imageView;
     };
     createAttachment("rsm_position", rsmPositionImage, rsmPositionAlloc, rsmPositionView);
     createAttachment("rsm_normal",   rsmNormalImage,   rsmNormalAlloc,   rsmNormalView);
     createAttachment("rsm_flux",     rsmFluxImage,     rsmFluxAlloc,     rsmFluxView);
 
     // Create RSM render pass with 3 color attachments, final layout for sampling
     auto rpBuilder = resourceManager->createRenderPass();
     rpBuilder
         .addColorAttachment(
             VK_FORMAT_R16G16B16A16_SFLOAT,
             VK_SAMPLE_COUNT_1_BIT,
             VK_ATTACHMENT_LOAD_OP_CLEAR,
             VK_ATTACHMENT_STORE_OP_STORE,
             VK_IMAGE_LAYOUT_UNDEFINED,
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
         .addColorAttachment(
             VK_FORMAT_R16G16B16A16_SFLOAT,
             VK_SAMPLE_COUNT_1_BIT,
             VK_ATTACHMENT_LOAD_OP_CLEAR,
             VK_ATTACHMENT_STORE_OP_STORE,
             VK_IMAGE_LAYOUT_UNDEFINED,
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
         .addColorAttachment(
             VK_FORMAT_R16G16B16A16_SFLOAT,
             VK_SAMPLE_COUNT_1_BIT,
             VK_ATTACHMENT_LOAD_OP_CLEAR,
             VK_ATTACHMENT_STORE_OP_STORE,
             VK_IMAGE_LAYOUT_UNDEFINED,
             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
         .beginSubpass()
             .addColorReference(0)
             .addColorReference(1)
             .addColorReference(2)
         .endSubpass();
     rsmRenderPass = rpBuilder.build("rsm-render-pass");
 
     // Create framebuffer
     auto fb = resourceManager->createFramebuffer();
     rsmFramebuffer = fb
         .addAttachment(rsmPositionView)
         .addAttachment(rsmNormalView)
         .addAttachment(rsmFluxView)
         .setDimensions(rsmWidth, rsmHeight)
         .build(rsmRenderPass, "rsm-fb");
 
     // Create sampler for sampling RSM textures
     rsmSampler = resourceManager->createSampler()
         .setMagFilter(VK_FILTER_LINEAR)
         .setMinFilter(VK_FILTER_LINEAR)
         .setAddressModeU(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)
         .setAddressModeV(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)
         .build("rsm-sampler");
 }
 
 void SDFMesh::recreateRSMResources(uint32_t newSize) {
     // Ensure GPU is idle before destroying resources referenced by in-flight command buffers
     if (device && device->getLogicalDevice() != VK_NULL_HANDLE) {
         vkDeviceWaitIdle(device->getLogicalDevice());
     }
     // Clear old RSM framebuffer and images via ResourceManager
     resourceManager->clearResource("rsm-fb", VK_OBJECT_TYPE_FRAMEBUFFER);
     resourceManager->clearResource("rsm_position", VK_OBJECT_TYPE_IMAGE);
     resourceManager->clearResource("rsm_normal", VK_OBJECT_TYPE_IMAGE);
     resourceManager->clearResource("rsm_flux", VK_OBJECT_TYPE_IMAGE);
 
     rsmFramebuffer = VK_NULL_HANDLE;
     rsmPositionImage = VK_NULL_HANDLE; rsmNormalImage = VK_NULL_HANDLE; rsmFluxImage = VK_NULL_HANDLE;
     rsmPositionView = VK_NULL_HANDLE; rsmNormalView = VK_NULL_HANDLE; rsmFluxView = VK_NULL_HANDLE;
     rsmPositionAlloc = VK_NULL_HANDLE; rsmNormalAlloc = VK_NULL_HANDLE; rsmFluxAlloc = VK_NULL_HANDLE;
 
     rsmWidth = newSize;
     rsmHeight = newSize;
 
     // Recreate images
     auto imgBuilder = resourceManager->createImage();
     auto recreateAttachment = [&](const char* name, VkImage& image, VmaAllocation& alloc, VkImageView& view){
         ev::ImageInfo info = imgBuilder
             .setFormat(VK_FORMAT_R16G16B16A16_SFLOAT)
             .setExtent(rsmWidth, rsmHeight)
             .setUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
             .build(name, &alloc);
         image = info.image;
         view = info.imageView;
     };
     recreateAttachment("rsm_position", rsmPositionImage, rsmPositionAlloc, rsmPositionView);
     recreateAttachment("rsm_normal",   rsmNormalImage,   rsmNormalAlloc,   rsmNormalView);
     recreateAttachment("rsm_flux",     rsmFluxImage,     rsmFluxAlloc,     rsmFluxView);
 
     // Recreate framebuffer with existing render pass
     auto fb = resourceManager->createFramebuffer();
     rsmFramebuffer = fb
         .addAttachment(rsmPositionView)
         .addAttachment(rsmNormalView)
         .addAttachment(rsmFluxView)
         .setDimensions(rsmWidth, rsmHeight)
         .build(rsmRenderPass, "rsm-fb");
 
     // Recreate and rebind descriptor sets to updated image views
     size_t count = swapchainManager->getSwapchainImages().size();
     descriptorSets.resize(count);
    for (size_t i = 0; i < count; ++i) {
         std::string dsName = std::string("SDFMesh_descriptor_set_") + std::to_string(i);
         resourceManager->clearResource(dsName, VK_OBJECT_TYPE_DESCRIPTOR_SET);
         auto builder = resourceManager->createDescriptorSet();
        builder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
               .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
               .addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
               .addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
               .addBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
               .addBufferDescriptor(0, uniformBuffer, 0, sizeof(SDFMeshUniforms), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
               .addImageDescriptor(1, rsmPositionView, rsmSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
               .addImageDescriptor(2, rsmNormalView, rsmSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
               .addImageDescriptor(3, rsmFluxView, rsmSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        if (sdfData.isLoaded && sdfMeshTextureView != VK_NULL_HANDLE) {
            builder.addImageDescriptor(4, sdfMeshTextureView, sdfMeshSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        }
         descriptorSets[i] = builder.build(descriptorSetLayout, dsName);
     }
 }
 
 void SDFMesh::createVertexBuffer() {
     const std::vector<SDFMeshVertex> vertices = {
         {{-1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
         {{ 1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
         {{-1.0f,  1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
         {{ 1.0f,  1.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, 1.0f}}
     };
 
     auto builder = resourceManager->createBuffer();
     fullscreenVertexBuffer = builder.setSize(sizeof(vertices[0]) * vertices.size())
         .setUsage(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
         .setMemoryProperties(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
         .buildAndInitialize(vertices.data(), sizeof(vertices[0]) * vertices.size(), "SDFMesh-vertex-buffer");
 }
 
// removed
 
 void SDFMesh::createPipeline() {
     std::string vertShaderPath = "shaders/triangle.vert.spv";
     std::string fragShaderPath = "shaders/sdf_mesh_main.frag.spv";
 
     auto vert = resourceManager->createShaderModule().loadFromFile(vertShaderPath).build("SDFMesh-vert");
     auto frag = resourceManager->createShaderModule().loadFromFile(fragShaderPath).build("SDFMesh-frag");
 
     VkVertexInputBindingDescription binding{};
     binding.binding = 0;
     binding.stride = sizeof(SDFMeshVertex);
     binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
 
     std::array<VkVertexInputAttributeDescription, 3> attrs{};
     attrs[0].binding = 0; attrs[0].location = 0; attrs[0].format = VK_FORMAT_R32G32_SFLOAT; attrs[0].offset = offsetof(SDFMeshVertex, pos);
     attrs[1].binding = 0; attrs[1].location = 1; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[1].offset = offsetof(SDFMeshVertex, color);
     attrs[2].binding = 0; attrs[2].location = 2; attrs[2].format = VK_FORMAT_R32G32_SFLOAT; attrs[2].offset = offsetof(SDFMeshVertex, texCoord);
 
     auto pipelineBuilder = resourceManager->createGraphicsPipeline();
     pipeline = pipelineBuilder
         .addShaderStage(VK_SHADER_STAGE_VERTEX_BIT, vert)
         .addShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, frag)
         .setVertexInputState(binding, std::vector<VkVertexInputAttributeDescription>(attrs.begin(), attrs.end()))
         .setInputAssemblyState(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP)
         .setDynamicState({VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR})
         .setDepthStencilState(VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS)
         .setColorBlendState({VkPipelineColorBlendAttachmentState{VK_FALSE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT}})
         .setRenderPass(renderPass, 0)
         .setDescriptorSetLayouts({descriptorSetLayout})
         .build("SDFMesh-pipeline");
 
     pipelineLayout = pipelineBuilder.getPipelineLayout();
 }
 
 void SDFMesh::createRSMPipeline() {
     // Fullscreen quad vertex + RSM light frag
     std::string vertShaderPath = "shaders/triangle.vert.spv";
     std::string fragShaderPath = "shaders/sdf_mesh_rsm.frag.spv";
 
     auto vert = resourceManager->createShaderModule().loadFromFile(vertShaderPath).build("rsm-vert");
     auto frag = resourceManager->createShaderModule().loadFromFile(fragShaderPath).build("rsm-frag");
 
     VkVertexInputBindingDescription binding{};
     binding.binding = 0;
     binding.stride = sizeof(SDFMeshVertex);
     binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
 
     std::array<VkVertexInputAttributeDescription, 3> attrs{};
     attrs[0].binding = 0; attrs[0].location = 0; attrs[0].format = VK_FORMAT_R32G32_SFLOAT; attrs[0].offset = offsetof(SDFMeshVertex, pos);
     attrs[1].binding = 0; attrs[1].location = 1; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[1].offset = offsetof(SDFMeshVertex, color);
     attrs[2].binding = 0; attrs[2].location = 2; attrs[2].format = VK_FORMAT_R32G32_SFLOAT; attrs[2].offset = offsetof(SDFMeshVertex, texCoord);
 
     auto builder = resourceManager->createGraphicsPipeline();
     rsmPipeline = builder
         .addShaderStage(VK_SHADER_STAGE_VERTEX_BIT, vert)
         .addShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, frag)
         .setVertexInputState(binding, std::vector<VkVertexInputAttributeDescription>(attrs.begin(), attrs.end()))
         .setInputAssemblyState(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP)
         .setDynamicState({VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR})
         .setDepthStencilState(VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS)
         .setColorBlendState({
             VkPipelineColorBlendAttachmentState{VK_FALSE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT},
             VkPipelineColorBlendAttachmentState{VK_FALSE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT},
             VkPipelineColorBlendAttachmentState{VK_FALSE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT}
         })
         .setRenderPass(rsmRenderPass, 0)
         .setDescriptorSetLayouts({descriptorSetLayout})
         .build("rsm-pipeline");
 
     rsmPipelineLayout = builder.getPipelineLayout();
 }
 
 void SDFMesh::createCommandBuffers() {
     if (commandPool == VK_NULL_HANDLE) {
         commandPool = cmdPoolManager->createCommandPool(device->getGraphicsQueueFamily(), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
     }
     auto builder = resourceManager->createCommandBuffer();
     commandBuffers = builder.setCommandPool(commandPool).setCount(swapchainManager->getSwapchainImageViews().size()).buildMultiple();
 }
 
 void SDFMesh::recordCommandBuffer(uint32_t imageIndex) {
     VkCommandBuffer cmd = commandBuffers[imageIndex];
     VkCommandBufferBeginInfo begin{}; begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; begin.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
     vkBeginCommandBuffer(cmd, &begin);
 
     // RSM pass (offscreen)
     if (enableRSM) {
         VkClearValue clears[3];
         clears[0].color = {{0,0,0,0}};
         clears[1].color = {{0,0,0,0}};
         clears[2].color = {{0,0,0,0}};
         VkRenderPassBeginInfo rsmRp{}; rsmRp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO; rsmRp.renderPass = rsmRenderPass; rsmRp.framebuffer = rsmFramebuffer;
         rsmRp.renderArea.offset = {0, 0}; rsmRp.renderArea.extent = {rsmWidth, rsmHeight}; rsmRp.clearValueCount = 3; rsmRp.pClearValues = clears;
         vkCmdBeginRenderPass(cmd, &rsmRp, VK_SUBPASS_CONTENTS_INLINE);
         vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rsmPipeline);
         VkViewport vp{}; vp.x = 0.0f; vp.y = 0.0f; vp.width = (float)rsmWidth; vp.height = (float)rsmHeight; vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
         vkCmdSetViewport(cmd, 0, 1, &vp);
         VkRect2D sc{}; sc.offset = {0,0}; sc.extent = {rsmWidth, rsmHeight}; vkCmdSetScissor(cmd, 0, 1, &sc);
         // Use same descriptor set (binding 0 UBO)
         vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rsmPipelineLayout, 0, 1, &descriptorSets[imageIndex], 0, nullptr);
         VkDeviceSize offsets[] = {0};
         vkCmdBindVertexBuffers(cmd, 0, 1, &fullscreenVertexBuffer, offsets);
         vkCmdDraw(cmd, 4, 1, 0, 0);
         vkCmdEndRenderPass(cmd);
     }else{
         ev::ResourceUtils::transitionImageLayout(
             device, cmd, rsmPositionImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
         ev::ResourceUtils::transitionImageLayout(
             device, cmd, rsmNormalImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
         ev::ResourceUtils::transitionImageLayout(
             device, cmd, rsmFluxImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
     }
 
     VkClearValue clear = {{{0.03f, 0.05f, 0.09f, 1.0f}}};
     VkRenderPassBeginInfo rp{}; rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO; rp.renderPass = renderPass; rp.framebuffer = framebuffers[imageIndex];
     rp.renderArea.offset = {0, 0}; rp.renderArea.extent = swapchainManager->getSwapchainExtent(); rp.clearValueCount = 1; rp.pClearValues = &clear;
     vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
 
     vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
     VkExtent2D extent = swapchainManager->getSwapchainExtent();
     VkViewport viewport{}; viewport.x = 0.0f; viewport.y = 0.0f; viewport.width = static_cast<float>(extent.width); viewport.height = static_cast<float>(extent.height); viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
     vkCmdSetViewport(cmd, 0, 1, &viewport);
     VkRect2D scissor{}; scissor.offset = {0, 0}; scissor.extent = extent; vkCmdSetScissor(cmd, 0, 1, &scissor);
 
     vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[imageIndex], 0, nullptr);
     VkDeviceSize offsets[] = {0};
     vkCmdBindVertexBuffers(cmd, 0, 1, &fullscreenVertexBuffer, offsets);
     vkCmdDraw(cmd, 4, 1, 0, 0);
 
     if (auto* imgui = context->getImGuiManager()) {
         imgui->beginFrame();
         ImGui::Begin("SDF Practice Controls");
 
         ImGui::Text("Box Rotation");
         ImGui::SliderFloat3("Euler (rad)", rotationEuler, -3.14159f, 3.14159f, "%.3f");
         ImGui::SliderFloat2("Virtual Joystick", virtualStick, -1.0f, 1.0f, "%.2f");
         ImGui::SliderFloat("Anim Speed", &rotationAnimSpeed, 0.0f, 3.0f, "%.2f");
         if (ImGui::Button("Reset Rotation")) { rotationEuler[0] = rotationEuler[1] = rotationEuler[2] = 0.0f; }
         ImGui::SameLine();
         if (ImGui::Button("Zero Stick")) { virtualStick[0] = virtualStick[1] = 0.0f; }
 
        ImGui::Separator();
        ImGui::Text("Arcball Rotation");
        ImVec2 arc_pos = ImGui::GetCursorScreenPos();
        ImVec2 arc_size = ImVec2(120, 120);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 arc_center = ImVec2(arc_pos.x + arc_size.x * 0.5f, arc_pos.y + arc_size.y * 0.5f);
        float arc_r = arc_size.x * 0.4f;
        dl->AddCircleFilled(arc_center, arc_r, IM_COL32(50, 50, 50, 255));
        dl->AddCircle(arc_center, arc_r, IM_COL32(150, 150, 150, 255), 0, 2.0f);

        float yaw = rotationEuler[1];
        float pitch = rotationEuler[0];
        float handle_x = arc_center.x + (yaw / 3.14159f) * arc_r * 0.8f;
        float handle_y = arc_center.y - (pitch / 1.57079f) * arc_r * 0.8f;
        dl->AddCircleFilled(ImVec2(handle_x, handle_y), 6.0f, IM_COL32(120, 200, 255, 255));
        dl->AddLine(arc_center, ImVec2(handle_x, handle_y), IM_COL32(120, 200, 255, 180), 2.0f);

        ImGui::InvisibleButton("arcball_box_rotation", arc_size);
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 m = ImGui::GetMousePos();
            float rel_x = (m.x - arc_center.x) / (arc_r * 0.8f);
            float rel_y = (arc_center.y - m.y) / (arc_r * 0.8f);
            float d = sqrtf(rel_x * rel_x + rel_y * rel_y);
            if (d > 1.0f) { rel_x /= d; rel_y /= d; }
            rotationEuler[1] = rel_x * 3.14159f;   // yaw
            rotationEuler[0] = rel_y * 1.57079f;   // pitch
        }
        ImGui::SliderFloat("Roll (Z)", &rotationEuler[2], -3.14159f, 3.14159f, "%.3f");
        ImGui::Text("Pitch: %.1f°, Yaw: %.1f°, Roll: %.1f°",
            rotationEuler[0] * 57.29578f,
            rotationEuler[1] * 57.29578f,
            rotationEuler[2] * 57.29578f);

         ImGui::Separator();
        ImGui::Text("Box Color");
         ImGui::ColorEdit3("Color", sphereColor);
 
         ImGui::Separator();
        ImGui::Text("Box Size (L/W/H)");
        // Length->X, Width->Z, Height->Y. Keep positive and clamp to a safe range.
        ImGui::SliderFloat3("L/W/H", boxSizeLWH, 0.2f, 6.0f, "%.2f");
        if (ImGui::Button("Reset Size")) { boxSizeLWH[0]=2.4f; boxSizeLWH[1]=2.4f; boxSizeLWH[2]=2.4f; }

        ImGui::Separator();
        ImGui::Text("SDF Resource");
        // Create SDF file selection combo box
        {
            const char* currentSDFName = availableSDFFiles[currentSDFIndex].c_str();
            if (ImGui::BeginCombo("SDF File", currentSDFName)) {
                for (int i = 0; i < static_cast<int>(availableSDFFiles.size()); ++i) {
                    bool isSelected = (currentSDFIndex == i);
                    if (ImGui::Selectable(availableSDFFiles[i].c_str(), isSelected)) {
                        if (currentSDFIndex != i) {
                            currentSDFIndex = i;
                            sdfSwitchPending = true;
                            pendingSDFFile = "assets/sdf/" + availableSDFFiles[i];
                        }
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }
        if (sdfData.isLoaded) {
            ImGui::Text("Dimensions: %dx%dx%d", sdfData.width, sdfData.height, sdfData.depth);
            ImGui::Text("Cell Size: %.3f", sdfData.cellSize);
        }

         ImGui::Separator();
         ImGui::Text("Lighting");
         ImGui::Checkbox("Key", &enableKey); ImGui::SameLine();
         ImGui::Checkbox("Fill", &enableFill); ImGui::SameLine();
         ImGui::Checkbox("Rim", &enableRim); ImGui::SameLine();
         ImGui::Checkbox("Env", &enableEnv);
         ImGui::Checkbox("Enable RSM", &enableRSM);
         if (enableRSM) {
             ImGui::SameLine();
             ImGui::Checkbox("Indirect Lighting", &enableIndirectLighting);
             ImGui::Checkbox("Importance Sampling", &enableImportanceSampling);
             ImGui::SliderFloat("Indirect Intensity", &indirectIntensity, 0.0f, 2.0f, "%.2f");
         }
         ImGui::Checkbox("Show RSM Only", &showRSMOnly);
         ImGui::Checkbox("Show Indirect Only", &showIndirectOnly);
         {
             const char* rsmItems[] = {"512", "1024", "2048", "4096"};
             int prevIndex = rsmResolutionIndex;
             if (ImGui::Combo("RSM Resolution", &rsmResolutionIndex, rsmItems, 4)) {
                 if (rsmResolutionIndex != prevIndex) {
                     uint32_t newSize = 1024;
                     switch (rsmResolutionIndex) {
                         case 0: newSize = 512; break;
                         case 1: newSize = 1024; break;
                         case 2: newSize = 2048; break;
                         case 3: newSize = 4096; break;
                         default: newSize = 1024; break;
                     }
                     rsmPendingSize = newSize;
                     rsmRecreatePending = true;
                 }
             }
         }
         ImGui::SliderFloat("Key Intensity", &keyIntensity, 0.0f, 3.0f, "%.2f");
         
         ImGui::Text("Main Light Direction");
         
         // Create a visual light direction control using ImGui drawing primitives
         ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
         ImVec2 canvas_size = ImVec2(120, 120);
         ImDrawList* draw_list = ImGui::GetWindowDrawList();
         
         // Draw circle background
         ImVec2 circle_center = ImVec2(canvas_pos.x + canvas_size.x * 0.5f, canvas_pos.y + canvas_size.y * 0.5f);
         float circle_radius = canvas_size.x * 0.4f;
         draw_list->AddCircleFilled(circle_center, circle_radius, IM_COL32(50, 50, 50, 255));
         draw_list->AddCircle(circle_center, circle_radius, IM_COL32(150, 150, 150, 255), 0, 2.0f);
         
         // Calculate light direction position on circle (convert from 3D to 2D projection)
         float light_x = circle_center.x + (lightAzimuth / 3.14f) * circle_radius * 0.8f;
         float light_y = circle_center.y - (lightElevation / 1.57f) * circle_radius * 0.8f;
         
         // Draw light direction indicator
         draw_list->AddCircleFilled(ImVec2(light_x, light_y), 6.0f, IM_COL32(255, 255, 100, 255));
         draw_list->AddLine(circle_center, ImVec2(light_x, light_y), IM_COL32(255, 255, 100, 180), 2.0f);
         
         // Handle mouse interaction
         ImGui::InvisibleButton("light_control", canvas_size);
         if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
             ImVec2 mouse_pos = ImGui::GetMousePos();
             float rel_x = (mouse_pos.x - circle_center.x) / (circle_radius * 0.8f);
             float rel_y = (circle_center.y - mouse_pos.y) / (circle_radius * 0.8f);
             
             // Clamp to circle and update angles
             float dist = sqrtf(rel_x * rel_x + rel_y * rel_y);
             if (dist > 1.0f) {
                 rel_x /= dist;
                 rel_y /= dist;
             }
             
             lightAzimuth = rel_x * 3.14f;
             lightElevation = rel_y * 1.57f;
         }
         
         // Show numerical values and reset button
         ImGui::Text("Elevation: %.2f°", lightElevation * 180.0f / 3.14159f);
         ImGui::Text("Azimuth: %.2f°", lightAzimuth * 180.0f / 3.14159f);
         if (ImGui::Button("Reset Light")) { lightElevation = 0.8f; lightAzimuth = -0.7f; }
         ImGui::SliderFloat("Ambient", &ambientStrength, 0.0f, 1.0f, "%.2f");
         ImGui::SliderFloat("Shadow Quality", &shadowQuality, 0.1f, 2.0f, "%.2f");
         ImGui::SliderFloat("Shadow Intensity", &shadowIntensity, 0.0f, 1.0f, "%.2f");
         ImGui::SliderFloat("Metallic", &metallic, 0.0f, 2.0f, "%.2f");
         ImGui::SliderFloat("Blue Tint", &blueTint, 0.0f, 2.0f, "%.2f");
 
         ImGui::Separator();
         ImGui::Text("PBR (Physically Based Rendering)");
         ImGui::Checkbox("Enable PBR", &enablePBR);
         
         if (enablePBR) {
             ImGui::Text("Global Settings");
             ImGui::SliderFloat("Global Roughness", &globalRoughness, 0.0f, 1.0f, "%.3f");
             ImGui::SliderFloat("Global Metallic", &globalMetallic, 0.0f, 1.0f, "%.3f");
             ImGui::SliderFloat("Base Color Intensity", &baseColorIntensity, 0.1f, 3.0f, "%.2f");

             ImGui::Text("Box Material");
             ImGui::SliderFloat("Box Roughness", &sphere1Roughness, 0.0f, 1.0f, "%.3f");
             ImGui::SliderFloat("Box Metallic", &sphere1Metallic, 0.0f, 1.0f, "%.3f");

             if (ImGui::Button("Reset PBR Settings")) {
                 globalRoughness = 0.5f;
                 globalMetallic = 0.0f;
                 sphere1Roughness = 0.4f;
                 sphere1Metallic = 0.1f;
                 baseColorIntensity = 1.0f;
             }
         }
 
         ImGui::Separator();
         ImGui::Text("Light Orthographic Projection");
         ImGui::SliderFloat2("Ortho Half Size", lightOrthoHalfSize, 1.0f, 20.0f, "%.1f");
         if (ImGui::Button("Reset Ortho Size")) { lightOrthoHalfSize[0] = lightOrthoHalfSize[1] = 8.0f; }
 
         ImGui::End();
         imgui->endFrame();
         imgui->record(cmd);
     }
 
     vkCmdEndRenderPass(cmd);
     vkEndCommandBuffer(cmd);
 }
 
 void SDFMesh::drawFrame() {
     VkFence inFlight = syncManager->getInFlightFence(currentFrame);
     vkWaitForFences(device->getLogicalDevice(), 1, &inFlight, VK_TRUE, UINT64_MAX);
     if (rsmRecreatePending) {
         recreateRSMResources(rsmPendingSize);
         rsmRecreatePending = false;
     }
     if (sdfSwitchPending) {
         switchSDFFile(pendingSDFFile);
         sdfSwitchPending = false;
     }
     uint32_t imageIndex = swapchainManager->acquireNextImage(syncManager->getImageAvailableSemaphore(currentFrame));
     vkResetFences(device->getLogicalDevice(), 1, &inFlight);
 
     updateUniformBuffer(imageIndex);
 
     vkResetCommandBuffer(commandBuffers[imageIndex], 0);
     recordCommandBuffer(imageIndex);
 
     VkSemaphore waitSemaphores[] = {syncManager->getImageAvailableSemaphore(currentFrame)};
     VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
     VkSemaphore signalSemaphores[] = {syncManager->getRenderFinishedSemaphore(currentFrame)};
     VkSubmitInfo submit{}; submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = waitSemaphores; submit.pWaitDstStageMask = waitStages; submit.commandBufferCount = 1; submit.pCommandBuffers = &commandBuffers[imageIndex]; submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = signalSemaphores;
     if (vkQueueSubmit(device->getGraphicsQueue(), 1, &submit, inFlight) != VK_SUCCESS) {
         throw std::runtime_error("failed to submit command buffer!");
     }
     swapchainManager->presentImage(imageIndex, syncManager->getRenderFinishedSemaphore(currentFrame));
     currentFrame = (currentFrame + 1) % frameNum;
     frameCounter++;
 }
 
 void SDFMesh::mainLoop() {
     while (!glfwWindowShouldClose(device->getWindow())) {
         glfwPollEvents();
         drawFrame();
     }
     vkDeviceWaitIdle(device->getLogicalDevice());
 }
 
 void SDFMesh::createUniformBuffer() {
    auto builder = resourceManager->createBuffer();
    uniformBuffer = builder
        .setSize(sizeof(SDFMeshUniforms))
        .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
        .setMemoryUsage(VMA_MEMORY_USAGE_CPU_TO_GPU)
        .setMemoryProperties(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        .setMemoryFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT)
        .build("SDFMesh-ubo", &uniformBufferAllocation);
 }
 
 void SDFMesh::createDescriptorSetLayout() {
     auto builder = resourceManager->createDescriptorSet();
     builder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
            .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
            .addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
           .addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
           .addBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
     descriptorSetLayout = builder.createLayout("SDFMesh_descriptor_layout");
 }
 
 void SDFMesh::createDescriptorSets() {
     size_t count = swapchainManager->getSwapchainImages().size();
     descriptorSets.resize(count);
     for (size_t i = 0; i < count; ++i) {
         auto builder = resourceManager->createDescriptorSet();
         builder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
                .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
                .addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
                .addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
                .addBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
                .addBufferDescriptor(0, uniformBuffer, 0, sizeof(SDFMeshUniforms), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .addImageDescriptor(1, rsmPositionView, rsmSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .addImageDescriptor(2, rsmNormalView, rsmSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .addImageDescriptor(3, rsmFluxView, rsmSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
                
         // Add SDF texture if available
         if (sdfData.isLoaded && sdfMeshTextureView != VK_NULL_HANDLE) {
             builder.addImageDescriptor(4, sdfMeshTextureView, sdfMeshSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
         }
         
         descriptorSets[i] = builder.build(descriptorSetLayout, std::string("SDFMesh_descriptor_set_") + std::to_string(i));
     }
 }
 
 void SDFMesh::updateUniformBuffer(uint32_t) {
     auto now = std::chrono::high_resolution_clock::now();
     float t = std::chrono::duration<float, std::chrono::seconds::period>(now - startTime).count();
     VkExtent2D extent = swapchainManager->getSwapchainExtent();
 
     // Update rotation from virtual joystick (pitch=yaw control)
     rotationEuler[0] += virtualStick[1] * 0.02f; // pitch
     rotationEuler[1] += virtualStick[0] * 0.02f; // yaw
 
     SDFMeshUniforms u{};
     u.iTime = t;
     u.iResolution[0] = static_cast<float>(extent.width);
     u.iResolution[1] = static_cast<float>(extent.height);
     u.iMouse[0] = mouseX;
     u.iMouse[1] = mouseY;
     u.iFrame = frameCounter;
 
     u.sphereRotation[0] = rotationEuler[0];
     u.sphereRotation[1] = rotationEuler[1];
     u.sphereRotation[2] = rotationEuler[2];
     u.sphereRotation[3] = t * rotationAnimSpeed;
 
     // Sphere color
     u.sphereColor[0] = sphereColor[0];
     u.sphereColor[1] = sphereColor[1];
     u.sphereColor[2] = sphereColor[2];
     u.sphereColor[3] = 1.0f;
 
     // Lighting: calculate main light direction from angles
     // Convert spherical coordinates to Cartesian (elevation, azimuth to x,y,z)
     float cosElevation = std::cos(lightElevation);
     float dir[3] = {
         cosElevation * std::sin(lightAzimuth),  // x
         std::sin(lightElevation),               // y  
         cosElevation * std::cos(lightAzimuth)   // z
     };
     // Normalize (should already be normalized, but ensure precision)
     float len = std::sqrt(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
     dir[0]/=len; dir[1]/=len; dir[2]/=len;
     u.enableLights[0] = enableKey ? 1 : 0;
     u.enableLights[1] = enableFill ? 1 : 0;
     u.enableLights[2] = enableRim ? 1 : 0;
     u.enableLights[3] = enableEnv ? 1 : 0;
     u.lightDir[0] = dir[0]; u.lightDir[1] = dir[1]; u.lightDir[2] = dir[2]; u.lightDir[3] = keyIntensity;
     // Key, Fill, Rim colors (RGBA where A is per-light scalar)
     u.lightColors[0][0] = 0.95f; u.lightColors[0][1] = 0.98f; u.lightColors[0][2] = 1.0f; u.lightColors[0][3] = 1.0f;
     u.lightColors[1][0] = 0.4f;  u.lightColors[1][1] = 0.6f;  u.lightColors[1][2] = 0.9f;  u.lightColors[1][3] = 0.6f;
     u.lightColors[2][0] = 0.6f;  u.lightColors[2][1] = 0.8f;  u.lightColors[2][2] = 1.0f;  u.lightColors[2][3] = 0.8f;
     u.ambientColor[0] = 0.08f; u.ambientColor[1] = 0.12f; u.ambientColor[2] = 0.22f; u.ambientColor[3] = ambientStrength;
 
     u.shadowParams[0] = shadowQuality;
     u.shadowParams[1] = shadowIntensity;
     u.shadowParams[2] = blueTint;
     u.shadowParams[3] = metallic;
 
     // Light camera basis for RSM (orthographic around scene center)
     // Build light direction unit vector
     float Lx = dir[0], Ly = dir[1], Lz = dir[2];
     // Choose up vector and build right/up via Gram-Schmidt
     float upCand[3] = {0.0f, 1.0f, 0.0f};
     if (std::abs(Ly) > 0.95f) { upCand[0] = 1.0f; upCand[1] = 0.0f; upCand[2] = 0.0f; }
     // right = normalize(cross(upCand, L))
     float rx = upCand[1]*Lz - upCand[2]*Ly;
     float ry = upCand[2]*Lx - upCand[0]*Lz;
     float rz = upCand[0]*Ly - upCand[1]*Lx;
     float rlen = std::sqrt(rx*rx+ry*ry+rz*rz) + 1e-8f; rx/=rlen; ry/=rlen; rz/=rlen;
     // up = cross(L, right)
     float ux = Ly*rz - Lz*ry;
     float uy = Lz*rx - Lx*rz;
     float uz = Lx*ry - Ly*rx;
     // Fill UBO light camera params
     u.lightRight[0]=rx; u.lightRight[1]=ry; u.lightRight[2]=rz; u.lightRight[3]=0.0f;
     u.lightUp[0]=ux; u.lightUp[1]=uy; u.lightUp[2]=uz; u.lightUp[3]=0.0f;
     // Place light origin so that u.lightDir points from origin toward the scene
     float originDist = 6.0f;
     u.lightOrigin[0] = -Lx * originDist;
     u.lightOrigin[1] = -Ly * originDist;
     u.lightOrigin[2] = -Lz * originDist;
     u.lightOrigin[3] = 1.0f;
     // Ortho half size to cover our room (controlled via ImGui)
     u.lightOrthoHalfSize[0] = lightOrthoHalfSize[0]; u.lightOrthoHalfSize[1] = lightOrthoHalfSize[1]; u.lightOrthoHalfSize[2] = 0.0f; u.lightOrthoHalfSize[3] = 0.0f;
     u.rsmResolution[0] = static_cast<float>(rsmWidth);
     u.rsmResolution[1] = static_cast<float>(rsmHeight);
     u.rsmResolution[2] = 0.0f; u.rsmResolution[3]=0.0f;
     u.rsmParams[0] = 6.0f; // radius in texel units (balanced for quality/aliasing)
     u.rsmParams[1] = 32.0f; // samples
     u.rsmParams[2] = (enableRSM && enableIndirectLighting) ? 1.0f : 0.0f; // enable indirect lighting
     u.rsmParams[3] = enableRSM ? 1.0f : 0.0f; // enable RSM
     u.indirectParams[0] = indirectIntensity;   // indirect intensity scale
     u.indirectParams[1] = 0.0f; u.indirectParams[2] = 0.0f; u.indirectParams[3] = 0.0f;
 
     // Debug params
     u.debugParams[0] = showRSMOnly ? 1.0f : 0.0f; // show RSM only
     u.debugParams[1] = enableImportanceSampling ? 1.0f : 0.0f; // importance sampling enabled
     u.debugParams[2] = showIndirectOnly ? 1.0f : 0.0f; // show indirect lighting only
     u.debugParams[3] = 0.0f;
 
    // PBR parameters
     u.pbrParams[0] = enablePBR ? 1.0f : 0.0f;  // enable PBR flag
     u.pbrParams[1] = globalRoughness;          // global roughness override
     u.pbrParams[2] = globalMetallic;           // global metallic override
     u.pbrParams[3] = 0.0f;                     // reserved
 
     // Per-material roughness and metallic values
     u.roughnessValues[0] = sphere1Roughness;   // box roughness
     u.roughnessValues[1] = sphere1Roughness;   // unused, keep same as box
     u.metallicValues[0] = sphere1Metallic;     // box metallic
     u.metallicValues[1] = sphere1Metallic;     // unused, keep same as box
 
     // Base color factors
     // Shader expects rgb multiplied by alpha as global intensity.
     // Put intensity into .a and keep rgb at 1 to avoid unintended desaturation.
     u.baseColorFactors[0] = 1.0f;              // R factor
     u.baseColorFactors[1] = 1.0f;              // G factor
     u.baseColorFactors[2] = 1.0f;              // B factor
     u.baseColorFactors[3] = baseColorIntensity; // Global intensity in alpha

    // SDF Mesh mapping params
    // Use the user-controlled box as the bounding box for the SDF mesh
    float halfX = std::max(0.05f, boxSizeLWH[0] * 0.5f); // Length -> X
    float halfY = std::max(0.05f, boxSizeLWH[1] * 0.5f); // Height -> Y
    float halfZ = std::max(0.05f, boxSizeLWH[2] * 0.5f); // Width  -> Z
    u.meshHalfSize[0] = halfX;
    u.meshHalfSize[1] = halfY;
    u.meshHalfSize[2] = halfZ;
    u.meshHalfSize[3] = 0.0f;

    if (sdfData.isLoaded) {
        // Original SDF physical extents in its own unit system
        // Use dims * cellSize as conservative length; scale distance by min scale for safe marching
        float origLenX = static_cast<float>(sdfData.width)  * sdfData.cellSize;
        float origLenY = static_cast<float>(sdfData.height) * sdfData.cellSize;
        float origLenZ = static_cast<float>(sdfData.depth)  * sdfData.cellSize;
        float boxLenX  = 2.0f * halfX;
        float boxLenY  = 2.0f * halfY;
        float boxLenZ  = 2.0f * halfZ;
        float sx = (origLenX > 1e-6f) ? (boxLenX / origLenX) : 1.0f;
        float sy = (origLenY > 1e-6f) ? (boxLenY / origLenY) : 1.0f;
        float sz = (origLenZ > 1e-6f) ? (boxLenZ / origLenZ) : 1.0f;
        float scaleMin = std::min(sx, std::min(sy, sz));
        
        u.sdfParams0[0] = scaleMin;                      // distance scale (minimum for safe ray marching)
        u.sdfParams0[1] = (sdfMeshTextureView != VK_NULL_HANDLE) ? 1.0f : 0.0f; // has texture flag
        u.sdfParams0[2] = 0.0f;
        u.sdfParams0[3] = 0.0f;
        
    } else {
        u.sdfParams0[0] = 1.0f;
        u.sdfParams0[1] = 0.0f;
        u.sdfParams0[2] = 0.0f;
        u.sdfParams0[3] = 0.0f;
    }
 
     ev::ResourceUtils::uploadDataToMappedBuffer(uniformBuffer, device, &uniformBufferAllocation, &u, sizeof(u), 0);
 }
 
 void SDFMesh::setupMouseCallback() {
 #if !defined(__OHOS__)
     glfwSetWindowUserPointer(device->getWindow(), this);
     glfwSetCursorPosCallback(device->getWindow(), [](GLFWwindow* w, double xpos, double ypos){
         SDFMesh* app = reinterpret_cast<SDFMesh*>(glfwGetWindowUserPointer(w));
         app->mouseX = static_cast<float>(xpos);
         app->mouseY = static_cast<float>(ypos);
     });
 #endif
 }
 
 bool SDFMesh::loadSDFFile(const std::string& filepath) {
     std::ifstream file(filepath);
     if (!file.is_open()) {
         std::cerr << "Failed to open SDF file: " << filepath << std::endl;
         return false;
     }
     
     std::string line;
     
     // Line 1: Grid dimensions
     if (!std::getline(file, line)) {
         std::cerr << "Failed to read grid dimensions" << std::endl;
         return false;
     }
     std::istringstream dimStream(line);
     if (!(dimStream >> sdfData.width >> sdfData.height >> sdfData.depth)) {
         std::cerr << "Failed to parse grid dimensions: " << line << std::endl;
         return false;
     }
     
     // Line 2: Origin point
     if (!std::getline(file, line)) {
         std::cerr << "Failed to read origin point" << std::endl;
         return false;
     }
     std::istringstream originStream(line);
     if (!(originStream >> sdfData.originX >> sdfData.originY >> sdfData.originZ)) {
         std::cerr << "Failed to parse origin point: " << line << std::endl;
         return false;
     }
     
     // Line 3: Cell size
     if (!std::getline(file, line)) {
         std::cerr << "Failed to read cell size" << std::endl;
         return false;
     }
     std::istringstream cellStream(line);
     if (!(cellStream >> sdfData.cellSize)) {
         std::cerr << "Failed to parse cell size: " << line << std::endl;
         return false;
     }
     
     // Calculate expected number of distance values
     size_t expectedValues = static_cast<size_t>(sdfData.width) * sdfData.height * sdfData.depth;
     sdfData.distances.reserve(expectedValues);
     
    // Read distance values: consume all tokens across lines
    while (std::getline(file, line)) {
        std::istringstream valueStream(line);
        float distance;
        while (valueStream >> distance) {
            sdfData.distances.push_back(distance);
        }
    }
     
     file.close();
     
     // Verify we have the correct number of values
     if (sdfData.distances.size() != expectedValues) {
         std::cerr << "SDF data size mismatch. Expected: " << expectedValues 
                   << ", Got: " << sdfData.distances.size() << std::endl;
         return false;
     }
     
     sdfData.isLoaded = true;
     
     std::cout << "SDF file loaded successfully:" << std::endl;
     std::cout << "  Dimensions: " << sdfData.width << "x" << sdfData.height << "x" << sdfData.depth << std::endl;
     std::cout << "  Origin: (" << sdfData.originX << ", " << sdfData.originY << ", " << sdfData.originZ << ")" << std::endl;
     std::cout << "  Cell size: " << sdfData.cellSize << std::endl;
     std::cout << "  Total values: " << sdfData.distances.size() << std::endl;
     
     return true;
 }
 
 void SDFMesh::createSDFTexture() {
     if (!sdfData.isLoaded) {
         std::cerr << "Cannot create SDF texture: no data loaded" << std::endl;
         return;
     }
     
     // Create 3D texture using ImageBuilder
     auto imgBuilder = resourceManager->createImage();
     ev::ImageInfo info = imgBuilder
         .setFormat(VK_FORMAT_R32_SFLOAT)
         .setExtent(sdfData.width, sdfData.height, sdfData.depth)
         .setImageType(VK_IMAGE_TYPE_3D)
         .setUsage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
         .build("sdf_mesh_texture", &sdfMeshTextureAlloc);
     
     sdfMeshTexture = info.image;
     sdfMeshTextureView = info.imageView;
     
     // Create staging buffer for data upload
     VkDeviceSize imageSize = sdfData.distances.size() * sizeof(float);
     VkBuffer stagingBuffer;
     VmaAllocation stagingBufferAlloc;
     
     stagingBuffer = ev::ResourceUtils::createBuffer(
         device,
         imageSize,
         VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
         &stagingBufferAlloc
     );
     
     // Copy SDF data to staging buffer
     ev::ResourceUtils::uploadDataToMappedBuffer(
         stagingBuffer, device, &stagingBufferAlloc, 
         sdfData.distances.data(), imageSize, 0);
     
    // Use single-time command buffer from CommandPoolManager for transfer
    VkCommandBuffer transferCmd = cmdPoolManager->beginSingleTimeCommands();
     
     // Transition image layout for transfer
     ev::ResourceUtils::transitionImageLayout(
         device, transferCmd, sdfMeshTexture,
         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
     
     // Copy buffer to image
     VkBufferImageCopy region{};
     region.bufferOffset = 0;
     region.bufferRowLength = 0;
     region.bufferImageHeight = 0;
     region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
     region.imageSubresource.mipLevel = 0;
     region.imageSubresource.baseArrayLayer = 0;
     region.imageSubresource.layerCount = 1;
     region.imageOffset = {0, 0, 0};
     region.imageExtent = {sdfData.width, sdfData.height, sdfData.depth};
     
     vkCmdCopyBufferToImage(transferCmd, stagingBuffer, sdfMeshTexture, 
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
     
     // Transition to shader read optimal
     ev::ResourceUtils::transitionImageLayout(
         device, transferCmd, sdfMeshTexture,
         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
     
    // Submit and wait using manager
    cmdPoolManager->endSingleTimeCommands(transferCmd);
     
    // Clean up staging buffer (not managed by ResourceManager)
     vmaDestroyBuffer(device->getAllocator(), stagingBuffer, stagingBufferAlloc);
     
     // Create sampler for SDF texture
     sdfMeshSampler = resourceManager->createSampler()
         .setMagFilter(VK_FILTER_LINEAR)
         .setMinFilter(VK_FILTER_LINEAR)
         .setAddressModeU(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)
         .setAddressModeV(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)
         .setAddressModeW(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)
         .build("sdf_mesh_sampler");
     
     std::cout << "SDF 3D texture created successfully" << std::endl;
 }

 void SDFMesh::switchSDFFile(const std::string& filepath) {
     // Ensure GPU is idle before destroying resources
     if (device && device->getLogicalDevice() != VK_NULL_HANDLE) {
         vkDeviceWaitIdle(device->getLogicalDevice());
     }
     
     // Clear old SDF texture and sampler
     if (sdfMeshTexture != VK_NULL_HANDLE) {
         resourceManager->clearResource("sdf_mesh_texture", VK_OBJECT_TYPE_IMAGE);
         sdfMeshTexture = VK_NULL_HANDLE;
         sdfMeshTextureView = VK_NULL_HANDLE;
         sdfMeshTextureAlloc = VK_NULL_HANDLE;
     }
     if (sdfMeshSampler != VK_NULL_HANDLE) {
         resourceManager->clearResource("sdf_mesh_sampler", VK_OBJECT_TYPE_SAMPLER);
         sdfMeshSampler = VK_NULL_HANDLE;
     }
     
     // Reset SDF data
     sdfData.isLoaded = false;
     sdfData.distances.clear();
     
     // Load new SDF file
     if (loadSDFFile(filepath)) {
         createSDFTexture();
         
         // Recreate descriptor sets with new SDF texture
         size_t count = swapchainManager->getSwapchainImages().size();
         descriptorSets.resize(count);
         for (size_t i = 0; i < count; ++i) {
             std::string dsName = std::string("SDFMesh_descriptor_set_") + std::to_string(i);
             resourceManager->clearResource(dsName, VK_OBJECT_TYPE_DESCRIPTOR_SET);
             auto builder = resourceManager->createDescriptorSet();
             builder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
                    .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
                    .addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
                    .addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
                    .addBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
                    .addBufferDescriptor(0, uniformBuffer, 0, sizeof(SDFMeshUniforms), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                    .addImageDescriptor(1, rsmPositionView, rsmSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                    .addImageDescriptor(2, rsmNormalView, rsmSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                    .addImageDescriptor(3, rsmFluxView, rsmSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
                    
             // Add SDF texture if available
             if (sdfData.isLoaded && sdfMeshTextureView != VK_NULL_HANDLE) {
                 builder.addImageDescriptor(4, sdfMeshTextureView, sdfMeshSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
             }
             
             descriptorSets[i] = builder.build(descriptorSetLayout, dsName);
         }
         
         std::cout << "Successfully switched to SDF file: " << filepath << std::endl;
     } else {
         std::cerr << "Failed to switch to SDF file: " << filepath << std::endl;
     }
 }

 SDFMesh::~SDFMesh() {

 }
