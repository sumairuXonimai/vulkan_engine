// input_structures.glsl
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require // <--- ADD THIS

// Global Bindless Set
layout (set = 0, binding = 0) uniform sampler2D globalTextures[];

// Global Scene Data Set
layout (set = 1, binding = 0) uniform SceneData {
    mat4 view;
    mat4 proj;
    mat4 viewproj;
    vec4 cameraPos;
    vec4 ambientColor;
    vec4 sunlightColor;
    vec4 sunlightDirection;
    vec4 lightPos;
    mat4 lightSpaceMatrix;
} sceneData;

// Material Buffer Reference Structure
layout (buffer_reference, std430, buffer_reference_align = 16) readonly buffer GLTFMaterialData {
    vec4 colorFactors;
    vec4 metalRoughFactors;
};

// Global Push Constants
layout (push_constant) uniform PushConstants {
    mat4 worldMatrix;              
    uint64_t vertexBuffer;         
    uint64_t materialBufferAddress;
    uint colorTexIndex;
    uint metalRoughTexIndex;
    uint shadowmapTexIndex;
} push;