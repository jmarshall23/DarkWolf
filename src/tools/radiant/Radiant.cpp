/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Foobar; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// Radiant.cpp : Defines the class behaviors for the application.
//

#include "stdafx.h"
#include "Radiant.h"

#include "MainFrm.h"
#include "ChildFrm.h"
#include "RadiantDoc.h"
#include "RadiantView.h"
#include "PrefsDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CRadiantApp

BEGIN_MESSAGE_MAP(CRadiantApp, CWinApp)
	//{{AFX_MSG_MAP(CRadiantApp)
	ON_COMMAND(ID_HELP, OnHelp)
	//}}AFX_MSG_MAP
	// Standard file based document commands
	ON_COMMAND(ID_FILE_NEW, CWinApp::OnFileNew)
	ON_COMMAND(ID_FILE_OPEN, CWinApp::OnFileOpen)
	// Standard print setup command
	ON_COMMAND(ID_FILE_PRINT_SETUP, CWinApp::OnFilePrintSetup)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CRadiantApp construction

CRadiantApp::CRadiantApp()
{
	// TODO: add construction code here,
	// Place all significant initialization in InitInstance
}

/////////////////////////////////////////////////////////////////////////////
// The one and only CRadiantApp object

CRadiantApp theApp;

/////////////////////////////////////////////////////////////////////////////
// CRadiantApp initialization

HINSTANCE g_hOpenGL32 = NULL;
HINSTANCE g_hOpenGL = NULL;
bool g_bBuildList = false;

BOOL CRadiantApp::InitInstance()
{
	// Standard initialization

#if 0
#ifdef _AFXDLL
	Enable3dControls();
#else
	Enable3dControlsStatic();
#endif
#endif

	// If there's a .INI file in the directory use it instead of registry
	char RadiantPath[_MAX_PATH];
	GetModuleFileName(NULL, RadiantPath, _MAX_PATH);

	CFileFind Finder;
	Finder.FindFile(RadiantPath);
	Finder.FindNextFile();

	CString Root = Finder.GetRoot();
	CString IniPath = Root + "\\REGISTRY.INI";

	Finder.FindNextFile();
	if (Finder.FindFile(IniPath))
	{
		Finder.FindNextFile();

		free((void*)m_pszProfileName);
		m_pszProfileName = _tcsdup(_T(Finder.GetFilePath()));

		int i = 0;
		CString key;
		HKEY hkResult;
		DWORD dwDisp;
		DWORD type;
		char iBuf[16];

		do
		{
			sprintf(iBuf, "%d", i);
			key = "Software\\Q3Radiant\\IniPrefs" + CString(iBuf);

			if (RegOpenKeyEx(HKEY_CURRENT_USER, key, 0, KEY_ALL_ACCESS, &hkResult) != ERROR_SUCCESS)
			{
				strcpy(g_qeglobals.use_ini_registry, key.GetBuffer(0));

				RegCreateKeyEx(
					HKEY_CURRENT_USER,
					key,
					0,
					NULL,
					REG_OPTION_NON_VOLATILE,
					KEY_ALL_ACCESS,
					NULL,
					&hkResult,
					&dwDisp);

				RegSetValueEx(
					hkResult,
					"RadiantName",
					0,
					REG_SZ,
					reinterpret_cast<const BYTE*>(RadiantPath),
					(DWORD)strlen(RadiantPath) + 1);

				RegCloseKey(hkResult);
				break;
			}
			else
			{
				char RadiantAux[_MAX_PATH] = { 0 };
				unsigned long size = _MAX_PATH;

				RegQueryValueEx(
					hkResult,
					"RadiantName",
					0,
					&type,
					reinterpret_cast<BYTE*>(RadiantAux),
					&size);

				RegCloseKey(hkResult);

				if (!strcmp(RadiantAux, RadiantPath))
				{
					strcpy(g_qeglobals.use_ini_registry, key.GetBuffer(0));
					break;
				}
			}

			++i;
		} while (1);

		g_qeglobals.use_ini = true;
	}
	else
	{
		SetRegistryKey("Q3Radiant");
		g_qeglobals.use_ini = false;
	}

	LoadStdProfileSettings();

	g_PrefsDlg.LoadPrefs();

	int nMenu = IDR_MENU1;

	CString strOpenGL = (g_PrefsDlg.m_bSGIOpenGL) ? "opengl.dll" : "opengl32.dll";
	CString strGLU = (g_PrefsDlg.m_bSGIOpenGL) ? "glu.dll" : "glu32.dll";

	
	{
		g_PrefsDlg.m_bSGIOpenGL ^= 1;
		strOpenGL = (g_PrefsDlg.m_bSGIOpenGL) ? "opengl.dll" : "opengl32.dll";
		strGLU = (g_PrefsDlg.m_bSGIOpenGL) ? "glu.dll" : "glu32.dll";

		g_PrefsDlg.SavePrefs();
	}

	CString strTemp = m_lpCmdLine;
	strTemp.MakeLower();
	if (strTemp.Find("builddefs") >= 0)
		g_bBuildList = true;

	CMainFrame* pMainFrame = new CMainFrame;
	if (!pMainFrame->LoadFrame(nMenu))
		return FALSE;

	if (pMainFrame->m_hAccelTable)
		::DestroyAcceleratorTable(pMainFrame->m_hAccelTable);

	pMainFrame->LoadAccelTable(MAKEINTRESOURCE(IDR_MINIACCEL));

	m_pMainWnd = pMainFrame;

	// Parse command line
	CCommandLineInfo cmdInfo;
	ParseCommandLine(cmdInfo);

	// ---- UI polish / icon setup ----

	// Set both large and small frame icons to IDI_ICON1
	HICON hAppIcon = AfxGetApp()->LoadIcon(IDI_ICON1);
	if (hAppIcon)
	{
		pMainFrame->SetIcon(hAppIcon, TRUE);   // big icon
		pMainFrame->SetIcon(hAppIcon, FALSE);  // small icon
	}

	// Optional nicer title
	pMainFrame->SetWindowText("Wolfenstein Editor");

	// Enable tooltip support
	pMainFrame->EnableToolTips(TRUE);

	// Show maximized if you want the editor to feel more modern/fullscreen-ish
	pMainFrame->ShowWindow(SW_SHOWMAXIMIZED);
	// If you do not want maximized startup, use:
	// pMainFrame->ShowWindow(m_nCmdShow);

	pMainFrame->UpdateWindow();
	pMainFrame->RedrawWindow(NULL, NULL,
		RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);

	// Help file
	free((void*)m_pszHelpFilePath);
	CString strHelp = g_strAppPath;
	AddSlash(strHelp);
	strHelp += "Q3RManual.chm";
	m_pszHelpFilePath = _tcsdup(strHelp);

	return TRUE;
}

/////////////////////////////////////////////////////////////////////////////
// CRadiantApp commands

int CRadiantApp::ExitInstance() 
{
	// TODO: Add your specialized code here and/or call the base class
  //::FreeLibrary(g_hOpenGL32);
	return CWinApp::ExitInstance();
}

BOOL CRadiantApp::OnIdle(LONG lCount) 
{
	if (g_pParentWnd)
    g_pParentWnd->RoutineProcessing();
	return CWinApp::OnIdle(lCount);
}

void CRadiantApp::OnHelp() 
{
  ShellExecute(m_pMainWnd->GetSafeHwnd(), "open", m_pszHelpFilePath, NULL, NULL, SW_SHOW);
}
