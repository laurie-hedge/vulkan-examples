#version 460

#extension GL_EXT_ray_tracing : enable

layout(binding = 0, set = 0) uniform accelerationStructureEXT top_level_as;
layout(binding = 1, set = 0, rgba8) uniform image2D image;
layout(binding = 2, set = 0) uniform CameraProperties
{
	mat4 view_inverse;
	mat4 proj_inverse;
} cam;

layout(location = 0) rayPayloadEXT vec3 hit_value;

void main() {
	const vec2 pixel_center = vec2(gl_LaunchIDEXT.xy) + vec2(0.5);
	const vec2 in_uv = pixel_center / vec2(gl_LaunchSizeEXT.xy);
	vec2 d = in_uv * 2.0 - 1.0;

	vec4 origin = cam.view_inverse * vec4(0,0,0,1);
	vec4 target = cam.proj_inverse * vec4(d.x, d.y, 1, 1) ;
	vec4 direction = cam.view_inverse * vec4(normalize(target.xyz), 0) ;

	float tmin = 0.001;
	float tmax = 10000.0;

    hit_value = vec3(0.0);

    traceRayEXT(top_level_as, gl_RayFlagsOpaqueEXT, 0xff, 0, 0, 0, origin.xyz, tmin, direction.xyz, tmax, 0);

	imageStore(image, ivec2(gl_LaunchIDEXT.xy), vec4(hit_value.bgr, 0.0));
}
