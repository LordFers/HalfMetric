#include "Renderer.h"
#include "../ThirdParty/GLFW/glfw3.h"

void Renderer::Draw(const Model& model, float pos_x, float pos_y, float pos_z)
{
	static float angle_z = -0.1f;
	angle_z += 0.01f;
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	GLfloat lightPos[] = { 0.0f, 1.0f, 1.0f, 0.0f };
	glLightfv(GL_LIGHT0, GL_POSITION, lightPos);
	glTranslatef(-pos_x, pos_y, pos_z);
	//glRotatef(90.0f, 1.0f, 0.0f, 0.0f);
	//glRotatef(angle_z, 0.0f, 1.0f, 0.0f);
	//glRotatef(angle_z, 0.0f, 0.0f, 1.0f);
	glBegin(GL_TRIANGLES);
	{
		const Mesh& mesh = model.GetMesh();
		const size_t idx_count = mesh.idx.size();
		for (size_t i = 0; i < idx_count; ++i)
		{
			const unsigned int idx = mesh.idx[i];
			const Vertex& vtx = mesh.vtx[idx];
			glNormal3f(vtx.normal.x, vtx.normal.y, vtx.normal.z);
			glVertex3f(vtx.position.x, vtx.position.y, vtx.position.z);
		}
	}
	glEnd();
}
