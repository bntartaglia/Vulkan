add_custom_command(
    TARGET pickbuffer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy 
    ${CMAKE_SOURCE_DIR}/shaders/glsl/pickbuffer/sphere.vert
    ${CMAKE_SOURCE_DIR}/shaders/glsl/pickbuffer/sphere.frag
    ${CMAKE_SOURCE_DIR}/shaders/glsl/pickbuffer/picking.frag
    ${CMAKE_BINARY_DIR}/shaders/glsl/pickbuffer
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/glsl/pickbuffer/sphere.vert
            ${CMAKE_SOURCE_DIR}/shaders/glsl/pickbuffer/sphere.frag
            ${CMAKE_SOURCE_DIR}/shaders/glsl/pickbuffer/picking.frag
)