//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
//
// battery.cpp
//
// implementation of CHudScorebar class
//
#include "cbase.h"
#include "hudelement.h"
#include "hud_macros.h"
#include "c_playerresource.h"
#include "iclientmode.h"
#include <vgui_controls/controls.h>
#include <vgui_controls/panel.h>
#include <vgui/ISurface.h>
#include <vgui/ILocalize.h>
#include <vgui_controls/TextImage.h>
#include <KeyValues.h>
#include <game_controls/baseviewport.h>
#include "clientmode_shared.h"
#include "c_baseplayer.h"
#include "c_team.h"
#include "c_ball.h"
#include "sdk_gamerules.h"
#include <vgui_controls/ImagePanel.h>
#include "UtlVector.h"
#include "hudelement.h"
#include <vgui_controls/Panel.h>
#include "ehandle.h"
#include "c_sdk_player.h"
#include <Windows.h>
// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

struct Event_t
{
	Panel *pEventBox;
	Label *pEventType;
	Label *pEventText;
	float startTime;
};

//-----------------------------------------------------------------------------
// Purpose: Displays suit power (armor) on hud
//-----------------------------------------------------------------------------
class CHudScorebar : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CHudScorebar, vgui::Panel );

public:
	CHudScorebar( const char *pElementName );
	void Init( void );

protected:
	virtual void OnThink( void );
	virtual void ApplySchemeSettings( vgui::IScheme *scheme );

private:

	Panel *m_pEventBars[2];
	Panel *m_pTeamColors[2][2];
	CUtlVector<Event_t> m_vEventLists[2];
	Label *m_pPlayers[2];
	Label *m_pSubPlayers[2];
	Label *m_pEvent;
	Label *m_pSubEvent;
	char  m_szCurrentPlayer[MAX_PLAYER_NAME_LENGTH];
	int	  m_nCurrentPlayerTeamIndex;
	Panel *m_pMainBar;
	Panel *m_pMainBarBG;
	Panel *m_pCenterBar;
	Panel *m_pCenterBarBG;
	Label *m_pTeamNames[2];
	Label *m_pTeamGoals[2];
	Label *m_pNewState;
	Label *m_pNewTime;
	Label *m_pInjuryTime;
	Panel *m_pPenaltyPanels[2];
	Panel *m_pPenalties[2][5];

	Panel		*m_pExtensionBar[2];
	Label		*m_pExtensionText[2];

	Panel	*m_pTeamCrestPanels[2];
	ImagePanel	*m_pTeamCrests[2];

	float m_flNextPlayerUpdate;
};

DECLARE_HUDELEMENT( CHudScorebar );

#define	HEIGHT_MARGIN			3
#define HEIGHT_TIMEBAR			35
#define HEIGHT_TEAMBAR			35
#define HPADDING				5
#define VPADDING				3

#define WIDTH_MARGIN			5
#define WIDTH_OVERLAP			20
#define WIDTH_EVENTTYPE			150
#define WIDTH_EVENTTEXT			100
#define WIDTH_TEAM				115
#define WIDTH_SCORE				30
//#define WIDTH_TIMEBAR			150
#define WIDTH_MATCHSTATE		40
#define WIDTH_TIME				70
#define WIDTH_TEAMCOLOR			5
#define WIDTH_INJURYTIME		30

#define WIDTH_TEAMBAR			(HPADDING + WIDTH_TEAM + WIDTH_MARGIN + WIDTH_TEAMCOLOR + WIDTH_MARGIN + WIDTH_SCORE + HPADDING)
#define WIDTH_TIMEBAR			(HPADDING + WIDTH_MATCHSTATE + WIDTH_MARGIN + WIDTH_TIME + HPADDING)

enum { MAINBAR_WIDTH = 480, MAINBAR_HEIGHT = 40, MAINBAR_MARGIN = 15 };
enum { TEAMNAME_WIDTH = 175, TEAMNAME_MARGIN = 5 };
enum { TEAMGOAL_WIDTH = 30, TEAMGOAL_MARGIN = 10 };
enum { TIME_WIDTH = 120, TIME_MARGIN = 5, INJURY_TIME_WIDTH = 20, INJURY_TIME_MARGIN = 5 };
enum { STATE_WIDTH = 120, STATE_MARGIN = 5 };
enum { TOPEXTENSION_WIDTH = 278, TOPEXTENSION_HEIGHT = MAINBAR_HEIGHT, TOPEXTENSION_MARGIN = 10, TOPEXTENSION_TEXTMARGIN = 5, TOPEXTENSION_TEXTOFFSET = 20 };
enum { TEAMCREST_SIZE = 70, TEAMCREST_HOFFSET = 482/*265*/, TEAMCREST_VOFFSET = 0, TEAMCREST_PADDING = 5 };
enum { TEAMCOLOR_WIDTH = 5, TEAMCOLOR_HEIGHT = MAINBAR_HEIGHT - 10, TEAMCOLOR_HMARGIN = 5, TEAMCOLOR_VMARGIN = (MAINBAR_HEIGHT - TEAMCOLOR_HEIGHT) / 2 };
enum { CENTERBAR_OFFSET = 5 };
enum { BAR_BORDER = 2 };
enum { PLAYERNAME_MARGIN = 5, PLAYERNAME_OFFSET = 50, PLAYERNAME_WIDTH = 150 };
enum { PENALTYPANEL_HEIGHT = 30, PENALTYPANEL_PADDING = 5, PENALTYPANEL_TOPMARGIN = 10 };
enum { EVENT_WIDTH = 200, EVENT_HEIGHT = 35, SUBEVENT_WIDTH = 200, SUBEVENT_HEIGHT = 35 };

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CHudScorebar::CHudScorebar( const char *pElementName ) : BaseClass(NULL, "HudScorebar"), CHudElement( pElementName )
{
	SetHiddenBits(HIDEHUD_SCOREBAR);

	vgui::Panel *pParent = g_pClientMode->GetViewport();
	SetParent( pParent );

	m_pMainBarBG = new Panel(this, "");
	m_pMainBarBG->SetVisible(false);
	m_pCenterBarBG = new Panel(this, "");
	m_pCenterBarBG->SetVisible(false);
	m_pMainBar = new Panel(this, "");
	m_pCenterBar = new Panel(this, "");

	for (int i = 0; i < 2; i++)
	{
		m_pTeamColors[i][0] = new Panel(m_pMainBar, VarArgs("TeamColor%d", i));
		m_pTeamColors[i][1] = new Panel(m_pMainBar, VarArgs("TeamColor%d", i));
		m_pEventBars[i] = new Label(this, VarArgs("ScoreLabel%d", i), "");

		m_pPlayers[i] = new Label(this, "", "");
		m_pSubPlayers[i] = new Label(this, "", "");

		m_pTeamNames[i] = new Label(m_pMainBar, "", "");
		m_pTeamGoals[i] = new Label(m_pMainBar, "", "");

		m_pExtensionBar[i] = new Panel(this, "");
		m_pExtensionText[i] = new Label(m_pExtensionBar[i], "", "");
		m_pTeamCrestPanels[i] = new Panel(this, "");
		m_pTeamCrests[i] = new ImagePanel(m_pTeamCrestPanels[i], "");

		m_pPenaltyPanels[i] = new Panel(this, "");

		for (int j = 0; j < 5; j++)
		{
			m_pPenalties[i][j] = new Panel(m_pPenaltyPanels[i], "");
		}
	}

	m_pEvent = new Label(this, "", "");
	m_pSubEvent = new Label(this, "", "");

	m_pNewState = new Label(m_pCenterBar, "", "");
	m_pNewTime = new Label(m_pCenterBar, "", "");
	m_pInjuryTime = new Label(m_pCenterBar, "", "");

	m_szCurrentPlayer[0] = 0;
	m_flNextPlayerUpdate = gpGlobals->curtime;
}

void CHudScorebar::ApplySchemeSettings( IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );
	
	SetBounds(0, 0, ScreenWidth(), 700);
 	//SetPaintBackgroundType (2); // Rounded corner box
 	//SetPaintBackgroundEnabled(true);
	//SetBgColor( Color( 0, 0, 255, 255 ) );

	Color white(255, 255, 255, 255);
	Color black(0, 0, 0, 255);

	m_pMainBarBG->SetBounds(GetWide() / 2 - MAINBAR_WIDTH / 2 - BAR_BORDER, MAINBAR_MARGIN - BAR_BORDER, MAINBAR_WIDTH + 2 * BAR_BORDER, MAINBAR_HEIGHT + 2 * BAR_BORDER);
	m_pMainBarBG->SetBgColor(white);
	m_pMainBarBG->SetPaintBackgroundType(2);

	m_pCenterBarBG->SetBounds(GetWide() / 2 - STATE_WIDTH / 2 - BAR_BORDER, MAINBAR_MARGIN - CENTERBAR_OFFSET - BAR_BORDER, STATE_WIDTH + 2 * BAR_BORDER, MAINBAR_HEIGHT + 2 * CENTERBAR_OFFSET + 2 * BAR_BORDER);
	m_pCenterBarBG->SetBgColor(white);
	m_pCenterBarBG->SetPaintBackgroundType(2);

	m_pMainBar->SetBounds(GetWide() / 2 - MAINBAR_WIDTH / 2, MAINBAR_MARGIN, MAINBAR_WIDTH, MAINBAR_HEIGHT);
	m_pMainBar->SetBgColor(black);
	m_pMainBar->SetPaintBackgroundType(2);
	m_pMainBar->SetZPos(2);

	m_pCenterBar->SetBounds(GetWide() / 2 - STATE_WIDTH / 2, MAINBAR_MARGIN - CENTERBAR_OFFSET, STATE_WIDTH, MAINBAR_HEIGHT + 2 * CENTERBAR_OFFSET);
	m_pCenterBar->SetBgColor(black);
	m_pCenterBar->SetPaintBackgroundType(2);
	m_pCenterBar->SetZPos(3);

	m_pNewState->SetBounds(m_pCenterBar->GetWide() / 2 - STATE_WIDTH / 2, 0, STATE_WIDTH, (MAINBAR_HEIGHT + 2 * CENTERBAR_OFFSET) / 2);
	m_pNewState->SetFgColor(white);
	m_pNewState->SetContentAlignment(Label::a_center);
	m_pNewState->SetFont(pScheme->GetFont("IOSScorebarSmall"));

	m_pNewTime->SetBounds(m_pCenterBar->GetWide() / 2 - TIME_WIDTH / 2, (MAINBAR_HEIGHT + 10) / 2, TIME_WIDTH, (MAINBAR_HEIGHT + 10) / 2);
	m_pNewTime->SetFgColor(white);
	m_pNewTime->SetContentAlignment(Label::a_center);
	m_pNewTime->SetFont(pScheme->GetFont("IOSScorebarMedium"));

	m_pInjuryTime->SetBounds(m_pCenterBar->GetWide() - INJURY_TIME_WIDTH - INJURY_TIME_MARGIN, (MAINBAR_HEIGHT + 10) / 2, INJURY_TIME_WIDTH, (MAINBAR_HEIGHT + 10) / 2);
	m_pInjuryTime->SetFgColor(white);
	m_pInjuryTime->SetContentAlignment(Label::a_center);
	m_pInjuryTime->SetFont(pScheme->GetFont("IOSScorebarMedium"));
	
	m_pEvent->SetBounds(GetWide() / 2 - MAINBAR_WIDTH / 2, MAINBAR_MARGIN + MAINBAR_HEIGHT + 10, MAINBAR_WIDTH, EVENT_HEIGHT);
	m_pEvent->SetContentAlignment(Label::a_center);
	m_pEvent->SetFont(pScheme->GetFont("IOSEvent"));
	m_pEvent->SetFgColor(Color(255, 255, 255, 255));
	//m_pEvent->SetVisible(false);

	m_pSubEvent->SetBounds(GetWide() / 2 - MAINBAR_WIDTH / 2, MAINBAR_MARGIN + MAINBAR_HEIGHT + 10 + EVENT_HEIGHT, MAINBAR_WIDTH, SUBEVENT_HEIGHT);
	m_pSubEvent->SetContentAlignment(Label::a_center);
	m_pSubEvent->SetFont(pScheme->GetFont("IOSSubEvent"));
	m_pSubEvent->SetFgColor(Color(255, 255, 255, 255));

	Color fgColor = Color(220, 220, 220, 255);
	Color bgColor = Color(35, 30, 40, 255);
	Color bgColorTransparent = Color(30, 30, 40, 200);

	for (int i = 0; i < 2; i++)
	{
		m_pTeamNames[i]->SetBounds(i * (MAINBAR_WIDTH - TEAMNAME_WIDTH), 0, TEAMNAME_WIDTH, MAINBAR_HEIGHT);
		m_pTeamNames[i]->SetFgColor(white);
		m_pTeamNames[i]->SetContentAlignment(Label::a_center);
		m_pTeamNames[i]->SetFont(pScheme->GetFont("IOSScorebar"));

		m_pTeamGoals[i]->SetBounds(MAINBAR_WIDTH / 2 - STATE_WIDTH / 2 - TEAMGOAL_WIDTH - TEAMGOAL_MARGIN + i * (2 * TEAMGOAL_MARGIN + TEAMGOAL_WIDTH + STATE_WIDTH), 0, TEAMGOAL_WIDTH, MAINBAR_HEIGHT);
		m_pTeamGoals[i]->SetFgColor(white);
		m_pTeamGoals[i]->SetContentAlignment(i == 0 ? Label::a_east : Label::a_west);
		m_pTeamGoals[i]->SetFont(pScheme->GetFont("IOSScorebar"));

		m_pExtensionBar[i]->SetBounds(GetWide() / 2 - TOPEXTENSION_WIDTH / 2 + (i == 0 ? -1 : 1) * (MAINBAR_WIDTH / 2 + TOPEXTENSION_WIDTH / 2 - TOPEXTENSION_MARGIN), MAINBAR_MARGIN, TOPEXTENSION_WIDTH, TOPEXTENSION_HEIGHT);
		m_pExtensionBar[i]->SetBgColor(black);
		m_pExtensionBar[i]->SetPaintBackgroundType(2);
		m_pExtensionBar[i]->SetZPos(1);

		m_pExtensionText[i]->SetBounds(TOPEXTENSION_TEXTMARGIN + (i == 0 ? 1 : -1) * TOPEXTENSION_TEXTOFFSET, 0, TOPEXTENSION_WIDTH - 2 * TOPEXTENSION_TEXTMARGIN, TOPEXTENSION_HEIGHT);
		m_pExtensionText[i]->SetFgColor(white);
		m_pExtensionText[i]->SetContentAlignment(Label::a_center);
		m_pExtensionText[i]->SetFont(pScheme->GetFont("IOSScorebarExtraInfo"));

		m_pTeamCrestPanels[i]->SetBounds(GetWide() / 2 - TEAMCREST_SIZE / 2 + (i == 0 ? -1 : 1) * TEAMCREST_HOFFSET, TEAMCREST_VOFFSET, TEAMCREST_SIZE, TEAMCREST_SIZE);
		m_pTeamCrestPanels[i]->SetZPos(3);
		//m_pTeamCrestPanels[i]->SetBgColor(black);
		//m_pTeamCrestPanels[i]->SetPaintBackgroundType(2);

		m_pTeamCrests[i]->SetBounds(TEAMCREST_PADDING, TEAMCREST_PADDING, TEAMCREST_SIZE - 2 * TEAMCREST_PADDING, TEAMCREST_SIZE - 2 * TEAMCREST_PADDING);
		m_pTeamCrests[i]->SetShouldScaleImage(true);
		m_pTeamCrests[i]->SetImage(i == 0 ? "hometeamcrest" : "awayteamcrest");
			
		m_pTeamColors[i][0]->SetBounds(TEAMCOLOR_HMARGIN + i * (MAINBAR_WIDTH - 2 * TEAMCOLOR_WIDTH - 2 * TEAMCOLOR_HMARGIN), TEAMCOLOR_VMARGIN, TEAMCOLOR_WIDTH, MAINBAR_HEIGHT - 2 * TEAMCOLOR_VMARGIN);
		m_pTeamColors[i][1]->SetBounds(TEAMCOLOR_HMARGIN + TEAMCOLOR_WIDTH + i * (MAINBAR_WIDTH - 2 * TEAMCOLOR_WIDTH - 2 * TEAMCOLOR_HMARGIN), TEAMCOLOR_VMARGIN, TEAMCOLOR_WIDTH, MAINBAR_HEIGHT - 2 * TEAMCOLOR_VMARGIN);

		m_pEventBars[i]->SetBounds(0, i * (HEIGHT_TEAMBAR + HEIGHT_MARGIN), 0, HEIGHT_TEAMBAR);
		m_pEventBars[i]->SetPaintBackgroundType(2);
		m_pEventBars[i]->SetBgColor(bgColorTransparent);
		m_pEventBars[i]->SetZPos(-1);

		m_pPlayers[i]->SetBounds(m_pMainBar->GetX() + m_pTeamNames[i]->GetX(), MAINBAR_MARGIN + MAINBAR_HEIGHT + 10, m_pTeamNames[i]->GetWide(), EVENT_HEIGHT);
		m_pPlayers[i]->SetContentAlignment(Label::a_center);
		m_pPlayers[i]->SetFont(pScheme->GetFont("IOSEventPlayer"));
		m_pPlayers[i]->SetFgColor(Color(255, 255, 255, 255));
		//m_pPlayers[i]->SetTextInset(5, 0);
		//m_pPlayers[i]->SetVisible(false);

		m_pSubPlayers[i]->SetBounds(m_pMainBar->GetX() + m_pTeamNames[i]->GetX(), MAINBAR_MARGIN + MAINBAR_HEIGHT + 10 + EVENT_HEIGHT, m_pTeamNames[i]->GetWide(), SUBEVENT_HEIGHT);
		m_pSubPlayers[i]->SetContentAlignment(Label::a_center);
		m_pSubPlayers[i]->SetFont(pScheme->GetFont("IOSSubEventPlayer"));
		m_pSubPlayers[i]->SetFgColor(Color(255, 255, 255, 255));

		m_pPenaltyPanels[i]->SetBounds(m_pMainBar->GetX() + m_pTeamNames[i]->GetX(), MAINBAR_MARGIN + MAINBAR_HEIGHT + 10 + EVENT_HEIGHT + PENALTYPANEL_TOPMARGIN, m_pTeamNames[i]->GetWide(), PENALTYPANEL_HEIGHT);
		m_pPenaltyPanels[i]->SetBgColor(Color(0, 0, 0, 255));

		for (int j = 0; j < 5; j++)
		{
			m_pPenalties[i][j]->SetBounds(j * (m_pPenaltyPanels[i]->GetWide() / 5) + PENALTYPANEL_PADDING, PENALTYPANEL_PADDING, m_pPenaltyPanels[i]->GetWide() / 5 - 2 * PENALTYPANEL_PADDING, PENALTYPANEL_HEIGHT - 2 * PENALTYPANEL_PADDING);
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudScorebar::Init( void )
{
}

const char *g_szStateNames[32] =
{
	"WU",
	"H1",
	"H1",
	"HT",
	"H2",
	"H2",
	"ETB",
	"ETH1",
	"ETH1",
	"ETHT",
	"ETH2",
	"ETH2",
	"PSB",
	"PS",
	"CD"
};

const char *g_szLongStateNames[32] =
{
	"WARMUP",
	"FIRST HALF",
	"FIRST HALF",
	"HALF TIME",
	"SECOND HALF",
	"SECOND HALF",
	"EX BREAK",
	"EX FIRST HALF",
	"EX FIRST HALF",
	"EX HALF TIME",
	"EX SECOND HALF",
	"EX SECOND HALF",
	"PEN BREAK",
	"PENALTIES",
	"COOLDOWN"
};

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudScorebar::OnThink( void )
{
	if (!SDKGameRules() || !GetGlobalTeam(TEAM_A) || !GetGlobalTeam(TEAM_B))
		return;

	if (SDKGameRules()->State_Get() == MATCH_PENALTIES)
	{
		for (int i = 0; i < 2; i++)
		{
			int relativeRound = GetGlobalTeam(TEAM_A + i)->m_nPenaltyRound == 0 ? -1 : (GetGlobalTeam(TEAM_A + i)->m_nPenaltyRound - 1) % 5;
			int fullRounds = max(0, GetGlobalTeam(TEAM_A + i)->m_nPenaltyRound - 1) / 5;
			for (int j = 0; j < 5; j++)
			{
				Color color;
				if (j > relativeRound)
					color = Color(100, 100, 100, 255);
				else
				{
					if ((GetGlobalTeam(TEAM_A + i)->m_nPenaltyGoalBits & (1 << (j + fullRounds * 5))) != 0)
						color = Color(0, 255, 0, 255);
					else
						color = Color(255, 0, 0, 255);
				}
				m_pPenalties[i][j]->SetBgColor(color);
			}
			m_pPenaltyPanels[i]->SetVisible(true);
		}
	}
	else
	{
		for (int i = 0; i < 2; i++)
		{
			m_pPenaltyPanels[i]->SetVisible(false);
		}
	}

	if (gViewPortInterface->FindPanelByName(PANEL_SCOREBOARD)->IsVisible())
	{
		//wchar_t text[64];
		//_snwprintf(text, ARRAYSIZE(text), L"%d pl.  �   %d%% poss.", GetGlobalTeam(TEAM_A)->GetNumPlayers(), GetGlobalTeam(TEAM_A)->Get_Possession());
		//m_pExtensionText[0]->SetText(text);
		//m_pExtensionText[0]->SetText(VarArgs("%d pl.     %d%% poss.", GetGlobalTeam(TEAM_A)->GetNumPlayers(), GetGlobalTeam(TEAM_A)->Get_Possession()));
		//m_pExtensionText[1]->SetText(VarArgs("%d%% poss.     %d pl.", GetGlobalTeam(TEAM_B)->Get_Possession(), GetGlobalTeam(TEAM_B)->GetNumPlayers()));

		for (int i = 0; i < 2; i++)
		{
			m_pExtensionText[i]->SetText(VarArgs("%d pl.     %d%% poss.", GetGlobalTeam(TEAM_A + i)->GetNumPlayers(), GetGlobalTeam(TEAM_A + i)->Get_Possession()));
			m_pExtensionText[i]->SetFgColor(GetGlobalTeam(TEAM_A + i)->Get_HudKitColor());
			m_pExtensionBar[i]->SetVisible(true);
			m_pTeamCrestPanels[i]->SetVisible(GameResources()->HasTeamCrest(TEAM_A + i));
		}
	}
	else
	{
		for (int i = 0; i < 2; i++)
		{
			m_pExtensionBar[i]->SetVisible(false);
			m_pTeamCrestPanels[i]->SetVisible(false);
		}
	}

	int nTime = SDKGameRules()->GetMatchDisplayTimeSeconds();
	nTime = abs(nTime);

	m_pNewState->SetText(g_szLongStateNames[SDKGameRules()->State_Get()]);
	char *szInjuryTime = (SDKGameRules()->m_nAnnouncedInjuryTime > 0) ? VarArgs("+%d", SDKGameRules()->m_nAnnouncedInjuryTime) : "";
	m_pInjuryTime->SetText(szInjuryTime);
	m_pNewTime->SetText(VarArgs("%d:%02d", nTime / 60, nTime % 60));

	for (int team = TEAM_A; team <= TEAM_B; team++)
	{
		m_pTeamNames[team - TEAM_A]->SetText(GetGlobalTeam(team)->Get_ShortTeamName());
		m_pTeamNames[team - TEAM_A]->SetFgColor(GetGlobalTeam(team)->Get_HudKitColor());
		m_pTeamColors[team - TEAM_A][0]->SetBgColor(GetGlobalTeam(team)->Get_PrimaryKitColor());
		m_pTeamColors[team - TEAM_A][1]->SetBgColor(GetGlobalTeam(team)->Get_SecondaryKitColor());
		m_pTeamGoals[team - TEAM_A]->SetText(VarArgs("%d", GetGlobalTeam(team)->Get_Goals()));
	}

	C_Ball *pBall = GetBall();
	if (pBall)
	{
		if (SDKGameRules()->IsIntermissionState())
		{
			m_pPlayers[0]->SetText("");
			m_pPlayers[1]->SetText("");
			m_pSubPlayers[0]->SetText("");
			m_pSubPlayers[1]->SetText("");
			m_szCurrentPlayer[0] = 0;
		}
		else
		{
			m_pEvent->SetText(g_szMatchEventNames[pBall->m_eMatchEvent]);
			m_pSubEvent->SetText(g_szMatchEventNames[pBall->m_eMatchSubEvent]);
			//m_pEvent->SetFgColor(Color(255, 255, 255, 255));

			if (pBall->m_eBallState == BALL_NORMAL)
			{
				if (pBall->m_pPl || Q_strlen(m_szCurrentPlayer) > 0)
				{
					if (pBall->m_pPl)
					{
						Q_strncpy(m_szCurrentPlayer, GameResources()->GetPlayerName(pBall->m_pPl->entindex()), sizeof(m_szCurrentPlayer));
						m_nCurrentPlayerTeamIndex = clamp(GameResources()->GetTeam(pBall->m_pPl->entindex()) - TEAM_A, 0, 1); //FIXME: fix properly
					}

					if (gpGlobals->curtime >= m_flNextPlayerUpdate)
					{
						m_pPlayers[m_nCurrentPlayerTeamIndex]->SetText(m_szCurrentPlayer);
						m_pPlayers[m_nCurrentPlayerTeamIndex]->SetFgColor(GetGlobalTeam(TEAM_A + m_nCurrentPlayerTeamIndex)->Get_HudKitColor());
						m_pPlayers[1 - m_nCurrentPlayerTeamIndex]->SetText("");
						m_flNextPlayerUpdate = gpGlobals->curtime + 1.0f;
					}
				}

				m_pSubPlayers[0]->SetText("");
				m_pSubPlayers[1]->SetText("");
			}
			else
			{
				if (pBall->m_pMatchEventPlayer)
				{
					int teamIndex = clamp(pBall->m_nMatchEventTeam - TEAM_A, 0, 1);
					m_pPlayers[teamIndex]->SetText(pBall->m_pMatchEventPlayer->GetPlayerName());
					m_pPlayers[teamIndex]->SetFgColor(GetGlobalTeam(TEAM_A + teamIndex)->Get_HudKitColor());
					m_pPlayers[1 - teamIndex]->SetText("");
					m_pEvent->SetFgColor(GetGlobalTeam(TEAM_A + teamIndex)->Get_HudKitColor());
				}
				
				if (pBall->m_pMatchSubEventPlayer)
				{
					int teamIndex = clamp(pBall->m_nMatchSubEventTeam - TEAM_A, 0, 1);
					m_pSubPlayers[teamIndex]->SetText(pBall->m_pMatchSubEventPlayer->GetPlayerName());
					m_pSubPlayers[teamIndex]->SetFgColor(GetGlobalTeam(TEAM_A + teamIndex)->Get_HudKitColor());
					m_pSubPlayers[1 - teamIndex]->SetText("");
					m_pSubEvent->SetFgColor(GetGlobalTeam(TEAM_A + teamIndex)->Get_HudKitColor());
				}
				else
				{
					m_pSubPlayers[0]->SetText("");
					m_pSubPlayers[1]->SetText("");
				}
			}
		}
	}
}