#!/bin/bash
# Simple script to compile all shaders in the repository

GLSLANG="/mnt/f/VulkanSDK/1.4.309.0/Bin/glslangValidator.exe"
SHADER_DIR="/mnt/f/source/repos/VulkanExamples/shaders/glsl"

# Counter for successful and failed compilations
SUCCESS_COUNT=0
ERROR_COUNT=0

# Go through all shader files in the glsl directory and compile them
for SHADER in $(find "$SHADER_DIR" -type f -name "*.vert" -o -name "*.frag" -o -name "*.comp" -o -name "*.geom" -o -name "*.tesc" -o -name "*.tese" -o -name "*.mesh" -o -name "*.task" -o -name "*.rgen" -o -name "*.rchit" -o -name "*.rmiss"); do
    # Skip already compiled .spv files
    if [[ "$SHADER" == *".spv" ]]; then
        continue
    fi
    
    # Check if the file actually exists and is not empty
    if [ ! -f "$SHADER" ] || [ ! -s "$SHADER" ]; then
        echo "Skipping non-existent or empty file: $SHADER"
        continue
    fi
    
    echo "Compiling $SHADER..."
    
    # Set additional parameters for specific shader types
    PARAMS=""
    
    # Ray tracing shaders require vulkan1.2 target environment
    if [[ "$SHADER" == *".rgen" || "$SHADER" == *".rchit" || "$SHADER" == *".rmiss" ]]; then
        PARAMS="$PARAMS --target-env vulkan1.2"
    fi
    
    # Ray queries require vulkan1.2 target environment
    if [[ "$SHADER" == *"rayquery"*".frag" ]]; then
        PARAMS="$PARAMS --target-env vulkan1.2"
    fi
    
    # Mesh shaders require spirv1.4 target environment
    if [[ "$SHADER" == *".mesh" || "$SHADER" == *".task" ]]; then
        PARAMS="$PARAMS --target-env spirv1.4"
    fi
    
    # Compile the shader
    if "$GLSLANG" -V "$SHADER" -o "$SHADER.spv" $PARAMS; then
        SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
    else
        ERROR_COUNT=$((ERROR_COUNT + 1))
        echo "Error compiling $SHADER"
    fi
done

echo "Compilation complete: $SUCCESS_COUNT successful, $ERROR_COUNT failed"

# Let's specifically check our pickbuffer shaders
echo -e "\nChecking pickbuffer shaders:"
for SHADER in $(ls "$SHADER_DIR/pickbuffer"/*.spv 2>/dev/null); do
    if [ -f "$SHADER" ]; then
        echo "✓ $SHADER exists"
    else
        echo "✗ $SHADER is missing"
    fi
done