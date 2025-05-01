# Simple 3D Object Manipulator

## Overview
This example demonstrates how to implement a simple object manipulator for 3D objects in Vulkan, allowing for translation, rotation, and scaling through keyboard and mouse interaction.

## Features
- Translation, rotation, and scaling of 3D objects
- Keyboard controls for selecting transformation modes and axes
- Mouse drag interaction for applying transformations
- Visual feedback through UI overlay
- Integration with model transformation

## Controls
- Press **T** to switch to Translation mode
- Press **R** to switch to Rotation mode
- Press **S** to switch to Scale mode
- Press **X**, **Y**, or **Z** to select the corresponding axis
- Click and drag with the left mouse button to transform the object along the selected axis

## Implementation
The example is based on the glTF model loading example and adds:
1. A `Manipulator` structure to manage transformations
2. Keyboard handling for mode and axis selection
3. Mouse drag handling for applying transformations
4. UI overlay for displaying current manipulator status
5. Integration with the model's transformation matrix

## Notes
This is a basic implementation without visual manipulator handles. In a more complete implementation, you would add visual representations for the axes and handle ray-based picking for axis selection.