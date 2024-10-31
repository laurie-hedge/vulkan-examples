#version 450

#extension GL_EXT_mesh_shader : require

taskPayloadSharedEXT struct {
	float offsets[2];
} mesh_data;

void main() {
	mesh_data.offsets[0] = -0.5;
	mesh_data.offsets[1] = 0.5;
	EmitMeshTasksEXT(2, 1, 1);
}
