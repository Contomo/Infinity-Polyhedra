/* --------------------------------------------------------------------------
 * geo_debug.c – Geometry wireframe dumper implementation
 * -------------------------------------------------------------------------- */
#include <stdio.h>
#include <math.h>
#include "geo_debug.h"
#include "usb_comms.h"   /* USBD_UsrLog */
#include "led_anim.h"      // for vertex_hue_from_xyz()
#include "led_debug.h" // debug_hue

static float edge_len(const Polyhedron *p, uint8_t e)
{
    Edge ed = p->e[e];
    const float *A = p->v[ed.a];
    const float *B = p->v[ed.b];
    float dx = A[0] - B[0];
    float dy = A[1] - B[1];
    float dz = A[2] - B[2];
    return sqrtf(dx*dx + dy*dy + dz*dz);
}

void geo_dump_wireframe(const Polyhedron *p, const char *name)
{
    /* Start dump with tag and metadata */
    USBD_UsrLog("#geo# %s V=%u E=%u", name, p->V, p->E);

    /* Emit each vertex */
    for (uint8_t v = 0; v < p->V; ++v) {
        USBD_UsrLog("v %u %.6f %.6f %.6f",
                    v,
                    (double)p->v[v][0],
                    (double)p->v[v][1],
                    (double)p->v[v][2]);
    }

    /* Emit each edge with length */
    for (uint8_t e = 0; e < p->E; ++e) {
        Edge ed = p->e[e];
        USBD_UsrLog("e %u %u %u %.6f",
                    e,
                    ed.a, ed.b,
                    (double)edge_len(p, e));
    }

    /* End dump marker */
    USBD_UsrLog("#endgeo#");
}



// how many vertices/edges per line
#define VERTS_PER_LINE  4
#define EDGES_PER_LINE 10

void geo_dump_model(const Polyhedron *p, const char *tag)
{
    // header: still include V, E, F counts
    USBD_UsrLog("#geo# %s V=%u E=%u F=%u",
                tag, p->V, p->E, p->F);

    // --- chunked vertex lines ---
    {
        char buf[256];
        int pos;
        for (uint8_t start = 0; start < p->V; start += VERTS_PER_LINE) {
            pos = snprintf(buf, sizeof(buf), "V:");
            for (uint8_t v = start;
                 v < p->V && v < start + VERTS_PER_LINE;
                 ++v)
            {
                uint8_t h;
                vertex_hue_from_xyz(p->v[v], &h, debug_hue);
                pos += snprintf(buf + pos, sizeof(buf) - pos,
                    "%u,(%.3f,%.3f,%.3f,%u); ",
                    v,
                    p->v[v][0], p->v[v][1], p->v[v][2],
                    h
                );
            }
            USBD_UsrLog("%s", buf);
        }
    }

    // --- chunked edge lines ---
    {
        char buf[256];
        int pos;
        for (uint8_t start = 0; start < p->E; start += EDGES_PER_LINE) {
            pos = snprintf(buf, sizeof(buf), "E:");
            for (uint8_t e = start;
                 e < p->E && e < start + EDGES_PER_LINE;
                 ++e)
            {
                const Edge *ed = &p->e[e];
                pos += snprintf(buf + pos, sizeof(buf) - pos,
                    "(%u-%u), ",
                    ed->a, ed->b
                );
            }
            USBD_UsrLog("%s", buf);
        }
    }

    // --- one line per face ---
    {
        char buf[128];
        int pos;
        for (uint8_t f = 0; f < p->F; ++f) {
            pos = snprintf(buf, sizeof(buf), "f%u:", f);
            for (uint8_t i = 0; i < p->fv[f]; ++i) {
                pos += snprintf(buf + pos, sizeof(buf) - pos,
                    "%u,", p->f[f][i]
                );
            }
            USBD_UsrLog("%s", buf);
        }
    }

    // footer
    USBD_UsrLog("#endgeo#");
}
