/*
===========================================================================

Return to Castle Wolfenstein single player GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company. 

This file is part of the Return to Castle Wolfenstein single player GPL Source Code (RTCW SP Source Code).  

RTCW SP Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

RTCW SP Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with RTCW SP Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the RTCW SP Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the RTCW SP Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

// Copyright (C) 1999-2000 Id Software, Inc.
//

// g_public.h -- game module information visible to server

#define GAME_API_VERSION    8

// entity->svFlags
// the server does not know how to interpret most of the values
// in entityStates (level eType), so the game must explicitly flag
// special server behaviors
#define SVF_NOCLIENT            0x00000001  // don't send entity to clients, even if it has effects
#define SVF_VISDUMMY            0x00000004  // this ent is a "visibility dummy" and needs it's master to be sent to clients that can see it even if they can't see the master ent
#define SVF_BOT                 0x00000008
// Wolfenstein
#define SVF_CASTAI              0x00000010
// done.
#define SVF_BROADCAST           0x00000020  // send to all connected clients
#define SVF_PORTAL              0x00000040  // merge a second pvs at origin2 into snapshots
#define SVF_USE_CURRENT_ORIGIN  0x00000080  // entity->r.currentOrigin instead of entity->s.origin
											// for link position (missiles and movers)
// Ridah
#define SVF_NOFOOTSTEPS         0x00000100
// done.
// MrE:
#define SVF_CAPSULE             0x00000200  // use capsule for collision detection

#define SVF_VISDUMMY_MULTIPLE   0x00000400  // so that one vis dummy can add to snapshot multiple speakers

// recent id changes
#define SVF_SINGLECLIENT        0x00000800  // only send to a single client (entityShared_t->singleClient)
#define SVF_NOSERVERINFO        0x00001000  // don't send CS_SERVERINFO updates to this client
											// so that it can be updated for ping tools without
											// lagging clients
#define SVF_NOTSINGLECLIENT     0x00002000  // send entity to everyone but one client
											// (entityShared_t->singleClient)

//===============================================================


typedef struct {
	entityState_t s;                // communicated by server to clients

	qboolean linked;                // qfalse if not in any good cluster
	int linkcount;

	int svFlags;                    // SVF_NOCLIENT, SVF_BROADCAST, etc
	int singleClient;               // only send to this client when SVF_SINGLECLIENT is set

	qboolean bmodel;                // if false, assume an explicit mins / maxs bounding box
									// only set by sys->SetBrushModel
	vec3_t mins, maxs;
	int contents;                   // CONTENTS_TRIGGER, CONTENTS_SOLID, CONTENTS_BODY, etc
									// a non-solid entity should set to 0

	vec3_t absmin, absmax;          // derived from mins/maxs and origin + rotation

	// currentOrigin will be used for all collision detection and world linking.
	// it will not necessarily be the same as the trajectory evaluation for the current
	// time, because each entity must be moved one at a time after time is advanced
	// to avoid simultanious collision issues
	vec3_t currentOrigin;
	vec3_t currentAngles;

	// when a trace call is made and passEntityNum != ENTITYNUM_NONE,
	// an ent will be excluded from testing if:
	// ent->s.number == passEntityNum	(don't interact with self)
	// ent->s.ownerNum = passEntityNum	(don't interact with your own missiles)
	// entity[ent->s.ownerNum].ownerNum = passEntityNum	(don't interact with other missiles from owner)
	int ownerNum;
	int eventTime;
} entityShared_t;



// the server looks at a sharedEntity, which is the start of the game's gentity_t structure
typedef struct {
	entityState_t s;                // communicated by server to clients
	entityShared_t r;               // shared by both the server system and game
} sharedEntity_t;

#ifdef GAMEDLL
typedef struct gentity_s gentity_t;
#else
#define gentity_t sharedEntity_t
#endif
typedef struct gclient_s gclient_t;

//===============================================================

class idGameSystemCalls {
public:
    virtual ~idGameSystemCalls() = default;

    // core
    virtual void Printf(const char* fmt) = 0;
    virtual void Error(const char* fmt) = 0;
    virtual void Endgame(void) = 0;
    virtual int Milliseconds(void) = 0;
    virtual int Argc(void) = 0;
    virtual void Argv(int n, char* buffer, int bufferLength) = 0;

    // filesystem
    virtual int FS_FOpenFile(const char* qpath, fileHandle_t* f, fsMode_t mode) = 0;
    virtual void FS_Read(void* buffer, int len, fileHandle_t f) = 0;
    virtual int FS_Write(const void* buffer, int len, fileHandle_t f) = 0;
    virtual int FS_Rename(const char* from, const char* to) = 0;
    virtual void FS_FCloseFile(fileHandle_t f) = 0;
    virtual void FS_CopyFile(char* from, char* to) = 0;
    virtual int FS_GetFileList(const char* path, const char* extension, char* listbuf, int bufsize) = 0;

    // console / cvars
    virtual void SendConsoleCommand(int exec_when, const char* text) = 0;
    virtual void Cvar_Register(vmCvar_t* cvar, const char* var_name, const char* value, int flags) = 0;
    virtual void Cvar_Update(vmCvar_t* cvar) = 0;
    virtual void Cvar_Set(const char* var_name, const char* value) = 0;
    virtual int Cvar_VariableIntegerValue(const char* var_name) = 0;
    virtual void Cvar_VariableStringBuffer(const char* var_name, char* buffer, int bufsize) = 0;

    // game state
    virtual void LocateGameData(gentity_t* gEnts, int numGEntities, int sizeofGEntity_t,
        playerState_t* clients, int sizeofGClient) = 0;
    virtual void DropClient(int clientNum, const char* reason) = 0;
    virtual void SendServerCommand(int clientNum, const char* text) = 0;
    virtual void SetConfigstring(int num, const char* string) = 0;
    virtual void GetConfigstring(int num, char* buffer, int bufferSize) = 0;
    virtual void GetUserinfo(int num, char* buffer, int bufferSize) = 0;
    virtual void SetUserinfo(int num, const char* buffer) = 0;
    virtual void GetServerinfo(char* buffer, int bufferSize) = 0;
    virtual void SetBrushModel(gentity_t* ent, const char* name) = 0;

    // collision / world
    virtual void Trace(trace_t* results, const vec3_t start, const vec3_t mins, const vec3_t maxs,
        const vec3_t end, int passEntityNum, int contentmask) = 0;
    virtual void TraceCapsule(trace_t* results, const vec3_t start, const vec3_t mins, const vec3_t maxs,
        const vec3_t end, int passEntityNum, int contentmask) = 0;
    virtual int PointContents(const vec3_t point, int passEntityNum) = 0;
    virtual qboolean InPVS(const vec3_t p1, const vec3_t p2) = 0;
    virtual qboolean InPVSIgnorePortals(const vec3_t p1, const vec3_t p2) = 0;
    virtual void AdjustAreaPortalState(gentity_t* ent, qboolean open) = 0;
    virtual qboolean AreasConnected(int area1, int area2) = 0;
    virtual void LinkEntity(gentity_t* ent) = 0;
    virtual void UnlinkEntity(gentity_t* ent) = 0;
    virtual int EntitiesInBox(const vec3_t mins, const vec3_t maxs, int* list, int maxcount) = 0;
    virtual qboolean EntityContact(const vec3_t mins, const vec3_t maxs, const gentity_t* ent) = 0;
    virtual qboolean EntityContactCapsule(const vec3_t mins, const vec3_t maxs, const gentity_t* ent) = 0;

    // clients / commands
    virtual int BotAllocateClient(void) = 0;
    virtual void BotFreeClient(int clientNum) = 0;
    virtual void GetUsercmd(int clientNum, usercmd_t* cmd) = 0;
    virtual qboolean GetEntityToken(char* buffer, int bufferSize) = 0;

    // debug / misc
    virtual int DebugPolygonCreate(int color, int numPoints, vec3_t* points) = 0;
    virtual void DebugPolygonDelete(int id) = 0;
    virtual int RealTime(qtime_t* qtime) = 0;
    virtual qboolean GetTag(int clientNum, char* tagName, orientation_t* or ) = 0;

    // botlib core
    virtual int BotLibSetup(void) = 0;
    virtual int BotLibShutdown(void) = 0;
    virtual int BotLibVarSet(char* var_name, char* value) = 0;
    virtual int BotLibVarGet(char* var_name, char* value, int size) = 0;
    virtual int BotLibDefine(char* string) = 0;
    virtual int BotLibStartFrame(float time) = 0;
    virtual int BotLibLoadMap(const char* mapname) = 0;
    virtual int BotLibUpdateEntity(int ent, void* bue) = 0;
    virtual int BotLibTest(int parm0, char* parm1, vec3_t parm2, vec3_t parm3) = 0;
    virtual int BotGetSnapshotEntity(int clientNum, int sequence) = 0;
    virtual int BotGetServerCommand(int clientNum, char* message, int size) = 0;
    virtual void BotUserCommand(int clientNum, usercmd_t* ucmd) = 0;

    // AAS
    virtual void AAS_EntityInfo(int entnum, void* info) = 0;
    virtual int AAS_Initialized(void) = 0;
    virtual void AAS_PresenceTypeBoundingBox(int presencetype, vec3_t mins, vec3_t maxs) = 0;
    virtual float AAS_Time(void) = 0;
    virtual void AAS_SetCurrentWorld(int index) = 0;
    virtual int AAS_PointAreaNum(vec3_t point) = 0;
    virtual int AAS_TraceAreas(vec3_t start, vec3_t end, int* areas, vec3_t* points, int maxareas) = 0;
    virtual int AAS_PointContents(vec3_t point) = 0;
    virtual int AAS_NextBSPEntity(int ent) = 0;
    virtual int AAS_ValueForBSPEpairKey(int ent, char* key, char* value, int size) = 0;
    virtual int AAS_VectorForBSPEpairKey(int ent, char* key, vec3_t v) = 0;
    virtual int AAS_FloatForBSPEpairKey(int ent, char* key, float* value) = 0;
    virtual int AAS_IntForBSPEpairKey(int ent, char* key, int* value) = 0;
    virtual int AAS_AreaReachability(int areanum) = 0;
    virtual int AAS_AreaTravelTimeToGoalArea(int areanum, vec3_t origin, int goalareanum, int travelflags) = 0;
    virtual int AAS_Swimming(vec3_t origin) = 0;
    virtual int AAS_PredictClientMovement(void* move, int entnum, vec3_t origin, int presencetype, int onground,
        vec3_t velocity, vec3_t cmdmove, int cmdframes, int maxframes,
        float frametime, int stopevent, int stopareanum, int visualize) = 0;
    virtual void AAS_RT_ShowRoute(vec3_t srcpos, int srcnum, int destnum) = 0;
    virtual qboolean AAS_RT_GetHidePos(vec3_t srcpos, int srcnum, int srcarea,
        vec3_t destpos, int destnum, int destarea, vec3_t returnPos) = 0;
    virtual int AAS_FindAttackSpotWithinRange(int srcnum, int rangenum, int enemynum, float rangedist,
        int travelflags, float* outpos) = 0;
    virtual qboolean AAS_GetRouteFirstVisPos(vec3_t srcpos, vec3_t destpos, int travelflags, vec3_t retpos) = 0;
    virtual void AAS_SetAASBlockingEntity(vec3_t absmin, vec3_t absmax, qboolean blocking) = 0;

    // elementary actions
    virtual void EA_Say(int client, char* str) = 0;
    virtual void EA_SayTeam(int client, char* str) = 0;
    virtual void EA_UseItem(int client, char* it) = 0;
    virtual void EA_DropItem(int client, char* it) = 0;
    virtual void EA_UseInv(int client, char* inv) = 0;
    virtual void EA_DropInv(int client, char* inv) = 0;
    virtual void EA_Gesture(int client) = 0;
    virtual void EA_Command(int client, char* command) = 0;
    virtual void EA_SelectWeapon(int client, int weapon) = 0;
    virtual void EA_Talk(int client) = 0;
    virtual void EA_Attack(int client) = 0;
    virtual void EA_Reload(int client) = 0;
    virtual void EA_Use(int client) = 0;
    virtual void EA_Respawn(int client) = 0;
    virtual void EA_Jump(int client) = 0;
    virtual void EA_DelayedJump(int client) = 0;
    virtual void EA_Crouch(int client) = 0;
    virtual void EA_MoveUp(int client) = 0;
    virtual void EA_MoveDown(int client) = 0;
    virtual void EA_MoveForward(int client) = 0;
    virtual void EA_MoveBack(int client) = 0;
    virtual void EA_MoveLeft(int client) = 0;
    virtual void EA_MoveRight(int client) = 0;
    virtual void EA_Move(int client, vec3_t dir, float speed) = 0;
    virtual void EA_View(int client, vec3_t viewangles) = 0;
    virtual void EA_EndRegular(int client, float thinktime) = 0;
    virtual void EA_GetInput(int client, float thinktime, void* input) = 0;
    virtual void EA_ResetInput(int client, void* init) = 0;

    // bot AI - character / chat
    virtual int BotLoadCharacter(char* charfile, int skill) = 0;
    virtual void BotFreeCharacter(int character) = 0;
    virtual float Characteristic_Float(int character, int index) = 0;
    virtual float Characteristic_BFloat(int character, int index, float min, float max) = 0;
    virtual int Characteristic_Integer(int character, int index) = 0;
    virtual int Characteristic_BInteger(int character, int index, int min, int max) = 0;
    virtual void Characteristic_String(int character, int index, char* buf, int size) = 0;
    virtual int BotAllocChatState(void) = 0;
    virtual void BotFreeChatState(int handle) = 0;
    virtual void BotQueueConsoleMessage(int chatstate, int type, char* message) = 0;
    virtual void BotRemoveConsoleMessage(int chatstate, int handle) = 0;
    virtual int BotNextConsoleMessage(int chatstate, void* cm) = 0;
    virtual int BotNumConsoleMessages(int chatstate) = 0;
    virtual void BotInitialChat(int chatstate, char* type, int mcontext,
        char* var0, char* var1, char* var2, char* var3,
        char* var4, char* var5, char* var6, char* var7) = 0;
    virtual int BotNumInitialChats(int chatstate, char* type) = 0;
    virtual int BotReplyChat(int chatstate, char* message, int mcontext, int vcontext,
        char* var0, char* var1, char* var2, char* var3,
        char* var4, char* var5, char* var6, char* var7) = 0;
    virtual int BotChatLength(int chatstate) = 0;
    virtual void BotEnterChat(int chatstate, int client, int sendto) = 0;
    virtual void BotGetChatMessage(int chatstate, char* buf, int size) = 0;
    virtual int StringContains(char* str1, char* str2, int casesensitive) = 0;
    virtual int BotFindMatch(char* str, void* match, unsigned long context) = 0;
    virtual void BotMatchVariable(void* match, int variable, char* buf, int size) = 0;
    virtual void UnifyWhiteSpaces(char* string) = 0;
    virtual void BotReplaceSynonyms(char* string, unsigned long context) = 0;
    virtual int BotLoadChatFile(int chatstate, char* chatfile, char* chatname) = 0;
    virtual void BotSetChatGender(int chatstate, int gender) = 0;
    virtual void BotSetChatName(int chatstate, char* name) = 0;

    // bot AI - goals
    virtual void BotResetGoalState(int goalstate) = 0;
    virtual void BotResetAvoidGoals(int goalstate) = 0;
    virtual void BotRemoveFromAvoidGoals(int goalstate, int number) = 0;
    virtual void BotPushGoal(int goalstate, void* goal) = 0;
    virtual void BotPopGoal(int goalstate) = 0;
    virtual void BotEmptyGoalStack(int goalstate) = 0;
    virtual void BotDumpAvoidGoals(int goalstate) = 0;
    virtual void BotDumpGoalStack(int goalstate) = 0;
    virtual void BotGoalName(int number, char* name, int size) = 0;
    virtual int BotGetTopGoal(int goalstate, void* goal) = 0;
    virtual int BotGetSecondGoal(int goalstate, void* goal) = 0;
    virtual int BotChooseLTGItem(int goalstate, vec3_t origin, int* inventory, int travelflags) = 0;
    virtual int BotChooseNBGItem(int goalstate, vec3_t origin, int* inventory, int travelflags, void* ltg, float maxtime) = 0;
    virtual int BotTouchingGoal(vec3_t origin, void* goal) = 0;
    virtual int BotItemGoalInVisButNotVisible(int viewer, vec3_t eye, vec3_t viewangles, void* goal) = 0;
    virtual int BotGetLevelItemGoal(int index, char* classname, void* goal) = 0;
    virtual int BotGetNextCampSpotGoal(int num, void* goal) = 0;
    virtual int BotGetMapLocationGoal(char* name, void* goal) = 0;
    virtual float BotAvoidGoalTime(int goalstate, int number) = 0;
    virtual void BotInitLevelItems(void) = 0;
    virtual void BotUpdateEntityItems(void) = 0;
    virtual int BotLoadItemWeights(int goalstate, char* filename) = 0;
    virtual void BotFreeItemWeights(int goalstate) = 0;
    virtual void BotInterbreedGoalFuzzyLogic(int parent1, int parent2, int child) = 0;
    virtual void BotSaveGoalFuzzyLogic(int goalstate, char* filename) = 0;
    virtual void BotMutateGoalFuzzyLogic(int goalstate, float range) = 0;
    virtual int BotAllocGoalState(int state) = 0;
    virtual void BotFreeGoalState(int handle) = 0;

    // bot AI - movement
    virtual void BotResetMoveState(int movestate) = 0;
    virtual void BotMoveToGoal(void* result, int movestate, void* goal, int travelflags) = 0;
    virtual int BotMoveInDirection(int movestate, vec3_t dir, float speed, int type) = 0;
    virtual void BotResetAvoidReach(int movestate) = 0;
    virtual void BotResetLastAvoidReach(int movestate) = 0;
    virtual int BotReachabilityArea(vec3_t origin, int testground) = 0;
    virtual int BotMovementViewTarget(int movestate, void* goal, int travelflags, float lookahead, vec3_t target) = 0;
    virtual int BotPredictVisiblePosition(vec3_t origin, int areanum, void* goal, int travelflags, vec3_t target) = 0;
    virtual int BotAllocMoveState(void) = 0;
    virtual void BotFreeMoveState(int handle) = 0;
    virtual void BotInitMoveState(int handle, void* initmove) = 0;
    virtual void BotInitAvoidReach(int handle) = 0;

    // bot AI - weapons
    virtual int BotChooseBestFightWeapon(int weaponstate, int* inventory) = 0;
    virtual void BotGetWeaponInfo(int weaponstate, int weapon, void* weaponinfo) = 0;
    virtual int BotLoadWeaponWeights(int weaponstate, char* filename) = 0;
    virtual int BotAllocWeaponState(void) = 0;
    virtual void BotFreeWeaponState(int weaponstate) = 0;
    virtual void BotResetWeaponState(int weaponstate) = 0;

    // genetic
    virtual int GeneticParentsAndChildSelection(int numranks, float* ranks, int* parent1, int* parent2, int* child) = 0;
};

#include "bg_public.h"

class idGameVM {
public:
    virtual ~idGameVM() = default;

    virtual void InitGame(int levelTime, int randomSeed, int restart) = 0;
    virtual void ShutdownGame(int restart) = 0;
    virtual char* ClientConnect(int clientNum, qboolean firstTime, qboolean isBot) = 0;
    virtual void ClientThink(int clientNum) = 0;
    virtual void ClientUserinfoChanged(int clientNum) = 0;
    virtual void ClientDisconnect(int clientNum) = 0;
    virtual void ClientBegin(int clientNum) = 0;
    virtual void ClientCommand(int clientNum) = 0;
    virtual void RunFrame(int levelTime) = 0;
    virtual int ConsoleCommand() = 0;
    virtual int BotAIStartFrame(int time) = 0;
    virtual int AICastVisibleFromPos(float* srcpos, int srcnum, float* destpos, int destnum, qboolean updateVisPos) = 0;
    virtual int AICastCheckAttackAtPos(int entnum, int enemy, float* pos, qboolean ducking, qboolean allowHitWorld) = 0;
    virtual void RetrieveMoveSpeedsFromClient(int entnum, char* text) = 0;
    virtual int GetModelInfo(int clientNum, char* modelName, animModelInfo_t** modelInfo) = 0;
};

extern idGameVM* gvm;

