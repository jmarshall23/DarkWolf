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


#include "client.h"

#include "../game/botlib.h"

extern botlib_export_t *botlib_export;

static dllhandle_t vmHandle = NULL;
idUserInterfaceVM* uivm = NULL;

extern char cl_cdkey[34];


/*
====================
GetClientState
====================
*/
static void GetClientState( uiClientState_t *state ) {
	state->connectPacketCount = clc.connectPacketCount;
	state->connState = cls.state;
	Q_strncpyz( state->servername, cls.servername, sizeof( state->servername ) );
	Q_strncpyz( state->updateInfoString, cls.updateInfoString, sizeof( state->updateInfoString ) );
	Q_strncpyz( state->messageString, clc.serverMessage, sizeof( state->messageString ) );
	state->clientNum = cl.snap.ps.clientNum;
}

/*
====================
LAN_LoadCachedServers
====================
*/
void LAN_LoadCachedServers() {
	// TTimo: stub, this is only relevant to MP, SP kills the servercache.dat (and favorites)
	// show_bug.cgi?id=445
	/*
	  int size;
	  fileHandle_t fileIn;
	  cls.numglobalservers = cls.nummplayerservers = cls.numfavoriteservers = 0;
	  cls.numGlobalServerAddresses = 0;
	  if (FS_SV_FOpenFileRead("servercache.dat", &fileIn)) {
		  FS_Read(&cls.numglobalservers, sizeof(int), fileIn);
		  FS_Read(&cls.nummplayerservers, sizeof(int), fileIn);
		  FS_Read(&cls.numfavoriteservers, sizeof(int), fileIn);
		  FS_Read(&size, sizeof(int), fileIn);
		  if (size == sizeof(cls.globalServers) + sizeof(cls.favoriteServers) + sizeof(cls.mplayerServers)) {
			  FS_Read(&cls.globalServers, sizeof(cls.globalServers), fileIn);
			  FS_Read(&cls.mplayerServers, sizeof(cls.mplayerServers), fileIn);
			  FS_Read(&cls.favoriteServers, sizeof(cls.favoriteServers), fileIn);
		  } else {
			  cls.numglobalservers = cls.nummplayerservers = cls.numfavoriteservers = 0;
			  cls.numGlobalServerAddresses = 0;
		  }
		  FS_FCloseFile(fileIn);
	  }
	*/
}

/*
====================
LAN_SaveServersToCache
====================
*/
void LAN_SaveServersToCache() {
	// TTimo: stub, this is only relevant to MP, SP kills the servercache.dat (and favorites)
	// show_bug.cgi?id=445
	/*
	  int size;
	  fileHandle_t fileOut;
  #ifdef __MACOS__	//DAJ MacOS file typing
	  {
		  extern _MSL_IMP_EXP_C long _fcreator, _ftype;
		  _ftype = 'WlfB';
		  _fcreator = 'WlfS';
	  }
  #endif
	  fileOut = FS_SV_FOpenFileWrite("servercache.dat");
	  FS_Write(&cls.numglobalservers, sizeof(int), fileOut);
	  FS_Write(&cls.nummplayerservers, sizeof(int), fileOut);
	  FS_Write(&cls.numfavoriteservers, sizeof(int), fileOut);
	  size = sizeof(cls.globalServers) + sizeof(cls.favoriteServers) + sizeof(cls.mplayerServers);
	  FS_Write(&size, sizeof(int), fileOut);
	  FS_Write(&cls.globalServers, sizeof(cls.globalServers), fileOut);
	  FS_Write(&cls.mplayerServers, sizeof(cls.mplayerServers), fileOut);
	  FS_Write(&cls.favoriteServers, sizeof(cls.favoriteServers), fileOut);
	  FS_FCloseFile(fileOut);
	*/
}


/*
====================
LAN_ResetPings
====================
*/
static void LAN_ResetPings( int source ) {
	int count,i;
	serverInfo_t *servers = NULL;
	count = 0;

	switch ( source ) {
	case AS_LOCAL:
		servers = &cls.localServers[0];
		count = MAX_OTHER_SERVERS;
		break;
	case AS_MPLAYER:
		servers = &cls.mplayerServers[0];
		count = MAX_OTHER_SERVERS;
		break;
	case AS_GLOBAL:
		servers = &cls.globalServers[0];
		count = MAX_GLOBAL_SERVERS;
		break;
	case AS_FAVORITES:
		servers = &cls.favoriteServers[0];
		count = MAX_OTHER_SERVERS;
		break;
	}
	if ( servers ) {
		for ( i = 0; i < count; i++ ) {
			servers[i].ping = -1;
		}
	}
}

/*
====================
LAN_AddServer
====================
*/
static int LAN_AddServer( int source, const char *name, const char *address ) {
	int max, *count, i;
	netadr_t adr;
	serverInfo_t *servers = NULL;
	max = MAX_OTHER_SERVERS;
	count = 0;

	switch ( source ) {
	case AS_LOCAL:
		count = &cls.numlocalservers;
		servers = &cls.localServers[0];
		break;
	case AS_MPLAYER:
		count = &cls.nummplayerservers;
		servers = &cls.mplayerServers[0];
		break;
	case AS_GLOBAL:
		max = MAX_GLOBAL_SERVERS;
		count = &cls.numglobalservers;
		servers = &cls.globalServers[0];
		break;
	case AS_FAVORITES:
		count = &cls.numfavoriteservers;
		servers = &cls.favoriteServers[0];
		break;
	}
	if ( servers && *count < max ) {
		NET_StringToAdr( address, &adr );
		for ( i = 0; i < *count; i++ ) {
			if ( NET_CompareAdr( servers[i].adr, adr ) ) {
				break;
			}
		}
		if ( i >= *count ) {
			servers[*count].adr = adr;
			Q_strncpyz( servers[*count].hostName, name, sizeof( servers[*count].hostName ) );
			servers[*count].visible = qtrue;
			( *count )++;
			return 1;
		}
		return 0;
	}
	return -1;
}

/*
====================
LAN_RemoveServer
====================
*/
static void LAN_RemoveServer( int source, const char *addr ) {
	int *count, i;
	serverInfo_t *servers = NULL;
	count = 0;
	switch ( source ) {
	case AS_LOCAL:
		count = &cls.numlocalservers;
		servers = &cls.localServers[0];
		break;
	case AS_MPLAYER:
		count = &cls.nummplayerservers;
		servers = &cls.mplayerServers[0];
		break;
	case AS_GLOBAL:
		count = &cls.numglobalservers;
		servers = &cls.globalServers[0];
		break;
	case AS_FAVORITES:
		count = &cls.numfavoriteservers;
		servers = &cls.favoriteServers[0];
		break;
	}
	if ( servers ) {
		netadr_t comp;
		NET_StringToAdr( addr, &comp );
		for ( i = 0; i < *count; i++ ) {
			if ( NET_CompareAdr( comp, servers[i].adr ) ) {
				int j = i;
				while ( j < *count - 1 ) {
					Com_Memcpy( &servers[j], &servers[j + 1], sizeof( servers[j] ) );
					j++;
				}
				( *count )--;
				break;
			}
		}
	}
}


/*
====================
LAN_GetServerCount
====================
*/
static int LAN_GetServerCount( int source ) {
	switch ( source ) {
	case AS_LOCAL:
		return cls.numlocalservers;
		break;
	case AS_MPLAYER:
		return cls.nummplayerservers;
		break;
	case AS_GLOBAL:
		return cls.numglobalservers;
		break;
	case AS_FAVORITES:
		return cls.numfavoriteservers;
		break;
	}
	return 0;
}

/*
====================
LAN_GetLocalServerAddressString
====================
*/
static void LAN_GetServerAddressString( int source, int n, char *buf, int buflen ) {
	switch ( source ) {
	case AS_LOCAL:
		if ( n >= 0 && n < MAX_OTHER_SERVERS ) {
			Q_strncpyz( buf, NET_AdrToString( cls.localServers[n].adr ), buflen );
			return;
		}
		break;
	case AS_MPLAYER:
		if ( n >= 0 && n < MAX_OTHER_SERVERS ) {
			Q_strncpyz( buf, NET_AdrToString( cls.mplayerServers[n].adr ), buflen );
			return;
		}
		break;
	case AS_GLOBAL:
		if ( n >= 0 && n < MAX_GLOBAL_SERVERS ) {
			Q_strncpyz( buf, NET_AdrToString( cls.globalServers[n].adr ), buflen );
			return;
		}
		break;
	case AS_FAVORITES:
		if ( n >= 0 && n < MAX_OTHER_SERVERS ) {
			Q_strncpyz( buf, NET_AdrToString( cls.favoriteServers[n].adr ), buflen );
			return;
		}
		break;
	}
	buf[0] = '\0';
}

/*
====================
LAN_GetServerInfo
====================
*/
static void LAN_GetServerInfo( int source, int n, char *buf, int buflen ) {
	char info[MAX_STRING_CHARS];
	serverInfo_t *server = NULL;
	info[0] = '\0';
	switch ( source ) {
	case AS_LOCAL:
		if ( n >= 0 && n < MAX_OTHER_SERVERS ) {
			server = &cls.localServers[n];
		}
		break;
	case AS_MPLAYER:
		if ( n >= 0 && n < MAX_OTHER_SERVERS ) {
			server = &cls.mplayerServers[n];
		}
		break;
	case AS_GLOBAL:
		if ( n >= 0 && n < MAX_GLOBAL_SERVERS ) {
			server = &cls.globalServers[n];
		}
		break;
	case AS_FAVORITES:
		if ( n >= 0 && n < MAX_OTHER_SERVERS ) {
			server = &cls.favoriteServers[n];
		}
		break;
	}
	if ( server && buf ) {
		buf[0] = '\0';
		Info_SetValueForKey( info, "hostname", server->hostName );
		Info_SetValueForKey( info, "mapname", server->mapName );
		Info_SetValueForKey( info, "clients", va( "%i",server->clients ) );
		Info_SetValueForKey( info, "sv_maxclients", va( "%i",server->maxClients ) );
		Info_SetValueForKey( info, "ping", va( "%i",server->ping ) );
		Info_SetValueForKey( info, "minping", va( "%i",server->minPing ) );
		Info_SetValueForKey( info, "maxping", va( "%i",server->maxPing ) );
		Info_SetValueForKey( info, "game", server->game );
		Info_SetValueForKey( info, "gametype", va( "%i",server->gameType ) );
		Info_SetValueForKey( info, "nettype", va( "%i",server->netType ) );
		Info_SetValueForKey( info, "addr", NET_AdrToString( server->adr ) );
		Info_SetValueForKey( info, "sv_allowAnonymous", va( "%i", server->allowAnonymous ) );
		Q_strncpyz( buf, info, buflen );
	} else {
		if ( buf ) {
			buf[0] = '\0';
		}
	}
}

/*
====================
LAN_GetServerPing
====================
*/
static int LAN_GetServerPing( int source, int n ) {
	serverInfo_t *server = NULL;
	switch ( source ) {
	case AS_LOCAL:
		if ( n >= 0 && n < MAX_OTHER_SERVERS ) {
			server = &cls.localServers[n];
		}
		break;
	case AS_MPLAYER:
		if ( n >= 0 && n < MAX_OTHER_SERVERS ) {
			server = &cls.mplayerServers[n];
		}
		break;
	case AS_GLOBAL:
		if ( n >= 0 && n < MAX_GLOBAL_SERVERS ) {
			server = &cls.globalServers[n];
		}
		break;
	case AS_FAVORITES:
		if ( n >= 0 && n < MAX_OTHER_SERVERS ) {
			server = &cls.favoriteServers[n];
		}
		break;
	}
	if ( server ) {
		return server->ping;
	}
	return -1;
}

/*
====================
LAN_GetServerPtr
====================
*/
static serverInfo_t *LAN_GetServerPtr( int source, int n ) {
	switch ( source ) {
	case AS_LOCAL:
		if ( n >= 0 && n < MAX_OTHER_SERVERS ) {
			return &cls.localServers[n];
		}
		break;
	case AS_MPLAYER:
		if ( n >= 0 && n < MAX_OTHER_SERVERS ) {
			return &cls.mplayerServers[n];
		}
		break;
	case AS_GLOBAL:
		if ( n >= 0 && n < MAX_GLOBAL_SERVERS ) {
			return &cls.globalServers[n];
		}
		break;
	case AS_FAVORITES:
		if ( n >= 0 && n < MAX_OTHER_SERVERS ) {
			return &cls.favoriteServers[n];
		}
		break;
	}
	return NULL;
}

/*
====================
LAN_CompareServers
====================
*/
static int LAN_CompareServers( int source, int sortKey, int sortDir, int s1, int s2 ) {
	int res;
	serverInfo_t *server1, *server2;

	server1 = LAN_GetServerPtr( source, s1 );
	server2 = LAN_GetServerPtr( source, s2 );
	if ( !server1 || !server2 ) {
		return 0;
	}

	res = 0;
	switch ( sortKey ) {
	case SORT_HOST:
		res = Q_stricmp( server1->hostName, server2->hostName );
		break;

	case SORT_MAP:
		res = Q_stricmp( server1->mapName, server2->mapName );
		break;
	case SORT_CLIENTS:
		if ( server1->clients < server2->clients ) {
			res = -1;
		} else if ( server1->clients > server2->clients )     {
			res = 1;
		} else {
			res = 0;
		}
		break;
	case SORT_GAME:
		if ( server1->gameType < server2->gameType ) {
			res = -1;
		} else if ( server1->gameType > server2->gameType )     {
			res = 1;
		} else {
			res = 0;
		}
		break;
	case SORT_PING:
		if ( server1->ping < server2->ping ) {
			res = -1;
		} else if ( server1->ping > server2->ping )     {
			res = 1;
		} else {
			res = 0;
		}
		break;
	}

	if ( sortDir ) {
		if ( res < 0 ) {
			return 1;
		}
		if ( res > 0 ) {
			return -1;
		}
		return 0;
	}
	return res;
}

/*
====================
LAN_GetPingQueueCount
====================
*/
static int LAN_GetPingQueueCount( void ) {
	return ( CL_GetPingQueueCount() );
}

/*
====================
LAN_ClearPing
====================
*/
static void LAN_ClearPing( int n ) {
	CL_ClearPing( n );
}

/*
====================
LAN_GetPing
====================
*/
static void LAN_GetPing( int n, char *buf, int buflen, int *pingtime ) {
	CL_GetPing( n, buf, buflen, pingtime );
}

/*
====================
LAN_GetPingInfo
====================
*/
static void LAN_GetPingInfo( int n, char *buf, int buflen ) {
	CL_GetPingInfo( n, buf, buflen );
}

/*
====================
LAN_MarkServerVisible
====================
*/
static void LAN_MarkServerVisible( int source, int n, qboolean visible ) {
	if ( n == -1 ) {
		int count = MAX_OTHER_SERVERS;
		serverInfo_t *server = NULL;
		switch ( source ) {
		case AS_LOCAL:
			server = &cls.localServers[0];
			break;
		case AS_MPLAYER:
			server = &cls.mplayerServers[0];
			break;
		case AS_GLOBAL:
			server = &cls.globalServers[0];
			count = MAX_GLOBAL_SERVERS;
			break;
		case AS_FAVORITES:
			server = &cls.favoriteServers[0];
			break;
		}
		if ( server ) {
			for ( n = 0; n < count; n++ ) {
				server[n].visible = visible;
			}
		}

	} else {
		switch ( source ) {
		case AS_LOCAL:
			if ( n >= 0 && n < MAX_OTHER_SERVERS ) {
				cls.localServers[n].visible = visible;
			}
			break;
		case AS_MPLAYER:
			if ( n >= 0 && n < MAX_OTHER_SERVERS ) {
				cls.mplayerServers[n].visible = visible;
			}
			break;
		case AS_GLOBAL:
			if ( n >= 0 && n < MAX_GLOBAL_SERVERS ) {
				cls.globalServers[n].visible = visible;
			}
			break;
		case AS_FAVORITES:
			if ( n >= 0 && n < MAX_OTHER_SERVERS ) {
				cls.favoriteServers[n].visible = visible;
			}
			break;
		}
	}
}


/*
=======================
LAN_ServerIsVisible
=======================
*/
static int LAN_ServerIsVisible( int source, int n ) {
	switch ( source ) {
	case AS_LOCAL:
		if ( n >= 0 && n < MAX_OTHER_SERVERS ) {
			return cls.localServers[n].visible;
		}
		break;
	case AS_MPLAYER:
		if ( n >= 0 && n < MAX_OTHER_SERVERS ) {
			return cls.mplayerServers[n].visible;
		}
		break;
	case AS_GLOBAL:
		if ( n >= 0 && n < MAX_GLOBAL_SERVERS ) {
			return cls.globalServers[n].visible;
		}
		break;
	case AS_FAVORITES:
		if ( n >= 0 && n < MAX_OTHER_SERVERS ) {
			return cls.favoriteServers[n].visible;
		}
		break;
	}
	return qfalse;
}

/*
=======================
LAN_UpdateVisiblePings
=======================
*/
qboolean LAN_UpdateVisiblePings( int source ) {
	return CL_UpdateVisiblePings_f( source );
}

/*
====================
LAN_GetServerStatus
====================
*/
int LAN_GetServerStatus( char *serverAddress, char *serverStatus, int maxLen ) {
	return CL_ServerStatus( serverAddress, serverStatus, maxLen );
}

/*
====================
CL_GetGlConfig
====================
*/
static void CL_GetGlconfig( glconfig_t *config ) {
	*config = cls.glconfig;
}

/*
====================
GetClipboardData
====================
*/
static void GetClipboardData( char *buf, int buflen ) {
	char    *cbd;

	cbd = Sys_GetClipboardData();

	if ( !cbd ) {
		*buf = 0;
		return;
	}

	Q_strncpyz( buf, cbd, buflen );

	Z_Free( cbd );
}

/*
====================
Key_KeynumToStringBuf
====================
*/
static void Key_KeynumToStringBuf( int keynum, char *buf, int buflen ) {
	Q_strncpyz( buf, Key_KeynumToString( keynum, qtrue ), buflen );
}

/*
====================
Key_GetBindingBuf
====================
*/
static void Key_GetBindingBuf( int keynum, char *buf, int buflen ) {
	char    *value;

	value = Key_GetBinding( keynum );
	if ( value ) {
		Q_strncpyz( buf, value, buflen );
	} else {
		*buf = 0;
	}
}

/*
====================
Key_GetCatcher
====================
*/
int Key_GetCatcher( void ) {
	return cls.keyCatchers;
}

/*
====================
Ket_SetCatcher
====================
*/
void Key_SetCatcher( int catcher ) {
	cls.keyCatchers = catcher;
}


/*
====================
CLUI_GetCDKey
====================
*/
static void CLUI_GetCDKey( char *buf, int buflen ) {
	cvar_t  *fs;
	fs = Cvar_Get( "fs_game", "", CVAR_INIT | CVAR_SYSTEMINFO );
	if ( UI_usesUniqueCDKey() && fs && fs->string[0] != 0 ) {
		memcpy( buf, &cl_cdkey[16], 16 );
		buf[16] = 0;
	} else {
		memcpy( buf, cl_cdkey, 16 );
		buf[16] = 0;
	}
}


/*
====================
CLUI_SetCDKey
====================
*/
static void CLUI_SetCDKey( char *buf ) {
	cvar_t  *fs;
	fs = Cvar_Get( "fs_game", "", CVAR_INIT | CVAR_SYSTEMINFO );
	if ( UI_usesUniqueCDKey() && fs && fs->string[0] != 0 ) {
		memcpy( &cl_cdkey[16], buf, 16 );
		cl_cdkey[32] = 0;
		// set the flag so the fle will be written at the next opportunity
		cvar_modifiedFlags |= CVAR_ARCHIVE;
	} else {
		memcpy( cl_cdkey, buf, 16 );
		// set the flag so the fle will be written at the next opportunity
		cvar_modifiedFlags |= CVAR_ARCHIVE;
	}
}


/*
====================
GetConfigString
====================
*/
static int GetConfigString( int index, char *buf, int size ) {
	int offset;

	if ( index < 0 || index >= MAX_CONFIGSTRINGS ) {
		return qfalse;
	}

	offset = cl.gameState.stringOffsets[index];
	if ( !offset ) {
		if ( size ) {
			buf[0] = 0;
		}
		return qfalse;
	}

	Q_strncpyz( buf, cl.gameState.stringData + offset, size );

	return qtrue;
}
class idClientUISystemCalls : public idUISystemCalls
{
public:
	virtual ~idClientUISystemCalls() {}

	virtual void Print(const char* string) override
	{
		::Com_Printf("%s", string);
	}

	virtual void Error(const char* string) override
	{
		::Com_Error(ERR_DROP, "%s", string);
	}

	virtual int Milliseconds(void) override
	{
		return ::Sys_Milliseconds();
	}

	virtual void Cvar_Register(vmCvar_t* cvar, const char* var_name, const char* value, int flags) override
	{
		::Cvar_Register(cvar, var_name, value, flags);
	}

	virtual void Cvar_Update(vmCvar_t* cvar) override
	{
		::Cvar_Update(cvar);
	}

	virtual void Cvar_Set(const char* var_name, const char* value) override
	{
		::Cvar_Set(var_name, value);
	}

	virtual float Cvar_VariableValue(const char* var_name) override
	{
		return ::Cvar_VariableValue(var_name);
	}

	virtual void Cvar_VariableStringBuffer(const char* var_name, char* buffer, int bufsize) override
	{
		::Cvar_VariableStringBuffer(var_name, buffer, bufsize);
	}

	virtual void Cvar_SetValue(const char* var_name, float value) override
	{
		::Cvar_SetValue(var_name, value);
	}

	virtual void Cvar_Reset(const char* name) override
	{
		::Cvar_Reset(name);
	}

	virtual void Cvar_Create(const char* var_name, const char* var_value, int flags) override
	{
		::Cvar_Get(var_name, var_value, flags);
	}

	virtual void Cvar_InfoStringBuffer(int bit, char* buffer, int bufsize) override
	{
		::Cvar_InfoStringBuffer(bit, buffer, bufsize);
	}

	virtual int Argc(void) override
	{
		return ::Cmd_Argc();
	}

	virtual void Argv(int n, char* buffer, int bufferLength) override
	{
		::Cmd_ArgvBuffer(n, buffer, bufferLength);
	}

	virtual void Cmd_ExecuteText(int exec_when, const char* text) override
	{
		::Cbuf_ExecuteText(exec_when, text);
	}

	virtual int FS_FOpenFile(const char* qpath, fileHandle_t* f, fsMode_t mode) override
	{
		return ::FS_FOpenFileByMode(qpath, f, mode);
	}

	virtual void FS_Read(void* buffer, int len, fileHandle_t f) override
	{
		::FS_Read(buffer, len, f);
	}

	virtual void FS_Seek(fileHandle_t f, long offset, int origin) override
	{
		::FS_Seek(f, offset, origin);
	}

	virtual void FS_Write(const void* buffer, int len, fileHandle_t f) override
	{
		::FS_Write(buffer, len, f);
	}

	virtual void FS_FCloseFile(fileHandle_t f) override
	{
		::FS_FCloseFile(f);
	}

	virtual int FS_GetFileList(const char* path, const char* extension, char* listbuf, int bufsize) override
	{
		return ::FS_GetFileList(path, extension, listbuf, bufsize);
	}

	virtual int FS_Delete(const char* filename) override
	{
		return ::FS_Delete((char *)filename);
	}

	virtual qhandle_t R_RegisterModel(const char* name) override
	{
		return re.RegisterModel(name);
	}

	virtual qhandle_t R_RegisterSkin(const char* name) override
	{
		return re.RegisterSkin(name);
	}

	virtual void R_RegisterFont(const char* fontName, int pointSize, fontInfo_t* font) override
	{
		re.RegisterFont(fontName, pointSize, font);
	}

	virtual qhandle_t R_RegisterShaderNoMip(const char* name) override
	{
		return re.RegisterShaderNoMip(name);
	}

	virtual void R_ClearScene(void) override
	{
		re.ClearScene();
	}

	virtual void R_AddRefEntityToScene(const refEntity_t* ent) override
	{
		re.AddRefEntityToScene(ent);
	}

	virtual void R_AddPolyToScene(qhandle_t hShader, int numVerts, const polyVert_t* verts) override
	{
		re.AddPolyToScene(hShader, numVerts, verts);
	}

	virtual void R_AddLightToScene(const vec3_t org, float intensity, float r, float g, float b, int overdraw) override
	{
		re.AddLightToScene(org, intensity, r, g, b, overdraw);
	}

	virtual void R_AddCoronaToScene(const vec3_t org, float r, float g, float b, float scale, int id, int flags) override
	{
		re.AddCoronaToScene(org, r, g, b, scale, id, flags);
	}

	virtual void R_RenderScene(const refdef_t* fd) override
	{
		re.RenderScene(fd);
	}

	virtual void R_SetColor(const float* rgba) override
	{
		re.SetColor(rgba);
	}

	virtual void R_DrawStretchPic(float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t hShader) override
	{
		re.DrawStretchPic(x, y, w, h, s1, t1, s2, t2, hShader);
	}

	virtual void R_ModelBounds(clipHandle_t model, vec3_t mins, vec3_t maxs) override
	{
		re.ModelBounds(model, mins, maxs);
	}

	virtual void R_RemapShader(const char* oldShader, const char* newShader, const char* timeOffset) override
	{
		re.RemapShader(oldShader, newShader, timeOffset);
	}

	virtual void UpdateScreen(void) override
	{
		::SCR_UpdateScreen();
	}

	virtual int CM_LerpTag(orientation_t* tag, const refEntity_t* refent, const char* tagName, int startIndex) override
	{
		return re.LerpTag(tag, refent, tagName, startIndex);
	}

	virtual void S_StartLocalSound(sfxHandle_t sfx, int channelNum) override
	{
		::S_StartLocalSound(sfx, channelNum);
	}

	virtual sfxHandle_t S_RegisterSound(const char* sample) override
	{
#ifdef DOOMSOUND
		return ::S_RegisterSound(sample);
#else
		return ::S_RegisterSound(sample, qfalse);
#endif
	}

	virtual void S_FadeBackgroundTrack(float targetvol, int time, int num) override
	{
		::S_FadeStreamingSound(targetvol, time, num);
	}

	virtual void S_FadeAllSound(float targetvol, int time) override
	{
		::S_FadeAllSounds(targetvol, time);
	}

	virtual void S_StopBackgroundTrack(void) override
	{
		::S_StopBackgroundTrack();
	}

	virtual void S_StartBackgroundTrack(const char* intro, const char* loop, int fadeupTime) override
	{
		::S_StartBackgroundTrack(intro, loop, fadeupTime);
	}

	virtual void Key_KeynumToStringBuf(int keynum, char* buf, int buflen) override
	{
		::Key_KeynumToStringBuf(keynum, buf, buflen);
	}

	virtual void Key_GetBindingBuf(int keynum, char* buf, int buflen) override
	{
		::Key_GetBindingBuf(keynum, buf, buflen);
	}

	virtual void Key_SetBinding(int keynum, const char* binding) override
	{
		::Key_SetBinding(keynum, binding);
	}

	virtual qboolean Key_IsDown(int keynum) override
	{
		return ::Key_IsDown(keynum);
	}

	virtual qboolean Key_GetOverstrikeMode(void) override
	{
		return ::Key_GetOverstrikeMode();
	}

	virtual void Key_SetOverstrikeMode(qboolean state) override
	{
		::Key_SetOverstrikeMode(state);
	}

	virtual void Key_ClearStates(void) override
	{
		::Key_ClearStates();
	}

	virtual int Key_GetCatcher(void) override
	{
		return ::Key_GetCatcher();
	}

	virtual void Key_SetCatcher(int catcher) override
	{
		::Key_SetCatcher(catcher);
	}

	virtual void GetClipboardData(char* buf, int bufsize) override
	{
		::GetClipboardData(buf, bufsize);
	}

	virtual void GetClientState(uiClientState_t* state) override
	{
		::GetClientState(state);
	}

	virtual void GetGlconfig(glconfig_t* glconfig) override
	{
		::CL_GetGlconfig(glconfig);
	}

	virtual int GetConfigString(int index, char* buff, int buffsize) override
	{
		return ::GetConfigString(index, buff, buffsize);
	}

	virtual int LAN_GetLocalServerCount(void) override
	{
		return ::LAN_GetServerCount(AS_LOCAL);
	}

	virtual void LAN_GetLocalServerAddressString(int n, char* buf, int buflen) override
	{
		::LAN_GetServerAddressString(AS_LOCAL, n, buf, buflen);
	}

	virtual int LAN_GetGlobalServerCount(void) override
	{
		return ::LAN_GetServerCount(AS_GLOBAL);
	}

	virtual void LAN_GetGlobalServerAddressString(int n, char* buf, int buflen) override
	{
		::LAN_GetServerAddressString(AS_GLOBAL, n, buf, buflen);
	}

	virtual int LAN_GetPingQueueCount(void) override
	{
		return ::LAN_GetPingQueueCount();
	}

	virtual void LAN_ClearPing(int n) override
	{
		::LAN_ClearPing(n);
	}

	virtual void LAN_GetPing(int n, char* buf, int buflen, int* pingtime) override
	{
		::LAN_GetPing(n, buf, buflen, pingtime);
	}

	virtual void LAN_GetPingInfo(int n, char* buf, int buflen) override
	{
		::LAN_GetPingInfo(n, buf, buflen);
	}

	virtual qboolean LAN_UpdateVisiblePings(int source) override
	{
		return ::LAN_UpdateVisiblePings(source);
	}

	virtual int LAN_GetServerCount(int source) override
	{
		return ::LAN_GetServerCount(source);
	}

	virtual int LAN_CompareServers(int source, int sortKey, int sortDir, int s1, int s2) override
	{
		return ::LAN_CompareServers(source, sortKey, sortDir, s1, s2);
	}

	virtual void LAN_GetServerAddressString(int source, int n, char* buf, int buflen) override
	{
		::LAN_GetServerAddressString(source, n, buf, buflen);
	}

	virtual void LAN_GetServerInfo(int source, int n, char* buf, int buflen) override
	{
		::LAN_GetServerInfo(source, n, buf, buflen);
	}

	virtual int LAN_AddServer(int source, const char* name, const char* addr) override
	{
		return ::LAN_AddServer(source, name, addr);
	}

	virtual void LAN_RemoveServer(int source, const char* addr) override
	{
		::LAN_RemoveServer(source, addr);
	}

	virtual int LAN_GetServerPing(int source, int n) override
	{
		return ::LAN_GetServerPing(source, n);
	}

	virtual int LAN_ServerIsVisible(int source, int n) override
	{
		return ::LAN_ServerIsVisible(source, n);
	}

	virtual int LAN_ServerStatus(const char* serverAddress, char* serverStatus, int maxLen) override
	{
		return ::LAN_GetServerStatus((char *)serverAddress, serverStatus, maxLen);
	}

	virtual void LAN_SaveCachedServers(void) override
	{
		::LAN_SaveServersToCache();
	}

	virtual void LAN_LoadCachedServers(void) override
	{
		::LAN_LoadCachedServers();
	}

	virtual void LAN_MarkServerVisible(int source, int n, qboolean visible) override
	{
		::LAN_MarkServerVisible(source, n, visible);
	}

	virtual void LAN_ResetPings(int n) override
	{
		::LAN_ResetPings(n);
	}

	virtual int MemoryRemaining(void) override
	{
		return ::Hunk_MemoryRemaining();
	}

	virtual void GetCDKey(char* buf, int buflen) override
	{
		::CLUI_GetCDKey(buf, buflen);
	}

	virtual void SetCDKey(char* buf) override
	{
		::CLUI_SetCDKey(buf);
	}

	virtual int PC_AddGlobalDefine(char* define) override
	{
		return ::botlib_export->PC_AddGlobalDefine(define);
	}

	virtual int PC_LoadSource(const char* filename) override
	{
		return ::botlib_export->PC_LoadSourceHandle(filename);
	}

	virtual int PC_FreeSource(int handle) override
	{
		return ::botlib_export->PC_FreeSourceHandle(handle);
	}

	virtual int PC_ReadToken(int handle, pc_token_t* pc_token) override
	{
		return ::botlib_export->PC_ReadTokenHandle(handle, pc_token);
	}

	virtual int PC_SourceFileAndLine(int handle, char* filename, int* line) override
	{
		return ::botlib_export->PC_SourceFileAndLine(handle, filename, line);
	}

	virtual int RealTime(qtime_t* qtime) override
	{
		return ::Com_RealTime(qtime);
	}

	virtual int CIN_PlayCinematic(const char* arg0, int xpos, int ypos, int width, int height, int bits) override
	{
		return ::CIN_PlayCinematic(arg0, xpos, ypos, width, height, bits);
	}

	virtual e_status CIN_StopCinematic(int handle) override
	{
		return ::CIN_StopCinematic(handle);
	}

	virtual e_status CIN_RunCinematic(int handle) override
	{
		return ::CIN_RunCinematic(handle);
	}

	virtual void CIN_DrawCinematic(int handle) override
	{
		::CIN_DrawCinematic(handle);
	}

	virtual void CIN_SetExtents(int handle, int x, int y, int w, int h) override
	{
		::CIN_SetExtents(handle, x, y, w, h);
	}

	virtual qboolean VerifyCDKey(const char* key, const char* chksum) override
	{
		return ::CL_CDKeyValidate(key, chksum);
	}

	virtual qboolean GetLimboString(int index, char* buf) override
	{
		return ::CL_GetLimboString(index, buf);
	}
};

/*
====================
CL_ShutdownUI
====================
*/
void CL_ShutdownUI( void ) {
	cls.keyCatchers &= ~KEYCATCH_UI;
	cls.uiStarted = qfalse;
	if ( !uivm ) {
		return;
	}
	uivm->Shutdown();
	Sys_UnloadDll((void *)vmHandle);

	uivm = NULL;
}

/*
====================
CL_InitUI
====================
*/

void CL_InitUI( void ) {
	int v;
	static idClientUISystemCalls uiSys;

//----(SA)	always dll

	vmHandle = (dllhandle_t)Sys_LoadDll("ui", UI_API_VERSION, &uiSys, (void**)&uivm);

	if ( !uivm ) {
		Com_Error( ERR_FATAL, "VM_Create on UI failed" );
	}

	// init for this gamestate
//	VM_Call( uivm, UI_INIT );
	uivm->Init(( cls.state >= CA_AUTHORIZING && cls.state < CA_ACTIVE ) );
}


qboolean UI_usesUniqueCDKey() {
	if ( uivm ) {
		return uivm->HasUniqueCDKey();
	} else {
		return qfalse;
	}
}

/*
====================
UI_GameCommand

See if the current console command is claimed by the ui
====================
*/
qboolean UI_GameCommand( void ) {
	if ( !uivm ) {
		return qfalse;
	}

	return uivm->ConsoleCommand( cls.realtime );
}
