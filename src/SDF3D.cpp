/*
 * @Author       : Calendar66 calendarsunday@163.com
 * @Date         : 2025-08-20 10:15:03
 * @Description  : 
 * @FilePath     : SDF3D.cpp
 * @LastEditTime : 2025-09-07 16:29:07
 * @LastEditors  : Calendar66 calendarsunday@163.com
 * @Version      : V1.0.0
 * Copyright 2025 CalendarSUNDAY, All Rights Reserved. 
 * 2025-08-20 14:29:25
 */

 #include "SDF3D.hpp"

 #include <EasyVulkan/Builders/BufferBuilder.hpp>
 #include <EasyVulkan/Builders/CommandBufferBuilder.hpp>
 #include <EasyVulkan/Builders/FramebufferBuilder.hpp>
 #include <EasyVulkan/Builders/GraphicsPipelineBuilder.hpp>
 #include <EasyVulkan/Builders/RenderPassBuilder.hpp>
 #include <EasyVulkan/Builders/ShaderModuleBuilder.hpp>
 #include <EasyVulkan/Builders/DescriptorSetBuilder.hpp>
 #include <EasyVulkan/Core/ImGuiManager.hpp>
 #include "imgui.h"
 
 #include <array>
 #include <stdexcept>
 #include <GLFW/glfw3.h>
 
 void SDF3D::run() {
     initVulkan();
     mainLoop();
 }
 
 bool SDF3D::initVulkan() {
     initVulkanPC();
     return true;
 }
 
 void SDF3D::initVulkanPC() {
     if (!glfwInit()) {
         throw std::runtime_error("Failed to initialize GLFW");
     }
     GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
     const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);
     int windowWidth = mode->width;
     int windowHeight = mode->height;
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
 
     swapchainManager->setPreferredColorSpace(VK_COLOR_SPACE_PASS_THROUGH_EXT);
     swapchainManager->setImageUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
     swapchainManager->createSwapchain();
 
     createRenderPass();
     createFramebuffers();
 
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
     createCommandBuffers();
     syncManager->createFrameSynchronization(frameNum);
 }
 
 void SDF3D::createRenderPass() {
     auto builder = resourceManager->createRenderPass();
     builder.addColorAttachment(
         swapchainManager->getSwapchainImageFormat(),
         VK_SAMPLE_COUNT_1_BIT,
         VK_ATTACHMENT_LOAD_OP_CLEAR,
         VK_ATTACHMENT_STORE_OP_STORE,
         VK_IMAGE_LAYOUT_UNDEFINED,
         VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
     builder.beginSubpass().addColorReference(0).endSubpass();
     renderPass = builder.build("sdf3d-render-pass");
 }
 
 void SDF3D::createFramebuffers() {
     const auto& views = swapchainManager->getSwapchainImageViews();
     const auto& extent = swapchainManager->getSwapchainExtent();
     framebuffers.resize(views.size());
     for (size_t i = 0; i < views.size(); ++i) {
         auto fb = resourceManager->createFramebuffer();
         framebuffers[i] = fb.addAttachment(views[i])
                            .setDimensions(extent.width, extent.height)
                            .build(renderPass, "sdf3d-fb-" + std::to_string(i));
     }
 }
 
 void SDF3D::createVertexBuffer() {
     const std::vector<SDF3DVertex> vertices = {
         {{-1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
         {{ 1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
         {{-1.0f,  1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
         {{ 1.0f,  1.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, 1.0f}}
     };
 
     auto builder = resourceManager->createBuffer();
     fullscreenVertexBuffer = builder.setSize(sizeof(vertices[0]) * vertices.size())
         .setUsage(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
         .setMemoryProperties(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
         .buildAndInitialize(vertices.data(), sizeof(vertices[0]) * vertices.size(), "sdf3d-vertex-buffer");
 }
 
 void SDF3D::createPipeline() {
     std::string vertShaderPath = "shaders/triangle.vert.spv";
     std::string fragShaderPath = "shaders/sdf3d.frag.spv";
 
     auto vert = resourceManager->createShaderModule().loadFromFile(vertShaderPath).build("sdf3d-vert");
     auto frag = resourceManager->createShaderModule().loadFromFile(fragShaderPath).build("sdf3d-frag");
 
     VkVertexInputBindingDescription binding{};
     binding.binding = 0;
     binding.stride = sizeof(SDF3DVertex);
     binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
 
     std::array<VkVertexInputAttributeDescription, 3> attrs{};
     attrs[0].binding = 0; attrs[0].location = 0; attrs[0].format = VK_FORMAT_R32G32_SFLOAT; attrs[0].offset = offsetof(SDF3DVertex, pos);
     attrs[1].binding = 0; attrs[1].location = 1; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[1].offset = offsetof(SDF3DVertex, color);
     attrs[2].binding = 0; attrs[2].location = 2; attrs[2].format = VK_FORMAT_R32G32_SFLOAT; attrs[2].offset = offsetof(SDF3DVertex, texCoord);
 
     auto pipelineBuilder = resourceManager->createGraphicsPipeline();
     graphicsPipeline = pipelineBuilder
         .addShaderStage(VK_SHADER_STAGE_VERTEX_BIT, vert)
         .addShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, frag)
         .setVertexInputState(binding, std::vector<VkVertexInputAttributeDescription>(attrs.begin(), attrs.end()))
         .setInputAssemblyState(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP)
         .setDynamicState({VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR})
         .setDepthStencilState(VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS)
         .setColorBlendState({VkPipelineColorBlendAttachmentState{VK_FALSE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT}})
         .setRenderPass(renderPass, 0)
         .setDescriptorSetLayouts({descriptorSetLayout})
         .build("sdf3d-pipeline");
 
     pipelineLayout = pipelineBuilder.getPipelineLayout();
 }
 
 void SDF3D::createCommandBuffers() {
     if (commandPool == VK_NULL_HANDLE) {
         commandPool = cmdPoolManager->createCommandPool(device->getGraphicsQueueFamily(), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
     }
     auto builder = resourceManager->createCommandBuffer();
     commandBuffers = builder.setCommandPool(commandPool).setCount(swapchainManager->getSwapchainImageViews().size()).buildMultiple();
 }
 
 void SDF3D::recordCommandBuffer(uint32_t imageIndex) {
     VkCommandBuffer cmd = commandBuffers[imageIndex];
     VkCommandBufferBeginInfo begin{}; begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; begin.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
     vkBeginCommandBuffer(cmd, &begin);
 
     VkClearValue clear = {{{0.05f, 0.07f, 0.10f, 1.0f}}};
     VkRenderPassBeginInfo rp{}; rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO; rp.renderPass = renderPass; rp.framebuffer = framebuffers[imageIndex];
     rp.renderArea.offset = {0, 0}; rp.renderArea.extent = swapchainManager->getSwapchainExtent(); rp.clearValueCount = 1; rp.pClearValues = &clear;
     vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
 
     vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
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
         ImGui::Begin("SDF3D Controls");
         ImGui::Checkbox("Enable Light 1 (Key Light)", &enableLight1);
         ImGui::Checkbox("Enable Light 2 (Sky/Env)", &enableLight2);
         ImGui::Checkbox("Enable Light 3 (Fill)", &enableLight3);
         ImGui::Checkbox("Enable Light 4 (Rim/Fresnel)", &enableLight4);
         ImGui::End();
         imgui->endFrame();
         imgui->record(cmd);
     }
 
     vkCmdEndRenderPass(cmd);
     vkEndCommandBuffer(cmd);
 }
 
 void SDF3D::drawFrame() {
     VkFence inFlight = syncManager->getInFlightFence(currentFrame);
     vkWaitForFences(device->getLogicalDevice(), 1, &inFlight, VK_TRUE, UINT64_MAX);
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
 
 void SDF3D::mainLoop() {
     while (!glfwWindowShouldClose(device->getWindow())) {
         glfwPollEvents();
         drawFrame();
     }
     vkDeviceWaitIdle(device->getLogicalDevice());
 }
 
 void SDF3D::createUniformBuffer() {
     uniformBuffer = ev::ResourceUtils::createBuffer(
         device,
         sizeof(ShaderToy3DUniforms),
         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
         &uniformBufferAllocation);
 }
 
 void SDF3D::createDescriptorSetLayout() {
     auto builder = resourceManager->createDescriptorSet();
     builder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
     descriptorSetLayout = builder.createLayout("sdf3d_descriptor_layout");
 }
 
 void SDF3D::createDescriptorSets() {
     size_t count = swapchainManager->getSwapchainImages().size();
     descriptorSets.resize(count);
     for (size_t i = 0; i < count; ++i) {
         auto builder = resourceManager->createDescriptorSet();
         builder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
                .addBufferDescriptor(0, uniformBuffer, 0, sizeof(ShaderToy3DUniforms), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
         descriptorSets[i] = builder.build(descriptorSetLayout, std::string("sdf3d_descriptor_set_") + std::to_string(i));
     }
 }
 
 void SDF3D::updateUniformBuffer(uint32_t) {
     auto now = std::chrono::high_resolution_clock::now();
     float t = std::chrono::duration<float, std::chrono::seconds::period>(now - startTime).count();
     VkExtent2D extent = swapchainManager->getSwapchainExtent();
     ShaderToy3DUniforms u{};
     u.iTime = t;
     u.iResolution[0] = static_cast<float>(extent.width);
     u.iResolution[1] = static_cast<float>(extent.height);
     // Disable mouse effect on camera by zeroing mouse input
     u.iMouse[0] = 0.0f;
     u.iMouse[1] = 0.0f;
     u.iFrame = frameCounter;
     u.enableLights[0] = enableLight1 ? 1 : 0;
     u.enableLights[1] = enableLight2 ? 1 : 0;
     u.enableLights[2] = enableLight3 ? 1 : 0;
     u.enableLights[3] = enableLight4 ? 1 : 0;
     ev::ResourceUtils::uploadDataToMappedBuffer(uniformBuffer, device, &uniformBufferAllocation, &u, sizeof(u), 0);
 }
 
 void SDF3D::setupMouseCallback() {
 #if !defined(__OHOS__)
     glfwSetWindowUserPointer(device->getWindow(), this);
     glfwSetCursorPosCallback(device->getWindow(), [](GLFWwindow* w, double xpos, double ypos){
         SDF3D* app = reinterpret_cast<SDF3D*>(glfwGetWindowUserPointer(w));
         app->mouseX = static_cast<float>(xpos);
         app->mouseY = static_cast<float>(ypos);
     });
 #endif
 }
 
 SDF3D::~SDF3D() {
     if (device && device->getLogicalDevice()) {
         vkDeviceWaitIdle(device->getLogicalDevice());
         if (uniformBuffer != VK_NULL_HANDLE && uniformBufferAllocation != VK_NULL_HANDLE) {
             vmaDestroyBuffer(device->getAllocator(), uniformBuffer, uniformBufferAllocation);
             uniformBuffer = VK_NULL_HANDLE;
             uniformBufferAllocation = VK_NULL_HANDLE;
         }
     }
 }
 
 