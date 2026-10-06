#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

// Bounding volume hierarchy over a static triangle soup, for CPU ray casts (light baking)
// and overlap queries (player collision). Build once; queries are const and thread-safe.
class TriangleBvh {
public:
    struct Hit {
        float t = 0.0f;
        int tri = -1;
    };

    // positions: 3 vertices per triangle (already in world space). ids / flags: per triangle user data
    // (e.g. material index, and bits that queries can skip). Triangles are reordered: use id(tri).
    void build(std::vector<glm::vec3> positions, std::vector<int> ids, std::vector<uint8_t> flags);
    bool empty() const { return nodes_.empty(); }
    int triangleCount() const { return (int)(verts_.size() / 3); }

    // Nearest hit along origin + t * dir for t in (0, tMax). skipMask: triangles whose flags share a bit are ignored.
    bool intersect(const glm::vec3& origin, const glm::vec3& dir, float tMax, Hit& hit, uint8_t skipMask = 0) const;
    // Any hit (cheaper: stops at the first one)
    bool occluded(const glm::vec3& origin, const glm::vec3& dir, float tMax, uint8_t skipMask = 0) const;
    // Calls fn(triangleIndex) for every triangle whose bounds overlap the box
    template <class Fn> void query(const glm::vec3& mn, const glm::vec3& mx, Fn&& fn, uint8_t skipMask = 0) const;

    const glm::vec3& vertex(int tri, int k) const { return verts_[tri * 3 + k]; }
    glm::vec3 normal(int tri) const;   // geometric normal (winding order)
    int id(int tri) const { return ids_[tri]; }

    glm::vec3 boundsMin() const { return nodes_.empty() ? glm::vec3(0) : nodes_[0].mn; }
    glm::vec3 boundsMax() const { return nodes_.empty() ? glm::vec3(0) : nodes_[0].mx; }

    // Closest point on triangle abc to p
    static glm::vec3 closestPoint(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c);

private:
    struct Node {
        glm::vec3 mn; int first;   // leaf: first triangle; inner: index of the left child (right = left + 1)
        glm::vec3 mx; int count;   // leaf: triangle count; inner: 0
    };
    std::vector<Node> nodes_;
    std::vector<glm::vec3> verts_;   // reordered so leaves reference contiguous triangles
    std::vector<int> ids_;
    std::vector<uint8_t> flags_;
    template <bool AnyHit> bool trace(const glm::vec3& o, const glm::vec3& d, float tMax, Hit* hit, uint8_t skip) const;
};

template <class Fn> void TriangleBvh::query(const glm::vec3& mn, const glm::vec3& mx, Fn&& fn, uint8_t skipMask) const {
    if (nodes_.empty()) return;
    int stack[64], sp = 0;
    stack[sp++] = 0;
    while (sp) {
        const Node& n = nodes_[stack[--sp]];
        if (n.mn.x > mx.x || n.mx.x < mn.x || n.mn.y > mx.y || n.mx.y < mn.y || n.mn.z > mx.z || n.mx.z < mn.z) continue;
        if (n.count) {
            for (int i = n.first; i < n.first + n.count; ++i) {
                if (flags_[i] & skipMask) continue;
                const glm::vec3 &a = verts_[i * 3], &b = verts_[i * 3 + 1], &c = verts_[i * 3 + 2];
                glm::vec3 tmn = glm::min(a, glm::min(b, c)), tmx = glm::max(a, glm::max(b, c));
                if (tmn.x > mx.x || tmx.x < mn.x || tmn.y > mx.y || tmx.y < mn.y || tmn.z > mx.z || tmx.z < mn.z) continue;
                fn(i);
            }
        } else if (sp < 62) {
            stack[sp++] = n.first;
            stack[sp++] = n.first + 1;
        }
    }
}
