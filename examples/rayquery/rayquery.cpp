/*
* Vulkan Example - Using ray queries for hardware accelerated ray tracing with object picking
*
* Ray queries (aka inline ray tracing) can be used in non-raytracing shaders. This sample makes use of that by
* doing ray traced shadows in a fragment shader and ray traced object picking on mouse click
*
* Copyright (C) 2020-2023 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"
#include "VulkanRaytracingSample.h"
#include <iostream>

class VulkanExample : public VulkanRaytracingSample
{
public:
	glm::vec3 lightPos = glm::vec3();

	struct UniformData {
		glm::mat4 projection;
		glm::mat4 view;
		glm::mat4 model;
		glm::vec3 lightPos;
	} uniformData;
	vks::Buffer uniformBuffer;

	// Shared model that will be used by all objects
	vkglTF::Model sphereModel;

	// Simple manipulator axis structure
	struct Manipulator {
		enum class Mode {
			TRANSLATE,
			ROTATE,
			SCALE
		};
		
		Mode mode = Mode::TRANSLATE;
		
		// Individual axis selection
		bool xAxisSelected = false;
		bool yAxisSelected = false;
		bool zAxisSelected = false;
		
		// Size/scale of manipulator axes
		float axisLength = 1.0f;
		float axisThickness = 0.05f;
		
		// Colors for each axis
		glm::vec3 xAxisColor = glm::vec3(1.0f, 0.0f, 0.0f); // Red
		glm::vec3 yAxisColor = glm::vec3(0.0f, 1.0f, 0.0f); // Green
		glm::vec3 zAxisColor = glm::vec3(0.0f, 0.0f, 1.0f); // Blue
		
		// Position of manipulator (usually same as selected object)
		glm::vec3 position = glm::vec3(0.0f);
		
		// Active/visible flag
		bool active = false;
		
		// Draw manipulator gizmo
		void draw(VkCommandBuffer cmdBuffer) {
			// Actual implementation will draw simple colored axes
		}
		
		// Mouse interaction - returns true if handled interaction
		bool handleMouseMove(const glm::vec3& rayOrigin, const glm::vec3& rayDir, glm::vec3& objectPosition) {
			if (!active) return false;
			
			// Check if any axis is already selected
			if (xAxisSelected || yAxisSelected || zAxisSelected) {
				// Apply movement based on ray intersection with axis planes
				if (xAxisSelected) {
					// Project movement onto X axis
					objectPosition.x += computeMovementAmount(rayOrigin, rayDir, glm::vec3(1.0f, 0.0f, 0.0f));
				}
				else if (yAxisSelected) {
					// Project movement onto Y axis
					objectPosition.y += computeMovementAmount(rayOrigin, rayDir, glm::vec3(0.0f, 1.0f, 0.0f));
				}
				else if (zAxisSelected) {
					// Project movement onto Z axis
					objectPosition.z += computeMovementAmount(rayOrigin, rayDir, glm::vec3(0.0f, 0.0f, 1.0f));
				}
				return true;
			}
			
			return false;
		}
		
		// Compute movement amount along axis based on ray direction
		float computeMovementAmount(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& axis) {
			// Simplified version - approximate movement amount using dot product
			return glm::dot(rayDir, axis) * 0.1f; // Scale factor for more controlled movement
		}
		
		// Check if ray intersects with an axis - returns true if intersection found
		bool checkAxisIntersection(const glm::vec3& rayOrigin, const glm::vec3& rayDir) {
			if (!active) return false;
			
			// Reset all selections
			xAxisSelected = false;
			yAxisSelected = false;
			zAxisSelected = false;
			
			// Calculate distances from ray to each axis
			float distX = rayDistanceToAxis(rayOrigin, rayDir, position, position + glm::vec3(axisLength, 0.0f, 0.0f));
			float distY = rayDistanceToAxis(rayOrigin, rayDir, position, position + glm::vec3(0.0f, axisLength, 0.0f));
			float distZ = rayDistanceToAxis(rayOrigin, rayDir, position, position + glm::vec3(0.0f, 0.0f, axisLength));
			
			// Threshold for selecting an axis
			const float threshold = axisThickness * 2.0f;
			
			// Select closest axis within threshold
			if (distX < threshold && distX < distY && distX < distZ) {
				xAxisSelected = true;
				return true;
			}
			else if (distY < threshold && distY < distX && distY < distZ) {
				yAxisSelected = true;
				return true;
			}
			else if (distZ < threshold && distZ < distX && distZ < distY) {
				zAxisSelected = true;
				return true;
			}
			
			return false;
		}
		
		// Compute distance from ray to axis line segment
		float rayDistanceToAxis(const glm::vec3& rayOrigin, const glm::vec3& rayDir, 
								const glm::vec3& axisStart, const glm::vec3& axisEnd) {
			// Calculate the axis direction
			glm::vec3 axisDir = glm::normalize(axisEnd - axisStart);
			
			// Calculate the vector perpendicular to both ray and axis
			glm::vec3 crossProduct = glm::cross(rayDir, axisDir);
			
			// If cross product is zero, ray and axis are parallel
			float crossLength = glm::length(crossProduct);
			if (crossLength < 0.00001f) return FLT_MAX;
			
			// Calculate distance
			glm::vec3 axisToRay = rayOrigin - axisStart;
			float distance = glm::abs(glm::dot(axisToRay, crossProduct)) / crossLength;
			
			// Check if the closest point is within the axis segment
			float t = glm::dot(axisDir, glm::cross(axisToRay, rayDir) / crossLength);
			if (t < 0.0f || t > glm::length(axisEnd - axisStart)) {
				return FLT_MAX; // Outside of axis segment
			}
			
			return distance;
		}
	};

	// Structure to hold object data (for object picking)
	struct PickableObject {
		glm::vec3 position;
		glm::mat4 matrix;
		uint32_t id;
		bool selected = false;
		glm::vec3 color;       // Base color of the object
		glm::vec3 selectColor; // Color when selected
		std::string name;      // Name of the object for display
	};

	// Vector of scene objects
	std::vector<PickableObject> objects;
	// Main scene model
	vkglTF::Model mainScene;

	VkPipeline pipeline{ VK_NULL_HANDLE };
	VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
	VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };
	VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };
	
	// Color for rendering objects
	glm::vec4 objColor = glm::vec4(1.0f);

	VulkanRaytracingSample::AccelerationStructure bottomLevelAS{};    // Main scene BLAS
	VulkanRaytracingSample::AccelerationStructure objectBLAS{};       // Objects BLAS (shared by all sphere objects)
	VulkanRaytracingSample::AccelerationStructure topLevelAS{};       // Top level acceleration structure

	VkPhysicalDeviceRayQueryFeaturesKHR enabledRayQueryFeatures{};

	// Picking information
	uint32_t selectedObjectID = UINT32_MAX;
	
	// Manipulator for transforming objects
	Manipulator manipulator;

	VulkanExample() : VulkanRaytracingSample()
	{
		title = "Ray-Based Object Picking with Manipulators";  // Updated title for manipulators
		camera.type = Camera::CameraType::lookat;
		timerSpeed *= 0.25f;
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 512.0f);
		// Position camera to look up at our spheres and outside the box
		camera.setRotation(glm::vec3(-15.0f, 0.0f, 0.0f)); // Look up slightly
		camera.setTranslation(glm::vec3(0.0f, 0.0f, 5.0f)); // Pull back to be outside the box
		rayQueryOnly = true;
		enableExtensions();
		enabledDeviceExtensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
	}

	~VulkanExample()
	{
		if (device) {
			vkDestroyPipeline(device, pipeline, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
			uniformBuffer.destroy();
			deleteAccelerationStructure(bottomLevelAS);
			deleteAccelerationStructure(objectBLAS);
			deleteAccelerationStructure(topLevelAS);
		}
	}

	void loadAssets()
	{
		std::cout << "LOADING ASSETS - THIS IS FROM THE MODIFIED CODE" << std::endl;
		
		vkglTF::memoryPropertyFlags = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;
		
		std::cout << "Loading main scene..." << std::endl;
		mainScene.loadFromFile(getAssetPath() + "models/vulkanscene_shadow.gltf", vulkanDevice, queue, glTFLoadingFlags);
		
		// Load sphere model for pickable objects
		std::cout << "Loading sphere model..." << std::endl;
		sphereModel.loadFromFile(getAssetPath() + "models/sphere.gltf", vulkanDevice, queue, glTFLoadingFlags);
		
		// Create pickable objects
		std::cout << "Setting up scene with pickable objects..." << std::endl;
		setupScene();
		
		std::cout << "Assets loaded successfully!" << std::endl;
	}
	
	// Set up multiple objects for our scene
	void setupScene()
	{
		// Create EXTREMELY VISIBLE objects with HUGE SIZE DIFFERENCES
		
		// First object - "TINY COW" - center position
		{
			PickableObject object;
			object.position = glm::vec3(0.0f, 5.0f, -15.0f); // Above and in front of the camera
			object.matrix = glm::scale(
				glm::translate(glm::mat4(1.0f), object.position),
				glm::vec3(1.0f) // Normal size
			);
			object.id = 0;
			object.selected = false;
			object.color = glm::vec3(1.0f, 0.0f, 0.0f); // RED
			object.selectColor = glm::vec3(1.0f, 1.0f, 0.0f); // YELLOW when selected
			object.name = "TINY COW";
			objects.push_back(object);
		}
		
		// Second object - "MEDIUM DOG" - right side
		{
			PickableObject object;
			object.position = glm::vec3(8.0f, 5.0f, -15.0f); // Right side (farther apart)
			object.matrix = glm::scale(
				glm::translate(glm::mat4(1.0f), object.position),
				glm::vec3(2.0f) // 2x size
			);
			object.id = 1;
			object.selected = false;
			object.color = glm::vec3(0.0f, 1.0f, 0.0f); // GREEN
			object.selectColor = glm::vec3(0.0f, 1.0f, 1.0f); // CYAN when selected
			object.name = "MEDIUM DOG";
			objects.push_back(object);
		}
		
		// Third object - "LARGE HORSE" - left side
		{
			PickableObject object;
			object.position = glm::vec3(-8.0f, 5.0f, -15.0f); // Left side (farther apart)
			object.matrix = glm::scale(
				glm::translate(glm::mat4(1.0f), object.position),
				glm::vec3(3.0f) // 3x size
			);
			object.id = 2;
			object.selected = false;
			object.color = glm::vec3(0.0f, 0.0f, 1.0f); // BLUE
			object.selectColor = glm::vec3(1.0f, 0.5f, 1.0f); // PINK when selected
			object.name = "LARGE HORSE";
			objects.push_back(object);
		}
		
		std::cout << "Created " << objects.size() << " objects with DIFFERENT SIZES:" << std::endl;
		std::cout << "  - TINY COW (1x size)" << std::endl;
		std::cout << "  - MEDIUM DOG (2x size)" << std::endl;
		std::cout << "  - LARGE HORSE (3x size)" << std::endl;
	}

	/*
		Create the bottom level acceleration structure contains the scene's actual geometry (vertices, triangles)
	*/
	void createBottomLevelAccelerationStructure()
	{
		// First, create BLAS for the main scene
		VkDeviceOrHostAddressConstKHR vertexBufferDeviceAddress{};
		VkDeviceOrHostAddressConstKHR indexBufferDeviceAddress{};

		vertexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(mainScene.vertices.buffer);
		indexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(mainScene.indices.buffer);

		uint32_t numTriangles = static_cast<uint32_t>(mainScene.indices.count) / 3;
		
		// Build
		VkAccelerationStructureGeometryKHR accelerationStructureGeometry = vks::initializers::accelerationStructureGeometryKHR();
		accelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
		accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
		accelerationStructureGeometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
		accelerationStructureGeometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
		accelerationStructureGeometry.geometry.triangles.vertexData = vertexBufferDeviceAddress;
		accelerationStructureGeometry.geometry.triangles.maxVertex = mainScene.vertices.count - 1;
		accelerationStructureGeometry.geometry.triangles.vertexStride = sizeof(vkglTF::Vertex);
		accelerationStructureGeometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
		accelerationStructureGeometry.geometry.triangles.indexData = indexBufferDeviceAddress;
		accelerationStructureGeometry.geometry.triangles.transformData.deviceAddress = 0;
		accelerationStructureGeometry.geometry.triangles.transformData.hostAddress = nullptr;

		// Get size info
		VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo = vks::initializers::accelerationStructureBuildGeometryInfoKHR();
		accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		accelerationStructureBuildGeometryInfo.geometryCount = 1;
		accelerationStructureBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;

		VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo = vks::initializers::accelerationStructureBuildSizesInfoKHR();
		vkGetAccelerationStructureBuildSizesKHR(
			device,
			VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
			&accelerationStructureBuildGeometryInfo,
			&numTriangles,
			&accelerationStructureBuildSizesInfo);

		createAccelerationStructure(bottomLevelAS, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, accelerationStructureBuildSizesInfo);

		// Create a small scratch buffer used during build of the bottom level acceleration structure
		ScratchBuffer scratchBuffer = createScratchBuffer(accelerationStructureBuildSizesInfo.buildScratchSize);

		VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo = vks::initializers::accelerationStructureBuildGeometryInfoKHR();
		accelerationBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		accelerationBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		accelerationBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		accelerationBuildGeometryInfo.dstAccelerationStructure = bottomLevelAS.handle;
		accelerationBuildGeometryInfo.geometryCount = 1;
		accelerationBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;
		accelerationBuildGeometryInfo.scratchData.deviceAddress = scratchBuffer.deviceAddress;

		VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo{};
		accelerationStructureBuildRangeInfo.primitiveCount = numTriangles;
		accelerationStructureBuildRangeInfo.primitiveOffset = 0;
		accelerationStructureBuildRangeInfo.firstVertex = 0;
		accelerationStructureBuildRangeInfo.transformOffset = 0;
		std::vector<VkAccelerationStructureBuildRangeInfoKHR*> accelerationBuildStructureRangeInfos = { &accelerationStructureBuildRangeInfo };

		// Build the acceleration structure on the device via a one-time command buffer submission
		VkCommandBuffer commandBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		vkCmdBuildAccelerationStructuresKHR(
			commandBuffer,
			1,
			&accelerationBuildGeometryInfo,
			accelerationBuildStructureRangeInfos.data());
		vulkanDevice->flushCommandBuffer(commandBuffer, queue);

		deleteScratchBuffer(scratchBuffer);
		
		// Now create BLAS for the sphere model used by all objects
		if (objects.size() > 0) {
			vertexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(sphereModel.vertices.buffer);
			indexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(sphereModel.indices.buffer);

			numTriangles = static_cast<uint32_t>(sphereModel.indices.count) / 3;
			
			// Build
			accelerationStructureGeometry = vks::initializers::accelerationStructureGeometryKHR();
			accelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
			accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
			accelerationStructureGeometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
			accelerationStructureGeometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
			accelerationStructureGeometry.geometry.triangles.vertexData = vertexBufferDeviceAddress;
			accelerationStructureGeometry.geometry.triangles.maxVertex = sphereModel.vertices.count - 1;
			accelerationStructureGeometry.geometry.triangles.vertexStride = sizeof(vkglTF::Vertex);
			accelerationStructureGeometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
			accelerationStructureGeometry.geometry.triangles.indexData = indexBufferDeviceAddress;
			accelerationStructureGeometry.geometry.triangles.transformData.deviceAddress = 0;
			accelerationStructureGeometry.geometry.triangles.transformData.hostAddress = nullptr;

			// Get size info
			accelerationStructureBuildGeometryInfo = vks::initializers::accelerationStructureBuildGeometryInfoKHR();
			accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
			accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
			accelerationStructureBuildGeometryInfo.geometryCount = 1;
			accelerationStructureBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;

			accelerationStructureBuildSizesInfo = vks::initializers::accelerationStructureBuildSizesInfoKHR();
			vkGetAccelerationStructureBuildSizesKHR(
				device,
				VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
				&accelerationStructureBuildGeometryInfo,
				&numTriangles,
				&accelerationStructureBuildSizesInfo);

			// Create object BLAS
			createAccelerationStructure(objectBLAS, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, accelerationStructureBuildSizesInfo);

			// Reuse scratch buffer
			scratchBuffer = createScratchBuffer(accelerationStructureBuildSizesInfo.buildScratchSize);

			accelerationBuildGeometryInfo = vks::initializers::accelerationStructureBuildGeometryInfoKHR();
			accelerationBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
			accelerationBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
			accelerationBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
			accelerationBuildGeometryInfo.dstAccelerationStructure = objectBLAS.handle;
			accelerationBuildGeometryInfo.geometryCount = 1;
			accelerationBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;
			accelerationBuildGeometryInfo.scratchData.deviceAddress = scratchBuffer.deviceAddress;

			accelerationStructureBuildRangeInfo = {};
			accelerationStructureBuildRangeInfo.primitiveCount = numTriangles;
			accelerationStructureBuildRangeInfo.primitiveOffset = 0;
			accelerationStructureBuildRangeInfo.firstVertex = 0;
			accelerationStructureBuildRangeInfo.transformOffset = 0;
			accelerationBuildStructureRangeInfos = { &accelerationStructureBuildRangeInfo };

			// Build the acceleration structure on the device
			commandBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
			vkCmdBuildAccelerationStructuresKHR(
				commandBuffer,
				1,
				&accelerationBuildGeometryInfo,
				accelerationBuildStructureRangeInfos.data());
			vulkanDevice->flushCommandBuffer(commandBuffer, queue);

			// Get device address
			VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{};
			accelerationDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
			accelerationDeviceAddressInfo.accelerationStructure = objectBLAS.handle;
			objectBLAS.deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(device, &accelerationDeviceAddressInfo);

			deleteScratchBuffer(scratchBuffer);
		}
	}

	/*
		The top level acceleration structure contains the scene's object instances
	*/
	void createTopLevelAccelerationStructure()
	{
		// Create a vector of all instances in the scene
		std::vector<VkAccelerationStructureInstanceKHR> instances;
		
		// First, add the main scene to the TLAS
		VkTransformMatrixKHR transformMatrix = {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f };

		VkAccelerationStructureInstanceKHR mainSceneInstance{};
		mainSceneInstance.transform = transformMatrix;
		mainSceneInstance.instanceCustomIndex = UINT32_MAX; // Use UINT32_MAX to indicate this is not a pickable object
		mainSceneInstance.mask = 0xFF;
		mainSceneInstance.instanceShaderBindingTableRecordOffset = 0;
		mainSceneInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		mainSceneInstance.accelerationStructureReference = bottomLevelAS.deviceAddress;
		
		instances.push_back(mainSceneInstance);
		
		// Now add all objects to the TLAS
		for (uint32_t i = 0; i < objects.size(); i++) {
			VkTransformMatrixKHR objTransform = {
				1.0f, 0.0f, 0.0f, objects[i].position.x,
				0.0f, 1.0f, 0.0f, objects[i].position.y,
				0.0f, 0.0f, 1.0f, objects[i].position.z
			};
			
			VkAccelerationStructureInstanceKHR instance{};
			instance.transform = objTransform;
			instance.instanceCustomIndex = objects[i].id; // Set the object's ID for picking
			instance.mask = 0xFF;
			instance.instanceShaderBindingTableRecordOffset = 0;
			instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
			instance.accelerationStructureReference = objectBLAS.deviceAddress; // Use the shared BLAS
			
			instances.push_back(instance);
		}

		std::cout << "Added " << instances.size() << " instances to TLAS" << std::endl;

		// Buffer for instance data
		vks::Buffer instancesBuffer;
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&instancesBuffer,
			instances.size() * sizeof(VkAccelerationStructureInstanceKHR),
			instances.data()));

		VkDeviceOrHostAddressConstKHR instanceDataDeviceAddress{};
		instanceDataDeviceAddress.deviceAddress = getBufferDeviceAddress(instancesBuffer.buffer);

		VkAccelerationStructureGeometryKHR accelerationStructureGeometry = vks::initializers::accelerationStructureGeometryKHR();
		accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
		accelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
		accelerationStructureGeometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
		accelerationStructureGeometry.geometry.instances.arrayOfPointers = VK_FALSE;
		accelerationStructureGeometry.geometry.instances.data = instanceDataDeviceAddress;

		// Get size info
		VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo = vks::initializers::accelerationStructureBuildGeometryInfoKHR();
		accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		accelerationStructureBuildGeometryInfo.geometryCount = 1;
		accelerationStructureBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;

		uint32_t primitive_count = static_cast<uint32_t>(instances.size());

		VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo = vks::initializers::accelerationStructureBuildSizesInfoKHR();
		vkGetAccelerationStructureBuildSizesKHR(
			device,
			VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
			&accelerationStructureBuildGeometryInfo,
			&primitive_count,
			&accelerationStructureBuildSizesInfo);

		createAccelerationStructure(topLevelAS, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, accelerationStructureBuildSizesInfo);

		// Create a small scratch buffer used during build of the top level acceleration structure
		ScratchBuffer scratchBuffer = createScratchBuffer(accelerationStructureBuildSizesInfo.buildScratchSize);

		VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo = vks::initializers::accelerationStructureBuildGeometryInfoKHR();
		accelerationBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		accelerationBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		accelerationBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		accelerationBuildGeometryInfo.dstAccelerationStructure = topLevelAS.handle;
		accelerationBuildGeometryInfo.geometryCount = 1;
		accelerationBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;
		accelerationBuildGeometryInfo.scratchData.deviceAddress = scratchBuffer.deviceAddress;

		VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo{};
		accelerationStructureBuildRangeInfo.primitiveCount = primitive_count;
		accelerationStructureBuildRangeInfo.primitiveOffset = 0;
		accelerationStructureBuildRangeInfo.firstVertex = 0;
		accelerationStructureBuildRangeInfo.transformOffset = 0;
		std::vector<VkAccelerationStructureBuildRangeInfoKHR*> accelerationBuildStructureRangeInfos = { &accelerationStructureBuildRangeInfo };

		// Build the acceleration structure on the device via a one-time command buffer submission
		VkCommandBuffer commandBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		vkCmdBuildAccelerationStructuresKHR(
			commandBuffer,
			1,
			&accelerationBuildGeometryInfo,
			accelerationBuildStructureRangeInfos.data());
		vulkanDevice->flushCommandBuffer(commandBuffer, queue);

		deleteScratchBuffer(scratchBuffer);
		instancesBuffer.destroy();
	}

	void buildCommandBuffers()
	{
		// Print a very visible message when building command buffers
		std::cout << "*** BUILDING COMMAND BUFFERS ***" << std::endl;
		
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		VkClearValue clearValues[2];
		VkViewport viewport;
		VkRect2D scissor;

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
		{
			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

			clearValues[0].color = { { 0.0f, 0.0f, 0.2f, 1.0f } };;
			clearValues[1].depthStencil = { 1.0f, 0 };

			VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
			renderPassBeginInfo.renderPass = renderPass;
			renderPassBeginInfo.framebuffer = frameBuffers[i];
			renderPassBeginInfo.renderArea.extent.width = width;
			renderPassBeginInfo.renderArea.extent.height = height;
			renderPassBeginInfo.clearValueCount = 2;
			renderPassBeginInfo.pClearValues = clearValues;

			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			scissor = vks::initializers::rect2D(width, height, 0, 0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			// Draw main scene first
			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			mainScene.draw(drawCmdBuffers[i]);
			
			// Draw all pickable objects with debug info
			std::cout << "Drawing " << objects.size() << " pickable objects:" << std::endl;
			
			for (size_t objIndex = 0; objIndex < objects.size(); objIndex++) {
				auto& obj = objects[objIndex];
				std::cout << "  Drawing object " << obj.id << " at position " 
					<< obj.position.x << ", " 
					<< obj.position.y << ", " 
					<< obj.position.z << std::endl;
				
				// Create a transform for this object
				glm::mat4 objMatrix;
				
				if (obj.selected) {
					// Move selected objects UP by 5 units to make it extremely obvious
					glm::vec3 elevatedPos = obj.position;
					elevatedPos.y += 5.0f; // Move up 5 units when selected
					
					// Also apply some animation to make it extremely obvious
					float bounce = sin(timer * 10.0f) * 0.5f; // Oscillate up/down
					elevatedPos.y += bounce;
					
					// Create the matrix
					objMatrix = glm::translate(glm::mat4(1.0f), elevatedPos);
					
					// Set selection color
					objColor = glm::vec4(obj.selectColor, 1.0f);
					
					std::cout << "=====================================================" << std::endl;
					std::cout << "DRAWING SELECTED OBJECT: " << obj.name << " (ID: " << obj.id << ")" << std::endl;
					std::cout << "MAKING IT 5X LARGER IN RENDER PASS" << std::endl;
					std::cout << "POSITION: " << obj.position.x << ", " << obj.position.y << ", " << obj.position.z << std::endl;
					std::cout << "=====================================================" << std::endl;
				} else {
					// Create matrix for normal position
					objMatrix = glm::translate(glm::mat4(1.0f), obj.position);
					
					// Use normal color for unselected objects
					objColor = glm::vec4(obj.color, 1.0f);
					
					// Also print unselected objects for debugging
					std::cout << "Drawing normal object: " << obj.name << " (ID: " << obj.id << ")" << std::endl;
				}
				
				// We can't use fragment shader push constants due to shader limitations
				// So we'll only apply the transform matrix
				vkCmdPushConstants(drawCmdBuffers[i], pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &objMatrix);
				
				// Draw the object
				sphereModel.draw(drawCmdBuffers[i]);
			}

			VulkanExampleBase::drawUI(drawCmdBuffers[i]);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}
	}

	void setupDescriptors()
	{
		// Pool
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1)
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 1);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		// Layout
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			// Binding 0 : Vertex shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),
			// Binding 1: Acceleration structure
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

		// Sets
		std::vector<VkWriteDescriptorSet> writeDescriptorSets;

		// Debug display
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

		// Scene rendering with shadow map applied
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));
		writeDescriptorSets = {
			// Binding 0 : Vertex shader uniform buffer
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffer.descriptor)
		};

		// The fragment needs access to the ray tracing acceleration structure, so we pass it as a descriptor

		// As this isn't part of Vulkan's core, we need to pass this informationen via pNext chaining
		VkWriteDescriptorSetAccelerationStructureKHR descriptorAccelerationStructureInfo = vks::initializers::writeDescriptorSetAccelerationStructureKHR();
		descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
		descriptorAccelerationStructureInfo.pAccelerationStructures = &topLevelAS.handle;

		VkWriteDescriptorSet accelerationStructureWrite{};
		accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		// The specialized acceleration structure descriptor has to be chained
		accelerationStructureWrite.pNext = &descriptorAccelerationStructureInfo;
		accelerationStructureWrite.dstSet = descriptorSet;
		accelerationStructureWrite.dstBinding = 1;
		accelerationStructureWrite.descriptorCount = 1;
		accelerationStructureWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

		writeDescriptorSets.push_back(accelerationStructureWrite);
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}

	void preparePipelines()
	{
		// Add push constants for object matrix for positioning objects
		VkPushConstantRange pushConstantRange{};
		pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(glm::mat4);
		
		// Layout
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
		pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
		pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

		// Pipeline
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationStateCI = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendStateCI = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportStateCI = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleStateCI = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicStateCI = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(pipelineLayout, renderPass, 0);
		pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
		pipelineCI.pRasterizationState = &rasterizationStateCI;
		pipelineCI.pColorBlendState = &colorBlendStateCI;
		pipelineCI.pMultisampleState = &multisampleStateCI;
		pipelineCI.pViewportState = &viewportStateCI;
		pipelineCI.pDepthStencilState = &depthStencilStateCI;
		pipelineCI.pDynamicState = &dynamicStateCI;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();

		// Scene rendering with ray traced shadows applied
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position, vkglTF::VertexComponent::UV, vkglTF::VertexComponent::Color, vkglTF::VertexComponent::Normal });
		rasterizationStateCI.cullMode = VK_CULL_MODE_BACK_BIT;
		// Use the original shaders
		shaderStages[0] = loadShader(getShadersPath() + "rayquery/scene.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "rayquery/scene.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));
	}


	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Scene vertex shader uniform buffer block
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffer,
			sizeof(UniformData)));

		// Map persistent
		VK_CHECK_RESULT(uniformBuffer.map());

		updateLight();
		updateUniformBuffers();
	}

	void updateLight()
	{
		// Animate the light source
		lightPos.x = cos(glm::radians(timer * 360.0f)) * 40.0f;
		lightPos.y = -50.0f + sin(glm::radians(timer * 360.0f)) * 20.0f;
		lightPos.z = 25.0f + sin(glm::radians(timer * 360.0f)) * 5.0f;
	}

	void updateUniformBuffers()
	{
		uniformData.projection = camera.matrices.perspective;
		uniformData.view = camera.matrices.view;
		uniformData.model = glm::mat4(1.0f);
		uniformData.lightPos = lightPos;
		memcpy(uniformBuffer.mapped, &uniformData, sizeof(UniformData));
	}

	void getEnabledFeatures()
	{
		// Enable features required for ray tracing using feature chaining via pNext		
		enabledBufferDeviceAddresFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
		enabledBufferDeviceAddresFeatures.bufferDeviceAddress = VK_TRUE;

		enabledAccelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
		enabledAccelerationStructureFeatures.accelerationStructure = VK_TRUE;
		enabledAccelerationStructureFeatures.pNext = &enabledBufferDeviceAddresFeatures;

		enabledRayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
		enabledRayQueryFeatures.rayQuery = VK_TRUE;
		enabledRayQueryFeatures.pNext = &enabledAccelerationStructureFeatures;

		deviceCreatepNextChain = &enabledRayQueryFeatures;
	}

	void draw()
	{
		VulkanExampleBase::prepareFrame();
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
		VulkanExampleBase::submitFrame();
	}
	
	// Get ray from camera through screen point
	void getRayFromScreenPoint(float screenX, float screenY, glm::vec3& rayOrigin, glm::vec3& rayDirection)
	{
		// Convert to normalized device coordinates (NDC)
		float ndcX = (2.0f * screenX) / width - 1.0f;
		float ndcY = 1.0f - (2.0f * screenY) / height;
		
		// Create ray in clip space
		glm::vec4 rayClip = glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
		
		// Transform to view space
		glm::mat4 invProj = glm::inverse(camera.matrices.perspective);
		glm::vec4 rayView = invProj * rayClip;
		rayView = glm::vec4(rayView.x, rayView.y, -1.0f, 0.0f);
		
		// Transform to world space
		glm::mat4 invView = glm::inverse(camera.matrices.view);
		glm::vec4 rayWorld = invView * rayView;
		
		rayOrigin = camera.position;
		rayDirection = glm::normalize(glm::vec3(rayWorld));
	}
	
	// Cast a ray from the camera through the mouse position to pick objects
	void pickObject(float mouseX, float mouseY)
	{
		std::cout << "Starting picking operation at screen position: " << mouseX << ", " << mouseY << std::endl;
		std::cout << "Camera position: " << camera.position.x << ", " << camera.position.y << ", " << camera.position.z << std::endl;
		
		// Get ray from screen point
		glm::vec3 rayOrigin, rayDirection;
		getRayFromScreenPoint(mouseX, mouseY, rayOrigin, rayDirection);
		
		std::cout << "Ray direction: " << rayDirection.x << ", " << rayDirection.y << ", " << rayDirection.z << std::endl;
		
		// First, check if manipulator is active and if it handles the interaction
		if (manipulator.active) {
			// Check if ray intersects with manipulator axes
			if (manipulator.checkAxisIntersection(rayOrigin, rayDirection)) {
				std::cout << "Manipulator axis selected" << std::endl;
				// Handled by manipulator - don't do object selection
				return;
			}
		}
		
		// Now trace the ray against our scene using ray queries
		// This would normally be done in a compute shader for efficient GPU-based picking
		// For this example, we're simulating the ray trace here
		
		// Reset selection
		if (selectedObjectID != UINT32_MAX && selectedObjectID < objects.size()) {
			objects[selectedObjectID].selected = false;
			
			// Deactivate manipulator when selection changes
			manipulator.active = false;
			manipulator.xAxisSelected = false;
			manipulator.yAxisSelected = false;
			manipulator.zAxisSelected = false;
		}
		selectedObjectID = UINT32_MAX;
		
		// Simple ray-sphere intersection for our objects
		float closestHit = FLT_MAX;
		
		std::cout << "Testing against " << objects.size() << " objects" << std::endl;
		
		for (uint32_t i = 0; i < objects.size(); i++) {
			// For simplicity, use sphere intersection
			float radius = 0.5f; // Sphere radius
			glm::vec3 sphereCenter = objects[i].position;
			
			// Ray-sphere intersection
			glm::vec3 L = sphereCenter - rayOrigin;
			float tca = glm::dot(L, rayDirection);
			if (tca < 0) {
				std::cout << "Object " << i << " behind ray origin" << std::endl;
				continue; // Behind the ray origin
			}
			
			float d2 = glm::dot(L, L) - tca * tca;
			float radius2 = radius * radius;
			if (d2 > radius2) {
				std::cout << "Object " << i << " no intersection (d2=" << d2 << ", radius2=" << radius2 << ")" << std::endl;
				continue; // No intersection
			}
			
			float thc = sqrt(radius2 - d2);
			float t0 = tca - thc;
			float t1 = tca + thc;
			
			float t = (t0 < 0) ? t1 : t0; // We want the closest hit in front of the ray
			
			std::cout << "Object " << i << " intersection at distance " << t << std::endl;
			
			if (t > 0 && t < closestHit) {
				closestHit = t;
				selectedObjectID = i;
				std::cout << "  New closest hit" << std::endl;
			}
		}
		
		// Mark the selected object
		if (selectedObjectID != UINT32_MAX) {
			objects[selectedObjectID].selected = true;
			
			// Activate and position manipulator at selected object
			manipulator.active = true;
			manipulator.position = objects[selectedObjectID].position;
			
			// Make selection very obvious with console output
			std::cout << "=================================================" << std::endl;
			std::cout << "SELECTED OBJECT ID: " << selectedObjectID << std::endl;
			std::cout << "Position: "
					  << objects[selectedObjectID].position.x << ", "
					  << objects[selectedObjectID].position.y << ", "
					  << objects[selectedObjectID].position.z << std::endl;
			std::cout << "=================================================" << std::endl;
					  
			// Force command buffer rebuild to update visuals
			buildCommandBuffers();
		}
		else {
			std::cout << "No object selected" << std::endl;
			
			// Deactivate manipulator when no selection
			manipulator.active = false;
		}
	}
	
	// Handle manipulator dragging
	void handleManipulatorDrag(float mouseX, float mouseY)
	{
		if (!manipulator.active || selectedObjectID == UINT32_MAX) {
			return;
		}
		
		// Get ray from screen point
		glm::vec3 rayOrigin, rayDirection;
		getRayFromScreenPoint(mouseX, mouseY, rayOrigin, rayDirection);
		
		// Let manipulator handle the movement
		if (manipulator.handleMouseMove(rayOrigin, rayDirection, objects[selectedObjectID].position)) {
			// Object moved, update manipulator position too
			manipulator.position = objects[selectedObjectID].position;
			
			// Rebuild command buffers to update visuals
			buildCommandBuffers();
			
			std::cout << "Object moved to: "
					  << objects[selectedObjectID].position.x << ", "
					  << objects[selectedObjectID].position.y << ", "
					  << objects[selectedObjectID].position.z << std::endl;
		}
	}

	void prepare()
	{
		VulkanRaytracingSample::prepare();
		loadAssets();
		prepareUniformBuffers();
		createBottomLevelAccelerationStructure();
		createTopLevelAccelerationStructure();
		setupDescriptors();
		preparePipelines();
		buildCommandBuffers();
		prepared = true;
	}

	// Override for handling mouse clicks
	void mouseMoved(double x, double y, bool &handled) override
	{
		// Check if left mouse button was JUST pressed (rather than held down)
		static bool wasPressed = false;
		static bool isDragging = false;
		static float lastX = 0.0f;
		static float lastY = 0.0f;
		
		// Get current position as floats
		float currentX = static_cast<float>(x);
		float currentY = static_cast<float>(y);
		
		// Handle initial click
		if (mouseState.buttons.left && !wasPressed) {
			wasPressed = true;
			isDragging = false;
			lastX = currentX;
			lastY = currentY;
			
			std::cout << "LEFT MOUSE BUTTON PRESSED - TRACING RAY FOR OBJECT PICKING" << std::endl;
			
			// Check for object or manipulator selection
			pickObject(currentX, currentY);
		}
		// Handle dragging for manipulator
		else if (mouseState.buttons.left && wasPressed) {
			// Mouse is still down - check if we're dragging a manipulator
			if (manipulator.active && 
				(manipulator.xAxisSelected || manipulator.yAxisSelected || manipulator.zAxisSelected)) {
				
				isDragging = true;
				
				// Calculate drag delta
				float deltaX = currentX - lastX;
				float deltaY = currentY - lastY;
				
				// If delta is large enough, handle the drag
				if (glm::abs(deltaX) > 1.0f || glm::abs(deltaY) > 1.0f) {
					handleManipulatorDrag(currentX, currentY);
					
					// Update last position
					lastX = currentX;
					lastY = currentY;
				}
				
				// Mark as handled so camera doesn't move during manipulation
				handled = true;
			}
		}
		// Handle mouse button release
		else if (!mouseState.buttons.left && wasPressed) {
			wasPressed = false;
			isDragging = false;
			
			// If we were dragging a manipulator, disable axis selection
			if (manipulator.active) {
				manipulator.xAxisSelected = false;
				manipulator.yAxisSelected = false;
				manipulator.zAxisSelected = false;
			}
		}
	}

	void keyPressed(uint32_t key) override
	{
		std::cout << "KEY PRESSED: " << key << std::endl;
		
		if (key == 'P' || key == 'p') {
			std::cout << "**********************************************************" << std::endl;
			std::cout << "               P KEY PRESSED - PICKING NOW                " << std::endl;
			std::cout << "**********************************************************" << std::endl;
			
			// Use ray picking for centered screen position
			// Cast ray from the center of the screen
			pickObject(width/2.0f, height/2.0f);
		}
		else if (key == 'T' || key == 't') {
			// Switch to translate mode
			manipulator.mode = Manipulator::Mode::TRANSLATE;
			std::cout << "Manipulator Mode: TRANSLATE" << std::endl;
		}
		else if (key == 'R' || key == 'r') {
			// Switch to rotate mode
			manipulator.mode = Manipulator::Mode::ROTATE;
			std::cout << "Manipulator Mode: ROTATE" << std::endl;
		}
		else if (key == 'S' || key == 's') {
			// Switch to scale mode
			manipulator.mode = Manipulator::Mode::SCALE;
			std::cout << "Manipulator Mode: SCALE" << std::endl;
		}
		
		// Let the base class handle other keys
		VulkanRaytracingSample::keyPressed(key);
	}
	
	virtual void render()
	{
		if (!prepared)
			return;
		updateUniformBuffers();
		if (!paused || camera.updated) {
			updateLight();
		}
		draw();
	}
	
	void OnUpdateUIOverlay(vks::UIOverlay *overlay) override
	{
		// Make the UI overlay EXTREMELY obvious
		if (overlay->header("!!! RAY-BASED OBJECT PICKING WITH MANIPULATORS !!!")) {
			// Debug header to clearly show what version we're running
			overlay->text("*** 3D MANIPULATORS FOR OBJECT TRANSFORMATION ***");
			overlay->text("*** LEFT CLICK TO SELECT AND MANIPULATE OBJECTS ***");
			overlay->text("");
				
			overlay->text("CONTROLS:");
			overlay->text("- LEFT CLICK on object to select it");
			overlay->text("- DRAG MANIPULATOR AXIS to transform object");
			overlay->text("- Press T key for TRANSLATE mode");
			overlay->text("- Press R key for ROTATE mode");
			overlay->text("- Press S key for SCALE mode");
			overlay->text("- Press P key to pick center object");
			overlay->text("- ARROW KEYS to move camera");
			overlay->text("- HOLD RIGHT MOUSE & DRAG to look around");
			
			// Show status info in a very simple way
			overlay->text("-------------------------------------");
			if (selectedObjectID != UINT32_MAX) {
				char selectionInfo[128];
				sprintf(selectionInfo, "SELECTED: %s (ID: %d)", 
					objects[selectedObjectID].name.c_str(), selectedObjectID);
				overlay->text(selectionInfo);
				
				char posInfo[128];
				sprintf(posInfo, "Position: (%.1f, %.1f, %.1f)", 
					objects[selectedObjectID].position.x,
					objects[selectedObjectID].position.y,
					objects[selectedObjectID].position.z);
				overlay->text(posInfo);
				
				// Show manipulator status
				char manipulatorInfo[128];
				const char* modeText = "TRANSLATE";
				if (manipulator.mode == Manipulator::Mode::ROTATE) {
					modeText = "ROTATE";
				} else if (manipulator.mode == Manipulator::Mode::SCALE) {
					modeText = "SCALE";
				}
				sprintf(manipulatorInfo, "Manipulator mode: %s", modeText);
				overlay->text(manipulatorInfo);
				
				if (manipulator.active) {
					if (manipulator.xAxisSelected) {
						overlay->text("X-AXIS selected (RED)");
					} else if (manipulator.yAxisSelected) {
						overlay->text("Y-AXIS selected (GREEN)");
					} else if (manipulator.zAxisSelected) {
						overlay->text("Z-AXIS selected (BLUE)");
					}
				}
			} else {
				overlay->text("NO SELECTION YET - CLICK ON AN OBJECT");
			}
			
			// Show this message in a way that will be completely unmissable
			overlay->text("THREE SPHERES ARE POSITIONED ABOVE CAMERA");
			overlay->text("-------------------------------------");
		}
	}
};

VULKAN_EXAMPLE_MAIN()