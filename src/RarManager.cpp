/*
 *  Copyright (C) 2005-2021 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "RarManager.h"
#include "RarControl.h"
#include "Helpers.h"

#include <algorithm>
#include <climits>
#include <kodi/Filesystem.h>
#include <kodi/General.h>
#include <kodi/gui/dialogs/YesNo.h>
#include <locale>
#include <set>

#ifdef _WIN32 // windows
#ifdef RemoveDirectory
#undef RemoveDirectory
#endif // RemoveDirectory
#endif // _WIN32

#define EXTRACTION_WARN_SIZE 50*1024*1024

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

CRarManager::CRarManager()
{
  // Load the current settings and store to reduce call amount of them
  m_asksToUnpack = kodi::GetSettingBoolean("asks_to_unpack");
  m_passwordAskAllowed = kodi::GetSettingBoolean("usercheck_for_password");
  for (unsigned int i = 0; i < MAX_STANDARD_PASSWORDS; ++i)
    m_standardPasswords[i] = kodi::GetSettingString("standard_password_" + std::to_string(i+1));
}

CRarManager::~CRarManager()
{
  ClearCache(true);
}

void CRarManager::SettingsUpdate(const std::string& settingName, const kodi::CSettingValue& settingValue)
{
  // Update the by CMyAddon called settings values, done after change inside
  // addon settings by e.g. user.
  if (settingName == "asks_to_unpack")
    m_asksToUnpack = settingValue.GetBoolean();
  else if (settingName == "usercheck_for_password")
    m_passwordAskAllowed = settingValue.GetBoolean();
  else if (settingName == "standard_password_1")
    m_standardPasswords[0] = settingValue.GetString();
  else if (settingName == "standard_password_2")
    m_standardPasswords[1] = settingValue.GetString();
  else if (settingName == "standard_password_3")
    m_standardPasswords[2] = settingValue.GetString();
  else if (settingName == "standard_password_4")
    m_standardPasswords[3] = settingValue.GetString();
  else if (settingName == "standard_password_5")
    m_standardPasswords[4] = settingValue.GetString();
}

bool CRarManager::CacheRarredFile(std::string& strPathInCache, const std::string& strRarPath,
                                  const std::string& strPathInRar, uint8_t bOptions,
                                  const std::string& strDir, const int64_t iSize)
{
  bool bShowProgress=false;
  if ((iSize > 1024*1024 || iSize == -2) && !(bOptions & EXFILE_NOCACHE)) // 1MB
    bShowProgress=true;

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

  //If file is listed in the cache, then use listed copy or cleanup before overwriting.
  bool bOverwrite = (bOptions & EXFILE_OVERWRITE) != 0;
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

  if (m_asksToUnpack && iSize > EXTRACTION_WARN_SIZE)
  {
    if (!kodi::gui::dialogs::YesNo::ShowAndGetInput(kodi::GetLocalizedString(30019),
                                                    kodi::GetLocalizedString(30020),
                                                    kodi::vfs::GetFileName(strRarPath),
                                                    ""))
      return false;
  }

  if (CheckFreeSpace(strDir) < iSize)
  {
    ClearCache();
    if (CheckFreeSpace(strDir) < iSize)
    {
      std::vector<kodi::vfs::CDirEntry> items;
      kodi::vfs::GetDirectory(kodi::GetTempAddonPath("/"), "", items);
      while (!items.empty() && CheckFreeSpace(strDir) < iSize)
      {
        std::string path = items.back().Path();
        items.pop_back();

        if (!kodi::vfs::RemoveDirectory(path, true))
        {
          kodiLog(ADDON_LOG_ERROR, "CRarManager::%s: Failed to delete cached rar %s", __func__, path.c_str());
          return false;
        }
      }

      if (items.empty())
      {
        kodi::QueueNotification(QUEUE_ERROR, "", kodi::GetLocalizedString(30021));
        return false;
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
    char name[MAX_PATH_LENGTH];
    for (const auto& entry : j->second.first)
    {
      std::string strName;

      /* convert to utf8 */
      if (entry.FileNameW && wcslen(entry.FileNameW) > 0)
      {
        WideToUtf(entry.FileNameW, name, sizeof(name));
        strName = name;
      }
      else
        kodi::UnknownToUTF8(entry.FileName, strName);

      /* replace back slashes into forward slashes */
      /* this could get us into troubles, file could two different files, one with / and one with \ */
      std::replace(strName.begin(), strName.end(), '\\', '/');

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

  char name[MAX_PATH_LENGTH];
  for (const auto& entry : pFileList)
  {
    std::string strName;

    /* convert to utf8 */
    if (entry.FileNameW && wcslen(entry.FileNameW) > 0)
    {
      WideToUtf(entry.FileNameW, name, sizeof(name));
      strName = name;
    }
    else
      kodi::UnknownToUTF8(entry.FileName, strName);

    /* replace back slashes into forward slashes */
    /* this could get us into troubles, file could two different files, one with / and one with \ */
    std::replace(strName.begin(), strName.end(), '\\', '/');

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
    if (entry.Flags & RHDF_DIRECTORY ||
        entry.FileAttr & iMask ||
        (vec.size() > iDepth + 1 && bMask)) // we have a directory
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
        file.AddProperty("rarcompressionmethod", std::to_string(entry.Method));

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
        file.SetSize((uint64_t(entry.UnpSizeHigh)<<32)|entry.UnpSize);
        file.SetFolder(false);
        file.AddProperty("rarcompressionmethod", std::to_string(entry.Method));

        vecpItems.push_back(file);
      }
    }
  }

  return true;
}

bool CRarManager::GetFileInRar(const std::string& strRarPath, const std::string& strPathInRar, kodi::vfs::CDirEntry& item)
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

  char name[MAX_PATH_LENGTH];
  for (const auto& entry : pFileList)
  {
    std::string strName;

    /* convert to utf8 */
    if (entry.FileNameW && wcslen(entry.FileNameW) > 0)
    {
      WideToUtf(entry.FileNameW, name, sizeof(name));
      strName = name;
    }
    else
      kodi::UnknownToUTF8(entry.FileName, strName);

    /* replace back slashes into forward slashes */
    /* this could get us into troubles, file could two different files, one with / and one with \ */
    std::replace(strName.begin(), strName.end(), '\\', '/');

    if (strPathInRar != strName)
      continue;

    unsigned int iMask = (entry.HostOS==3 ? 0x0040000 : 16); // win32 or unix attribs?

    item.SetLabel(strName);
    item.SetPath(strName.c_str() + strPathInRar.size());
    item.SetSize((uint64_t(entry.UnpSizeHigh)<<32)|entry.UnpSize);
    item.SetFolder(entry.Flags & RHDF_DIRECTORY || entry.FileAttr & iMask ? true : false);
    item.AddProperty("rarcompressionmethod", std::to_string(entry.Method));
    return true;
  }

  return false;
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

bool CRarManager::IsFileInRar(const std::string& strRarPath, const std::string& strPathInRar)
{
  kodi::vfs::CDirEntry item;
  return GetFileInRar(strRarPath, strPathInRar, item);
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
  kodi::vfs::RemoveSlashAtEnd(strPath2);
  if (!m_control.ArchiveExtract(strPath2, ""))
    kodiLog(ADDON_LOG_ERROR,"CRarManager::%s: error while extracting %s", __func__, strArchive.c_str());
}

uint64_t CRarManager::CheckFreeSpace(const std::string& targetPath)
{
  uint64_t capacity = ULLONG_MAX;
  uint64_t free = ULLONG_MAX;
  uint64_t available = ULLONG_MAX;
  kodi::vfs::GetDiskSpace(targetPath, capacity, free, available);
  return available;
}
