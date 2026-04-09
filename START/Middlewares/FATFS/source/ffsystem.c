/**
 * @file    ffsystem.c
 * @brief   FatFS system interface — NECCS project
 * @details FF_FS_NORTC=1 so get_fattime() is not needed.
 *          FF_USE_LFN=2 (stack-based) so ff_memalloc/ff_memfree not needed.
 *          This file is kept minimal — only required stubs.
 */
#include "ff.h"








