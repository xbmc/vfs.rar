/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "RarExtractThread.h"
#include "rar.hpp"

#include <kodi/General.h>

CRarFileExtractThread::CRarFileExtractThread() : hRunning(false), m_thread{}
{
}

CRarFileExtractThread::~CRarFileExtractThread()
{
  hRestart.Wait();
  m_stopThread = true;
  if (m_thread.joinable())
    m_thread.join();
}

void CRarFileExtractThread::Start(Archive* pArc, CommandData* pCmd, CmdExtract* pExtract, int iSize)
{
  m_pArc = pArc;
  m_pCmd = pCmd;
  m_pExtract = pExtract;
  m_iSize = iSize;

  m_pExtract->GetDataIO().hBufferFilled = new ThreadHelpers::CEvent();
  m_pExtract->GetDataIO().hBufferEmpty = new ThreadHelpers::CEvent();
  m_pExtract->GetDataIO().hSeek = new ThreadHelpers::CEvent(false);
  m_pExtract->GetDataIO().hSeekDone = new ThreadHelpers::CEvent();
  m_pExtract->GetDataIO().hQuit = new ThreadHelpers::CEvent(false);

  hRunning.Signal();
  hRestart.Signal();

  m_thread = std::thread{&CRarFileExtractThread::Process, this};

}

void CRarFileExtractThread::Process()
{
  while (!m_pExtract->GetDataIO().hQuit->Wait(1) && !m_stopThread)
  {
    if (hRestart.Wait(1))
    {
      bool Repeat = false;
      try
      {
        m_pExtract->ExtractCurrentFile(*m_pArc, m_iSize, Repeat);
      }
      catch (int rarErrCode)
      {
        kodiLog(ADDON_LOG_ERROR, "CFileRarExtractThread::%s: failed. CmdExtract::ExtractCurrentFile threw a UnrarXLib error code of %d", __func__, rarErrCode);
      }
      catch (...)
      {
        kodiLog(ADDON_LOG_ERROR, "CFileRarExtractThread::%s: failed. CmdExtract::ExtractCurrentFile threw an Unknown exception (error code of %d)", __func__, ErrHandler.GetErrorCode());
      }

      hRunning.Reset();
    }
  }

  hRunning.Reset();
  hRestart.Signal();
}
