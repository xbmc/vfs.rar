/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */
#include "RarExtractThread.h"
#include <kodi/General.h>
#include "rar.hpp"


CRarFileExtractThread::CRarFileExtractThread() : hRunning(false), hQuit(false)
{
  m_pArc = NULL;
  m_pCmd = NULL;
  m_pExtract = NULL;
  CreateThread();
}

CRarFileExtractThread::~CRarFileExtractThread()
{
  hQuit.Signal();
  hRestart.Wait();
  StopThread();
}

void CRarFileExtractThread::Start(Archive* pArc, CommandData* pCmd, CmdExtract* pExtract, int iSize)
{
  m_pArc = pArc;
  m_pCmd = pCmd;
  m_pExtract = pExtract;
  m_iSize = iSize;

  m_pExtract->GetDataIO().hBufferFilled = new P8PLATFORM::CEvent();
  m_pExtract->GetDataIO().hBufferEmpty = new P8PLATFORM::CEvent();
  m_pExtract->GetDataIO().hSeek = new P8PLATFORM::CEvent(false);
  m_pExtract->GetDataIO().hSeekDone = new P8PLATFORM::CEvent();
  m_pExtract->GetDataIO().hQuit = new P8PLATFORM::CEvent(false);

  hRunning.Signal();
  hRestart.Signal();
}

void* CRarFileExtractThread::Process()
{
  while (!hQuit.Wait(1))
  {
    if (hRestart.Wait(1))
    {
      bool Repeat = false;
      try
      {
        m_pExtract->ExtractCurrentFile(m_pCmd,*m_pArc,m_iSize,Repeat);
      }
      catch (int rarErrCode)
      {
        kodi::Log(ADDON_LOG_ERROR,"CFileRarExtractThread::Process failed. CmdExtract::ExtractCurrentFile threw a UnrarXLib error code of %d",rarErrCode);
      }
      catch (...)
      {
        kodi::Log(ADDON_LOG_ERROR,"filerar CFileRarExtractThread::Process failed. CmdExtract::ExtractCurrentFile threw an Unknown exception");
      }

      hRunning.Reset();
    }
  }
  hRestart.Signal();
  return NULL;
}
