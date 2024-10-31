#version 450

layout(location = 0) out vec4 colour;

layout(location = 0) in VertexData {
	vec4 colour;
} varying_in;  

void main() {
	colour = varying_in.colour;
}
