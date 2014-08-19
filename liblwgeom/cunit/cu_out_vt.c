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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "CUnit/Basic.h"

#include "liblwgeom_internal.h"
#include "cu_tester.h"

/*
** Global variable to hold hex VT strings
*/
char *s;
lwvt_cfg *default_cfg;

/*
** The suite initialization function.
** Create any re-used objects.
*/
static int init_out_vt_suite(void)
{
	s = NULL;
  default_cfg = lwvt_cfg_create(0,0,1,1);
	return 0;
}

/*
** The suite cleanup function.
** Frees any global objects.
*/
static int clean_out_vt_suite(void)
{
	if (s) free(s);
  if ( default_cfg ) lwvt_cfg_release(default_cfg);
	s = NULL;
	return 0;
}

/*
** Creating an output WKB from a vt string 
** 
** @param cfg Vector Tileconfiguration to use, or null for a default
**            being ipx=ipy=0, sfx=sfy=1
*/
static void cu_vt(const char *wkt, lwvt_cfg *cfg)
{
	LWGEOM *g = lwgeom_from_wkt(wkt, LW_PARSER_CHECK_NONE);
  size_t sz;
  uint8_t *vt;

  if ( ! cfg ) cfg = default_cfg;
  vt = lwgeom_to_vt_geom(g, cfg, &sz);

  if ( s ) free(s);
	if ( vt ) {
    lwgeom_free(g);
    s = hexbytes_from_bytes(vt, sz);
    lwfree(vt);
  } else {
    s = strdup("DEADBEEF");
  }
}

#define do_test_vt_geom(wkt, cfg, expected_hex) { \
	cu_vt(wkt, cfg); \
  if ( strcmp(s, expected_hex) ) {{ \
	  printf(" %s:%d: Exp: %s\n", __FILE__, __LINE__, expected_hex); \
    printf(" %s:%d: Obt: %s\n", __FILE__, __LINE__, s); \
  }} \
	CU_ASSERT_STRING_EQUAL(s, expected_hex); \
}


static void test_vt_out_point(void)
{
  /* first byte is count=1 (1<<3) | cmd=moveTo (1) */
	do_test_vt_geom("POINT(0 0 0 0)", NULL, "090000");

  /* zigzag makes 01 become 02 */ 
	do_test_vt_geom("SRID=4;POINTM(1 2 1)", NULL, "090204");

	do_test_vt_geom("POINTZ(-1 -2 1)", NULL, "090103"); 
  /*
    encoding of Y value (-2):
    1111:1111 ... 1111:1110 - input (fffffffe)
    1111:1111 ... 1111:1100 - A: input << 1
    1111:1111 ... 1111:1111 - B: input >> 31
    0000:0000 ... 0000:0011 - zigzag (A xor B) == output
   */
}

static void test_vt_out_linestring(void)
{
	do_test_vt_geom("LINESTRING(0 0, 1 1, 0 3)",
      NULL,
      "0900001202020104"); 
/*
       ^^ 0000:1001 ( 1 moveTo )
         ^^^^ (0,0)
             ^^ 0001:0010 ( 2 lineTo )
               ^^^^ (+1,+1 -- zigzag'ed to 02,02)
                   ^^^^ (-1,+1 -- zigzag'ed to 01,04)
*/

	do_test_vt_geom("LINESTRING(0 0, 0 1, 0 2, 0 3, 0 4, \
                              0 5, 0 6, 0 7, 0 8, 0 9, \
                              0 10, 0 11, 0 12, 0 13, 0 14, \
                              0 15, 0 16, 0 17, 0 18, 0 19, \
                              0 20, 0 21, 0 22, 0 23, 0 24, \
                              0 25, 0 26, 0 27, 0 28, 0 29, \
                              0 30, 0 31, 0 32)",
      NULL,
      "09"
    // ^^ 0000:1001 ( 1 moveTo )
      "0000"
    // ^^^^ moveTo(0,0)
      "8202"
    // ^^^^ 1000:0010 0000:0010 
    //       000:0010  000:0010 -- chop high bits
    //      0000:0001 0000:0010 -- concatenate 
    //      0000:0000 0010:0000 -- right shift >> 3 to find length (32)
    //      Meaning: 32 lineTo commands follow
      "0002000200020002" // 4
      "0002000200020002" // 8
      "0002000200020002" // 12
      "0002000200020002" // 16
      "0002000200020002" // 20
      "0002000200020002" // 24
      "0002000200020002" // 28
      "0002000200020002" // 32
      ); 
}

/*
** Used by test harness to register the tests in this file.
*/

CU_TestInfo vt_out_tests[] =
{
	PG_TEST(test_vt_out_point),
	PG_TEST(test_vt_out_linestring),
	CU_TEST_INFO_NULL
};
CU_SuiteInfo out_vt_suite = {"vt",  init_out_vt_suite,  clean_out_vt_suite, vt_out_tests};
