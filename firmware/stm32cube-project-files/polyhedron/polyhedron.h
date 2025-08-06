#ifndef POLYHEDRON_H
#define POLYHEDRON_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>   // For sqrtf()

/* ────────────────────────────────────────────────────────────────────────── */
/* CONFIGURATION: Polyhedron memory limits                                    */
/* ────────────────────────────────────────────────────────────────────────── */


//You’re overflowing the Cortex-M stack when you build polyhedra inside the initializer.
//With POLY_MAX_V = 200 each Polyhedron is ≈ 10 kB
//The default bare metal stack on CubeIDE projects is often 4 kB or 8 kB

// do not increase any of those over 255
#define POLY_MAX_V   200     // Maximum number of vertices
#define POLY_MAX_E   300     // Maximum number of unique edges
#define POLY_MAX_F   120     // Maximum number of faces
#define POLY_MAX_FV  10      // Maximum vertices per face (ie 8 = octagon, 10 dodecagon etc..)

// note that these values lead top ~8kbytes. building larger ones leads to stack overflow. (not functional rn)

#define PHI  ((1.0f + sqrtf(5.0f)) * 0.5f)  // Golden ratio




/* ────────────────────────────────────────────────────────────────────────── */
/* DATA STRUCTURES                                                            */
/* ────────────────────────────────────────────────────────────────────────── */

typedef struct {
    uint16_t a, b;    // Vertex indices, sorted: a < b
} Edge;

typedef struct {
    /* ── Base geometry ─────────────────────────────── */
    uint8_t  V;                            // Number of vertices
    float    v[POLY_MAX_V][3];             // Vertex positions (XYZ)

    uint8_t  F;                            // Number of faces
    uint8_t  fv[POLY_MAX_F];              // Vertices per face
    uint8_t  f[POLY_MAX_F][POLY_MAX_FV];  // Vertex indices per face

    /* ── Derived topology ──────────────────────────── */
    uint8_t  E;                            // Number of unique edges
    Edge     e[POLY_MAX_E];               // Edge list (a < b)
    uint8_t  e2f[POLY_MAX_E][2];          // Edge → Face adjacency (2 faces per edge)
} Polyhedron;


/* ────────────────────────────────────────────────────────────────────────── */
/* CORE FUNCTIONS                                                             */
/* ────────────────────────────────────────────────────────────────────────── */

/* ── Geometry Helpers ───────────────────────────────────────────────────── */
void  poly_face_centroid(const Polyhedron *p, uint16_t fidx, float out[3]);
void  poly_face_normal  (const Polyhedron *p, uint16_t fidx, float out[3]);

/* ── Edge Table Builder ─────────────────────────────────────────────────── */
uint16_t poly_edges(const Polyhedron *p, Edge *buf, uint16_t buf_sz);


/* ────────────────────────────────────────────────────────────────────────── */
/* SEED POLYHEDRA (Platonic Solids)                                          */
/* ────────────────────────────────────────────────────────────────────────── */
void poly_rotate(Polyhedron *p, float yaw, float pitch, float roll);

/**
 * Orientiert das Polyeder so, dass der angegebene Vertex unten steht (Z-Achse durch den Vertex).
 * @param p     Pointer auf das Polyhedron-Objekt
 * @param vidx  Index des Vertex
 */
void poly_orient_to_vertex(Polyhedron *p, uint8_t vidx);

/**
 * Orientiert das Polyeder so, dass die angegebene Kante unten steht.
 * Die Kante wird definiert durch die beiden Vertex-Indizes.
 * @param p     Pointer auf das Polyhedron-Objekt
 * @param v0    Erster Vertex-Index der Kante
 * @param v1    Zweiter Vertex-Index der Kante
 */
void poly_orient_to_edge(Polyhedron *p, uint8_t v0, uint8_t v1);

/**
 * Orientiert das Polyeder so, dass die angegebene Fläche unten steht.
 * @param p     Pointer auf das Polyhedron-Objekt
 * @param fidx  Index der Face
 */
void poly_orient_to_face(Polyhedron *p, uint8_t fidx);

void poly_init_tetrahedron 							(Polyhedron *p);
void poly_init_cube        							(Polyhedron *p);
void poly_init_octahedron  							(Polyhedron *p);
void poly_init_icosahedron 							(Polyhedron *p);
void poly_init_dodecahedron							(Polyhedron *p); // Dual of icosahedron
void poly_init_rhombitruncated_icosidodecahedron	(Polyhedron *p); //broke rn (definete stack overflow)
void poly_init_icosidodecahedron					(Polyhedron *p); //can get to work maybe (tune down max FV etc..)

/* ────────────────────────────────────────────────────────────────────────── */
/* TOPOLOGY HELPERS                                                           */
/* ────────────────────────────────────────────────────────────────────────── */

void     poly_prepare(Polyhedron *p);  // Builds edges + e2f

/* ── Edge Access ────────────────────────────────────────────────────────── */
uint8_t  poly_edge_count(const Polyhedron *p);
Edge     poly_get_edge(const Polyhedron *p, uint8_t idx);
uint8_t  poly_find_edge(const Polyhedron *p, uint8_t v0, uint8_t v1);
void     poly_edge_faces(const Polyhedron *p, uint8_t edgeIdx, uint8_t out[2]);

/* ── Face Access ────────────────────────────────────────────────────────── */
uint8_t        	poly_face_vertex_count(const Polyhedron *p, uint8_t faceIdx);
const uint8_t* 	poly_face_vertices(const Polyhedron *p, uint8_t faceIdx);
bool 			poly_face_edge_is_ccw(const Polyhedron *p, uint8_t faceIdx, uint8_t edgeIdx);

/*────────────────────  SMALL POOL ALLOCATOR  ────────────────────*/
/**
 * Allocate one zero-initialized Polyhedron on the heap.
 * Returns NULL if out of memory.
 */
static inline Polyhedron *poly_alloc(void) {
    Polyhedron *p = malloc(sizeof *p);
    if (p) memset(p, 0, sizeof *p);
    return p;
}

/**
 * Free a Polyhedron previously returned by poly_alloc().
 */
static inline void poly_free(Polyhedron *p) {
    free(p);
}
#endif  // POLYHEDRON_H
