/**********************************************************************
 *
 * PostGIS - Spatial Types for PostgreSQL
 * http://postgis.net
 *
 * Copyright (C) 2014 Sandro Santilli <strk@keybit.net>
 *
 * This is free software; you can redistribute and/or modify it under
 * the terms of hte GNU General Public Licence. See the COPYING file.
 *
 **********************************************************************/

#include "lwout_vt.h"
#include "liblwgeom_internal.h"
#include "varint.h"

typedef struct
{
  int c; /* command */
  int x;
  int y;
} draw_command;

#define CMD_MOVE_TO 1
#define CMD_LINE_TO 2
#define CMD_CLOSE_PATH 7

typedef struct 
{
  draw_command *cmds;
  unsigned int size;
  unsigned int capacity;
  int x0;
  int y0;
} dbuf;


#define TRANSFORMY(y,c) ((int)rint(((y)-c->ipy)/c->sfy))
#define TRANSFORMX(x,c) ((int)rint(((x)-c->ipx)/c->sfx))

static dbuf*
dbuf_new(size_t init_size)
{
  dbuf *b = lwalloc(sizeof(dbuf));
  if ( ! b ) {
    lwerror("Out of virtual memory creating draw buffer");
    return NULL;
  }
  b->capacity = b->size = init_size;
  b->cmds = lwalloc(b->capacity * sizeof(draw_command));
  if ( ! b->cmds ) {
    lwfree(b);
    lwerror("Could not allocate %d commands in draw buffer", init_size);
    return NULL;
  }
  b->x0 = b->y0 = 0;
  return b;
}

static void
dbuf_append(dbuf *buf, const draw_command* c)
{
  draw_command *tmp;
  buf->size += 1;
  if ( buf->size > buf->capacity ) {
    buf->capacity *= 2;
    tmp = lwrealloc(buf->cmds, buf->capacity * sizeof(draw_command));
    if ( ! buf ) {
      lwerror("Out of virtual memory reallocating command buffer");
      return;
    }
    buf->cmds = tmp;
  }
  memcpy(&buf->cmds[buf->size-1], c, sizeof(draw_command));
}

/**
 * @param x relative movement in the x direction
 * @param y relative movement in the x direction
 */
static void
dbuf_moveTo(dbuf *buf, int x, int y)
{
  draw_command cmd = {
    CMD_MOVE_TO, x, y
  };
  dbuf_append(buf, &cmd);
}

/**
 * @param x relative movement in the x direction
 * @param y relative movement in the x direction
 */
static void
dbuf_lineTo(dbuf *buf, int x, int y)
{
  draw_command cmd = {
    CMD_LINE_TO, x, y
  };
  dbuf_append(buf, &cmd);
}

/** Compute size of encoded draw buffer */
static size_t
dbuf_encoded_size(const dbuf *buf)
{
  unsigned int i;
  size_t sz = 0;
  int last_command = 0;

  /* worst case: no commands are grouped, all params are 4 bytes */
  /* return buf->size * ( 1 + 4 + 4 ); */

  for (i=0; i<buf->size; ++i) {
    draw_command* dc = &(buf->cmds[i]);
    if ( ! last_command ) {
      ++sz;
      last_command = dc->c;
    }
    else if ( dc->c != last_command ) {
      ++sz;
    }
    sz += varint_s32_encoded_size(dc->x);
    sz += varint_s32_encoded_size(dc->y);
  }

  return sz;
}

/**
 *  Encode draw buffer to given memory buffer.
 *
 *  The target memory buffer must be allocated by the
 *  caller and have a size of at least dbuf_encoded_size(buf)
 *
 *  @returns a pointer to the byte next to the last one of the
 *           encoded output.
 */
static uint8_t *
dbuf_encode_buf(const dbuf *buf, uint8_t *to)
{
  unsigned int i, j;
  size_t sz;
  int last_command = 0;
  const int cmd_bits = 3;

  for (i=0; i<buf->size; ++i) {
    draw_command* dc = &(buf->cmds[i]);
    if ( ! last_command || dc->c != last_command ) {
      last_command = dc->c;
      /* find size of this command */
      for (j=i+1; j<buf->size; ++j) {
        if ( buf->cmds[j].c != last_command ) break;
      }
      sz = j-1;
      /* encode command + length */
      *to++ = sz << cmd_bits | dc->c;
    }
    /* encode X parameter */
    to = varint_s32_encode_buf(dc->x, to);
    /* encode Y parameter */
    to = varint_s32_encode_buf(dc->y, to);
  }

  return to;
}

static void
vt_draw_ptarray(const POINTARRAY *pa, const lw_vt_cfg *cfg, dbuf *buf)
{
  int i;
  int x, y;
  int xd, yd;
  double *dptr;

  if ( ! pa->npoints ) return;

  for ( i = 0; i < pa->npoints; ++i )
  {
    dptr = (double*)getPoint_internal(pa, i);
    x = TRANSFORMX(*dptr++, cfg);
    y = TRANSFORMY(*dptr, cfg);
    xd = x - buf->x0;
    yd = y - buf->y0;
    if ( xd || yd ) {
      if ( ! i ) {
        /* Write first moveTo */
        dbuf_moveTo(buf, xd, yd);
      } else {
        /* Write lineTo for subsequent vertices */
        dbuf_lineTo(buf, xd, yd);
      }
      buf->x0=x;
      buf->y0=y;
    }
  }
}

static void
vt_draw_point(const LWPOINT *g, const lw_vt_cfg *cfg, dbuf *buf)
{
  vt_draw_ptarray(g->point, cfg, buf);
}

static void
vt_draw_line(const LWLINE *g, const lw_vt_cfg *cfg, dbuf *buf)
{
  vt_draw_ptarray(g->points, cfg, buf);
}

static void
vt_draw_poly(const LWPOLY *g, const lw_vt_cfg *cfg, dbuf *buf)
{
  int i = g->nrings;
  for ( i = 0; i < g->nrings; ++i ) {
    vt_draw_ptarray(g->rings[i], cfg, buf);
  }
}

static void vt_draw_geom(const LWGEOM *g, const lw_vt_cfg *cfg, dbuf *buf);

static void
vt_draw_coll(const LWCOLLECTION *g, const lw_vt_cfg *cfg, dbuf *buf)
{
  int i = g->ngeoms;
  for ( i = 0; i < g->ngeoms; ++i ) {
    vt_draw_geom(g->geoms[i], cfg, buf);
  }
}

static void
vt_draw_geom(const LWGEOM *geom, const lw_vt_cfg *cfg, dbuf *buf)
{
  int type = geom->type;
  switch (type)
  {
    case POINTTYPE:
      vt_draw_point((LWPOINT*)geom, cfg, buf);
      break;
    case LINETYPE:
      vt_draw_line((LWLINE*)geom, cfg, buf);
      break;
    case POLYGONTYPE:
      vt_draw_poly((LWPOLY*)geom, cfg, buf);
      break;
    case MULTIPOINTTYPE:
    case MULTILINETYPE:
    case MULTIPOLYGONTYPE:
    case COLLECTIONTYPE:
      vt_draw_coll((LWCOLLECTION*)geom, cfg, buf);
      break;
    default:
      lwerror("vt_draw_geom: '%s' geometry type not supported",
            lwtype_name(type));
      break;
  }
}

/**
 * Takes a GEOMETRY and returns a VectorTile.geometry representation
 * See https://github.com/mapbox/vector-tile-spec
 */
uint8_t *
lwgeom_to_vt_geom(const LWGEOM *geom, const lw_vt_cfg *cfg)
{
  dbuf *buf = dbuf_new(8);
  size_t encoded_size;
  uint8_t *encoded;

  vt_draw_geom(geom, cfg, buf);

  encoded_size = dbuf_encoded_size(buf);
  encoded = lwalloc(encoded_size);
  dbuf_encode_buf(buf, encoded);

  return encoded;
}

