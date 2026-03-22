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



#define CMD_BACKUP          64
#define CMD_MASK            ( CMD_BACKUP - 1 )
// allow a lot of command backups for very fast systems
// multiple commands may be combined into a single packet, so this
// needs to be larger than PACKET_BACKUP


#define MAX_ENTITIES_IN_SNAPSHOT    256

// snapshots are a view of the server at a given time

// Snapshots are generated at regular time intervals by the server,
// but they may not be sent if a client's rate level is exceeded, or
// they may be dropped by the network.
typedef struct {
	int snapFlags;                      // SNAPFLAG_RATE_DELAYED, etc
	int ping;

	int serverTime;                 // server time the message is valid for (in msec)

	byte areamask[MAX_MAP_AREA_BYTES];                  // portalarea visibility bits

	playerState_t ps;                       // complete information about the current player at this time

	int numEntities;                        // all of the entities that need to be presented
	entityState_t entities[MAX_ENTITIES_IN_SNAPSHOT];   // at the time of this snapshot

	int numServerCommands;                  // text based server commands to execute when this
	int serverCommandSequence;              // snapshot becomes current
} snapshot_t;

enum {
	CGAME_EVENT_NONE,
	CGAME_EVENT_TEAMMENU,
	CGAME_EVENT_SCOREBOARD,
	CGAME_EVENT_EDITHUD
};


/*
==================================================================

functions imported from the main executable

==================================================================
*/

#include "../game/bg_public.h"

#define CGAME_IMPORT_API_VERSION    3
class idCGSystemCalls
{
public:
	virtual ~idCGSystemCalls() = default;

	virtual void Print(const char* fmt) = 0;
	virtual void Error(const char* fmt) = 0;
	virtual int Milliseconds(void) = 0;

	virtual void Cvar_Register(vmCvar_t* vmCvar, const char* varName, const char* defaultValue, int flags) = 0;
	virtual void Cvar_Update(vmCvar_t* vmCvar) = 0;
	virtual void Cvar_Set(const char* var_name, const char* value) = 0;
	virtual void Cvar_VariableStringBuffer(const char* var_name, char* buffer, int bufsize) = 0;

	virtual int Argc(void) = 0;
	virtual void Argv(int n, char* buffer, int bufferLength) = 0;
	virtual void Args(char* buffer, int bufferLength) = 0;

	virtual int FS_FOpenFile(const char* qpath, fileHandle_t* f, fsMode_t mode) = 0;
	virtual void FS_Read(void* buffer, int len, fileHandle_t f) = 0;
	virtual void FS_Write(const void* buffer, int len, fileHandle_t f) = 0;
	virtual void FS_FCloseFile(fileHandle_t f) = 0;

	virtual void SendConsoleCommand(const char* text) = 0;
	virtual void AddCommand(const char* cmdName) = 0;
	virtual void SendClientCommand(const char* s) = 0;
	virtual void UpdateScreen(void) = 0;

	virtual void CM_LoadMap(const char* mapname) = 0;
	virtual int CM_NumInlineModels(void) = 0;
	virtual clipHandle_t CM_InlineModel(int index) = 0;
	virtual clipHandle_t CM_TempBoxModel(const vec3_t mins, const vec3_t maxs) = 0;
	virtual clipHandle_t CM_TempCapsuleModel(const vec3_t mins, const vec3_t maxs) = 0;
	virtual int CM_PointContents(const vec3_t p, clipHandle_t model) = 0;
	virtual int CM_TransformedPointContents(const vec3_t p, clipHandle_t model, const vec3_t origin, const vec3_t angles) = 0;

	virtual void CM_BoxTrace(
		trace_t* results,
		const vec3_t start,
		const vec3_t end,
		const vec3_t mins,
		const vec3_t maxs,
		clipHandle_t model,
		int brushmask) = 0;

	virtual void CM_TransformedBoxTrace(
		trace_t* results,
		const vec3_t start,
		const vec3_t end,
		const vec3_t mins,
		const vec3_t maxs,
		clipHandle_t model,
		int brushmask,
		const vec3_t origin,
		const vec3_t angles) = 0;

	virtual void CM_CapsuleTrace(
		trace_t* results,
		const vec3_t start,
		const vec3_t end,
		const vec3_t mins,
		const vec3_t maxs,
		clipHandle_t model,
		int brushmask) = 0;

	virtual void CM_TransformedCapsuleTrace(
		trace_t* results,
		const vec3_t start,
		const vec3_t end,
		const vec3_t mins,
		const vec3_t maxs,
		clipHandle_t model,
		int brushmask,
		const vec3_t origin,
		const vec3_t angles) = 0;

	virtual int CM_MarkFragments(
		int numPoints,
		const vec3_t* points,
		const vec3_t projection,
		int maxPoints,
		vec3_t pointBuffer,
		int maxFragments,
		markFragment_t* fragmentBuffer) = 0;

	virtual void S_StartSound(vec3_t origin, int entityNum, int entchannel, sfxHandle_t sfx) = 0;
	virtual void S_StartSoundEx(vec3_t origin, int entityNum, int entchannel, sfxHandle_t sfx, int flags) = 0;
	virtual void S_StartLocalSound(sfxHandle_t sfx, int channelNum) = 0;
	virtual void S_ClearLoopingSounds(qboolean killall) = 0;
	virtual void S_AddLoopingSound(int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx, int volume) = 0;
	virtual void S_AddRangedLoopingSound(int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx, int range) = 0;
	virtual void S_AddRealLoopingSound(int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx) = 0;
	virtual void S_StopLoopingSound(int entityNum) = 0;
	virtual void S_StopStreamingSound(int entityNum) = 0;
	virtual void S_UpdateEntityPosition(int entityNum, const vec3_t origin) = 0;
	virtual int S_GetVoiceAmplitude(int entityNum) = 0;
	virtual void S_Respatialize(int entityNum, const vec3_t origin, vec3_t axis[3], int inwater) = 0;
	virtual sfxHandle_t S_RegisterSound(const char* sample) = 0;
	virtual void S_StartBackgroundTrack(const char* intro, const char* loop, int fadeupTime) = 0;
	virtual void S_FadeBackgroundTrack(float targetvol, int time, int num) = 0;
	virtual void S_FadeAllSound(float targetvol, int time) = 0;
	virtual void S_StartStreamingSound(const char* intro, const char* loop, int entnum, int channel, int attenuation) = 0;
	virtual void S_StopBackgroundTrack(void) = 0;

	virtual void R_LoadWorldMap(const char* mapname) = 0;
	virtual qhandle_t R_RegisterModel(const char* name) = 0;
	virtual qboolean R_GetSkinModel(qhandle_t skinid, const char* type, char* name) = 0;
	virtual qhandle_t R_GetShaderFromModel(qhandle_t modelid, int surfnum, int withlightmap) = 0;
	virtual qhandle_t R_RegisterSkin(const char* name) = 0;
	virtual qhandle_t R_RegisterShader(const char* name) = 0;
	virtual qhandle_t R_RegisterShaderNoMip(const char* name) = 0;
	virtual void R_RegisterFont(const char* fontName, int pointSize, fontInfo_t* font) = 0;
	virtual void R_ClearScene(void) = 0;
	virtual void R_AddRefEntityToScene(const refEntity_t* re) = 0;
	virtual void R_AddPolyToScene(qhandle_t hShader, int numVerts, const polyVert_t* verts) = 0;
	virtual void R_AddPolysToScene(qhandle_t hShader, int numVerts, const polyVert_t* verts, int numPolys) = 0;
	virtual void RB_ZombieFXAddNewHit(int entityNum, const vec3_t hitPos, const vec3_t hitDir) = 0;
	virtual void R_AddLightToScene(const vec3_t org, float intensity, float r, float g, float b, int overdraw) = 0;
	virtual void R_AddCoronaToScene(const vec3_t org, float r, float g, float b, float scale, int id, int flags) = 0;
	virtual void R_SetFog(int fogvar, int var1, int var2, float r, float g, float b, float density) = 0;
	virtual void R_RenderScene(const refdef_t* fd) = 0;
	virtual void R_SetColor(const float* rgba) = 0;

	virtual void R_DrawStretchPic(
		float x,
		float y,
		float w,
		float h,
		float s1,
		float t1,
		float s2,
		float t2,
		qhandle_t hShader) = 0;

	virtual void R_DrawStretchPicGradient(
		float x,
		float y,
		float w,
		float h,
		float s1,
		float t1,
		float s2,
		float t2,
		qhandle_t hShader,
		const float* gradientColor,
		int gradientType) = 0;

	virtual void R_ModelBounds(clipHandle_t model, vec3_t mins, vec3_t maxs) = 0;
	virtual int R_LerpTag(orientation_t* tag, const refEntity_t* refent, const char* tagName, int startIndex) = 0;
	virtual void R_RemapShader(const char* oldShader, const char* newShader, const char* timeOffset) = 0;

	virtual void GetGlconfig(glconfig_t* glconfig) = 0;
	virtual void GetGameState(gameState_t* gamestate) = 0;
	virtual void GetCurrentSnapshotNumber(int* snapshotNumber, int* serverTime) = 0;
	virtual qboolean GetSnapshot(int snapshotNumber, snapshot_t* snapshot) = 0;
	virtual qboolean GetServerCommand(int serverCommandNumber) = 0;
	virtual int GetCurrentCmdNumber(void) = 0;
	virtual qboolean GetUserCmd(int cmdNumber, usercmd_t* ucmd) = 0;
	virtual void SetUserCmdValue(int stateValue, int holdableValue, float sensitivityScale, int cld) = 0;

	virtual void TestPrintInt(char* string, int i) = 0;
	virtual void TestPrintFloat(char* string, float f) = 0;

	virtual int MemoryRemaining(void) = 0;

	virtual qboolean LoadCamera(int camNum, const char* name) = 0;
	virtual void StartCamera(int camNum, int time) = 0;
	virtual void StopCamera(int camNum) = 0;
	virtual qboolean GetCameraInfo(int camNum, int time, vec3_t* origin, vec3_t* angles, float* fov) = 0;

	virtual qboolean Key_IsDown(int keynum) = 0;
	virtual int Key_GetCatcher(void) = 0;
	virtual void Key_SetCatcher(int catcher) = 0;
	virtual int Key_GetKey(const char* binding) = 0;

	virtual int PC_AddGlobalDefine(char* define) = 0;
	virtual int PC_LoadSource(const char* filename) = 0;
	virtual int PC_FreeSource(int handle) = 0;
	virtual int PC_ReadToken(int handle, pc_token_t* pc_token) = 0;
	virtual int PC_SourceFileAndLine(int handle, char* filename, int* line) = 0;

	virtual int RealTime(qtime_t* qtime) = 0;
	virtual void SendMoveSpeedsToGame(int entnum, char* movespeeds) = 0;

	virtual int CIN_PlayCinematic(const char* arg0, int xpos, int ypos, int width, int height, int bits) = 0;
	virtual e_status CIN_StopCinematic(int handle) = 0;
	virtual e_status CIN_RunCinematic(int handle) = 0;
	virtual void CIN_DrawCinematic(int handle) = 0;
	virtual void CIN_SetExtents(int handle, int x, int y, int w, int h) = 0;

	virtual qboolean GetEntityToken(char* buffer, int bufferSize) = 0;

	virtual void UI_Popup(const char* arg0) = 0;
	virtual void UI_LimboChat(const char* arg0) = 0;
	virtual void UI_ClosePopup(const char* arg0) = 0;

	virtual qboolean GetModelInfo(int clientNum, char* modelName, animModelInfo_t** modelInfo) = 0;
};


/*
==================================================================

functions exported to the main executable

==================================================================
*/

class idClientGameVM {
public:
	virtual ~idClientGameVM() = default;

	virtual int GetTag(int clientNum, char* tagName, orientation_t* tag) = 0;
	virtual void DrawActiveFrame(int serverTime, stereoFrame_t stereoView, qboolean demoPlayback) = 0;
	virtual void EventHandling(int type) = 0;
	virtual void Init(int serverMessageNum, int serverCommandSequence) = 0;
	virtual void Shutdown() = 0;
	virtual int ConsoleCommand() = 0;
	virtual int CrosshairPlayer() = 0;
	virtual int LastAttacker() = 0;
	virtual void KeyEvent(int key, qboolean down) = 0;
	virtual void MouseEvent(int dx, int dy) = 0;
};

extern idClientGameVM* cgvm;

//----------------------------------------------
