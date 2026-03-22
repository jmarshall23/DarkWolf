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

#ifndef __UI_PUBLIC_H__
#define __UI_PUBLIC_H__

#define UI_API_VERSION  1001

typedef struct {
	connstate_t connState;
	int connectPacketCount;
	int clientNum;
	char servername[MAX_STRING_CHARS];
	char updateInfoString[MAX_STRING_CHARS];
	char messageString[MAX_STRING_CHARS];
} uiClientState_t;

#pragma once

class idUISystemCalls
{
public:
	virtual ~idUISystemCalls() {}

	virtual void Print(const char* string) = 0;
	virtual void Error(const char* string) = 0;
	virtual int Milliseconds(void) = 0;

	virtual void Cvar_Register(vmCvar_t* cvar, const char* var_name, const char* value, int flags) = 0;
	virtual void Cvar_Update(vmCvar_t* cvar) = 0;
	virtual void Cvar_Set(const char* var_name, const char* value) = 0;
	virtual float Cvar_VariableValue(const char* var_name) = 0;
	virtual void Cvar_VariableStringBuffer(const char* var_name, char* buffer, int bufsize) = 0;
	virtual void Cvar_SetValue(const char* var_name, float value) = 0;
	virtual void Cvar_Reset(const char* name) = 0;
	virtual void Cvar_Create(const char* var_name, const char* var_value, int flags) = 0;
	virtual void Cvar_InfoStringBuffer(int bit, char* buffer, int bufsize) = 0;

	virtual int Argc(void) = 0;
	virtual void Argv(int n, char* buffer, int bufferLength) = 0;
	virtual void Cmd_ExecuteText(int exec_when, const char* text) = 0;

	virtual int FS_FOpenFile(const char* qpath, fileHandle_t* f, fsMode_t mode) = 0;
	virtual void FS_Read(void* buffer, int len, fileHandle_t f) = 0;
	virtual void FS_Seek(fileHandle_t f, long offset, int origin) = 0;
	virtual void FS_Write(const void* buffer, int len, fileHandle_t f) = 0;
	virtual void FS_FCloseFile(fileHandle_t f) = 0;
	virtual int FS_GetFileList(const char* path, const char* extension, char* listbuf, int bufsize) = 0;
	virtual int FS_Delete(const char* filename) = 0;

	virtual qhandle_t R_RegisterModel(const char* name) = 0;
	virtual qhandle_t R_RegisterSkin(const char* name) = 0;
	virtual void R_RegisterFont(const char* fontName, int pointSize, fontInfo_t* font) = 0;
	virtual qhandle_t R_RegisterShaderNoMip(const char* name) = 0;
	virtual void R_ClearScene(void) = 0;
	virtual void R_AddRefEntityToScene(const refEntity_t* re) = 0;
	virtual void R_AddPolyToScene(qhandle_t hShader, int numVerts, const polyVert_t* verts) = 0;
	virtual void R_AddLightToScene(const vec3_t org, float intensity, float r, float g, float b, int overdraw) = 0;
	virtual void R_AddCoronaToScene(const vec3_t org, float r, float g, float b, float scale, int id, int flags) = 0;
	virtual void R_RenderScene(const refdef_t* fd) = 0;
	virtual void R_SetColor(const float* rgba) = 0;
	virtual void R_DrawStretchPic(float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t hShader) = 0;
	virtual void R_ModelBounds(clipHandle_t model, vec3_t mins, vec3_t maxs) = 0;
	virtual void R_RemapShader(const char* oldShader, const char* newShader, const char* timeOffset) = 0;

	virtual void UpdateScreen(void) = 0;

	virtual int CM_LerpTag(orientation_t* tag, const refEntity_t* refent, const char* tagName, int startIndex) = 0;

	virtual void S_StartLocalSound(sfxHandle_t sfx, int channelNum) = 0;
	virtual sfxHandle_t S_RegisterSound(const char* sample) = 0;
	virtual void S_FadeBackgroundTrack(float targetvol, int time, int num) = 0;
	virtual void S_FadeAllSound(float targetvol, int time) = 0;
	virtual void S_StopBackgroundTrack(void) = 0;
	virtual void S_StartBackgroundTrack(const char* intro, const char* loop, int fadeupTime) = 0;

	virtual void Key_KeynumToStringBuf(int keynum, char* buf, int buflen) = 0;
	virtual void Key_GetBindingBuf(int keynum, char* buf, int buflen) = 0;
	virtual void Key_SetBinding(int keynum, const char* binding) = 0;
	virtual qboolean Key_IsDown(int keynum) = 0;
	virtual qboolean Key_GetOverstrikeMode(void) = 0;
	virtual void Key_SetOverstrikeMode(qboolean state) = 0;
	virtual void Key_ClearStates(void) = 0;
	virtual int Key_GetCatcher(void) = 0;
	virtual void Key_SetCatcher(int catcher) = 0;

	virtual void GetClipboardData(char* buf, int bufsize) = 0;
	virtual void GetClientState(uiClientState_t* state) = 0;
	virtual void GetGlconfig(glconfig_t* glconfig) = 0;
	virtual int GetConfigString(int index, char* buff, int buffsize) = 0;

	virtual int LAN_GetLocalServerCount(void) = 0;
	virtual void LAN_GetLocalServerAddressString(int n, char* buf, int buflen) = 0;
	virtual int LAN_GetGlobalServerCount(void) = 0;
	virtual void LAN_GetGlobalServerAddressString(int n, char* buf, int buflen) = 0;
	virtual int LAN_GetPingQueueCount(void) = 0;
	virtual void LAN_ClearPing(int n) = 0;
	virtual void LAN_GetPing(int n, char* buf, int buflen, int* pingtime) = 0;
	virtual void LAN_GetPingInfo(int n, char* buf, int buflen) = 0;
	virtual qboolean LAN_UpdateVisiblePings(int source) = 0;
	virtual int LAN_GetServerCount(int source) = 0;
	virtual int LAN_CompareServers(int source, int sortKey, int sortDir, int s1, int s2) = 0;
	virtual void LAN_GetServerAddressString(int source, int n, char* buf, int buflen) = 0;
	virtual void LAN_GetServerInfo(int source, int n, char* buf, int buflen) = 0;
	virtual int LAN_AddServer(int source, const char* name, const char* addr) = 0;
	virtual void LAN_RemoveServer(int source, const char* addr) = 0;
	virtual int LAN_GetServerPing(int source, int n) = 0;
	virtual int LAN_ServerIsVisible(int source, int n) = 0;
	virtual int LAN_ServerStatus(const char* serverAddress, char* serverStatus, int maxLen) = 0;
	virtual void LAN_SaveCachedServers(void) = 0;
	virtual void LAN_LoadCachedServers(void) = 0;
	virtual void LAN_MarkServerVisible(int source, int n, qboolean visible) = 0;
	virtual void LAN_ResetPings(int n) = 0;

	virtual int MemoryRemaining(void) = 0;

	virtual void GetCDKey(char* buf, int buflen) = 0;
	virtual void SetCDKey(char* buf) = 0;
	virtual int PC_AddGlobalDefine(char* define) = 0;
	virtual int PC_LoadSource(const char* filename) = 0;
	virtual int PC_FreeSource(int handle) = 0;
	virtual int PC_ReadToken(int handle, pc_token_t* pc_token) = 0;
	virtual int PC_SourceFileAndLine(int handle, char* filename, int* line) = 0;

	virtual int RealTime(qtime_t* qtime) = 0;

	virtual int CIN_PlayCinematic(const char* arg0, int xpos, int ypos, int width, int height, int bits) = 0;
	virtual e_status CIN_StopCinematic(int handle) = 0;
	virtual e_status CIN_RunCinematic(int handle) = 0;
	virtual void CIN_DrawCinematic(int handle) = 0;
	virtual void CIN_SetExtents(int handle, int x, int y, int w, int h) = 0;

	virtual qboolean VerifyCDKey(const char* key, const char* chksum) = 0;
	virtual qboolean GetLimboString(int index, char* buf) = 0;
};

typedef enum {
	UIMENU_NONE,
	UIMENU_MAIN,
	UIMENU_INGAME,
	UIMENU_NEED_CD,
	UIMENU_ENDGAME, //----(SA)	added
	UIMENU_BAD_CD_KEY,
	UIMENU_TEAM,
	UIMENU_PREGAME, //----(SA)	added
	UIMENU_POSTGAME,
	UIMENU_NOTEBOOK,
	UIMENU_CLIPBOARD,
	UIMENU_HELP,
	UIMENU_BOOK1,           //----(SA)	added
	UIMENU_BOOK2,           //----(SA)	added
	UIMENU_BOOK3,           //----(SA)	added
	UIMENU_WM_PICKTEAM,     // NERVE - SMF - for multiplayer only
	UIMENU_WM_PICKPLAYER,   // NERVE - SMF - for multiplayer only
	UIMENU_WM_QUICKMESSAGE, // NERVE - SMF
	UIMENU_WM_LIMBO,        // NERVE - SMF
	UIMENU_BRIEFING         //----(SA)	added
} uiMenuCommand_t;

#define SORT_HOST           0
#define SORT_MAP            1
#define SORT_CLIENTS        2
#define SORT_GAME           3
#define SORT_PING           4

#define SORT_SAVENAME       0
#define SORT_SAVETIME       1

class idUserInterfaceVM {
public:
	virtual ~idUserInterfaceVM() = default;

	// UI API
	virtual void Init(qboolean inGame) = 0;
	virtual void Shutdown() = 0;
	virtual void KeyEvent(int key, qboolean down) = 0;
	virtual void MouseEvent(int dx, int dy) = 0;
	virtual void Refresh(int realtime) = 0;
	virtual qboolean IsFullscreen() = 0;
	virtual void SetActiveMenu(uiMenuCommand_t menu) = 0;
	virtual int GetActiveMenu() = 0;
	virtual qboolean ConsoleCommand(int realtime) = 0;
	virtual void DrawConnectScreen(qboolean overlay) = 0;
	virtual qboolean HasUniqueCDKey() = 0;
};

extern idUserInterfaceVM* uivm;

typedef idUserInterfaceVM* (*uiEntry_t)(int version, idUISystemCalls* sys);

#endif
