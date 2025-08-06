/* --------------------------------------------------------------------------
 * polyhedron.c – Geometry and topology utilities for convex polyhedra
 * -------------------------------------------------------------------------- */

#include "polyhedron.h"
#include <math.h>
#include <string.h>
#include <float.h>  // for FLT_MAX


/* ────────────────────────────────────────────────────────────────────────── */
/* VECTOR HELPERS                                                            */
/* ────────────────────────────────────────────────────────────────────────── */

static inline void v_add(float d[3], const float s[3]) {
    for (int i = 0; i < 3; ++i) d[i] += s[i];
}

static inline void v_scale(float d[3], float k) {
    for (int i = 0; i < 3; ++i) d[i] *= k;
}

static inline void v_copy(float d[3], const float s[3]) {
    memcpy(d, s, 3 * sizeof *d);
}

static inline float v_len(const float v[3]) {
    return sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
}

static inline void v_lerp(const float a[3], const float b[3], float t, float d[3]) {
    for (int i = 0; i < 3; ++i)
        d[i] = a[i] + t * (b[i] - a[i]);
}

/* ────────────────────────────────────────────────────────────────────────── */
/* CORE FUNCTIONS                                                             */
/* ────────────────────────────────────────────────────────────────────────── */

static void poly_normalize(Polyhedron *p) {
    // Normalize to unit average vertex length
    float sum = 0.0f;
    for (uint16_t i = 0; i < p->V; ++i) sum += v_len(p->v[i]);
    float inv = 1.0f / (sum / p->V);
    for (uint16_t i = 0; i < p->V; ++i) v_scale(p->v[i], inv);
}

static void poly_radial_normalize(Polyhedron *p)
{
    for (uint16_t i = 0; i < p->V; ++i) {
        float r = v_len(p->v[i]);
        if (r > 0.0f) {
            v_scale(p->v[i], 1.0f / r);
        }
    }
}


/* ──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────── */
/*
// -----------------------------------------------------------------------------
// Sort all edges incident on vertex `vi` into a consistent (CCW) order.
//   in     – must be prepared (e2f filled).
//   vi     – vertex whose star we sort.
//   inc[n] – list of edge-indices touching vi.
//   n      – count of edges in inc.
//   order  – output permutation of [0..n-1] giving CCW walk around vi.
// -----------------------------------------------------------------------------
// indecent? what do you mean im indecent?
static void sort_incident_edges(const Polyhedron *in,
                                uint8_t           vi,
                                const uint8_t    *inc,
                                uint8_t           n,
                                uint8_t          *order)
{
    order[0] = 0;
    for (uint8_t k = 1; k < n; ++k) {
        uint8_t prev   = order[k - 1];
        uint8_t eprev  = inc[prev];
        // pick the face we haven't come from
        uint8_t walkFace = in->e2f[eprev][0];
        if (in->e2f[eprev][1] != 0xFF &&
            memchr(in->f[walkFace], vi, in->fv[walkFace]) == NULL)
        {
            walkFace = in->e2f[eprev][1];
        }
        // find the next edge in that face
        for (uint8_t i = 0; i < n; ++i) {
            if (i == prev) continue;
            uint8_t e = inc[i];
            if (in->e2f[e][0] == walkFace || in->e2f[e][1] == walkFace) {
                order[k] = i;
                break;
            }
        }
    }
}
*/
// ----------------------------------------------------------------------------
// Sort all faces incident on vertex `vi` into CCW order.
//   in    – must be prepared (e2f + e built).
//   vi    – vertex whose star we sort.
//   inc[n]– list of face-indices touching vi.
//   n     – count of faces in inc.
//   order – output permutation of [0..n-1] giving CCW walk around vi.
// ----------------------------------------------------------------------------
static void sort_incident_faces(const Polyhedron *in,
                                uint8_t           vi,
                                const uint8_t    *inc,
                                uint8_t           n,
                                uint8_t          *order)
{
    bool used[POLY_MAX_FV] = { false };
    order[0] = 0;
    used[0]  = true;

    for (uint8_t k = 1; k < n; ++k) {
        uint8_t prev_face = inc[ order[k - 1] ];
        // find the next face sharing an edge with prev_face at vi
        for (uint8_t j = 0; j < n; ++j) {
            if (used[j]) continue;
            uint8_t next_face = inc[j];

            // count common vertices between prev_face and next_face
            uint8_t common = 0;
            for (uint8_t a = 0; a < in->fv[prev_face]; ++a)
                for (uint8_t b = 0; b < in->fv[next_face]; ++b)
                    if (in->f[prev_face][a] == in->f[next_face][b])
                        ++common;

            // if they share exactly two verts, they share an edge
            if (common == 2) {
                order[k] = j;
                used[j]  = true;
                break;
            }
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────
// Build the dual of a polyhedron: faces → verts, verts → faces.
// ──────────────────────────────────────────────────────────────────────────
/*
 *
 *
 *
 *
 */
static void poly_dual(const Polyhedron *in, Polyhedron *out)
{
    if (in->F > POLY_MAX_V || in->V > POLY_MAX_F) return;

    // 1) face centroids → new vertices
    out->V = in->F;
    for (uint16_t f = 0; f < in->F; ++f) {
        poly_face_centroid(in, f, out->v[f]);
        float r = v_len(out->v[f]);
        if (r > 0.0f) v_scale(out->v[f], 1.0f / r);
    }

    // 2) vertex stars → new faces
    out->F = in->V;
    for (uint16_t vi = 0; vi < in->V; ++vi) {
        uint8_t inc[POLY_MAX_FV], cnt = 0;
        for (uint16_t f = 0; f < in->F; ++f) {
            for (uint8_t j = 0; j < in->fv[f]; ++j) {
                if (in->f[f][j] == vi) {
                    inc[cnt++] = f;
                    break;
                }
            }
        }

        if (cnt > 2) {
            uint8_t order[POLY_MAX_FV];
            sort_incident_faces(in, vi, inc, cnt, order);
            for (uint8_t k = 0; k < cnt; ++k)
                out->f[vi][k] = inc[ order[k] ];
        } else {
            // 0–2 faces: no sorting needed
            for (uint8_t k = 0; k < cnt; ++k)
                out->f[vi][k] = inc[k];
        }

        out->fv[vi] = cnt;
    }

    // 3) finalize
    poly_radial_normalize(out);
    poly_prepare(out);
}

// ──────────────────────────────────────────────────────────────────────────
// Truncate every vertex of `in` by cutting off a fraction `t` of each edge.
// ──────────────────────────────────────────────────────────────────────────
/*   - in  : original polyhedron (must have p->E built via poly_prepare)
 *   - out : resulting truncated polyhedron
 *   - t   : cut-fraction along each edge (0 < t < 0.5; 0.5 = midpoint)
 *
 * Algorithm (edge-centric):
 *  1. Call poly_prepare(in) to ensure E, e[] and e2f[][] are valid.
 *  2. For each edge e=(a,b):
 *       - create one new vertex at  LERP(v[a],v[b], t)
 *       - create one new vertex at  LERP(v[b],v[a], t)
 *     (so out->V = 2*in->E)
 *  3. Build the **truncated faces**:
 *     • For each original face f with verts v0,v1,…,v{n-1},
 *       make a new n-gon whose vertices are the “A-cut” points
 *       on edges (v0→v1),(v1→v2)…
 *     • For each original vertex vi, collect all the “B-cut” points
 *       on the edges incident on vi (in cyclic order) → that gives another face.
 *  4. Normalize & rebuild topology with your usual
 *       poly_radial_normalize(out);
 *       poly_prepare(out);
 *
 * You’ll get exactly the **Archimedean** truncation with parameter t.
 */
void poly_truncate(const Polyhedron *in, Polyhedron *out, float t)
{
    /* 1) Allocate a temp on the pool instead of the stack */
    Polyhedron *tmp = poly_alloc();
    if (!tmp) return;                // pool exhausted, just abort
    *tmp = *in;                      // struct‐copy the input
    poly_prepare(tmp);

    /* 2) Create 2 new verts per edge */
    uint8_t cutA[POLY_MAX_E], cutB[POLY_MAX_E];
    out->V = 0;
    for (uint16_t e = 0; e < tmp->E; ++e) {
        uint8_t a = tmp->e[e].a, b = tmp->e[e].b;
        v_lerp(tmp->v[a], tmp->v[b], t, out->v[out->V]);
        cutA[e] = out->V++;
        v_lerp(tmp->v[b], tmp->v[a], t, out->v[out->V]);
        cutB[e] = out->V++;
    }

    /* 3a) Truncate each original face */
    out->F = 0;
    for (uint16_t f = 0; f < tmp->F; ++f) {
        uint8_t n = tmp->fv[f];
        out->fv[out->F] = n;
        for (uint8_t i = 0; i < n; ++i) {
            uint8_t vi = tmp->f[f][i], vj = tmp->f[f][(i+1)%n];
            uint8_t aa = vi < vj ? vi : vj, bb = vi < vj ? vj : vi;
            uint16_t eidx = poly_find_edge(tmp, aa, bb);
            out->f[out->F][i] = (vi == tmp->e[eidx].a ? cutA[eidx] : cutB[eidx]);
        }
        out->F++;
    }

    /* 3b) One new face per original vertex */
    for (uint8_t vi = 0; vi < tmp->V; ++vi) {
        uint8_t inc[POLY_MAX_FV], cnt = 0;
        for (uint16_t e = 0; e < tmp->E; ++e) {
            if (tmp->e[e].a == vi || tmp->e[e].b == vi)
                inc[cnt++] = e;
        }

        /* no need to sort for correctness—just emit in found order */
        out->fv[out->F] = cnt;
        for (uint8_t k = 0; k < cnt; ++k) {
            uint8_t eidx = inc[k];
            out->f[out->F][k] = (vi == tmp->e[eidx].a ? cutA[eidx] : cutB[eidx]);
        }
        out->F++;
    }

    /* 4) Normalize & build topology */
    poly_radial_normalize(out);
    poly_prepare(out);

    /* 5) Return tmp to the pool */
    poly_free(tmp);
}

/* ──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────── */


/* ────────────────────────────────────────────────────────────────────────── */
/* GEOMETRY HELPERS                                                          */
/* ────────────────────────────────────────────────────────────────────────── */

void poly_face_centroid(const Polyhedron *p, uint16_t fidx, float out[3]) {
    out[0]=out[1]=out[2]=0;
    for (uint8_t j=0; j<p->fv[fidx]; ++j)
        v_add(out, p->v[p->f[fidx][j]]);
    v_scale(out, 1.0f / p->fv[fidx]);
}

void poly_face_normal(const Polyhedron *p, uint16_t fidx, float out[3]) {
    // Newell's method – accurate for convex polygons
    out[0]=out[1]=out[2]=0;
    uint8_t n = p->fv[fidx];
    for (uint8_t i=0; i<n; ++i) {
        const float *v0 = p->v[p->f[fidx][i]];
        const float *v1 = p->v[p->f[fidx][(i+1)%n]];
        out[0] += (v0[1]-v1[1])*(v0[2]+v1[2]);
        out[1] += (v0[2]-v1[2])*(v0[0]+v1[0]);
        out[2] += (v0[0]-v1[0])*(v0[1]+v1[1]);
    }
    float inv = 1.0f / v_len(out);
    v_scale(out, inv);
    /*
     *     float len = sqrtf(out[0]*out[0] + out[1]*out[1] + out[2]*out[2]);
     * out[0] /= len;  out[1] /= len;  out[2] /= len;   // unit vector
     */
}




/* ────────────────────────────────────────────────────────────────────────── */
/* EDGE COLLECTION                                                           */
/* ────────────────────────────────────────────────────────────────────────── */

uint16_t poly_edges(const Polyhedron *p, Edge *buf, uint16_t buf_sz) {
    uint16_t cnt = 0;
    for (uint16_t f=0; f<p->F; ++f) {
        for (uint8_t i=0; i<p->fv[f]; ++i) {
            uint16_t a = p->f[f][i];
            uint16_t b = p->f[f][(i+1)%p->fv[f]];
            if (a>b) { uint16_t t=a; a=b; b=t; } // Sort
            bool exists = false;
            for (uint16_t k=0; k<cnt; ++k)
                if (buf[k].a==a && buf[k].b==b) { exists=true; break; }
            if (!exists && cnt<buf_sz) {
                buf[cnt].a = a;
                buf[cnt].b = b;
                ++cnt;
            }
        }
    }
    return cnt;
}


/* ────────────────────────────────────────────────────────────────────────── */
/* TOPOLOGY BUILDER                                                          */
/* ────────────────────────────────────────────────────────────────────────── */

/* ------------------------------------------------------------------
 * _build_edges() – Topology-driven: scan faces, collect unique edges
 * ------------------------------------------------------------------ */
static void _build_edges(Polyhedron *p)
{
    p->E = 0;
    memset(p->e2f, 0xFF, sizeof p->e2f);

    for (uint8_t f = 0; f < p->F; ++f) {
        uint8_t n = p->fv[f];
        for (uint8_t i = 0; i < n; ++i) {
            uint8_t a = p->f[f][i];
            uint8_t b = p->f[f][(i + 1) % n];
            if (a > b) { uint8_t t = a; a = b; b = t; }

            /* Already known? */
            uint8_t e;
            for (e = 0; e < p->E; ++e)
                if (p->e[e].a == a && p->e[e].b == b) break;

            if (e == p->E) {                   /* new edge                        */
                if (p->E >= POLY_MAX_E) break; /* safety                          */
                p->e[e].a = a; p->e[e].b = b;
                ++p->E;
            }
            /* Face adjacency */
            if (p->e2f[e][0] == 0xFF) p->e2f[e][0] = f;
            else                      p->e2f[e][1] = f;
        }
    }
}

/**
 * poly_prepare – normalize geometry and build edge table + e2f map
 */
void poly_prepare(Polyhedron *p) {
    // 1) normalize all points (to unit box or sphere, depending on poly_normalize)
    poly_normalize(p);

    // 2) clear any previous edge→face links
    memset(p->e2f, 0xFF, sizeof p->e2f);

    // 3) scan faces and collect unique edges, filling both p->e[] and p->e2f[][]
    _build_edges(p);
}

/* ────────────────────────────────────────────────────────────────────────── */
/* EDGE + FACE ACCESSORS                                                     */
/* ────────────────────────────────────────────────────────────────────────── */

uint8_t  poly_edge_count(const Polyhedron *p)                  				{ return p->E; }
Edge     poly_get_edge(const Polyhedron *p, uint8_t idx)       				{ return p->e[idx]; }
uint8_t  poly_find_edge(const Polyhedron *p, uint8_t v0, uint8_t v1) 		{if (v0 > v1) { uint8_t t=v0; v0=v1; v1=t; } for (uint8_t e=0; e<p->E; ++e) if (p->e[e].a==v0 && p->e[e].b==v1) return e; return 0xFF;}
void     poly_edge_faces(const Polyhedron *p, uint8_t eidx, uint8_t out[2]) { out[0]=p->e2f[eidx][0]; out[1]=p->e2f[eidx][1]; }
uint8_t  poly_face_vertex_count(const Polyhedron *p, uint8_t fidx) 			{ return p->fv[fidx]; }
const uint8_t* poly_face_vertices(const Polyhedron *p, uint8_t fidx) 		{ return p->f[fidx]; }
bool poly_face_edge_is_ccw(const Polyhedron *p, uint8_t fidx, uint8_t eidx) {
    Edge e = poly_get_edge(p, eidx);
    uint8_t a = e.a, b = e.b;
    uint8_t  n   = poly_face_vertex_count(p, fidx);
    const uint8_t *vs = poly_face_vertices(p, fidx);
    for (uint8_t i = 0; i < n; ++i) {
        uint8_t v0 = vs[i];
        uint8_t v1 = vs[(i + 1) % n];
        if (v0 == a && v1 == b) {
            return true;
        }
    }
    return false;
}

/* -------- rotation helpers (right-handed) ---------------- */
static void rotate_xyz(float v[3], float R[3][3])
{
    float x = v[0], y = v[1], z = v[2];
    v[0] = R[0][0]*x + R[0][1]*y + R[0][2]*z;
    v[1] = R[1][0]*x + R[1][1]*y + R[1][2]*z;
    v[2] = R[2][0]*x + R[2][1]*y + R[2][2]*z;
}

void poly_rotate(Polyhedron *p, float yaw, float pitch, float roll)
{
    /* Z-yaw → Y-pitch → X-roll (Tait-Bryan) */
    float cy = cosf(yaw),  sy = sinf(yaw);
    float cp = cosf(pitch),sp = sinf(pitch);
    float cr = cosf(roll), sr = sinf(roll);

    float R[3][3] = {
        { cy*cp, cy*sp*sr - sy*cr, cy*sp*cr + sy*sr },
        { sy*cp, sy*sp*sr + cy*cr, sy*sp*cr - cy*sr },
        {  -sp ,      cp*sr      ,      cp*cr       }
    };

    for (uint16_t i = 0; i < p->V; ++i)
        rotate_xyz(p->v[i], R);

    /* edges & faces stay valid, but re-normalise just in case */
    poly_prepare(p);
}

void poly_orient_to_vertex(Polyhedron *p, uint8_t vidx) {
    float vx = p->v[vidx][0];
    float vy = p->v[vidx][1];
    float vz = p->v[vidx][2];

    // Yaw um Z: (vx,vy) auf X-Achse
    float yaw = -atan2f(vy, vx);
    // Abstandsvektor in XY-Ebene
    float r   = sqrtf(vx*vx + vy*vy);
    // Pitch um Y: kippt (r,0,vz) auf -Z
    float pitch = atan2f(r, -vz);
    float roll  = 0.0f;

    poly_rotate(p, yaw, pitch, roll);
    poly_prepare(p);
}

void poly_orient_to_edge(Polyhedron *p, uint8_t v0, uint8_t v1) {
    // Kantenindex ermitteln
    uint8_t eidx = poly_find_edge(p, v0, v1);
    if (eidx == 0xFF) return;

    // Angrenzende Flächen holen
    uint8_t faces[2];
    poly_edge_faces(p, eidx, faces);

    // Normale der beiden Flächen
    float n0[3], n1[3];
    poly_face_normal(p, faces[0], n0);
    poly_face_normal(p, faces[1], n1);

    // Summe der Normalen = Gravitation
    float g[3] = { n0[0] + n1[0], n0[1] + n1[1], n0[2] + n1[2] };
    float mag = sqrtf(g[0]*g[0] + g[1]*g[1] + g[2]*g[2]);
    g[0] /= mag; g[1] /= mag; g[2] /= mag;

    // Yaw um Z
    float yaw   = -atan2f(g[1], g[0]);
    // Pitch um Y
    float pitch = -atan2f(sqrtf(g[0]*g[0] + g[1]*g[1]), g[2]);
    float roll  = 0.0f;

    poly_rotate(p, yaw, pitch, roll);
    poly_prepare(p);
}

void poly_orient_to_face(Polyhedron *p, uint8_t fidx) {
    // Flächennormale holen
    float n[3];
    poly_face_normal(p, fidx, n);

    // Yaw um Z
    float yaw   = -atan2f(n[1], n[0]);
    // Pitch um Y
    float pitch = -atan2f(sqrtf(n[0]*n[0] + n[1]*n[1]), n[2]);
    float roll  = 0.0f;

    poly_rotate(p, yaw, pitch, roll);
    poly_prepare(p);
}

/*------------------------------------------------------------------
 * Finalized poly_init_* pipelines: seed → normalize → prepare → (dual/other) → normalize → prepare
 * After these, you can call the init and the Polyhedron is ready for dumping or rendering.
 *------------------------------------------------------------------*/

static void _seed_tri(Polyhedron *p, const float (*V)[3], uint16_t vcnt, const uint8_t (*F)[3], uint16_t fcnt) {
    p->V = vcnt;
    memcpy(p->v, V, sizeof(float)*3*vcnt);
    p->F = fcnt;
    for (uint16_t i = 0; i < fcnt; ++i) {
        p->fv[i] = 3;
        memcpy(p->f[i], F[i], 3);
    }
}

// 1) Tetrahedron (seed with 4 triangles)
void poly_init_tetrahedron(Polyhedron *p) {
    static const float V[4][3] = { { 1,  1,  1}, { 1, -1, -1}, {-1,  1, -1}, {-1, -1,  1} };
    static const uint8_t F[4][3] = { {0,1,2},{0,3,1},{0,2,3},{1,3,2} };
    _seed_tri(p, V, 4, F, 4);
    poly_radial_normalize(p);
    poly_prepare(p);
}

// 2) Cube (seed with 12 triangles)
void poly_init_cube(Polyhedron *p) {
    static const float V[8][3] = {
        { 1, 1, 1},{ 1, 1,-1},{ 1,-1, 1},{ 1,-1,-1},
        {-1, 1, 1},{-1, 1,-1},{-1,-1, 1},{-1,-1,-1}
    };
    static const uint8_t F[12][3] = {
        {0,2,3},{0,3,1}, {4,5,7},{4,7,6},
        {0,1,5},{0,5,4}, {2,6,7},{2,7,3},
        {0,4,6},{0,6,2}, {1,3,7},{1,7,5}
    };
    _seed_tri(p, V, 8, F, 12);
    poly_radial_normalize(p);
    poly_prepare(p);
}

void poly_init_cube4(Polyhedron *p)        /* 6 quads */
{
    static const float V[8][3] = {
        { 1, 1, 1},{ 1, 1,-1},{ 1,-1, 1},{ 1,-1,-1},
        {-1, 1, 1},{-1, 1,-1},{-1,-1, 1},{-1,-1,-1}
    };
    static const uint8_t F[6][4] = {
        {0,2,3,1},{4,5,7,6},
        {0,1,5,4},{2,6,7,3},
        {0,4,6,2},{1,3,7,5}
    };
    p->V = 8;  memcpy(p->v, V, sizeof V);
    p->F = 6;
    for (uint8_t i=0;i<6;++i){ p->fv[i]=4; memcpy(p->f[i],F[i],4); }
    poly_radial_normalize(p);
    poly_prepare(p);
}


// 3) Icosahedron (seed with 20 triangles)
void poly_init_icosahedron(Polyhedron *p) {
    static const float V[12][3] = {
        {0,  1,  PHI},{0, -1,  PHI},{0,  1, -PHI},{0, -1, -PHI},
        {1,  PHI, 0 },{-1, PHI, 0 },{1, -PHI, 0 },{-1,-PHI, 0 },
        {PHI,0,  1  },{PHI,0, -1  },{-PHI,0, 1  },{-PHI,0, -1 }
    };
    static const uint8_t F[20][3] = {
        {0,1,8},{0,8,4},{0,4,5},{0,5,10},{0,10,1},
        {1,8,6},{1,6,7},{1,7,10},
        {2,3,11},{2,11,5},{2,5,4},{2,4,9},{2,9,3},
        {3,9,6},{3,6,7},{3,7,11},
        {4,8,9},{5,11,10},{6,8,9},{7,10,11}
    };
    _seed_tri(p, V, 12, F, 20);
    poly_radial_normalize(p);
    poly_prepare(p);
}




void poly_init_octahedron(Polyhedron *p)
{
    Polyhedron *tmp = poly_alloc();
    if (!tmp) return;
    poly_init_cube4(tmp);
    poly_dual(tmp, p);
    poly_free(tmp);
}


// 5) Dodecahedron (dual of Icosahedron)
void poly_init_dodecahedron(Polyhedron *p) {
    Polyhedron *tmp = poly_alloc();
    if (!tmp) return;
    poly_init_icosahedron(tmp);
    poly_dual(tmp, p);
    poly_free(tmp);
}

void poly_init_icosidodecahedron(Polyhedron *p)
{
    Polyhedron *dode  = poly_alloc();
    Polyhedron *trunc = poly_alloc();
    if (!dode || !trunc) {
        if (dode)  poly_free(dode);
        if (trunc) poly_free(trunc);
        return;
    }
    // 1) seed dodecahedron
    poly_init_dodecahedron(dode);
    // 2) half-truncate → triangles + pentagons
    poly_truncate(dode, trunc, 0.5f);
    // 3) copy result into caller‐supplied buffer
    *p = *trunc;    //<— this must actually write back into *p

    // cleanup
    poly_free(dode);
    poly_free(trunc);
}

void poly_init_rhombitruncated_icosidodecahedron(Polyhedron *p)
{
    // scratch on heap, not stack
    Polyhedron *seed  = poly_alloc();
    Polyhedron *tmp   = poly_alloc();
    if (!seed || !tmp) {
        if (seed) poly_free(seed);
        if (tmp)  poly_free(tmp);
        return;
    }

    // 1) build Archimedean icosidodecahedron
    poly_init_icosidodecahedron(seed);
    // ensure its topology is up-to-date
    poly_radial_normalize(seed);
    poly_prepare(seed);

    // 2) truncate at t=0.5 → Archimedean truncated icosidodecahedron
    poly_truncate(seed, tmp, 0.5f);

    // 3) dualize → rhombic solid
    poly_dual(tmp, p);

    // 4) final normalize + prepare
    poly_radial_normalize(p);
    poly_prepare(p);

    // cleanup
    poly_free(seed);
    poly_free(tmp);
}




