cmake_minimum_required(VERSION 3.16.5)
project(SimpleSDF VERSION 0.1.0)

# ------------------------------------------------------------------------------
# Global Configuration
# ------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------
# Add the EasyVulkan library (used for Vulkan abstraction)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/thirdParty/EasyVulkan)

# ------------------------------------------------------------------------------
# Source Files
# ------------------------------------------------------------------------------
file(GLOB_RECURSE SRC_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

file(GLOB_RECURSE HEADER_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)


# ------------------------------------------------------------------------------
# Executable Target Definition
# ------------------------------------------------------------------------------

add_executable(SDF 
    ${SRC_FILES}
)

# Link with EasyVulkan library
target_link_libraries(SDF PUBLIC EasyVulkan)

# Add include directories for the executable
target_include_directories(SDF
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdParty/EasyVulkan/include
)


# ------------------------------------------------------------------------------
# Shader Compilation
# ------------------------------------------------------------------------------
# Find the GLSL validator (glslangValidator) from the Vulkan SDK
find_program(GLSL_VALIDATOR glslangValidator REQUIRED)
if(NOT GLSL_VALIDATOR)
    message(FATAL_ERROR "glslangValidator not found! Please install the Vulkan SDK.")
endif()

# Set directories for shader sources and compiled binaries
set(SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

# Create the output directory for the compiled shaders
file(MAKE_DIRECTORY ${SHADER_BINARY_DIR})

# List the shader source files to be compiled
set(SHADERS
    ${SHADER_SOURCE_DIR}/triangle.vert
    ${SHADER_SOURCE_DIR}/triangle.frag
    ${SHADER_SOURCE_DIR}/sdf2d.frag
    ${SHADER_SOURCE_DIR}/sdf2dCircle.frag
    ${SHADER_SOURCE_DIR}/sdf2dCircleRect.frag
    ${SHADER_SOURCE_DIR}/sdf3d.frag
    ${SHADER_SOURCE_DIR}/rsm_light.frag
    ${SHADER_SOURCE_DIR}/sdf_practice.frag
)

# Compile each shader into a SPIR-V binary
foreach(SHADER ${SHADERS})
    get_filename_component(FILENAME ${SHADER} NAME)
    add_custom_command(
        OUTPUT ${SHADER_BINARY_DIR}/${FILENAME}.spv
        COMMAND ${GLSL_VALIDATOR} -V ${SHADER} -o ${SHADER_BINARY_DIR}/${FILENAME}.spv
        DEPENDS ${SHADER}
        COMMENT "Compiling shader ${FILENAME}"
    )
    list(APPEND SPV_SHADERS ${SHADER_BINARY_DIR}/${FILENAME}.spv)
endforeach()

# Create a custom target that depends on all compiled shader binaries
add_custom_target(myShaders ALL DEPENDS ${SPV_SHADERS})

# Ensure that shaders are compiled before the Blur executable is built
add_dependencies(SDF myShaders)

# ------------------------------------------------------------------------------
# Post-Build Commands
# ------------------------------------------------------------------------------
# After building Blur, copy the compiled shaders to its output directory
add_custom_command(
    TARGET SDF POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:SDF>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${SHADER_BINARY_DIR} $<TARGET_FILE_DIR:SDF>/shaders
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:SDF>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:SDF>/assets
)