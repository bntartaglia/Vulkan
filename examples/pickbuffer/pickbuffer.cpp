/*
* Vulkan Example - Object picking using a separate render pass and buffer
*
* This example demonstrates how to implement object picking using a separate rendering pass
* where each object is rendered with a unique color, and then looking up the pixel color
* on mouse click to identify the selected object.
*
* Copyright (C) 2023-2024 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

// Do not include the TinyGLTF library at all
// We don't actually use it in this example
// #include "tiny_gltf.h"

#include <random>
#include "vulkanexamplebase.h"
#include "keycodes.hpp"

// Define button constants if they're not defined in your framework
#define MY_MOUSE_BUTTON_LEFT 0
#define MY_PRESS 1

// Vertex layout for this example
struct Vertex {
    float pos[3];
    float normal[3];
    float uv[2];
    float color[3];
};

// Scene object with a unique ID
class Object {
public:
    glm::mat4 transform;
    std::string name;
    uint32_t id;
    glm::vec3 color;
    bool selected = false;

    Object(uint32_t id, const std::string& name, const glm::vec3& position, const glm::vec3& color) : 
        id(id), 
        name(name),
        color(color) 
    {
        transform = glm::translate(glm::mat4(1.0f), position);
    }

    // Convert object ID to color (for pick buffer)
    glm::vec3 getIdColor() const {
        // Convert the ID to a RGB color (simply by bit shifting in this case)
        float r = ((id & 0x000000FF) >> 0) / 255.0f;
        float g = ((id & 0x0000FF00) >> 8) / 255.0f;
        float b = ((id & 0x00FF0000) >> 16) / 255.0f;
        return glm::vec3(r, g, b);
    }
};

class VulkanExample : public VulkanExampleBase {
public:
    bool wireframe = false;

    // Model used for the spheres
    struct Model {
        vks::Buffer vertices;
        vks::Buffer indices;
        uint32_t indexCount;
    } model;

    // Scene objects
    std::vector<Object> objects;
    int selectedObjectIndex = -1;

    // Main rendering pass
    struct {
        VkPipelineLayout pipelineLayout;
        VkPipeline pipeline;
        VkPipeline wireframe;
        VkDescriptorSet descriptorSet;
        VkDescriptorSetLayout descriptorSetLayout;
    } graphics;

    // Pick buffer pass
    struct {
        VkRenderPass renderPass;
        VkFramebuffer frameBuffer;
        VkImage image;
        VkImageView imageView;
        VkDeviceMemory memory;
        VkPipelineLayout pipelineLayout;
        VkPipeline pipeline;
        VkDescriptorSet descriptorSet;
        VkDescriptorSetLayout descriptorSetLayout;
        uint32_t width, height;
    } pickBuffer;

    // Uniform buffers
    struct {
        vks::Buffer scene;
    } uniformBuffers;

    // Uniform buffer data structure
    struct UniformData {
        glm::mat4 projection;
        glm::mat4 view;
        glm::vec4 lightPos = glm::vec4(5.0f, 5.0f, 5.0f, 1.0f);
    } uniformData;

    VulkanExample() : VulkanExampleBase() {
        title = "Object picking with ID buffer";
        camera.type = Camera::CameraType::lookat;
        camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
        camera.setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
        camera.setPosition(glm::vec3(0.0f, 0.0f, -10.0f));
        camera.setRotationSpeed(0.5f);
        camera.setMovementSpeed(2.0f);
    }

    ~VulkanExample() {
        // Clean up used Vulkan resources
        // Note: Inherited destructor cleans up resources stored in base class
        vkDestroyPipeline(device, graphics.pipeline, nullptr);
        if (graphics.wireframe != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, graphics.wireframe, nullptr);
        }
        vkDestroyPipelineLayout(device, graphics.pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, graphics.descriptorSetLayout, nullptr);

        // Pick buffer resources
        vkDestroyPipeline(device, pickBuffer.pipeline, nullptr);
        vkDestroyPipelineLayout(device, pickBuffer.pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, pickBuffer.descriptorSetLayout, nullptr);
        vkDestroyRenderPass(device, pickBuffer.renderPass, nullptr);
        vkDestroyFramebuffer(device, pickBuffer.frameBuffer, nullptr);
        vkDestroyImageView(device, pickBuffer.imageView, nullptr);
        vkDestroyImage(device, pickBuffer.image, nullptr);
        vkFreeMemory(device, pickBuffer.memory, nullptr);

        // Models
        model.vertices.destroy();
        model.indices.destroy();

        // Uniform buffers
        uniformBuffers.scene.destroy();
    }

    // Create a sphere model using UV sphere construction
    void createSphereModel() {
        // Generate sphere vertices
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        const int sectors = 32;
        const int stacks = 16;
        const float radius = 0.5f;
        
        // Vertices
        float sectorStep = 2.0f * M_PI / sectors;
        float stackStep = M_PI / stacks;

        for (int i = 0; i <= stacks; ++i) {
            float stackAngle = M_PI / 2.0f - i * stackStep;
            float xy = radius * cosf(stackAngle);
            float z = radius * sinf(stackAngle);

            for (int j = 0; j <= sectors; ++j) {
                float sectorAngle = j * sectorStep;

                // Vertex position
                float x = xy * cosf(sectorAngle);
                float y = xy * sinf(sectorAngle);

                // Vertex
                Vertex vertex;
                vertex.pos[0] = x;
                vertex.pos[1] = y;
                vertex.pos[2] = z;

                // Normal
                vertex.normal[0] = x / radius;
                vertex.normal[1] = y / radius;
                vertex.normal[2] = z / radius;

                // Texture coordinates
                vertex.uv[0] = (float)j / sectors;
                vertex.uv[1] = (float)i / stacks;

                // Color (white by default)
                vertex.color[0] = 1.0f;
                vertex.color[1] = 1.0f;
                vertex.color[2] = 1.0f;

                vertices.push_back(vertex);
            }
        }

        // Indices
        for (int i = 0; i < stacks; ++i) {
            int k1 = i * (sectors + 1);
            int k2 = k1 + sectors + 1;

            for (int j = 0; j < sectors; ++j, ++k1, ++k2) {
                if (i != 0) {
                    indices.push_back(k1);
                    indices.push_back(k2);
                    indices.push_back(k1 + 1);
                }

                if (i != (stacks - 1)) {
                    indices.push_back(k1 + 1);
                    indices.push_back(k2);
                    indices.push_back(k2 + 1);
                }
            }
        }

        model.indexCount = static_cast<uint32_t>(indices.size());

        // Create and upload vertex and index buffer
        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &model.vertices,
            vertices.size() * sizeof(Vertex),
            vertices.data()));

        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &model.indices,
            indices.size() * sizeof(uint32_t),
            indices.data()));
    }

    // Initialize the pick buffer
    void setupPickBuffer() {
        pickBuffer.width = width;
        pickBuffer.height = height;

        // Create the image for the pick buffer
        VkImageCreateInfo imageInfo = vks::initializers::imageCreateInfo();
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.extent.width = pickBuffer.width;
        imageInfo.extent.height = pickBuffer.height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        // Image needs to be sampled and used as transfer source for reading
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        VK_CHECK_RESULT(vkCreateImage(device, &imageInfo, nullptr, &pickBuffer.image));

        // Allocate memory for the image
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(device, pickBuffer.image, &memReqs);
        VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &pickBuffer.memory));
        VK_CHECK_RESULT(vkBindImageMemory(device, pickBuffer.image, pickBuffer.memory, 0));

        // Create the image view
        VkImageViewCreateInfo viewInfo = vks::initializers::imageViewCreateInfo();
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange = {};
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        viewInfo.image = pickBuffer.image;
        VK_CHECK_RESULT(vkCreateImageView(device, &viewInfo, nullptr, &pickBuffer.imageView));

        // Create render pass
        std::array<VkAttachmentDescription, 2> attachmentDescs = {};

        // Color attachment (pick buffer)
        attachmentDescs[0].format = VK_FORMAT_R8G8B8A8_UNORM;
        attachmentDescs[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachmentDescs[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachmentDescs[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachmentDescs[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachmentDescs[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachmentDescs[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachmentDescs[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

        // Depth attachment
        attachmentDescs[1].format = depthFormat;
        attachmentDescs[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachmentDescs[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachmentDescs[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachmentDescs[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachmentDescs[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachmentDescs[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachmentDescs[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference depthReference = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorReference;
        subpass.pDepthStencilAttachment = &depthReference;

        VkRenderPassCreateInfo renderPassInfo = vks::initializers::renderPassCreateInfo();
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachmentDescs.size());
        renderPassInfo.pAttachments = attachmentDescs.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &pickBuffer.renderPass));

        // Create framebuffer
        VkImageView attachments[2];
        attachments[0] = pickBuffer.imageView;
        attachments[1] = depthStencil.view;

        VkFramebufferCreateInfo framebufferInfo = vks::initializers::framebufferCreateInfo();
        framebufferInfo.renderPass = pickBuffer.renderPass;
        framebufferInfo.attachmentCount = 2;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = pickBuffer.width;
        framebufferInfo.height = pickBuffer.height;
        framebufferInfo.layers = 1;
        VK_CHECK_RESULT(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &pickBuffer.frameBuffer));
    }

    void setupObjects() {
        // Create 10 sphere objects with random positions and colors
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> positionDist(-5.0f, 5.0f);
        std::uniform_real_distribution<float> colorDist(0.2f, 1.0f);

        // Create 10 spheres with random positions and colors
        for (uint32_t i = 0; i < 10; i++) {
            // Generate random position
            glm::vec3 position(
                positionDist(gen),
                positionDist(gen),
                positionDist(gen) * 0.5f
            );

            // Generate random color
            glm::vec3 color(
                colorDist(gen),
                colorDist(gen),
                colorDist(gen)
            );

            // Create object with unique ID
            objects.emplace_back(i + 1, "Sphere " + std::to_string(i + 1), position, color);
        }
    }

    void setupDescriptorPool() {
        std::vector<VkDescriptorPoolSize> poolSizes = {
            vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2)
        };
        VkDescriptorPoolCreateInfo descriptorPoolInfo =
            vks::initializers::descriptorPoolCreateInfo(static_cast<uint32_t>(poolSizes.size()), poolSizes.data(), 2);
        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
    }

    void setupDescriptorSetLayout() {
        // Graphics pipeline descriptor set layout
        VkDescriptorSetLayoutBinding sceneBinding = vks::initializers::descriptorSetLayoutBinding(
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 
            0);
        std::vector<VkDescriptorSetLayoutBinding> bindings = { sceneBinding };
        
        VkDescriptorSetLayoutCreateInfo descriptorLayout =
            vks::initializers::descriptorSetLayoutCreateInfo(bindings.data(), static_cast<uint32_t>(bindings.size()));
        
        // Create descriptor set layouts
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &graphics.descriptorSetLayout));
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &pickBuffer.descriptorSetLayout));

        // Pipeline layouts
        VkPipelineLayoutCreateInfo pipelineLayoutInfo =
            vks::initializers::pipelineLayoutCreateInfo(&graphics.descriptorSetLayout, 1);
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &graphics.pipelineLayout));

        pipelineLayoutInfo.pSetLayouts = &pickBuffer.descriptorSetLayout;
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pickBuffer.pipelineLayout));
    }

    void setupDescriptorSets() {
        // Graphics pipeline descriptor set
        VkDescriptorSetAllocateInfo allocInfo =
            vks::initializers::descriptorSetAllocateInfo(descriptorPool, &graphics.descriptorSetLayout, 1);
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &graphics.descriptorSet));

        VkWriteDescriptorSet writeDescriptorSet =
            vks::initializers::writeDescriptorSet(graphics.descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers.scene.descriptor);
        
        // Update with only 3 parameters if your framework uses the older API
        vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);

        // Pick buffer descriptor set
        allocInfo.pSetLayouts = &pickBuffer.descriptorSetLayout;
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &pickBuffer.descriptorSet));
        
        writeDescriptorSet.dstSet = pickBuffer.descriptorSet;
        
        // Update with only 3 parameters if your framework uses the older API
        vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
    }

    void preparePipelines() {
        VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
            vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);

        VkPipelineRasterizationStateCreateInfo rasterizationState =
            vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);

        VkPipelineColorBlendAttachmentState blendAttachmentState =
            vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);

        VkPipelineColorBlendStateCreateInfo colorBlendState =
            vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

        VkPipelineDepthStencilStateCreateInfo depthStencilState =
            vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);

        VkPipelineViewportStateCreateInfo viewportState =
            vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);

        VkPipelineMultisampleStateCreateInfo multisampleState =
            vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);

        std::vector<VkDynamicState> dynamicStateEnables = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo dynamicState =
            vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);

        // Vertex input state
        VkVertexInputBindingDescription vertexInputBinding =
            vks::initializers::vertexInputBindingDescription(0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX);

        std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
            vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)),
            vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)),
            vks::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)),
            vks::initializers::vertexInputAttributeDescription(0, 3, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color))
        };

        VkPipelineVertexInputStateCreateInfo vertexInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
        vertexInputState.vertexBindingDescriptionCount = 1;
        vertexInputState.pVertexBindingDescriptions = &vertexInputBinding;
        vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
        vertexInputState.pVertexAttributeDescriptions = vertexInputAttributes.data();

        // Graphics pipeline
        std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;
        VkGraphicsPipelineCreateInfo pipelineCreateInfo =
            vks::initializers::pipelineCreateInfo(graphics.pipelineLayout, renderPass, 0);

        pipelineCreateInfo.pVertexInputState = &vertexInputState;
        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        pipelineCreateInfo.pRasterizationState = &rasterizationState;
        pipelineCreateInfo.pColorBlendState = &colorBlendState;
        pipelineCreateInfo.pMultisampleState = &multisampleState;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pDepthStencilState = &depthStencilState;
        pipelineCreateInfo.pDynamicState = &dynamicState;
        pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineCreateInfo.pStages = shaderStages.data();

        // For both pipelines, we use the same vertex shader and different fragment shaders
        shaderStages[0] = loadShader(getShadersPath() + "pickbuffer/sphere.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        
        // Graphics pipeline with color
        shaderStages[1] = loadShader(getShadersPath() + "pickbuffer/sphere.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &graphics.pipeline));

        // Pick buffer pipeline that renders object IDs as colors
        pipelineCreateInfo.renderPass = pickBuffer.renderPass;
        shaderStages[1] = loadShader(getShadersPath() + "pickbuffer/picking.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pickBuffer.pipeline));

        // Wire frame rendering if supported
        if (deviceFeatures.fillModeNonSolid) {
            rasterizationState.polygonMode = VK_POLYGON_MODE_LINE;
            rasterizationState.lineWidth = 1.0f;
            pipelineCreateInfo.renderPass = renderPass;
            shaderStages[1] = loadShader(getShadersPath() + "pickbuffer/sphere.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
            VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &graphics.wireframe));
        }
    }

    // Prepare and initialize uniform buffer containing shader uniforms
    void prepareUniformBuffers() {
        // Vertex shader uniform buffer block
        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &uniformBuffers.scene,
            sizeof(UniformData)));

        // Map persistently
        VK_CHECK_RESULT(uniformBuffers.scene.map());

        updateUniformBuffers();
    }

    void updateUniformBuffers() {
        uniformData.projection = camera.matrices.perspective;
        uniformData.view = camera.matrices.view;
        memcpy(uniformBuffers.scene.mapped, &uniformData, sizeof(UniformData));
    }

    // Build command buffers for main rendering and pick buffer rendering
    void buildCommandBuffers() {
        VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

        VkClearValue clearValues[2];
        clearValues[0].color = { { 0.2f, 0.2f, 0.2f, 1.0f } };
        clearValues[1].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
        renderPassBeginInfo.renderArea.offset.x = 0;
        renderPassBeginInfo.renderArea.offset.y = 0;
        renderPassBeginInfo.renderArea.extent.width = width;
        renderPassBeginInfo.renderArea.extent.height = height;
        renderPassBeginInfo.clearValueCount = 2;
        renderPassBeginInfo.pClearValues = clearValues;

        for (int32_t i = 0; i < drawCmdBuffers.size(); ++i) {
            VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

            // First render pass: Main visible scene rendering
            renderPassBeginInfo.renderPass = renderPass;
            renderPassBeginInfo.framebuffer = frameBuffers[i];
            vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
            vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

            VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
            vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

            // Bind the uniform buffer
            vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphics.pipelineLayout, 0, 1, &graphics.descriptorSet, 0, nullptr);

            // Bind the graphics pipeline
            vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, wireframe ? graphics.wireframe : graphics.pipeline);

            // Bind the vertex and index buffers
            VkDeviceSize offsets[1] = { 0 };
            vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &model.vertices.buffer, offsets);
            vkCmdBindIndexBuffer(drawCmdBuffers[i], model.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

            // Draw all objects
            for (const auto& object : objects) {
                // Push constants for model matrix
                vkCmdPushConstants(drawCmdBuffers[i], graphics.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &object.transform);

                // Push constants for object color and selection state
                struct {
                    glm::vec3 color;
                    float selected;
                } fragPushConstants;
                
                fragPushConstants.color = object.color;
                fragPushConstants.selected = object.selected ? 1.0f : 0.0f;
                
                vkCmdPushConstants(drawCmdBuffers[i], graphics.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 
                                   sizeof(glm::mat4), sizeof(fragPushConstants), &fragPushConstants);

                // Draw the object
                vkCmdDrawIndexed(drawCmdBuffers[i], model.indexCount, 1, 0, 0, 0);
            }

            // Add UI overlay
            drawUI(drawCmdBuffers[i]);

            // End main render pass
            vkCmdEndRenderPass(drawCmdBuffers[i]);

            // Second render pass: Object picking
            if (i == 0) { // Only need to do this for one command buffer
                // Clear values for pick buffer
                VkClearValue pickClearValues[2];
                pickClearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
                pickClearValues[1].depthStencil = { 1.0f, 0 };

                renderPassBeginInfo.renderPass = pickBuffer.renderPass;
                renderPassBeginInfo.framebuffer = pickBuffer.frameBuffer;
                renderPassBeginInfo.clearValueCount = 2;
                renderPassBeginInfo.pClearValues = pickClearValues;

                vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

                // Set viewport and scissor
                vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
                vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

                // Bind the pick buffer pipeline and descriptor set
                vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pickBuffer.pipelineLayout, 0, 1, &pickBuffer.descriptorSet, 0, nullptr);
                vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pickBuffer.pipeline);

                // Bind the same vertex and index buffers
                vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &model.vertices.buffer, offsets);
                vkCmdBindIndexBuffer(drawCmdBuffers[i], model.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

                // Draw all objects with unique ID colors
                for (const auto& object : objects) {
                    // Push constants for model matrix
                    vkCmdPushConstants(drawCmdBuffers[i], pickBuffer.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &object.transform);

                    // Push constants for object ID color
                    struct {
                        glm::vec3 idColor;
                        float padding;
                    } pickPushConstants;
                    
                    pickPushConstants.idColor = object.getIdColor();
                    pickPushConstants.padding = 0.0f;
                    
                    vkCmdPushConstants(drawCmdBuffers[i], pickBuffer.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 
                                      sizeof(glm::mat4), sizeof(pickPushConstants), &pickPushConstants);

                    // Draw the object
                    vkCmdDrawIndexed(drawCmdBuffers[i], model.indexCount, 1, 0, 0, 0);
                }

                vkCmdEndRenderPass(drawCmdBuffers[i]);
            }

            VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
        }
    }

    // Process object selection based on mouse click
    void pickObject(float mouseX, float mouseY) {
        // Make sure the initial command buffer has been built
        if (prepared) {
            // Get matrices for conversion
            glm::mat4 viewMatrix = camera.matrices.view;
            glm::mat4 projMatrix = camera.matrices.perspective;

            // Create a single-use command buffer for copy operations
            VkCommandBuffer cmdBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

            // Create a host-visible buffer to store the picked pixel
            VkBuffer stagingBuffer;
            VkDeviceMemory stagingMemory;
            VkBufferCreateInfo bufInfo = vks::initializers::bufferCreateInfo();
            bufInfo.size = 4; // RGBA
            bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            VK_CHECK_RESULT(vkCreateBuffer(device, &bufInfo, nullptr, &stagingBuffer));

            VkMemoryRequirements memReqs;
            vkGetBufferMemoryRequirements(device, stagingBuffer, &memReqs);
            VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
            memAlloc.allocationSize = memReqs.size;
            memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &stagingMemory));
            VK_CHECK_RESULT(vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0));

            // Calculate the pixel coordinates to read from the pick buffer
            VkOffset3D offset;
            offset.x = static_cast<int32_t>(mouseX);
            offset.y = static_cast<int32_t>(mouseY);
            offset.z = 0;

            VkExtent3D extent;
            extent.width = 1;
            extent.height = 1;
            extent.depth = 1;

            // Define the image region to read
            VkImageSubresourceLayers subresource = {};
            subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            subresource.baseArrayLayer = 0;
            subresource.mipLevel = 0;
            subresource.layerCount = 1;

            // Copy the pixel from the pick buffer image to the staging buffer
            VkBufferImageCopy region = {};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource = subresource;
            region.imageOffset = offset;
            region.imageExtent = extent;

            vkCmdCopyImageToBuffer(
                cmdBuffer,
                pickBuffer.image,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,  // Must match finalLayout in renderpass
                stagingBuffer,
                1,
                &region);

            // Submit command buffer
            vulkanDevice->flushCommandBuffer(cmdBuffer, queue, true);

            // Get the pixel data
            uint8_t pixelData[4];
            void* data;
            VK_CHECK_RESULT(vkMapMemory(device, stagingMemory, 0, 4, 0, &data));
            memcpy(pixelData, data, 4);
            vkUnmapMemory(device, stagingMemory);

            // Clean up resources
            vkDestroyBuffer(device, stagingBuffer, nullptr);
            vkFreeMemory(device, stagingMemory, nullptr);

            // Convert the pixel data to an object ID
            uint32_t objectId = pixelData[0] | (pixelData[1] << 8) | (pixelData[2] << 16);

            // Reset all selections
            for (auto& object : objects) {
                object.selected = false;
            }

            // Find the object with the picked ID and select it
            if (objectId > 0) { // ID 0 is background
                for (size_t i = 0; i < objects.size(); i++) {
                    if (objects[i].id == objectId) {
                        objects[i].selected = true;
                        selectedObjectIndex = static_cast<int>(i);
                        std::cout << "Selected object: " << objects[i].name << " (ID: " << objectId << ")" << std::endl;
                        break;
                    }
                }
            } else {
                // No object selected
                selectedObjectIndex = -1;
                std::cout << "No object selected" << std::endl;
            }

            // Rebuild command buffers to update the selection visuals
            buildCommandBuffers();
        }
    }

    void prepare() {
        VulkanExampleBase::prepare();
        
        // Check if we have required features
        if (deviceFeatures.fillModeNonSolid) {
            enabledFeatures.fillModeNonSolid = VK_TRUE;
        }

        createSphereModel();
        setupObjects();
        setupPickBuffer();
        prepareUniformBuffers();
        setupDescriptorPool();
        setupDescriptorSetLayout();
        setupDescriptorSets();
        preparePipelines();
        buildCommandBuffers();
        
        prepared = true;
    }

    virtual void render() {
        if (!prepared)
            return;
        renderFrame(); // Use renderFrame() instead of draw()
        if (camera.updated) {
            updateUniformBuffers();
        }
    }

    virtual void viewChanged() {
        updateUniformBuffers();
    }

    virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay) {
        if (overlay->header("Settings")) {
            if (overlay->checkBox("Wireframe", &wireframe)) {
                buildCommandBuffers();
            }
        }
        
        if (overlay->header("Object Info")) {
            if (selectedObjectIndex >= 0 && selectedObjectIndex < objects.size()) {
                const auto& object = objects[selectedObjectIndex];
                overlay->text("Selected: %s", object.name.c_str());
                overlay->text("ID: %d", object.id);
                overlay->text("Position: %.2f, %.2f, %.2f", 
                    object.transform[3][0], object.transform[3][1], object.transform[3][2]);
                overlay->text("Color: %.2f, %.2f, %.2f", 
                    object.color.r, object.color.g, object.color.b);
            } else {
                overlay->text("No object selected");
                overlay->text("Click on an object to select it");
            }
        }
    }

    // Custom mouse handling using the mouseMoved function
    // Since there's no mouseButtons function in the base class, we'll detect clicks in mouseMoved
    bool wasMouseDown = false;
    
    // Need this to track if the event was handled
    bool handled = false;
    
    // Handle mouse move events
    virtual void mouseMoved(double x, double y, bool& handled) override {
        // Store mouse position for handling clicks
        mousePos.x = x;
        mousePos.y = y;
        
        // Check if a click event has happened using mouseState from the base class
        if (mouseState.buttons.left) {
            if (!wasMouseDown) {
                // Mouse button just pressed - pick object
                pickObject(static_cast<float>(x), static_cast<float>(y));
                handled = true;
                wasMouseDown = true;
            }
        } else {
            // Reset state when mouse is released
            wasMouseDown = false;
        }
        
        // Pass to base class
        VulkanExampleBase::mouseMoved(x, y, handled);
    }
    
    // Process input events
    virtual void keyPressed(uint32_t key) override {
        VulkanExampleBase::keyPressed(key);
    }
    
    // Custom mouse position tracking
    struct {
        double x = 0;
        double y = 0;
    } mousePos;
};

VULKAN_EXAMPLE_MAIN()