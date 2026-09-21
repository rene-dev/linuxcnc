/********************************************************************
* Description: emcargs.cc
*   Globals initialized to values in emccfg.h
*
*   Derived from a work by Fred Proctor & Will Shackleford
*
* Author:
* License: GPL Version 2
* System: Linux
*    
* Copyright (c) 2004 All rights reserved.
*
* Last change:
********************************************************************/

#include "strutil.hh"
#include <string.h>		/* strcpy() */
#include <stdio.h>		/* fprintf() */
#include "emcglb.h"		/* these decls */

using namespace linuxcnc;

int emcGetArgs(int argc, char *argv[])
{
    int t;

    /* process command line args, indexing argv[] from [1] */
    for (t = 1; t < argc; t++) {
	if (!strcmp(argv[t], "-ini")) {
	    if (t == argc - 1) {
		return -1;
	    } else {
                if (strlen(argv[t+1]) >= LINELEN) {
                    fprintf(stderr, "INI file name too long (max %d):\n", LINELEN);
                    fprintf(stderr, "    %s\n", argv[t+1]);
                    return -1;
                }
		strxcpy(emc_inifile, argv[t + 1]);
		t++;
	    }
	    continue;
	}
	/* else not recognized -- ignore */
    }

    return 0;
}
