#pragma once

#include <stdio.h>
#include <stdlib.h>

static inline const char* get_linuxcnc_option(const char* name) {
    const char* opt = getenv(name);
    if(!opt) {
        return "";
    }

    return opt;
}

/* LINELEN is used throughout for buffer sizes, length of file name strings,
   etc. Let's just have one instead of a multitude of defines all the same. */
#define LINELEN 255
/* Used in a number of places for sprintf() buffers. */
#define BUFFERLEN 80

#define MM_PER_INCH 25.4
#define INCH_PER_MM (1.0/25.4)

#define EMC2_HOME get_linuxcnc_option("EMC2_HOME")
#define EMC2_SUFFIX get_linuxcnc_option("EMC2_SUFFIX")
#define EMC2_ICON get_linuxcnc_option("EMC2_ICON")
#define EMC2_RTLIB_DIR get_linuxcnc_option("EMC2_RTLIB_DIR")
#define EMC2_BIN_DIR get_linuxcnc_option("EMC2_BIN_DIR")
#define EMC2_PO_DIR get_linuxcnc_option("EMC2_PO_DIR")
#define MODULE_EXT get_linuxcnc_option("MODULE_EXT")
#define RTDIR get_linuxcnc_option("RTDIR")
#define EMC2_DEFAULT_NMLFILE get_linuxcnc_option("EMC2_DEFAULT_NMLFILE")

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif