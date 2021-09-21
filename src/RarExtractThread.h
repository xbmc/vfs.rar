/*
 *  Copyright (C) 2005-2021 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
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
