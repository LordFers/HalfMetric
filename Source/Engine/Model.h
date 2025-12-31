#include <unordered_map>
#include "../ThirdParty/glm/glm.hpp"
#include <vector>

struct Face;
struct Vertex;

struct HalfEdge
{
	uint32_t origin;
	Face* face;
	HalfEdge* next;
	HalfEdge* prev;
	HalfEdge* twin;
	float cost;
};

struct Face
{
	HalfEdge* halfedge;
};

struct Vertex
{
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec2 uv;
	glm::mat4 Q; // sum(K_p)
};

struct Mesh
{
	std::vector<Vertex> vtx;
	std::vector<unsigned int> idx;
};

class Model
{
public:
	Model(const char* file_name);

public:
	void Simplify(unsigned int iterations);

public:
	inline const Mesh& GetMesh() const { return m_Mesh; }

private:
	void GenerateMeshData();
	void PrepareQEMData(); // compute error metric 'vTQv' for candidate pairs.
	void EdgeCollapse(HalfEdge* halfedge);
	bool IsCollapseSafe(HalfEdge* halfedge);

private:
	inline uint64_t HalfEdgeKey(uint32_t u, uint32_t v) { return (uint64_t(u) << 32) | uint64_t(v); }

private:
	Mesh m_Mesh;
	std::unordered_map<uint64_t, HalfEdge*> m_HalfEdges;
	std::vector<Face*> m_Faces;
	std::vector<uint64_t> m_Keys;
	std::vector<uint64_t> m_LastCollapseds;
};
