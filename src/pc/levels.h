#ifndef LEVELS_H
#define LEVELS_H

/*
This file helps to provide useful info about the game's levels,
such as level count, level list, courses, scripts and names.
*/

#include <stdbool.h>
#include "types.h"
#include "level_table.h"
#include "course_table.h"

int level_get_count(bool noCastle);
const enum LevelNum *level_get_list(bool noCastle, bool ordered);
enum CourseNum level_get_course(enum LevelNum levelNum);
const LevelScript *level_get_script(enum LevelNum levelNum);
const u8 *level_get_name(enum LevelNum levelNum, bool decaps, bool addCourseNumber);
const u8 *level_get_star_name(enum LevelNum levelNum, int starNum, bool decaps, bool addStarNumber);

#endif // LEVELS_H