#define _CRT_SECURE_NO_WARNINGS
#include "Model.h"
#include <string>
#include <array>
#include <algorithm>
#include <iterator>
#include <unordered_map>
#include <unordered_set>

Model::Model(const char* file_name)
{
    FILE* file = fopen(file_name, "r");
    if (file == NULL)
    {
        printf("[Error] Fail trying to open the file: %s\n", file_name);
        return;
    }

    std::vector<glm::vec3> temp_vertices;
    std::vector<glm::vec3> temp_normals;
    std::vector<glm::vec2> temp_uvs;

    struct ObjTriplet { int vi, ti, ni; };
    std::vector<ObjTriplet> tri_list;
    tri_list.reserve(4096);

    auto ResolveObjIndex = [](long idx, size_t count) -> int
    {
        if (idx > 0) return (int)(idx - 1);
        if (idx < 0) return (int)((long)count + idx);
        return -1;
    };

    auto ParseFaceVertexToken = [&](const char* token, ObjTriplet& out) -> bool
    {
        out.vi = -1; out.ti = -1; out.ni = -1;
        char* end = nullptr;
        long v = std::strtol(token, &end, 10);
        if (end == token)
            return false;
        out.vi = ResolveObjIndex(v, temp_vertices.size());

        if (*end == '/')
        {
            ++end;
            if (*end != '/')
            {
                long t = std::strtol(end, &end, 10);
                out.ti = ResolveObjIndex(t, temp_uvs.size());
            }
            if (*end == '/')
            {
                ++end;
                long n = std::strtol(end, &end, 10);
                out.ni = ResolveObjIndex(n, temp_normals.size());
            }
        }
        return (out.vi >= 0);
    };

    char line[512];
    while (fgets(line, sizeof(line), file))
    {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;

        if (line[0] == 'v' && line[1] == ' ')
        {
            glm::vec3 v; sscanf(line + 2, "%f %f %f", &v.x, &v.y, &v.z);
            temp_vertices.push_back(v);
        }
        else if (line[0] == 'v' && line[1] == 't')
        {
            glm::vec2 uv; sscanf(line + 3, "%f %f", &uv.x, &uv.y);
            temp_uvs.push_back(uv);
        }
        else if (line[0] == 'v' && line[1] == 'n')
        {
            glm::vec3 n; sscanf(line + 3, "%f %f %f", &n.x, &n.y, &n.z);
            temp_normals.push_back(n);
        }
        else if (line[0] == 'f' && std::isspace((unsigned char)line[1]))
        {
            std::vector<ObjTriplet> face;
            face.reserve(4);
            char* p = line + 1;
            while (*p)
            {
                while (*p && std::isspace((unsigned char)*p)) ++p;
                if (!*p) break;
                char* start = p;
                while (*p && !std::isspace((unsigned char)*p)) ++p;
                char saved = *p; *p = '\0';
                ObjTriplet t;
                if (ParseFaceVertexToken(start, t)) face.push_back(t);
                *p = saved;
            }

            // Triangulate
            for (size_t i = 1; i + 1 < face.size(); ++i)
            {
                tri_list.push_back(face[0]);
                tri_list.push_back(face[i]);
                tri_list.push_back(face[i + 1]);
            }
        }
    }

    fclose(file);

    m_Mesh.vtx.clear();
    m_Mesh.idx.clear();
    m_Mesh.vtx.reserve(tri_list.size());
    m_Mesh.idx.reserve(tri_list.size());

    constexpr float WELD_POS_EPS = 1e-4f;

    struct CellKey
    {
        int64_t x, y, z;
        bool operator==(const CellKey& o) const { return x == o.x && y == o.y && z == o.z; }
    };

    struct CellHasher
    {
        size_t operator()(const CellKey& k) const
        {
            size_t h = (size_t)k.x * 73856093;
            h ^= (size_t)k.y * 19349663;
            h ^= (size_t)k.z * 83492791;
            return h;
        }
    };

    auto GetCell = [&](glm::vec3 p) -> CellKey
    {
        return { (int64_t)(p.x / WELD_POS_EPS), (int64_t)(p.y / WELD_POS_EPS), (int64_t)(p.z / WELD_POS_EPS) };
    };

    std::unordered_map<CellKey, std::vector<unsigned int>, CellHasher> grid;

    for (const ObjTriplet& t : tri_list)
    {
        if (t.vi < 0)
            continue;

        Vertex cand;
        cand.position = temp_vertices[t.vi];

        bool hasUV = (t.ti >= 0);
        bool hasN = (t.ni >= 0);
        cand.uv = hasUV ? temp_uvs[t.ti] : glm::vec2(0.0f);
        cand.normal = hasN ? temp_normals[t.ni] : glm::vec3(0.0f, 0.0f, 1.0f);

        CellKey key = GetCell(cand.position);
        int foundIdx = -1;

        for (int dz = -1; dz <= 1 && foundIdx == -1; ++dz)
        {
            for (int dy = -1; dy <= 1 && foundIdx == -1; ++dy)
            {
                for (int dx = -1; dx <= 1 && foundIdx == -1; ++dx)
                {
                    CellKey neighbor = { key.x + dx, key.y + dy, key.z + dz };
                    auto it = grid.find(neighbor);
                    if (it != grid.end())
                    {
                        for (unsigned int existingIdx : it->second)
                        {
                            const Vertex& existing = m_Mesh.vtx[existingIdx];

                            float dx = std::abs(existing.position.x - cand.position.x);
                            float dy = std::abs(existing.position.y - cand.position.y);
                            float dz = std::abs(existing.position.z - cand.position.z);

                            if (dx <= WELD_POS_EPS && dy <= WELD_POS_EPS && dz <= WELD_POS_EPS)
                            {
                                foundIdx = (int)existingIdx;
                                break;
                            }
                        }
                    }
                }
            }
        }

        if (foundIdx != -1)
        {
            m_Mesh.idx.push_back((unsigned int)foundIdx);
        }
        else
        {
            unsigned int newIdx = (unsigned int)m_Mesh.vtx.size();
            m_Mesh.vtx.push_back(cand);
            m_Mesh.idx.push_back(newIdx);
            grid[key].push_back(newIdx);
        }
    }

    std::unordered_map<uint64_t, int> edgeCounts;
    for (size_t i = 0; i < m_Mesh.idx.size(); i += 3)
    {
        unsigned int idx[3] = { m_Mesh.idx[i], m_Mesh.idx[i + 1], m_Mesh.idx[i + 2] };
        for (int j = 0; j < 3; ++j)
        {
            unsigned int a = idx[j];
            unsigned int b = idx[(j + 1) % 3];
            uint64_t minV = std::min(a, b);
            uint64_t maxV = std::max(a, b);
            edgeCounts[(minV << 32) | maxV]++;
        }
    }

    int boundaryEdges = 0;
    int nonManifoldEdges = 0;

    for (auto const& [edge, count] : edgeCounts)
    {
        if (count == 1) boundaryEdges++;
        if (count > 2) nonManifoldEdges++;
    }

    bool isClosed = (boundaryEdges == 0);
    bool isTwoManifold = (nonManifoldEdges == 0);

    printf("--- Model Analysis: %s ---\n", file_name);
    printf("  > Vertices (Unique Pos): %zu\n", m_Mesh.vtx.size());
    printf("  > Triangles:             %zu\n", m_Mesh.idx.size() / 3);
    printf("Topology Status:\n");
    if (isClosed && isTwoManifold)
    {
        printf("  [OK] CLOSED MANIFOLD (Watertight)\n");
    }
    else
    {
        printf("  [WARNING] Issues Found:\n");
        if (boundaryEdges > 0) printf("    - Open Edges (Holes): %d\n", boundaryEdges);
        if (nonManifoldEdges > 0) printf("    - Non-Manifold Edges: %d\n", nonManifoldEdges);
    }
    printf("--------------------------------\n");

    GenerateMeshData();
    PrepareQEMData();
}

void Model::GenerateMeshData()
{
    const size_t count = m_Mesh.idx.size();
    m_Faces.reserve(count / 3);
    m_HalfEdges.reserve(count);
    m_Keys.reserve(count);

    for (size_t i = 0; i < count; i += 3)
    {
        unsigned int idx1 = m_Mesh.idx[i + 0];
        unsigned int idx2 = m_Mesh.idx[i + 1];
        unsigned int idx3 = m_Mesh.idx[i + 2];

        Vertex* v1 = &m_Mesh.vtx[idx1];
        Vertex* v2 = &m_Mesh.vtx[idx2];
        Vertex* v3 = &m_Mesh.vtx[idx3];

        HalfEdge* halfedge = new HalfEdge;
        halfedge->origin = idx1;
        halfedge->face = nullptr;
        halfedge->twin = nullptr;
        halfedge->cost = 0.0f;

        //next
        halfedge->next = new HalfEdge;
        halfedge->next->origin = idx2;
        halfedge->next->twin = nullptr;
        halfedge->next->cost = 0.0f;
        halfedge->next->face = nullptr;
        halfedge->next->next = new HalfEdge;
        halfedge->next->next->origin = idx3;
        halfedge->next->next->twin = nullptr;
        halfedge->next->next->cost = 0.0f;
        halfedge->next->next->face = nullptr;
        halfedge->next->next->next = halfedge;

        // prev
        halfedge->prev = halfedge->next->next;
        halfedge->next->prev = halfedge;
        halfedge->next->next->prev = halfedge->next;

        uint64_t e1_key = HalfEdgeKey(idx1, idx2);
        uint64_t e2_key = HalfEdgeKey(idx2, idx3);
        uint64_t e3_key = HalfEdgeKey(idx3, idx1);

        // No es necesario hacer un swap por si se repiten los half/twin, porque no puede pasar.
        // Es topológicamente imposible en un 2-manifold orientado. (Si hacés CW o CCW, un halfedge de un triángulo queda hacia arriba y el otro halfedge del triángulo vecino queda hacia abajo).
        if (m_HalfEdges.find(e1_key) == m_HalfEdges.end())
        {
            m_HalfEdges[e1_key] = halfedge;
            m_Keys.push_back(e1_key);
        }

        if (m_HalfEdges.find(e2_key) == m_HalfEdges.end())
        {
            m_HalfEdges[e2_key] = halfedge->next;
            m_Keys.push_back(e2_key);
        }

        if (m_HalfEdges.find(e3_key) == m_HalfEdges.end())
        {
            m_HalfEdges[e3_key] = halfedge->next->next;
            m_Keys.push_back(e3_key);
        }

        uint64_t twin1_key = HalfEdgeKey(idx2, idx1);
        uint64_t twin2_key = HalfEdgeKey(idx3, idx2);
        uint64_t twin3_key = HalfEdgeKey(idx1, idx3);

        if (m_HalfEdges.find(twin1_key) != m_HalfEdges.end())
        {
            halfedge->twin = m_HalfEdges[twin1_key];
            halfedge->twin->twin = halfedge;
        }

        if (m_HalfEdges.find(twin2_key) != m_HalfEdges.end())
        {
            halfedge->next->twin = m_HalfEdges[twin2_key];
            halfedge->next->twin->twin = halfedge->next;
        }

        if (m_HalfEdges.find(twin3_key) != m_HalfEdges.end())
        {
            halfedge->next->next->twin = m_HalfEdges[twin3_key];
            halfedge->next->next->twin->twin = halfedge->next->next;
        }

        Face* face = new Face;
        face->halfedge = halfedge;
        m_Faces.push_back(face);

        halfedge->face = face;
        halfedge->next->face = face;
        halfedge->next->next->face = face;
    }
}

void Model::PrepareQEMData()
{
    std::unordered_set<uint32_t> VisitedVtx;
    if (!m_LastCollapseds.empty())
    {
        for (uint64_t key : m_LastCollapseds)
        {
            HalfEdge* collapsed_edge = m_HalfEdges[key];
            HalfEdge* current_edge = collapsed_edge;
            uint32_t idx1 = current_edge->origin;
            Vertex& v1 = m_Mesh.vtx[idx1];
            if (VisitedVtx.find(idx1) == VisitedVtx.end())
            {
                VisitedVtx.insert(idx1);
                v1.Q = glm::mat4(0.0f);

                do {
                    if (current_edge->twin == nullptr)
                        break;

                    Vertex& v2 = m_Mesh.vtx[current_edge->next->origin];
                    Vertex& v3 = m_Mesh.vtx[current_edge->next->next->origin];
                    glm::vec3 np = glm::cross(v2.position - v1.position, v3.position - v1.position);
                    glm::vec3 n = normalize(np);
                    float d = -glm::dot(n, v1.position);
                    glm::vec4 plane(n, d);
                    v1.Q += glm::outerProduct(plane, plane); //sum(K_p);
                    current_edge = current_edge->twin->next;
                } while (current_edge != collapsed_edge);
            }

            Vertex& v2 = m_Mesh.vtx[collapsed_edge->next->origin];
            glm::vec4 v(v2.position, 1.0f);
            collapsed_edge->cost = glm::dot(v, (v1.Q + v2.Q) * v);
            if (collapsed_edge->twin != nullptr)
            {
                v = glm::vec4(v1.position, 1.0f);
                collapsed_edge->twin->cost = glm::dot(v, (v1.Q + v2.Q) * v);
            }
        }

        m_LastCollapseds.clear();
        return;
    }

    // If its first time computing Q matrices:
    for (const Face* face : m_Faces)
    {
        HalfEdge* current_edge = face->halfedge;
        uint32_t idx1 = current_edge->origin;
        Vertex& v1 = m_Mesh.vtx[idx1];
        if (VisitedVtx.find(idx1) == VisitedVtx.end())
        {
            VisitedVtx.insert(idx1);
            v1.Q = glm::mat4(0.0f);

            do {
                if (current_edge->twin == nullptr)
                    break;

                Vertex& v2 = m_Mesh.vtx[current_edge->next->origin];
                Vertex& v3 = m_Mesh.vtx[current_edge->next->next->origin];
                glm::vec3 np = glm::cross(v2.position - v1.position, v3.position - v1.position);
                glm::vec3 n = normalize(np);
                float d = -glm::dot(n, v1.position);
                glm::vec4 plane(n, d);
                v1.Q += glm::outerProduct(plane, plane); //sum(K_p);
                current_edge = current_edge->twin->next;
            } while (current_edge != face->halfedge);
        }

        current_edge = face->halfedge->next;
        uint32_t idx2 = current_edge->origin;
        Vertex& v2 = m_Mesh.vtx[idx2];
        if (VisitedVtx.find(idx2) == VisitedVtx.end())
        {
            VisitedVtx.insert(idx2);
            v2.Q = glm::mat4(0.0f);

            do {
                if (current_edge->twin == nullptr)
                    break;

                Vertex& v3 = m_Mesh.vtx[current_edge->next->origin];
                Vertex& v4 = m_Mesh.vtx[current_edge->next->next->origin];
                glm::vec3 np = glm::cross(v3.position - v2.position, v4.position - v2.position);
                glm::vec3 n = normalize(np);
                float d = -glm::dot(n, v2.position);
                glm::vec4 plane(n, d);
                v2.Q += glm::outerProduct(plane, plane); //sum(K_p);
                current_edge = current_edge->twin->next;
            } while (current_edge != face->halfedge->next);
        }

        glm::vec4 v(v2.position, 1.0f);
        face->halfedge->cost = glm::dot(v, (v1.Q + v2.Q) * v);
        if (face->halfedge->twin != nullptr)
        {
            v = glm::vec4(v1.position, 1.0f);
            face->halfedge->twin->cost = glm::dot(v, (v1.Q + v2.Q) * v);
        }
    }
}

bool Model::IsCollapseSafe(HalfEdge* halfedge)
{
    std::unordered_set<uint32_t> s1;
    std::unordered_set<uint32_t> s2;

    HalfEdge* currenth = halfedge;
    do {
        if (currenth == nullptr || currenth->twin == nullptr) return false; // Necesario para descartar non-manifold.
        s1.insert(currenth->twin->origin);
        currenth = currenth->twin->next;
    } while (currenth != halfedge);

    currenth = halfedge->next;
    
    do {
        if (currenth == nullptr || currenth->twin == nullptr) return false; // Necesario para descartar non-manifold.
        s2.insert(currenth->twin->origin);
        currenth = currenth->twin->next;
    } while (currenth != halfedge->next);

    std::vector<uint32_t> v1(s1.begin(), s1.end());
    std::vector<uint32_t> v2(s2.begin(), s2.end());

    std::sort(v1.begin(), v1.end());
    std::sort(v2.begin(), v2.end());

    std::vector<uint32_t> I;
    std::set_intersection(v1.begin(), v1.end(), v2.begin(), v2.end(), std::back_inserter(I));

    return I.size() == 2;
}

void Model::EdgeCollapse(HalfEdge* halfedge)
{
    HalfEdge* twin = halfedge->twin;
    HalfEdge* currenth = halfedge;
    uint32_t v1 = halfedge->origin;
    uint32_t v2 = halfedge->next->origin;
    uint64_t k1 = HalfEdgeKey(halfedge->origin, halfedge->next->origin);
    uint64_t k2 = HalfEdgeKey(halfedge->next->origin, halfedge->next->next->origin);
    uint64_t k3 = HalfEdgeKey(halfedge->next->next->origin, halfedge->origin);
    uint64_t k4 = HalfEdgeKey(twin->origin, twin->next->origin);
    uint64_t k5 = HalfEdgeKey(twin->next->origin, twin->next->next->origin);
    uint64_t k6 = HalfEdgeKey(twin->next->next->origin, twin->origin);

    HalfEdge* n1 = halfedge->next->twin;
    HalfEdge* n2 = twin->prev->twin;

    uint64_t n1_key = HalfEdgeKey(n1->origin, n1->next->origin);
    uint64_t n2_key = HalfEdgeKey(n2->origin, n2->next->origin);

    std::vector<HalfEdge*> to_delete = { m_HalfEdges[k1], m_HalfEdges[k2], m_HalfEdges[k3], m_HalfEdges[k4], m_HalfEdges[k5], m_HalfEdges[k6] };
    do {
        uint64_t current_key = HalfEdgeKey(currenth->origin, currenth->next->origin);
        uint64_t current_twin_key = HalfEdgeKey(currenth->twin->origin, currenth->origin);

        m_HalfEdges.erase(current_key);
        auto it = std::find(m_Keys.begin(), m_Keys.end(), current_key);
        m_Keys.erase(it);

        m_HalfEdges.erase(current_twin_key);
        it = std::find(m_Keys.begin(), m_Keys.end(), current_twin_key);
        m_Keys.erase(it);

        if (currenth != halfedge)
        {
            uint64_t new_key = HalfEdgeKey(v2, currenth->twin->origin);
            if (m_HalfEdges.find(new_key) == m_HalfEdges.end())
            {
                m_HalfEdges.emplace(new_key, currenth);
                m_Keys.push_back(new_key);
            }
            else if (new_key != n1_key && new_key != n2_key)
            {
                m_HalfEdges[new_key] = currenth;
            }
            m_LastCollapseds.push_back(new_key);

            // new twin key
            new_key = HalfEdgeKey(currenth->twin->origin, v2);
            if (m_HalfEdges.find(new_key) == m_HalfEdges.end())
            {
                m_HalfEdges.emplace(new_key, currenth->twin);
                m_Keys.push_back(new_key);
            }
            else if (new_key != n1_key && new_key != n2_key)
            {
                m_HalfEdges[new_key] = currenth->twin;
            }

            m_LastCollapseds.push_back(new_key);
            currenth->origin = v2;
        }

        currenth = currenth->twin->next;

    } while (currenth != halfedge);

    halfedge->next->twin->twin = halfedge->prev->twin;
    halfedge->prev->twin->twin = halfedge->next->twin;

    twin->next->twin->twin = twin->prev->twin;
    twin->prev->twin->twin = twin->next->twin;

    auto it = std::find(m_Faces.begin(), m_Faces.end(), halfedge->face);
    m_Faces.erase(it);
    delete halfedge->face;

    it = std::find(m_Faces.begin(), m_Faces.end(), twin->face);
    m_Faces.erase(it);
    delete twin->face;

    for (size_t i = 0; i < to_delete.size(); ++i)
    {
        delete to_delete[i];
    }
}

void Model::Simplify(unsigned int iterations)
{
    while (iterations--)
    {
        float minimal = std::numeric_limits<float>::infinity();
        HalfEdge* best_he = nullptr;

        for (auto& pair : m_HalfEdges)
        {
            HalfEdge* he = pair.second;

            // No-valid halfedge
            //if (!he || !he->next)
                //continue;

            //const glm::vec3& a = m_Mesh.vtx[he->origin].position;
            //const glm::vec3& b = m_Mesh.vtx[he->next->origin].position;
            //const glm::vec3 d = b - a;
            //const float dist2 = glm::dot(d, d);

            //if (dist2 < minimal)
            if (he->cost < minimal)
            {
                if (IsCollapseSafe(he))
                {
                    //minimal = dist2;
                    minimal = he->cost;
                    best_he = he;
                }
            }
        }

        if (!best_he)
        {
            printf("No more valid edges.\n");
            break;
        }

        EdgeCollapse(best_he);
        PrepareQEMData();
    }

    m_Mesh.idx.clear();
    m_Mesh.idx.resize(0);
    m_Mesh.idx.reserve(m_Faces.size() * 3);

    for (Face* face : m_Faces)
    {
        m_Mesh.idx.push_back(face->halfedge->origin);
        m_Mesh.idx.push_back(face->halfedge->next->origin);
        m_Mesh.idx.push_back(face->halfedge->next->next->origin);
    }
}
