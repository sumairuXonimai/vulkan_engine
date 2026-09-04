
# Add source to this project's executable.
file(GLOB_RECURSE ENGINE_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

add_executable(engine ${ENGINE_SOURCES})

file(GLOB_RECURSE ALL_HEADERS CONFIGURE_DEPENDS 
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h" 
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

set(HEADER_DIRS "")
foreach(HEADER_PATH ${ALL_HEADERS})
    get_filename_component(HEADER_DIR "${HEADER_PATH}" DIRECTORY)
    list(APPEND HEADER_DIRS "${HEADER_DIR}")
endforeach()

if(HEADER_DIRS)
    list(REMOVE_DUPLICATES HEADER_DIRS)
    target_include_directories(engine PRIVATE ${HEADER_DIRS})
endif()

add_dependencies(engine Shaders)

set_property(TARGET engine PROPERTY CXX_STANDARD 20)
target_compile_definitions(engine PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)
target_include_directories(engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(engine PUBLIC vma glm Vulkan::Vulkan fmt::fmt stb_image SDL2::SDL2 vkbootstrap imgui fastgltf::fastgltf spirv_reflect)

target_precompile_headers(engine PUBLIC <optional> <vector> <memory> <string> <vector> <unordered_map> <glm/mat4x4.hpp>  <glm/vec4.hpp> <vulkan/vulkan.h>)

if(WIN32)
  add_custom_command(TARGET engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:engine> $<TARGET_FILE_DIR:engine>
    COMMAND_EXPAND_LISTS
    )
endif()