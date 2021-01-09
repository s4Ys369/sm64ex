#include <stdio.h>
#include <dirent.h>
#include <SDL2/SDL.h>
#include "controller/controller_keyboard.h"
#include "controller/controller_sdl.h"
#include "game/object_list_processor.h"
#include "game/spawn_object.h"
#include "game/sound_init.h"
#include "engine/level_script.h"
#include "fs/fs.h"
#include "levels.h"
#include "utils.h"
#include "configfile.h"
#include "gfx_dimensions.h"
#ifdef RENDER96_2_0
#include "text/text-loader.h"
#include "text/txtconv.h"
#endif

static void *make_copy(const void *p, int s) {
    void *q = calloc(s, 1);
    memcpy(q, p, s);
    return q;
}

//
// String utilities
//

static const char *string_to_configname(const char *str) {
    if (str == NULL || strcmp(str, "NULL") == 0 || strcmp(str, "null") == 0) {
        return NULL;
    }
    char *dstr = calloc(strlen(str) + 1, sizeof(char));
    char *pstr = dstr;
    for (; *str != 0; str++, pstr++) {
        if ((*str >= '0' && *str <= '9') || (*str >= 'A' && *str <= 'Z') || (*str >= 'a' && *str <= 'z')) {
            *pstr = *str;
        } else {
            *pstr = '_';
        }
    }
    *pstr = 0;
    return dstr;
}

static int string_to_int(const char *str) {
    int x = 0;
    if (strlen(str) > 2 && str[0] == '0' && str[1] == 'x') {
        sscanf(str + 2, "%X", &x);
    } else {
        sscanf(str, "%d", &x);
    }
    return x;
}

//
// Action list
//

typedef bool (*DynosActionFunction)(const char *);
struct DynosAction {
    const char *str;
    DynosActionFunction action;
};

// "Construct On First Use" aka COFU
static DA *dynos_get_action_list() {
    static DA sDynosActions = da_type(struct DynosAction);
    return &sDynosActions;
}

static DynosActionFunction dynos_get_action(const char *funcName) {
    DA *dynosActions = dynos_get_action_list();
    for (int i = 0; i != dynosActions->count; ++i) {
        if (strcmp(da_getp(*dynosActions, i, struct DynosAction)->str, funcName) == 0) {
            return da_getp(*dynosActions, i, struct DynosAction)->action;
        }
    }
    return NULL;
}

void dynos_add_action(const char *funcName, bool (*funcPtr)(const char *), bool overwrite) {
    DA *dynosActions = dynos_get_action_list();
    for (int i = 0; i != dynosActions->count; ++i) {
        if (strcmp(da_getp(*dynosActions, i, struct DynosAction)->str, funcName) == 0) {
            if (overwrite) da_getp(*dynosActions, i, struct DynosAction)->action = funcPtr;
            return;
        }
    }
    struct DynosAction dynosAction = { make_copy(funcName, strlen(funcName) + 1), funcPtr };
    da_add(*dynosActions, dynosAction);
}

//
// Utils
//

static bool dynos_is_txt_file(const char *filename, unsigned int length) {
    return
        length >= 4 &&
        filename[length - 4] == '.' &&
        filename[length - 3] == 't' &&
        filename[length - 2] == 'x' &&
        filename[length - 1] == 't';
}

//
// Tokenizer
//

#define DYNOS_STR_TOKENS_MAX_LENGTH 64
#define DYNOS_STR_TOKENS_MAX_COUNT  128
#define DYNOS_LINE_MAX_LENGTH       (DYNOS_STR_TOKENS_MAX_LENGTH * DYNOS_STR_TOKENS_MAX_COUNT)

struct StrTokens {
    char tokens[DYNOS_STR_TOKENS_MAX_COUNT][DYNOS_STR_TOKENS_MAX_LENGTH];
    unsigned int count;
};

static struct StrTokens dynos_split_string(const char *str) {
    struct StrTokens strtk = { .count = 0 };
    bool treatSpacesAsChar = false;
    for (int l = 0;; str++) {
        char c = *str;
        if (c == 0 || (c == ' ' && !treatSpacesAsChar) || c == '\t' || c == '\r' || c == '\n' || c == '#') {
            if (l > 0) {
                strtk.tokens[strtk.count][l] = 0;
                strtk.count++;
                if (strtk.count == DYNOS_STR_TOKENS_MAX_COUNT) {
                    break;
                }
                l = 0;
            }
            if (c == 0 || c == '#') {
                break;
            }
        } else if (c == '\"') {
            treatSpacesAsChar = !treatSpacesAsChar;
        } else if (l < (DYNOS_STR_TOKENS_MAX_LENGTH - 1)) {
            strtk.tokens[strtk.count][l] = c;
            l++;
        }
    }
    return strtk;
}

//
// Types and data
//

enum DynosOptType {
    DOPT_TOGGLE,
    DOPT_CHOICE,
    DOPT_SCROLL,
    DOPT_BIND,
    DOPT_BUTTON,
    DOPT_SUBMENU,

    // These ones are used by the Warp to Level built-in submenu
    DOPT_CHOICELEVEL,
    DOPT_CHOICESTAR,
};

struct DynosOption {
    const char *name;
    const char *configName; // Name used in sm64config.txt
    const unsigned char *label;
    const unsigned char *label2; // Full caps label, displayed with colored font
    struct DynosOption *prev;
    struct DynosOption *next;
    struct DynosOption *parent;
    bool dynos; // true from create, false from convert

    enum DynosOptType type;
    union {

        // TOGGLE
        struct Toggle {
            bool *ptog;
        } toggle;

        // CHOICE
        struct Choice {
            const unsigned char **choices;
            int count;
            int *pindex;
        } choice;

        // SCROLL
        struct Scroll {
            int min;
            int max;
            int step;
            int *pvalue;
        } scroll;

        // BIND
        struct Bind {
            unsigned int mask;
            unsigned int *pbinds;
            int index;
        } bind;

        // BUTTON
        struct Button {
            const char *funcName;
        } button;
        
        // SUBMENU
        struct Submenu {
            struct DynosOption *child;
            bool empty;
        } submenu;
    };
};

static struct DynosOption *sPrevOpt = NULL;
static struct DynosOption *sDynosMenu = NULL;
static struct DynosOption *sOptionsMenu = NULL;
static const unsigned char *sDynosTextDynosMenu;
static const unsigned char *sDynosTextA;
static const unsigned char *sDynosTextOpenLeft;
static const unsigned char *sDynosTextCloseLeft;
#ifndef RENDER96_2_0
static const unsigned char *sDynosTextOptionsMenu;
static const unsigned char *sDynosTextDisabled;
static const unsigned char *sDynosTextEnabled;
static const unsigned char *sDynosTextNone;
static const unsigned char *sDynosTextDotDotDot;
static const unsigned char *sDynosTextOpenRight;
static const unsigned char *sDynosTextCloseRight;
#else
#define sDynosTextOptionsMenu r96str("TEXT_OPT_OPTIONS", false)
#define sDynosTextDisabled    r96str("TEXT_OPT_DISABLED", true)
#define sDynosTextEnabled     r96str("TEXT_OPT_ENABLED", true)
#define sDynosTextNone        r96str("TEXT_OPT_UNBOUND", false)
#define sDynosTextDotDotDot   r96str("TEXT_OPT_PRESSKEY", false)
#define sDynosTextOpenRight   r96str("TEXT_OPT_BUTTON1", true)
#define sDynosTextCloseRight  r96str("TEXT_OPT_BUTTON2", true)
#endif

//
// Constructors
//

static struct DynosOption *dynos_add_option(const char *name, const char *configName, const char *label, const char *label2) {
    struct DynosOption *opt = calloc(1, sizeof(struct DynosOption));
    opt->name       = make_copy(name, strlen(name) + 1);
    opt->configName = string_to_configname(configName);
    opt->label      = str64h(label);
    opt->label2     = str64h(label2);
    opt->dynos      = true;
    if (sPrevOpt == NULL) { // The very first option
        opt->prev   = NULL;
        opt->next   = NULL;
        opt->parent = NULL;
        sDynosMenu  = opt;
    } else {
    if (sPrevOpt->type == DOPT_SUBMENU && sPrevOpt->submenu.empty) { // First option of a sub-menu
        opt->prev   = NULL;
        opt->next   = NULL;
        opt->parent = sPrevOpt;
        sPrevOpt->submenu.child = opt;
        sPrevOpt->submenu.empty = false;
    } else {
        opt->prev   = sPrevOpt;
        opt->next   = NULL;
        opt->parent = sPrevOpt->parent;
        sPrevOpt->next = opt;
    }
    }
    sPrevOpt = opt;
    return opt;
}

static void dynos_end_submenu() {
    if (sPrevOpt) {
        if (sPrevOpt->type == DOPT_SUBMENU && sPrevOpt->submenu.empty) { // ENDMENU command following a SUBMENU command
            sPrevOpt->submenu.empty = false;
        } else {
            sPrevOpt = sPrevOpt->parent;
        }
    }
}

static void dynos_create_submenu(const char *name, const char *label, const char *label2) {
    struct DynosOption *opt = dynos_add_option(name, NULL, label, label2);
    opt->type               = DOPT_SUBMENU;
    opt->submenu.child      = NULL;
    opt->submenu.empty      = true;
}

static void dynos_create_toggle(const char *name, const char *configName, const char *label, int initValue) {
    struct DynosOption *opt = dynos_add_option(name, configName, label, label);
    opt->type               = DOPT_TOGGLE;
    opt->toggle.ptog        = calloc(1, sizeof(bool));
    *opt->toggle.ptog       = (unsigned char) initValue;
}

static void dynos_create_scroll(const char *name, const char *configName, const char *label, int min, int max, int step, int initValue) {
    struct DynosOption *opt = dynos_add_option(name, configName, label, label);
    opt->type               = DOPT_SCROLL;
    opt->scroll.min         = min;
    opt->scroll.max         = max;
    opt->scroll.step        = step;
    opt->scroll.pvalue      = calloc(1, sizeof(int));
    *opt->scroll.pvalue     = initValue;
}

static void dynos_create_choice(const char *name, const char *configName, const char *label, const char **choices, int count, int initValue) {
    struct DynosOption *opt = dynos_add_option(name, configName, label, label);
    opt->type               = DOPT_CHOICE;
    opt->choice.choices     = calloc(count, sizeof(const unsigned char *));
    for (int i = 0; i != count; ++i) {
    opt->choice.choices[i]  = str64h(choices[i]);
    }
    opt->choice.count       = count;
    opt->choice.pindex      = calloc(1, sizeof(int));
    *opt->choice.pindex     = initValue;
}

static void dynos_create_button(const char *name, const char *label, const char *funcName) {
    struct DynosOption *opt = dynos_add_option(name, NULL, label, label);
    opt->type               = DOPT_BUTTON;
    opt->button.funcName    = make_copy(funcName, strlen(funcName) + 1);
}

static void dynos_create_bind(const char *name, const char *configName, const char *label, unsigned int mask, unsigned int *binds) {
    struct DynosOption *opt = dynos_add_option(name, configName, label, label);
    opt->type               = DOPT_BIND;
    opt->bind.mask          = mask;
    opt->bind.pbinds        = calloc(MAX_BINDS, sizeof(unsigned int));
    for (int i = 0; i != MAX_BINDS; ++i) {
    opt->bind.pbinds[i]     = binds[i];
    }
    opt->bind.index         = 0;
}

static void dynos_read_file(const char *folder, const char *filename) {

    // Open file
    char fullname[SYS_MAX_PATH];
    snprintf(fullname, SYS_MAX_PATH, "%s/%s", folder, filename);
    FILE *f = fopen(fullname, "rt");
    if (f == NULL) {
        return;
    }

    // Read file and create options
    char buffer[DYNOS_LINE_MAX_LENGTH];
    while (fgets(buffer, DYNOS_LINE_MAX_LENGTH, f) != NULL) {
        struct StrTokens strtk = dynos_split_string(buffer);

        // Empty line
        if (strtk.count == 0) {
            continue;
        }

        // SUBMENU [Name] [Label]
        if (strcmp(strtk.tokens[0], "SUBMENU") == 0 && strtk.count >= 4) {
            dynos_create_submenu(strtk.tokens[1], strtk.tokens[2], strtk.tokens[3]);
            continue;
        }

        // TOGGLE  [Name] [Label] [ConfigName] [InitialValue]
        if (strcmp(strtk.tokens[0], "TOGGLE") == 0 && strtk.count >= 5) {
            dynos_create_toggle(strtk.tokens[1], strtk.tokens[3], strtk.tokens[2], string_to_int(strtk.tokens[4]));
            continue;
        }

        // SCROLL  [Name] [Label] [ConfigName] [InitialValue] [Min] [Max] [Step]
        if (strcmp(strtk.tokens[0], "SCROLL") == 0 && strtk.count >= 8) {
            dynos_create_scroll(strtk.tokens[1], strtk.tokens[3], strtk.tokens[2], string_to_int(strtk.tokens[5]), string_to_int(strtk.tokens[6]), string_to_int(strtk.tokens[7]), string_to_int(strtk.tokens[4]));
            continue;
        }

        // CHOICE  [Name] [Label] [ConfigName] [InitialValue] [ChoiceStrings...]
        if (strcmp(strtk.tokens[0], "CHOICE") == 0 && strtk.count >= 6) {
            const char *choices[DYNOS_STR_TOKENS_MAX_COUNT];
            for (unsigned int i = 0; i != strtk.count - 5; ++i) {
                choices[i] = &strtk.tokens[5 + i][0];
            }
            dynos_create_choice(strtk.tokens[1], strtk.tokens[3], strtk.tokens[2], choices, strtk.count - 5, string_to_int(strtk.tokens[4]));
            continue;
        }

        // BUTTON  [Name] [Label] [FuncName]
        if (strcmp(strtk.tokens[0], "BUTTON") == 0 && strtk.count >= 4) {
            dynos_create_button(strtk.tokens[1], strtk.tokens[2], strtk.tokens[3]);
            continue;
        }

        // BIND    [Name] [Label] [ConfigName] [Mask] [DefaultValues]
        if (strcmp(strtk.tokens[0], "BIND") == 0 && strtk.count >= 5 + MAX_BINDS) {
            unsigned int binds[MAX_BINDS];
            for (int i = 0; i != MAX_BINDS; ++i) {
                binds[i] = string_to_int(strtk.tokens[5 + i]);
            }
            dynos_create_bind(strtk.tokens[1], strtk.tokens[3], strtk.tokens[2], string_to_int(strtk.tokens[4]), binds);
            continue;
        }

        // ENDMENU
        if (strcmp(strtk.tokens[0], "ENDMENU") == 0) {
            dynos_end_submenu();
            continue;
        }
    }
    fclose(f);
}

static void dynos_load_options() {
    char optionsFolder[SYS_MAX_PATH];
    snprintf(optionsFolder, SYS_MAX_PATH, "%s/%s", sys_exe_path(), FS_BASEDIR);
    DIR *dir = opendir(optionsFolder);
    sPrevOpt = NULL;
    if (dir) {
        struct dirent *ent = NULL;
        while ((ent = readdir(dir)) != NULL) {
            if (dynos_is_txt_file(ent->d_name, strlen(ent->d_name))) {
                dynos_read_file(optionsFolder, ent->d_name);
            }
        }
        closedir(dir);
    }
}

//
// Vanilla options menu
//

// Not my problem
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsizeof-pointer-div"
#pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"
#pragma GCC diagnostic ignored "-Wpointer-sign"
#pragma GCC diagnostic ignored "-Wsign-compare"
#define optmenu_toggle optmenu_toggle_unused
#define optmenu_draw optmenu_draw_unused
#define optmenu_draw_prompt optmenu_draw_prompt_unused
#define optmenu_check_buttons optmenu_check_buttons_unused
#define optmenu_open optmenu_open_unused
#define DYNOS_INL
#include "game/options_menu.c"
#undef DYNOS_INL
#undef optmenu_toggle
#undef optmenu_draw
#undef optmenu_draw_prompt
#undef optmenu_check_buttons
#undef optmenu_open
#pragma GCC diagnostic pop
// Now, that's my problem

//
// Vanilla actions
//

typedef void (*VanillaActionFunction)(struct Option *, int);
struct VanillaAction {
    const char *str;
    VanillaActionFunction action;
};

// "Construct On First Use" aka COFU
static DA *dynos_get_vanilla_action_list() {
    static DA sVanillaActions = da_type(struct VanillaAction);
    return &sVanillaActions;
}

static VanillaActionFunction dynos_get_vanilla_action(const char *name) {
    DA *vanillaActions = dynos_get_vanilla_action_list();
    for (int i = 0; i != vanillaActions->count; ++i) {
        if (strcmp(da_getp(*vanillaActions, i, struct VanillaAction)->str, name) == 0) {
            return da_getp(*vanillaActions, i, struct VanillaAction)->action;
        }
    }
    return NULL;
}

static void dynos_add_vanilla_action(const char *name, VanillaActionFunction func) {
    DA *vanillaActions = dynos_get_vanilla_action_list();
    for (int i = 0; i != vanillaActions->count; ++i) {
        if (strcmp(da_getp(*vanillaActions, i, struct VanillaAction)->str, name) == 0) {
            return;
        }
    }
    struct VanillaAction vanillaAction = { name, func };
    da_add(*vanillaActions, vanillaAction);
}

static bool dynos_call_vanilla_action(const char *optName) {
    VanillaActionFunction func = dynos_get_vanilla_action(optName);
    if (func) {
        func(NULL, 0);
        return true;
    }
    return false;
}

//
// Convert classic options menu into DynOS menu
//

static struct DynosOption *dynos_convert_option(const unsigned char *label, const unsigned char *label2) {
    struct DynosOption *opt = calloc(1, sizeof(struct DynosOption));
    opt->name        = (const char *) label;
    opt->configName  = NULL;
#ifdef RENDER96_2_0
    opt->label       = label;
#else
    opt->label       = str64d(make_copy(label, str64l(label) + 1));
#endif
    opt->label2      = label2;
    opt->dynos       = false;
    if (sPrevOpt == NULL) { // The very first option
        opt->prev    = NULL;
        opt->next    = NULL;
        opt->parent  = NULL;
        sOptionsMenu = opt;
    } else {
    if (sPrevOpt->type == DOPT_SUBMENU && sPrevOpt->submenu.empty) { // First option of a sub-menu
        opt->prev    = NULL;
        opt->next    = NULL;
        opt->parent  = sPrevOpt;
        sPrevOpt->submenu.child = opt;
        sPrevOpt->submenu.empty = false;
    } else {
        opt->prev    = sPrevOpt;
        opt->next    = NULL;
        opt->parent  = sPrevOpt->parent;
        sPrevOpt->next = opt;
    }
    }
    sPrevOpt = opt;
    return opt;
}

static void dynos_convert_submenu(const unsigned char *label, const unsigned char *label2) {
    struct DynosOption *opt = dynos_convert_option(label, label2);
    opt->type               = DOPT_SUBMENU;
    opt->submenu.child      = NULL;
    opt->submenu.empty      = true;
}

static void dynos_convert_toggle(const unsigned char *label, bool *bval) {
    struct DynosOption *opt = dynos_convert_option(label, label);
    opt->type               = DOPT_TOGGLE;
    opt->toggle.ptog        = (bool *) bval;
}

static void dynos_convert_scroll(const unsigned char *label, int min, int max, int step, unsigned int *uval) {
    struct DynosOption *opt = dynos_convert_option(label, label);
    opt->type               = DOPT_SCROLL;
    opt->scroll.min         = min;
    opt->scroll.max         = max;
    opt->scroll.step        = step;
    opt->scroll.pvalue      = (int *) uval;
}

static void dynos_convert_choice(const unsigned char *label, const unsigned char **choices, int count, unsigned int *uval) {
    struct DynosOption *opt = dynos_convert_option(label, label);
    opt->type               = DOPT_CHOICE;
    opt->choice.choices     = choices;
    opt->choice.count       = count;
    opt->choice.pindex      = (int *) uval;
}

static void dynos_convert_button(const unsigned char *label, VanillaActionFunction action) {
    struct DynosOption *opt = dynos_convert_option(label, label);
    opt->type               = DOPT_BUTTON;
    opt->button.funcName    = make_copy("dynos_call_vanilla_action", 26);
    dynos_add_vanilla_action(opt->name, action);
}

static void dynos_convert_bind(const unsigned char *label, unsigned int *binds) {
    struct DynosOption *opt = dynos_convert_option(label, label);
    opt->type               = DOPT_BIND;
    opt->bind.mask          = 0;
    opt->bind.pbinds        = binds;
    opt->bind.index         = 0;
}

static void dynos_convert_submenu_options(struct SubMenu *submenu) {
    for (int i = 0; i != submenu->numOpts; ++i) {
        struct Option *opt = &submenu->opts[i];
        switch (opt->type) {
            case OPT_TOGGLE:
                dynos_convert_toggle(opt->label, opt->bval);
                break;

            case OPT_CHOICE:
                dynos_convert_choice(opt->label, opt->choices, opt->numChoices, opt->uval);
                break;

            case OPT_SCROLL:
                dynos_convert_scroll(opt->label, opt->scrMin, opt->scrMax, opt->scrStep, opt->uval);
                break;

            case OPT_SUBMENU:
                dynos_convert_submenu(opt->label, opt->nextMenu->label);
                dynos_convert_submenu_options(opt->nextMenu);
                dynos_end_submenu();
                break;

            case OPT_BIND:
                dynos_convert_bind(opt->label, opt->uval);
                break;

            case OPT_BUTTON:
                dynos_convert_button(opt->label, opt->actionFn);
                break;

            default:
                break;
        }
    }
}

static void dynos_convert_options_menu() {
    sPrevOpt = NULL;
    dynos_convert_submenu_options(&menuMain);
    dynos_add_action("dynos_call_vanilla_action", dynos_call_vanilla_action, true);
}

//
// Loop through DynosOptions
//

typedef bool (*DynosLoopFunc)(struct DynosOption *, void *);
static struct DynosOption *dynos_loop(struct DynosOption *opt, DynosLoopFunc func, void *data) {
    while (opt) {
        if (opt->type == DOPT_SUBMENU) {
            struct DynosOption *res = dynos_loop(opt->submenu.child, func, data);
            if (res) {
                return res;
            }
        } else {
            if (func(opt, data)) {
                return opt;
            }
        }
        opt = opt->next;
    }
    return NULL;
}

//
// Controllers
//

static bool dynos_controller_is_key_down(int i, int k) {

    // Keyboard
    if (i == 0 && k >= 0 && k < SDL_NUM_SCANCODES) {
        return SDL_GetKeyboardState(NULL)[k];
    }

    // Game Controller
    else if (k >= 0x1000) {
            
        // Button
        int b = (k - 0x1000);
        if (b < SDL_CONTROLLER_BUTTON_MAX) {
            return SDL_GameControllerGetButton(SDL_GameControllerOpen(i - 1), b);
        }

        // Axis
        int a = (k - 0x1000 - SDL_CONTROLLER_BUTTON_MAX);
        if (a < SDL_CONTROLLER_AXIS_MAX * 2) {
            int axis = SDL_GameControllerGetAxis(SDL_GameControllerOpen(i - 1), a / 2);
            if (a & 1) return (axis < SDL_JOYSTICK_AXIS_MIN / 2);
            else       return (axis > SDL_JOYSTICK_AXIS_MAX / 2);
        }
    }

    // Invalid
    return false;
}

#define MAX_CONTS 8
static bool dynos_controller_update(struct DynosOption *opt, void *data) {
    if (opt->type == DOPT_BIND) {
        OSContPad *pad = (OSContPad *) data;
        for (int i = 0; i < MAX_CONTS; ++i)
        for (int j = 0; j < MAX_BINDS; ++j) {
            pad->button |= opt->bind.mask * dynos_controller_is_key_down(i, opt->bind.pbinds[j]);
        }
    }
    return false;
}

#define MAX_GKEYS (SDL_CONTROLLER_BUTTON_MAX + SDL_CONTROLLER_AXIS_MAX * 2)
static int sBindingState = 0; // 0 = No bind, 1 = Wait for all keys released, 2 = Return first pressed key
static int dynos_controller_get_key_pressed() {
    
    // Keyboard
    for (int k = 0; k < SDL_NUM_SCANCODES; ++k) {
        if (dynos_controller_is_key_down(0, k)) {
            if (sBindingState == 1) return VK_INVALID;
            return k;
        }
    }

    // Game Controller
    for (int i = 1; i < MAX_CONTS; ++i)
    for (int k = 0; k < MAX_GKEYS; ++k) {
        if (dynos_controller_is_key_down(i, k + 0x1000)) {
            if (sBindingState == 1) return VK_INVALID;
            return k + 0x1000;
        }
    }
    
    // No key
    sBindingState = 2;
    return VK_INVALID;
}

//
// Config
//

static const char *sDynosConfigFilename = "DynOSconfig.cfg";

static bool dynos_get_option_config(struct DynosOption *opt, void *data) {
    unsigned char type = *(unsigned char *) ((void **) data)[0];
    const char *name = (const char *) ((void **) data)[1];
    return (opt->configName != NULL && strcmp(opt->configName, name) == 0 && opt->type == type);
}

static bool dynos_read_config_type_and_name(FILE *f, unsigned char *type, char *name) {
    
    // Read type
    if (fread(type, sizeof(unsigned char), 1, f) != 1) {
        return false;
    }

    // Read string length
    unsigned char len = 0;
    if (fread(&len, sizeof(unsigned char), 1, f) != 1) {
        return false;
    }

    // Read config name
    if (fread(name, sizeof(char), len, f) != len) {
        return false;
    }
    name[len] = 0;
    return true;
}

static void dynos_load_config() {
    char filename[SYS_MAX_PATH];
    snprintf(filename, SYS_MAX_PATH, "%s/%s", sys_user_path(), sDynosConfigFilename);
    FILE *f = fopen(filename, "rb");
    if (f == NULL) return;
    while (true) {
        unsigned char type;
        char name[DYNOS_STR_TOKENS_MAX_LENGTH];
        if (!dynos_read_config_type_and_name(f, &type, name)) {
            break;
        }
    
        struct DynosOption *opt = dynos_loop(sDynosMenu, dynos_get_option_config, (void **) (void *[]){ &type, name });
        if (opt != NULL) {
            switch (opt->type) {
                case DOPT_TOGGLE:
                    fread(opt->toggle.ptog, sizeof(bool), 1, f);
                    break;

                case DOPT_CHOICE:
                    fread(opt->choice.pindex, sizeof(int), 1, f);
                    break;

                case DOPT_SCROLL:
                    fread(opt->scroll.pvalue, sizeof(int), 1, f);
                    break;

                case DOPT_BIND:
                    fread(opt->bind.pbinds, sizeof(unsigned int), MAX_BINDS, f);
                    break;

                default:
                    break;
            }
        }
    }
    fclose(f);
}

static void dynos_write_config_type_and_name(FILE *f, unsigned char type, const char *name) {
    unsigned char len = (unsigned char) strlen(name);
    fwrite(&type, sizeof(unsigned char), 1, f);
    fwrite(&len, sizeof(unsigned char), 1, f);
    fwrite(name, sizeof(char), len, f);
}

static bool dynos_save_option_config(struct DynosOption *opt, void *data) {
    if (opt->configName != NULL) {
        FILE *f = (FILE *) data;
        switch (opt->type) {
            case DOPT_TOGGLE:
                dynos_write_config_type_and_name(f, DOPT_TOGGLE, opt->configName);
                fwrite(opt->toggle.ptog, sizeof(bool), 1, f);
                break;

            case DOPT_CHOICE:
                dynos_write_config_type_and_name(f, DOPT_CHOICE, opt->configName);
                fwrite(opt->choice.pindex, sizeof(int), 1, f);
                break;

            case DOPT_SCROLL:
                dynos_write_config_type_and_name(f, DOPT_SCROLL, opt->configName);
                fwrite(opt->scroll.pvalue, sizeof(int), 1, f);
                break;

            case DOPT_BIND:
                dynos_write_config_type_and_name(f, DOPT_BIND, opt->configName);
                fwrite(opt->bind.pbinds, sizeof(unsigned int), MAX_BINDS, f);
                break;

            default:
                break;
        }
    }
    return 0;
}

static void dynos_save_config() {
    char filename[SYS_MAX_PATH];
    snprintf(filename, SYS_MAX_PATH, "%s/%s", sys_user_path(), sDynosConfigFilename);
    FILE *f = fopen(filename, "wb");
    if (f == NULL) return;
    dynos_loop(sDynosMenu, dynos_save_option_config, (void *) f);
    fclose(f);
}

//
// Get/Set values
//

static bool dynos_get(struct DynosOption *opt, void *data) {
    return (strcmp(opt->name, (const char *) data) == 0);
}

int dynos_get_value(const char *name) {
    struct DynosOption *opt = dynos_loop(sDynosMenu, dynos_get, (void *) name);
    if (opt) {
        switch (opt->type) {
            case DOPT_TOGGLE:      return (int) *opt->toggle.ptog;
            case DOPT_CHOICE:      return *opt->choice.pindex;
            case DOPT_CHOICELEVEL: return *opt->choice.pindex;
            case DOPT_CHOICESTAR:  return *opt->choice.pindex;
            case DOPT_SCROLL:      return *opt->scroll.pvalue;
            default:               break;
        }
    }
    return 0;
}

void dynos_set_value(const char *name, int value) {
    struct DynosOption *opt = dynos_loop(sDynosMenu, dynos_get, (void *) name);
    if (opt) {
        switch (opt->type) {
            case DOPT_TOGGLE:      *opt->toggle.ptog = (bool) value; break;
            case DOPT_CHOICE:      *opt->choice.pindex = value; break;
            case DOPT_CHOICELEVEL: *opt->choice.pindex = value; break;
            case DOPT_CHOICESTAR:  *opt->choice.pindex = value; break;
            case DOPT_SCROLL:      *opt->scroll.pvalue = value; break;
            default:               break;
        }
    }
}

//
// Render
//

static struct DynosOption *sCurrentMenu = NULL;
static struct DynosOption *sCurrentOpt = NULL;

static int dynos_get_render_string_length(const unsigned char *str64) {
    int length = 0;
    for (; *str64 != DIALOG_CHAR_TERMINATOR; ++str64) {
        length += str64w(*str64);
    }
    return length;
}

static void dynos_render_string(const unsigned char *str64, int x, int y) {
    create_dl_translation_matrix(MENU_MTX_PUSH, x, y, 0);
    for (; *str64 != DIALOG_CHAR_TERMINATOR; ++str64) {
        if (*str64 != DIALOG_CHAR_SPACE) {
            if (*str64 == 253) { // underscore
                create_dl_translation_matrix(MENU_MTX_NOPUSH, -1, -5, 0);
                void **fontLUT = (void **) segmented_to_virtual(main_font_lut);
                void *packedTexture = segmented_to_virtual(fontLUT[159]);
                gDPPipeSync(gDisplayListHead++);
                gDPSetTextureImage(gDisplayListHead++, G_IM_FMT_IA, G_IM_SIZ_16b, 1, VIRTUAL_TO_PHYSICAL(packedTexture));
                gSPDisplayList(gDisplayListHead++, dl_ia_text_tex_settings);
                create_dl_translation_matrix(MENU_MTX_NOPUSH, 0, +5, 0);
            } else {
                void **fontLUT = (void **) segmented_to_virtual(main_font_lut);
                void *packedTexture = segmented_to_virtual(fontLUT[*str64]);
                gDPPipeSync(gDisplayListHead++);
                gDPSetTextureImage(gDisplayListHead++, G_IM_FMT_IA, G_IM_SIZ_16b, 1, VIRTUAL_TO_PHYSICAL(packedTexture));
                gSPDisplayList(gDisplayListHead++, dl_ia_text_tex_settings);
            }
        }
        create_dl_translation_matrix(MENU_MTX_NOPUSH, str64w(*str64), 0, 0);
    }
    gSPPopMatrix(gDisplayListHead++, G_MTX_MODELVIEW);
}

static void dynos_print_string(const unsigned char *str64, int x, int y, unsigned int rgbaFront, unsigned int rgbaBack, bool alignLeft) {
    gSPDisplayList(gDisplayListHead++, dl_ia_text_begin);
    if ((rgbaBack & 0xFF) != 0) {
        gDPSetEnvColor(gDisplayListHead++, ((rgbaBack >> 24) & 0xFF), ((rgbaBack >> 16) & 0xFF), ((rgbaBack >> 8) & 0xFF), ((rgbaBack >> 0) & 0xFF));
        if (alignLeft) {
            dynos_render_string(str64, GFX_DIMENSIONS_FROM_LEFT_EDGE(x) + 1, y - 1);
        } else {
            dynos_render_string(str64, GFX_DIMENSIONS_FROM_RIGHT_EDGE(x + dynos_get_render_string_length(str64) - 1), y - 1);
        }
    }
    if ((rgbaFront & 0xFF) != 0) {
        gDPSetEnvColor(gDisplayListHead++, ((rgbaFront >> 24) & 0xFF), ((rgbaFront >> 16) & 0xFF), ((rgbaFront >> 8) & 0xFF), ((rgbaFront >> 0) & 0xFF));
        if (alignLeft) {
            dynos_render_string(str64, GFX_DIMENSIONS_FROM_LEFT_EDGE(x), y);
        } else {
            dynos_render_string(str64, GFX_DIMENSIONS_FROM_RIGHT_EDGE(x + dynos_get_render_string_length(str64)), y);
        }
    }
    gSPDisplayList(gDisplayListHead++, dl_ia_text_end);
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, 255);
}

static void dynos_print_box(int x, int y, int w, int h, unsigned int rgbaColor, bool alignLeft) {
    if ((rgbaColor && 0xFF) != 0) {
        Mtx *matrix = (Mtx *) alloc_display_list(sizeof(Mtx));
        if (!matrix) return;
        if (alignLeft) {
            create_dl_translation_matrix(MENU_MTX_PUSH, GFX_DIMENSIONS_FROM_LEFT_EDGE(x), y + h, 0);
        } else {
            create_dl_translation_matrix(MENU_MTX_PUSH, GFX_DIMENSIONS_FROM_RIGHT_EDGE(x + w), y + h, 0);
        }
        guScale(matrix, (float) w / 130.f, (float) h / 80.f, 1.f);
        gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(matrix), G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_NOPUSH);
        gDPSetEnvColor(gDisplayListHead++, ((rgbaColor >> 24) & 0xFF), ((rgbaColor >> 16) & 0xFF), ((rgbaColor >> 8) & 0xFF), ((rgbaColor >> 0) & 0xFF));
        gSPDisplayList(gDisplayListHead++, dl_draw_text_bg_box);
        gSPPopMatrix(gDisplayListHead++, G_MTX_MODELVIEW);
        gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, 255);
    }
}

static inline int dynos_get_label2_x(int x, const unsigned char *str) {
    int len = 0;
    while (*str != GLOBAR_CHAR_TERMINATOR) ++str, ++len;
    return x - len * 6; // stride is 12
}

//
// Options menu
//

#ifdef RENDER96_2_0
static const unsigned char *r96lang(struct DynosOption *opt) {
    static unsigned char buffer[DYNOS_STR_TOKENS_MAX_LENGTH];
    unsigned char *s = getTranslatedText(languages[*opt->choice.pindex]->name);
    memcpy(buffer, s, str64l(s) + 1);
    free(s);
    opt->choice.count = languagesAmount;
    return str64d(buffer);
}

static const unsigned char *r96str(const char *str64, bool decaps) {
    static unsigned char buffer[DYNOS_STR_TOKENS_MAX_LENGTH];
    unsigned char *s = get_key_string((char *) str64);
    memcpy(buffer, s, str64l(s) + 1);
    return (decaps ? str64d(buffer) : buffer);
}

#define get_label(opt)  (opt->dynos ? opt->label : r96str((const char *) opt->label, true))
#define get_label2(opt) (opt->dynos ? opt->label2 : r96str((const char *) opt->label2, false))
#define get_choice(opt) (opt->dynos ? opt->choice.choices[*opt->choice.pindex] : (strcmp((const char *) opt->label, (const char *) optsGameStr[0]) == 0 ? r96lang(opt) : r96str((const char *) opt->choice.choices[*opt->choice.pindex], true)))
#else
#define get_label(opt)  (opt->label)
#define get_label2(opt) (opt->label2)
#define get_choice(opt) (opt->choice.choices[*opt->choice.pindex])
#endif

static int dynos_get_option_count() {
    struct DynosOption *opt = sCurrentOpt;
    while (opt->prev) {
        opt = opt->prev;
    }
    int count = 0;
    while (opt) {
        opt = opt->next;
        count++;
    }
    return count;
}

static int dynos_get_current_option_index() {
    struct DynosOption *opt = sCurrentOpt;
    int index = 0;
    while (opt->prev) {
        opt = opt->prev;
        index++;
    }
    return index;
}

#define PREV(opt) (opt == NULL ? NULL : opt->prev)
#define NEXT(opt) (opt == NULL ? NULL : opt->next)
static struct DynosOption **dynos_get_options_to_draw() {
    static struct DynosOption *sOptionList[13];
    bzero(sOptionList, 13 * sizeof(struct DynosOption *));

    sOptionList[6]  = sCurrentOpt;
    sOptionList[5]  = PREV(sOptionList[6]);
    sOptionList[4]  = PREV(sOptionList[5]);
    sOptionList[3]  = PREV(sOptionList[4]);
    sOptionList[2]  = PREV(sOptionList[3]);
    sOptionList[1]  = PREV(sOptionList[2]);
    sOptionList[0]  = PREV(sOptionList[1]);
    sOptionList[7]  = NEXT(sOptionList[6]);
    sOptionList[8]  = NEXT(sOptionList[7]);
    sOptionList[9]  = NEXT(sOptionList[8]);
    sOptionList[10] = NEXT(sOptionList[9]);
    sOptionList[11] = NEXT(sOptionList[10]);
    sOptionList[12] = NEXT(sOptionList[11]);

    int start = 12, end = 0;
    for (int i = 0; i != 13; ++i) {
        if (sOptionList[i] != NULL) {
            start = MIN(start, i);
            end = MAX(end, i);
        }
    }

    if (end - start < 7) {
        return &sOptionList[start];
    }
    if (end <= 9) {
        return &sOptionList[end - 6];
    }
    if (start >= 3) {
        return &sOptionList[start];
    }
    return &sOptionList[3];
}
#undef PREV
#undef NEXT

#define COLOR_WHITE             0xFFFFFFFF
#define COLOR_BLACK             0x000000FF
#define COLOR_GRAY              0xA0A0A0FF
#define COLOR_DARK_GRAY         0x808080FF
#define COLOR_SELECT            0x80E0FFFF
#define COLOR_SELECT_BOX        0x00FFFF20
#define COLOR_ENABLED           0x20E020FF
#define COLOR_DISABLED          0xFF2020FF
#define OFFSET_FROM_LEFT_EDGE   (20.f * sqr(GFX_DIMENSIONS_ASPECT_RATIO))
#define OFFSET_FROM_RIGHT_EDGE  (20.f * sqr(GFX_DIMENSIONS_ASPECT_RATIO))
#define SCROLL_BAR_SIZE         ((int) (45.f * GFX_DIMENSIONS_ASPECT_RATIO))

static void dynos_draw_option(struct DynosOption *opt, int y) {
    if (opt == NULL) {
        return;
    }

    // Selected box
    if (opt == sCurrentOpt) {
        unsigned char a = (unsigned char) ((coss(gGlobalTimer * 0x800) + 1.f) * 0x20);
        dynos_print_box(OFFSET_FROM_LEFT_EDGE - 4, y - 2, GFX_DIMENSIONS_FROM_RIGHT_EDGE(OFFSET_FROM_RIGHT_EDGE) - GFX_DIMENSIONS_FROM_LEFT_EDGE(OFFSET_FROM_LEFT_EDGE) + 8, 20, COLOR_SELECT_BOX + a, 1);
    }

    // Label
    if (opt == sCurrentOpt) {
        dynos_print_string(get_label(opt), OFFSET_FROM_LEFT_EDGE, y, COLOR_SELECT, COLOR_BLACK, 1);
    } else {
        dynos_print_string(get_label(opt), OFFSET_FROM_LEFT_EDGE, y, COLOR_WHITE, COLOR_BLACK, 1);
    }

    // Values
    int w;
    switch (opt->type) {
        case DOPT_TOGGLE:
            if (*opt->toggle.ptog) {
                dynos_print_string(sDynosTextEnabled, OFFSET_FROM_RIGHT_EDGE, y, COLOR_ENABLED, COLOR_BLACK, 0);
            } else {
                dynos_print_string(sDynosTextDisabled, OFFSET_FROM_RIGHT_EDGE, y, COLOR_DISABLED, COLOR_BLACK, 0);
            }
            break;

        case DOPT_CHOICE:
            dynos_print_string(get_choice(opt), OFFSET_FROM_RIGHT_EDGE, y, opt == sCurrentOpt ? COLOR_SELECT : COLOR_WHITE, COLOR_BLACK, 0);
            break;

        case DOPT_CHOICELEVEL:
            dynos_print_string(level_get_name(level_get_list(true, true)[*opt->choice.pindex], true, true), OFFSET_FROM_RIGHT_EDGE, y, opt == sCurrentOpt ? COLOR_SELECT : COLOR_WHITE, COLOR_BLACK, 0);
            break;

        case DOPT_CHOICESTAR:
            dynos_print_string(level_get_star_name(level_get_list(true, true)[dynos_get_value("dynos_warp_level")], *opt->choice.pindex + 1, true, true), OFFSET_FROM_RIGHT_EDGE, y, opt == sCurrentOpt ? COLOR_SELECT : COLOR_WHITE, COLOR_BLACK, 0);
            break;

        case DOPT_SCROLL:
            w = (int) (SCROLL_BAR_SIZE * (float) (*opt->scroll.pvalue - opt->scroll.min) / (float) (opt->scroll.max - opt->scroll.min));
            dynos_print_string(str64s("%d", *opt->scroll.pvalue), OFFSET_FROM_RIGHT_EDGE, y, opt == sCurrentOpt ? COLOR_SELECT : COLOR_WHITE, COLOR_BLACK, 0);
            dynos_print_box(OFFSET_FROM_RIGHT_EDGE + 28, y + 4, SCROLL_BAR_SIZE + 2, 8, COLOR_DARK_GRAY, 0);
            dynos_print_box(OFFSET_FROM_RIGHT_EDGE + 29 + SCROLL_BAR_SIZE - w, y + 5, w, 6, opt == sCurrentOpt ? COLOR_SELECT : COLOR_WHITE, 0);
            break;

        case DOPT_BIND:
            for (int i = 0; i != MAX_BINDS; ++i) {
                unsigned int bind = opt->bind.pbinds[i];
                if (opt == sCurrentOpt && i == opt->bind.index) {
                    if (sBindingState != 0) {
                        dynos_print_string(sDynosTextDotDotDot, OFFSET_FROM_RIGHT_EDGE + (2 - i) * 36, y, COLOR_SELECT, COLOR_BLACK, 0);
                    } else if (bind == VK_INVALID) {
                        dynos_print_string(sDynosTextNone, OFFSET_FROM_RIGHT_EDGE + (2 - i) * 36, y, COLOR_SELECT, COLOR_BLACK, 0);
                    } else {
                        dynos_print_string(str64s("%04X", bind), OFFSET_FROM_RIGHT_EDGE + (2 - i) * 36, y, COLOR_SELECT, COLOR_BLACK, 0);
                    }
                } else {
                    if (bind == VK_INVALID) {
                        dynos_print_string(sDynosTextNone, OFFSET_FROM_RIGHT_EDGE + (2 - i) * 36, y, COLOR_GRAY, COLOR_BLACK, 0);
                    } else {
                        dynos_print_string(str64s("%04X", bind), OFFSET_FROM_RIGHT_EDGE + (2 - i) * 36, y, COLOR_WHITE, COLOR_BLACK, 0);
                    }
                }
            }
            break;

        case DOPT_BUTTON:
            break;

        case DOPT_SUBMENU:
            if (opt == sCurrentOpt) {
                dynos_print_string(sDynosTextA, OFFSET_FROM_RIGHT_EDGE, y, COLOR_SELECT, COLOR_BLACK, 0);
            }
            break;
    }
}

static void dynos_draw_menu() {
    if (sCurrentMenu == NULL) {
        return;
    }

    // Colorful label
    const unsigned char *label2 = NULL;
    if (sCurrentOpt->parent) {
        label2 = get_label2(sCurrentOpt->parent);
    } else if (sCurrentMenu == sDynosMenu) {
        label2 = sDynosTextDynosMenu;
    } else {
        label2 = sDynosTextOptionsMenu;
    }
    gSPDisplayList(gDisplayListHead++, dl_rgba16_text_begin);
    gDPSetEnvColor(gDisplayListHead++, 255, 255, 255, 255);
    print_hud_lut_string(HUD_LUT_GLOBAL, dynos_get_label2_x(SCREEN_WIDTH / 2, label2), 40, label2);
    gSPDisplayList(gDisplayListHead++, dl_rgba16_text_end);

    // Display options
    struct DynosOption **optionsToDraw = dynos_get_options_to_draw();
    for (int i = 0; i != 7; ++i) {
        dynos_draw_option(optionsToDraw[i], 156 - 20 * i);
    }

    // Scroll bar
    int optCount = dynos_get_option_count();
    int optIndex = dynos_get_current_option_index();
    if (optCount > 7) {
        int h = (int) (134.f * sqrtf(1.f / (optCount - 6)));
        int y = 37 + (134 - h) * (1.f - MAX(0.f, MIN(1.f, (float)(optIndex - 3) / (float)(optCount - 6))));
        dynos_print_box(OFFSET_FROM_RIGHT_EDGE - 16, 36, 8, 136, COLOR_DARK_GRAY, 0);
        dynos_print_box(OFFSET_FROM_RIGHT_EDGE - 15, y, 6, h, COLOR_WHITE, 0);
    }
}

//
// Processing
//

#define SOUND_DYNOS_SAVED   (SOUND_MENU_MARIO_CASTLE_WARP2  | (0xFF << 8))
#define SOUND_DYNOS_SELECT  (SOUND_MENU_CHANGE_SELECT       | (0xF8 << 8))
#define SOUND_DYNOS_OK      (SOUND_MENU_CHANGE_SELECT       | (0xF8 << 8))
#define SOUND_DYNOS_CANCEL  (SOUND_MENU_CAMERA_BUZZ         | (0xFC << 8))

enum {
    INPUT_LEFT,
    INPUT_RIGHT,
    INPUT_A,
    INPUT_Z
};

enum {
    RESULT_NONE,
    RESULT_OK,
    RESULT_CANCEL
};

static int dynos_opt_process_input(struct DynosOption *opt, int input) {
    switch (opt->type) {
        case DOPT_TOGGLE:
            if (input == INPUT_LEFT) {
                *opt->toggle.ptog = false;
                return RESULT_OK;
            }
            if (input == INPUT_RIGHT) {
                *opt->toggle.ptog = true;
                return RESULT_OK;
            }
            if (input == INPUT_A) {
                *opt->toggle.ptog = !(*opt->toggle.ptog);
                return RESULT_OK;
            }
            break;

        case DOPT_CHOICE:
        case DOPT_CHOICELEVEL:
        case DOPT_CHOICESTAR:
            if (input == INPUT_LEFT) {
                *opt->choice.pindex = (*opt->choice.pindex + opt->choice.count - 1) % (opt->choice.count);
                return RESULT_OK;
            }
            if (input == INPUT_RIGHT || input == INPUT_A) {
                *opt->choice.pindex = (*opt->choice.pindex + 1) % (opt->choice.count);
                return RESULT_OK;
            }
            break;

        case DOPT_SCROLL:
            if (input == INPUT_LEFT) {
                *opt->scroll.pvalue = MAX(opt->scroll.min, *opt->scroll.pvalue - opt->scroll.step * (gPlayer1Controller->buttonDown & A_BUTTON ? 5 : 1));
                return RESULT_OK;
            }
            if (input == INPUT_RIGHT) {
                *opt->scroll.pvalue = MIN(opt->scroll.max, *opt->scroll.pvalue + opt->scroll.step * (gPlayer1Controller->buttonDown & A_BUTTON ? 5 : 1));
                return RESULT_OK;
            }
            break;

        case DOPT_BIND:
            if (input == INPUT_LEFT) {
                opt->bind.index = MAX(0, opt->bind.index - 1);
                return RESULT_OK;
            }
            if (input == INPUT_RIGHT) {
                opt->bind.index = MIN(MAX_BINDS - 1, opt->bind.index + 1);
                return RESULT_OK;
            }
            if (input == INPUT_Z) {
                opt->bind.pbinds[opt->bind.index] = VK_INVALID;
                return RESULT_OK;
            }
            if (input == INPUT_A) {
                opt->bind.pbinds[opt->bind.index] = VK_INVALID;
                sBindingState = 1;
                controller_get_raw_key();
                return RESULT_OK;
            }
            break;

        case DOPT_BUTTON:
            if (input == INPUT_A && opt->button.funcName != NULL) {
                DynosActionFunction action = dynos_get_action(opt->button.funcName);
                if (action != NULL && action(opt->name)) {
                    return RESULT_OK;
                }
                return RESULT_CANCEL;
            }
            break;

        case DOPT_SUBMENU:
            if (input == INPUT_A) {
                if (opt->submenu.child != NULL) {
                    sCurrentOpt = opt->submenu.child;
                    return RESULT_OK;
                }
                return RESULT_CANCEL;
            }
            break;
    }
    return RESULT_NONE;
}

static void dynos_open(struct DynosOption *menu) {
    play_sound(SOUND_DYNOS_SELECT, gDefaultSoundArgs);
    sCurrentMenu = menu;
    sCurrentOpt = menu;
}

static void dynos_close() {
    if (sCurrentMenu != NULL) {
        play_sound(SOUND_DYNOS_SAVED, gDefaultSoundArgs);
        controller_reconfigure();
        configfile_save(configfile_name());
        dynos_save_config();
        sCurrentMenu = NULL;
    }
}

static void dynos_process_inputs() {
    static int sStickTimer = 0;
    static bool sPrevStick = 0;

    // Stick values
    float stickX = gPlayer1Controller->stickX;
    float stickY = gPlayer1Controller->stickY;
    if (absx(stickX) > 60 || absx(stickY) > 60) {
        if (sStickTimer == 0) {
            sStickTimer = (sPrevStick ? 2 : 9);
        } else {
            stickX = 0;
            stickY = 0;
            sStickTimer--;
        }
        sPrevStick = true;
    } else {
        sStickTimer = 0;
        sPrevStick = false;
    }

    // Key binding
    if (sBindingState != 0) {
        unsigned int key = (sCurrentOpt->dynos ? (unsigned int) dynos_controller_get_key_pressed() : controller_get_raw_key());
        if (key != VK_INVALID) {
            play_sound(SOUND_DYNOS_SELECT, gDefaultSoundArgs);
            sCurrentOpt->bind.pbinds[sCurrentOpt->bind.index] = key;
            sBindingState = false;
        }
        return;
    }

    if (sCurrentMenu != NULL) {

        // Up
        if (stickY > +60) {
            if (sCurrentOpt->prev != NULL) {
                sCurrentOpt = sCurrentOpt->prev;
            } else {
                while (sCurrentOpt->next) sCurrentOpt = sCurrentOpt->next;
            }
            play_sound(SOUND_DYNOS_SELECT, gDefaultSoundArgs);
            return;
        }

        // Down
        if (stickY < -60) {
            if (sCurrentOpt->next != NULL) {
                sCurrentOpt = sCurrentOpt->next;
            } else {
                while (sCurrentOpt->prev) sCurrentOpt = sCurrentOpt->prev;
            }
            play_sound(SOUND_DYNOS_SELECT, gDefaultSoundArgs);
            return;
        }

        // Left
        if (stickX < -60) {
            switch (dynos_opt_process_input(sCurrentOpt, INPUT_LEFT)) {
                case RESULT_OK:     play_sound(SOUND_DYNOS_OK, gDefaultSoundArgs); break;
                case RESULT_CANCEL: play_sound(SOUND_DYNOS_CANCEL, gDefaultSoundArgs); break;
                case RESULT_NONE:   break;
            }
            return;
        }

        // Right
        if (stickX > +60) {
            switch (dynos_opt_process_input(sCurrentOpt, INPUT_RIGHT)) {
                case RESULT_OK:     play_sound(SOUND_DYNOS_OK, gDefaultSoundArgs); break;
                case RESULT_CANCEL: play_sound(SOUND_DYNOS_CANCEL, gDefaultSoundArgs); break;
                case RESULT_NONE:   break;
            }
            return;
        }

        // A
        if (gPlayer1Controller->buttonPressed & A_BUTTON) {
            switch (dynos_opt_process_input(sCurrentOpt, INPUT_A)) {
                case RESULT_OK:     play_sound(SOUND_DYNOS_OK, gDefaultSoundArgs); break;
                case RESULT_CANCEL: play_sound(SOUND_DYNOS_CANCEL, gDefaultSoundArgs); break;
                case RESULT_NONE:   break;
            }
            return;
        }

        // B
        if (gPlayer1Controller->buttonPressed & B_BUTTON) {
            if (sCurrentOpt->parent != NULL) {
                sCurrentOpt = sCurrentOpt->parent;
                play_sound(SOUND_DYNOS_SELECT, gDefaultSoundArgs);
            } else {
                dynos_close();
            }
            return;
        }

        // Z
        if (gPlayer1Controller->buttonPressed & Z_TRIG) {
            switch (dynos_opt_process_input(sCurrentOpt, INPUT_Z)) {
                case RESULT_OK:     play_sound(SOUND_DYNOS_OK, gDefaultSoundArgs); break;
                case RESULT_CANCEL: play_sound(SOUND_DYNOS_CANCEL, gDefaultSoundArgs); break;
                case RESULT_NONE:
                    if (sCurrentMenu == sDynosMenu) {
                        dynos_close();
                    } else {
                        dynos_open(sDynosMenu);
                    } break;
            }
            return;
        }

        // R
        if (gPlayer1Controller->buttonPressed & R_TRIG) {
            if (sCurrentMenu == sOptionsMenu) {
                dynos_close();
            } else {
                dynos_open(sOptionsMenu);
            }
            return;
        }

        // Start
        if (gPlayer1Controller->buttonPressed & START_BUTTON) {
            dynos_close();
            return;
        }
    } else if (gPlayer1Controller->buttonPressed & R_TRIG) {
        dynos_open(sOptionsMenu);
    } else if (gPlayer1Controller->buttonPressed & Z_TRIG) {
        dynos_open(sDynosMenu);
    }
}

#define PROMPT_OFFSET (56.25f * GFX_DIMENSIONS_ASPECT_RATIO)
static void dynos_draw_prompt() {
    if (sCurrentMenu == sOptionsMenu) {
        dynos_print_string(sDynosTextOpenLeft,   PROMPT_OFFSET, 212, COLOR_WHITE, COLOR_BLACK, 1);
        dynos_print_string(sDynosTextCloseRight, PROMPT_OFFSET, 212, COLOR_WHITE, COLOR_BLACK, 0);
    } else if (sCurrentMenu == sDynosMenu) {
        dynos_print_string(sDynosTextCloseLeft,  PROMPT_OFFSET, 212, COLOR_WHITE, COLOR_BLACK, 1);
        dynos_print_string(sDynosTextOpenRight,  PROMPT_OFFSET, 212, COLOR_WHITE, COLOR_BLACK, 0);
    } else {
        dynos_print_string(sDynosTextOpenLeft,   PROMPT_OFFSET, 212, COLOR_WHITE, COLOR_BLACK, 1);
        dynos_print_string(sDynosTextOpenRight,  PROMPT_OFFSET, 212, COLOR_WHITE, COLOR_BLACK, 0);
    }
    dynos_process_inputs();
#ifdef RENDER96_2_0
    set_language(languages[configLanguage]);
#endif
}

//
// Init
//

static void (*controller_read)(OSContPad *);
static void dynos_controller_read(OSContPad *pad) {
    controller_read(pad);
    dynos_loop(sDynosMenu, dynos_controller_update, (void *) pad);
}

static void dynos_create_warp_to_level_options() {
    dynos_create_submenu("dynos_warp_submenu", "Warp to Level", "WARP TO LEUEL");

    // Level select
    {
    struct DynosOption *opt = dynos_add_option("dynos_warp_level", NULL, "Level Select", "");
    opt->type               = DOPT_CHOICELEVEL;
    opt->choice.count       = level_get_count(true);
    opt->choice.pindex      = calloc(1, sizeof(int));
    *opt->choice.pindex     = 0;
    }

    // Star select
    {
    struct DynosOption *opt = dynos_add_option("dynos_warp_act", NULL, "Star Select", "");
    opt->type               = DOPT_CHOICESTAR;
    opt->choice.count       = 6;
    opt->choice.pindex      = calloc(1, sizeof(int));
    *opt->choice.pindex     = 0;
    }

    dynos_create_button("dynos_warp_to_level", "Warp", "dynos_warp_to_level");
    dynos_end_submenu();
}

void dynos_init() {

    // Convert options menu
    dynos_convert_options_menu();

    // Create DynOS menu
    dynos_load_options();

    // Warp to level
    dynos_create_warp_to_level_options();

    // Restart level
    dynos_create_button("dynos_restart_level", "Restart Level", "dynos_restart_level");

    // Return to main menu
    dynos_create_button("dynos_return_to_main_menu", "Return to Main Menu", "dynos_return_to_main_menu");

    // Init config
    dynos_load_config();

    // Init controller
    controller_read = controller_keyboard.read;
    controller_keyboard.read = dynos_controller_read;

    // Init text
    sDynosTextDynosMenu   = str64h("DYNOS MENU");
    sDynosTextA           = str64h("([A]) >");
    sDynosTextOpenLeft    = str64h("[Z] DynOS");
    sDynosTextCloseLeft   = str64h("[Z] Return");
#ifndef RENDER96_2_0
    sDynosTextOptionsMenu = str64h("OPTIONS");
    sDynosTextDisabled    = str64h("Disabled");
    sDynosTextEnabled     = str64h("Enabled");
    sDynosTextNone        = str64h("NONE");
    sDynosTextDotDotDot   = str64h("...");
    sDynosTextOpenRight   = str64h("[R] Options");
    sDynosTextCloseRight  = str64h("[R] Return");
#endif
}

//
// Hijack
//

unsigned char optmenu_open = 0;

void optmenu_toggle(void) {
    dynos_close();
}

void optmenu_draw(void) {
    dynos_draw_menu();
}

void optmenu_draw_prompt(void) {
    dynos_draw_prompt();
    optmenu_open = (sCurrentMenu != NULL);
}

void optmenu_check_buttons(void) {
}

//
// Return to Main Menu
//

extern char gDialogBoxState;
extern short gMenuMode;
void dynos_unpause_game() {
    level_set_transition(0, 0);
    play_sound(SOUND_MENU_PAUSE_2, gDefaultSoundArgs);
    gDialogBoxState = 0;
    gMenuMode = -1;
}

bool dynos_return_to_main_menu(UNUSED const char *optName) {
    optmenu_toggle();
    dynos_unpause_game();
    fade_into_special_warp(-2, 0);
    return true;
}

//
// Warp to Level
//

bool dynos_perform_warp(enum LevelNum levelNum, int actNum) {
    enum CourseNum courseNum = level_get_course(levelNum);
    if (courseNum == COURSE_NONE) {
        return false;
    }

    // Free everything from the current level
    optmenu_toggle();
    dynos_unpause_game();
    set_sound_disabled(FALSE);
    play_shell_music();
    stop_shell_music();
    stop_cap_music();
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

    // Load the new level
    gCurrLevelNum = levelNum;
    gCurrCourseNum = courseNum;
    gCurrActNum = (courseNum <= COURSE_STAGES_MAX ? actNum : 0);
    gDialogCourseActNum = gCurrActNum;
    gCurrAreaIndex = 1;
    level_script_execute((struct LevelCommand *) level_get_script(levelNum));
    sWarpDest.type = 2;
    sWarpDest.levelNum = gCurrLevelNum;
    sWarpDest.areaIdx = 1;
    sWarpDest.nodeId = 0x0A;
    sWarpDest.arg = 0;
    gSavedCourseNum = gCurrCourseNum;
    return true;
}

bool dynos_warp_to_level(UNUSED const char *optName) {
    enum LevelNum levelNum = level_get_list(true, true)[dynos_get_value("dynos_warp_level")];
    return dynos_perform_warp(levelNum, dynos_get_value("dynos_warp_act") + 1);
}

//
// Restart Level
//

bool dynos_restart_level(UNUSED const char *optName) {
    enum LevelNum levelNum = (enum LevelNum) gCurrLevelNum;
    if (levelNum == LEVEL_BOWSER_1) levelNum = LEVEL_BITDW;
    if (levelNum == LEVEL_BOWSER_2) levelNum = LEVEL_BITFS;
    if (levelNum == LEVEL_BOWSER_3) levelNum = LEVEL_BITS;
    return dynos_perform_warp(levelNum, gCurrActNum);
}
