#version 460
#extension GL_EXT_buffer_reference : require

struct Vertex {
    vec3 position;
    float uv_x;
    vec3 normal;
    float uv_y;
    vec4 color;
};

layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(push_constant) uniform PushConstants {
    mat4 lightMVP;
    VertexBuffer vertexBuffer;
} push;

void main() {
    Vertex v = push.vertexBuffer.vertices[gl_VertexIndex];
    gl_Position = push.lightMVP * vec4(v.position, 1.0);
}