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

#include "ui_local.h"

idUISystemCalls* sys;

void _UI_Init(qboolean);
void _UI_Shutdown(void);
void _UI_KeyEvent(int key, qboolean down);
void _UI_MouseEvent(int dx, int dy);
void _UI_Refresh(int realtime);
qboolean _UI_IsFullscreen(void);

class idUserInterfaceVMLocal : public idUserInterfaceVM {
public:
	virtual ~idUserInterfaceVMLocal() override = default;

	virtual void Init(qboolean inGame) override {
		_UI_Init(inGame);
	}

	virtual void Shutdown() override {
		_UI_Shutdown();
	}

	virtual void KeyEvent(int key, qboolean down) override {
		_UI_KeyEvent(key, down);
	}

	virtual void MouseEvent(int dx, int dy) override {
		_UI_MouseEvent(dx, dy);
	}

	virtual void Refresh(int realtime) override {
		_UI_Refresh(realtime);
	}

	virtual qboolean IsFullscreen() override {
		return _UI_IsFullscreen();
	}

	virtual void SetActiveMenu(uiMenuCommand_t menu) override {
		_UI_SetActiveMenu(menu);
	}

	virtual int GetActiveMenu() override {
		return _UI_GetActiveMenu();
	}

	virtual qboolean ConsoleCommand(int realtime) override {
		return UI_ConsoleCommand(realtime);
	}

	virtual void DrawConnectScreen(qboolean overlay) override {
		UI_DrawConnectScreen(overlay);
	}

	virtual qboolean HasUniqueCDKey() override {
		return qtrue;
	}
};

static idUserInterfaceVMLocal vm;
idUserInterfaceVM* uivm = &vm;

idUserInterfaceVM *dllEntry(int version, idUISystemCalls *sys) {
	if (version != UI_API_VERSION) {
		sys->Error("UI version mismatch!");
	}
	
	::sys = sys;
	return &vm;
}
