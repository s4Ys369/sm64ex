#ifndef DYNOS_H
#define DYNOS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <limits.h>
#include <dirent.h>
#include <SDL2/SDL.h>
#ifdef __cplusplus
#include <new>
#include <utility>
#include <string>
extern "C" {
#endif
#include "types.h"
#include "config.h"
#include "engine/math_util.h"
#include "pc/configfile.h"
#include "pc/fs/fs.h"
#undef STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#ifdef __cplusplus
}
#endif

#define DYNOS_FOLDER            "dynos"
#define DYNOS_GFX_FOLDER        DYNOS_FOLDER "/gfx"
#define DYNOS_AUDIO_FOLDER      DYNOS_FOLDER "/audio"
#define DYNOS_PACKS_FOLDER      DYNOS_FOLDER "/packs"
#define DYNOS_CONFIG_FILENAME   "DynOSconfig.cfg"

//
// Routines
//

enum {
    DYNOS_ROUTINE_OPT_UPDATE,  // Executed once per frame, before running the level script
    DYNOS_ROUTINE_GFX_UPDATE,  // Executed once per frame, before rendering the scene
    DYNOS_ROUTINE_LEVEL_START, // Executed at the start of a level
};
typedef void (*DynosRoutine)(void *);

#endif