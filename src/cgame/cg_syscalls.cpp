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

// cg_syscalls.c -- this file is only included when building a dll
// cg_syscalls.asm is included instead when building a qvm
#include "cg_local.h"
#include "../ui/ui_shared.h"

extern displayContextDef_t cgDC;

void CG_Init(int serverMessageNum, int serverCommandSequence);
void CG_Shutdown(void);

class idClientGameVMLocal : public idClientGameVM {
public:
	
	virtual int GetTag(int clientNum, char* tagName, orientation_t* tag) override {
		return ::CG_GetTag(clientNum, tagName, tag);
	}

	virtual void DrawActiveFrame(int serverTime, stereoFrame_t stereoView, qboolean demoPlayback) override {
		::CG_DrawActiveFrame(serverTime, stereoView, demoPlayback);
	}

	virtual void EventHandling(int type) override {
		::CG_EventHandling(type);
	}

	virtual void Init(int serverMessageNum, int serverCommandSequence) override {
		::CG_Init(serverMessageNum, serverCommandSequence);
	}

	virtual void Shutdown() override {
		::CG_Shutdown();
	}

	virtual int ConsoleCommand() override {
		return ::CG_ConsoleCommand();
	}

	virtual int CrosshairPlayer() override {
		return ::CG_CrosshairPlayer();
	}

	virtual int LastAttacker() override {
		return ::CG_LastAttacker();
	}

	virtual void KeyEvent(int key, qboolean down) override {
		::CG_KeyEvent(key, down);
	}

	virtual void MouseEvent(int dx, int dy) override {
		cgDC.cursorx = cgs.cursorX;
		cgDC.cursory = cgs.cursorY;
		::CG_MouseEvent(dx, dy);
	}
};

static idClientGameVMLocal vm;
idClientGameVM* cgvm = &vm;
idCGSystemCalls* sys;

idClientGameVM* dllEntry(int version, idCGSystemCalls* sys) {
	if (version != CGAME_IMPORT_API_VERSION) {
		sys->Error("cgame version mismatch!");
	}

	::sys = sys;
	return &vm;
}
