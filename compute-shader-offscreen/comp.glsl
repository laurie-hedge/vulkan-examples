#version 450

layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

layout(binding = 0, rgba8) uniform image2D image;

void main() {
	vec2 rg = vec2(gl_LocalInvocationID.xy) / (vec2(gl_WorkGroupSize.xy) - vec2(-1.0));
	float b = float(gl_WorkGroupID.x) / float(gl_NumWorkGroups.x - 1);
	imageStore(image, ivec2(gl_GlobalInvocationID.xy), vec4(rg, b, 1.0));
}
