add_custom_command(
    TARGET marqueemarqueepick POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy 
    ${CMAKE_SOURCE_DIR}/shaders/glsl/marqueemarqueepick/sphere.vert
    ${CMAKE_SOURCE_DIR}/shaders/glsl/marqueemarqueepick/sphere.frag
    ${CMAKE_SOURCE_DIR}/shaders/glsl/marqueemarqueepick/marqueepicking.frag
    ${CMAKE_BINARY_DIR}/shaders/glsl/marqueemarqueepick
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/glsl/marqueemarqueepick/sphere.vert
            ${CMAKE_SOURCE_DIR}/shaders/glsl/marqueemarqueepick/sphere.frag
            ${CMAKE_SOURCE_DIR}/shaders/glsl/marqueemarqueepick/marqueepicking.frag
)