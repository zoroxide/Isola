#include "TriangleBvh.h"
#include <algorithm>
#include <cmath>

namespace isola::detail {

namespace {
struct Box {
    glm::vec3 mn{1e30f}, mx{-1e30f};
    void grow(const glm::vec3& p) { mn = glm::min(mn, p); mx = glm::max(mx, p); }
    void grow(const Box& b) { mn = glm::min(mn, b.mn); mx = glm::max(mx, b.mx); }
    float area() const {
        glm::vec3 e = glm::max(mx - mn, glm::vec3(0.0f));
        return e.x * e.y + e.y * e.z + e.z * e.x;
    }
};
}

// Binned surface-area-heuristic build (16 bins per split), leaves of up to 4 triangles
void TriangleBvh::build(std::vector<glm::vec3> positions, std::vector<int> ids, std::vector<uint8_t> flags) {
    nodes_.clear();
    int n = (int)(positions.size() / 3);
    ids.resize(n, 0);
    flags.resize(n, 0);
    if (n == 0) { verts_.clear(); ids_.clear(); flags_.clear(); return; }

    std::vector<Box> boxes(n);
    std::vector<glm::vec3> centers(n);
    for (int i = 0; i < n; ++i) {
        for (int k = 0; k < 3; ++k) boxes[i].grow(positions[i * 3 + k]);
        centers[i] = (boxes[i].mn + boxes[i].mx) * 0.5f;
    }
    std::vector<int> order(n);
    for (int i = 0; i < n; ++i) order[i] = i;

    struct Task { int node, first, count; };
    nodes_.reserve(n);
    nodes_.push_back({});
    std::vector<Task> stack{{0, 0, n}};
    constexpr int kBins = 16;
    while (!stack.empty()) {
        Task t = stack.back();
        stack.pop_back();
        Box bounds, cbounds;
        for (int i = t.first; i < t.first + t.count; ++i) { bounds.grow(boxes[order[i]]); cbounds.grow(centers[order[i]]); }
        Node& node = nodes_[t.node];
        node.mn = bounds.mn; node.mx = bounds.mx;

        int bestAxis = -1, bestSplit = 0;
        float bestCost = 1e30f;
        if (t.count > 4) {
            for (int axis = 0; axis < 3; ++axis) {
                float lo = cbounds.mn[axis], ext = cbounds.mx[axis] - lo;
                if (ext < 1e-6f) continue;
                Box bin[kBins];
                int cnt[kBins] = {};
                for (int i = t.first; i < t.first + t.count; ++i) {
                    int b = std::min(kBins - 1, (int)((centers[order[i]][axis] - lo) / ext * kBins));
                    bin[b].grow(boxes[order[i]]);
                    ++cnt[b];
                }
                // Sweep: cost of splitting after bin s
                float rightArea[kBins];
                int rightCount[kBins];
                Box acc;
                int c = 0;
                for (int s = kBins - 1; s > 0; --s) {
                    acc.grow(bin[s]); c += cnt[s];
                    rightArea[s] = acc.area(); rightCount[s] = c;
                }
                acc = Box(); c = 0;
                for (int s = 0; s < kBins - 1; ++s) {
                    acc.grow(bin[s]); c += cnt[s];
                    if (c == 0 || rightCount[s + 1] == 0) continue;
                    float cost = acc.area() * c + rightArea[s + 1] * rightCount[s + 1];
                    if (cost < bestCost) { bestCost = cost; bestAxis = axis; bestSplit = s; }
                }
            }
        }
        // Leaf when small, or when no split beats keeping the triangles together
        if (bestAxis < 0 || (t.count <= 8 && bestCost >= bounds.area() * t.count)) {
            node.first = t.first;
            node.count = t.count;
            continue;
        }
        float lo = cbounds.mn[bestAxis], ext = cbounds.mx[bestAxis] - lo;
        int* mid = std::partition(order.data() + t.first, order.data() + t.first + t.count, [&](int i) {
            return std::min(kBins - 1, (int)((centers[i][bestAxis] - lo) / ext * kBins)) <= bestSplit;
        });
        int leftCount = (int)(mid - (order.data() + t.first));
        int left = (int)nodes_.size();
        node.first = left;
        node.count = 0;
        nodes_.push_back({});
        nodes_.push_back({});
        stack.push_back({left, t.first, leftCount});
        stack.push_back({left + 1, t.first + leftCount, t.count - leftCount});
    }

    verts_.resize(positions.size());
    ids_.resize(n);
    flags_.resize(n);
    for (int i = 0; i < n; ++i) {
        for (int k = 0; k < 3; ++k) verts_[i * 3 + k] = positions[order[i] * 3 + k];
        ids_[i] = ids[order[i]];
        flags_[i] = flags[order[i]];
    }
}

glm::vec3 TriangleBvh::normal(int tri) const {
    const glm::vec3 &a = verts_[tri * 3], &b = verts_[tri * 3 + 1], &c = verts_[tri * 3 + 2];
    glm::vec3 n = glm::cross(b - a, c - a);
    float l = glm::length(n);
    return l > 0.0f ? n / l : glm::vec3(0, 1, 0);
}

template <bool AnyHit>
bool TriangleBvh::trace(const glm::vec3& o, const glm::vec3& d, float tMax, Hit* hit, uint8_t skip) const {
    if (nodes_.empty()) return false;
    glm::vec3 inv(1.0f / (std::fabs(d.x) > 1e-12f ? d.x : 1e-12f), 1.0f / (std::fabs(d.y) > 1e-12f ? d.y : 1e-12f),
                  1.0f / (std::fabs(d.z) > 1e-12f ? d.z : 1e-12f));
    auto boxEntry = [&](const Node& n, float limit) {
        glm::vec3 t0 = (n.mn - o) * inv, t1 = (n.mx - o) * inv;
        glm::vec3 tn = glm::min(t0, t1), tf = glm::max(t0, t1);
        float enter = std::max(std::max(tn.x, tn.y), std::max(tn.z, 0.0f));
        float exit = std::min(std::min(tf.x, tf.y), std::min(tf.z, limit));
        return enter <= exit ? enter : 1e30f;
    };
    bool found = false;
    float best = tMax;
    int stack[64], sp = 0;
    stack[sp++] = 0;
    while (sp) {
        const Node& n = nodes_[stack[--sp]];
        if (n.count) {
            for (int i = n.first; i < n.first + n.count; ++i) {
                if (flags_[i] & skip) continue;
                // Moller-Trumbore, both faces
                const glm::vec3& a = verts_[i * 3];
                glm::vec3 e1 = verts_[i * 3 + 1] - a, e2 = verts_[i * 3 + 2] - a;
                glm::vec3 p = glm::cross(d, e2);
                float det = glm::dot(e1, p);
                if (std::fabs(det) < 1e-12f) continue;
                float invDet = 1.0f / det;
                glm::vec3 s = o - a;
                float u = glm::dot(s, p) * invDet;
                if (u < 0.0f || u > 1.0f) continue;
                glm::vec3 q = glm::cross(s, e1);
                float v = glm::dot(d, q) * invDet;
                if (v < 0.0f || u + v > 1.0f) continue;
                float t = glm::dot(e2, q) * invDet;
                if (t <= 1e-5f || t >= best) continue;
                if (AnyHit) return true;
                best = t;
                found = true;
                hit->t = t;
                hit->tri = i;
            }
        } else if (sp < 62) {
            // Visit the nearer child first
            float tl = boxEntry(nodes_[n.first], best), tr = boxEntry(nodes_[n.first + 1], best);
            if (tl > tr) {
                if (tl < 1e30f) stack[sp++] = n.first;
                if (tr < 1e30f) stack[sp++] = n.first + 1;
            } else {
                if (tr < 1e30f) stack[sp++] = n.first + 1;
                if (tl < 1e30f) stack[sp++] = n.first;
            }
        }
    }
    return found;
}

bool TriangleBvh::intersect(const glm::vec3& origin, const glm::vec3& dir, float tMax, Hit& hit, uint8_t skipMask) const {
    return trace<false>(origin, dir, tMax, &hit, skipMask);
}

bool TriangleBvh::occluded(const glm::vec3& origin, const glm::vec3& dir, float tMax, uint8_t skipMask) const {
    return trace<true>(origin, dir, tMax, nullptr, skipMask);
}

// Ericson, Real-Time Collision Detection 5.1.5
glm::vec3 TriangleBvh::closestPoint(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    glm::vec3 ab = b - a, ac = c - a, ap = p - a;
    float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a;
    glm::vec3 bp = p - b;
    float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b;
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) return a + ab * (d1 / (d1 - d3));
    glm::vec3 cp = p - c;
    float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return c;
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) return a + ac * (d2 / (d2 - d6));
    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    float denom = 1.0f / (va + vb + vc);
    return a + ab * (vb * denom) + ac * (vc * denom);
}

} // namespace isola::detail
