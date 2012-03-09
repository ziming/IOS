//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef TEAMMENU_H
#define TEAMMENU_H
#ifdef _WIN32
#pragma once
#endif

#include <vgui_controls/Frame.h>
#include <vgui_controls/Button.h>
#include <vgui_bitmapbutton.h>
#include <vgui_controls/ImagePanel.h>

#include <game/client/iviewport.h>

#include <vgui/KeyCode.h>
#include <UtlVector.h>

#include <vgui_controls/EditablePanel.h>

namespace vgui
{
	class RichText;
	class HTML;
}
class TeamFortressViewport;

using namespace vgui;

struct StatPanel_t
{
	Panel *pPanel;
	Label *pGoals;
	Label *pGoalText;
	Label *pAssists;
	Label *pAssistText;
	Label *pFouls;
	Label *pFoulsText;
	Label *pYellowCards;
	Label *pYellowCardText;
	Label *pRedCards;
	Label *pRedCardText;
	Label *pPossession;
	Label *pPossessionText;
	Label *pPing;
	Label *pPingText;
};

struct PosPanel_t
{
	Panel *pPosPanel;
	Button *pPlayerName;
	Label *pClubName;
	Label *pPosName;
	StatPanel_t *pStatPanel;
	Button *pKickButton;
	ImagePanel *pCountryFlag;
};

//-----------------------------------------------------------------------------
// Purpose: Displays the team menu
//-----------------------------------------------------------------------------
class CTeamMenu : public vgui::Frame, public IViewPortPanel
{
private:
	DECLARE_CLASS_SIMPLE( CTeamMenu, vgui::Frame );

public:
	CTeamMenu(IViewPort *pViewPort);
	virtual ~CTeamMenu();

	virtual const char *GetName( void ) { return PANEL_TEAM; }
	//ios virtual void SetData(KeyValues *data) {};
	virtual void SetData(KeyValues *data);
	virtual void Reset();
	virtual void Update();
	virtual bool NeedsUpdate( void );
	virtual bool HasInputElements( void ) { return true; }
	virtual void ShowPanel( bool bShow );

	// both vgui::Frame and IViewPortPanel define these, so explicitly define them here as passthroughs to vgui
	vgui::VPANEL GetVPanel( void ) { return BaseClass::GetVPanel(); }
  	virtual bool IsVisible() { return BaseClass::IsVisible(); }
	virtual void SetParent( vgui::VPANEL parent ) { BaseClass::SetParent( parent ); }
	
	virtual void OnCommand( char const *cmd );	//ios

	virtual void PerformLayout();

public:
	
	void AutoAssign();
	
protected:

	// int GetNumTeams() { return m_iNumTeams; }
	
	// VGUI2 overrides
	virtual void ApplySchemeSettings(vgui::IScheme *pScheme);
	virtual void OnKeyCodePressed(vgui::KeyCode code);

	IViewPort	*m_pViewPort;
//	int m_iNumTeams;
	ButtonCode_t m_iJumpKey;
	ButtonCode_t m_nGoalsBoardKey;

	//CUtlVectorFixed<CBitmapButton *, 11> m_pTeamButtons;
	PosPanel_t *m_pPosPanels[2][11];
	//StatPanel_t *m_pStatPanels[2][11];

	Button *m_pSpectateButton;
	Button *m_pTabButtons[2];

	int m_nActiveTeam;

	Label *m_pTeamNames[2];
	Label *m_pTeamPossession[2];
	Label *m_pTeamPlayerCount[2];

	Panel *m_pTeamPanels[2];

	Label *m_pSpectatorNames;

	float m_flNextUpdateTime;
	int m_nMaxPlayers;
};


#endif // TEAMMENU_H
