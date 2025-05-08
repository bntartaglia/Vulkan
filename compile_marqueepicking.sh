#!/bin/bash


GLSLC="/mnt/f/VulkanSDK/1.4.309.0/Bin/glslc.exe"


$GLSLC -o shaders/hlsl/marqueepick/sphere.vert.spv  shaders/glsl/marqueepick/sphere.vert
$GLSLC -o shaders/hlsl/marqueepick/sphere.frag.spv  shaders/glsl/marqueepick/sphere.frag
$GLSLC -o shaders/hlsl/marqueepick/marqueepicking.frag.spv  shaders/glsl/marqueepick/marqueepicking.frag
