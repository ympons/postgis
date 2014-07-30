/**********************************************************************
 *
 * PostGIS - Spatial Types for PostgreSQL
 * http://postgis.net
 *
 * Copyright (C) 2014 Sandro Santilli <strk@keybit.net>
 *
 * This is free software; you can redistribute and/or modify it under
 * the terms of the GNU General Public Licence. See the COPYING file.
 *
 **********************************************************************/

#ifndef _LIBLWGEOM_LWOUT_VT_H
#define _LIBLWGEOM_LWOUT_VT_H 1

#include "liblwgeom.h"

typedef struct
{
  /** X ordinate value of the tile origin */
  double ipx;

  /** Y ordinate value of the tile origin */
  double ipy;

  /** Scale factor X */
  double sfx;

  /** Scale factor Y */
  double sfy;

} lw_vt_cfg;


/**
 * Takes a GEOMETRY and returns a VectorTile.geometry representation
 * See https://github.com/mapbox/vector-tile-spec
 */
uint8_t * lwgeom_to_vt_geom(const LWGEOM *geom, const lw_vt_cfg *cfg);

#endif /* !defined _LIBLWGEOM_LWOUT_VT_H  */
