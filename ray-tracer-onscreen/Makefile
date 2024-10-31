all: ray-tracer-onscreen rgen.spv miss.spv hit.spv

ray-tracer-onscreen: main.c
	gcc -o ray-tracer-onscreen main.c -lvulkan -lglfw

rgen.spv: rgen.glsl
	glslc -fshader-stage=rgen rgen.glsl -o rgen.spv --target-spv=spv1.4

miss.spv: miss.glsl
	glslc -fshader-stage=rmiss miss.glsl -o miss.spv --target-spv=spv1.4

hit.spv: hit.glsl
	glslc -fshader-stage=rchit hit.glsl -o hit.spv --target-spv=spv1.4

.PHONY: clean
clean:
	rm -f ray-tracer-onscreen *.spv
