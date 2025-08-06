/* --------------------------------------------------------------------------
 * geo_debug.h – Geometry wireframe dumper (updated)
 * -------------------------------------------------------------------------- */
#ifndef GEO_DEBUG_H
#define GEO_DEBUG_H

#include "polyhedron.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Dump a polyhedron's wireframe over USB for external plotting.
 * @param p     Pointer to the Polyhedron to dump
 * @param name  Null-terminated window title or tag
 */
void geo_dump_wireframe(const Polyhedron *p, const char *name);

void geo_dump_model(const Polyhedron *p, const char *tag);

#ifdef __cplusplus
}
#endif

#endif // GEO_DEBUG_H
