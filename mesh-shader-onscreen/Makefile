all: mesh-shader-onscreen mesh.spv frag.spv

mesh-shader-onscreen: main.c
	gcc -o mesh-shader-onscreen main.c -lvulkan -lglfw

mesh.spv: mesh.glsl
	glslc -fshader-stage=mesh mesh.glsl -o mesh.spv --target-spv=spv1.4

frag.spv: frag.glsl
	glslc -fshader-stage=frag frag.glsl -o frag.spv --target-spv=spv1.4

.PHONY: clean
clean:
	rm -f mesh-shader-onscreen *.spv
