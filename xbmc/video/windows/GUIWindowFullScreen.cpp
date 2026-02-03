/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "GUIWindowFullScreen.h"

#include "FileItem.h"
#include "cores/IPlayer.h"
#include "GUIInfoManager.h"
#include "GUIWindowFullScreenDefines.h"
#include "ServiceBroker.h"
#include "application/Application.h"
#include "application/ApplicationComponents.h"
#include "application/ApplicationPlayer.h"
#include "cores/IPlayer.h"
#include "SeekHandler.h"
#include "guilib/GUIComponent.h"
#include "guilib/GUIWindowManager.h"
#include "guilib/LocalizeStrings.h"
#include "input/actions/Action.h"
#include "input/actions/ActionIDs.h"
#include "input/mouse/MouseEvent.h"
#include "settings/DisplaySettings.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "utils/StringUtils.h"
#include "video/ViewModeSettings.h"
#include "video/dialogs/GUIDialogFullScreenInfo.h"
#include "video/dialogs/GUIDialogSubtitleSettings.h"
#include "cores/VideoPlayer/DVDInputStreams/DVDInputStreamBluray.h"
#include "windowing/WinSystem.h"
#include "dialogs/GUIDialogYesNo.h"
#include "utils/log.h"

#include <algorithm>
#include <chrono>
#include <stdio.h>
#if defined(TARGET_DARWIN)
#include "platform/posix/PosixResourceCounter.h"
#endif

using namespace KODI;
using namespace GUILIB;
using namespace MESSAGING;

#if defined(TARGET_DARWIN)
static CPosixResourceCounter m_resourceCounter;
#endif

CGUIWindowFullScreen::CGUIWindowFullScreen()
  : CGUIWindow(WINDOW_FULLSCREEN_VIDEO, "VideoFullScreen.xml"), m_dwShowViewModeTimeout{}
{
  m_viewModeChanged = true;
  m_bShowCurrentTime = false;
  m_loadType = KEEP_IN_MEMORY;
  m_controlStats = new GUICONTROLSTATS;
  
  m_isSeeking = false;
  m_ismenuon = true;
}

CGUIWindowFullScreen::~CGUIWindowFullScreen(void)
{
  delete m_controlStats;
}

bool CGUIWindowFullScreen::OnAction(const CAction &action)
{
  auto& components = CServiceBroker::GetAppComponents();
  const auto appPlayer = components.GetComponent<CApplicationPlayer>();
  bool hasMenu = (appPlayer->GetSupportedMenuType() != MenuType::NONE);
  
  switch (action.GetID())
  {
  case ACTION_MOVE_LEFT:
  case ACTION_MOVE_RIGHT:
    {
      // ===== 核心修改：按要求重构m_ismenuon判断逻辑 =====
      if (m_ismenuon)
      {
        // 状态变量为true时，检测全局isinmenu是否为false，是则置为false
        if (!appPlayer->IsInMenu())
        {
          m_ismenuon = false;
          CLog::Log(LOGDEBUG, "CGUIWindowFullScreen::OnAction - IsInMenu is false, set m_ismenuon to false");
        }
      }
      // 状态变量为false时，执行快速seek逻辑
      if (!m_ismenuon)
      {
        InitiateSeek(action.GetID() == ACTION_MOVE_RIGHT);
        return true;
      }
      // ===================================================
      break;
    }

  case ACTION_NAV_BACK:
    {
      auto pDialog = CServiceBroker::GetGUI()->GetWindowManager().GetWindow<CGUIDialogYesNo>(WINDOW_DIALOG_YES_NO);
      if (pDialog)
      {
        pDialog->SetHeading(CVariant{"CoreELEC"});
        pDialog->SetLine(0, CVariant{"Do you want to exit the current video?"});
        pDialog->SetLine(1, CVariant{""});
        pDialog->SetLine(2, CVariant{""});
        pDialog->Open();
        if (pDialog->IsConfirmed())
          g_application.StopPlaying();
        return true;
      }
      break;
    }
  case ACTION_SHOW_OSD:
    {
      if (hasMenu)
      {
        appPlayer->OnAction(CAction(ACTION_SHOW_VIDEOMENU));
      }
      else
      {
        ToggleOSD();
      }
      return true;
    }

  case ACTION_TRIGGER_OSD:
    TriggerOSD();
    return true;

  case ACTION_MOUSE_MOVE:
    if (action.GetAmount(2) || action.GetAmount(3))
    {
      if (!appPlayer->IsInMenu())
      {
        TriggerOSD();
        return true;
      }
    }
    break;

  case ACTION_MOUSE_LEFT_CLICK:
    if (!appPlayer->IsInMenu())
    {
      TriggerOSD();
      return true;
    }
    break;

  case ACTION_SHOW_GUI:
    CServiceBroker::GetGUI()->GetWindowManager().PreviousWindow();
    return true;

  case ACTION_SHOW_OSD_TIME:
    m_bShowCurrentTime = !m_bShowCurrentTime;
    CServiceBroker::GetGUI()->GetInfoManager().GetInfoProviders().GetPlayerInfoProvider().SetShowTime(m_bShowCurrentTime);
    return true;

  case ACTION_SHOW_INFO:
    {
      CGUIDialog* pProcessInfo = CServiceBroker::GetGUI()->GetWindowManager().GetDialog(WINDOW_DIALOG_PLAYER_PROCESS_INFO);
      pProcessInfo->Open();
      return true;
    }

  case ACTION_ASPECT_RATIO:
    {
      if (m_dwShowViewModeTimeout.time_since_epoch().count() != 0)
      {
        CVideoSettings vs = appPlayer->GetVideoSettings();
        vs.m_ViewMode = CViewModeSettings::GetNextQuickCycleViewMode(vs.m_ViewMode);
        appPlayer->SetRenderViewMode(vs.m_ViewMode, vs.m_CustomZoomAmount, vs.m_CustomPixelRatio,
                                     vs.m_CustomVerticalShift, vs.m_CustomNonLinStretch);
      }
      else
        m_viewModeChanged = true;
      m_dwShowViewModeTimeout = std::chrono::steady_clock::now();
    }
    return true;

  case ACTION_SHOW_PLAYLIST:
    {
      CFileItem item(g_application.CurrentFileItem());
      if (item.HasPVRChannelInfoTag())
        CServiceBroker::GetGUI()->GetWindowManager().ActivateWindow(WINDOW_DIALOG_PVR_OSD_CHANNELS);
      else if (item.HasVideoInfoTag())
        CServiceBroker::GetGUI()->GetWindowManager().ActivateWindow(WINDOW_VIDEO_PLAYLIST);
      else if (item.HasMusicInfoTag())
        CServiceBroker::GetGUI()->GetWindowManager().ActivateWindow(WINDOW_MUSIC_PLAYLIST);
    }
    return true;

  case ACTION_BROWSE_SUBTITLE:
    {
      std::string path = CGUIDialogSubtitleSettings::BrowseForSubtitle();
      if (!path.empty())
        appPlayer->AddSubtitle(path);
      return true;
    }
  default:
      break;
  }

  return CGUIWindow::OnAction(action);
}

void CGUIWindowFullScreen::InitiateSeek(bool forward)
{
  auto& components = CServiceBroker::GetAppComponents();
  const auto appPlayer = components.GetComponent<CApplicationPlayer>();
  auto now = std::chrono::steady_clock::now();

  // 第三个参数 false 表示“相对偏移”，确保它是在当前进度上累加
  appPlayer->GetSeekHandler().Seek(forward, false, false);
  
  m_isSeeking = true;
  m_lastSeekActionTime = now;

  // 确保 SeekBar 对话框打开
  CGUIDialog* pSeekBar = CServiceBroker::GetGUI()->GetWindowManager().GetDialog(WINDOW_DIALOG_SEEK_BAR);
  if (pSeekBar && !pSeekBar->IsDialogRunning())
  {
    pSeekBar->Open();
  }
}

void CGUIWindowFullScreen::ExecuteSeek()
{
  auto& components = CServiceBroker::GetAppComponents();
  const auto appPlayer = components.GetComponent<CApplicationPlayer>();

  // 执行真正的跳转
  appPlayer->GetSeekHandler().Configure();
  
  m_isSeeking = false;
  m_ismenuon = true;
}

void CGUIWindowFullScreen::FrameMove()
{
  const auto& components = CServiceBroker::GetAppComponents();
  const auto appPlayer = components.GetComponent<CApplicationPlayer>();
  if (!appPlayer->HasPlayer())
    return;

  // --- 处理左右键 Seek 延迟逻辑 ---
  if (m_isSeeking)
  {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastSeekActionTime).count();
    
    if (elapsed > 1000) // 超过1s未再次按键，执行Seek
    {
      ExecuteSeek();
    }
  }

  // --- 原有 ViewMode 信息逻辑 ---
  auto now = std::chrono::steady_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - m_dwShowViewModeTimeout);

  if (m_dwShowViewModeTimeout.time_since_epoch().count() != 0 && duration.count() > 2500)
  {
    m_dwShowViewModeTimeout = {};
    m_viewModeChanged = true;
  }

  if (m_dwShowViewModeTimeout.time_since_epoch().count() != 0)
  {
    RESOLUTION_INFO res = CServiceBroker::GetWinSystem()->GetGfxContext().GetResInfo();
    {
      const std::string& strTitle = g_localizeStrings.Get(629);
      const auto& vs = appPlayer->GetVideoSettings();
      int sId = CViewModeSettings::GetViewModeStringIndex(vs.m_ViewMode);
      const std::string& strMode = g_localizeStrings.Get(sId);
      std::string strInfo = StringUtils::Format("{} : {}", strTitle, strMode);
      CGUIMessage msg(GUI_MSG_LABEL_SET, GetID(), LABEL_ROW1);
      msg.SetLabel(strInfo);
      OnMessage(msg);
    }
    VideoStreamInfo info;
    appPlayer->GetVideoStreamInfo(CURRENT_STREAM, info);
    {
      float xscale = (float)res.iScreenWidth  / (float)res.iWidth;
      float yscale = (float)res.iScreenHeight / (float)res.iHeight;

      std::string strSizing = StringUtils::Format(
          g_localizeStrings.Get(245), (int)info.SrcRect.Width(), (int)info.SrcRect.Height(),
          (int)(info.DestRect.Width() * xscale), (int)(info.DestRect.Height() * yscale),
          CDisplaySettings::GetInstance().GetZoomAmount(),
          info.videoAspectRatio * CDisplaySettings::GetInstance().GetPixelRatio(),
          CDisplaySettings::GetInstance().GetPixelRatio(),
          CDisplaySettings::GetInstance().GetVerticalShift());
      CGUIMessage msg(GUI_MSG_LABEL_SET, GetID(), LABEL_ROW2);
      msg.SetLabel(strSizing);
      OnMessage(msg);
    }
    {
      std::string strStatus;
      if (CServiceBroker::GetWinSystem()->IsFullScreen())
        strStatus = StringUtils::Format("{} {}x{}@{:.2f}Hz - {}", g_localizeStrings.Get(13287),
                                        res.iScreenWidth, res.iScreenHeight, res.fRefreshRate,
                                        g_localizeStrings.Get(244));
      else
        strStatus =
            StringUtils::Format("{} {}x{} - {}", g_localizeStrings.Get(13287), res.iScreenWidth,
                                res.iScreenHeight, g_localizeStrings.Get(242));

      CGUIMessage msg(GUI_MSG_LABEL_SET, GetID(), LABEL_ROW3);
      msg.SetLabel(strStatus);
      OnMessage(msg);
    }
  }

  if (m_viewModeChanged)
  {
    if (m_dwShowViewModeTimeout.time_since_epoch().count() != 0)
    {
      SET_CONTROL_VISIBLE(LABEL_ROW1);
      SET_CONTROL_VISIBLE(LABEL_ROW2);
      SET_CONTROL_VISIBLE(LABEL_ROW3);
      SET_CONTROL_VISIBLE(BLUE_BAR);
    }
    else
    {
      SET_CONTROL_HIDDEN(LABEL_ROW1);
      SET_CONTROL_HIDDEN(LABEL_ROW2);
      SET_CONTROL_HIDDEN(LABEL_ROW3);
      SET_CONTROL_HIDDEN(BLUE_BAR);
    }
    m_viewModeChanged = false;
  }
}

void CGUIWindowFullScreen::ClearBackground()
{
  const auto& components = CServiceBroker::GetAppComponents();
  const auto appPlayer = components.GetComponent<CApplicationPlayer>();
  if (appPlayer->IsRenderingVideoLayer())
    CServiceBroker::GetWinSystem()->GetGfxContext().Clear(0);
}

void CGUIWindowFullScreen::OnWindowLoaded()
{
  CGUIWindow::OnWindowLoaded();
  m_clearBackground = 0;
}

bool CGUIWindowFullScreen::OnMessage(CGUIMessage& message)
{
  switch (message.GetMessage())
  {
  case GUI_MSG_WINDOW_INIT:
    {
      const auto& components = CServiceBroker::GetAppComponents();
      const auto appPlayer = components.GetComponent<CApplicationPlayer>();
      if (message.GetParam1() == WINDOW_INVALID && !appPlayer->IsPlayingVideo())
      {
        CServiceBroker::GetGUI()->GetWindowManager().PreviousWindow();
        return true;
      }

      GUIINFO::CPlayerGUIInfo& guiInfo = CServiceBroker::GetGUI()->GetInfoManager().GetInfoProviders().GetPlayerInfoProvider();
      guiInfo.SetShowInfo(false);
      m_bShowCurrentTime = false;
      CServiceBroker::GetWinSystem()->GetGfxContext().SetFullScreenVideo(true);

      CGUIWindow::OnMessage(message);

      m_dwShowViewModeTimeout = {};
      m_viewModeChanged = true;
      return true;
    }
  case GUI_MSG_WINDOW_DEINIT:
    {
      CServiceBroker::GetGUI()->GetWindowManager().CloseInternalModalDialogs(true);
      CGUIWindow::OnMessage(message);
      CServiceBroker::GetSettingsComponent()->GetSettings()->Save();
      CServiceBroker::GetWinSystem()->GetGfxContext().SetFullScreenVideo(false);
      return true;
    }
  case GUI_MSG_SETFOCUS:
  case GUI_MSG_LOSTFOCUS:
    if (message.GetSenderId() != WINDOW_FULLSCREEN_VIDEO) return true;
    break;
  }

  return CGUIWindow::OnMessage(message);
}

EVENT_RESULT CGUIWindowFullScreen::OnMouseEvent(const CPoint& point,
                                                const MOUSE::CMouseEvent& event)
{
  if (event.m_id == ACTION_MOUSE_RIGHT_CLICK)
  {
    OnAction(CAction(ACTION_SHOW_GUI));
    return EVENT_RESULT_HANDLED;
  }
  if (event.m_id == ACTION_MOUSE_WHEEL_UP)
  {
    return g_application.OnAction(CAction(ACTION_ANALOG_SEEK_FORWARD, 0.5f)) ? EVENT_RESULT_HANDLED : EVENT_RESULT_UNHANDLED;
  }
  if (event.m_id == ACTION_MOUSE_WHEEL_DOWN)
  {
    return g_application.OnAction(CAction(ACTION_ANALOG_SEEK_BACK, 0.5f)) ? EVENT_RESULT_HANDLED : EVENT_RESULT_UNHANDLED;
  }
  return EVENT_RESULT_UNHANDLED;
}

void CGUIWindowFullScreen::Process(unsigned int currentTime, CDirtyRegionList &dirtyregion)
{
  const auto& components = CServiceBroker::GetAppComponents();
  const auto appPlayer = components.GetComponent<CApplicationPlayer>();
  if (appPlayer->IsRenderingGuiLayer())
    MarkDirtyRegion();

  m_controlStats->Reset();
  CGUIWindow::Process(currentTime, dirtyregion);
  m_renderRegion.SetRect(0, 0, (float)CServiceBroker::GetWinSystem()->GetGfxContext().GetWidth(), (float)CServiceBroker::GetWinSystem()->GetGfxContext().GetHeight());
}

void CGUIWindowFullScreen::Render()
{
  CServiceBroker::GetWinSystem()->GetGfxContext().SetRenderingResolution(CServiceBroker::GetWinSystem()->GetGfxContext().GetVideoResolution(), false);
  auto& components = CServiceBroker::GetAppComponents();
  const auto appPlayer = components.GetComponent<CApplicationPlayer>();
  appPlayer->Render(true, 255);
  CServiceBroker::GetWinSystem()->GetGfxContext().SetRenderingResolution(m_coordsRes, m_needsScaling);
  CGUIWindow::Render();
}

void CGUIWindowFullScreen::RenderEx()
{
  CGUIWindow::RenderEx();
  CServiceBroker::GetWinSystem()->GetGfxContext().SetRenderingResolution(CServiceBroker::GetWinSystem()->GetGfxContext().GetVideoResolution(), false);
  auto& components = CServiceBroker::GetAppComponents();
  const auto appPlayer = components.GetComponent<CApplicationPlayer>();
  appPlayer->Render(false, 255, false);
  CServiceBroker::GetWinSystem()->GetGfxContext().SetRenderingResolution(m_coordsRes, m_needsScaling);
}

void CGUIWindowFullScreen::SeekChapter(int iChapter)
{
  auto& components = CServiceBroker::GetAppComponents();
  const auto appPlayer = components.GetComponent<CApplicationPlayer>();
  appPlayer->SeekChapter(iChapter);
}

void CGUIWindowFullScreen::ToggleOSD()
{
  CGUIDialog *pOSD = GetOSD();
  if (pOSD)
  {
    if (pOSD->IsDialogRunning())
      pOSD->Close();
    else
      pOSD->Open();
  }
  MarkDirtyRegion();
}

void CGUIWindowFullScreen::TriggerOSD()
{
  CGUIDialog *pOSD = GetOSD();
  if (pOSD && !pOSD->IsDialogRunning())
  {
    const auto& components = CServiceBroker::GetAppComponents();
    const auto appPlayer = components.GetComponent<CApplicationPlayer>();
    if (!appPlayer->IsPlayingGame())
      pOSD->SetAutoClose(3000);
    pOSD->Open();
  }
}

bool CGUIWindowFullScreen::HasVisibleControls()
{
  return m_controlStats->nCountVisible > 0;
}

CGUIDialog* CGUIWindowFullScreen::GetOSD()
{
  return CServiceBroker::GetGUI()->GetWindowManager().GetDialog(WINDOW_DIALOG_VIDEO_OSD);
}
