#version 450

const vec2 positions[3] = { vec2(0.0, -0.5), vec2(0.5, 0.5), vec2(-0.5, 0.5) };
const vec3 colours[3] = { vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0) };

layout (location = 0) out PerVertexData {
	vec4 colour;
} varying_out;

void main() {
	gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
	varying_out.colour = vec4(colours[gl_VertexIndex], 1.0);
}
