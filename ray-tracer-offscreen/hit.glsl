#version 460

#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) rayPayloadInEXT vec3 ray_colour;
hitAttributeEXT vec2 hit_attribs;

void main() {
	ray_colour = vec3(hit_attribs, 1.0 - hit_attribs.x - hit_attribs.y);
}
