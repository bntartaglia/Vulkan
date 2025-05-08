/*
* Vulkan Example - Object marqueepicking using a separate render pass and buffer
*
* This example demonstrates how to implement object marqueepicking using a separate rendering pass
* where each object is rendered with a unique color, and then looking up the pixel color
* on mouse click to identify the selected object.
*
* Copyright (C) 2023-2024 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
* New test added by Bruce Tartaglia - objectId marqueemarqueepick (not deep)
*/

/*
+---------------------+
|   Swapchain Images  |
|  (Presentation)     |
+---------------------+
          ^
          |
          | (Rendered by)
          |
+---------------------+
|   Command Buffers   |
|  (Per Swapchain)    |
+---------------------+
          ^
          |
          | (Includes)
          |
+---------------------+       +---------------------+
|   Main Render Pass  |       |  Pick Buffer Pass   |
|  (Scene Rendering)  |       | (Object Selection)  |
+---------------------+       +---------------------+
*/

#include <random>
#include <unordered_set>
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

    // Convert object ID to color (for marqueepick buffer)
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

    // Separate command buffer for marqueepick pass � do not share swapchain draw command buffers
    VkCommandBuffer marqueepickCmdBuffer;
    VkFence marqueepickFence;
    bool dragging = false;
    glm::vec2 dragStart;
    glm::vec2 dragEnd;


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
    } marqueepickBuffer;

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
        title = "Object marqueepicking with ID buffer";
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
        vkDestroyPipeline(device, marqueepickBuffer.pipeline, nullptr);
        vkDestroyPipelineLayout(device, marqueepickBuffer.pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(device, marqueepickBuffer.descriptorSetLayout, nullptr);
        vkDestroyRenderPass(device, marqueepickBuffer.renderPass, nullptr);
        vkDestroyFramebuffer(device, marqueepickBuffer.frameBuffer, nullptr);
        vkDestroyImageView(device, marqueepickBuffer.imageView, nullptr);
        vkDestroyImage(device, marqueepickBuffer.image, nullptr);
        vkFreeMemory(device, marqueepickBuffer.memory, nullptr);

        // Models
        model.vertices.destroy();
        model.indices.destroy();

        // Uniform buffers
        uniformBuffers.scene.destroy();

        // destroy marqueepick buffer fence
        vkDestroyFence(device, marqueepickFence, nullptr);
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

    // Initialize the marqueepick buffer
    void setupPickBuffer() {
        marqueepickBuffer.width = width;
        marqueepickBuffer.height = height;

        // Create the image for the marqueepick buffer
        VkImageCreateInfo imageInfo = vks::initializers::imageCreateInfo();
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.extent.width = marqueepickBuffer.width;
        imageInfo.extent.height = marqueepickBuffer.height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        // Image needs to be both a color attachment (for rendering) and transfer source (for reading)
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        // Set initial layout to undefined
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VK_CHECK_RESULT(vkCreateImage(device, &imageInfo, nullptr, &marqueepickBuffer.image));

        // Allocate memory for the image
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(device, marqueepickBuffer.image, &memReqs);
        VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &marqueepickBuffer.memory));
        VK_CHECK_RESULT(vkBindImageMemory(device, marqueepickBuffer.image, marqueepickBuffer.memory, 0));

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
        viewInfo.image = marqueepickBuffer.image;
        VK_CHECK_RESULT(vkCreateImageView(device, &viewInfo, nullptr, &marqueepickBuffer.imageView));

        // Create render pass
        std::array<VkAttachmentDescription, 2> attachmentDescs = {};

        // Color attachment (marqueepick buffer)
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
        VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &marqueepickBuffer.renderPass));

        // Create framebuffer
        VkImageView attachments[2];
        attachments[0] = marqueepickBuffer.imageView;
        attachments[1] = depthStencil.view;

        VkFramebufferCreateInfo framebufferInfo = vks::initializers::framebufferCreateInfo();
        framebufferInfo.renderPass = marqueepickBuffer.renderPass;
        framebufferInfo.attachmentCount = 2;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = marqueepickBuffer.width;
        framebufferInfo.height = marqueepickBuffer.height;
        framebufferInfo.layers = 1;
        VK_CHECK_RESULT(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &marqueepickBuffer.frameBuffer));
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
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &marqueepickBuffer.descriptorSetLayout));

        // Pipeline layouts
        VkPipelineLayoutCreateInfo pipelineLayoutInfo =
            vks::initializers::pipelineLayoutCreateInfo(&graphics.descriptorSetLayout, 1);
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &graphics.pipelineLayout));

        pipelineLayoutInfo.pSetLayouts = &marqueepickBuffer.descriptorSetLayout;
        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &marqueepickBuffer.pipelineLayout));
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
        allocInfo.pSetLayouts = &marqueepickBuffer.descriptorSetLayout;
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &marqueepickBuffer.descriptorSet));
        
        writeDescriptorSet.dstSet = marqueepickBuffer.descriptorSet;
        
        // Update with only 3 parameters if your framework uses the older API
        vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
    }
    void preparePipelines() {
        // === Input Assembly ===
        // Specifies how primitives are assembled from vertices.
        // Triangle list is the most common primitive topology.
        VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
            vks::initializers::pipelineInputAssemblyStateCreateInfo(
                VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);

        // === Rasterization State ===
        // Describes how geometry is rasterized (e.g., filled or wireframe).
        // No default exists; must be explicitly defined.
        VkPipelineRasterizationStateCreateInfo rasterizationState =
            vks::initializers::pipelineRasterizationStateCreateInfo(
                VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);

        // === Color Blend State ===
        // Controls how fragment output is blended with framebuffer contents.
        // Even if you don�t use blending, this state must be set.
        VkPipelineColorBlendAttachmentState blendAttachmentState =
            vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
        VkPipelineColorBlendStateCreateInfo colorBlendState =
            vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

        // === Depth and Stencil State ===
        // Enables depth testing; stencil test is not used in this example.
        VkPipelineDepthStencilStateCreateInfo depthStencilState =
            vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);

        // === Viewport State ===
        // Describes viewport and scissor count/types.
        // Even if you use dynamic viewport/scissor, this struct is required.
        VkPipelineViewportStateCreateInfo viewportState =
            vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);

        // === Multisampling State ===
        // Used for anti-aliasing. Even if not enabled, must be defined.
        VkPipelineMultisampleStateCreateInfo multisampleState =
            vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);

        // === Dynamic State ===
        // Allows you to change viewport/scissor without recreating the pipeline.
        std::vector<VkDynamicState> dynamicStateEnables = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo dynamicState =
            vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);

        // === Vertex Input State ===
        // Describes how vertex data is laid out in memory and fed to the vertex shader.
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

        // === Shader Stages ===
        // Each graphics pipeline requires at least a vertex and fragment shader.
        std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

        // === Pipeline Create Info ===
        // Master struct that ties everything together and creates the pipeline.
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

        // === Load Vertex Shader (shared across pipelines) ===
        shaderStages[0] = loadShader(getShadersPath() + "marqueepick/sphere.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);

        // === Load Fragment Shader for visible rendering ===
        shaderStages[1] = loadShader(getShadersPath() + "marqueepick/sphere.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &graphics.pipeline));

        // === Load Fragment Shader for object marqueepicking (ID rendering) ===
        pipelineCreateInfo.renderPass = marqueepickBuffer.renderPass;
        shaderStages[1] = loadShader(getShadersPath() + "marqueepick/marqueepicking.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &marqueepickBuffer.pipeline));

        // === Optional: Wireframe pipeline (if supported by hardware) ===
        if (deviceFeatures.fillModeNonSolid) {
            rasterizationState.polygonMode = VK_POLYGON_MODE_LINE;
            rasterizationState.lineWidth = 1.0f;
            pipelineCreateInfo.renderPass = renderPass;
            shaderStages[1] = loadShader(getShadersPath() + "marqueepick/sphere.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
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

    void buildPickCommandBuffer() {
        // Begin the command buffer (no need to reset, we'll overwrite it)
        VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
        cmdBufInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK_RESULT(vkBeginCommandBuffer(marqueepickCmdBuffer, &cmdBufInfo));

        // Clear the marqueepick buffer to pure black - this represents "no object"
        VkClearValue clearValues[2];
        clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
        clearValues[1].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
        renderPassBeginInfo.renderPass = marqueepickBuffer.renderPass;
        renderPassBeginInfo.framebuffer = marqueepickBuffer.frameBuffer;
        renderPassBeginInfo.renderArea.offset = { 0, 0 };
        renderPassBeginInfo.renderArea.extent = { marqueepickBuffer.width, marqueepickBuffer.height };
        renderPassBeginInfo.clearValueCount = 2;
        renderPassBeginInfo.pClearValues = clearValues;

        vkCmdBeginRenderPass(marqueepickCmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
        VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
        vkCmdSetViewport(marqueepickCmdBuffer, 0, 1, &viewport);
        vkCmdSetScissor(marqueepickCmdBuffer, 0, 1, &scissor);

        vkCmdBindDescriptorSets(marqueepickCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, marqueepickBuffer.pipelineLayout, 0, 1, &marqueepickBuffer.descriptorSet, 0, nullptr);
        vkCmdBindPipeline(marqueepickCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, marqueepickBuffer.pipeline);

        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(marqueepickCmdBuffer, 0, 1, &model.vertices.buffer, offsets);
        vkCmdBindIndexBuffer(marqueepickCmdBuffer, model.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

        for (const auto& object : objects) {
            vkCmdPushConstants(marqueepickCmdBuffer, marqueepickBuffer.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &object.transform);

            struct {
                glm::vec3 idColor;
                float pad;
            } push{};
            push.idColor = object.getIdColor();

            vkCmdPushConstants(marqueepickCmdBuffer, marqueepickBuffer.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(glm::mat4), sizeof(push), &push);

            vkCmdDrawIndexed(marqueepickCmdBuffer, model.indexCount, 1, 0, 0, 0);
        }

        vkCmdEndRenderPass(marqueepickCmdBuffer);
        VK_CHECK_RESULT(vkEndCommandBuffer(marqueepickCmdBuffer));
    }

    // Build command buffers for main rendering and marqueepick buffer rendering
    void buildCommandBuffers() {
        VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

        // These clear values are for the main scene
        VkClearValue clearValues[2];
        clearValues[0].color = { { 0.2f, 0.2f, 0.2f, 1.0f } };  // Dark gray background
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

            // End main render pass0kkkkk
            vkCmdEndRenderPass(drawCmdBuffers[i]);
            
            // We don't render the marqueepick buffer as part of the main command buffer anymore
            // This is now done on-demand in the marqueepickObject function

            VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
        }
    }
    
    std::unordered_set<uint32_t> GetObjectIDsFromRect(
        VkRect2D rubberBandRect)
    {
        std::unordered_set<uint32_t> selectedIDs;

        // Calculate image size for region
        VkDeviceSize imageSize = rubberBandRect.extent.width * rubberBandRect.extent.height * 4;

        // Create staging buffer using the framework's helper
        vks::Buffer stagingBuffer;
        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &stagingBuffer,
            imageSize));

        // Begin command buffer for image copy operation
        VkCommandBuffer cmdBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

        // Transition image layout to optimal for transfer
        VkImageMemoryBarrier imageBarrier = vks::initializers::imageMemoryBarrier();
        imageBarrier.image = marqueepickBuffer.image;
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        imageBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageBarrier.subresourceRange.levelCount = 1;
        imageBarrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(
            cmdBuffer,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &imageBarrier);

        // Copy pick image region to buffer
        VkBufferImageCopy region = {};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = { rubberBandRect.offset.x, rubberBandRect.offset.y, 0 };
        region.imageExtent = { rubberBandRect.extent.width, rubberBandRect.extent.height, 1 };

        vkCmdCopyImageToBuffer(
            cmdBuffer,
            marqueepickBuffer.image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            stagingBuffer.buffer,
            1, &region);

        // Transition back to original layout
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        vkCmdPipelineBarrier(
            cmdBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &imageBarrier);

        // Submit and wait for completion
        vulkanDevice->flushCommandBuffer(cmdBuffer, queue, true);

        // Map buffer memory and decode IDs
        uint8_t* pixels;
        VK_CHECK_RESULT(stagingBuffer.map());
        pixels = static_cast<uint8_t*>(stagingBuffer.mapped);

        size_t numPixels = rubberBandRect.extent.width * rubberBandRect.extent.height;

        for (size_t i = 0; i < numPixels; ++i) {
            uint8_t r = pixels[i * 4 + 0];
            uint8_t g = pixels[i * 4 + 1];
            uint8_t b = pixels[i * 4 + 2];

            uint32_t objectID = r | (g << 8) | (b << 16);
            if (objectID != 0)
                selectedIDs.insert(objectID);
        }

        // Cleanup
        stagingBuffer.unmap();
        stagingBuffer.destroy();

        return selectedIDs;
    }

    // Process object selection based on mouse click
    void marqueepickObject(float mouseX, float mouseY) {
        // Make sure the initial command buffer has been built
        if (prepared) {
            std::cout << "Processing marqueepick at: " << mouseX << ", " << mouseY << std::endl;
            
            // Make sure we have up-to-date uniform buffers (camera matrices)
            // This is critical - we need to make sure the scene is rendered with current camera
            uniformData.projection = camera.matrices.perspective;
            uniformData.view = camera.matrices.view;
            memcpy(uniformBuffers.scene.mapped, &uniformData, sizeof(UniformData));
            
            // 1. Render the objects with ID colors to the marqueepick buffer
            // Create and begin command buffer for the marqueepick rendering
            buildPickCommandBuffer();

            // Submit the marqueepick rendering command buffer - wait for it to complete
            VkSubmitInfo submitInfo = vks::initializers::submitInfo();
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &marqueepickCmdBuffer;
            
            // Force the queue to render the marqueepick buffer
            VK_CHECK_RESULT(vkResetFences(device, 1, &marqueepickFence));
            VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, marqueepickFence));
            VK_CHECK_RESULT(vkWaitForFences(device, 1, &marqueepickFence, VK_TRUE, UINT64_MAX));
            
            // 2. Read the pixel color at mouse coordinates
            // Create a staging buffer for the pixel data
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

            // Create a separate command buffer for the copy operation
            VkCommandBuffer copyCmdBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

            // Calculate the pixel coordinates to read from the marqueepick buffer
            VkOffset3D offset;
            offset.x = static_cast<int32_t>(mouseX);
            // Try without Y inversion first since we don't know the actual rendering system
            offset.y = static_cast<int32_t>(mouseY);
            offset.z = 0;

            std::cout << "Reading pixel at: " << offset.x << ", " << offset.y << std::endl;

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

            // Copy the pixel from the marqueepick buffer image to the staging buffer
            VkBufferImageCopy region = {};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource = subresource;
            region.imageOffset = offset;
            region.imageExtent = extent;

            // Record and submit the copy command
            vkCmdCopyImageToBuffer(
                copyCmdBuffer,
                marqueepickBuffer.image,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,  // Must match finalLayout in renderpass
                stagingBuffer,
                1,
                &region);

            // Submit and wait for completion
            vulkanDevice->flushCommandBuffer(copyCmdBuffer, queue);

            // Read the pixel data
            uint8_t pixelData[4] = {0, 0, 0, 0}; // Initialize with zeros
            void* data;
            VK_CHECK_RESULT(vkMapMemory(device, stagingMemory, 0, 4, 0, &data));
            memcpy(pixelData, data, 4);
            vkUnmapMemory(device, stagingMemory);

            // Debug output
            std::cout << "Picked pixel data: [" 
                      << (int)pixelData[0] << ", " 
                      << (int)pixelData[1] << ", " 
                      << (int)pixelData[2] << ", " 
                      << (int)pixelData[3] << "]" << std::endl;

            // Clean up resources
            vkDestroyBuffer(device, stagingBuffer, nullptr);
            vkFreeMemory(device, stagingMemory, nullptr);

            // 3. Convert the pixel data to an object ID
            uint32_t objectId = pixelData[0] | (pixelData[1] << 8) | (pixelData[2] << 16);

            // Reset all selections
            for (auto& object : objects) {
                object.selected = false;
            }

            // Find the object with the marqueepicked ID and select it
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
                std::cout << "No object selected (ID: " << objectId << ")" << std::endl;
            }

            // 4. Rebuild command buffers to update the selection visuals
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

        // Pick Buffer preparation.... (might want to factor into its own function)
        // Allocate marqueepick command buffer
        VkCommandBufferAllocateInfo allocInfo = vks::initializers::commandBufferAllocateInfo(
            vulkanDevice->commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
        VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &allocInfo, &marqueepickCmdBuffer));

        // Create fence for synchronization
        VkFenceCreateInfo fenceInfo = vks::initializers::fenceCreateInfo(VK_FLAGS_NONE);
        VK_CHECK_RESULT(vkCreateFence(device, &fenceInfo, nullptr, &marqueepickFence));
        
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

    void performRubberBandPick(const glm::vec2& start, const glm::vec2& end) {
        std::cout << "Rubber-band pick from (" << start.x << ", " << start.y
            << ") to (" << end.x << ", " << end.y << ")" << std::endl;

        updateUniformBuffers();
        buildPickCommandBuffer();

        VkSubmitInfo submitInfo = vks::initializers::submitInfo();
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &marqueepickCmdBuffer;

        VK_CHECK_RESULT(vkResetFences(device, 1, &marqueepickFence));
        VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, marqueepickFence));
        VK_CHECK_RESULT(vkWaitForFences(device, 1, &marqueepickFence, VK_TRUE, UINT64_MAX));

        // Convert to VkRect2D
        VkRect2D rect;
        rect.offset.x = static_cast<int32_t>(std::min(start.x, end.x));
        rect.offset.y = static_cast<int32_t>(std::min(start.y, end.y));
        rect.extent.width = static_cast<uint32_t>(std::abs(end.x - start.x));
        rect.extent.height = static_cast<uint32_t>(std::abs(end.y - start.y));

        std::unordered_set<uint32_t> selectedIDs = GetObjectIDsFromRect(rect);

        for (auto& obj : objects) {
            obj.selected = selectedIDs.count(obj.id) > 0;
        }

        buildCommandBuffers();
    }

    
    // Handle mouse move events
#if 1

    virtual void mouseMoved(double x, double y, bool& handled) override {
        mousePos.x = x;
        mousePos.y = y;

        bool leftButtonDown = mouseState.buttons.left;

        if (leftButtonDown) {
            if (!wasMouseDown) {
                // Mouse just pressed → start drag
                dragStart = glm::vec2(x, y);
                dragging = true;
                wasMouseDown = true;
                handled = true; // Mark as handled to prevent camera movement
            }
            else if (dragging) {
                // Mouse held down → update drag
                dragEnd = glm::vec2(x, y);
                handled = true; // Mark as handled to prevent camera movement
            }
        }
        else {
            if (wasMouseDown && dragging) {
                // Mouse just released → finalize drag
                dragEnd = glm::vec2(x, y);

                // Only perform pick if actually dragged (distance > small threshold)
                float dragDistance = glm::distance(dragStart, dragEnd);
                if (dragDistance > 3.0f) { // Small threshold to distinguish from clicks
                    performRubberBandPick(dragStart, dragEnd);
                }
                else {
                    // Treat as a normal click
                    marqueepickObject(static_cast<float>(x), static_cast<float>(y));
                }

                handled = true;
            }

            dragging = false;
            wasMouseDown = false;
        }

        // Only pass to the base class if we're not dragging
        if (!dragging) {
            VulkanExampleBase::mouseMoved(x, y, handled);
        }
    }

#else 
    virtual void mouseMoved(double x, double y, bool& handled) override {
        mousePos.x = x;
        mousePos.y = y;

        bool leftButtonDown = mouseState.buttons.left;

        if (leftButtonDown) {
            if (!wasMouseDown) {
                // Mouse just pressed → start drag
                dragStart = glm::vec2(x, y);
                dragging = true;
                wasMouseDown = true;
            }
            else {
                // Mouse held down → update drag
                dragEnd = glm::vec2(x, y);
            }
        }
        else {
            if (wasMouseDown && dragging) {
                // Mouse just released → finalize drag
                dragEnd = glm::vec2(x, y);
                performRubberBandPick(dragStart, dragEnd);
            }

            dragging = false;
            wasMouseDown = false;
        }

        VulkanExampleBase::mouseMoved(x, y, handled);
    }

#endif
    
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