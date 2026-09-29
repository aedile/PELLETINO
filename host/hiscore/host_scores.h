/* what the games' host harnesses use on top of hiscore.h - see store_file.c */
#pragma once
#include "hiscore.h"
void host_scores_frame(const hiscore_t *g, double now);
void host_scores_dump(const hiscore_t *g);
