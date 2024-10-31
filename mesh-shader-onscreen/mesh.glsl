#version 450

#extension GL_EXT_mesh_shader : require

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(triangles, max_vertices = 3, max_primitives = 1) out;

layout (location = 0) out VertexData {
	vec4 colour;
} varying_out[];

const vec2 positions[3] = { vec2(0.0, -0.5), vec2(0.5, 0.5), vec2(-0.5, 0.5) };
const vec3 colours[3] = { vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0) };

void main() {
	SetMeshOutputsEXT(3, 1);

	gl_MeshVerticesEXT[0].gl_Position = vec4(positions[0], 0.0, 1.0);
	gl_MeshVerticesEXT[1].gl_Position = vec4(positions[1], 0.0, 1.0);
	gl_MeshVerticesEXT[2].gl_Position = vec4(positions[2], 0.0, 1.0);

	varying_out[0].colour = vec4(colours[0], 1.0);
	varying_out[1].colour = vec4(colours[1], 1.0);
	varying_out[2].colour = vec4(colours[2], 1.0);

	gl_PrimitiveTriangleIndicesEXT[0] = uvec3(0, 1, 2);
}
