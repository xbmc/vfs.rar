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

#include "RarManager.h"
#include "RarControl.h"
#include "Helpers.h"

#if (defined(__GNUC__) || defined(__clang__)) && !defined(_LIBCPP_VERSION)
#include "wstring_convert.h"
#include "codecvt.h"
#else
#include <codecvt>
#endif
#include <kodi/General.h>
#include <locale>
#include <set>

void CRarManager::Tokenize(const std::string& input, std::vector<std::string>& tokens, const std::string& delimiters)
{
  tokens.clear();
  // Skip delimiters at beginning.
  std::string::size_type dataPos = input.find_first_not_of(delimiters);
  while (dataPos != std::string::npos)
  {
    // Find next delimiter
    const std::string::size_type nextDelimPos = input.find_first_of(delimiters, dataPos);
    // Found a token, add it to the vector.
    tokens.push_back(input.substr(dataPos, nextDelimPos - dataPos));
    // Skip delimiters.  Note the "not_of"
    dataPos = input.find_first_not_of(delimiters, nextDelimPos);
  }
}

CRarManager& CRarManager::Get()
{
  static CRarManager instance;
  
  return instance;
}


/////////////////////////////////////////////////

CRarManager::~CRarManager()
{
  ClearCache(true);
}

bool CRarManager::CacheRarredFile(std::string& strPathInCache, const std::string& strRarPath,
                                  const std::string& strPathInRar, uint8_t bOptions,
                                  const std::string& strDir, const int64_t iSize)
{
  bool bShowProgress=false;
  if ((iSize > 1024*1024 || iSize == -2) && !(bOptions & EXFILE_NOCACHE)) // 1MB
    bShowProgress=true;

  //If file is listed in the cache, then use listed copy or cleanup before overwriting.
  bool bOverwrite = (bOptions & EXFILE_OVERWRITE) != 0;

  /*
   * alwinus:
   * Mallet on my Head, hard if main thread is locked somewhere in this class
   * and within here, GUI where need main thread, is called (if bShowProgress is
   * true) :(, Crrrrh
   *
   * The progress is mandatory for me, in case of "Solid RAR" package need the
   * content complete extracted which can e.g. on my test RAR with 15 splited
   * RAR files, 8 GByte size of all and over 40 files inside take very long, if
   * last of them is played (many minutes).
   *
   * During tests this has worked, but is not nice and is ugly!
   *
   * VERY BIG TODO: Fix Kodi for this cases!!!
   */
  std::unique_lock<std::recursive_mutex> lock(m_lock);
  if (bShowProgress)
    lock.unlock();

  auto j = m_ExFiles.find(strRarPath);
  CFileInfo* pFile = nullptr;
  if (j != m_ExFiles.end())
  {
    pFile = GetFileInRar(strRarPath, strPathInRar);
    if (pFile)
    {
      if (kodi::vfs::FileExists(pFile->m_strCachedPath, true))
      {
        if (!bOverwrite)
        {
          strPathInCache = pFile->m_strCachedPath;
          pFile->m_iUsed++;
          return true;
        }

        kodi::vfs::DeleteFile(pFile->m_strCachedPath);
        pFile->m_iUsed++;
      }
    }
  }

  std::string cf = strDir+"rarfolderXXXXXX";
#ifdef _WIN32
  _mktemp_s(const_cast<char*>(cf.c_str()), cf.length() + 1);
#else
  mkdtemp(const_cast<char*>(cf.c_str()));
#endif

  std::string strPath = strPathInRar;
  std::string strCachedPath = cf+'/'+strPathInRar;
  if (strCachedPath.empty())
  {
    kodiLog(ADDON_LOG_ERROR, "CRarManager::%s: Could not cache file %s", __func__, (strRarPath + strPathInRar).c_str());
    return false;
  }

  if (j != m_ExFiles.end())  // grab from list
  {
    for (const auto& entry : j->second.first)
    {
      std::string strName;

      /* convert to utf8 */
      if (entry.FileNameW && wcslen(entry.FileNameW) > 0)
      {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
        strName = conv.to_bytes(entry.FileNameW);
      }
      else
        kodi::UnknownToUTF8(entry.FileName, strName);

      if (strName == strPath)
      {
        break;
      }
    }
  }

  CRARControl control(strRarPath);
  int iRes = control.ArchiveExtract(cf, strPath, bShowProgress);
  if (iRes == 0)
  {
    kodiLog(ADDON_LOG_ERROR, "CRarManager::%s: failed to extract file: %s", __func__, strPathInRar.c_str());
    return false;
  }

  if (!pFile)
  {
    CFileInfo fileInfo;
    fileInfo.m_strPathInRar = strPathInRar;
    if (j == m_ExFiles.end())
    {
      std::vector<RARHeaderDataEx> pArchiveList;

      if (control.ArchiveList(pArchiveList))
      {
        m_ExFiles.insert(std::make_pair(strRarPath, std::make_pair(pArchiveList, std::vector<CFileInfo>())));
        j = m_ExFiles.find(strRarPath);
      }
      else
      {
        return false;
      }
    }
    j->second.second.push_back(fileInfo);
    pFile = &(j->second.second[j->second.second.size() - 1]);
    pFile->m_iUsed = 1;
  }
  pFile->m_strCachedPath = strCachedPath;
  pFile->m_bAutoDel = (bOptions & EXFILE_AUTODELETE) != 0;
  strPathInCache = pFile->m_strCachedPath;

  if (iRes == 2) //canceled
  {
    kodi::vfs::DeleteFile(pFile->m_strCachedPath);
    return false;
  }

  return true;
}

// NB: The rar manager expects paths in rars to be terminated with a "\".
bool CRarManager::GetFilesInRar(std::vector<kodi::vfs::CDirEntry>& vecpItems, const std::string& strRarPath,
                                bool bMask, const std::string& strPathInRar)
{
  std::unique_lock<std::recursive_mutex> lock(m_lock);

  std::vector<RARHeaderDataEx> pFileList;
  auto it = m_ExFiles.find(strRarPath);
  if (it == m_ExFiles.end())
  {
    CRARControl control(strRarPath);
    if (control.ArchiveList(pFileList))
      m_ExFiles.insert(std::make_pair(strRarPath, std::make_pair(pFileList, std::vector<CFileInfo>())));
    else
      return false;
  }
  else
    pFileList = it->second.first;

  std::vector<std::string> vec;
  std::set<std::string> dirSet;
  Tokenize(strPathInRar, vec, "/");
  unsigned int iDepth = vec.size();

  std::string strCompare = strPathInRar;
  if (!strCompare.empty() && strCompare[strCompare.size()-1] != '/')
    strCompare += '/';

  for (const auto& entry : pFileList)
  {
    std::string strName;

    /* convert to utf8 */
    if (entry.FileNameW && wcslen(entry.FileNameW) > 0)
    {
      std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
      strName = conv.to_bytes(entry.FileNameW);
    }
    else
      kodi::UnknownToUTF8(entry.FileName, strName);

    /* replace back slashes into forward slashes */
    /* this could get us into troubles, file could two different files, one with / and one with \ */
    //StringUtils::Replace(strName, '\\', '/');
    size_t index = 0;
    std::string oldStr = "\\";
    std::string newStr = "/";
    while (index < strName.size() && (index = strName.find(oldStr, index)) != std::string::npos)
    {
      strName.replace(index, oldStr.size(), newStr);
      index += newStr.size();
    }

    if (bMask)
    {
      if (!strstr(strName.c_str(), strCompare.c_str()))
        continue;

      vec.clear();
      Tokenize(strName, vec, "/");
      if (vec.size() < iDepth)
        continue;
    }

    unsigned int iMask = (entry.HostOS==3 ? 0x0040000 : 16); // win32 or unix attribs?
    if (((entry.FileAttr & iMask) == iMask) || (vec.size() > iDepth + 1 && bMask)) // we have a directory
    {
      if (!bMask)
        continue;
      if (vec.size() == iDepth)
        continue; // remove root of listing

      if (dirSet.find(vec[iDepth]) == dirSet.end())
      {
        kodi::vfs::CDirEntry file;
        dirSet.insert(vec[iDepth]);
        file.SetLabel(vec[iDepth]);
        file.SetPath(vec[iDepth] + '/');
        file.SetSize(0);
        file.SetFolder(true);

        char tmp[16];
        sprintf(tmp, "%i", entry.Method);
        file.AddProperty("rarcompressionmethod", tmp);

        vecpItems.push_back(file);
      }
    }
    else
    {
      if (vec.size() == iDepth + 1 || !bMask)
      {
        kodi::vfs::CDirEntry file;
        if (vec.size() == 0)
          file.SetLabel(strName);
        else
          file.SetLabel(vec[iDepth]);
        file.SetPath(strName.c_str() + strPathInRar.size());
        file.SetSize(entry.UnpSize);
        file.SetFolder(false);

        char tmp[16];
        sprintf(tmp, "%i", entry.Method);
        file.AddProperty("rarcompressionmethod", tmp);

        vecpItems.push_back(file);
      }
    }
  }

  return true;
}

CFileInfo* CRarManager::GetFileInRar(const std::string& strRarPath, const std::string& strPathInRar)
{
  auto j = m_ExFiles.find(strRarPath);
  if (j == m_ExFiles.end())
    return nullptr;

  for (auto& it : j->second.second)
  {
    if (it.m_strPathInRar == strPathInRar)
      return &it;
  }

  return nullptr;
}

bool CRarManager::GetPathInCache(std::string& strPathInCache, const std::string& strRarPath, const std::string& strPathInRar)
{
  auto j = m_ExFiles.find(strRarPath);
  if (j == m_ExFiles.end())
    return false;

  for (const auto& it : j->second.second)
  {
    if (it.m_strPathInRar == strPathInRar)
      return kodi::vfs::FileExists(it.m_strCachedPath, true);
  }

  return false;
}

bool CRarManager::IsFileInRar(bool& bResult, const std::string& strRarPath, const std::string& strPathInRar)
{
  bResult = false;
  std::vector<kodi::vfs::CDirEntry> ItemList;

  if (!GetFilesInRar(ItemList, strRarPath, false))
    return false;

  size_t it;
  for (it = 0; it < ItemList.size(); ++it)
  {
    if (strPathInRar.compare(ItemList[it].Path()) == 0)
      break;
  }
  if (it != ItemList.size())
    bResult = true;

  // LEAKs the list

  return true;
}

void CRarManager::ClearCache(bool force)
{
  std::unique_lock<std::recursive_mutex> lock(m_lock);

  for (const auto& it : m_ExFiles)
  {
    for (const auto& it2 : it.second.second)
    {
      const CFileInfo* pFile = &it2;
      if (pFile->m_bAutoDel && (pFile->m_iUsed < 1 || force))
        kodi::vfs::DeleteFile(pFile->m_strCachedPath);
    }
  }

  m_ExFiles.clear();
}

void CRarManager::ClearCachedFile(const std::string& strRarPath, const std::string& strPathInRar)
{
  std::unique_lock<std::recursive_mutex> lock(m_lock);

  auto j = m_ExFiles.find(strRarPath);
  if (j == m_ExFiles.end())
  {
    return; // no such subpath
  }

  for (auto& it : j->second.second)
  {
    if (it.m_strPathInRar == strPathInRar)
      if (it.m_iUsed > 0)
      {
        it.m_iUsed--;
        break;
      }
  }
}

void CRarManager::ExtractArchive(const std::string& strArchive, const std::string& strPath)
{
  CRARControl m_control(strArchive);

  std::string strPath2(strPath);
  if (!strPath2.empty() && strPath2[strPath2.size()-1] == '/')
    strPath2.erase(strPath2.end()-1);
  if (!m_control.ArchiveExtract(strPath2, ""))
    kodiLog(ADDON_LOG_ERROR,"CRarManager::%s: error while extracting %s", __func__, strArchive.c_str());
}
