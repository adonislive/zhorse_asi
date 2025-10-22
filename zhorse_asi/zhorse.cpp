#include <windows.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <functional>
#include <map>
#include <algorithm>
#include "detours.h"
#include "eqmac.h"
#include "eqmac_functions.h"

// Essential globals
HMODULE eqmain_dll = NULL;
bool bInitalized = false;

// Horse QOL Support globals
bool HorsesEnabled = false; // Initialized from eqclient.ini (UseLuclinElementals/UseLuclinHorses)

// Essential utility functions
void PatchA(LPVOID address, const void* dwValue, SIZE_T dwBytes) {
    unsigned long oldProtect;
    VirtualProtect((void*)address, dwBytes, PAGE_EXECUTE_READWRITE, &oldProtect);
    memcpy((void*)address, dwValue, dwBytes);
    FlushInstructionCache(GetCurrentProcess(), (void*)address, dwBytes);
    VirtualProtect((void*)address, dwBytes, oldProtect, &oldProtect);
}

template <class T>
void PatchT(int target, T value)
{
    PatchA((void*)target, &value, sizeof(T));
}

void PatchNopByRange(DWORD start, DWORD end)
{
    for (DWORD i = start; i < end; i++)
        PatchT(i, (BYTE)0x90);
}

// Detour tracking structure
typedef struct _detourinfo
{
    DWORD_PTR tramp;
    DWORD_PTR detour;
} detourinfo;

std::map<DWORD, _detourinfo> ourdetours;

template <class T>
void AddDetourf(DWORD address, T detour, T& trampoline)
{
    if (ourdetours.find(address) == ourdetours.end())
    {
        trampoline = (T)DetourFunction((PBYTE)address, (PBYTE)detour);
        _detourinfo di;
        di.detour = (DWORD_PTR)detour;
        di.tramp = (DWORD_PTR)trampoline;
        ourdetours[address] = di;
    }
}

#define EzDetour(offset,detour,trampoline) AddDetourf((DWORD)offset,detour,trampoline)

// --------------------------------------------------------------------------
// Horse QOL Support
// --------------------------------------------------------------------------

typedef bool(__thiscall* EQ_FUNCTION_TYPE_EQPlayer__HasInvalidRiderTexture)(void* this_ptr);
EQ_FUNCTION_TYPE_EQPlayer__HasInvalidRiderTexture HasInvalidRiderTexture_Trampoline;
bool __fastcall HasInvalidRiderTexture_Detour(EQSPAWNINFO* this_ptr, void* not_used)
{
    return !HorsesEnabled;
}

typedef void(__thiscall* EQ_FUNCTION_TYPE_EQPlayer__MountEQPlayer)(EQSPAWNINFO* this_ptr, EQSPAWNINFO* mount);
EQ_FUNCTION_TYPE_EQPlayer__MountEQPlayer EQPlayer__MountEQPlayer_Trampoline;
void __fastcall EQPlayer__MountEQPlayer_Detour(EQSPAWNINFO* this_ptr, int unused, EQSPAWNINFO* horse)
{
    BYTE* cdisplay = *(BYTE**)EQ_POINTER_CDisplay;
    BYTE display_0xA0 = cdisplay[0xA0];
    cdisplay[0xA0] = HorsesEnabled;
    EQPlayer__MountEQPlayer_Trampoline(this_ptr, horse);
    cdisplay[0xA0] = display_0xA0;
}

// Horse Tilt
typedef void(__cdecl* EQ_FUNCTION_TYPE_ProcessPhysics)(EQSPAWNINFO* ent, int* EQMissile, DWORD* EQEffect);
EQ_FUNCTION_TYPE_ProcessPhysics ProcessPhysics_Trampoline;
void __cdecl ProcessPhysics_Detour(EQSPAWNINFO* ent, int* missile, DWORD* effect)
{
    ProcessPhysics_Trampoline(ent, missile, effect);
    if (ent && ent->Race == 216 && ent->ActorInfo && ent->ActorInfo->ActorInstance && ent->ActorInfo->Rider)
    {
        if (ent->LevitationState != 0)
        {
            float ground_distance = ent->ActorInfo->Z + ent->ModelHeightOffset - ent->Z; // Distance above ground (negative units)
            if (ground_distance <= -5.0f)
            {
                if (ent->MovementSpeed != 0)
                    ent->ActorInfo->ActorInstance->SurfacePitchType = 1;
            }
            else if (ground_distance > -2.5f)
                ent->ActorInfo->ActorInstance->SurfacePitchType = 2;
        }
        else
        {
            ent->ActorInfo->ActorInstance->SurfacePitchType = 2;
        }
    }
}

// Mounted Inertia Fix
static void __fastcall EQPlayer_SetAccel_Detour(EQSPAWNINFO* this_entity, int unused_edx, float target_speed, int ignore_rider_flag)
{
    if (this_entity->MovementSpeed == target_speed) return;

    // If not a mount with a rider, immediately accelerate to target_speed.
    if (this_entity->Race != 216 || !this_entity->ActorInfo || !this_entity->ActorInfo->Rider)
    {
        this_entity->MovementSpeed = target_speed;
        return;
    }

    // Special mount handling:
    // Immediately decelerate to target speed.
    if (target_speed < this_entity->MovementSpeed)
    {
        this_entity->MovementSpeed = target_speed;
        return;
    }

    // Accelerate immediately up to at least running speed but then slowly accelerate to max.
    // The nominal acceleration is frame rate adjusted in process_physics.
    // .006 constant used in formula was tested against original function. Can be tweaked later if necessary
    // First 5 second total distance matches original function total distance in 5 seconds
    // Max Speed horse reaches max speed in ~9 seconds so Max speed horse does not get supercharged by this fix
    float min_speed = min(target_speed, 0.7f);
    float process_physics_fps_factor = *reinterpret_cast<float*>(0x007d01dc);
    float accel_speed = process_physics_fps_factor * 0.006f + this_entity->MovementSpeed;
    this_entity->MovementSpeed = max(min_speed, min(target_speed, accel_speed));
}

// Mounted Ducking fix
typedef int(__cdecl* EQ_FUNCTION_TYPE_ExecuteCmd)(UINT cmd, bool isdown, int unk2);
EQ_FUNCTION_TYPE_ExecuteCmd ExecuteCmd_Trampoline;
int __cdecl ExecuteCmd_Detour(UINT cmd, bool isdown, int unk2)
{
    if (cmd == 0xA) //duck while mounted command hook
    {
        if (!isdown) return 1;
        EQSPAWNINFO* controlled = EQ_OBJECT_ControlledSpawn;
        EQSPAWNINFO* self = EQ_OBJECT_PlayerSpawn;
        if (controlled && self && self->CharInfo && self->ActorInfo && controlled->Race == 216 && controlled != self && controlled->ActorInfo && controlled->ActorInfo->Rider == self)
        {
            float tmp = self->ActorInfo->ZCeiling;
            self->ActorInfo->ZCeiling = self->Z + self->ModelHeightOffset + 1.0f;
            if (self->StandingState == EQ_STANDING_STATE_DUCKING)
                EQPlayer::ChangeStance(self, EQ_STANDING_STATE_STANDING);
            else if (self->StandingState == EQ_STANDING_STATE_STANDING)
                EQPlayer::ChangeStance(self, EQ_STANDING_STATE_DUCKING);
            self->ActorInfo->ZCeiling = tmp;
            return 1;
        }
    }
    return ExecuteCmd_Trampoline(cmd, isdown, unk2);
}

// Mounted Z-coordinate fix while levitating
typedef __int64(_cdecl* EQ_FUNCTION_TYPE_PackPhysics)(PlayerPosition* playerPositionPtr, void* packedDataBuffer);
EQ_FUNCTION_TYPE_PackPhysics PackPhysics_Trampoline;
__int64 _cdecl PackPhysics_Detour(PlayerPosition* playerPositionPtr, void* packedDataBuffer)
{
    EQSPAWNINFO* controlled = EQ_OBJECT_ControlledSpawn;
    EQSPAWNINFO* self = EQ_OBJECT_PlayerSpawn;
    // Fix levitating mount coordinates before they get packed
    if (controlled && self != controlled && controlled->Race == 216 && controlled->ActorInfo && controlled->ActorInfo->Rider == self)
    {
        float playerZ = self->Z - self->ModelHeightOffset;
        if (playerZ + 0.501f >= playerPositionPtr->Z)
            playerPositionPtr->Z = playerZ;
    }
    return PackPhysics_Trampoline(playerPositionPtr, packedDataBuffer);
}

// Mounted Z-axis Downward Control and horse tilt
void HorseLeviatePitchControl()
{
    static int* const kKeyDownStates = reinterpret_cast<int*>(0x007ce04c);
    const int CMD_FORWARD = 3;
    const int CMD_PITCH_DOWN = 16;
    EQSPAWNINFO* self = EQ_OBJECT_PlayerSpawn;
    EQSPAWNINFO* controlled = EQ_OBJECT_ControlledSpawn;
    if (!self || !controlled) return;

    if (controlled->Race == 216 && self->ActorInfo && self->ActorInfo->Mount && self->ActorInfo->Mount == controlled)
    {
        if (self->LevitationState != 0)
        {
            float pitch = self->Pitch;
            float ground_distance = controlled->ActorInfo->Z + controlled->ModelHeightOffset - controlled->Z;

            // Pitch Control
            if (pitch < 0 && controlled->MovementSpeed > 0)
            {
                bool auto_run_active = *reinterpret_cast<int*>(0x00798600) != 0;
                if (kKeyDownStates[CMD_FORWARD] || auto_run_active)
                {
                    if (ground_distance <= -2.501f)
                    {
                        // If off the ground set their Z speed based on pitch.
                        float down_speed = std::abs(pitch) / -14.0f;
                        if (down_speed < -7.0f)
                            down_speed = -7.0f;
                        if (controlled->MovementSpeedZ <= 0.0f && controlled->MovementSpeedZ > down_speed)
                            controlled->MovementSpeedZ = down_speed;
                    }
                    else
                    {
                        // Close to the floor.
                        float speed_factor = -pitch / 64.0f;
                        controlled->Z -= speed_factor;
                    }
                }
            }
        }
    }
}

typedef void(*EQ_FUNCTION_TYPE_MainLoop)();
EQ_FUNCTION_TYPE_MainLoop MainLoop_Trampoline;
void MainLoop_Detour()
{
    HorseLeviatePitchControl();
    MainLoop_Trampoline();
}

typedef int(__thiscall* EQ_FUNCTION_TYPE_EQPlayer_bodyEnvironmentChange)(EQSPAWNINFO* this_ptr, BYTE swimmingWaterType);
EQ_FUNCTION_TYPE_EQPlayer_bodyEnvironmentChange EQPlayer_bodyEnvironmentChange_Trampoline;
int __fastcall EQPlayer_bodyEnvironmentChange_Detour(EQSPAWNINFO* this_ptr, int u, BYTE swimmingWaterType)
{
    if (this_ptr && this_ptr->Race == 216)
    {
        // Prevent horse 'super speed' when in water
        PatchT(0x0050BB31, (BYTE)11);
        int ret = EQPlayer_bodyEnvironmentChange_Trampoline(this_ptr, swimmingWaterType);
        PatchT(0x0050BB31, (BYTE)5);
        return ret;
    }
    return EQPlayer_bodyEnvironmentChange_Trampoline(this_ptr, swimmingWaterType);
}

typedef void* (__thiscall* EQ_FUNCTION_TYPE_CDisplay__CreatePlayerActor)(int* this_ptr, EQSPAWNINFO* entity);
EQ_FUNCTION_TYPE_CDisplay__CreatePlayerActor CDisplay__CreatePlayerActor_Trampoline;
static void* __fastcall CDisplay__CreatePlayerActor_Detour(int* this_ptr, int u, EQSPAWNINFO* entity)
{
    if (entity && entity->Race == 216)
    {
        // QOL - Prevents the hidden horse from colliding with players, blocking zonelines, etc
        entity->TargetType = EQ_SPAWN_TARGET_TYPE_CANNOT_TARGET;
        // Since the horse has no collision, stop it from falling through the world
        entity->LevitationState = 1;
        if (entity->ActorInfo)
            entity->ActorInfo->IsAffectedByGravity = 0;
    }
    return CDisplay__CreatePlayerActor_Trampoline(this_ptr, entity);
}

typedef int(__cdecl* EQ_FUNCTION_TYPE_ProcessUpdateStats)(short* stats_update);
EQ_FUNCTION_TYPE_ProcessUpdateStats ProcessUpdateStats_Trampoline;
static int __cdecl ProcessUpdateStats_Detour(short* stats_update) {
    int result = ProcessUpdateStats_Trampoline(stats_update);
    if (result != 1) return result;

    // When another player is mounted, the client intercepts the OP_MobUpdate
    // targeting the other player spawn id and updates their mount entity, not the
    // player entity. This process includes the UpdatePlayerActor() call which
    // updates the mount. For some reason that UpdatePlayerActor() has code to
    // update a riders's mount to match the rider but not vice versa. As a result
    // the other player entity position and view actor location are both left
    // behind at the mount point until the client player gets close enough to that
    // stuck point so they are added to the world actors list (less than a
    // distance of 600). This results in client targeting and spell distance check
    // bugs (heals failing, etc) on a mounted other player next to the client
    // player until that sync happens.

    // This patch copies the logic in process_physics when the players are within
    // the world visible actors list to sync the rider entity position and actor
    // info floor height with the horse mount.
    short spawn_id = *stats_update;
    EQSPAWNINFO* entity = EQPlayer::GetSpawn(spawn_id);
    // First filter to only apply to other players with valid actorinfo.
    if (!entity || entity->Type != 0 || entity == EQ_OBJECT_PlayerSpawn ||
        !entity->ActorInfo)
        return 1;

    // And then further restrict to horse mounted other players.
    EQSPAWNINFO* mount = entity->ActorInfo->Mount;
    if (!mount || mount->Race != 0xd8 || !mount->ActorInfo) return 1;

    // Keep the player entity and actorinfo floor height in sync with the mount.
    // This is redundant with process_physics when the other player is within
    // view.
    entity->Y = mount->Y;
    entity->X = mount->X;
    entity->Z = mount->Z + mount->ModelHeightOffset + 1.0;  // Shortcut approx.
    entity->Heading = mount->Heading;
    entity->MovementSpeed = 0.0f;
    entity->MovementSpeedY = 0.0f;
    entity->MovementSpeedX = 0.0f;
    entity->MovementSpeedZ = 0.0f;
    entity->MovementSpeedHeading = 0.0f;
    entity->ActorInfo->Z = mount->ActorInfo->Z;

    // Additionally we need to update the actor location to also be in sync or
    // things like the visible actors list and spell effects from a heal won't
    // work properly.
    int world = Graphics::GetDisplay() ? Graphics::GetDisplay()[1] : 0;
    int t3dSetActorLocationAddr = *(int*)(0x007f9ac4);
    _EQACTORINSTANCEINFO* actor_instance = entity->ActorInfo->ActorInstance;
    if (world && t3dSetActorLocationAddr && actor_instance) {
        float location[6] = { entity->Y, entity->X, entity->Z,
                                                    entity->Heading, entity->Pitch, 0 };
        ((void(__cdecl*)(int, _EQACTORINSTANCEINFO*, float*))t3dSetActorLocationAddr)
            (world, actor_instance, location);
    }

    return 1;
}


void ApplyHorseQolPatches(HINSTANCE heqGfxMod)
{
    char trueValue[] = "TRUE";
    HorsesEnabled = GetEQClientIniFlag_55B947("Defaults", "UseLuclinElementals", trueValue) && GetEQClientIniFlag_55B947("Defaults", "UseLuclinHorses", trueValue);
    HasInvalidRiderTexture_Trampoline = (EQ_FUNCTION_TYPE_EQPlayer__HasInvalidRiderTexture)DetourFunction((PBYTE)0x0051FCA6, (PBYTE)HasInvalidRiderTexture_Detour);
    EQPlayer__MountEQPlayer_Trampoline = (EQ_FUNCTION_TYPE_EQPlayer__MountEQPlayer)DetourFunction((PBYTE)0x51FD83, (PBYTE)EQPlayer__MountEQPlayer_Detour);

    // If Horses enabled: Turn on Horse Mechanic QoL
    if (HorsesEnabled)
    {
        // Inertia
        DetourFunction((PBYTE)0x520074, (PBYTE)EQPlayer_SetAccel_Detour);
        // Synchronize rider / mount position
        ProcessUpdateStats_Trampoline = (EQ_FUNCTION_TYPE_ProcessUpdateStats)DetourFunction((PBYTE)0x004dbcf5, (PBYTE)ProcessUpdateStats_Detour);
        // Duck
        ExecuteCmd_Trampoline = (EQ_FUNCTION_TYPE_ExecuteCmd)DetourFunction((PBYTE)0x54050C, (PBYTE)ExecuteCmd_Detour);
        // Tilt
        ProcessPhysics_Trampoline = (EQ_FUNCTION_TYPE_ProcessPhysics)DetourFunction((PBYTE)0x54D964, (PBYTE)ProcessPhysics_Detour);
        // Accurate Z-coord
        PackPhysics_Trampoline = (EQ_FUNCTION_TYPE_PackPhysics)DetourFunction((PBYTE)0x4F224E, (PBYTE)PackPhysics_Detour);
        PatchNopByRange(0x4DFBF0, 0x4DFBF5);
        // Downward Pitch Control
        MainLoop_Trampoline = (EQ_FUNCTION_TYPE_MainLoop)DetourFunction((PBYTE)0x5473c3, (PBYTE)MainLoop_Detour);
        // Prevent horse super speed in water
        EQPlayer_bodyEnvironmentChange_Trampoline = (EQ_FUNCTION_TYPE_EQPlayer_bodyEnvironmentChange)DetourFunction((PBYTE)0x50BAED, (PBYTE)EQPlayer_bodyEnvironmentChange_Detour);
    }

    // If Horses disabled: Turn on AK Mechanic QoL
    if (!HorsesEnabled)
    {
        // Prevent invisible horse collision
        CDisplay__CreatePlayerActor_Trampoline = (EQ_FUNCTION_TYPE_CDisplay__CreatePlayerActor)DetourFunction((PBYTE)0x4ADF2C, (PBYTE)CDisplay__CreatePlayerActor_Detour);
    }
}

// --------------------------------------------------------------------------
// Horse QOL Support [End]
// --------------------------------------------------------------------------

void InitHooks()
{
    //bypass filename req
    unsigned char test3[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0xEB, 0x1B, 0x90, 0x90, 0x90, 0x90 };
    PatchA((DWORD*)0x005595A7, test3, sizeof(test3));

    //Changes the limit to 0x3E8 (1000) on race limit checks.
    unsigned char test15[] = { 0xE8, 0x03 };
    PatchA((DWORD*)0x004AE612, test15, sizeof(test15));

    //Changes the limit to 0x3E8 (1000) on race animations.
    unsigned char test16[] = { 0xE8, 0x03 };
    PatchA((DWORD*)0x4d93c5, test16, sizeof(test16));

    //Changes the limit to 0x3E8 (1000) on race spawning to apply sounds and textures.
    unsigned char test17[] = { 0xE8, 0x03 };
    PatchA((DWORD*)0x50704c, test17, sizeof(test17));

    HINSTANCE heqGfxMod = GetModuleHandle("eqgfx_dx8.dll");

    // Horse Support
    ApplyHorseQolPatches(heqGfxMod);

    bInitalized = true;
}

void ExitHooks()
{
    if (!bInitalized)
    {
        return;
    }

    for (std::map<DWORD, _detourinfo>::iterator i = ourdetours.begin(); i != ourdetours.end(); i++)
    {
        DetourRemove((PBYTE)i->second.tramp, (PBYTE)i->second.detour);
    }
}

BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        InitHooks();
        return TRUE;
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH)
    {
        ExitHooks();
        return TRUE;
    }
    return TRUE;

}
