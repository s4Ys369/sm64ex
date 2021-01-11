#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"
#include "levels.h"
#include "game/segment2.h"
#ifdef RENDER96_2_0
#include "text/text-loader.h"
#endif

#define STUB_LEVEL(_0, _1, _2, _3, _4, _5, _6, _7, _8)
#define DEFINE_LEVEL(_0, _1, _2, _name, _4, _5, _6, _7, _8, _9, _10) extern const LevelScript level_##_name##_entry[];
#include "levels/level_defines.h"
#undef DEFINE_LEVEL
#undef STUB_LEVEL

//
// Level, Course, Script
//

struct LevelCourseScript {
    enum LevelNum level;
    enum CourseNum course;
    const LevelScript *script;
};

#define STUB_LEVEL(_0, _1, _2, _3, _4, _5, _6, _7, _8)
#define DEFINE_LEVEL(_0, _level, _course, _name, _4, _5, _6, _7, _8, _9, _10) { _level, _course, level_##_name##_entry },
static const struct LevelCourseScript sLevelCourseScript[] = {
#include "levels/level_defines.h"
};
static const int sLevelCourseScriptCount = sizeof(sLevelCourseScript) / sizeof(sLevelCourseScript[0]);
#undef DEFINE_LEVEL
#undef STUB_LEVEL

static const u8 sEmpty[]        = { 255 };
static const u8 sCastle[]       = { 12, 36, 54, 55, 47, 40, 255 };
static const u8 sBowser1[]      = { 11, 50, 58, 54, 40, 53, 158, 1, 255 };
static const u8 sBowser2[]      = { 11, 50, 58, 54, 40, 53, 158, 2, 255 };
static const u8 sBowser3[]      = { 11, 50, 58, 54, 40, 53, 158, 3, 255 };
static const u8 s100CoinsStar[] = { 1, 0, 0, 158, 12, 50, 44, 49, 54, 158, 28, 55, 36, 53, 255 };

//
// Accessors
//

static enum CourseNum get_course(enum LevelNum levelNum) {
    for (int i = 0; i != sLevelCourseScriptCount; ++i) {
        if (sLevelCourseScript[i].level == levelNum) {
            return sLevelCourseScript[i].course;
        }
    }
    return COURSE_NONE;
}

static const LevelScript *get_script(enum LevelNum levelNum) {
    for (int i = 0; i != sLevelCourseScriptCount; ++i) {
        if (sLevelCourseScript[i].level == levelNum) {
            return sLevelCourseScript[i].script;
        }
    }
    return NULL;
}

static void set_level_name(u8 *buffer, enum LevelNum levelNum) {
    if (levelNum == LEVEL_BOWSER_1)   { memcpy(buffer, sBowser1, str64l(sBowser1)); return; }
    if (levelNum == LEVEL_BOWSER_2)   { memcpy(buffer, sBowser2, str64l(sBowser2)); return; }
    if (levelNum == LEVEL_BOWSER_3)   { memcpy(buffer, sBowser3, str64l(sBowser3)); return; }

    enum CourseNum courseNum = get_course(levelNum);
    if (courseNum < COURSE_BOB)       { memcpy(buffer, sCastle,  str64l(sCastle));  return; }
    if (courseNum >= COURSE_CAKE_END) { memcpy(buffer, sCastle,  str64l(sCastle));  return; }
    
    const u8 *courseName = ((const u8 **) seg2_course_name_table)[courseNum - COURSE_BOB] + 3;
    memcpy(buffer, courseName, str64l(courseName));
}

static void set_act_name(u8 *buffer, enum LevelNum levelNum, int starNum) {
    enum CourseNum courseNum = get_course(levelNum);
    if (courseNum > COURSE_STAGES_MAX) { memcpy(buffer, sEmpty,        str64l(sEmpty));        return; }
    if (starNum == 7)                  { memcpy(buffer, s100CoinsStar, str64l(s100CoinsStar)); return; }

    const u8 *actName = ((const u8 **) seg2_act_name_table)[(courseNum - COURSE_BOB) * 6 + starNum - 1];
    memcpy(buffer, actName, str64l(actName));
}

static void prefix_number(u8 *buffer, int num) {
    memmove(buffer + 5, buffer, str64l(buffer));
    buffer[0] = ((num / 10) == 0 ? 158 : (num / 10));
    buffer[1] = (num % 10);
    buffer[2] = 158;
    buffer[3] = 159;
    buffer[4] = 158;
}

//
// Data
//

static int sLevelCount[2]                = { 0, 0 };
static enum LevelNum *sLevelList[2][2]   = { { NULL, NULL }, { NULL, NULL } };
static enum CourseNum *sLevelCourses     = NULL;
static const LevelScript **sLevelScripts = NULL;

// Runs only once
static void level_init_data() {
    static bool inited = false;
    if (!inited) {
        
        // Level count
        sLevelCount[0] = sLevelCourseScriptCount;

        // Level count (no Castle)
        sLevelCount[1] = 0;
        for (int i = 0; i != sLevelCount[0]; ++i) {
            if (sLevelCourseScript[i].course >= COURSE_BOB &&
                sLevelCourseScript[i].course < COURSE_CAKE_END) {
                sLevelCount[1]++;
            }
        }

        // Lists allocation
        sLevelList[0][0] = calloc(sLevelCount[0], sizeof(enum LevelNum));
        sLevelList[0][1] = calloc(sLevelCount[0], sizeof(enum LevelNum));
        sLevelList[1][0] = calloc(sLevelCount[1], sizeof(enum LevelNum));
        sLevelList[1][1] = calloc(sLevelCount[1], sizeof(enum LevelNum));
        sLevelCourses    = calloc(LEVEL_COUNT, sizeof(enum CourseNum));
        sLevelScripts    = calloc(LEVEL_COUNT, sizeof(const LevelScript *));

        // Level list
        for (int i = 0; i != sLevelCount[0]; ++i) {
            sLevelList[0][0][i] = sLevelCourseScript[i].level;
        }

        // Level list ordered by course id
        for (int i = 0, k = 0; i < COURSE_END; ++i) {
            for (int j = 0; j < sLevelCount[0]; ++j) {
                if (sLevelCourseScript[j].course == (enum CourseNum) i) {
                    sLevelList[0][1][k++] = sLevelCourseScript[j].level;
                }
            }
        }

        // Level list (no Castle)
        for (int i = 0, k = 0; i != sLevelCount[0]; ++i) {
            if (sLevelCourseScript[i].course >= COURSE_BOB &&
                sLevelCourseScript[i].course < COURSE_CAKE_END) {
                sLevelList[1][0][k++] = sLevelCourseScript[i].level;
            }
        }

        // Level list ordered by course id (no Castle)
        for (int i = COURSE_BOB, k = 0; i < COURSE_CAKE_END; ++i) {
            for (int j = 0; j < sLevelCount[0]; ++j) {
                if (sLevelCourseScript[j].course == (enum CourseNum) i) {
                    sLevelList[1][1][k++] = sLevelCourseScript[j].level;
                }
            }
        }
        
        // Level courses
        for (int i = 0; i != LEVEL_COUNT; ++i) {
            sLevelCourses[i] = get_course((enum LevelNum) i);
        }

        // Level scripts
        for (int i = 0; i != LEVEL_COUNT; ++i) {
            sLevelScripts[i] = get_script((enum LevelNum) i);
        }

        // Done
        inited = true;
    }
}

//
// Getters
//

int level_get_count(bool noCastle) {
    level_init_data();
    return sLevelCount[noCastle];
}

const enum LevelNum *level_get_list(bool noCastle, bool ordered) {
    level_init_data();
    return sLevelList[noCastle][ordered];
}

enum CourseNum level_get_course(enum LevelNum levelNum) {
    level_init_data();
    return sLevelCourses[levelNum];
}

const LevelScript *level_get_script(enum LevelNum levelNum) {
    level_init_data();
    return sLevelScripts[levelNum];
}

const u8 *level_get_name(enum LevelNum levelNum, bool decaps, bool addCourseNumber) {
    level_init_data();
    static u8 buffer[256];
    memset(buffer, 0xFF, 256);

    // Level name
    set_level_name(buffer, levelNum);

    // Decaps
    if (decaps) str64d(buffer);

    // Course number
    if (addCourseNumber) {
        enum CourseNum courseNum = get_course(levelNum);
        if (courseNum >= COURSE_BOB && courseNum <= COURSE_STAGES_MAX)
            prefix_number(buffer, courseNum);
    }

    return buffer;
}

const u8 *level_get_star_name(enum LevelNum levelNum, int starNum, bool decaps, bool addStarNumber) {
    level_init_data();
    static u8 buffer[256];
    memset(buffer, 0xFF, 256);

    // Star name
    set_act_name(buffer, levelNum, starNum);

    // Decaps
    if (decaps) str64d(buffer);

    // Star number
    if (addStarNumber) {
        enum CourseNum courseNum = get_course(levelNum);
        if (courseNum >= COURSE_BOB && courseNum <= COURSE_STAGES_MAX)
            prefix_number(buffer, starNum);
    }

    return buffer;
}
