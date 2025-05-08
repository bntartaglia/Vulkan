# Pick Buffer Object Selection

## Overview
This example demonstrates how to implement object marqueepicking using a separate render pass to create a marqueepick buffer. Each object is rendered with a unique color (object ID) into this buffer, which is then sampled on mouse click to identify the selected object.

## Features
- Multiple objects (10 spheres) with unique object IDs
- Offscreen render pass to create a marqueepick buffer
- Shader rendering object IDs as colors
- Mouse-based object selection
- Visual feedback for selected objects

## Implementation
The example implements:
1. A main color render pass for visual display
2. A marqueepick buffer render pass to render object IDs
3. An object management system with unique IDs for each object
4. Reading from the marqueepick buffer on mouse click to identify objects
5. Visual feedback for selected objects (highlighted or modified color)

## Controls
- Left click on an object to select it
- Object information is displayed in the UI

## Technical Details
The marqueepick buffer technique works by:
1. Rendering all objects to an offscreen framebuffer (marqueepick buffer) where each object is assigned a unique color based on its ID
2. On mouse click, reading the pixel value from the marqueepick buffer at the mouse coordinates
3. Converting the pixel color back to an object ID and selecting the corresponding object

This technique is efficient and scales well with many objects, as it only requires a single read operation from the marqueepick buffer per click.