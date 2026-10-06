#include "Village.h"
#include "Terrain.h"
#include "Textures.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <random>

// ===========================================================================
// Geometry toolkit
// ===========================================================================
namespace {
// Material ids (0..7 are layers of the village texture arrays)
enum Mat { PLASTER = 0, STONE, ROOF, INTERIOR, FLOOR, WOOD, PAINTED, FABRIC,
           EMISSIVE, WATER, LEAVES, FIRE, METAL, GLASS };
const float kTile[14] = { 2.4f, 1.7f, 2.2f, 2.4f, 1.8f, 1.3f, 1.1f, 0.8f, 1, 1, 1, 1, 1, 1 };

const float F0 = 0.45f;     // ground floor height above the terrace
const float SH = 3.0f;      // storey height
const float WT = 0.3f;      // wall thickness
const float SLAB = 0.22f;   // floor slab between storeys

struct Vtx { glm::vec3 p, n, t; glm::vec2 uv; glm::vec3 tint; float mat, ao, emis; };
// Indexed mesh under construction
struct MeshOut {
    std::vector<Vtx> v;
    std::vector<GLuint> i;
    size_t size() const { return i.size(); }
};

struct Surf {
    int mat = PLASTER; glm::vec3 tint{1.0f}; float ao = 1.0f, emis = 0.0f;
    Surf() = default;
    Surf(int m, glm::vec3 c = glm::vec3(1.0f), float a = 1.0f, float e = 0.0f) : mat(m), tint(c), ao(a), emis(e) {}
};

float smooth01(float e0, float e1, float x) { float t = glm::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f); return t * t * (3 - 2 * t); }

// Distance from p to a polyline; optionally the index of the closest segment and the position on it
float polyDist(const std::vector<glm::vec2>& pl, glm::vec2 p, int* seg = nullptr, float* frac = nullptr) {
    float best = 1e9f;
    for (size_t i = 0; i + 1 < pl.size(); ++i) {
        glm::vec2 a = pl[i], b = pl[i + 1], ab = b - a;
        float t = glm::clamp(glm::dot(p - a, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
        float d = glm::length(p - (a + ab * t));
        if (d < best) { best = d; if (seg) *seg = (int)i; if (frac) *frac = t; }
    }
    return best;
}

// Separating-axis test for two oriented rectangles on the ground (ax = unit local x axis)
bool obbOverlap(glm::vec2 c1, glm::vec2 a1, glm::vec2 h1, glm::vec2 c2, glm::vec2 a2, glm::vec2 h2) {
    glm::vec2 axes[4] = { a1, { -a1.y, a1.x }, a2, { -a2.y, a2.x } };
    glm::vec2 d = c2 - c1;
    for (glm::vec2 ax : axes) {
        float r1 = h1.x * std::fabs(glm::dot(a1, ax)) + h1.y * std::fabs(glm::dot(glm::vec2(-a1.y, a1.x), ax));
        float r2 = h2.x * std::fabs(glm::dot(a2, ax)) + h2.y * std::fabs(glm::dot(glm::vec2(-a2.y, a2.x), ax));
        if (std::fabs(glm::dot(d, ax)) > r1 + r2) return false;
    }
    return true;
}
} // namespace

// Builds all village geometry under a transform stack, and registers colliders, walkable surfaces
// and lights in world space as it goes.
class VillageBuilder {
public:
    Village& v;
    MeshOut opaque, cards, glass;
    struct Xf { glm::vec3 o{0.0f}, ax{1, 0, 0}, ay{0, 1, 0}, az{0, 0, 1}; };
    Xf xf;
    std::vector<Xf> stack;
    std::mt19937 rng;

    explicit VillageBuilder(Village& vil, unsigned seed) : v(vil), rng(seed) {}
    float rnd() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }

    glm::vec3 P(glm::vec3 l) const { return xf.o + xf.ax * l.x + xf.ay * l.y + xf.az * l.z; }
    glm::vec3 D(glm::vec3 l) const { return xf.ax * l.x + xf.ay * l.y + xf.az * l.z; }
    void push(glm::vec3 origin, float yawDeg = 0.0f) {
        stack.push_back(xf);
        Xf n = xf;
        n.o = P(origin);
        float a = glm::radians(yawDeg), c = std::cos(a), s = std::sin(a);
        n.ax = xf.ax * c + xf.az * s;
        n.az = -xf.ax * s + xf.az * c;
        xf = n;
    }
    void pop() { xf = stack.back(); stack.pop_back(); }

    // --- Primitives (local coordinates). UVs are planar projections in the local frame so textures
    // continue across neighbouring pieces; b = n x t is the texture "up" direction.
    void quad(MeshOut& out, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 n, glm::vec3 t, const Surf& s) {
        glm::vec3 bt = glm::cross(n, t);
        float tile = kTile[s.mat];
        auto uv = [&](glm::vec3 p) { return glm::vec2(glm::dot(p, t), -glm::dot(p, bt)) / tile; };
        glm::vec3 wn = glm::normalize(D(n)), wt = glm::normalize(D(t));
        Vtx q[4] = { { P(a), wn, wt, uv(a), s.tint, (float)s.mat, s.ao, s.emis },
                     { P(b), wn, wt, uv(b), s.tint, (float)s.mat, s.ao, s.emis },
                     { P(c), wn, wt, uv(c), s.tint, (float)s.mat, s.ao, s.emis },
                     { P(d), wn, wt, uv(d), s.tint, (float)s.mat, s.ao, s.emis } };
        GLuint base = (GLuint)out.v.size();
        out.v.insert(out.v.end(), { q[0], q[1], q[2], q[3] });
        out.i.insert(out.i.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
    }
    void tri(MeshOut& out, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 n, glm::vec3 t, const Surf& s) {
        glm::vec3 bt = glm::cross(n, t);
        float tile = kTile[s.mat];
        auto uv = [&](glm::vec3 p) { return glm::vec2(glm::dot(p, t), -glm::dot(p, bt)) / tile; };
        glm::vec3 wn = glm::normalize(D(n)), wt = glm::normalize(D(t));
        GLuint base = (GLuint)out.v.size();
        out.v.push_back({ P(a), wn, wt, uv(a), s.tint, (float)s.mat, s.ao, s.emis });
        out.v.push_back({ P(b), wn, wt, uv(b), s.tint, (float)s.mat, s.ao, s.emis });
        out.v.push_back({ P(c), wn, wt, uv(c), s.tint, (float)s.mat, s.ao, s.emis });
        out.i.insert(out.i.end(), { base, base + 1, base + 2 });
    }
    // Box faces: 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z. Bits in `skip` drop faces.
    void box(glm::vec3 mn, glm::vec3 mx, const Surf f[6], unsigned skip = 0, MeshOut* out = nullptr) {
        MeshOut& o = out ? *out : opaque;
        static const glm::vec3 N[6] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
        static const glm::vec3 T[6] = { {0,0,-1}, {0,0,1}, {1,0,0}, {1,0,0}, {1,0,0}, {-1,0,0} };
        glm::vec3 c = (mn + mx) * 0.5f, h = (mx - mn) * 0.5f;
        for (int i = 0; i < 6; ++i) {
            if (skip & (1u << i)) continue;
            glm::vec3 n = N[i], t = T[i], b = glm::cross(n, t);
            glm::vec3 fc = c + n * glm::dot(glm::abs(n), h);
            float ht = glm::dot(glm::abs(t), h), hb = glm::dot(glm::abs(b), h);
            quad(o, fc - t * ht - b * hb, fc + t * ht - b * hb, fc + t * ht + b * hb, fc - t * ht + b * hb, n, t, f[i]);
        }
    }
    void box(glm::vec3 mn, glm::vec3 mx, const Surf& s, unsigned skip = 0, MeshOut* out = nullptr) {
        Surf f[6] = { s, s, s, s, s, s };
        box(mn, mx, f, skip, out);
    }
    // Vertical n-gon prism (barrels, columns, the fountain)
    void prism(glm::vec3 c, float r, float y0, float y1, int sides, const Surf& side, const Surf* top = nullptr,
               const Surf* bottom = nullptr, bool inward = false) {
        for (int i = 0; i < sides; ++i) {
            float a0 = i * 6.2831853f / sides, a1 = (i + 1) * 6.2831853f / sides, am = (a0 + a1) * 0.5f;
            glm::vec3 p0 = c + glm::vec3(std::cos(a0) * r, 0, std::sin(a0) * r), p1 = c + glm::vec3(std::cos(a1) * r, 0, std::sin(a1) * r);
            glm::vec3 n(std::cos(am), 0, std::sin(am));
            if (inward) { n = -n; std::swap(p0, p1); }
            glm::vec3 t = glm::normalize(p1 - p0);
            // planar UV per face: continuous around via the arc length offset
            Surf s = side;
            quad(opaque, glm::vec3(p0.x, y0, p0.z), glm::vec3(p1.x, y0, p1.z), glm::vec3(p1.x, y1, p1.z), glm::vec3(p0.x, y1, p0.z), n, t, s);
        }
        auto cap = [&](float y, bool up, const Surf& s) {
            for (int i = 0; i < sides; ++i) {
                float a0 = i * 6.2831853f / sides, a1 = (i + 1) * 6.2831853f / sides;
                glm::vec3 p0 = c + glm::vec3(std::cos(a0) * r, 0, std::sin(a0) * r), p1 = c + glm::vec3(std::cos(a1) * r, 0, std::sin(a1) * r);
                glm::vec3 cc(c.x, y, c.z), q0(p0.x, y, p0.z), q1(p1.x, y, p1.z);
                if (up) tri(opaque, cc, q1, q0, { 0, 1, 0 }, { 1, 0, 0 }, s);
                else tri(opaque, cc, q0, q1, { 0, -1, 0 }, { 1, 0, 0 }, s);
            }
        };
        if (top) cap(y1, true, *top);
        if (bottom) cap(y0, false, *bottom);
    }
    // Ring (a basin rim): outer wall, inner wall and a flat top
    void ring(glm::vec3 c, float rIn, float rOut, float y0, float y1, int sides, const Surf& s) {
        prism(c, rOut, y0, y1, sides, s);
        prism(c, rIn, y0, y1, sides, s, nullptr, nullptr, true);
        for (int i = 0; i < sides; ++i) {
            float a0 = i * 6.2831853f / sides, a1 = (i + 1) * 6.2831853f / sides;
            glm::vec3 i0 = c + glm::vec3(std::cos(a0) * rIn, y1 - c.y, std::sin(a0) * rIn), i1 = c + glm::vec3(std::cos(a1) * rIn, y1 - c.y, std::sin(a1) * rIn);
            glm::vec3 o0 = c + glm::vec3(std::cos(a0) * rOut, y1 - c.y, std::sin(a0) * rOut), o1 = c + glm::vec3(std::cos(a1) * rOut, y1 - c.y, std::sin(a1) * rOut);
            quad(opaque, i0, i1, o1, o0, { 0, 1, 0 }, { 1, 0, 0 }, s);
        }
    }
    // Alpha-tested card (flowers, fire): double sided, uv 0..1
    void card(glm::vec3 c, glm::vec3 halfU, glm::vec3 halfV, const Surf& s) {
        glm::vec3 n = glm::normalize(glm::cross(halfU, halfV));
        glm::vec3 wn = glm::normalize(D(n)), wt = glm::normalize(D(halfU));
        glm::vec3 a = c - halfU - halfV, b = c + halfU - halfV, cc = c + halfU + halfV, d = c - halfU + halfV;
        Vtx q[4] = { { P(a), wn, wt, { 0, 1 }, s.tint, (float)s.mat, s.ao, s.emis }, { P(b), wn, wt, { 1, 1 }, s.tint, (float)s.mat, s.ao, s.emis },
                     { P(cc), wn, wt, { 1, 0 }, s.tint, (float)s.mat, s.ao, s.emis }, { P(d), wn, wt, { 0, 0 }, s.tint, (float)s.mat, s.ao, s.emis } };
        GLuint base = (GLuint)cards.v.size();
        cards.v.insert(cards.v.end(), { q[0], q[1], q[2], q[3] });
        cards.i.insert(cards.i.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
    }
    void pane(glm::vec3 mn, glm::vec3 mx) {   // window glass, a thin quad facing local z
        Surf g(GLASS);
        quad(glass, { mn.x, mn.y, mn.z }, { mx.x, mn.y, mn.z }, { mx.x, mx.y, mn.z }, { mn.x, mx.y, mn.z }, { 0, 0, -1 }, { 1, 0, 0 }, g);
    }

    // --- Gameplay registration (current frame) ---
    void solid(glm::vec3 mn, glm::vec3 mx) {
        glm::vec3 c = P((mn + mx) * 0.5f);
        glm::vec2 ax = glm::normalize(glm::vec2(xf.ax.x, xf.ax.z));
        Village::Collider col;
        col.c = glm::vec2(c.x, c.z); col.ax = ax; col.half = glm::vec2(mx.x - mn.x, mx.z - mn.z) * 0.5f;
        // the y axis is never rotated, so heights are simply offset
        col.y0 = xf.o.y + mn.y;
        col.y1 = xf.o.y + mx.y;
        col.radius = 0.0f;
        v.colliders_.push_back(col);
    }
    void pillar(glm::vec3 c, float r, float y0, float y1) {
        glm::vec3 w = P(c);
        Village::Collider col;
        col.c = glm::vec2(w.x, w.z); col.ax = glm::vec2(1, 0); col.half = glm::vec2(r); col.y0 = xf.o.y + y0; col.y1 = xf.o.y + y1; col.radius = r;
        v.colliders_.push_back(col);
    }
    void walk(float x0, float z0, float x1, float z1, float h0, float h1) {   // ramp rises along local x
        glm::vec3 c = P(glm::vec3((x0 + x1) * 0.5f, 0, (z0 + z1) * 0.5f));
        Village::Walkable w;
        w.c = glm::vec2(c.x, c.z); w.ax = glm::normalize(glm::vec2(xf.ax.x, xf.ax.z));
        w.half = glm::vec2(x1 - x0, z1 - z0) * 0.5f; w.h0 = xf.o.y + h0; w.h1 = xf.o.y + h1;
        v.walkables_.push_back(w);
    }
    int currentHouse = -1;
    void light(glm::vec3 p, glm::vec3 color, float radius, bool outdoor, float flicker = 0.0f) {
        v.lights_.push_back({ P(p), color, radius, outdoor, flicker, outdoor ? -1 : currentHouse });
    }

    // --- Small things ---
    void flowers(glm::vec3 c, float w, float h, glm::vec3 color) {
        Surf s(LEAVES, color);
        card(c + glm::vec3(0, h * 0.5f, 0), glm::vec3(w * 0.5f, 0, 0), glm::vec3(0, h * 0.5f, 0), s);
        card(c + glm::vec3(0, h * 0.45f, 0), glm::vec3(w * 0.35f, 0, w * 0.2f), glm::vec3(0, h * 0.45f, 0), s);
        card(c + glm::vec3(0, h * 0.4f, 0), glm::vec3(w * 0.35f, 0, -w * 0.2f), glm::vec3(0, h * 0.4f, 0), s);
    }
    void lantern(glm::vec3 c, float s, bool outdoor) {   // metal frame with glowing glass
        Surf metal(METAL, glm::vec3(0.18f, 0.17f, 0.16f));
        Surf glow(EMISSIVE, glm::vec3(1.0f, 0.72f, 0.38f), 1.0f, 3.2f);
        box(c - glm::vec3(s * 0.5f, s * 0.7f, s * 0.5f), c + glm::vec3(s * 0.5f, -s * 0.55f, s * 0.5f), metal);
        box(c - glm::vec3(s * 0.38f, s * 0.55f, s * 0.38f), c + glm::vec3(s * 0.38f, s * 0.45f, s * 0.38f), glow);
        box(c + glm::vec3(-s * 0.55f, s * 0.45f, -s * 0.55f), c + glm::vec3(s * 0.55f, s * 0.6f, s * 0.55f), metal);
        box(c + glm::vec3(-s * 0.25f, s * 0.6f, -s * 0.25f), c + glm::vec3(s * 0.25f, s * 0.78f, s * 0.25f), metal);
        for (int i = 0; i < 4; ++i) {   // corner bars
            float sx = (i & 1) ? 1.0f : -1.0f, sz = (i & 2) ? 1.0f : -1.0f;
            glm::vec3 k(sx * s * 0.42f, 0, sz * s * 0.42f);
            box(c + k - glm::vec3(0.012f, s * 0.55f, 0.012f), c + k + glm::vec3(0.012f, s * 0.45f, 0.012f), metal);
        }
        (void)outdoor;
    }
    void candle(glm::vec3 base) {
        Surf wax(PLASTER, glm::vec3(0.98f, 0.95f, 0.85f), 1.0f, 0.15f);
        prism(base, 0.025f, base.y, base.y + 0.12f, 6, wax, &wax);
        Surf flame(FIRE, glm::vec3(1.0f), 1.0f, 1.0f);
        card(base + glm::vec3(0, 0.17f, 0), glm::vec3(0.025f, 0, 0), glm::vec3(0, 0.05f, 0), flame);
        card(base + glm::vec3(0, 0.17f, 0), glm::vec3(0, 0, 0.025f), glm::vec3(0, 0.05f, 0), flame);
    }
    void barrel(glm::vec3 p, float h = 0.9f) {
        Surf wood(WOOD, glm::vec3(0.95f, 0.8f, 0.65f));
        Surf metal(METAL, glm::vec3(0.22f, 0.2f, 0.18f));
        prism(p, 0.32f, p.y, p.y + h, 10, wood, &wood);
        for (float y : { 0.12f, h - 0.16f }) prism(p, 0.335f, p.y + y, p.y + y + 0.05f, 10, metal);
        pillar(p, 0.34f, p.y, p.y + h);
    }
    void crate(glm::vec3 p, float s, float yaw) {
        push(p, yaw);
        Surf wood(WOOD, glm::vec3(1.05f, 0.92f, 0.75f));
        box({ -s * 0.5f, 0, -s * 0.5f }, { s * 0.5f, s, s * 0.5f }, wood);
        Surf dark(WOOD, glm::vec3(0.6f, 0.5f, 0.4f));
        for (float y : { 0.02f, s - 0.06f }) box({ -s * 0.52f, y, -s * 0.52f }, { s * 0.52f, y + 0.04f, s * 0.52f }, dark);
        solid({ -s * 0.5f, 0, -s * 0.5f }, { s * 0.5f, s, s * 0.5f });
        pop();
    }

    // --- Furniture (current frame = house; F = floor height of the storey) ---
    void chair(float x, float z, float yaw, float F) {
        push({ x, F, z }, yaw);
        Surf wood(WOOD, glm::vec3(1.0f, 0.88f, 0.75f));
        box({ -0.22f, 0.43f, -0.22f }, { 0.22f, 0.48f, 0.22f }, wood);
        for (int i = 0; i < 4; ++i) {
            float sx = (i & 1) ? 0.18f : -0.18f, sz = (i & 2) ? 0.18f : -0.18f;
            box({ sx - 0.025f, 0, sz - 0.025f }, { sx + 0.025f, 0.43f, sz + 0.025f }, wood);
        }
        box({ -0.22f, 0.48f, 0.17f }, { -0.17f, 0.98f, 0.22f }, wood);
        box({ 0.17f, 0.48f, 0.17f }, { 0.22f, 0.98f, 0.22f }, wood);
        box({ -0.2f, 0.72f, 0.18f }, { 0.2f, 0.94f, 0.21f }, wood);
        pop();
    }
    void table(float x, float z, float F, const glm::vec3& rugColor) {
        Surf wood(WOOD, glm::vec3(1.1f, 0.95f, 0.8f));
        Surf rug(FABRIC, rugColor);
        box({ x - 1.35f, F, z - 0.95f }, { x + 1.35f, F + 0.012f, z + 0.95f }, rug);
        box({ x - 0.8f, F + 0.72f, z - 0.45f }, { x + 0.8f, F + 0.78f, z + 0.45f }, wood);
        for (int i = 0; i < 4; ++i) {
            float sx = (i & 1) ? 0.7f : -0.7f, sz = (i & 2) ? 0.36f : -0.36f;
            box({ x + sx - 0.04f, F, z + sz - 0.04f }, { x + sx + 0.04f, F + 0.72f, z + sz + 0.04f }, wood);
        }
        solid({ x - 0.8f, F, z - 0.45f }, { x + 0.8f, F + 0.78f, z + 0.45f });
        chair(x - 1.1f, z, 90, F);
        chair(x + 1.1f, z, -90, F);
        chair(x - 0.35f, z - 0.75f, 0, F);
        chair(x + 0.35f, z + 0.75f, 180, F);
        // A bowl of fruit and a candle
        Surf bowl(STONE, glm::vec3(0.85f, 0.5f, 0.32f));
        prism({ x + 0.2f, F + 0.78f, z }, 0.17f, F + 0.78f, F + 0.86f, 8, bowl, &bowl);
        Surf fruit(FABRIC, glm::vec3(0.9f, 0.35f, 0.12f), 1.0f, 0.0f);
        for (int i = 0; i < 3; ++i) {
            glm::vec3 c(x + 0.13f + 0.07f * i, F + 0.9f, z - 0.03f + 0.04f * (i % 2));
            box(c - glm::vec3(0.035f), c + glm::vec3(0.035f), Surf(FABRIC, i == 1 ? glm::vec3(0.95f, 0.75f, 0.15f) : fruit.tint));
        }
        candle({ x - 0.35f, F + 0.78f, z + 0.1f });
    }
    void bed(float x0, float z0, bool alongX, float F, const glm::vec3& blanket, bool standLeft) {
        // footprint 1.5 wide x 2.1 long; the head is at the far end of the length axis
        // (alongX: the bed lies along +x, its width covers z0 .. z0 + 1.5)
        push({ x0, F, alongX ? z0 + 1.5f : z0 }, alongX ? -90.0f : 0.0f);
        Surf wood(WOOD, glm::vec3(0.95f, 0.8f, 0.65f));
        Surf sheet(FABRIC, glm::vec3(1.25f, 1.22f, 1.15f));
        Surf cover(FABRIC, blanket);
        box({ 0, 0, 0 }, { 1.5f, 0.35f, 2.1f }, wood);
        box({ 0, 0, 2.05f }, { 1.5f, 1.05f, 2.14f }, wood);
        box({ 0, 0, -0.04f }, { 1.5f, 0.6f, 0.03f }, wood);
        box({ 0.05f, 0.35f, 0.05f }, { 1.45f, 0.55f, 2.02f }, sheet);
        box({ 0.02f, 0.55f, 0.02f }, { 1.48f, 0.61f, 1.45f }, cover);
        box({ 0.0f, 0.2f, 0.02f }, { 0.02f, 0.61f, 1.45f }, cover);
        box({ 1.48f, 0.2f, 0.02f }, { 1.5f, 0.61f, 1.45f }, cover);
        box({ 0.2f, 0.55f, 1.6f }, { 0.68f, 0.68f, 1.95f }, sheet);
        box({ 0.82f, 0.55f, 1.6f }, { 1.3f, 0.68f, 1.95f }, sheet);
        solid({ 0, 0, 0 }, { 1.5f, 0.6f, 2.14f });
        // Night stand with a candle beside the head
        float sx = standLeft ? -0.55f : 1.6f;
        box({ sx, 0, 1.65f }, { sx + 0.45f, 0.55f, 2.12f }, wood);
        solid({ sx, 0, 1.65f }, { sx + 0.45f, 0.55f, 2.12f });
        candle({ sx + 0.22f, 0.55f, 1.88f });
        pop();
    }
    void wardrobe(glm::vec3 mn, glm::vec3 mx) {
        Surf wood(WOOD, glm::vec3(0.9f, 0.75f, 0.6f));
        Surf dark(WOOD, glm::vec3(0.55f, 0.45f, 0.35f));
        box(mn, mx, wood);
        glm::vec3 c = (mn + mx) * 0.5f;
        bool facesX = (mx.x - mn.x) < (mx.z - mn.z);
        if (facesX) box({ mn.x - 0.01f, mn.y + 0.1f, c.z - 0.01f }, { mx.x + 0.01f, mx.y - 0.1f, c.z + 0.01f }, dark);
        else box({ c.x - 0.01f, mn.y + 0.1f, mn.z - 0.01f }, { c.x + 0.01f, mx.y - 0.1f, mx.z + 0.01f }, dark);
        box({ mn.x - 0.03f, mx.y, mn.z - 0.03f }, { mx.x + 0.03f, mx.y + 0.06f, mx.z + 0.03f }, wood);
        solid(mn, mx);
    }
    void cabinetWithJars(glm::vec3 mn, glm::vec3 mx) {
        wardrobe(mn, mx);
        static const glm::vec3 jarTints[3] = { { 0.85f, 0.5f, 0.32f }, { 0.35f, 0.5f, 0.75f }, { 0.9f, 0.85f, 0.7f } };
        glm::vec3 c = (mn + mx) * 0.5f;
        bool alongZ = (mx.z - mn.z) > (mx.x - mn.x);
        for (int i = 0; i < 3; ++i) {
            float o = (i - 1) * 0.42f;
            glm::vec3 p = alongZ ? glm::vec3(c.x, mx.y + 0.06f, c.z + o) : glm::vec3(c.x + o, mx.y + 0.06f, c.z);
            Surf jar(STONE, jarTints[i]);
            prism(p, 0.09f + 0.02f * i, p.y, p.y + 0.22f + 0.06f * (i % 2), 8, jar, &jar);
        }
    }
    void hangingLamp(float x, float z, float ceilingY) {
        Surf metal(METAL, glm::vec3(0.2f, 0.19f, 0.17f));
        box({ x - 0.01f, ceilingY - 0.55f, z - 0.01f }, { x + 0.01f, ceilingY, z + 0.01f }, metal);
        lantern({ x, ceilingY - 0.75f, z }, 0.3f, false);
        light({ x, ceilingY - 0.85f, z }, glm::vec3(1.0f, 0.68f, 0.38f) * 2.4f, 7.5f, false, 0.03f);
    }

    // --- Houses ---
    struct Opening { float a, b, y0, y1; int kind; bool flowerBox; };   // kind 0 window, 1 door, 2 french window
    void house(const Village::House& h);
    void wall(const Village::House& h, float len, float y0, float y1, std::vector<Opening> ops, bool isFront, int storey);
    void roof(const Village::House& h, float top);
    void fireplace(const Village::House& h, float X1);
    void stairs(float xs0, float xs1, float z0, float z1, float Fa, float Fb);
    void quoins(const Village::House& h, float top);

    // --- Plaza and streets ---
    void fountain(glm::vec3 c);
    void bench(glm::vec3 p, float yaw);
    void streetLamp(glm::vec3 p);
    void stall(glm::vec3 p, float yaw, glm::vec3 stripeA, glm::vec3 stripeB);
    void planter(glm::vec3 p, float yaw, glm::vec3 flowerColor);
};

// Wall in its own frame: x along the wall (0..len), outer face at z = 0 (facing -z), inner at z = WT
void VillageBuilder::wall(const Village::House& h, float len, float y0, float y1, std::vector<Opening> ops, bool isFront, int storey) {
    std::sort(ops.begin(), ops.end(), [](const Opening& a, const Opening& b) { return a.a < b.a; });
    Surf outS(PLASTER, h.wall), inS(INTERIOR, h.interior, 0.55f), rev(PLASTER, h.wall * 1.04f, 0.85f);
    Surf faces[6] = { rev, rev, rev, rev, inS, outS };
    auto piece = [&](float xa, float xb, float ya, float yb) {
        if (xb - xa < 0.01f || yb - ya < 0.01f) return;
        box({ xa, ya, 0 }, { xb, yb, WT }, faces);
        solid({ xa, ya, 0 }, { xb, yb, WT });
    };
    float cursor = 0.0f;
    for (const Opening& o : ops) {
        piece(cursor, o.a, y0, y1);
        piece(o.a, o.b, y0, o.y0);
        piece(o.a, o.b, o.y1, y1);
        cursor = o.b;
    }
    piece(cursor, len, y0, y1);

    Surf trim(WOOD, h.trim), sill(STONE, glm::vec3(1.05f, 1.0f, 0.95f)), shutter(PAINTED, h.accent);
    for (const Opening& o : ops) {
        float wdt = o.b - o.a;
        if (o.kind == 1) {
            // Door: frame, stone lintel and an open door leaf swung into the room
            box({ o.a - 0.07f, o.y0, -0.02f }, { o.a, o.y1 + 0.07f, 0.12f }, trim);
            box({ o.b, o.y0, -0.02f }, { o.b + 0.07f, o.y1 + 0.07f, 0.12f }, trim);
            box({ o.a - 0.07f, o.y1, -0.02f }, { o.b + 0.07f, o.y1 + 0.07f, 0.12f }, trim);
            box({ o.a - 0.22f, o.y1 + 0.07f, -0.05f }, { o.b + 0.22f, o.y1 + 0.25f, 0.06f }, sill);
            push({ o.a + 0.03f, o.y0, WT - 0.02f }, -105.0f);
            Surf leaf(PAINTED, h.accent * 0.9f);
            box({ 0, 0, 0 }, { wdt - 0.06f, o.y1 - o.y0 - 0.02f, 0.05f }, leaf);
            Surf knob(METAL, glm::vec3(0.6f, 0.5f, 0.3f));
            box({ wdt - 0.22f, 1.0f, -0.04f }, { wdt - 0.16f, 1.06f, 0.09f }, knob);
            solid({ 0, 0, 0 }, { wdt - 0.06f, o.y1 - o.y0, 0.05f });
            pop();
            continue;
        }
        // Window: frame, cross mullions, glass, stone sill and painted shutters folded open
        float fz0 = 0.06f, fz1 = 0.16f;
        box({ o.a, o.y0, fz0 }, { o.a + 0.07f, o.y1, fz1 }, trim);
        box({ o.b - 0.07f, o.y0, fz0 }, { o.b, o.y1, fz1 }, trim);
        box({ o.a, o.y1 - 0.07f, fz0 }, { o.b, o.y1, fz1 }, trim);
        box({ o.a, o.y0, fz0 }, { o.b, o.y0 + 0.06f, fz1 }, trim);
        float mx = (o.a + o.b) * 0.5f;
        box({ mx - 0.025f, o.y0, fz0 + 0.02f }, { mx + 0.025f, o.y1, fz1 - 0.02f }, trim);
        if (o.kind == 0) box({ o.a, (o.y0 + o.y1) * 0.5f + 0.1f, fz0 + 0.02f }, { o.b, (o.y0 + o.y1) * 0.5f + 0.15f, fz1 - 0.02f }, trim);
        pane({ o.a + 0.05f, o.y0 + 0.05f, 0.11f }, { o.b - 0.05f, o.y1 - 0.05f, 0.11f });
        if (o.kind == 0) box({ o.a - 0.08f, o.y0 - 0.07f, -0.09f }, { o.b + 0.08f, o.y0, 0.1f }, sill);
        float sw = wdt * 0.5f + 0.03f;
        box({ o.a - sw - 0.02f, o.y0 - 0.02f, -0.05f }, { o.a - 0.02f, o.y1 + 0.02f, -0.01f }, shutter);
        box({ o.b + 0.02f, o.y0 - 0.02f, -0.05f }, { o.b + sw + 0.02f, o.y1 + 0.02f, -0.01f }, shutter);
        Surf slat(PAINTED, h.accent * 0.75f);
        for (float t = 0.25f; t < 0.95f; t += 0.25f) {
            float y = o.y0 + (o.y1 - o.y0) * t;
            box({ o.a - sw - 0.02f, y - 0.015f, -0.065f }, { o.a - 0.02f, y + 0.015f, -0.05f }, slat);
            box({ o.b + 0.02f, y - 0.015f, -0.065f }, { o.b + sw + 0.02f, y + 0.015f, -0.05f }, slat);
        }
        if (o.flowerBox) {
            Surf boxS(WOOD, glm::vec3(0.9f, 0.7f, 0.5f));
            box({ o.a - 0.05f, o.y0 - 0.32f, -0.32f }, { o.b + 0.05f, o.y0 - 0.08f, -0.09f }, boxS);
            static const glm::vec3 bloom[4] = { { 0.95f, 0.2f, 0.25f }, { 1.0f, 0.55f, 0.75f }, { 1.0f, 0.95f, 0.9f }, { 0.65f, 0.35f, 0.95f } };
            glm::vec3 col = bloom[(h.seed + storey * 3 + (int)(o.a * 7)) % 4];
            for (float x = o.a + 0.18f; x < o.b - 0.1f; x += 0.38f) flowers({ x, o.y0 - 0.12f, -0.2f }, 0.5f, 0.38f, col);
        }
    }
    (void)isFront;
}

void VillageBuilder::stairs(float xs0, float xs1, float z0, float z1, float Fa, float Fb) {
    int n = 15;
    float run = (xs1 - xs0) / n, rise = (Fb - Fa) / n;
    Surf step(FLOOR, glm::vec3(0.95f, 0.85f, 0.75f), 0.8f), side(WOOD, glm::vec3(0.8f, 0.68f, 0.55f), 0.7f);
    for (int i = 0; i < n; ++i) {
        Surf f[6] = { side, side, step, side, side, side };
        box({ xs0 + i * run, Fa, z0 }, { xs0 + (i + 1) * run, Fa + (i + 1) * rise, z1 }, f, (1u << 3));
    }
    walk(xs0, z0, xs1, z1, Fa, Fb);
    // The space under the upper flight is solid (you can't walk through the staircase)
    solid({ xs0 + 1.6f, Fa, z0 }, { xs1, Fa + 1.0f, z1 });
    // Handrail along the open side
    Surf rail(WOOD, glm::vec3(0.7f, 0.55f, 0.42f));
    for (int i = 2; i <= n; i += 3) {
        float x = xs0 + i * run - run * 0.5f, y = Fa + i * rise;
        box({ x - 0.03f, y, z0 + 0.02f }, { x + 0.03f, y + 0.9f, z0 + 0.08f }, rail);
    }
    glm::vec3 a(xs0 + run, Fa + rise + 0.9f, z0 + 0.05f), b(xs1, Fb + 0.9f, z0 + 0.05f);
    glm::vec3 dir = glm::normalize(b - a);
    glm::vec3 n2(0, 0, -1), up = glm::cross(n2, dir);
    float len = glm::length(b - a);
    push(a, 0);
    // a slanted rail: quad strip along dir
    quad(opaque, { 0, -0.03f, -0.04f }, dir * len + glm::vec3(0, -0.03f, -0.04f), dir * len + glm::vec3(0, 0.04f, -0.04f), { 0, 0.04f, -0.04f }, n2, dir, rail);
    quad(opaque, { 0, 0.04f, -0.04f }, dir * len + glm::vec3(0, 0.04f, -0.04f), dir * len + glm::vec3(0, 0.04f, 0.04f), { 0, 0.04f, 0.04f }, up, dir, rail);
    pop();
}

void VillageBuilder::fireplace(const Village::House& h, float X1) {
    Surf stone(STONE, glm::vec3(1.0f, 0.95f, 0.9f), 0.8f), soot(STONE, glm::vec3(0.18f, 0.16f, 0.15f), 0.5f);
    Surf wood(WOOD, glm::vec3(0.9f, 0.72f, 0.55f));
    float top = F0 + SH - 0.15f;
    box({ X1 - 0.75f, F0, -1.0f }, { X1, F0 + 0.16f, 1.0f }, stone);
    box({ X1 - 0.55f, F0 + 0.16f, -0.85f }, { X1, F0 + 1.3f, -0.5f }, stone);
    box({ X1 - 0.55f, F0 + 0.16f, 0.5f }, { X1, F0 + 1.3f, 0.85f }, stone);
    box({ X1 - 0.14f, F0 + 0.16f, -0.5f }, { X1, F0 + 1.3f, 0.5f }, soot);
    box({ X1 - 0.55f, F0 + 1.15f, -0.5f }, { X1 - 0.14f, F0 + 1.3f, 0.5f }, soot);
    box({ X1 - 0.5f, F0 + 1.3f, -0.8f }, { X1, top, 0.8f }, stone);
    box({ X1 - 0.68f, F0 + 1.3f, -1.0f }, { X1, F0 + 1.4f, 1.0f }, wood);
    // Logs and flames
    for (int i = 0; i < 3; ++i) {
        push({ X1 - 0.32f, F0 + 0.22f + i * 0.06f, -0.15f + i * 0.12f }, 20.0f + i * 55.0f);
        box({ -0.28f, -0.05f, -0.05f }, { 0.28f, 0.05f, 0.05f }, Surf(WOOD, glm::vec3(0.45f, 0.32f, 0.22f)));
        pop();
    }
    Surf fire(FIRE, glm::vec3(1.0f), 1.0f, 1.0f);
    card({ X1 - 0.3f, F0 + 0.55f, 0 }, { 0, 0, 0.33f }, { 0, 0.33f, 0 }, fire);
    card({ X1 - 0.3f, F0 + 0.55f, 0 }, { 0.2f, 0, 0.22f }, { 0, 0.3f, 0 }, fire);
    card({ X1 - 0.3f, F0 + 0.55f, 0 }, { -0.2f, 0, 0.22f }, { 0, 0.28f, 0 }, fire);
    light({ X1 - 0.85f, F0 + 0.6f, 0 }, glm::vec3(1.0f, 0.45f, 0.15f) * 2.8f, 7.0f, false, 0.35f);
    solid({ X1 - 0.75f, F0, -1.0f }, { X1, F0 + SH, 1.0f });
    // Things on the mantel
    candle({ X1 - 0.4f, F0 + 1.4f, -0.7f });
    candle({ X1 - 0.4f, F0 + 1.4f, 0.7f });
    Surf jar(STONE, glm::vec3(0.85f, 0.5f, 0.32f));
    prism({ X1 - 0.38f, F0 + 1.4f, 0.1f }, 0.1f, F0 + 1.4f, F0 + 1.68f, 8, jar, &jar);
    (void)h;
}

void VillageBuilder::quoins(const Village::House& h, float top) {
    Surf q(STONE, glm::vec3(1.08f, 1.04f, 0.98f));
    float hw = h.w * 0.5f, hd = h.d * 0.5f, p = 0.035f;
    int k = 0;
    for (float y = F0; y + 0.4f <= top + 0.01f; y += 0.45f, ++k) {
        float lx = (k % 2) ? 0.5f : 0.28f, lz = (k % 2) ? 0.28f : 0.5f;
        for (int c = 0; c < 4; ++c) {
            float sx = (c & 1) ? 1.0f : -1.0f, sz = (c & 2) ? 1.0f : -1.0f;
            glm::vec3 a(sx * (hw + p), y, sz * (hw > 0 ? hd + p : 0)), b(sx * (hw - lx), y + 0.4f, sz * (hd - lz));
            box(glm::min(a, b), glm::max(a, b), q);
        }
    }
}

void VillageBuilder::roof(const Village::House& h, float top) {
    float hw = h.w * 0.5f, hd = h.d * 0.5f;
    float pitch = glm::radians(34.0f + 6.0f * ((h.seed >> 3) % 3) / 2.0f);
    float rise = (hd + 0.1f) * std::tan(pitch);
    float OH = 0.55f, GO = 0.35f, RT = 0.16f;
    float ridge = top + rise;
    Surf tiles(ROOF, h.roof), soffit(WOOD, glm::vec3(0.75f, 0.62f, 0.5f), 0.7f), fascia(WOOD, glm::vec3(0.6f, 0.48f, 0.38f));
    float eaveY = top - OH * std::tan(pitch);
    float D = hd + OH;
    for (int s : { -1, 1 }) {
        glm::vec3 n = glm::normalize(glm::vec3(0, D, s * (ridge - eaveY)));
        glm::vec3 t(1, 0, 0);
        glm::vec3 e0(-hw - GO, eaveY, s * D), e1(hw + GO, eaveY, s * D), r0(-hw - GO, ridge, 0), r1(hw + GO, ridge, 0);
        glm::vec3 off = n * RT;
        // Tiles on top; plank soffit underneath; fascia boards along the eave and barge boards at the gables
        if (s < 0) quad(opaque, e0 + off, e1 + off, r1 + off, r0 + off, n, t, tiles);
        else quad(opaque, e1 + off, e0 + off, r0 + off, r1 + off, n, -t, tiles);
        if (s < 0) quad(opaque, r0, r1, e1, e0, -n, -t, soffit);
        else quad(opaque, r1, r0, e0, e1, -n, t, soffit);
        glm::vec3 fn(0, 0, (float)s);
        if (s < 0) quad(opaque, e0, e1, e1 + off, e0 + off, fn, { 1, 0, 0 }, fascia);
        else quad(opaque, e1, e0, e0 + off, e1 + off, fn, { -1, 0, 0 }, fascia);
        for (int g : { -1, 1 }) {
            glm::vec3 gn((float)g, 0, 0);
            glm::vec3 a = g < 0 ? e0 : e1, b = g < 0 ? r0 : r1;
            glm::vec3 tt = glm::normalize(b - a);
            if (g * s > 0) quad(opaque, a, b, b + off, a + off, gn, tt, fascia);
            else quad(opaque, b, a, a + off, b + off, gn, -tt, fascia);
        }
    }
    // Ridge cap
    Surf cap(ROOF, h.roof * 0.8f);
    box({ -hw - GO - 0.05f, ridge + RT * 0.6f, -0.17f }, { hw + GO + 0.05f, ridge + RT + 0.14f, 0.17f }, cap);
    // Gables (outside plaster, inside interior plaster)
    Surf gableOut(PLASTER, h.wall), gableIn(INTERIOR, h.interior, 0.5f);
    for (int g : { -1, 1 }) {
        float xo = g * hw, xi = g * (hw - WT);
        glm::vec3 a(xo, top, -hd), b(xo, top, hd), c(xo, ridge + 0.05f, 0);
        if (g > 0) tri(opaque, a, c, b, { 1, 0, 0 }, { 0, 0, -1 }, gableOut);
        else tri(opaque, b, c, a, { -1, 0, 0 }, { 0, 0, 1 }, gableOut);
        glm::vec3 ai(xi, top, -hd + WT), bi(xi, top, hd - WT), ci(xi, ridge - 0.1f, 0);
        if (g > 0) tri(opaque, bi, ci, ai, { -1, 0, 0 }, { 0, 0, 1 }, gableIn);
        else tri(opaque, ai, ci, bi, { 1, 0, 0 }, { 0, 0, -1 }, gableIn);
    }
    // Chimney above the fireplace
    if (h.fireplace) {
        Surf stone(STONE), dark(STONE, glm::vec3(0.2f));
        float cx0 = hw - WT - 0.65f, cx1 = hw - WT + 0.05f;
        box({ cx0, top - 0.3f, -0.42f }, { cx1, ridge + 1.0f, 0.42f }, stone);
        box({ cx0 - 0.08f, ridge + 1.0f, -0.5f }, { cx1 + 0.08f, ridge + 1.12f, 0.5f }, stone);
        box({ cx0 + 0.1f, ridge + 1.12f, -0.3f }, { cx1 - 0.1f, ridge + 1.13f, 0.3f }, dark);
    }
}

void VillageBuilder::house(const Village::House& h) {
    xf = Xf();
    stack.clear();
    xf.o = glm::vec3(h.c.x, h.padY, h.c.y);
    xf.ax = glm::vec3(h.ex.x, 0, h.ex.y);
    xf.az = glm::vec3(h.ez.x, 0, h.ez.y);
    std::mt19937 local(h.seed);
    auto r = [&]() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(local); };

    const float hw = h.w * 0.5f, hd = h.d * 0.5f;
    const float X0 = -hw + WT, X1 = hw - WT, Z0 = -hd + WT, Z1 = hd - WT;
    const float top = F0 + h.storeys * SH;
    const float doorA = h.doorX - 0.55f, doorB = h.doorX + 0.55f;

    // Walls, storey by storey, with windows (and the door on the front)
    for (int k = 0; k < h.storeys; ++k) {
        float y0 = F0 + k * SH, y1 = y0 + SH;
        float sill = y0 + 0.95f, head = y0 + 2.25f;
        auto windowsAlong = [&](float len, float avoidA, float avoidB, bool boxes) {
            std::vector<Opening> o;
            int n = std::max(1, (int)((len - 1.0f) / 2.6f));
            for (int i = 0; i < n; ++i) {
                float c = (i + 0.5f) * len / n;
                if (c + 0.6f > avoidA && c - 0.6f < avoidB) continue;
                o.push_back({ c - 0.5f, c + 0.5f, sill, head, 0, boxes && r() < 0.6f });
            }
            return o;
        };
        // Front (local -z): door on the ground floor
        std::vector<Opening> front;
        if (k == 0) {
            front.push_back({ doorA + hw, doorB + hw, y0, y0 + 2.3f, 1, false });
            for (float c : { (doorA + hw) * 0.5f, (doorB + hw + h.w) * 0.5f })
                if (c > 0.9f && c < h.w - 0.9f && std::fabs(c - (h.doorX + hw)) > 1.6f) front.push_back({ c - 0.5f, c + 0.5f, sill, head, 0, r() < 0.7f });
        } else {
            front = windowsAlong(h.w, -10, -9, true);
            if (h.balcony && k == 1 && !front.empty()) {
                Opening& b = front[front.size() / 2];
                b.kind = 2; b.y0 = y0; b.y1 = y0 + 2.3f; b.flowerBox = false;
            }
        }
        push({ -hw, 0, -hd }, 0.0f);
        wall(h, h.w, y0, y1, front, true, k);
        pop();
        // Back (+z)
        push({ hw, 0, hd }, 180.0f);
        wall(h, h.w, y0, y1, windowsAlong(h.w, -10, -9, k > 0), false, k);
        pop();
        // Right (+x): the fireplace takes the middle of the ground floor
        float side = h.d - 2 * WT;
        push({ hw, 0, -hd + WT }, 90.0f);
        if (k == 0 && h.fireplace) wall(h, side, y0, y1, windowsAlong(side, side * 0.5f - 1.1f, side * 0.5f + 1.1f, false), false, k);
        else wall(h, side, y0, y1, windowsAlong(side, -10, -9, k > 0), false, k);
        pop();
        // Left (-x)
        push({ -hw, 0, hd - WT }, -90.0f);
        wall(h, side, y0, y1, windowsAlong(side, -10, -9, k > 0), false, k);
        pop();
    }
    quoins(h, top);
    roof(h, top);

    // Balcony on the upper floor
    if (h.balcony && h.storeys > 1) {
        float Fb = F0 + SH;
        float cx = -hw + (h.w / std::max(1, (int)((h.w - 1.0f) / 2.6f))) * (std::max(1, (int)((h.w - 1.0f) / 2.6f)) / 2 + 0.5f);
        Surf deck(FLOOR, glm::vec3(0.9f, 0.8f, 0.7f)), rail(WOOD, glm::vec3(0.75f, 0.6f, 0.45f));
        box({ cx - 1.3f, Fb - 0.14f, -hd - 1.05f }, { cx + 1.3f, Fb, -hd }, deck);
        walk(cx - 1.3f, -hd - 1.05f, cx + 1.3f, -hd + 0.01f, Fb, Fb);
        for (float s : { -1.0f, 1.0f }) {
            push({ cx + s * 1.0f, Fb - 0.14f, -hd }, 0);
            box({ -0.06f, -0.55f, -0.9f }, { 0.06f, 0.0f, 0.0f }, rail);
            pop();
        }
        box({ cx - 1.3f, Fb + 0.95f, -hd - 1.05f }, { cx + 1.3f, Fb + 1.02f, -hd - 0.98f }, rail);
        for (float s : { -1.0f, 1.0f }) box({ cx + s * 1.3f - 0.035f, Fb + 0.95f, -hd - 1.05f }, { cx + s * 1.3f + 0.035f, Fb + 1.02f, -hd }, rail);
        for (float x = cx - 1.25f; x <= cx + 1.26f; x += 0.22f) box({ x - 0.02f, Fb, -hd - 1.03f }, { x + 0.02f, Fb + 0.95f, -hd - 1.0f }, rail);
        for (float z = -hd - 0.85f; z < -hd; z += 0.22f)
            for (float s : { -1.0f, 1.0f }) box({ cx + s * 1.28f - 0.02f, Fb, z - 0.02f }, { cx + s * 1.28f + 0.02f, Fb + 0.95f, z + 0.02f }, rail);
        solid({ cx - 1.3f, Fb, -hd - 1.08f }, { cx + 1.3f, Fb + 1.05f, -hd - 0.98f });
        for (float s : { -1.0f, 1.0f }) solid({ cx + s * 1.3f - 0.05f, Fb, -hd - 1.05f }, { cx + s * 1.3f + 0.05f, Fb + 1.05f, -hd });
        static const glm::vec3 bloom[3] = { { 0.95f, 0.2f, 0.3f }, { 1.0f, 0.6f, 0.8f }, { 1.0f, 0.9f, 0.3f } };
        for (float x = cx - 1.0f; x <= cx + 1.01f; x += 0.5f) flowers({ x, Fb + 0.9f, -hd - 1.1f }, 0.45f, 0.35f, bloom[(h.seed + (int)(x * 3)) % 3]);
    }

    // Small roof over the door
    if (h.canopy) {
        float cy = F0 + 2.75f;
        Surf tiles(ROOF, h.roof), wood(WOOD, glm::vec3(0.75f, 0.6f, 0.45f));
        glm::vec3 a(doorA - 0.45f, cy, -hd), b(doorB + 0.45f, cy, -hd), c(doorB + 0.45f, cy - 0.35f, -hd - 1.0f), d(doorA - 0.45f, cy - 0.35f, -hd - 1.0f);
        glm::vec3 n = glm::normalize(glm::cross(b - a, d - a));
        if (n.y < 0) n = -n;
        quad(opaque, d + n * 0.08f, c + n * 0.08f, b + n * 0.08f, a + n * 0.08f, n, { 1, 0, 0 }, tiles);
        quad(opaque, a, b, c, d, -n, { -1, 0, 0 }, wood);
        for (float x : { doorA - 0.35f, doorB + 0.35f }) {
            push({ x, cy - 0.5f, -hd }, 0);
            box({ -0.05f, 0, -0.75f }, { 0.05f, 0.1f, 0 }, wood);
            box({ -0.05f, -0.4f, -0.12f }, { 0.05f, 0.05f, 0 }, wood);
            pop();
        }
    }
    // Lantern beside the door
    {
        Surf metal(METAL, glm::vec3(0.18f, 0.17f, 0.16f));
        float lx = doorB + 0.45f;
        box({ lx - 0.03f, F0 + 2.15f, -hd - 0.32f }, { lx + 0.03f, F0 + 2.21f, -hd }, metal);
        lantern({ lx, F0 + 2.0f, -hd - 0.3f }, 0.24f, true);
        light({ lx, F0 + 1.95f, -hd - 0.55f }, glm::vec3(1.0f, 0.74f, 0.42f) * 1.8f, 9.0f, true, 0.04f);
    }
    // Flower beds along the front
    static const glm::vec3 bloom[4] = { { 0.95f, 0.25f, 0.2f }, { 1.0f, 0.85f, 0.25f }, { 0.95f, 0.55f, 0.85f }, { 0.95f, 0.95f, 1.0f } };
    Surf edging(STONE, glm::vec3(0.95f));
    for (float s : { -1.0f, 1.0f }) {
        float xa = s < 0 ? -hw + 0.3f : doorB + 0.8f, xb = s < 0 ? doorA - 0.8f : hw - 0.3f;
        if (xb - xa < 0.8f) continue;
        box({ xa, 0, -hd - 0.7f }, { xb, 0.18f, -hd - 0.08f }, edging, 0);
        for (float x = xa + 0.3f; x < xb - 0.15f; x += 0.45f) flowers({ x, 0.15f, -hd - 0.4f }, 0.55f, 0.45f, bloom[(h.seed + (int)(x * 5 + 40)) % 4]);
        solid({ xa, 0, -hd - 0.7f }, { xb, 0.18f, -hd - 0.08f });
    }

    // --- Floors, stairs and ceilings (after the shell so the walls hide them early) ---
    // Foundation (stone plinth, deep enough to hide any slope) and the front step
    Surf stone(STONE), floorS(FLOOR, glm::vec3(1.0f), 0.8f), woodS(WOOD, glm::vec3(0.8f, 0.68f, 0.55f), 0.6f);
    box({ -hw - 0.08f, -1.8f, -hd - 0.08f }, { hw + 0.08f, F0 - 0.15f, hd + 0.08f }, stone);
    box({ doorA - 0.3f, -0.6f, -hd - 0.62f }, { doorB + 0.3f, 0.22f, -hd }, stone);
    walk(doorA - 0.3f, -hd - 0.62f, doorB + 0.3f, -hd, 0.22f, 0.22f);
    box({ doorA, F0 - 0.15f, -hd }, { doorB, F0, Z0 }, stone);
    walk(doorA, -hd - 0.01f, doorB, Z0 + 0.01f, F0, F0);

    // Ground floor
    Surf gfloor[6] = { woodS, woodS, floorS, woodS, woodS, woodS };
    box({ X0, F0 - 0.15f, Z0 }, { X1, F0, Z1 }, gfloor);
    walk(X0, Z0, X1, Z1, F0, F0);

    // Stairs and upper floors
    const float sz0 = Z1 - 1.0f, sz1 = Z1;
    const float xs0 = X0 + 0.25f, xs1 = xs0 + 4.05f;
    for (int k = 1; k < h.storeys; ++k) {
        float Fa = F0 + (k - 1) * SH, Fb = F0 + k * SH;
        stairs(xs0, xs1, sz0, sz1, Fa, Fb);
        Surf top6[6] = { woodS, woodS, floorS, Surf(WOOD, glm::vec3(0.85f, 0.72f, 0.58f), 0.55f), woodS, woodS };
        box({ X0, Fb - SLAB, Z0 }, { X1, Fb, sz0 }, top6);
        box({ xs1, Fb - SLAB, sz0 }, { X1, Fb, Z1 }, top6);
        box({ X0, Fb - SLAB, sz0 }, { xs0, Fb, Z1 }, top6);
        walk(X0, Z0, X1, sz0, Fb, Fb);
        walk(xs1, sz0, X1, Z1, Fb, Fb);
        walk(X0, sz0, xs0, Z1, Fb, Fb);
        // Beams under the floor
        for (float x = X0 + 0.9f; x < X1 - 0.3f; x += 1.3f)
            if (x < xs0 - 0.2f || x > xs1 + 0.2f) box({ x - 0.09f, Fb - SLAB - 0.2f, Z0 }, { x + 0.09f, Fb - SLAB, Z1 }, woodS);
            else box({ x - 0.09f, Fb - SLAB - 0.2f, Z0 }, { x + 0.09f, Fb - SLAB, sz0 }, woodS);
        // Railing around the stairwell upstairs
        Surf rail(WOOD, glm::vec3(0.7f, 0.55f, 0.42f));
        box({ xs0, Fb + 0.88f, sz0 - 0.06f }, { xs1 - 0.9f, Fb + 0.95f, sz0 + 0.01f }, rail);
        for (float x = xs0 + 0.15f; x < xs1 - 0.9f; x += 0.35f) box({ x - 0.02f, Fb, sz0 - 0.04f }, { x + 0.02f, Fb + 0.9f, sz0 - 0.01f }, rail);
        solid({ xs0, Fb, sz0 - 0.05f }, { xs1 - 0.9f, Fb + 1.0f, sz0 });
        box({ xs0 - 0.06f, Fb + 0.88f, sz0 }, { xs0 + 0.01f, Fb + 0.95f, Z1 }, rail);
        for (float z = sz0 + 0.2f; z < Z1 - 0.05f; z += 0.35f) box({ xs0 - 0.04f, Fb, z - 0.02f }, { xs0, Fb + 0.88f, z + 0.02f }, rail);
        solid({ xs0 - 0.05f, Fb, sz0 }, { xs0, Fb + 1.0f, Z1 });
    }
    // Ceiling with beams under the roof
    Surf ceil[6] = { woodS, woodS, woodS, Surf(WOOD, glm::vec3(0.85f, 0.72f, 0.58f), 0.55f), woodS, woodS };
    box({ X0, top - 0.12f, Z0 }, { X1, top, Z1 }, ceil);
    for (float x = X0 + 0.7f; x < X1 - 0.3f; x += 1.2f) box({ x - 0.09f, top - 0.32f, Z0 }, { x + 0.09f, top - 0.12f, Z1 }, woodS);

    // --- Interior ---
    const float Fg = F0;
    if (h.fireplace) fireplace(h, X1);
    table(-0.4f + (r() - 0.5f) * 0.6f, -0.3f, Fg, h.fabric);
    if (h.storeys == 1) {
        bed(X0 + 0.1f, Z1 - 2.15f, false, Fg, h.fabric * 0.85f, false);
        wardrobe({ X0, Fg, Z0 + 0.4f }, { X0 + 0.6f, Fg + 2.0f, Z0 + 1.6f });
        cabinetWithJars({ X1 - 1.9f, Fg, Z1 - 0.55f }, { X1 - 0.3f, Fg + 0.95f, Z1 });
    } else {
        cabinetWithJars({ X0, Fg, Z0 + 0.4f }, { X0 + 0.5f, Fg + 0.95f, Z0 + 2.0f });
        float Fu = F0 + SH;
        bed(X1 - 2.15f, Z0 + 0.35f, true, Fu, h.fabric, true);
        wardrobe({ X0, Fu, Z0 + 0.3f }, { X0 + 0.6f, Fu + 2.0f, Z0 + 1.5f });
        Surf rug(FABRIC, h.fabric * 1.1f);
        box({ -1.2f, Fu, -0.9f }, { 1.0f, Fu + 0.012f, 0.5f }, rug);
        chair(X0 + 1.4f, Z0 + 2.3f, 30, Fu);
        hangingLamp(0.0f, -0.4f, Fu + SH - SLAB - 0.05f);
    }
    hangingLamp(-0.4f, -0.3f, F0 + SH - (h.storeys > 1 ? SLAB : 0.12f) - 0.05f);
}

// ===========================================================================
// Plaza props
// ===========================================================================
void VillageBuilder::fountain(glm::vec3 c) {
    Surf stone(STONE, glm::vec3(1.1f, 1.06f, 1.0f)), water(WATER, glm::vec3(0.35f, 0.55f, 0.6f)), dark(STONE, glm::vec3(0.5f));
    ring(c, 2.45f, 2.85f, c.y - 0.3f, c.y + 0.6f, 16, stone);
    prism(c, 2.46f, c.y - 0.3f, c.y + 0.42f, 16, dark, &water);
    pillar(c, 2.85f, c.y - 0.3f, c.y + 0.6f);
    prism(c, 0.38f, c.y + 0.3f, c.y + 1.45f, 10, stone);
    ring(c + glm::vec3(0, 1.4f, 0), 0.95f, 1.15f, c.y + 1.35f, c.y + 1.62f, 14, stone);
    Surf under(STONE, glm::vec3(0.9f));
    prism(c, 1.15f, c.y + 1.3f, c.y + 1.36f, 14, under, nullptr, &under);
    prism(c, 0.96f, c.y + 1.36f, c.y + 1.55f, 14, dark, &water);
    prism(c, 0.14f, c.y + 1.55f, c.y + 2.25f, 8, stone, &stone);
    prism(c + glm::vec3(0, 2.25f, 0), 0.22f, c.y + 2.25f, c.y + 2.35f, 8, stone, &stone);
}

void VillageBuilder::bench(glm::vec3 p, float yaw) {
    push(p, yaw);
    Surf wood(WOOD, glm::vec3(1.1f, 0.92f, 0.75f)), stone(STONE);
    for (float x : { -0.75f, 0.75f }) box({ x - 0.09f, 0, -0.22f }, { x + 0.09f, 0.42f, 0.22f }, stone);
    for (int i = 0; i < 3; ++i) box({ -1.0f, 0.42f, -0.24f + i * 0.17f }, { 1.0f, 0.47f, -0.11f + i * 0.17f }, wood);
    for (float x : { -0.75f, 0.75f }) box({ x - 0.05f, 0.42f, 0.2f }, { x + 0.05f, 0.95f, 0.26f }, wood);
    for (int i = 0; i < 2; ++i) box({ -1.0f, 0.62f + i * 0.17f, 0.26f }, { 1.0f, 0.74f + i * 0.17f, 0.3f }, wood);
    solid({ -1.0f, 0, -0.26f }, { 1.0f, 0.5f, 0.3f });
    pop();
}

void VillageBuilder::streetLamp(glm::vec3 p) {
    Surf metal(METAL, glm::vec3(0.16f, 0.16f, 0.15f)), stone(STONE);
    box(p + glm::vec3(-0.2f, -0.3f, -0.2f), p + glm::vec3(0.2f, 0.35f, 0.2f), stone);
    prism(p, 0.06f, p.y + 0.35f, p.y + 3.3f, 8, metal);
    prism(p, 0.1f, p.y + 0.35f, p.y + 0.6f, 8, metal, &metal);
    lantern(p + glm::vec3(0, 3.55f, 0), 0.34f, true);
    box(p + glm::vec3(-0.16f, 3.3f, -0.16f), p + glm::vec3(0.16f, 3.36f, 0.16f), metal);
    light(p + glm::vec3(0, 3.45f, 0), glm::vec3(1.0f, 0.76f, 0.46f) * 2.4f, 13.0f, true, 0.03f);
    pillar(p, 0.22f, p.y - 0.3f, p.y + 3.3f);
}

void VillageBuilder::stall(glm::vec3 p, float yaw, glm::vec3 stripeA, glm::vec3 stripeB) {
    push(p, yaw);
    Surf wood(WOOD, glm::vec3(1.0f, 0.85f, 0.7f));
    box({ -1.6f, 0, -0.5f }, { 1.6f, 0.95f, 0.5f }, wood);
    box({ -1.7f, 0.95f, -0.6f }, { 1.7f, 1.0f, 0.6f }, wood);
    solid({ -1.7f, 0, -0.6f }, { 1.7f, 1.0f, 0.6f });
    for (float x : { -1.6f, 1.6f }) for (float z : { -0.55f, 0.55f }) {
        float h = z < 0 ? 2.35f : 2.6f;
        box({ x - 0.05f, 0, z - 0.05f }, { x + 0.05f, h, z + 0.05f }, wood);
    }
    // Striped awning sloping towards the customers
    for (int i = 0; i < 8; ++i) {
        float x0 = -1.85f + i * 0.4625f, x1 = x0 + 0.4625f;
        Surf cloth(FABRIC, (i % 2) ? stripeA : stripeB);
        glm::vec3 a(x0, 2.62f, 0.75f), b(x1, 2.62f, 0.75f), c(x1, 2.3f, -1.05f), d(x0, 2.3f, -1.05f);
        glm::vec3 n = glm::normalize(glm::cross(d - a, b - a));
        if (n.y < 0) n = -n;
        quad(opaque, d, c, b, a, n, { 1, 0, 0 }, cloth);
        quad(opaque, a, b, c, d, -n, { -1, 0, 0 }, Surf(FABRIC, cloth.tint * 0.8f, 0.8f));
        quad(opaque, { x0, 2.3f, -1.05f }, { x1, 2.3f, -1.05f }, { x1, 2.12f, -1.08f }, { x0, 2.12f, -1.08f }, { 0, 0, -1 }, { 1, 0, 0 }, cloth);
    }
    // Produce in crates on the counter
    static const glm::vec3 produce[5] = { { 0.85f, 0.15f, 0.1f }, { 1.0f, 0.6f, 0.1f }, { 0.4f, 0.7f, 0.15f }, { 0.95f, 0.85f, 0.2f }, { 0.5f, 0.2f, 0.45f } };
    for (int i = 0; i < 4; ++i) {
        float x = -1.2f + i * 0.8f;
        box({ x - 0.33f, 1.0f, -0.32f }, { x + 0.33f, 1.18f, 0.32f }, Surf(WOOD, glm::vec3(0.85f, 0.7f, 0.55f)));
        glm::vec3 col = produce[(i + (int)std::fabs(p.x * 3.0f)) % 5];
        for (int j = 0; j < 9; ++j) {
            glm::vec3 c(x - 0.2f + (j % 3) * 0.2f, 1.22f, -0.2f + (j / 3) * 0.2f);
            box(c - glm::vec3(0.07f), c + glm::vec3(0.07f), Surf(FABRIC, col * (0.85f + 0.1f * (j % 3))));
        }
    }
    pop();
}

void VillageBuilder::planter(glm::vec3 p, float yaw, glm::vec3 flowerColor) {
    push(p, yaw);
    Surf stone(STONE, glm::vec3(1.05f));
    box({ -0.7f, -0.2f, -0.35f }, { 0.7f, 0.55f, 0.35f }, stone);
    box({ -0.6f, 0.5f, -0.25f }, { 0.6f, 0.52f, 0.25f }, Surf(WOOD, glm::vec3(0.35f, 0.25f, 0.18f)));
    for (float x = -0.45f; x <= 0.46f; x += 0.3f) flowers({ x, 0.5f, 0 }, 0.6f, 0.55f, flowerColor);
    solid({ -0.7f, -0.2f, -0.35f }, { 0.7f, 0.55f, 0.35f });
    pop();
}

// ===========================================================================
// Planning
// ===========================================================================
Village::~Village() {
    clear();
    if (albedo_) glDeleteTextures(1, &albedo_);
    if (normal_) glDeleteTextures(1, &normal_);
}

bool Village::init(const std::string& dir) {
    const char* names[8] = { "plaster", "stone", "roof", "interior", "floor", "wood", "painted", "fabric" };
    albedo_ = Textures::loadMaterialArray(dir, names, 8, "_albedo", false, 4.0f);
    normal_ = Textures::loadMaterialArray(dir, names, 8, "_normal", true, 2.0f);
    if (!albedo_) std::cerr << "Village: textures missing in " << dir << " (using plain colours)\n";
    return albedo_ != 0;
}

void Village::clear() {
    for (int i = 0; i < 3; ++i) {
        if (vbo_[i]) glDeleteBuffers(1, &vbo_[i]);
        if (ebo_[i]) glDeleteBuffers(1, &ebo_[i]);
        if (vao_[i]) glDeleteVertexArrays(1, &vao_[i]);
        vbo_[i] = ebo_[i] = vao_[i] = 0; counts_[i] = 0;
    }
    if (posVbo_) glDeleteBuffers(1, &posVbo_);
    if (posVao_) glDeleteVertexArrays(1, &posVao_);
    posVbo_ = posVao_ = 0;
    if (maskTex_) glDeleteTextures(1, &maskTex_);
    if (shadowFbo_) glDeleteFramebuffers(1, &shadowFbo_);
    if (shadowTex_) glDeleteTextures(1, &shadowTex_);
    maskTex_ = shadowFbo_ = shadowTex_ = 0;
    shadowBuilt_ = false;
    houses_.clear(); streets_.clear(); streetY_.clear(); mapHouses_.clear(); planted_.clear();
    colliders_.clear(); walkables_.clear(); lights_.clear(); ranges_.clear();
    colGrid_.clear(); walkGrid_.clear();
}

bool Village::chooseSite(const Terrain& t) {
    const float water = t.getWaterY();
    const float half = t.getHalfExtent() - 110.0f;
    float best = 1e30f;
    for (float z = -half; z <= half; z += 14.0f)
        for (float x = -half; x <= half; x += 14.0f) {
            float h0 = t.getHeightAt(x, z);
            if (h0 < water + 3.0f) continue;
            float lo = h0, hi = h0, rough = 0.0f;
            bool dry = true;
            for (int a = 0; a < 16 && dry; ++a) {
                float ang = a * 0.3927f;
                glm::vec2 d(std::cos(ang), std::sin(ang));
                float prev = h0;
                for (float r = 12.0f; r <= 54.0f; r += 14.0f) {
                    float h = t.getHeightAt(x + d.x * r, z + d.y * r);
                    if (h < water + 1.5f) { dry = false; break; }
                    lo = std::min(lo, h); hi = std::max(hi, h);
                    rough += std::fabs(h - prev);
                    prev = h;
                }
            }
            if (!dry) continue;
            // A view of the sea: water within ~70-170 m in some direction
            float coast = 999.0f;
            for (int a = 0; a < 24; ++a) {
                float ang = a * 0.2618f;
                for (float r = 70.0f; r <= 170.0f; r += 25.0f)
                    if (t.getHeightAt(x + std::cos(ang) * r, z + std::sin(ang) * r) < water) { coast = std::min(coast, r); break; }
            }
            float alt = h0 - water;
            float score = (hi - lo) * 1.0f + rough * 0.08f + (coast > 170.0f ? 18.0f : coast * 0.04f)
                        + std::max(0.0f, alt - 28.0f) * 0.8f + std::max(0.0f, 5.0f - alt) * 2.0f;
            if (score < best) { best = score; center_ = glm::vec3(x, h0, z); }
        }
    return best < 1e29f;
}

void Village::planStreets(const Terrain& t) {
    std::mt19937 rng(seed_ * 2654435761u + 7u);
    auto rnd = [&]() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); };
    const float water = t.getWaterY();
    // The main street heads towards the sea; the others branch off around the plaza
    float coastAng = 0.0f, coastBest = 1e9f;
    for (int a = 0; a < 36; ++a) {
        float ang = a * 0.1745f;
        for (float r = 40.0f; r <= 220.0f; r += 10.0f)
            if (t.getHeightAt(center_.x + std::cos(ang) * r, center_.z + std::sin(ang) * r) < water) {
                if (r < coastBest) { coastBest = r; coastAng = ang; }
                break;
            }
    }
    int count = 3 + (rnd() < 0.55f ? 1 : 0);
    std::vector<float> dirs = { coastAng };
    for (int i = 1; i < count; ++i) dirs.push_back(coastAng + i * 6.2831853f / count + (rnd() - 0.5f) * 0.5f);
    for (float a0 : dirs) {
        std::vector<glm::vec2> pts;
        glm::vec2 p = glm::vec2(center_.x, center_.z) + glm::vec2(std::cos(a0), std::sin(a0)) * (plazaR_ - 1.0f);
        float heading = a0, turn = (rnd() - 0.5f) * 0.05f;
        pts.push_back(glm::vec2(center_.x, center_.z) + glm::vec2(std::cos(a0), std::sin(a0)) * 2.0f);
        pts.push_back(p);
        float len = 46.0f + rnd() * 14.0f;
        for (float s = 0.0f; s < len; s += 4.0f) {
            // Follow the contour a little: prefer the heading with the gentlest climb
            float hNow = t.getHeightAt(p.x, p.y), bestCost = 1e9f, bestH = heading;
            for (float dh : { -0.18f, 0.0f, 0.18f }) {
                float hh = heading + turn + dh;
                glm::vec2 q = p + glm::vec2(std::cos(hh), std::sin(hh)) * 4.0f;
                float cost = std::fabs(t.getHeightAt(q.x, q.y) - hNow) + std::fabs(dh) * 1.2f;
                if (cost < bestCost) { bestCost = cost; bestH = hh; }
            }
            // stay roughly radial so streets don't curl back into the plaza
            heading = glm::mix(bestH, a0, 0.25f);
            glm::vec2 q = p + glm::vec2(std::cos(heading), std::sin(heading)) * 4.0f;
            float hq = t.getHeightAt(q.x, q.y);
            if (hq < water + 2.0f || std::fabs(hq - hNow) > 1.8f) break;
            p = q;
            pts.push_back(p);
        }
        if (pts.size() >= 6) streets_.push_back(pts);
    }
    // Street surface heights: the terrain profile, smoothed, flattening into the plaza
    for (auto& pl : streets_) {
        std::vector<float> raw, sm(pl.size());
        for (auto& q : pl) raw.push_back(t.getHeightAt(q.x, q.y));
        for (size_t i = 0; i < pl.size(); ++i) {
            float sum = 0, n = 0;
            for (int k = -2; k <= 2; ++k) {
                int j = glm::clamp((int)i + k, 0, (int)pl.size() - 1);
                sum += raw[j]; n += 1;
            }
            sm[i] = sum / n;
            float plaza = smooth01(plazaR_ + 6.0f, plazaR_ - 1.0f, glm::length(pl[i] - glm::vec2(center_.x, center_.z)));
            sm[i] = glm::mix(sm[i], center_.y, plaza);
        }
        streetY_.push_back(sm);
    }
}

void Village::planHouses(const Terrain& t) {
    std::mt19937 rng(seed_ * 40503u + 11u);
    auto rnd = [&]() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); };
    const float water = t.getWaterY();
    static const glm::vec3 walls[9] = { { 0.98f, 0.9f, 0.72f }, { 0.95f, 0.72f, 0.52f }, { 0.98f, 0.96f, 0.9f }, { 0.75f, 0.85f, 0.92f },
                                        { 0.82f, 0.9f, 0.74f }, { 0.97f, 0.8f, 0.78f }, { 0.93f, 0.82f, 0.6f }, { 1.0f, 0.93f, 0.82f },
                                        { 0.86f, 0.8f, 0.9f } };
    static const glm::vec3 accents[7] = { { 0.25f, 0.45f, 0.75f }, { 0.25f, 0.55f, 0.35f }, { 0.7f, 0.2f, 0.18f }, { 0.2f, 0.55f, 0.6f },
                                          { 0.55f, 0.35f, 0.2f }, { 0.85f, 0.75f, 0.3f }, { 0.35f, 0.3f, 0.55f } };
    static const glm::vec3 roofs[4] = { { 1.0f, 1.0f, 1.0f }, { 0.82f, 0.68f, 0.6f }, { 1.0f, 0.82f, 0.7f }, { 0.7f, 0.62f, 0.6f } };
    static const glm::vec3 fabrics[6] = { { 0.75f, 0.25f, 0.2f }, { 0.25f, 0.4f, 0.7f }, { 0.35f, 0.55f, 0.3f }, { 0.8f, 0.65f, 0.3f },
                                          { 0.6f, 0.3f, 0.55f }, { 0.85f, 0.85f, 0.8f } };
    glm::vec2 C(center_.x, center_.z);

    auto tryPlace = [&](glm::vec2 frontPoint, glm::vec2 towardStreet, float streetY, bool ring) -> bool {
        House h;
        h.storeys = rnd() < 0.42f ? 2 : 1;
        h.w = h.storeys == 2 ? 7.8f + rnd() * 1.8f : 6.6f + rnd() * 2.6f;
        h.d = 6.2f + rnd() * 1.6f;
        glm::vec2 f = glm::normalize(towardStreet);
        float jitter = (rnd() - 0.5f) * 0.12f;
        f = glm::vec2(f.x * std::cos(jitter) - f.y * std::sin(jitter), f.x * std::sin(jitter) + f.y * std::cos(jitter));
        h.ez = -f;
        h.ex = glm::vec2(h.ez.y, -h.ez.x);
        h.c = frontPoint - f * (h.d * 0.5f + (ring ? 1.0f : 1.4f));
        glm::vec2 half(h.w * 0.5f + 0.6f, h.d * 0.5f + 0.6f);
        // Not on other houses, streets or the plaza
        for (const House& o : houses_)
            if (obbOverlap(h.c, h.ex, half + glm::vec2(1.0f), o.c, o.ex, glm::vec2(o.w * 0.5f + 0.6f, o.d * 0.5f + 0.6f))) return false;
        glm::vec2 corners[8];
        for (int i = 0; i < 4; ++i) {
            float sx = (i & 1) ? 1.0f : -1.0f, sz = (i & 2) ? 1.0f : -1.0f;
            corners[i] = h.c + h.ex * (sx * h.w * 0.5f) + h.ez * (sz * h.d * 0.5f);
            corners[4 + i] = h.c + h.ex * (sx * h.w * 0.25f) + h.ez * (sz * h.d * 0.5f);
        }
        float lo = 1e9f, hi = -1e9f, sum = 0;
        for (glm::vec2 q : corners) {
            if (glm::length(q - C) < plazaR_ + 1.2f) return false;
            for (auto& pl : streets_) if (polyDist(pl, q) < streetHalf_ + 0.5f) return false;
            float hq = t.getHeightAt(q.x, q.y);
            if (hq < water + 1.5f) return false;
            lo = std::min(lo, hq); hi = std::max(hi, hq); sum += hq;
        }
        if (hi - lo > 5.0f || glm::length(h.c - C) > radius_ - 4.0f) return false;
        // Terrace height between the ground under the house and the street in front of it
        h.padY = glm::clamp(glm::mix(sum / 8.0f, streetY, 0.55f), streetY - 0.9f, streetY + 1.2f);
        h.doorX = (rnd() - 0.5f) * (h.w - 3.2f);
        h.seed = (unsigned)(rnd() * 1e6f);
        h.wall = walls[h.seed % 9];
        h.accent = accents[(h.seed / 9) % 7];
        h.roof = roofs[(h.seed / 63) % 4];
        h.trim = rnd() < 0.5f ? glm::vec3(1.35f, 1.3f, 1.2f) : glm::vec3(0.75f, 0.6f, 0.45f);
        h.interior = glm::mix(glm::vec3(1.0f, 0.97f, 0.9f), h.wall, 0.35f);
        h.fabric = fabrics[(h.seed / 7) % 6];
        h.fireplace = rnd() < 0.8f;
        h.balcony = h.storeys == 2 && rnd() < 0.6f;
        h.canopy = rnd() < 0.5f;
        houses_.push_back(h);
        return true;
    };

    // Houses along both sides of every street, facing it
    for (size_t si = 0; si < streets_.size(); ++si) {
        auto& pl = streets_[si];
        float acc = 0.0f, next = plazaR_ + 4.0f;
        for (size_t i = 1; i < pl.size(); ++i) {
            glm::vec2 a = pl[i - 1], b = pl[i];
            float segLen = glm::length(b - a);
            while (acc + segLen >= next) {
                float u = (next - acc) / segLen;
                glm::vec2 p = a + (b - a) * u;
                glm::vec2 dir = glm::normalize(b - a), side(-dir.y, dir.x);
                float y = glm::mix(streetY_[si][i - 1], streetY_[si][i], u);
                for (float s : { -1.0f, 1.0f }) {
                    if (houses_.size() >= 16) break;
                    if (rnd() < 0.15f) continue;
                    tryPlace(p + side * s * (streetHalf_ + 0.2f), -side * s, y, false);
                }
                next += 9.5f + rnd() * 3.0f;
            }
            acc += segLen;
        }
    }
    // A few houses around the plaza, between the streets, facing the fountain
    for (int i = 0; i < 12 && houses_.size() < 18; ++i) {
        float ang = i * 0.5236f + 0.26f;
        glm::vec2 d(std::cos(ang), std::sin(ang));
        bool nearStreet = false;
        for (auto& pl : streets_) {
            glm::vec2 sd = glm::normalize(pl[2] - C);
            if (glm::dot(sd, d) > 0.88f) nearStreet = true;
        }
        if (nearStreet) continue;
        tryPlace(C + d * (plazaR_ + 0.6f), -d, center_.y, true);
    }
}

void Village::shapeTerrain(Terrain& t) {
    glm::vec2 C(center_.x, center_.z);
    float R = radius_ + 20.0f;
    t.editHeights(C - glm::vec2(R), C + glm::vec2(R), [&](float x, float z, float h) {
        glm::vec2 p(x, z);
        // Streets follow the smoothed terrain profile
        for (size_t si = 0; si < streets_.size(); ++si) {
            int seg = 0; float frac = 0;
            float d = polyDist(streets_[si], p, &seg, &frac);
            float w = 1.0f - smooth01(streetHalf_ + 0.5f, streetHalf_ + 4.0f, d);
            if (w <= 0) continue;
            float target = glm::mix(streetY_[si][seg], streetY_[si][seg + 1], frac);
            h = glm::mix(h, target, w * 0.9f);
        }
        // The plaza is level
        float wp = 1.0f - smooth01(plazaR_ + 1.0f, plazaR_ + 7.0f, glm::length(p - C));
        h = glm::mix(h, center_.y, wp);
        // Each house sits on its own level terrace that blends into the slope
        for (const House& hs : houses_) {
            glm::vec2 q = p - hs.c;
            glm::vec2 l(glm::dot(q, hs.ex), glm::dot(q, hs.ez));
            glm::vec2 e = glm::abs(l) - glm::vec2(hs.w * 0.5f + 1.3f, hs.d * 0.5f + 1.3f);
            float sd = glm::length(glm::max(e, glm::vec2(0.0f))) + std::min(std::max(e.x, e.y), 0.0f);
            float w = 1.0f - smooth01(0.0f, 4.5f, sd);
            if (w > 0) h = glm::mix(h, hs.padY, w);
        }
        return h;
    });
}

void Village::buildMask() {
    const int S = 512;
    float H = radius_ + 14.0f;
    glm::vec2 mn(center_.x - H, center_.z - H);
    maskRect_ = glm::vec4(mn.x, mn.y, 1.0f / (2 * H), 1.0f / (2 * H));
    std::vector<unsigned char> px((size_t)S * S * 2, 0);
    glm::vec2 C(center_.x, center_.z);
    for (int y = 0; y < S; ++y) for (int x = 0; x < S; ++x) {
        glm::vec2 p = mn + (glm::vec2(x, y) + 0.5f) * (2 * H / S);
        float pave = 1.0f - smooth01(plazaR_ - 0.8f, plazaR_ + 0.4f, glm::length(p - C));
        for (auto& pl : streets_) pave = std::max(pave, 1.0f - smooth01(streetHalf_ - 0.6f, streetHalf_ + 0.3f, polyDist(pl, p)));
        float cover = pave;
        for (const House& h : houses_) {
            glm::vec2 q = p - h.c;
            glm::vec2 l(glm::dot(q, h.ex), glm::dot(q, h.ez));
            // footprint (no grass) and the little path from the door to the street
            if (std::fabs(l.x) < h.w * 0.5f + 0.25f && std::fabs(l.y) < h.d * 0.5f + 0.25f) cover = 1.0f;
            if (std::fabs(l.x - h.doorX) < 0.8f && l.y < -h.d * 0.5f && l.y > -h.d * 0.5f - 3.5f) { pave = std::max(pave, 1.0f); cover = 1.0f; }
        }
        px[((size_t)y * S + x) * 2] = (unsigned char)(glm::clamp(pave, 0.0f, 1.0f) * 255);
        px[((size_t)y * S + x) * 2 + 1] = (unsigned char)(glm::clamp(cover, 0.0f, 1.0f) * 255);
    }
    if (!maskTex_) glGenTextures(1, &maskTex_);
    glBindTexture(GL_TEXTURE_2D, maskTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, S, S, 0, GL_RG, GL_UNSIGNED_BYTE, px.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Village::generate(Terrain& terrain, int seed) {
    clear();
    if (terrain.isFlat() || terrain.getHalfExtent() < 150.0f) return;
    seed_ = (unsigned)seed;
    radius_ = 60.0f;
    if (!chooseSite(terrain)) { std::cerr << "Village: no suitable site on this island\n"; return; }
    planStreets(terrain);
    planHouses(terrain);
    if (houses_.empty()) { clear(); std::cerr << "Village: could not place any houses\n"; return; }
    // The real extent: streets and house corners
    float ext = plazaR_ + 4.0f;
    glm::vec2 C(center_.x, center_.z);
    for (auto& pl : streets_) for (glm::vec2 q : pl) ext = std::max(ext, glm::length(q - C) + streetHalf_);
    for (const House& h : houses_) ext = std::max(ext, glm::length(h.c - C) + glm::length(glm::vec2(h.w, h.d)) * 0.5f + 2.0f);
    radius_ = ext + 3.0f;
    shapeTerrain(terrain);
    buildMask();
    buildAll(terrain);
    buildGrids();
    upload();
    for (const House& h : houses_) {
        static const glm::vec3 roofBase(0.72f, 0.36f, 0.22f);
        mapHouses_.push_back({ h.c, glm::vec2(h.w, h.d) * 0.5f, h.ex, roofBase * h.roof });
    }
    // Spawn at the far end of the main street, looking towards the plaza
    if (!streets_.empty()) {
        const auto& main = streets_[0];
        glm::vec2 end = main[main.size() - 2];
        spawn_ = glm::vec3(end.x, terrain.getHeightAt(end.x, end.y) + 1.7f, end.y);
        glm::vec2 look = glm::vec2(center_.x, center_.z) - end;
        spawnYaw_ = glm::degrees(std::atan2(look.y, look.x));
    }
    std::cout << "Village: " << houses_.size() << " houses, " << streets_.size() << " streets, " << lights_.size()
              << " lights at (" << center_.x << ", " << center_.z << ")\n";
}

// ===========================================================================
// Geometry
// ===========================================================================
void Village::buildAll(const Terrain& t) {
    VillageBuilder b(*this, seed_ * 977u + 3u);
    auto range = [&](size_t first, glm::vec3 c, float r, int house = -1) {
        ranges_.push_back({ (GLint)first, (GLsizei)(b.opaque.size() - first), c, r, house });
    };
    // Houses
    for (const House& h : houses_) {
        size_t first = b.opaque.size();
        b.currentHouse = (int)(&h - &houses_[0]);
        b.house(h);
        b.currentHouse = -1;
        range(first, glm::vec3(h.c.x, h.padY + 4.0f, h.c.y), std::sqrt(h.w * h.w + h.d * h.d) * 0.5f + 5.0f, (int)(&h - &houses_[0]));
        // the door: a doorway test point and a wall test point (for the smoke test)
        if (&h == &houses_.front()) {
            glm::vec2 door = h.c + h.ex * h.doorX + h.ez * (-h.d * 0.5f + WT * 0.5f);
            testDoor_ = glm::vec3(door.x, h.padY + F0 + 1.7f, door.y);
            // between the door frame and the nearest window
            glm::vec2 wall = h.c + h.ex * (h.doorX + (h.doorX > 0 ? -0.85f : 0.85f)) + h.ez * (-h.d * 0.5f + WT * 0.5f);
            testWall_ = glm::vec3(wall.x, h.padY + F0 + 1.7f, wall.y);
        }
    }
    // Plaza
    b.xf = VillageBuilder::Xf();
    b.stack.clear();
    size_t first = b.opaque.size();
    glm::vec3 C = center_;
    b.fountain(C);
    std::mt19937 rng(seed_ + 99u);
    auto rnd = [&]() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); };
    std::vector<float> streetAngles;
    for (auto& pl : streets_) { glm::vec2 d = pl[2] - glm::vec2(C.x, C.z); streetAngles.push_back(std::atan2(d.y, d.x)); }
    auto freeAngle = [&](float a, float margin) {
        for (float s : streetAngles) {
            float d = std::fabs(std::remainder(a - s, 6.2831853f));
            if (d < margin) return false;
        }
        return true;
    };
    for (int i = 0; i < 4; ++i) {
        float a = i * 1.5708f + 0.785f;
        if (!freeAngle(a, 0.3f)) continue;
        glm::vec3 p(C.x + std::cos(a) * 5.2f, C.y, C.z + std::sin(a) * 5.2f);
        b.bench(p, glm::degrees(-a) - 90.0f);
    }
    int lamps = 0;
    for (int i = 0; i < 8 && lamps < 5; ++i) {
        float a = i * 0.785f + 0.2f;
        if (!freeAngle(a, 0.22f)) continue;
        b.streetLamp(glm::vec3(C.x + std::cos(a) * (plazaR_ - 1.2f), C.y, C.z + std::sin(a) * (plazaR_ - 1.2f)));
        ++lamps;
    }
    // Market stalls, barrels, crates and flower planters in the gaps between the streets
    static const glm::vec3 stripes[3][2] = { { { 0.85f, 0.2f, 0.15f }, { 1.2f, 1.15f, 1.0f } },
                                             { { 0.2f, 0.4f, 0.75f }, { 1.2f, 1.15f, 1.0f } },
                                             { { 0.25f, 0.55f, 0.3f }, { 1.15f, 1.0f, 0.6f } } };
    int stalls = 0, planters = 0;
    for (int i = 0; i < 12; ++i) {
        float a = i * 0.5236f + 0.1f;
        if (!freeAngle(a, 0.45f)) continue;
        glm::vec3 p(C.x + std::cos(a) * (plazaR_ - 3.6f), C.y, C.z + std::sin(a) * (plazaR_ - 3.6f));
        float facing = glm::degrees(-a) + 90.0f;
        if (stalls < 3 && i % 3 == 0) {
            b.stall(p, facing, stripes[stalls % 3][0], stripes[stalls % 3][1]);
            glm::vec3 side(std::cos(a + 0.35f), 0, std::sin(a + 0.35f));
            b.barrel(p + side * 2.2f);
            b.crate(p - side * 2.3f + glm::vec3(0.2f, 0, 0.1f), 0.6f, rnd() * 40.0f);
            b.crate(p - side * 2.3f + glm::vec3(-0.3f, 0, 0.7f), 0.5f, rnd() * 40.0f);
            ++stalls;
        } else if (planters < 4 && i % 3 == 1) {
            static const glm::vec3 bl[4] = { { 0.95f, 0.25f, 0.25f }, { 1.0f, 0.85f, 0.25f }, { 0.9f, 0.5f, 0.9f }, { 1.0f, 1.0f, 1.0f } };
            b.planter(p, facing, bl[planters % 4]);
            ++planters;
        }
    }
    range(first, C + glm::vec3(0, 2, 0), plazaR_ + 4.0f);

    // Street lamps along the streets, alternating sides
    for (size_t si = 0; si < streets_.size(); ++si) {
        size_t f2 = b.opaque.size();
        auto& pl = streets_[si];
        int k = 0;
        for (size_t i = 4; i + 1 < pl.size(); i += 3, ++k) {
            glm::vec2 dir = glm::normalize(pl[i + 1] - pl[i]), side(-dir.y, dir.x);
            glm::vec2 p = pl[i] + side * ((k % 2) ? 1.0f : -1.0f) * (streetHalf_ + 0.55f);
            bool clear = true;
            for (const House& h : houses_) {
                glm::vec2 q = p - h.c;
                if (std::fabs(glm::dot(q, h.ex)) < h.w * 0.5f + 1.2f && std::fabs(glm::dot(q, h.ez)) < h.d * 0.5f + 1.2f) clear = false;
            }
            if (!clear) continue;
            b.streetLamp(glm::vec3(p.x, t.getHeightAt(p.x, p.y), p.y));
        }
        if (b.opaque.size() > f2) {
            glm::vec2 mid = pl[pl.size() / 2];
            range(f2, glm::vec3(mid.x, t.getHeightAt(mid.x, mid.y) + 2.0f, mid.y), 40.0f);
        }
    }

    // Trees: a few around the plaza and in the back gardens
    for (int i = 0; i < 6; ++i) {
        float a = i * 1.047f + 0.5f;
        if (!freeAngle(a, 0.45f)) continue;
        glm::vec2 p = glm::vec2(C.x, C.z) + glm::vec2(std::cos(a), std::sin(a)) * (plazaR_ + 1.0f);
        bool clear = true;
        for (const House& h : houses_) if (glm::length(p - h.c) < std::max(h.w, h.d) * 0.5f + 3.0f) clear = false;
        if (clear) planted_.push_back(glm::vec4(p.x, t.getHeightAt(p.x, p.y), p.y, 1));
    }
    for (const House& h : houses_) {
        if (rnd() > 0.55f) continue;
        glm::vec2 p = h.c + h.ez * (h.d * 0.5f + 4.0f) + h.ex * ((rnd() - 0.5f) * h.w);
        bool clear = glm::length(p - glm::vec2(C.x, C.z)) > plazaR_ + 3.0f;
        for (const House& o : houses_) {
            glm::vec2 q = p - o.c;
            if (std::fabs(glm::dot(q, o.ex)) < o.w * 0.5f + 2.5f && std::fabs(glm::dot(q, o.ez)) < o.d * 0.5f + 2.5f) clear = false;
        }
        for (auto& pl : streets_) if (polyDist(pl, p) < streetHalf_ + 2.5f) clear = false;
        if (clear) planted_.push_back(glm::vec4(p.x, t.getHeightAt(p.x, p.y), p.y, rnd() < 0.7f ? 1 : 2));
    }

    // Upload: packed 36-byte vertices + indices; positions alone for depth / shadow passes
    struct Packed { float px, py, pz; GLuint n, t; float u, v; GLubyte tint[4], m[4]; };
    auto pack10 = [](glm::vec3 v) {
        auto q = [](float f) { int i = (int)std::lround(glm::clamp(f, -1.0f, 1.0f) * 511.0f); return (GLuint)(i & 1023); };
        return q(v.x) | (q(v.y) << 10) | (q(v.z) << 20);
    };
    auto u8 = [](float f) { return (GLubyte)glm::clamp((int)std::lround(f * 255.0f), 0, 255); };
    MeshOut* meshes[3] = { &b.opaque, &b.cards, &b.glass };
    for (int k = 0; k < 3; ++k) {
        const MeshOut& m = *meshes[k];
        std::vector<Packed> pv(m.v.size());
        for (size_t q = 0; q < m.v.size(); ++q) {
            const Vtx& x = m.v[q];
            Packed& p = pv[q];
            p.px = x.p.x; p.py = x.p.y; p.pz = x.p.z;
            p.n = pack10(glm::normalize(x.n)); p.t = pack10(glm::normalize(x.t));
            p.u = x.uv.x; p.v = x.uv.y;
            p.tint[0] = u8(x.tint.r * 0.5f); p.tint[1] = u8(x.tint.g * 0.5f); p.tint[2] = u8(x.tint.b * 0.5f); p.tint[3] = 255;
            p.m[0] = (GLubyte)x.mat; p.m[1] = u8(x.ao); p.m[2] = u8(x.emis * 0.25f); p.m[3] = 0;
        }
        if (!vao_[k]) { glGenVertexArrays(1, &vao_[k]); glGenBuffers(1, &vbo_[k]); glGenBuffers(1, &ebo_[k]); }
        glBindVertexArray(vao_[k]);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_[k]);
        glBufferData(GL_ARRAY_BUFFER, pv.size() * sizeof(Packed), pv.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_[k]);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, m.i.size() * sizeof(GLuint), m.i.data(), GL_STATIC_DRAW);
        const GLsizei st = sizeof(Packed);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, st, (void*)offsetof(Packed, px)); glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 4, GL_INT_2_10_10_10_REV, GL_TRUE, st, (void*)offsetof(Packed, n)); glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 4, GL_INT_2_10_10_10_REV, GL_TRUE, st, (void*)offsetof(Packed, t)); glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, st, (void*)offsetof(Packed, u)); glEnableVertexAttribArray(3);
        glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, st, (void*)offsetof(Packed, tint)); glEnableVertexAttribArray(4);
        glVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_FALSE, st, (void*)offsetof(Packed, m)); glEnableVertexAttribArray(5);
        counts_[k] = (GLsizei)m.i.size();
        if (k == 0) {
            std::vector<glm::vec3> pos(m.v.size());
            for (size_t q = 0; q < m.v.size(); ++q) pos[q] = m.v[q].p;
            if (!posVao_) { glGenVertexArrays(1, &posVao_); glGenBuffers(1, &posVbo_); }
            glBindVertexArray(posVao_);
            glBindBuffer(GL_ARRAY_BUFFER, posVbo_);
            glBufferData(GL_ARRAY_BUFFER, pos.size() * sizeof(glm::vec3), pos.data(), GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_[0]);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0); glEnableVertexAttribArray(0);
        }
    }
    glBindVertexArray(0);
}

void Village::upload() {}   // geometry is uploaded at the end of buildAll()

void Village::buildGrids() {
    float H = radius_ + 20.0f;
    gridMinX_ = center_.x - H; gridMinZ_ = center_.z - H;
    gridW_ = gridH_ = (int)std::ceil(2 * H / gridCell_);
    colGrid_.assign((size_t)gridW_ * gridH_, {});
    walkGrid_.assign((size_t)gridW_ * gridH_, {});
    auto addTo = [&](std::vector<std::vector<int>>& grid, glm::vec2 c, float r, int idx) {
        int x0 = std::max(0, (int)((c.x - r - gridMinX_) / gridCell_)), x1 = std::min(gridW_ - 1, (int)((c.x + r - gridMinX_) / gridCell_));
        int z0 = std::max(0, (int)((c.y - r - gridMinZ_) / gridCell_)), z1 = std::min(gridH_ - 1, (int)((c.y + r - gridMinZ_) / gridCell_));
        for (int z = z0; z <= z1; ++z) for (int x = x0; x <= x1; ++x) grid[(size_t)z * gridW_ + x].push_back(idx);
    };
    for (int i = 0; i < (int)colliders_.size(); ++i) addTo(colGrid_, colliders_[i].c, glm::length(colliders_[i].half) + 0.5f, i);
    for (int i = 0; i < (int)walkables_.size(); ++i) addTo(walkGrid_, walkables_[i].c, glm::length(walkables_[i].half) + 0.1f, i);
}

// ===========================================================================
// Rendering
// ===========================================================================
static void extractPlanes(const glm::mat4& VP, glm::vec4 planes[5]) {
    glm::vec4 rows[4];
    for (int i = 0; i < 4; ++i) rows[i] = glm::vec4(VP[0][i], VP[1][i], VP[2][i], VP[3][i]);
    planes[0] = rows[3] + rows[0]; planes[1] = rows[3] - rows[0];
    planes[2] = rows[3] + rows[1]; planes[3] = rows[3] - rows[1];
    planes[4] = rows[3] + rows[2];
    for (int i = 0; i < 5; ++i) planes[i] /= glm::length(glm::vec3(planes[i]));
}

void Village::draw(GLuint p, GLuint depthProgram, const glm::mat4& view, const glm::mat4& proj, GLuint foliageTex,
                   float lightScale, float time) const {
    drawnHouses_ = 0;
    if (!active() || !p) return;
    glm::mat4 vp = proj * view;
    auto U = [&](const char* n) { return glGetUniformLocation(p, n); };
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D_ARRAY, albedo_);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D_ARRAY, normal_);
    glActiveTexture(GL_TEXTURE7); glBindTexture(GL_TEXTURE_2D_ARRAY, foliageTex);
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_CULL_FACE);
    // Visible houses / prop groups, nearest first so walls hide what is behind them early
    glm::vec3 cam = glm::vec3(glm::inverse(view)[3]);
    glm::vec4 planes[5];
    extractPlanes(vp, planes);
    std::vector<std::pair<float, int>> order;
    for (int i = 0; i < (int)ranges_.size(); ++i) {
        const Range& r = ranges_[i];
        bool inside = true;
        for (auto& pl : planes) if (glm::dot(glm::vec3(pl), r.center) + pl.w < -r.radius) { inside = false; break; }
        if (inside) order.push_back({ glm::length(r.center - cam), i });
    }
    std::sort(order.begin(), order.end());
    auto drawRange = [](const Range& r) { glDrawElements(GL_TRIANGLES, r.count, GL_UNSIGNED_INT, (void*)((size_t)r.first * sizeof(GLuint))); };
    // 1. Depth only: interiors, roofs and walls overlap a lot; afterwards each pixel is shaded once
    if (depthProgram) {
        glBindVertexArray(posVao_);
        glUseProgram(depthProgram);
        glUniformMatrix4fv(glGetUniformLocation(depthProgram, "lightViewProj"), 1, GL_FALSE, &vp[0][0]);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        for (const auto& o : order) drawRange(ranges_[o.second]);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
    }
    // 2. Shading
    glBindVertexArray(vao_[0]);
    glUseProgram(p);
    glUniformMatrix4fv(U("viewProj"), 1, GL_FALSE, &vp[0][0]);
    glUniform1i(U("albedoMaps"), 0);
    glUniform1i(U("normalMaps"), 1);
    glUniform1i(U("foliageTex"), 7);
    glUniform1i(U("hasAlbedo"), albedo_ != 0);
    glUniform1i(U("hasNormal"), normal_ != 0);
    glUniform1i(U("glassPass"), 0);
    GLint locN = U("numHouseLights"), locP = U("houseLightPos"), locC = U("houseLightColor");
    for (const auto& o : order) {
        const Range& r = ranges_[o.second];
        // The house's own interior lights (lamps, fireplace)
        glm::vec4 pos[4], col[4];
        int n = 0;
        if (r.house >= 0 && lightScale > 0.0f)
            for (int li = 0; li < (int)lights_.size() && n < std::min(4, maxHouseLights_); ++li) {
                const VillageLight& L = lights_[li];
                if (L.house != r.house) continue;
                float f = 1.0f;
                if (L.flicker > 0.0f)
                    f += L.flicker * (0.5f * std::sin(time * 13.0f + li * 1.7f) + 0.3f * std::sin(time * 7.3f + li * 2.9f) +
                                      0.2f * std::sin(time * 23.0f + li));
                pos[n] = glm::vec4(L.pos, L.radius);
                col[n] = glm::vec4(L.color * lightScale * f, 0.0f);
                ++n;
            }
        glUniform1i(locN, n);
        if (n) { glUniform4fv(locP, n, &pos[0].x); glUniform4fv(locC, n, &col[0].x); }
        drawRange(r);
        ++drawnHouses_;
    }
    glUniform1i(locN, 0);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glBindVertexArray(vao_[1]);
    glDrawElements(GL_TRIANGLES, counts_[1], GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void Village::drawGlass(GLuint p, const glm::mat4& view, const glm::mat4& proj) const {
    if (!active() || !p || !counts_[2]) return;
    glUseProgram(p);
    glm::mat4 vp = proj * view;
    glUniformMatrix4fv(glGetUniformLocation(p, "viewProj"), 1, GL_FALSE, &vp[0][0]);
    glUniform1i(glGetUniformLocation(p, "glassPass"), 1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(vao_[2]);
    glDrawElements(GL_TRIANGLES, counts_[2], GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glUniform1i(glGetUniformLocation(p, "glassPass"), 0);
}

void Village::bindMask(GLuint program, int unit) const {
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "villageMask"), unit);
    glUniform1i(glGetUniformLocation(program, "hasVillageMask"), maskTex_ && active() ? 1 : 0);
    glUniform4fv(glGetUniformLocation(program, "villageMaskRect"), 1, &maskRect_.x);
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, maskTex_);
    glActiveTexture(GL_TEXTURE0);
}

void Village::buildShadow(GLuint program, const glm::vec3& sunDir) {
    if (!active() || !program) return;
    glm::vec3 sun = glm::normalize(sunDir);
    if (sun.y <= 0.0f) { shadowBuilt_ = false; return; }
    if (shadowBuilt_ && glm::dot(sun, shadowSun_) > 0.999999f) return;

    GLint prevFbo, viewport[4], prevProgram;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    if (!shadowTex_) {
        glGenTextures(1, &shadowTex_);
        glBindTexture(GL_TEXTURE_2D, shadowTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, shadowRes_, shadowRes_, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        const float border[] = { 1, 1, 1, 1 };
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
        glGenFramebuffers(1, &shadowFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex_, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "Village: could not create the sun shadow framebuffer\n";
            glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
            glDeleteFramebuffers(1, &shadowFbo_); glDeleteTextures(1, &shadowTex_);
            shadowFbo_ = shadowTex_ = 0;
            return;
        }
    }
    // A fixed light volume around the village: no shimmering when the camera moves
    glm::vec3 up = std::fabs(sun.y) > 0.98f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    glm::vec3 target = center_ + glm::vec3(0, 3, 0);
    float H = radius_ + 12.0f;
    shadowMatrix_ = glm::ortho(-H, H, -H, H, 1.0f, 400.0f) * glm::lookAt(target + sun * 200.0f, target, up);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glViewport(0, 0, shadowRes_, shadowRes_);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 2.0f);
    glUseProgram(program);
    glUniformMatrix4fv(glGetUniformLocation(program, "lightViewProj"), 1, GL_FALSE, &shadowMatrix_[0][0]);
    glBindVertexArray(posVao_);
    glDrawElements(GL_TRIANGLES, counts_[0], GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glUseProgram(prevProgram);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    shadowSun_ = sun;
    shadowBuilt_ = true;
}

void Village::bindShadow(GLuint program, int unit, float strength, bool enabled) const {
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "villageShadowTex"), unit);
    glUniform1i(glGetUniformLocation(program, "hasVillageShadow"), enabled && shadowBuilt_ && active() ? 1 : 0);
    glUniform1f(glGetUniformLocation(program, "villageShadowStrength"), strength);
    glUniformMatrix4fv(glGetUniformLocation(program, "villageLightViewProj"), 1, GL_FALSE, &shadowMatrix_[0][0]);
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    glActiveTexture(GL_TEXTURE0);
}

// ===========================================================================
// Gameplay queries
// ===========================================================================
float Village::groundAt(float x, float z, float feetY) const {
    if (!active()) return -1e9f;
    int gx = (int)((x - gridMinX_) / gridCell_), gz = (int)((z - gridMinZ_) / gridCell_);
    if (gx < 0 || gz < 0 || gx >= gridW_ || gz >= gridH_) return -1e9f;
    float best = -1e9f;
    for (int i : walkGrid_[(size_t)gz * gridW_ + gx]) {
        const Walkable& w = walkables_[i];
        glm::vec2 d(x - w.c.x, z - w.c.y);
        float lx = glm::dot(d, w.ax), lz = glm::dot(d, glm::vec2(-w.ax.y, w.ax.x));
        if (std::fabs(lx) > w.half.x || std::fabs(lz) > w.half.y) continue;
        float h = glm::mix(w.h0, w.h1, (lx / std::max(w.half.x, 1e-3f)) * 0.5f + 0.5f);
        if (h <= feetY + 0.55f) best = std::max(best, h);
    }
    return best;
}

void Village::collide(glm::vec3& eye) const {
    if (!active()) return;
    const float r = 0.32f;
    float feet = eye.y - 1.7f, head = eye.y + 0.12f;
    int gx = (int)((eye.x - gridMinX_) / gridCell_), gz = (int)((eye.z - gridMinZ_) / gridCell_);
    if (gx < 0 || gz < 0 || gx >= gridW_ || gz >= gridH_) return;
    for (int pass = 0; pass < 2; ++pass)
        for (int i : colGrid_[(size_t)gz * gridW_ + gx]) {
            const Collider& c = colliders_[i];
            // Low things (a step, a kerb) can be stepped onto; anything taller blocks
            if (feet + 0.45f >= c.y1 || head <= c.y0) continue;
            glm::vec2 d(eye.x - c.c.x, eye.z - c.c.y);
            if (c.radius > 0.0f) {
                float l = glm::length(d), m = c.radius + r;
                if (l < m) { glm::vec2 push = (l > 1e-4f ? d / l : glm::vec2(1, 0)) * m; eye.x = c.c.x + push.x; eye.z = c.c.y + push.y; }
                continue;
            }
            glm::vec2 az(-c.ax.y, c.ax.x);
            float lx = glm::dot(d, c.ax), lz = glm::dot(d, az);
            float px = c.half.x + r - std::fabs(lx), pz = c.half.y + r - std::fabs(lz);
            if (px <= 0 || pz <= 0) continue;
            if (px < pz) lx += (lx < 0 ? -px : px);
            else lz += (lz < 0 ? -pz : pz);
            glm::vec2 w = c.ax * lx + az * lz;
            eye.x = c.c.x + w.x; eye.z = c.c.y + w.y;
        }
}

void Village::setShadowResolution(int res) {
    if (res == shadowRes_) return;
    shadowRes_ = res;
    if (shadowFbo_) glDeleteFramebuffers(1, &shadowFbo_);
    if (shadowTex_) glDeleteTextures(1, &shadowTex_);
    shadowFbo_ = shadowTex_ = 0;
    shadowBuilt_ = false;   // rebuilt at the new size next frame
}
