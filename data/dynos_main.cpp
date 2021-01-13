#include "dynos.cpp.h"
extern "C" {
#include "sm64.h"
#include "seq_ids.h"
#include "course_table.h"
#include "audio/external.h"
#include "engine/level_script.h"
#include "game/ingame_menu.h"
#include "game/level_update.h"
#include "game/mario.h"
#include "game/mario_step.h"
#include "game/object_list_processor.h"
#include "game/options_menu.h"
#include "game/sound_init.h"
extern char gDialogBoxState;
extern s16  gMenuMode;
extern s16  gCutsceneTimer;
extern void set_play_mode(s16);
}

static s32  sDynosWarpLevelNum    = -1;
static s32  sDynosWarpActNum      = -1;
static bool sDynosWarpToCastle    = false;
static bool sDynosLevelExit       = false;
static u32  sDynosLevelExitAction = 0;

//
// Warps
//

bool DynOS_WarpToLevel(s32 aLevel, s32 aAct) {
    if (DynOS_Level_GetCourse(aLevel) == COURSE_NONE) return false;
    sDynosWarpLevelNum = aLevel;
    sDynosWarpActNum   = aAct;
    sDynosWarpToCastle = false;
    return true;
}

bool DynOS_RestartLevel() {
    s32 aLevel = gCurrLevelNum;
    if (aLevel == LEVEL_BOWSER_1) aLevel = LEVEL_BITDW;
    if (aLevel == LEVEL_BOWSER_2) aLevel = LEVEL_BITFS;
    if (aLevel == LEVEL_BOWSER_3) aLevel = LEVEL_BITS;
    return DynOS_WarpToLevel(aLevel, gCurrActNum);
}

bool DynOS_ExitLevel(s32 aDelay) {
    s32 courseNum = DynOS_Level_GetCourse(gCurrLevelNum);
    if (courseNum == COURSE_NONE) {
        return false;
    }

    // Close the pause menu if it was open
    optmenu_toggle();
    level_set_transition(0, NULL);
    gDialogBoxState = 0;
    gMenuMode = -1;

    // Trigger a special death warp
    gMarioState->invincTimer = -1;
    sDelayedWarpArg = 0;
    sDelayedWarpOp = WARP_OP_DEATH;
    sDelayedWarpTimer = MAX(1, aDelay);
    sSourceWarpNodeId = 0xF1;
    play_transition(WARP_TRANSITION_FADE_INTO_MARIO, sDelayedWarpTimer, 0x00, 0x00, 0x00);
    set_play_mode(0);
    sDynosLevelExit = true;
    return true;
}

bool DynOS_WarpToCastle(s32 aLevel) {
    if (DynOS_Level_GetCourse(aLevel) == COURSE_NONE) return false;
    sDynosWarpLevelNum = aLevel;
    sDynosWarpActNum   = 0;
    sDynosWarpToCastle = true;
    return true;
}

bool DynOS_ReturnToMainMenu() {

    // Close the pause menu if it was open
    optmenu_toggle();
    level_set_transition(0, NULL);
    gDialogBoxState = 0;
    gMenuMode = -1;

    // Warp to the main menu
    fade_into_special_warp(-2, 0);
    return true;
}

//
// Routines
//

struct DynosRoutineParams {
    DynosRoutine mRoutine;
    void *mData;
};

static Array<DynosRoutineParams> &DynOS_GetRoutines(u8 aType) {
    static Array<DynosRoutineParams> sDynosRoutines[3];
    return sDynosRoutines[aType];
}

static void DynOS_ExecuteRoutines(u8 aType) {
    Array<DynosRoutineParams> &_Routines = DynOS_GetRoutines(aType);
    for (auto& _Routine : _Routines) {
        _Routine.mRoutine(_Routine.mData);
    }
}

void DynOS_AddRoutine(u8 aType, DynosRoutine aRoutine, void *aData) {
    Array<DynosRoutineParams> &_Routines = DynOS_GetRoutines(aType);
    for (auto &_Routine : _Routines) {
        if (_Routine.mRoutine == aRoutine) {
            _Routine.mData = aData;
            return;
        }
    }
    _Routines.Add({ aRoutine, aData });
}

//
// Warp
//

static void DynOS_PerformWarp() {
    if (sDynosWarpLevelNum == -1 || sDynosWarpActNum == -1) {
        return;
    }

    // Close the pause menu if it was open
    optmenu_toggle();
    level_set_transition(0, NULL);
    gDialogBoxState = 0;
    gMenuMode = -1;

    // Cancel out every music/sound/sequence
    set_sound_disabled(FALSE);
    sequence_player_unlower(SEQ_PLAYER_LEVEL, 0);
    for (u16 seqid = 0; seqid != SEQ_COUNT; ++seqid) {
    stop_background_music(seqid);
    }
    play_shell_music();
    stop_shell_music();
    stop_cap_music();
    func_80321080(0);

    // Free everything from the current level
    clear_objects();
    clear_area_graph_nodes();
    clear_areas();
    main_pool_pop_state();

    // Reset Mario's state
    gMarioState->healCounter = 0;
    gMarioState->hurtCounter = 0;
    gMarioState->numCoins = 0;
    gMarioState->input = 0;
    gMarioState->controller->buttonPressed = 0;
    gHudDisplay.coins = 0;

    // Set up new level values
    gCurrLevelNum = sDynosWarpLevelNum;
    gCurrCourseNum = DynOS_Level_GetCourse(gCurrLevelNum);
    gSavedCourseNum = gCurrCourseNum;
    gCurrActNum = (gCurrCourseNum <= COURSE_STAGES_MAX ? sDynosWarpActNum : 0);
    gDialogCourseActNum = gCurrActNum;
    gCurrAreaIndex = 1;

    // Load the new level and restart the background music
    level_script_execute((struct LevelCommand *) DynOS_Level_GetScript(gCurrLevelNum));
    play_music(SEQ_PLAYER_LEVEL, gAreas[1].musicParam2, 0);
    if (gCurrLevelNum == LEVEL_BOWSER_1 ||
        gCurrLevelNum == LEVEL_BOWSER_2 ||
        gCurrLevelNum == LEVEL_BOWSER_3) {
        sound_banks_enable(0, 0xFFFF); // Bowser levels sound fix
    }

    // Perform the actual warp
    if (sDynosWarpToCastle) {
        DynOS_ExitLevel(1);
        sDynosWarpToCastle = false;
    } else {
        sWarpDest.type = 2;
        sWarpDest.levelNum = gCurrLevelNum;
        sWarpDest.areaIdx = 1;
        sWarpDest.nodeId = 0x0A;
        sWarpDest.arg = 0;
    }

    // Reset warp values
    sDynosWarpLevelNum = -1;
    sDynosWarpActNum   = -1;
}

//
// Exit
//

bool DynOS_IsLevelExit() {
    if (sDynosLevelExit && (
        gMarioState->action == ACT_FALLING_DEATH_EXIT ||
        gMarioState->action == ACT_SPECIAL_DEATH_EXIT ||
        gMarioState->action == ACT_DEATH_EXIT)) {

        // Mario must be alive
        if (gMarioState->health <= 0xFF) {
#ifdef OMM_DEFINES_H
            omm_fully_heal_mario(gMarioState);
            omm_set_mario_health(gMarioState, gMarioState->health);
#else
            gMarioState->health = 0x880;
#endif
        }

        // Set action
        if (sDynosLevelExitAction == 0) {
            sDynosLevelExitAction = gMarioState->action;
        }

        // Set constants
        u16 _ExitTimer;
        u32 _ExitAnim;
        f32 _ExitVel;
        switch (sDynosLevelExitAction) {
            case ACT_DEATH_EXIT:
                _ExitTimer = 15;
                _ExitAnim = MARIO_ANIM_GENERAL_FALL;
                _ExitVel = -32.f;
                break;

            case ACT_FALLING_DEATH_EXIT:
                _ExitTimer = 0;
                _ExitAnim = MARIO_ANIM_GENERAL_FALL;
                _ExitVel = 0.f;
                break;

            case ACT_SPECIAL_DEATH_EXIT:
                play_sound_if_no_flag(gMarioState, SOUND_MARIO_YAHOO, MARIO_MARIO_SOUND_PLAYED);
                _ExitTimer = 12;
                _ExitAnim = MARIO_ANIM_SINGLE_JUMP;
                _ExitVel = -24.f;
                break;

            case ACT_EXIT_LAND_SAVE_DIALOG:
                _ExitTimer = 0;
                _ExitAnim = MARIO_ANIM_GENERAL_LAND;
                _ExitVel = 0.f;
                break;
        }

        // Hide Mario before starting
        if (gMarioState->actionTimer++ < _ExitTimer) {
            gMarioState->marioObj->header.gfx.node.flags &= ~GRAPH_RENDER_ACTIVE;
            return true;
        }

        // Launch Mario in the air until he lands
        if (sDynosLevelExitAction != ACT_EXIT_LAND_SAVE_DIALOG) {
            mario_set_forward_vel(gMarioState, _ExitVel);
            set_mario_animation(gMarioState, _ExitAnim);
            if (perform_air_step(gMarioState, 0) == AIR_STEP_LANDED) {
                sDynosLevelExitAction = ACT_EXIT_LAND_SAVE_DIALOG;
            }
        }

        // Play the land animation, then return the hand to the player
        else {
            stationary_ground_step(gMarioState);
            play_mario_landing_sound_once(gMarioState, SOUND_ACTION_TERRAIN_LANDING);
            set_mario_animation(gMarioState, _ExitAnim);
            if (is_anim_past_end(gMarioState)) {
                gMarioState->faceAngle[1] += 0x8000;
                set_mario_action(gMarioState, ACT_IDLE, 0);
                gCutsceneTimer = CUTSCENE_STOP;
                gMarioState->area->camera->cutscene = 0;
                sDynosLevelExitAction = 0;
                sDynosLevelExit = false;
            }
        }

        // Update Mario's graphics
        gMarioState->marioObj->header.gfx.node.flags |= GRAPH_RENDER_ACTIVE;
        gMarioState->marioObj->header.gfx.angle[1] += 0x8000;
        return true;
    }
    return false;
}

//
// Init
//

__attribute__((constructor))
void DynOS_Init() {
    DynOS_Opt_Init();
}

//
// Update
//

static bool DynOS_IsLevelEntry() {
    if (!gMarioState) {
        return false;
    }

    // Action, Arg, Timer
    static const s32 sLevelEntryTable[LEVEL_COUNT - 1][3] = {
        { -1,                         -1, -1 }, // LEVEL_UNKNOWN_1
        { -1,                         -1, -1 }, // LEVEL_UNKNOWN_2
        { -1,                         -1, -1 }, // LEVEL_UNKNOWN_3
        { ACT_SPAWN_SPIN_AIRBORNE,    -1, -1 }, // LEVEL_BBH
        { ACT_SPAWN_SPIN_AIRBORNE,    -1, -1 }, // LEVEL_CCM
        { -1,                         -1, -1 }, // LEVEL_CASTLE
        { ACT_SPAWN_SPIN_AIRBORNE,    -1, -1 }, // LEVEL_HMC
        { ACT_SPAWN_SPIN_AIRBORNE,    -1, -1 }, // LEVEL_SSL
        { ACT_SPAWN_SPIN_AIRBORNE,    -1, -1 }, // LEVEL_BOB
        { ACT_SPAWN_SPIN_AIRBORNE,    -1, -1 }, // LEVEL_SL
        { ACT_SPAWN_SPIN_AIRBORNE,    -1, -1 }, // LEVEL_WDW
        { ACT_SPAWN_SPIN_AIRBORNE,    -1, -1 }, // LEVEL_JRB
        { ACT_SPAWN_SPIN_AIRBORNE,    -1, -1 }, // LEVEL_THI
        { ACT_SPAWN_SPIN_AIRBORNE,    -1, -1 }, // LEVEL_TTC
        { ACT_SPAWN_SPIN_AIRBORNE,    -1, -1 }, // LEVEL_RR
        { -1,                         -1, -1 }, // LEVEL_CASTLE_GROUNDS
        { ACT_SPAWN_NO_SPIN_AIRBORNE, -1, -1 }, // LEVEL_BITDW
        { ACT_SPAWN_NO_SPIN_AIRBORNE, -1, -1 }, // LEVEL_VCUTM
        { ACT_SPAWN_NO_SPIN_AIRBORNE, -1, -1 }, // LEVEL_BITFS
        { ACT_WATER_IDLE,              1,  0 }, // LEVEL_SA
        { ACT_SPAWN_NO_SPIN_AIRBORNE, -1, -1 }, // LEVEL_BITS
        { ACT_SPAWN_SPIN_AIRBORNE,    -1, -1 }, // LEVEL_LLL
        { ACT_SPAWN_SPIN_AIRBORNE,    -1, -1 }, // LEVEL_DDD
        { ACT_SPAWN_SPIN_AIRBORNE,    -1, -1 }, // LEVEL_WF
        { -1,                         -1, -1 }, // LEVEL_ENDING
        { -1,                         -1, -1 }, // LEVEL_CASTLE_COURTYARD
        { ACT_SPAWN_NO_SPIN_AIRBORNE, -1, -1 }, // LEVEL_PSS
        { ACT_SPAWN_NO_SPIN_AIRBORNE, -1, -1 }, // LEVEL_COTMC
        { ACT_FLYING,                  2,  0 }, // LEVEL_TOTWC
        { ACT_SPAWN_SPIN_AIRBORNE,    -1, -1 }, // LEVEL_BOWSER_1
        { ACT_SPAWN_NO_SPIN_AIRBORNE, -1, -1 }, // LEVEL_WMOTR
        { -1,                         -1, -1 }, // LEVEL_UNKNOWN_32
        { ACT_SPAWN_SPIN_AIRBORNE,    -1, -1 }, // LEVEL_BOWSER_2
        { ACT_SPAWN_SPIN_AIRBORNE,    -1, -1 }, // LEVEL_BOWSER_3
        { -1,                         -1, -1 }, // LEVEL_UNKNOWN_35
        { ACT_SPAWN_SPIN_AIRBORNE,    -1, -1 }, // LEVEL_TTM
        { -1,                         -1, -1 }, // LEVEL_UNKNOWN_37
        { -1,                         -1, -1 }, // LEVEL_UNKNOWN_38
    };

    s32 _Action = sLevelEntryTable[gCurrLevelNum - 1][0];
    s32 _ActionArg = sLevelEntryTable[gCurrLevelNum - 1][1];
    s32 _ActionTimer = sLevelEntryTable[gCurrLevelNum - 1][2];
    bool _LevelEntry =
        (_Action      != -1 && gMarioState->action        == (u32) _Action) &&
        (_ActionArg   == -1 || gMarioState->actionArg     == (u32) _ActionArg) &&
        (_ActionTimer == -1 || gMarioState->actionTimer++ == (u32) _ActionTimer);

    static bool sWasLevelEntry = false;
    bool _Result = (_LevelEntry && !sWasLevelEntry);
    sWasLevelEntry = _LevelEntry;
    return _Result;
}

void DynOS_UpdateOpt(void *aPad) {
    DynOS_PerformWarp();
    DynOS_Opt_Update((OSContPad *) aPad);
    DynOS_ExecuteRoutines(DYNOS_ROUTINE_OPT_UPDATE);
    if (DynOS_IsLevelEntry()) {
        DynOS_ExecuteRoutines(DYNOS_ROUTINE_LEVEL_START);
    }
}

void DynOS_UpdateGfx() {
    DynOS_Gfx_Update();
    DynOS_ExecuteRoutines(DYNOS_ROUTINE_GFX_UPDATE);
}
