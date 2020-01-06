/*
 *      Copyright (C) 2005-2019 Team Kodi
 *      http://kodi.tv
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
 *  along with Kodi; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include "Helpers.h"

#include <thread>

class Archive;
class CmdExtract;
class CommandData;

class CRarFileExtractThread
{
public:
  CRarFileExtractThread();
  virtual ~CRarFileExtractThread();

  void Start(Archive* pArc, CommandData* pCmd, CmdExtract* pExtract, int iSize);

  ThreadHelpers::CEvent hRunning;
  ThreadHelpers::CEvent hRestart;

protected:
  void Process();

  Archive* m_pArc = nullptr;
  CommandData* m_pCmd = nullptr;
  CmdExtract* m_pExtract = nullptr;
  int m_iSize = 0;
  std::thread m_thread;
  bool m_stopThread = false;
};
