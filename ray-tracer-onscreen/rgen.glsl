#version 460

#extension GL_EXT_ray_tracing : enable

layout(binding = 0, set = 0) uniform accelerationStructureEXT acceleration_struct;
layout(binding = 1, set = 0, rgba8) uniform image2D image;

layout(location = 0) rayPayloadEXT vec3 ray_colour;

void main() {
	const vec2 pixel_centre_viewport   = vec2(gl_LaunchIDEXT.xy) + vec2(0.5);
	const vec2 normalised_pixel_centre = pixel_centre_viewport / vec2(gl_LaunchSizeEXT.xy);
	const vec2 pixel_centre_clip_space = normalised_pixel_centre * 2.0 - 1.0;

	const vec3 ray_origin    = vec3(pixel_centre_clip_space, -2.5);
	const vec3 ray_direction = vec3(0.0, 0.0, 1.0);
	const float tmin         = 0.001;
	const float tmax         = 1000.0;

    traceRayEXT(acceleration_struct, gl_RayFlagsOpaqueEXT, 0xff, 0, 0, 0, ray_origin, tmin, ray_direction, tmax, 0);

	imageStore(image, ivec2(gl_LaunchIDEXT.xy), vec4(ray_colour, 1.0));
}
