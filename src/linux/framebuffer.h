#pragma once
#include <stdint.h>
/* All access to a live surface, including bind callbacks, must hold the
 * guest machine lock. Snapshots own independent memory and may be used outside it. */
typedef struct { uint32_t *pixels; int width, height; } civ3_surface;
typedef void (*civ3_surface_bind)(const civ3_surface *, void *);
int civ3_surface_resize(civ3_surface *, int width, int height, civ3_surface_bind, void *);
int civ3_surface_snapshot(const civ3_surface *, civ3_surface *copy);
void civ3_surface_dispose(civ3_surface *);
int civ3_surface_write_ppm(const civ3_surface *, const char *path);
