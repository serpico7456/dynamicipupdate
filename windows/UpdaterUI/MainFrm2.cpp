// Copyright (c) 2009 OpenDNS, LLC. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "stdafx.h"

#if MAIN_FRM == 2
#include "MainFrm2.h"

#define TXT_DIV_ACCOUNT _T("OpenDNS account")
#define TXT_DIV_NETWORK_TO_UPDATE _T("Network to update")
#define TXT_DIV_IP_ADDRESS _T("IP address")
#define TXT_DIV_STATUS _T("Status")
#define TXT_DIV_UPDATE _T("Update")

static bool HasDynamicNetworkWithLabel(NetworkInfo *head, const char *label)
{
	while (head) {
		if (head->isDynamic) {
			if (streq(head->label, label))
				return true;
		}
		head = head->next;
	}
	return false;
}

static void LogIpUpdate(char *resp)
{
	slog("sent ip update for\nuser '");
	assert(g_pref_user_name);
	if (g_pref_user_name)
		slog(g_pref_user_name);
	slog("', \nresponse: '");
	if (resp)
		slog(resp);
	slog("'\nurl: ");
	const char *urlTxt = GetIpUpdateUrl();
	if (urlTxt)
		slog(urlTxt);
	free((void*)urlTxt);
	slog("\nhost: ");
	slog(GetIpUpdateHost());
	slog("\n");
}

static TCHAR *GetNetworkName()
{
	char *name = "default";
	if (!strempty(g_pref_hostname))
		name = g_pref_hostname;
	return StrToTStr(name);
}

static TCHAR *AccountName()
{
	char *name = "Not logged in";
	if (!strempty(g_pref_user_name))
		name = g_pref_user_name;
	return StrToTStr(name);
}

CMainFrame::CMainFrame()
{
	m_ipFromDns = IP_UNKNOWN;
	m_ipFromHttp = NULL;
	m_ipUpdateResult = IpUpdateOk;
	m_simulatedError = SE_NO_ERROR;
	m_editErrorMsgRequestedDy = 0;
	m_editFontName = NULL;
	m_minWinDx = 320;
	// this is absolute minimum height of the window's client area
	m_minWinDy = 200;
	m_minStatusEditDx = 320 - 16;
	m_uiState = UI_STATE_VISIBLE;
	m_minutesSinceLastUpdate = 0;
	m_winBgColorBrush = ::CreateSolidBrush(colWinBg);
}

CMainFrame::~CMainFrame()
{
	free(m_ipFromHttp);
	free(m_editFontName);
	DeleteObject(m_winBgColorBrush);
}

BOOL CMainFrame::PreTranslateMessage(MSG* pMsg)
{
	return CFrameWindowImpl<CMainFrame>::PreTranslateMessage(pMsg);
}

void CMainFrame::OnClose()
{
	BOOL sendingUpdates = GetPrefValBool(g_pref_send_updates);
	if (CanSendIPUpdates() && sendingUpdates && !IsLeftAltAndCtrlPressed()) {
		SwitchToHiddenState();
		SetMsgHandled(TRUE);
	} else {
		SetMsgHandled(FALSE);
	}
}

LRESULT CMainFrame::OnLButtonDown(UINT /*nFlags*/, CPoint /*point*/)
{
	SetFocus();
	return 0;
}

void CMainFrame::OnGetMinMaxInfo(LPMINMAXINFO lpMMI)
{
	lpMMI->ptMinTrackSize.x = m_minWinDx;
	lpMMI->ptMinTrackSize.y = m_minWinDy;
}

// returns true if the time has changed and we need to update
// the text in UI
bool CMainFrame::GetLastIpUpdateTime()
{
	// optimization: not showing ui => no need to update
	if (m_uiState != UI_STATE_VISIBLE)
		return false;
	int minutesSinceLastUpdate = m_updaterThread->MinutesSinceLastUpdate();
#if 0
	// we don't want to show "0 minutes ago", so show at least "1 minute ago"
	if (0 == minutesSinceLastUpdate)
		minutesSinceLastUpdate = 1;
#endif
	if (minutesSinceLastUpdate == m_minutesSinceLastUpdate)
		return false;
	m_minutesSinceLastUpdate = minutesSinceLastUpdate;
	return true;
}

void CMainFrame::UpdateLastUpdateText()
{
	if (!GetLastIpUpdateTime())
		return;
	UpdateStatusEdit();
}

BOOL CMainFrame::OnIdle()
{
	return FALSE;
}

void CMainFrame::ChangeNetwork(int supressFlags)
{
	StartDownloadNetworks(g_pref_token, supressFlags);
	UpdateStatusEdit();
}

LRESULT CMainFrame::OnSelChange(LPNMHDR /*pnmh*/)
{
	SetFocus();
	return 0;
}

void CMainFrame::OnChangeAccount(UINT /*uNotifyCode*/, int /*nID*/, CWindow /*wndCtl*/)
{
	ChangeAccount();
}

void CMainFrame::OnChangeNetwork(UINT /*uNotifyCode*/, int /*nID*/, CWindow /*wndCtl*/)
{
	ChangeNetwork(0);
}

void CMainFrame::OnSendUpdate(UINT /*uNotifyCode*/, int /*nID*/, CWindow /*wndCtl*/)
{
	m_updaterThread->ForceSendIpUpdate();
	UpdateStatusEdit();
}

void CMainFrame::OnSendUpdatesButtonClicked(UINT /*uNotifyCode*/, int /*nID*/, CWindow wndCtl)
{
	CButton b = wndCtl;
	BOOL checked = b.GetCheck();
	SetPrefValBool(&g_pref_send_updates, checked);
	PreferencesSave();
}

// sent by rich edit control so that we can know its desired height
LRESULT CMainFrame::OnRequestResize(LPNMHDR pnmh)
{
	REQRESIZE* r = (REQRESIZE*)pnmh;
	m_editErrorMsgRequestedDy =  RectDy(r->rc);
	return 0;
}

bool CMainFrame::IsLink(HWND hwnd)
{
	if (hwnd == m_linkAbout.m_hWnd)
		return true;
	return false;
}

bool CMainFrame::IsCheckBoxButton(HWND hwnd)
{
	if (hwnd == m_buttonSendIpUpdates.m_hWnd)
		return true;
	return false;
}

bool CMainFrame::IsStatic(HWND /*hwnd*/)
{
	return false;
}

void CMainFrame::DrawDivider(CDCHandle dc, const TCHAR *txt, CRect& rect)
{
	CRect      rc;
	GetClientRect(rc);

	dc.FillSolidRect(rect, colDividerBg);

	HFONT prevFont = dc.SelectFont(m_dividerTextFont);
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(colDividerTxt);
	int x = LEFT_MARGIN + DIVIDER_TEXT_LEFT_MARGIN;
	int y = rect.top + DIVIDER_TEXT_TOP_MARGIN;
	dc.TextOut(x, y, txt);
	dc.SelectFont(prevFont);
}

BOOL CMainFrame::OnEraseBkgnd(CDCHandle dc)
{
	CRect		rc;
	TCHAR *		txt;
	int			x, y;

	GetClientRect(rc);
	{
		// Double-buffering. In our case we don't draw enough to make a difference
		CMemoryDC dc(dc, rc);
		CDCHandle dc2(dc);

		// paint top bar in a bluish gradient
		TRIVERTEX        vert[2] ;
		GRADIENT_RECT    gRect;
		vert[0].x      = 0;
		vert[0].y      = 0;
		vert[0].Red    = 0x6b00;
		vert[0].Green  = 0x7900;
		vert[0].Blue   = 0xde00;
		vert[0].Alpha  = 0x0000;

		vert[1].x      = RectDx(rc);
		vert[1].y      = TOP_BAR_DY; 
		vert[1].Red    = 0x8400;
		vert[1].Green  = 0xa600;
		vert[1].Blue   = 0xef00;
		vert[1].Alpha  = 0x0000;

		gRect.UpperLeft  = 0;
		gRect.LowerRight = 1;
		GradientFill(dc, vert, 2, &gRect, 1, GRADIENT_FILL_RECT_H);

		// paint solid background everywhere except in top bar
		//rc.MoveToY(TOP_BAR_DY);
		rc.MoveToY(0);
		dc.FillSolidRect(rc, colWinBg);

		DrawDivider(dc2, TXT_DIV_ACCOUNT, m_txtAccountRect);
		DrawDivider(dc2, TXT_DIV_NETWORK_TO_UPDATE, m_txtNetworkRect);
		DrawDivider(dc2, TXT_DIV_IP_ADDRESS, m_txtIpAddressRect);
		DrawDivider(dc2, TXT_DIV_STATUS, m_txtStatusRect);
		DrawDivider(dc2, TXT_DIV_UPDATE, m_txtUpdateRect);

		//HFONT prevFont = dc.SelectFont(m_dividerTextFont);
		HFONT prevFont = dc.SelectFont(m_defaultGuiFont);
		dc.SetBkMode(TRANSPARENT);
		dc.SetTextColor(colBlack);
		x = LEFT_MARGIN + DIVIDER_TEXT_LEFT_MARGIN;

		// Draw account
		y = m_txtAccountRect.bottom + DIVIDER_Y_SPACING + 6;
		txt = AccountName();
		dc.TextOut(x, y, txt);
		free(txt);

		// Draw network name
		y = m_txtNetworkRect.bottom + DIVIDER_Y_SPACING + 6;
		txt = GetNetworkName();
		dc.TextOut(x, y, txt);
		free(txt);

		// Draw IP address
		y = m_txtIpAddressRect.bottom + DIVIDER_Y_SPACING;
		txt = IpAddress();
		dc.TextOut(x, y, txt);
		free(txt);

		// Draw "Using OpenDNS:" "Yes"/"No"
		y = m_txtStatusRect.bottom + DIVIDER_Y_SPACING;
		dc.TextOutA(x, y, _T("Using OpenDNS: Yes"));

		// Draw "Last update: "
		y = m_txtUpdateRect.bottom + DIVIDER_Y_SPACING + 6;
		txt = LastUpdateTxt();
		dc.TextOutA(x, y, txt);
		free(txt);

		dc.SelectFont(prevFont);

		y += 24;
		rc.top = y;
		rc.bottom = y + 1;
		dc.FillSolidRect(rc, colBlack);

#if 0
		// draw top bar text
		HFONT prevFont = dc.SelectFont(m_topBarFont);
		dc.SetBkMode(TRANSPARENT);
		dc.SetTextColor(colWhite);
		dc.TextOut(m_topBarX, m_topBarY, TOP_BAR_TXT);
		dc.SelectFont(prevFont);
#endif
	}
	return 1;
}

HBRUSH CMainFrame::OnCtlColorStatic(CDCHandle dc, CWindow wnd)
{
	HWND hwnd = wnd;
	// TODO: could probabably do IsLink() and IsStatic() by
	// comparing WINDOWINFO.atomWindowType (obtained via GetWindowInfo())
	// with ATOM corresponding to syslink and static classes found
	// with GetClassInfo()
	if (IsLink(hwnd)) {
		dc.SetBkColor(colWinBg);
		return 0;
	} else if (IsStatic(hwnd)) {
		//dc.SetBkColor(colWinBg);
		dc.SetTextColor(colBlack);
		dc.SetBkMode(TRANSPARENT);
	} else if (IsCheckBoxButton(hwnd)) {
		return m_winBgColorBrush;
	} else {
		SetMsgHandled(false);
		return 0;
	}

	return (HBRUSH)::GetStockObject(NULL_BRUSH);
}

bool CMainFrame::IsLoggedIn()
{
	if (SE_NOT_LOGGED_IN == m_simulatedError)
		return false;
	if (strempty(g_pref_user_name))
		return false;
	return true;
}

// returns true if we get valid ip address from both
// http query and dns query and they are not the same
// (it does happen)
bool CMainFrame::DnsVsHttpIpMismatch()
{
	if (!RealIpAddress(m_ipFromDns) || !m_ipFromHttp)
		return false;
	return (0 != m_ipFromDnsStr.Compare(m_ipFromHttp));
}

TCHAR *CMainFrame::LastUpdateTxt()
{
	if (m_minutesSinceLastUpdate < 0)
		return NULL;

	CString s(_T("Last update: "));
	TCHAR *timeTxt = FormatUpdateTime(m_minutesSinceLastUpdate);
	if (timeTxt) {
		s += timeTxt;
		free(timeTxt);
	}
	s += " ago.";
	const TCHAR* res = s;
	return tstrdup(res);
}

TCHAR* CMainFrame::IpAddress()
{
	IP4_ADDRESS a = m_ipFromDns;
	if (!RealIpAddress(a))
		return NULL;

	CString s;
	s.Format(_T("%u.%u.%u.%u"), (a >> 24) & 255, (a >> 16) & 255, (a >> 8) & 255, a & 255);
	const TCHAR *res = s;
	return tstrdup(res);
}

void CMainFrame::BuildStatusEditRtf(RtfTextInfo& ti)
{
	CString s;
	CUITextSizer sizer(*this);
	sizer.SetFont(m_statusEditFont);
	int minDx = 80;
	int dx;

	IP4_ADDRESS myNewIp = m_ipFromDns;

	ti.Init(m_editFontName, EDIT_CTRL_FONT_SIZE);

#if 0
	ti.StartBoldStyle();
	if (IsLoggedIn()) {
		s = "Logged in as ";
		s += g_pref_user_name;
		s += ". ";
		ti.AddTxt(s);
		ti.AddLink("Change account.", LINK_CHANGE_ACCOUNT);
		s += "Change account. ";
		sizer.SetText(s);
		dx = sizer.GetIdealSizeDx();
		if (dx > minDx)
			minDx = dx;
		ti.AddPara();
	}

	if (!strempty(g_pref_user_name)) {
		if (streq(UNS_OK, g_pref_user_networks_state)) {
			if (strempty(g_pref_hostname)) {
				s = "Sending updates for default network. ";
			} else {
				s = "Sending updates for network '";
				s += g_pref_hostname;
				s += "'. ";
			}
			ti.AddTxt(s);
			ti.AddLink("Change network.", LINK_CHANGE_NETWORK);
			s += "Change network. ";
			sizer.SetText(s);
			dx = sizer.GetIdealSizeDx();
			if (dx > minDx)
				minDx = dx;
			ti.AddPara();
		}
	}

	if (RealIpAddress(myNewIp)) {
		IP4_ADDRESS a = myNewIp;
		s.Format(_T("Your IP address is %u.%u.%u.%u"), (a >> 24) & 255, (a >> 16) & 255, (a >> 8) & 255, a & 255);
		ti.AddTxt(s);
		ti.AddPara();
		ti.AddTxt("You're using OpenDNS service.");
	}

	if (m_minutesSinceLastUpdate >= 0) {
		ti.AddPara();
		ti.AddTxt("Last update: ");
		TCHAR *timeTxt = FormatUpdateTime(m_minutesSinceLastUpdate);
		if (timeTxt) {
			ti.AddTxt(timeTxt);
			free(timeTxt);
		}
		ti.AddTxt(" ago. ");
		ti.AddLink("Update now", LINK_SEND_IP_UPDATE);
	}

	ti.EndStyle();
	ti.AddPara();
	ti.AddPara();
#endif

	// show error scenarios at the end, in bold red
	ti.StartBoldStyle();
	ti.StartFgCol(RtfTextInfo::ColRed);

	if (!IsLoggedIn()) {
		s = "You're not logged to your OpenDNS account. ";
		ti.AddTxt(s);
		ti.AddLink("Log in.", LINK_CHANGE_ACCOUNT);
		s += "Log in. ";
		sizer.SetText(s);
		dx = sizer.GetIdealSizeDx();
		if (dx > minDx)
			minDx = dx;
		ti.AddPara();
	}

	if ((IP_NOT_USING_OPENDNS == myNewIp) || (SE_NOT_USING_OPENDNS == m_simulatedError)) {
		ti.AddTxt("You're not using OpenDNS service. Learn how to ");
		ti.AddLink("setup OpenDNS.", LINK_SETUP_OPENDNS);
		ti.AddPara();
		ti.AddPara();
	} else if ((IP_DNS_RESOLVE_ERROR == myNewIp) || (SE_NO_INTERNET == m_simulatedError)) {
		ti.AddTxt("Looks like there's no internet connectivity.");
		ti.AddPara();
		ti.AddPara();
	}

	if (!strempty(g_pref_user_name)) {
		if (streq(UNS_NO_NETWORKS, g_pref_user_networks_state) || (SE_NO_NETWORKS_CONFIGURED == m_simulatedError)) {
#if 1
			ti.AddTxt("You don't have any networks configured. ");
			ti.AddLink("Configure a network", LINK_CONFIGURE_NETWORKS);
			ti.AddTxt(" in your OpenDNS account.");
#else
			ti.AddTxt("You don't have any networks configured. Configure a network in your OpenDNS account. ");
			ti.AddLink("Configure a network.", LINK_CONFIGURE_NETWORKS);
#endif
			ti.AddPara();
			ti.AddPara();
		} else if (streq(UNS_NO_DYNAMIC_IP_NETWORKS, g_pref_user_networks_state) || (SE_NO_DYNAMIC_IP_NETWORKS == m_simulatedError)) {
			ti.AddTxt("None of your networks is configured for dynamic IP. ");
			ti.AddLink("Configure a network", LINK_CONFIGURE_NETWORKS);
			ti.AddTxt(" for dynamic IP in your OpenDNS account and ");
			ti.AddLink("choose a network", LINK_SELECT_NETWORK);
			ti.AddPara();
			ti.AddPara();
		} else if (streq(UNS_NO_NETWORK_SELECTED, g_pref_user_networks_state) || (SE_NO_NETWORK_SELECTED == m_simulatedError)) {
			ti.AddTxt("You need to select one of your networks for IP updates. ");
			ti.AddLink("Select network.", LINK_SELECT_NETWORK);
			ti.AddPara();
			ti.AddPara();
		}
	}

	if ((IpUpdateNotYours == m_ipUpdateResult) || (SE_IP_NOT_YOURS == m_simulatedError)) {
		ti.AddTxt(_T("Your IP address is taken by another user."));
		ti.AddPara();
		ti.AddPara();
	}

	if ((IpUpdateBadAuth == m_ipUpdateResult) || (SE_BAD_AUTH == m_simulatedError)) {
		ti.AddTxt(_T("Your authorization token is invalid."));
		ti.AddPara();
		ti.AddPara();
	}

	bool ipMismatch = DnsVsHttpIpMismatch();
	if (ipMismatch) {
		ti.AddTxt(_T("Your OpenDNS filtering settings might not work due to DNS IP address ("));
		ti.AddTxt(m_ipFromDnsStr);
		ti.AddTxt(_T(") and HTTP IP address ("));
		ti.AddTxt(m_ipFromHttp);
		ti.AddTxt(_T(") mismatch."));
		ti.AddPara();
		ti.AddPara();
	}

	ti.EndCol();
	ti.EndStyle();

	if (g_showDebug) {
		if (UsingDevServers()) {
			ti.AddTxt("Using dev api servers ");
		} else {
			ti.AddTxt("Using production api servers ");
		}
		ti.AddLink("(toggle)", LINK_TOGGLE_DEV_PRODUCTION);
		ti.AddTxt(".");
		ti.AddPara();

		ti.AddLink("Send IP update", LINK_SEND_IP_UPDATE);
		ti.AddTxt(" ");
		ti.AddLink("Crash me", LINK_CRASH_ME);
		ti.AddPara();
	} else {
#if 0
	if (UsingDevServers()) {
		ti.AddTxt("Using dev api servers.");
		ti.AddPara();
	}
#endif
	}

	ti.End();
	m_minStatusEditDx = minDx;
}

void CMainFrame::UpdateStatusEdit(bool doLayout)
{
	GetLastIpUpdateTime();
	BuildStatusEditRtf(m_rti);
	const TCHAR *s = m_rti.text;
#ifdef UNICODE
	// Don't know why I have to do this, but SetWindowText() with unicode
	// doesn't work (rtf codes are not being recognized)
	const char *sUtf = WstrToUtf8(s);
	m_editErrorMsg.SetTextEx((LPCTSTR)sUtf, ST_DEFAULT, CP_UTF8);
#else
	m_editErrorMsg.SetWindowText(s);
#endif
	SetRtfLinks(&m_rti);

#if 0
	m_editErrorMsg.SetSelAll();
	PARAFORMAT2 paraFormat;
	memset(&paraFormat, 0, sizeof(paraFormat));
	paraFormat.cbSize = sizeof(PARAFORMAT2);
	paraFormat.dwMask = PFM_LINESPACING;
	paraFormat.bLineSpacingRule = 5;
	// spacing is dyLineSpacing/20 lines (i.e. 20 - single spacing, 40 - double spacing etc.)
	paraFormat.dyLineSpacing = 20;
	HWND hwndEdit = m_editErrorMsg;
	::SendMessage(hwndEdit, EM_SETPARAFORMAT, 0, (LPARAM)&paraFormat);
#endif

	m_editErrorMsg.SetSelNone();
	m_editErrorMsg.RequestResize();
	if (doLayout)
		PostMessage(WMAPP_DO_LAYOUT);
}

void CMainFrame::SetRtfLinks(RtfTextInfo *rti)
{
	RtfLinkInfo *link = rti->firstLink;
	while (link) {
		LONG start = link->start;
		LONG end = link->end;
		m_editErrorMsg.SetSel(start, end);
		CHARFORMAT2 cf;
		cf.cbSize = sizeof(cf);
		cf.dwMask = CFM_LINK;
		cf.dwEffects = CFE_LINK;
		m_editErrorMsg.SetCharFormat(cf, SCF_SELECTION);
		link = link->next;
	}
}

LRESULT CMainFrame::OnLinkStatusEdit(LPNMHDR pnmh)
{
	ENLINK *e = (ENLINK *)pnmh;
	if (e->msg != WM_LBUTTONDOWN)
		return 0;

	CHARRANGE chr = e->chrg;
	LONG start = chr.cpMin;
	LONG end = chr.cpMax;
	RtfLinkId linkId;
	BOOL found = m_rti.FindLinkFromRange(start, end, linkId);
	assert(found);
	if (!found)
		return 0;
	if (LINK_CHANGE_ACCOUNT == linkId) {
		ChangeAccount();
	} else if (LINK_CHANGE_NETWORK == linkId) {
		ChangeNetwork(0);
	} else if (LINK_SETUP_OPENDNS == linkId) {
		LaunchUrl(SETUP_OPENDNS_URL);
	} else if (LINK_SELECT_NETWORK == linkId) {
		ChangeNetwork(0);
	} else if (LINK_CONFIGURE_NETWORKS == linkId) {
		LaunchUrl(GetDashboardUrl());
	} else if (LINK_TOGGLE_DEV_PRODUCTION == linkId) {
		if (UsingDevServers())
			UseDevServers(false);
		else
			UseDevServers(true);
		UpdateStatusEdit();
	} else if (LINK_SEND_IP_UPDATE == linkId) {
		m_updaterThread->ForceSendIpUpdate();
		UpdateStatusEdit();
	} else if (LINK_CRASH_ME == linkId) {
		CrashMe();
	} else
		assert(0);
	SetFocus();
	return 0;
}

void CMainFrame::ChangeAccount()
{
	CSignInDlg dlg;
	INT_PTR nRet = dlg.DoModal();
	if (IDCANCEL == nRet) {
		// nothing has changed
		return;
	}
	StartDownloadNetworks(g_pref_token, SupressOneNetworkMsgFlag | SuppressNoDynamicIpMsgFlag);
	UpdateStatusEdit();
}

// Calculate @rectOut for a divider line with text @txt, starting at position @y,
// given current widht of client area is @clientDx.
// Returns minimum required size to display text
int CMainFrame::SizeDividerLineText(TCHAR *txt, int y, int clientDx, CRect& rectOut)
{
	CUITextSizer textSizer;
	textSizer.SetWindow(m_hWnd); // doesn't matter which window
	textSizer.SetFont(m_dividerTextFont);
	textSizer.SetText(txt);
	SIZE s = textSizer.GetIdealSize2();
	rectOut.top = y;
	int yMargins = DIVIDER_TEXT_TOP_MARGIN + DIVIDER_TEXT_BOTTOM_MARGIN;
	rectOut.bottom = y + s.cy + yMargins;

	rectOut.left = LEFT_MARGIN;
	rectOut.right = clientDx - RIGHT_MARGIN;

	int xMargins = DIVIDER_TEXT_LEFT_MARGIN + DIVIDER_TEXT_LEFT_MARGIN + LEFT_MARGIN + RIGHT_MARGIN;
	int minDx = s.cx + xMargins;
	return minDx;
}

// I want all three bugttons on the left be the same size, so this will
// calculate common dx/dy of the buttons. Dy should be always the same,
// dx is the biggest of them
void CMainFrame::SizeButtons(int& dxOut, int& dyOut)
{
	int dxMax, dy;
	int dxTmp, dyTmp;
	TCHAR *txt;

	CUITextSizer textSizer;
	textSizer.SetWindow(m_hWnd); // doesn't matter which window

	txt = MyGetWindowText(m_buttonChangeAccount);
	textSizer.GetIdealSize(txt, m_buttonsFont, dxMax, dy);
	free(txt);

	txt = MyGetWindowText(m_buttonChangeConfigureNetwork);
	textSizer.GetIdealSize(txt, m_buttonsFont, dxTmp, dyTmp);
	free(txt);
	assert(dy == dyTmp);
	if (dxTmp > dxMax)
		dxMax = dxTmp;

	txt = MyGetWindowText(m_buttonUpdate);
	textSizer.GetIdealSize(txt, m_buttonsFont, dxTmp, dyTmp);
	free(txt);
	assert(dy == dyTmp);
	if (dxTmp > dxMax)
		dxMax = dxTmp;

	dxOut = dxMax + 32;
	dyOut = dy + 12;
}

void CMainFrame::DoLayout()
{
	int x, y;
	int btnDx, btnDy;
	int minDx = m_minStatusEditDx + LEFT_MARGIN + RIGHT_MARGIN;
	int dxLine;
	SIZE s;
	TCHAR *txt;

	// place exit button on the lower right corner
	BOOL ok;
	RECT clientRect;
	ok = GetClientRect(&clientRect);
	if (!ok) return;
	int clientDx = RectDx(clientRect);
	int clientDy = RectDy(clientRect);

	SizeButtons(btnDx, btnDy);

	// position "Send IP updates" check-box in the bottom right corner
	CUICheckBoxButtonSizer sendIpUpdatsSizer(m_buttonSendIpUpdates);
	static const int BTN_SEND_UPDATES_RIGHT_MARGIN = 8;
	static const int BTN_SEND_UPDATES_BOTTOM_MARGIN = 4;
	RECT pos;
	CalcFixedPositionBottomRight(clientDx, clientDy, BTN_SEND_UPDATES_RIGHT_MARGIN, BTN_SEND_UPDATES_BOTTOM_MARGIN, &sendIpUpdatsSizer, pos);
	m_buttonSendIpUpdates.MoveWindow(&pos);
	int buttonDy = RectDy(pos);

	CUILinkSizer linkSizer;
	CUITextSizer textSizer;
	textSizer.SetWindow(m_hWnd); // doesn't matter which window
	CUIButtonSizer buttonSizer;

	// position "About" link in the bottom left corner
	x = LEFT_MARGIN;
	linkSizer.SetWindow(m_linkAbout);
	linkSizer.SetFont(m_defaultGuiFont);
	s = linkSizer.GetIdealSize2();
	static const int LINK_ABOUT_BOTTOM_MARGIN = 5;
	m_linkAbout.MoveWindow(x, clientDy - s.cy - LINK_ABOUT_BOTTOM_MARGIN, s.cx, s.cy);

#if 0
	// position title in the middle of top bar
	RECT topBarRect = {0, 0, clientDx, TOP_BAR_DY};
	m_topBarRect = topBarRect;
	textSizer.SetFont(m_topBarFont);
	textSizer.SetText(TOP_BAR_TXT);
	s = textSizer.GetIdealSize2();
	//y = (TOP_BAR_DY - s.cy) / 2;
	m_topBarY = 4;
	m_topBarX = (clientDx - s.cx) / 2;
	dxLine = s.cx + 2 * LEFT_MARGIN;
	if (dxLine > minDx)
		minDx = dxLine;
#endif

	// Position "OpenDNS account" divider line
	y = 4;
	dxLine = SizeDividerLineText(TXT_DIV_ACCOUNT, y, clientDx, m_txtAccountRect);
	if (dxLine > minDx)
		minDx = dxLine;
	y += m_txtAccountRect.Height();

	// position account name text and "Change account" button
	y += DIVIDER_Y_SPACING;
	dxLine = LEFT_MARGIN + RIGHT_MARGIN;
	//buttonSizer.GetIdealSize(&m_buttonChangeAccount, btnDx, btnDy);
	x = clientDx - RIGHT_MARGIN - btnDx;
	m_buttonChangeAccount.MoveWindow(x, y, btnDx, btnDy);
	dxLine += btnDx;
	y += btnDy;
	if (dxLine > minDx)
		minDx = dxLine;

	// position "Network to update" divider line
	y += DIVIDER_Y_SPACING;
	dxLine = SizeDividerLineText(TXT_DIV_NETWORK_TO_UPDATE, y, clientDx, m_txtNetworkRect);
	if (dxLine > minDx)
		minDx = dxLine;
	y += m_txtNetworkRect.Height();

	// position network name and "Change network"/"Configure network" button
	y += DIVIDER_Y_SPACING;
	dxLine = LEFT_MARGIN + RIGHT_MARGIN;
	//buttonSizer.GetIdealSize(&m_buttonChangeConfigureNetwork, btnDx, btnDy);
	x = clientDx - RIGHT_MARGIN - btnDx;
	m_buttonChangeConfigureNetwork.MoveWindow(x, y, btnDx, btnDy);
	dxLine += btnDx;
	if (dxLine > minDx)
		minDx = dxLine;
	y += btnDy;

	//m_buttonChangeConfigureNetwork.ShowWindow(SW_HIDE);

	// position "IP address" divider line
	y += DIVIDER_Y_SPACING;
	dxLine = SizeDividerLineText(TXT_DIV_IP_ADDRESS, y, clientDx, m_txtIpAddressRect);
	if (dxLine > minDx)
		minDx = dxLine;
	y += m_txtIpAddressRect.Height();

	// position IP address text
	y += DIVIDER_Y_SPACING;
	txt = IpAddress();
	if (txt) {
		textSizer.SetText(txt);
		textSizer.SetFont(m_dividerTextFont);
		s = textSizer.GetIdealSize2();
		y += s.cy;
		free(txt);
	}

	// position "Status" divider line
	y += DIVIDER_Y_SPACING;
	dxLine = SizeDividerLineText(TXT_DIV_STATUS, y, clientDx, m_txtStatusRect);
	if (dxLine > minDx)
		minDx = dxLine;
	y += m_txtStatusRect.Height();

	// position "Using OpenDNS: " + "Yes"/"No" line
	y += DIVIDER_Y_SPACING;
	textSizer.SetText(_T("Using OpenDNS: Yes"));
	textSizer.SetFont(m_defaultGuiFont);
	s = textSizer.GetIdealSize2();
	y += s.cy;

	// position "Update" divider line
	y += DIVIDER_Y_SPACING;
	dxLine = SizeDividerLineText(TXT_DIV_UPDATE, y, clientDx, m_txtUpdateRect);
	if (dxLine > minDx)
		minDx = dxLine;
	y += m_txtUpdateRect.Height();

	// position "Last updated: " text and "Update now" button
	y += DIVIDER_Y_SPACING;
	dxLine = LEFT_MARGIN + RIGHT_MARGIN;
	//buttonSizer.GetIdealSize(&m_buttonUpdate, btnDx, btnDy);
	x = clientDx - RIGHT_MARGIN - btnDx;
	m_buttonUpdate.MoveWindow(x, y, btnDx, btnDy);
	y += btnDy;
	dxLine += btnDx;
	txt = LastUpdateTxt();
	if (txt) {
		textSizer.SetFont(m_defaultGuiFont);
		s = textSizer.GetIdealSize2();
		dxLine += s.cx + 16;
		free(txt);
	}
	if (dxLine > minDx)
		minDx = dxLine;

	// position status edit box
	y += DIVIDER_Y_SPACING * 2;
	m_editErrorMsg.MoveWindow(LEFT_MARGIN, y, m_editErrorMsgDx, m_editErrorMsgRequestedDy);
	y += (m_editErrorMsgRequestedDy + Y_SPACING);

	dxLine = x + s.cx + RIGHT_MARGIN;
	if (dxLine > minDx)
		minDx = dxLine;
	int minDy = y + buttonDy + 8;
	
	// resize the window if the current size is smaller than
	// what's needed to display content
	int newClientDx = clientDx;
	if (minDx > clientDx)
		newClientDx = minDx;
	int newClientDy = clientDy;
	if (minDy > newClientDy)
		newClientDy = minDy;

	if ((newClientDx != clientDx) || (newClientDy != clientDy))
	{
		ResizeClient(newClientDx, newClientDy);
		RECT r = {0, 0, newClientDx, newClientDy};
		DWORD winStyle = GetStyle();
		BOOL hasMenu = FALSE;
		::AdjustWindowRect(&r, winStyle, hasMenu);
		m_minWinDx = RectDx(r);
		Invalidate();
	}

	RECT r = {0, 0, 0, minDy};
	DWORD winStyle = GetStyle();
	BOOL hasMenu = FALSE;
	::AdjustWindowRect(&r, winStyle, hasMenu);
	m_minWinDy = RectDy(r);
}

LRESULT CMainFrame::OnLayout(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	DoLayout();
	Invalidate();
	return 0;
}

void CMainFrame::OnIpCheckResult(IP4_ADDRESS myIp)
{
	if (m_ipFromDns == myIp) {
		// since this is called every minute, we use this
		// to check if we should update display of "last updated" time
		if (GetLastIpUpdateTime())
			PostMessage(WMAPP_UPDATE_STATUS);
		return;
	}
	m_ipFromDns = myIp;
	IP4_ADDRESS a = m_ipFromDns;
	m_ipFromDnsStr.Format(_T("%u.%u.%u.%u"), (a >> 24) & 255, (a >> 16) & 255, (a >> 8) & 255, a & 255);

	// This is called on dns thread so trigger update of
	// the ui on ui thread
	PostMessage(WMAPP_UPDATE_STATUS);
	// on ip change force sending ip update to update
	// possible error state
	m_updaterThread->ForceSendIpUpdate();
}

LRESULT CMainFrame::OnLinkAbout(LPNMHDR /*pnmh*/)
{
	/* Show debug info when clicking on a link while pressing left alt key */
	if (IsLeftAltPressed()) {
		if (g_showDebug)
			g_showDebug = false;
		else
			g_showDebug = true;
		UpdateStatusEdit();
		return 0;
	}
	LaunchUrl(ABOUT_URL);
	return 0;
}

void CMainFrame::OnIpUpdateResult(char *ipUpdateRes)
{
	IpUpdateResult ipUpdateResult =	IpUpdateResultFromString(ipUpdateRes);
	LogIpUpdate(ipUpdateRes);
	free(m_ipFromHttp);
	m_ipFromHttp = NULL;

	if (IpUpdateNotAvailable == ipUpdateResult)
		return;

	if ((IpUpdateOk == ipUpdateResult) || (IpUpdateNotYours == ipUpdateResult)) {
		const char *ip = StrFindChar(ipUpdateRes, ' ');
		if (ip)
			m_ipFromHttp = StrToTStr(ip+1);
	}

	if (ipUpdateResult == m_ipUpdateResult)
		return;

	m_ipUpdateResult = ipUpdateResult;
	if (ipUpdateResult != IpUpdateOk)
		SwitchToVisibleState();
	PostMessage(WMAPP_UPDATE_STATUS);
}

LRESULT CMainFrame::OnNewVersion(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
{
	TCHAR *url = (TCHAR*)wParam;
	int ret = ::MessageBox(NULL, _T("New version of OpenDNS Updater client is available. Download new version?"), MAIN_FRAME_TITLE, MB_YESNO);
	if (ret == IDOK) {
		LaunchUrl(url);
	}
	free(url);
	return 0;
}

void CMainFrame::OnNewVersionAvailable(char *updateUrl)
{
	TCHAR *url = StrToTStr(updateUrl);
	if (!url)
		return;
	PostMessage(WMAPP_NEW_VERSION, (WPARAM)url);
}

void CMainFrame::OnSize(UINT nType, CSize /*size*/)
{
	if (SIZE_MINIMIZED == nType)
		return;
	RECT clientRect;
	BOOL ok = GetClientRect(&clientRect);
	if (!ok) return;
	int clientDx = RectDx(clientRect);
	m_editErrorMsgDx = clientDx - LEFT_MARGIN - RIGHT_MARGIN;
	m_editErrorMsg.RequestResize();
	PostMessage(WMAPP_DO_LAYOUT);
}

void CMainFrame::StartDownloadNetworks(char *token, int supressFlags)
{
	CString params = ApiParamsNetworksGet(token);
	const char *paramsTxt = TStrToStr(params);
	// TODO: could do it async but probably not worth it
	//HttpPostAsync(API_HOST, API_URL, paramsTxt, API_IS_HTTPS, m_hWnd, WM_HTTP_DOWNLOAD_NETOWRKS);
	const char *apiHost = GetApiHost();
	bool apiHostIsHttps = IsApiHostHttps();
	HttpResult *httpRes = HttpPost(apiHost, API_URL, paramsTxt, apiHostIsHttps);		
	free((void*)paramsTxt);
	OnDownloadNetworks(0, (WPARAM)httpRes, (LPARAM)supressFlags);
}

static BOOL IsBitSet(int flags, int bit)
{
	if (flags & bit)
		return true;
	return false;
}

LRESULT CMainFrame::OnDownloadNetworks(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
{
	NetworkInfo *ni = NULL;
	char *jsonTxt = NULL;
	JsonEl *json = NULL;
	NetworkInfo *selectedNetwork = NULL;
	int supressFlags = (int)lParam;
	BOOL supressOneNetworkMsg = IsBitSet(supressFlags,SupressOneNetworkMsgFlag);
	BOOL suppressNoDynamicIpMsg = IsBitSet(supressFlags, SuppressNoDynamicIpMsgFlag);

	HttpResult *ctx = (HttpResult*)wParam;
	assert(ctx);
	if (!ctx || !ctx->IsValid())
		goto Error;

	DWORD dataSize;
	jsonTxt = (char*)ctx->data.getData(&dataSize);
	json = ParseJsonToDoc(jsonTxt);
	if (!json)
		goto Error;
	WebApiStatus status = GetApiStatus(json);

	if (WebApiStatusSuccess != status) {
		if (WebApiStatusFailure == status) {
			long err;
			bool ok = GetApiError(json, &err);
			if (!ok)
				goto Error;
			if (ERR_NETWORK_DOESNT_EXIST == err)
				goto NoNetworks;
			if (ERR_BAD_TOKEN == err)
				goto BadToken;
			goto Error;
		}
	}
	ni = ParseNetworksGetJson(json);
	size_t networksCount = ListLengthGeneric(ni);
	assert(0 != networksCount);
	if (0 == networksCount)
		goto Error;

	size_t dynamicNetworksCount = DynamicNetworksCount(ni);
	if (0 == dynamicNetworksCount)
		goto NoDynamicNetworks;

	NetworkInfo *dynamicNetwork = FindFirstDynamic(ni);
	assert(dynamicNetwork);
	if (!dynamicNetwork)
		goto Error;

	if (1 == dynamicNetworksCount) {
		if (!supressOneNetworkMsg)
			MessageBox(_T("Only one network configured for dynamic IP updates. Using that network."), MAIN_FRAME_TITLE);
		PrefSetHostname(dynamicNetwork->label);
		SetPrefVal(&g_pref_user_networks_state, UNS_OK);
		goto Exit;
	}

	selectedNetwork = SelectNetwork(ni);
	if (!selectedNetwork) {
		// if cancelled selection but has a network already
		// selected, keep the old network
		if (!streq(UNS_OK, g_pref_user_networks_state))
			goto NoNetworkSelected;
		if (strempty(g_pref_hostname))
			goto NoNetworkSelected;
		// rare but possible case: currently selected network
		// is not on the list of downloaded network (e.g. user
		// changed networks on website)
		if (!HasDynamicNetworkWithLabel(ni, g_pref_hostname))
			goto NoNetworkSelected;
		goto Exit;
	}

	PrefSetHostname(selectedNetwork->label);
	SetPrefVal(&g_pref_user_networks_state, UNS_OK);

Exit:
	NetworkInfoFreeList(ni);
	JsonElFree(json);
	delete ctx;
	// prefs changed so save them
	PreferencesSave();
	m_updaterThread->ForceSendIpUpdate();
	// hack: sometimes cursor gets hidden so set
	// it to standard cursor to ensure it's visible
	HCURSOR curs = LoadCursor(NULL, IDC_ARROW);
	SetCursor(curs);
	return 0;

NoNetworkSelected:
	//MessageBox(_T("You need to select a network for Dynamic IP Update."), MAIN_FRAME_TITLE);
	SetPrefVal(&g_pref_user_networks_state, UNS_NO_NETWORK_SELECTED);
	SetPrefVal(&g_pref_hostname, NULL);
	goto Exit;

NoDynamicNetworks:
	if (!suppressNoDynamicIpMsg)
		MessageBox(_T("You don't have any networks enabled for Dynamic IP Update. Enable Dynamic IP Updates in your OpenDNS account"), MAIN_FRAME_TITLE); 
	SetPrefVal(&g_pref_user_networks_state, UNS_NO_DYNAMIC_IP_NETWORKS);
	SetPrefVal(&g_pref_hostname, NULL);
	goto Exit;

NoNetworks:
	//MessageBox(_T("You don't have any networks configured. You need to configure a network in your OpenDNS account"), MAIN_FRAME_TITLE);
	SetPrefVal(&g_pref_user_networks_state, UNS_NO_NETWORKS);
	SetPrefVal(&g_pref_hostname, NULL);
	goto Exit;

BadToken:
	// TODO: this should never happen, not sure what the user can do
	// should we just nuke username/pwd/token in preferences?
	MessageBox(_T("Not a valid token"), MAIN_FRAME_TITLE);
	goto Exit;

Error:
	MessageBox(_T("There was an error downloading networks"), MAIN_FRAME_TITLE);
	goto Exit;
}

NetworkInfo *CMainFrame::SelectNetwork(NetworkInfo *ni)
{
	CSelectNetworkDlg dlg(ni);
	INT_PTR nRet = dlg.DoModal();
	if (IDOK != nRet)
		return NULL;
	NetworkInfo *selected = dlg.m_selectedNetwork;
	assert(selected);
	return selected;
}

int CMainFrame::OnCreate(LPCREATESTRUCT /* lpCreateStruct */)
{
	SetMenu(NULL);

	// remove WS_CLIPCHILDREN style to make transparent
	// static controls work
	ModifyStyle(WS_CLIPCHILDREN, 0);

	SetWindowText(MAIN_FRAME_TITLE);

#if 0
	m_defaultFont = AtlGetDefaultGuiFont();
#else
	HDC dc = GetWindowDC();
	CLogFont logFontDefault(AtlGetDefaultGuiFont());
	//_tcscpy_s(logFontDefault.lfFaceName, dimof(logFontDefault.lfFaceName), "Tahoma");
	logFontDefault.SetBold();
	logFontDefault.SetHeight(DEFAULT_FONT_SIZE, dc);
	m_defaultFont.Attach(logFontDefault.CreateFontIndirect());

	CLogFont logFontEditFont(AtlGetDefaultGuiFont());
	logFontEditFont.SetBold();
	logFontEditFont.SetHeight(EDIT_CTRL_FONT_SIZE, dc);
	m_statusEditFont.Attach(logFontEditFont.CreateFontIndirect());
	ReleaseDC(dc);
#endif

	CLogFont lf(m_defaultFont);
	m_editFontName = tstrdup(lf.lfFaceName);
	//m_editFontName = tstrdup(_T("Tahoma"));
	//m_editFontName = tstrdup(_T("Times New Roman"));
	//m_editFontName = tstrdup(_T("Arial"));
	//m_editFontName = tstrdup(_T("Trebuchet MS"));
	//m_editFontName = tstrdup(_T("Fixedsys"));

	m_topBarFont = m_defaultFont;

	// values inside r don't matter - things get positioned in DoLayout()
	RECT r = {10, 10, 20, 20};

	m_defaultGuiFont = AtlGetDefaultGuiFont();

	m_buttonsFont = AtlGetDefaultGuiFont();

	CLogFont logFontDivider(AtlGetDefaultGuiFont());
	//_tcscpy_s(logFontDefault.lfFaceName, dimof(logFontDefault.lfFaceName), "Tahoma");
	logFontDivider.SetBold();
	m_dividerTextFont.Attach(logFontDivider.CreateFontIndirect());

	// TODO: tried using LWS_TRANSPARENT and/or WS_EX_TRANSPARENT but they don't
	// seem to work as expected (i.e. create transparent background for the link
	// control)
	m_linkAbout.Create(m_hWnd, r, _T("<a>About this program</a>"), WS_CHILD | WS_VISIBLE);
	m_linkAbout.SetFont(m_defaultGuiFont);
	m_linkAbout.SetDlgCtrlID(IDC_LINK_ABOUT);

	m_buttonSendIpUpdates.Create(m_hWnd, r, _T("Send background IP updates"), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX);
	m_buttonSendIpUpdates.SetFont(m_buttonsFont);
	//m_buttonSendIpUpdates.SetFont(m_topBarFont);
	m_buttonSendIpUpdates.SetDlgCtrlID(IDC_CHECK_SEND_UPDATES);
	BOOL sendingUpdates = GetPrefValBool(g_pref_send_updates);
	m_buttonSendIpUpdates.SetCheck(sendingUpdates);

	m_buttonChangeAccount.Create(m_hWnd, r, _T("Change account"),  WS_CHILD | WS_VISIBLE);
	m_buttonChangeAccount.SetFont(m_buttonsFont);
	m_buttonChangeAccount.SetDlgCtrlID(IDC_BUTTON_CHANGE_ACCOUNT);
	//m_buttonChangeAccount.ShowWindow(SW_HIDE);

	m_buttonChangeConfigureNetwork.Create(m_hWnd, r, _T("Change network"),  WS_CHILD | WS_VISIBLE);
	m_buttonChangeConfigureNetwork.SetFont(m_buttonsFont);
	//m_buttonChangeConfigureNetwork.SetFont(m_topBarFont);
	m_buttonChangeConfigureNetwork.SetDlgCtrlID(IDC_BUTTON_CHANGE_NETWORK);
	//m_buttonChangeAccount.ShowWindow(SW_HIDE);

	m_buttonUpdate.Create(m_hWnd, r, _T("Update now"),  WS_CHILD | WS_VISIBLE);
	// TODO: long text here will not size the window
	//m_buttonUpdate.Create(m_hWnd, r, _T("Update now because this is a long text"),  WS_CHILD | WS_VISIBLE);
	m_buttonUpdate.SetFont(m_defaultGuiFont);
	m_buttonUpdate.SetDlgCtrlID(IDC_BUTTON_SEND_IP_UPDATE);
	//m_buttonChangeAccount.ShowWindow(SW_HIDE);

	m_editErrorMsg.Create(m_hWnd, r, _T(""), WS_CHILD | WS_VISIBLE | ES_MULTILINE);
	m_editErrorMsg.SetReadOnly(TRUE);
	m_editErrorMsg.SetEventMask(ENM_REQUESTRESIZE | ENM_LINK | ENM_SELCHANGE);
	//m_editErrorMsg.SetEventMask(ENM_REQUESTRESIZE | ENM_LINK);
	m_editErrorMsg.SetBackgroundColor(colWinBg);
	m_editErrorMsg.SetDlgCtrlID(IDC_EDIT_STATUS);

	if (strempty(g_pref_user_name) ||
		strempty(g_pref_token))
	{
		CSignInDlg dlg;
		dlg.DoModal();
	}

	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->AddMessageFilter(this);
	pLoop->AddIdleHandler(this);

	m_updaterThread = new UpdaterThread(this);
	m_updaterThread->ForceSendIpUpdate();
	m_updaterThread->ForceSoftwareUpdateCheck();
	return 0;
}

LRESULT CMainFrame::OnUpdateStatus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	UpdateStatusEdit();
	return 0;
}

void CMainFrame::SwitchToVisibleState()
{
	ShowWindow(SW_SHOW);
	m_uiState = UI_STATE_VISIBLE;
	UpdateStatusEdit();
}

void CMainFrame::SwitchToHiddenState()
{
	ShowWindow(SW_HIDE);
	m_uiState = UI_STATE_HIDDEN;
}

LRESULT CMainFrame::OnErrorNotif(UINT /*uMsg*/, WPARAM specialCmd, LPARAM /*lParam*/)
{
	slog("CMainFrame::OnErrorNotif(): "); 
	if (SPECIAL_CMD_SHOW == specialCmd) {
		SwitchToVisibleState();
		slog("SPECIAL_CMD_SHOW\n");
	}
	else {
		char buf[32];
		slog("unknown specialCmd=");
		itoa((int)specialCmd, buf, 10);
		slog(buf);
		slog("\n");
		assert(0);
	}
	return 0;
}

#endif
