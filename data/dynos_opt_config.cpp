#include "dynos.cpp.h"

extern DynosOption *DynOS_Opt_Loop(DynosOption *aOpt, DynosLoopFunc aFunc, void *aData);

static bool DynOS_Opt_GetConfig(DynosOption *aOpt, void *aData) {
    Pair<u8, String> _TypeAndName = *((Pair<u8, String> *) aData);
    return (aOpt->mType == _TypeAndName.first && aOpt->mConfigName == _TypeAndName.second);
}

void DynOS_Opt_LoadConfig(DynosOption *aMenu) {
    SysPath _Filename = fstring("%s/%s", sys_user_path(), DYNOS_CONFIG_FILENAME);
    FILE *_File = fopen(_Filename.c_str(), "rb");
    if (_File == NULL) {
        return;
    }

    while (true) {
        Pair<u8, String> _ConfigTypeAndName = { DOPT_NONE, "" };
        _ConfigTypeAndName.first = ReadBytes<u8>(_File);
        _ConfigTypeAndName.second.Read(_File);
        if (_ConfigTypeAndName.first == DOPT_NONE) {
            break;
        }

        DynosOption *_Opt = DynOS_Opt_Loop(aMenu, DynOS_Opt_GetConfig, (void *) &_ConfigTypeAndName);
        if (_Opt != NULL) {
            switch (_Opt->mType) {
                case DOPT_TOGGLE: *_Opt->mToggle.mTog    = ReadBytes<bool>(_File); break;
                case DOPT_CHOICE: *_Opt->mChoice.mIndex  = ReadBytes<s32> (_File); break;
                case DOPT_SCROLL: *_Opt->mScroll.mValue  = ReadBytes<s32> (_File); break;
                case DOPT_BIND:    _Opt->mBind.mBinds[0] = ReadBytes<u32>(_File);
                                   _Opt->mBind.mBinds[1] = ReadBytes<u32>(_File);
                                   _Opt->mBind.mBinds[2] = ReadBytes<u32>(_File); break;
            }
        }
    }
    fclose(_File);
}

static bool DynOS_Opt_SetConfig(DynosOption *aOpt, void *aData) {
    if (aOpt->mConfigName.Length() != 0 &&
        aOpt->mConfigName          != "null" &&
        aOpt->mConfigName          != "NULL") {
        FILE *_File = (FILE *) aData;
        switch (aOpt->mType) {
            case DOPT_TOGGLE:
                WriteBytes<u8>(_File, DOPT_TOGGLE);
                aOpt->mConfigName.Write(_File);
                WriteBytes<bool>(_File, *aOpt->mToggle.mTog);
                break;

            case DOPT_CHOICE:
                WriteBytes<u8>(_File, DOPT_CHOICE);
                aOpt->mConfigName.Write(_File);
                WriteBytes<s32>(_File, *aOpt->mChoice.mIndex);
                break;

            case DOPT_SCROLL:
                WriteBytes<u8>(_File, DOPT_SCROLL);
                aOpt->mConfigName.Write(_File);
                WriteBytes<s32>(_File, *aOpt->mScroll.mValue);
                break;

            case DOPT_BIND:
                WriteBytes<u8>(_File, DOPT_BIND);
                aOpt->mConfigName.Write(_File);
                WriteBytes<u32>(_File, aOpt->mBind.mBinds[0]);
                WriteBytes<u32>(_File, aOpt->mBind.mBinds[1]);
                WriteBytes<u32>(_File, aOpt->mBind.mBinds[2]);
                break;
        }
    }
    return 0;
}

void DynOS_Opt_SaveConfig(DynosOption *aMenu) {
    SysPath _Filename = fstring("%s/%s", sys_user_path(), DYNOS_CONFIG_FILENAME);
    FILE *_File = fopen(_Filename.c_str(), "wb");
    if (!_File) {
        return;
    }

    DynOS_Opt_Loop(aMenu, DynOS_Opt_SetConfig, (void *) _File);
    fclose(_File);
}
