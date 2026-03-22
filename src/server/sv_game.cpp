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

// sv_game.c -- interface to the game dll

#include "server.h"

#define MAX_STRINGFIELD             80
#include "../game/botlib.h"
#include "../game/be_ai_goal.h"
#include "../game/be_ai_move.h"
#include "../game/be_ai_weap.h"
#include "../game/be_ai_chat.h"
#include "../game/be_ai_char.h"
#include "../game/be_ai_gen.h"

static dllhandle_t vmHandle = NULL;
idGameVM* gvm = NULL;
botlib_export_t *botlib_export;

void SV_GameError( const char *string ) {
	Com_Error( ERR_DROP, "%s", string );
}

void SV_GamePrint( const char *string ) {
	Com_Printf( "%s", string );
}

// these functions must be used instead of pointer arithmetic, because
// the game allocates gentities with private information after the server shared part
int SV_NumForGentity( sharedEntity_t *ent ) {
	int num;

	num = ( (byte *)ent - (byte *)sv.gentities ) / sv.gentitySize;

	return num;
}

sharedEntity_t *SV_GentityNum( int num ) {
	sharedEntity_t *ent;

	ent = ( sharedEntity_t * )( (byte *)sv.gentities + sv.gentitySize * ( num ) );

	return ent;
}

playerState_t *SV_GameClientNum( int num ) {
	playerState_t   *ps;

	ps = ( playerState_t * )( (byte *)sv.gameClients + sv.gameClientSize * ( num ) );

	return ps;
}

svEntity_t  *SV_SvEntityForGentity( sharedEntity_t *gEnt ) {
	if ( !gEnt || gEnt->s.number < 0 || gEnt->s.number >= MAX_GENTITIES ) {
		Com_Error( ERR_DROP, "SV_SvEntityForGentity: bad gEnt" );
	}
	return &sv.svEntities[ gEnt->s.number ];
}

sharedEntity_t *SV_GEntityForSvEntity( svEntity_t *svEnt ) {
	int num;

	num = svEnt - sv.svEntities;
	return SV_GentityNum( num );
}

/*
===============
SV_GameSendServerCommand

Sends a command string to a client
===============
*/
void SV_GameSendServerCommand( int clientNum, const char *text ) {
	if ( clientNum == -1 ) {
		SV_SendServerCommand( NULL, "%s", text );
	} else {
		if ( clientNum < 0 || clientNum >= sv_maxclients->integer ) {
			return;
		}
		SV_SendServerCommand( svs.clients + clientNum, "%s", text );
	}
}


/*
===============
SV_GameDropClient

Disconnects the client with a message
===============
*/
void SV_GameDropClient( int clientNum, const char *reason ) {
	if ( clientNum < 0 || clientNum >= sv_maxclients->integer ) {
		return;
	}
	SV_DropClient( svs.clients + clientNum, reason );
}


/*
=================
SV_SetBrushModel

sets mins and maxs for inline bmodels
=================
*/
void SV_SetBrushModel( sharedEntity_t *ent, const char *name ) {
	clipHandle_t h;
	vec3_t mins, maxs;

	if ( !name ) {
		Com_Error( ERR_DROP, "SV_SetBrushModel: NULL" );
	}

	if ( name[0] != '*' ) {
		Com_Error( ERR_DROP, "SV_SetBrushModel: %s isn't a brush model", name );
	}


	ent->s.modelindex = atoi( name + 1 );

	h = CM_InlineModel( ent->s.modelindex );
	CM_ModelBounds( h, mins, maxs );
	VectorCopy( mins, ent->r.mins );
	VectorCopy( maxs, ent->r.maxs );
	ent->r.bmodel = qtrue;

	ent->r.contents = -1;       // we don't know exactly what is in the brushes

	SV_LinkEntity( ent );       // FIXME: remove
}



/*
=================
SV_inPVS

Also checks portalareas so that doors block sight
=================
*/
qboolean SV_inPVS( const vec3_t p1, const vec3_t p2 ) {
	int leafnum;
	int cluster;
	int area1, area2;
	byte    *mask;

	leafnum = CM_PointLeafnum( p1 );
	cluster = CM_LeafCluster( leafnum );
	area1 = CM_LeafArea( leafnum );
	mask = CM_ClusterPVS( cluster );

	leafnum = CM_PointLeafnum( p2 );
	cluster = CM_LeafCluster( leafnum );
	area2 = CM_LeafArea( leafnum );
	if ( mask && ( !( mask[cluster >> 3] & ( 1 << ( cluster & 7 ) ) ) ) ) {
		return qfalse;
	}
	if ( !CM_AreasConnected( area1, area2 ) ) {
		return qfalse;      // a door blocks sight
	}
	return qtrue;
}


/*
=================
SV_inPVSIgnorePortals

Does NOT check portalareas
=================
*/
qboolean SV_inPVSIgnorePortals( const vec3_t p1, const vec3_t p2 ) {
	int leafnum;
	int cluster;
	int area1, area2;
	byte    *mask;

	leafnum = CM_PointLeafnum( p1 );
	cluster = CM_LeafCluster( leafnum );
	area1 = CM_LeafArea( leafnum );
	mask = CM_ClusterPVS( cluster );

	leafnum = CM_PointLeafnum( p2 );
	cluster = CM_LeafCluster( leafnum );
	area2 = CM_LeafArea( leafnum );

	if ( mask && ( !( mask[cluster >> 3] & ( 1 << ( cluster & 7 ) ) ) ) ) {
		return qfalse;
	}

	return qtrue;
}


/*
========================
SV_AdjustAreaPortalState
========================
*/
void SV_AdjustAreaPortalState( sharedEntity_t *ent, qboolean open ) {
	svEntity_t  *svEnt;

	svEnt = SV_SvEntityForGentity( ent );
	if ( svEnt->areanum2 == -1 ) {
		return;
	}
	CM_AdjustAreaPortalState( svEnt->areanum, svEnt->areanum2, open );
}


/*
==================
SV_GameAreaEntities
==================
*/
qboolean    SV_EntityContact( const vec3_t mins, const vec3_t maxs, const sharedEntity_t *gEnt, const int capsule ) {
	const float *origin, *angles;
	clipHandle_t ch;
	trace_t trace;

	// check for exact collision
	origin = gEnt->r.currentOrigin;
	angles = gEnt->r.currentAngles;

	ch = SV_ClipHandleForEntity( gEnt );
	CM_TransformedBoxTrace( &trace, vec3_origin, vec3_origin, mins, maxs,
							ch, -1, origin, angles, capsule );

	return trace.startsolid;
}


/*
===============
SV_GetServerinfo

===============
*/
void SV_GetServerinfo( char *buffer, int bufferSize ) {
	if ( bufferSize < 1 ) {
		Com_Error( ERR_DROP, "SV_GetServerinfo: bufferSize == %i", bufferSize );
	}
	Q_strncpyz( buffer, Cvar_InfoString( CVAR_SERVERINFO ), bufferSize );
}

/*
===============
SV_LocateGameData

===============
*/
void SV_LocateGameData( sharedEntity_t *gEnts, int numGEntities, int sizeofGEntity_t,
						playerState_t *clients, int sizeofGameClient ) {
	sv.gentities = gEnts;
	sv.gentitySize = sizeofGEntity_t;
	sv.num_entities = numGEntities;

	sv.gameClients = clients;
	sv.gameClientSize = sizeofGameClient;
}


/*
===============
SV_GetUsercmd

===============
*/
void SV_GetUsercmd( int clientNum, usercmd_t *cmd ) {
	if ( clientNum < 0 || clientNum >= sv_maxclients->integer ) {
		Com_Error( ERR_DROP, "SV_GetUsercmd: bad clientNum:%i", clientNum );
	}
	*cmd = svs.clients[clientNum].lastUsercmd;
}

//==============================================
class idGameSystemLocal : public idGameSystemCalls {
public:
    virtual ~idGameSystemLocal() override = default;

    // core
    virtual void Printf(const char* fmt) override {
        ::Com_Printf("%s", fmt);
    }

    virtual void Error(const char* fmt) override {
        ::Com_Error(ERR_DROP, "%s", fmt);
    }

    virtual void Endgame(void) override {
        ::Com_Error(ERR_ENDGAME, "endgame");
    }

    virtual int Milliseconds(void) override {
        return ::Sys_Milliseconds();
    }

    virtual int Argc(void) override {
        return ::Cmd_Argc();
    }

    virtual void Argv(int n, char* buffer, int bufferLength) override {
        ::Cmd_ArgvBuffer(n, buffer, bufferLength);
    }

    // filesystem
    virtual int FS_FOpenFile(const char* qpath, fileHandle_t* f, fsMode_t mode) override {
        return ::FS_FOpenFileByMode(qpath, f, mode);
    }

    virtual void FS_Read(void* buffer, int len, fileHandle_t f) override {
        ::FS_Read(buffer, len, f);
    }

    virtual int FS_Write(const void* buffer, int len, fileHandle_t f) override {
        return ::FS_Write(buffer, len, f);
    }

    virtual int FS_Rename(const char* from, const char* to) override {
        ::FS_Rename(from, to);
        return 0;
    }

    virtual void FS_FCloseFile(fileHandle_t f) override {
        ::FS_FCloseFile(f);
    }

    virtual void FS_CopyFile(char* from, char* to) override {
        ::FS_CopyFileOS(from, to);
    }

    virtual int FS_GetFileList(const char* path, const char* extension, char* listbuf, int bufsize) override {
        return ::FS_GetFileList(path, extension, listbuf, bufsize);
    }

    // console / cvars
    virtual void SendConsoleCommand(int exec_when, const char* text) override {
        ::Cbuf_ExecuteText(exec_when, text);
    }

    virtual void Cvar_Register(vmCvar_t* cvar, const char* var_name, const char* value, int flags) override {
        ::Cvar_Register(cvar, var_name, value, flags);
    }

    virtual void Cvar_Update(vmCvar_t* cvar) override {
        ::Cvar_Update(cvar);
    }

    virtual void Cvar_Set(const char* var_name, const char* value) override {
        ::Cvar_Set(var_name, value);
    }

    virtual int Cvar_VariableIntegerValue(const char* var_name) override {
        return ::Cvar_VariableIntegerValue(var_name);
    }

    virtual void Cvar_VariableStringBuffer(const char* var_name, char* buffer, int bufsize) override {
        ::Cvar_VariableStringBuffer(var_name, buffer, bufsize);
    }

    // game state
    virtual void LocateGameData(gentity_t* gEnts, int numGEntities, int sizeofGEntity_t,
        playerState_t* clients, int sizeofGClient) override {
        ::SV_LocateGameData(gEnts, numGEntities, sizeofGEntity_t, clients, sizeofGClient);
    }

    virtual void DropClient(int clientNum, const char* reason) override {
        ::SV_GameDropClient(clientNum, reason);
    }

    virtual void SendServerCommand(int clientNum, const char* text) override {
        ::SV_GameSendServerCommand(clientNum, text);
    }

    virtual void SetConfigstring(int num, const char* string) override {
        ::SV_SetConfigstring(num, string);
    }

    virtual void GetConfigstring(int num, char* buffer, int bufferSize) override {
        ::SV_GetConfigstring(num, buffer, bufferSize);
    }

    virtual void GetUserinfo(int num, char* buffer, int bufferSize) override {
        ::SV_GetUserinfo(num, buffer, bufferSize);
    }

    virtual void SetUserinfo(int num, const char* buffer) override {
        ::SV_SetUserinfo(num, buffer);
    }

    virtual void GetServerinfo(char* buffer, int bufferSize) override {
        ::SV_GetServerinfo(buffer, bufferSize);
    }

    virtual void SetBrushModel(gentity_t* ent, const char* name) override {
        ::SV_SetBrushModel(ent, name);
    }

    // collision / world
    virtual void Trace(trace_t* results, const vec3_t start, const vec3_t mins, const vec3_t maxs,
        const vec3_t end, int passEntityNum, int contentmask) override {
        ::SV_Trace(results, start, mins, maxs, end, passEntityNum, contentmask, qfalse);
    }

    virtual void TraceCapsule(trace_t* results, const vec3_t start, const vec3_t mins, const vec3_t maxs,
        const vec3_t end, int passEntityNum, int contentmask) override {
        ::SV_Trace(results, start, mins, maxs, end, passEntityNum, contentmask, qtrue);
    }

    virtual int PointContents(const vec3_t point, int passEntityNum) override {
        return ::SV_PointContents(point, passEntityNum);
    }

    virtual qboolean InPVS(const vec3_t p1, const vec3_t p2) override {
        return ::SV_inPVS(p1, p2);
    }

    virtual qboolean InPVSIgnorePortals(const vec3_t p1, const vec3_t p2) override {
        return ::SV_inPVSIgnorePortals(p1, p2);
    }

    virtual void AdjustAreaPortalState(gentity_t* ent, qboolean open) override {
        ::SV_AdjustAreaPortalState(ent, open);
    }

    virtual qboolean AreasConnected(int area1, int area2) override {
        return ::CM_AreasConnected(area1, area2);
    }

    virtual void LinkEntity(gentity_t* ent) override {
        ::SV_LinkEntity(ent);
    }

    virtual void UnlinkEntity(gentity_t* ent) override {
        ::SV_UnlinkEntity(ent);
    }

    virtual int EntitiesInBox(const vec3_t mins, const vec3_t maxs, int* list, int maxcount) override {
        return ::SV_AreaEntities(mins, maxs, list, maxcount);
    }

    virtual qboolean EntityContact(const vec3_t mins, const vec3_t maxs, const gentity_t* ent) override {
        return ::SV_EntityContact(mins, maxs, ent, qfalse);
    }

    virtual qboolean EntityContactCapsule(const vec3_t mins, const vec3_t maxs, const gentity_t* ent) override {
        return ::SV_EntityContact(mins, maxs, ent, qtrue);
    }

    // clients / commands
    virtual int BotAllocateClient(void) override {
        return ::SV_BotAllocateClient();
    }

    virtual void BotFreeClient(int clientNum) override {
        ::SV_BotFreeClient(clientNum);
    }

    virtual void GetUsercmd(int clientNum, usercmd_t* cmd) override {
        ::SV_GetUsercmd(clientNum, cmd);
    }

    virtual qboolean GetEntityToken(char* buffer, int bufferSize) override {
        const char* s = ::COM_Parse(&sv.entityParsePoint);
        ::Q_strncpyz(buffer, s, bufferSize);
        if (!sv.entityParsePoint && !s[0]) {
            return qfalse;
        }
        return qtrue;
    }

    // debug / misc
    virtual int DebugPolygonCreate(int color, int numPoints, vec3_t* points) override {
        return ::BotImport_DebugPolygonCreate(color, numPoints, points);
    }

    virtual void DebugPolygonDelete(int id) override {
        ::BotImport_DebugPolygonDelete(id);
    }

    virtual int RealTime(qtime_t* qtime) override {
        return ::Com_RealTime(qtime);
    }

    virtual qboolean GetTag(int clientNum, char* tagName, orientation_t* or_) override {
        return ::SV_GetTag(clientNum, tagName, or_);
    }

    // botlib core
    virtual int BotLibSetup(void) override {
        return ::SV_BotLibSetup();
    }

    virtual int BotLibShutdown(void) override {
        return ::SV_BotLibShutdown();
    }

    virtual int BotLibVarSet(char* var_name, char* value) override {
        return ::botlib_export->BotLibVarSet(var_name, value);
    }

    virtual int BotLibVarGet(char* var_name, char* value, int size) override {
        return ::botlib_export->BotLibVarGet(var_name, value, size);
    }

    virtual int BotLibDefine(char* string) override {
        return ::botlib_export->PC_AddGlobalDefine(string);
    }

    virtual int BotLibStartFrame(float time) override {
        return ::botlib_export->BotLibStartFrame(time);
    }

    virtual int BotLibLoadMap(const char* mapname) override {
        return ::botlib_export->BotLibLoadMap(mapname);
    }

    virtual int BotLibUpdateEntity(int ent, void* bue) override {
        return ::botlib_export->BotLibUpdateEntity(ent, static_cast<bot_entitystate_t*>(bue));
    }

    virtual int BotLibTest(int parm0, char* parm1, vec3_t parm2, vec3_t parm3) override {
        return ::botlib_export->Test(parm0, parm1, parm2, parm3);
    }

    virtual int BotGetSnapshotEntity(int clientNum, int sequence) override {
        return ::SV_BotGetSnapshotEntity(clientNum, sequence);
    }

    virtual int BotGetServerCommand(int clientNum, char* message, int size) override {
        return ::SV_BotGetConsoleMessage(clientNum, message, size);
    }

    virtual void BotUserCommand(int clientNum, usercmd_t* ucmd) override {
        ::SV_ClientThink(&svs.clients[clientNum], ucmd);
    }

    // AAS
    virtual void AAS_EntityInfo(int entnum, void* info) override {
        ::botlib_export->aas.AAS_EntityInfo(entnum, (aas_entityinfo_s * )info);
    }

    virtual int AAS_Initialized(void) override {
        return ::botlib_export->aas.AAS_Initialized();
    }

    virtual void AAS_PresenceTypeBoundingBox(int presencetype, vec3_t mins, vec3_t maxs) override {
        ::botlib_export->aas.AAS_PresenceTypeBoundingBox(presencetype, mins, maxs);
    }

    virtual float AAS_Time(void) override {
        return ::botlib_export->aas.AAS_Time();
    }

    virtual void AAS_SetCurrentWorld(int index) override {
        ::botlib_export->aas.AAS_SetCurrentWorld(index);
    }

    virtual int AAS_PointAreaNum(vec3_t point) override {
        return ::botlib_export->aas.AAS_PointAreaNum(point);
    }

    virtual int AAS_TraceAreas(vec3_t start, vec3_t end, int* areas, vec3_t* points, int maxareas) override {
        return ::botlib_export->aas.AAS_TraceAreas(start, end, areas, points, maxareas);
    }

    virtual int AAS_PointContents(vec3_t point) override {
        return ::botlib_export->aas.AAS_PointContents(point);
    }

    virtual int AAS_NextBSPEntity(int ent) override {
        return ::botlib_export->aas.AAS_NextBSPEntity(ent);
    }

    virtual int AAS_ValueForBSPEpairKey(int ent, char* key, char* value, int size) override {
        return ::botlib_export->aas.AAS_ValueForBSPEpairKey(ent, key, value, size);
    }

    virtual int AAS_VectorForBSPEpairKey(int ent, char* key, vec3_t v) override {
        return ::botlib_export->aas.AAS_VectorForBSPEpairKey(ent, key, v);
    }

    virtual int AAS_FloatForBSPEpairKey(int ent, char* key, float* value) override {
        return ::botlib_export->aas.AAS_FloatForBSPEpairKey(ent, key, value);
    }

    virtual int AAS_IntForBSPEpairKey(int ent, char* key, int* value) override {
        return ::botlib_export->aas.AAS_IntForBSPEpairKey(ent, key, value);
    }

    virtual int AAS_AreaReachability(int areanum) override {
        return ::botlib_export->aas.AAS_AreaReachability(areanum);
    }

    virtual int AAS_AreaTravelTimeToGoalArea(int areanum, vec3_t origin, int goalareanum, int travelflags) override {
        return ::botlib_export->aas.AAS_AreaTravelTimeToGoalArea(areanum, origin, goalareanum, travelflags);
    }

    virtual int AAS_Swimming(vec3_t origin) override {
        return ::botlib_export->aas.AAS_Swimming(origin);
    }

    virtual int AAS_PredictClientMovement(
        void* move,
        int entnum,
        vec3_t origin,
        int presencetype,
        int onground,
        vec3_t velocity,
        vec3_t cmdmove,
        int cmdframes,
        int maxframes,
        float frametime,
        int stopevent,
        int stopareanum,
        int visualize) override {
        return ::botlib_export->aas.AAS_PredictClientMovement(
            static_cast<aas_clientmove_s*>(move),
            entnum,
            origin,
            presencetype,
            onground,
            velocity,
            cmdmove,
            cmdframes,
            maxframes,
            frametime,
            stopevent,
            stopareanum,
            visualize);
    }

    virtual void AAS_RT_ShowRoute(vec3_t srcpos, int srcnum, int destnum) override {
        ::botlib_export->aas.AAS_RT_ShowRoute(srcpos, srcnum, destnum);
    }

    virtual qboolean AAS_RT_GetHidePos(
        vec3_t srcpos,
        int srcnum,
        int srcarea,
        vec3_t destpos,
        int destnum,
        int destarea,
        vec3_t returnPos) override {
        return ::botlib_export->aas.AAS_RT_GetHidePos(
            srcpos, srcnum, srcarea, destpos, destnum, destarea, returnPos);
    }

    virtual int AAS_FindAttackSpotWithinRange(
        int srcnum,
        int rangenum,
        int enemynum,
        float rangedist,
        int travelflags,
        float* outpos) override {
        return ::botlib_export->aas.AAS_FindAttackSpotWithinRange(
            srcnum, rangenum, enemynum, rangedist, travelflags, outpos);
    }

    virtual qboolean AAS_GetRouteFirstVisPos(vec3_t srcpos, vec3_t destpos, int travelflags, vec3_t retpos) override {
        return ::botlib_export->aas.AAS_GetRouteFirstVisPos(srcpos, destpos, travelflags, retpos);
    }

    virtual void AAS_SetAASBlockingEntity(vec3_t absmin, vec3_t absmax, qboolean blocking) override {
        ::botlib_export->aas.AAS_SetAASBlockingEntity(absmin, absmax, blocking);
    }

    // elementary actions
    virtual void EA_Say(int client, char* str) override {
        ::botlib_export->ea.EA_Say(client, str);
    }

    virtual void EA_SayTeam(int client, char* str) override {
        ::botlib_export->ea.EA_SayTeam(client, str);
    }

    virtual void EA_UseItem(int client, char* it) override {
        ::botlib_export->ea.EA_UseItem(client, it);
    }

    virtual void EA_DropItem(int client, char* it) override {
        ::botlib_export->ea.EA_DropItem(client, it);
    }

    virtual void EA_UseInv(int client, char* inv) override {
        ::botlib_export->ea.EA_UseInv(client, inv);
    }

    virtual void EA_DropInv(int client, char* inv) override {
        ::botlib_export->ea.EA_DropInv(client, inv);
    }

    virtual void EA_Gesture(int client) override {
        ::botlib_export->ea.EA_Gesture(client);
    }

    virtual void EA_Command(int client, char* command) override {
        ::botlib_export->ea.EA_Command(client, command);
    }

    virtual void EA_SelectWeapon(int client, int weapon) override {
        ::botlib_export->ea.EA_SelectWeapon(client, weapon);
    }

    virtual void EA_Talk(int client) override {
        ::botlib_export->ea.EA_Talk(client);
    }

    virtual void EA_Attack(int client) override {
        ::botlib_export->ea.EA_Attack(client);
    }

    virtual void EA_Reload(int client) override {
        ::botlib_export->ea.EA_Reload(client);
    }

    virtual void EA_Use(int client) override {
        ::botlib_export->ea.EA_Use(client);
    }

    virtual void EA_Respawn(int client) override {
        ::botlib_export->ea.EA_Respawn(client);
    }

    virtual void EA_Jump(int client) override {
        ::botlib_export->ea.EA_Jump(client);
    }

    virtual void EA_DelayedJump(int client) override {
        ::botlib_export->ea.EA_DelayedJump(client);
    }

    virtual void EA_Crouch(int client) override {
        ::botlib_export->ea.EA_Crouch(client);
    }

    virtual void EA_MoveUp(int client) override {
        ::botlib_export->ea.EA_MoveUp(client);
    }

    virtual void EA_MoveDown(int client) override {
        ::botlib_export->ea.EA_MoveDown(client);
    }

    virtual void EA_MoveForward(int client) override {
        ::botlib_export->ea.EA_MoveForward(client);
    }

    virtual void EA_MoveBack(int client) override {
        ::botlib_export->ea.EA_MoveBack(client);
    }

    virtual void EA_MoveLeft(int client) override {
        ::botlib_export->ea.EA_MoveLeft(client);
    }

    virtual void EA_MoveRight(int client) override {
        ::botlib_export->ea.EA_MoveRight(client);
    }

    virtual void EA_Move(int client, vec3_t dir, float speed) override {
        ::botlib_export->ea.EA_Move(client, dir, speed);
    }

    virtual void EA_View(int client, vec3_t viewangles) override {
        ::botlib_export->ea.EA_View(client, viewangles);
    }

    virtual void EA_EndRegular(int client, float thinktime) override {
        ::botlib_export->ea.EA_EndRegular(client, thinktime);
    }

    virtual void EA_GetInput(int client, float thinktime, void* input) override {
        ::botlib_export->ea.EA_GetInput(client, thinktime, static_cast<bot_input_t*>(input));
    }

    virtual void EA_ResetInput(int client, void* init) override {
        ::botlib_export->ea.EA_ResetInput(client, static_cast<bot_input_t*>(init));
    }

    // bot AI - character / chat
    virtual int BotLoadCharacter(char* charfile, int skill) override {
        return ::botlib_export->ai.BotLoadCharacter(charfile, skill);
    }

    virtual void BotFreeCharacter(int character) override {
        ::botlib_export->ai.BotFreeCharacter(character);
    }

    virtual float Characteristic_Float(int character, int index) override {
        return ::botlib_export->ai.Characteristic_Float(character, index);
    }

    virtual float Characteristic_BFloat(int character, int index, float min, float max) override {
        return ::botlib_export->ai.Characteristic_BFloat(character, index, min, max);
    }

    virtual int Characteristic_Integer(int character, int index) override {
        return ::botlib_export->ai.Characteristic_Integer(character, index);
    }

    virtual int Characteristic_BInteger(int character, int index, int min, int max) override {
        return ::botlib_export->ai.Characteristic_BInteger(character, index, min, max);
    }

    virtual void Characteristic_String(int character, int index, char* buf, int size) override {
        ::botlib_export->ai.Characteristic_String(character, index, buf, size);
    }

    virtual int BotAllocChatState(void) override {
        return ::botlib_export->ai.BotAllocChatState();
    }

    virtual void BotFreeChatState(int handle) override {
        ::botlib_export->ai.BotFreeChatState(handle);
    }

    virtual void BotQueueConsoleMessage(int chatstate, int type, char* message) override {
        ::botlib_export->ai.BotQueueConsoleMessage(chatstate, type, message);
    }

    virtual void BotRemoveConsoleMessage(int chatstate, int handle) override {
        ::botlib_export->ai.BotRemoveConsoleMessage(chatstate, handle);
    }

    virtual int BotNextConsoleMessage(int chatstate, void* cm) override {
        return ::botlib_export->ai.BotNextConsoleMessage(chatstate, static_cast<bot_consolemessage_t*>(cm));
    }

    virtual int BotNumConsoleMessages(int chatstate) override {
        return ::botlib_export->ai.BotNumConsoleMessages(chatstate);
    }

    virtual void BotInitialChat(
        int chatstate,
        char* type,
        int mcontext,
        char* var0,
        char* var1,
        char* var2,
        char* var3,
        char* var4,
        char* var5,
        char* var6,
        char* var7) override {
        ::botlib_export->ai.BotInitialChat(
            chatstate, type, mcontext, var0, var1, var2, var3, var4, var5, var6, var7);
    }

    virtual int BotNumInitialChats(int chatstate, char* type) override {
        return ::botlib_export->ai.BotNumInitialChats(chatstate, type);
    }

    virtual int BotReplyChat(
        int chatstate,
        char* message,
        int mcontext,
        int vcontext,
        char* var0,
        char* var1,
        char* var2,
        char* var3,
        char* var4,
        char* var5,
        char* var6,
        char* var7) override {
        return ::botlib_export->ai.BotReplyChat(
            chatstate, message, mcontext, vcontext,
            var0, var1, var2, var3, var4, var5, var6, var7);
    }

    virtual int BotChatLength(int chatstate) override {
        return ::botlib_export->ai.BotChatLength(chatstate);
    }

    virtual void BotEnterChat(int chatstate, int client, int sendto) override {
        ::botlib_export->ai.BotEnterChat(chatstate, client, sendto);
    }

    virtual void BotGetChatMessage(int chatstate, char* buf, int size) override {
        ::botlib_export->ai.BotGetChatMessage(chatstate, buf, size);
    }

    virtual int StringContains(char* str1, char* str2, int casesensitive) override {
        return ::botlib_export->ai.StringContains(str1, str2, casesensitive);
    }

    virtual int BotFindMatch(char* str, void* match, unsigned long context) override {
        return ::botlib_export->ai.BotFindMatch(str, static_cast<bot_match_t*>(match), context);
    }

    virtual void BotMatchVariable(void* match, int variable, char* buf, int size) override {
        ::botlib_export->ai.BotMatchVariable(static_cast<bot_match_t*>(match), variable, buf, size);
    }

    virtual void UnifyWhiteSpaces(char* string) override {
        ::botlib_export->ai.UnifyWhiteSpaces(string);
    }

    virtual void BotReplaceSynonyms(char* string, unsigned long context) override {
        ::botlib_export->ai.BotReplaceSynonyms(string, context);
    }

    virtual int BotLoadChatFile(int chatstate, char* chatfile, char* chatname) override {
        return ::botlib_export->ai.BotLoadChatFile(chatstate, chatfile, chatname);
    }

    virtual void BotSetChatGender(int chatstate, int gender) override {
        ::botlib_export->ai.BotSetChatGender(chatstate, gender);
    }

    virtual void BotSetChatName(int chatstate, char* name) override {
        ::botlib_export->ai.BotSetChatName(chatstate, name);
    }

    // bot AI - goals
    virtual void BotResetGoalState(int goalstate) override {
        ::botlib_export->ai.BotResetGoalState(goalstate);
    }

    virtual void BotResetAvoidGoals(int goalstate) override {
        ::botlib_export->ai.BotResetAvoidGoals(goalstate);
    }

    virtual void BotRemoveFromAvoidGoals(int goalstate, int number) override {
        ::botlib_export->ai.BotRemoveFromAvoidGoals(goalstate, number);
    }

    virtual void BotPushGoal(int goalstate, void* goal) override {
        ::botlib_export->ai.BotPushGoal(goalstate, static_cast<bot_goal_t*>(goal));
    }

    virtual void BotPopGoal(int goalstate) override {
        ::botlib_export->ai.BotPopGoal(goalstate);
    }

    virtual void BotEmptyGoalStack(int goalstate) override {
        ::botlib_export->ai.BotEmptyGoalStack(goalstate);
    }

    virtual void BotDumpAvoidGoals(int goalstate) override {
        ::botlib_export->ai.BotDumpAvoidGoals(goalstate);
    }

    virtual void BotDumpGoalStack(int goalstate) override {
        ::botlib_export->ai.BotDumpGoalStack(goalstate);
    }

    virtual void BotGoalName(int number, char* name, int size) override {
        ::botlib_export->ai.BotGoalName(number, name, size);
    }

    virtual int BotGetTopGoal(int goalstate, void* goal) override {
        return ::botlib_export->ai.BotGetTopGoal(goalstate, static_cast<bot_goal_t*>(goal));
    }

    virtual int BotGetSecondGoal(int goalstate, void* goal) override {
        return ::botlib_export->ai.BotGetSecondGoal(goalstate, static_cast<bot_goal_t*>(goal));
    }

    virtual int BotChooseLTGItem(int goalstate, vec3_t origin, int* inventory, int travelflags) override {
        return ::botlib_export->ai.BotChooseLTGItem(goalstate, origin, inventory, travelflags);
    }

    virtual int BotChooseNBGItem(
        int goalstate,
        vec3_t origin,
        int* inventory,
        int travelflags,
        void* ltg,
        float maxtime) override {
        return ::botlib_export->ai.BotChooseNBGItem(
            goalstate, origin, inventory, travelflags, static_cast<bot_goal_t*>(ltg), maxtime);
    }

    virtual int BotTouchingGoal(vec3_t origin, void* goal) override {
        return ::botlib_export->ai.BotTouchingGoal(origin, static_cast<bot_goal_t*>(goal));
    }

    virtual int BotItemGoalInVisButNotVisible(
        int viewer,
        vec3_t eye,
        vec3_t viewangles,
        void* goal) override {
        return ::botlib_export->ai.BotItemGoalInVisButNotVisible(
            viewer, eye, viewangles, static_cast<bot_goal_t*>(goal));
    }

    virtual int BotGetLevelItemGoal(int index, char* classname, void* goal) override {
        return ::botlib_export->ai.BotGetLevelItemGoal(index, classname, static_cast<bot_goal_t*>(goal));
    }

    virtual int BotGetNextCampSpotGoal(int num, void* goal) override {
        return ::botlib_export->ai.BotGetNextCampSpotGoal(num, static_cast<bot_goal_t*>(goal));
    }

    virtual int BotGetMapLocationGoal(char* name, void* goal) override {
        return ::botlib_export->ai.BotGetMapLocationGoal(name, static_cast<bot_goal_t*>(goal));
    }

    virtual float BotAvoidGoalTime(int goalstate, int number) override {
        return ::botlib_export->ai.BotAvoidGoalTime(goalstate, number);
    }

    virtual void BotInitLevelItems(void) override {
        ::botlib_export->ai.BotInitLevelItems();
    }

    virtual void BotUpdateEntityItems(void) override {
        ::botlib_export->ai.BotUpdateEntityItems();
    }

    virtual int BotLoadItemWeights(int goalstate, char* filename) override {
        return ::botlib_export->ai.BotLoadItemWeights(goalstate, filename);
    }

    virtual void BotFreeItemWeights(int goalstate) override {
        ::botlib_export->ai.BotFreeItemWeights(goalstate);
    }

    virtual void BotInterbreedGoalFuzzyLogic(int parent1, int parent2, int child) override {
        ::botlib_export->ai.BotInterbreedGoalFuzzyLogic(parent1, parent2, child);
    }

    virtual void BotSaveGoalFuzzyLogic(int goalstate, char* filename) override {
        ::botlib_export->ai.BotSaveGoalFuzzyLogic(goalstate, filename);
    }

    virtual void BotMutateGoalFuzzyLogic(int goalstate, float range) override {
        ::botlib_export->ai.BotMutateGoalFuzzyLogic(goalstate, range);
    }

    virtual int BotAllocGoalState(int state) override {
        return ::botlib_export->ai.BotAllocGoalState(state);
    }

    virtual void BotFreeGoalState(int handle) override {
        ::botlib_export->ai.BotFreeGoalState(handle);
    }

    // bot AI - movement
    virtual void BotResetMoveState(int movestate) override {
        ::botlib_export->ai.BotResetMoveState(movestate);
    }

    virtual void BotMoveToGoal(void* result, int movestate, void* goal, int travelflags) override {
        ::botlib_export->ai.BotMoveToGoal(
            static_cast<bot_moveresult_t*>(result),
            movestate,
            static_cast<bot_goal_t*>(goal),
            travelflags);
    }

    virtual int BotMoveInDirection(int movestate, vec3_t dir, float speed, int type) override {
        return ::botlib_export->ai.BotMoveInDirection(movestate, dir, speed, type);
    }

    virtual void BotResetAvoidReach(int movestate) override {
        ::botlib_export->ai.BotResetAvoidReach(movestate);
    }

    virtual void BotResetLastAvoidReach(int movestate) override {
        ::botlib_export->ai.BotResetLastAvoidReach(movestate);
    }

    virtual int BotReachabilityArea(vec3_t origin, int testground) override {
        return ::botlib_export->ai.BotReachabilityArea(origin, testground);
    }

    virtual int BotMovementViewTarget(
        int movestate,
        void* goal,
        int travelflags,
        float lookahead,
        vec3_t target) override {
        return ::botlib_export->ai.BotMovementViewTarget(
            movestate,
            static_cast<bot_goal_t*>(goal),
            travelflags,
            lookahead,
            target);
    }

    virtual int BotPredictVisiblePosition(
        vec3_t origin,
        int areanum,
        void* goal,
        int travelflags,
        vec3_t target) override {
        return ::botlib_export->ai.BotPredictVisiblePosition(
            origin,
            areanum,
            static_cast<bot_goal_t*>(goal),
            travelflags,
            target);
    }

    virtual int BotAllocMoveState(void) override {
        return ::botlib_export->ai.BotAllocMoveState();
    }

    virtual void BotFreeMoveState(int handle) override {
        ::botlib_export->ai.BotFreeMoveState(handle);
    }

    virtual void BotInitMoveState(int handle, void* initmove) override {
        ::botlib_export->ai.BotInitMoveState(handle, static_cast<bot_initmove_t*>(initmove));
    }

    virtual void BotInitAvoidReach(int handle) override {
        ::botlib_export->ai.BotInitAvoidReach(handle);
    }

    // bot AI - weapons
    virtual int BotChooseBestFightWeapon(int weaponstate, int* inventory) override {
        return ::botlib_export->ai.BotChooseBestFightWeapon(weaponstate, inventory);
    }

    virtual void BotGetWeaponInfo(int weaponstate, int weapon, void* weaponinfo) override {
        ::botlib_export->ai.BotGetWeaponInfo(weaponstate, weapon, static_cast<weaponinfo_t*>(weaponinfo));
    }

    virtual int BotLoadWeaponWeights(int weaponstate, char* filename) override {
        return ::botlib_export->ai.BotLoadWeaponWeights(weaponstate, filename);
    }

    virtual int BotAllocWeaponState(void) override {
        return ::botlib_export->ai.BotAllocWeaponState();
    }

    virtual void BotFreeWeaponState(int weaponstate) override {
        ::botlib_export->ai.BotFreeWeaponState(weaponstate);
    }

    virtual void BotResetWeaponState(int weaponstate) override {
        ::botlib_export->ai.BotResetWeaponState(weaponstate);
    }

    // genetic
    virtual int GeneticParentsAndChildSelection(int numranks, float* ranks, int* parent1, int* parent2, int* child) override {
        return ::botlib_export->ai.GeneticParentsAndChildSelection(numranks, ranks, parent1, parent2, child);
    }
};

/*
===============
SV_ShutdownGameProgs

Called every time a map changes
===============
*/
void SV_ShutdownGameProgs( void ) {
	if ( !gvm ) {
		return;
	}
    gvm->ShutdownGame(qfalse);
    Sys_UnloadDll((void *)vmHandle);
    vmHandle = NULL;
	gvm = NULL;
}

/*
==================
SV_InitGameVM

Called for both a full init and a restart
==================
*/
static void SV_InitGameVM( qboolean restart ) {
	int i;

	// start the entity parsing at the beginning
	sv.entityParsePoint = CM_EntityString();

	// use the current msec count for a random seed
	// init for this gamestate
	gvm->InitGame( svs.time, Com_Milliseconds(), restart );

	// clear all gentity pointers that might still be set from
	// a previous level
	for ( i = 0 ; i < sv_maxclients->integer ; i++ ) {
		svs.clients[i].gentity = NULL;
	}
}



/*
===================
SV_RestartGameProgs

Called on a map_restart, but not on a normal map change
===================
*/
void SV_RestartGameProgs( void ) {
	if ( !gvm ) {
		return;
	}
    SV_ShutdownGameProgs();

	SV_InitGameVM( qtrue );
}


/*
===============
SV_InitGameProgs

Called on a normal map change, not on a map_restart
===============
*/
void SV_InitGameProgs( void ) {
	cvar_t  *var;
	//FIXME these are temp while I make bots run in vm
	extern int bot_enable;
    static idGameSystemLocal gameImports;

	var = Cvar_Get( "bot_enable", "1", CVAR_LATCH );
	if ( var ) {
		bot_enable = var->integer;
	} else {
		bot_enable = 0;
	}

	// load the dll or bytecode
    vmHandle = (dllhandle_t)Sys_LoadDll("qagame", GAME_API_VERSION, &gameImports, (void**)&gvm);
	if ( !gvm ) {
		Com_Error( ERR_FATAL, "VM_Create on game failed" );
	}

	SV_InitGameVM( qfalse );
}


/*
====================
SV_GameCommand

See if the current console command is claimed by the game
====================
*/
qboolean SV_GameCommand( void ) {
	if ( sv.state != SS_GAME ) {
		return qfalse;
	}

    return gvm->ConsoleCommand();
}


/*
====================
SV_SendMoveSpeedsToGame
====================
*/
void SV_SendMoveSpeedsToGame( int entnum, char *text ) {
	if ( !gvm ) {
		return;
	}
    
    gvm->RetrieveMoveSpeedsFromClient(entnum, text );
}

/*
====================
SV_GetTag

  return qfalse if unable to retrieve tag information for this client
====================
*/
extern qboolean CL_GetTag( int clientNum, char *tagname, orientation_t *or );

qboolean SV_GetTag( int clientNum, char *tagname, orientation_t *or ) {
	if ( com_dedicated->integer ) {
		return qfalse;
	}

	return CL_GetTag( clientNum, tagname, or );
}

/*
===================
SV_GetModelInfo

  request this modelinfo from the game
===================
*/
qboolean SV_GetModelInfo( int clientNum, char *modelName, animModelInfo_t **modelInfo ) {
	return gvm->GetModelInfo( clientNum, modelName, modelInfo );
}