CFLAGS = -std=c++17 -O2
LDFLAGS = -lglfw -lvulkan -ldl -lpthread -lX11 -lXxf86vm -lXrandr -lXi

ray-tracer-test: main.cpp rgen.spv miss.spv hit.spv
	g++ $(CFLAGS) -o ray-tracer-test main.cpp $(LDFLAGS)

rgen.spv: rgen.glsl
	glslc -fshader-stage=rgen rgen.glsl -o rgen.spv --target-spv=spv1.4

miss.spv: miss.glsl
	glslc -fshader-stage=rmiss miss.glsl -o miss.spv --target-spv=spv1.4

hit.spv: hit.glsl
	glslc -fshader-stage=rchit hit.glsl -o hit.spv --target-spv=spv1.4

.PHONY: clean
clean:
	rm -f ray-tracer-test
