/*
 *      Copyright (C) 2005-2020 Team Kodi
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

#include "RarControl.h"

#include <map>
#include <mutex>
#include <string>
#include <vector>

#define EXFILE_OVERWRITE 1
#define EXFILE_AUTODELETE 2
#define EXFILE_UNIXPATH 4
#define EXFILE_NOCACHE 8

class CFileInfo
{
public:
  CFileInfo() = default;

  std::string m_strCachedPath;
  std::string m_strPathInRar;
  bool m_bAutoDel = true;
  int m_iUsed = 0;
  int m_iIsSeekable = -1;
};

class CRarManager
{
public:
  static CRarManager& Get();
  ~CRarManager();

  static void Tokenize(const std::string& input, std::vector<std::string>& tokens, const std::string& delimiters);

  bool CacheRarredFile(std::string& strPathInCache, const std::string& strRarPath,
                       const std::string& strPathInRar, uint8_t bOptions,
                       const std::string& strDir, const int64_t iSize=-1);
  bool GetPathInCache(std::string& strPathInCache, const std::string& strRarPath,
                      const std::string& strPathInRar = "");
  bool GetFilesInRar(std::vector<kodi::vfs::CDirEntry>& vecpItems, const std::string& strRarPath,
                     bool bMask=true, const std::string& strPathInRar="");
  CFileInfo* GetFileInRar(const std::string& strRarPath, const std::string& strPathInRar);
  bool IsFileInRar(bool& bResult, const std::string& strRarPath, const std::string& strPathInRar);
  void ClearCache(bool force=false);
  void ClearCachedFile(const std::string& strRarPath, const std::string& strPathInRar);
  void ExtractArchive(const std::string& strArchive, const std::string& strPath);

private:
  CRarManager() = default;

  std::map<std::string, std::pair<std::vector<RARHeaderDataEx>,std::vector<CFileInfo> > > m_ExFiles;
  std::recursive_mutex m_lock;
};
