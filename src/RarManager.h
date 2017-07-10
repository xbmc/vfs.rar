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
#ifndef RAR_MANAGER_H_
#define RAR_MANAGER_H_

#include <map>
#include <string>
#include <vector>

#include "UnrarX.hpp"

#include "p8-platform/threads/threads.h"
#include <kodi/addon-instance/VFS.h>

#define EXFILE_OVERWRITE 1
#define EXFILE_AUTODELETE 2
#define EXFILE_UNIXPATH 4
#define EXFILE_NOCACHE 8
#define RAR_DEFAULT_CACHE "special://temp/"
#define RAR_DEFAULT_PASSWORD ""

class CFileInfo{
public:
  CFileInfo();
  ~CFileInfo();
  std::string m_strCachedPath;
  std::string m_strPathInRar;
  bool  m_bAutoDel;
  int m_iUsed;
  int64_t m_iOffset;

  bool m_bIsCanceled()
  {
    if (watch.IsSet())
      if (watch.TimeLeft() > 0)
        return true;

    return false;
  }
  P8PLATFORM::CTimeout watch;
  int m_iIsSeekable;
};

class CRarManager
{
public:
  static CRarManager& Get();
  ~CRarManager();
  static void Tokenize(const std::string& input, std::vector<std::string>& tokens, const std::string& delimiters);
  bool CacheRarredFile(std::string& strPathInCache, const std::string& strRarPath,
                       const std::string& strPathInRar, uint8_t bOptions = EXFILE_AUTODELETE,
                       const std::string& strDir =RAR_DEFAULT_CACHE, const int64_t iSize=-1);
  bool GetPathInCache(std::string& strPathInCache, const std::string& strRarPath,
                      const std::string& strPathInRar = "");
  bool GetFilesInRar(std::vector<kodi::vfs::CDirEntry>& vecpItems, const std::string& strRarPath,
                     bool bMask=true, const std::string& strPathInRar="");
  CFileInfo* GetFileInRar(const std::string& strRarPath, const std::string& strPathInRar);
  bool IsFileInRar(bool& bResult, const std::string& strRarPath, const std::string& strPathInRar);
  void ClearCache(bool force=false);
  void ClearCachedFile(const std::string& strRarPath, const std::string& strPathInRar);
  void ExtractArchive(const std::string& strArchive, const std::string& strPath);
protected:
  CRarManager();

  bool ListArchive(const std::string& strRarPath, ArchiveList_struct* &pArchiveList);
  std::map<std::string, std::pair<ArchiveList_struct*,std::vector<CFileInfo> > > m_ExFiles;
  P8PLATFORM::CMutex m_lock;

  int64_t CheckFreeSpace(const std::string& strDrive);
};
#endif
