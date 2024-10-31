all: compute-shader-offscreen comp.spv

compute-shader-offscreen: main.c
	gcc -o compute-shader-offscreen main.c -lvulkan

comp.spv: comp.glsl
	glslc -fshader-stage=comp comp.glsl -o comp.spv

.PHONY: clean
clean:
	rm -f compute-shader-offscreen *.spv
