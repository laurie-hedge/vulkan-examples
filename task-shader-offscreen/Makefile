all: task-shader-offscreen task.spv mesh.spv frag.spv

task-shader-offscreen: main.c
	gcc -o task-shader-offscreen main.c -lvulkan

task.spv: task.glsl
	glslc -fshader-stage=task task.glsl -o task.spv --target-spv=spv1.4

mesh.spv: mesh.glsl
	glslc -fshader-stage=mesh mesh.glsl -o mesh.spv --target-spv=spv1.4

frag.spv: frag.glsl
	glslc -fshader-stage=frag frag.glsl -o frag.spv --target-spv=spv1.4

.PHONY: clean
clean:
	rm -f task-shader-offscreen *.spv
