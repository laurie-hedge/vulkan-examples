all: mesh-shader-onscreen-anim mesh.spv frag.spv

mesh-shader-onscreen-anim: main.c
	gcc -o mesh-shader-onscreen-anim main.c -lvulkan -lglfw -lm

mesh.spv: mesh.glsl
	glslc -fshader-stage=mesh mesh.glsl -o mesh.spv --target-spv=spv1.4

frag.spv: frag.glsl
	glslc -fshader-stage=frag frag.glsl -o frag.spv --target-spv=spv1.4

.PHONY: clean
clean:
	rm -f mesh-shader-onscreen-anim *.spv
