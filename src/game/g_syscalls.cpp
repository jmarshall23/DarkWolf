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
#include "g_local.h"

void G_InitGame(int levelTime, int randomSeed, int restart);
void G_RunFrame(int levelTime);
void G_ShutdownGame(int restart);
void CheckExitRules(void);

// Ridah, Cast AI
qboolean AICast_VisibleFromPos(vec3_t srcpos, int srcnum,
	vec3_t destpos, int destnum, qboolean updateVisPos);
qboolean AICast_CheckAttackAtPos(int entnum, int enemy, vec3_t pos, qboolean ducking, qboolean allowHitWorld);
void AICast_Init(void);
// done.

void G_RetrieveMoveSpeedsFromClient(int entnum, char* text);
class idGameVMLocal : public idGameVM {
public:
	virtual ~idGameVMLocal() override = default;

	virtual void InitGame(int levelTime, int randomSeed, int restart) override {
		G_InitGame(levelTime, randomSeed, restart);
	}

	virtual void ShutdownGame(int restart) override {
		G_ShutdownGame(restart);
	}

	virtual char * ClientConnect(int clientNum, qboolean firstTime, qboolean isBot) override {
		return ::ClientConnect(clientNum, firstTime, isBot);
	}

	virtual void ClientThink(int clientNum) override {
		::ClientThink(clientNum);
	}

	virtual void ClientUserinfoChanged(int clientNum) override {
		::ClientUserinfoChanged(clientNum);
	}

	virtual void ClientDisconnect(int clientNum) override {
		::ClientDisconnect(clientNum);
	}

	virtual void ClientBegin(int clientNum) override {
		::ClientBegin(clientNum);
	}

	virtual void ClientCommand(int clientNum) override {
		::ClientCommand(clientNum);
	}

	virtual void RunFrame(int levelTime) override {
		G_RunFrame(levelTime);
	}

	virtual int ConsoleCommand() override {
		return ::ConsoleCommand();
	}

	virtual int BotAIStartFrame(int time) override {
		return ::BotAIStartFrame(time);
	}

	virtual int AICastVisibleFromPos(float* srcpos, int srcnum, float* destpos, int destnum, qboolean updateVisPos) override {
		return ::AICast_VisibleFromPos(srcpos, srcnum, destpos, destnum, updateVisPos);
	}

	virtual int AICastCheckAttackAtPos(int entnum, int enemy, float* pos, qboolean ducking, qboolean allowHitWorld) override {
		return ::AICast_CheckAttackAtPos(entnum, enemy, pos, ducking, allowHitWorld);
	}

	virtual void RetrieveMoveSpeedsFromClient(int entnum, char* text) override {
		::G_RetrieveMoveSpeedsFromClient(entnum, text);
	}

	virtual int GetModelInfo(int clientNum, char* modelName, animModelInfo_t** modelInfo) override {
		return ::G_GetModelInfo(clientNum, modelName, modelInfo);
	}
};

idGameSystemCalls* sys;

static idGameVMLocal vm;
idGameVM* gvm = &vm;

idGameVM* dllEntry(int version, idGameSystemCalls* sys) {
	if (version != GAME_API_VERSION) {
		sys->Error("Game version mismatch!");
	}

	::sys = sys;
	return &vm;
}
