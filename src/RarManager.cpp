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

#include "RarManager.h"
#include <kodi/General.h>
#include <set>

using namespace std;
using namespace XFILE;

CFileInfo::CFileInfo()
{
  m_strCachedPath.clear();
  m_bAutoDel = true;
  m_iUsed = 0;
  m_iIsSeekable = -1;
}

CFileInfo::~CFileInfo()
{
}

void CRarManager::Tokenize(const std::string& input, std::vector<std::string>& tokens, const std::string& delimiters)
{
  // Tokenize ripped from http://www.linuxselfhelp.com/HOWTO/C++Programming-HOWTO-7.html
  // Skip delimiters at beginning.
  string::size_type lastPos = input.find_first_not_of(delimiters, 0);
  // Find first "non-delimiter".
  string::size_type pos = input.find_first_of(delimiters, lastPos);

  while (string::npos != pos || string::npos != lastPos)
  {
    // Found a token, add it to the vector.
    tokens.push_back(input.substr(lastPos, pos - lastPos));
    // Skip delimiters.  Note the "not_of"
    lastPos = input.find_first_not_of(delimiters, pos);
    // Find next "non-delimiter"
    pos = input.find_first_of(delimiters, lastPos);
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
}

CRarManager::~CRarManager()
{
  ClearCache(true);
}

bool CRarManager::CacheRarredFile(std::string& strPathInCache, const std::string& strRarPath, const std::string& strPathInRar, uint8_t bOptions, const std::string& strDir, const int64_t iSize)
{
  P8PLATFORM::CLockObject lock(m_lock);

  //If file is listed in the cache, then use listed copy or cleanup before overwriting.
  bool bOverwrite = (bOptions & EXFILE_OVERWRITE) != 0;
  map<std::string, pair<ArchiveList_struct*,vector<CFileInfo> > >::iterator j = m_ExFiles.find( strRarPath );
  CFileInfo* pFile=NULL;
  if (j != m_ExFiles.end())
  {
    pFile = GetFileInRar(strRarPath,strPathInRar);
    if (pFile)
    {
      if (pFile->m_bIsCanceled())
      {
        return false;
      }

      if (kodi::vfs::FileExists(pFile->m_strCachedPath.c_str(), true))
      {
        if (!bOverwrite)
        {
          strPathInCache = pFile->m_strCachedPath;
          pFile->m_iUsed++;
          return true;
        }

        kodi::vfs::DeleteFile(pFile->m_strCachedPath.c_str());
        pFile->m_iUsed++;
      }
    }
  }

  int iRes = 0;

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
    kodi::Log(ADDON_LOG_ERROR, "Could not cache file %s", (strRarPath + strPathInRar).c_str());
    return false;
  }
  int64_t iOffset = -1;
  if (iRes != 2)
  {
    if (pFile)
    {
      if (pFile->m_iOffset != -1)
        iOffset = pFile->m_iOffset;
    }


    if (iOffset == -1 && j != m_ExFiles.end())  // grab from list
    {
      for( ArchiveList_struct* pIterator = j->second.first; pIterator  ; pIterator ? pIterator = pIterator->next : NULL)
      {
        std::string strName;

        /* convert to utf8 */
        //if( pIterator->item.NameW && wcslen(pIterator->item.NameW) > 0)
        //  ;//g_charsetConverter.wToUTF8(pIterator->item.NameW, strName); // TODO
       // else
        {
          kodi::UnknownToUTF8(pIterator->item.Name, strName);
        }
        if (strName == strPath)
        {
          iOffset = pIterator->item.iOffset;
          break;
        }
      }
    }
    bool bShowProgress=false;
    if (iSize > 1024*1024 || iSize == -2) // 1MB
      bShowProgress=true;

    size_t pos = strCachedPath.rfind('/');
    std::string strDir2 = strCachedPath.substr(0,pos);
    kodi::vfs::CreateDirectory(strDir2);
    iRes = urarlib_get(const_cast<char*>(strRarPath.c_str()), const_cast<char*>(strDir2.c_str()),
                       const_cast<char*>(strPath.c_str()),NULL,&iOffset,bShowProgress);
  }
  if (iRes == 0)
  {
    kodi::Log(ADDON_LOG_ERROR,"failed to extract file: %s",strPathInRar.c_str());
    return false;
  }

  if(!pFile)
  {
    CFileInfo fileInfo;
    fileInfo.m_strPathInRar = strPathInRar;
    if (j == m_ExFiles.end())
    {
      ArchiveList_struct* pArchiveList;
      if(ListArchive(strRarPath,pArchiveList))
      {
        m_ExFiles.insert(make_pair(strRarPath,make_pair(pArchiveList,vector<CFileInfo>())));
        j = m_ExFiles.find(strRarPath);
      }
      else
      {
        return false;
      }
    }
    j->second.second.push_back(fileInfo);
    pFile = &(j->second.second[j->second.second.size()-1]);
    pFile->m_iUsed = 1;
  }
  pFile->m_strCachedPath = strCachedPath;
  pFile->m_bAutoDel = (bOptions & EXFILE_AUTODELETE) != 0;
  pFile->m_iOffset = iOffset;
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
  P8PLATFORM::CLockObject lock(m_lock);

  ArchiveList_struct* pFileList = NULL;
  map<std::string,pair<ArchiveList_struct*,std::vector<CFileInfo> > >::iterator it = m_ExFiles.find(strRarPath);
  if (it == m_ExFiles.end())
  {
    if( urarlib_list((char*) strRarPath.c_str(), &pFileList, NULL) )
      m_ExFiles.insert(make_pair(strRarPath,make_pair(pFileList,vector<CFileInfo>())));
    else
    {
      if( pFileList )
        urarlib_freelist(pFileList);
      return false;
    }
  }
  else
    pFileList = it->second.first;

  vector<std::string> vec;
  set<std::string> dirSet;
  Tokenize(strPathInRar,vec,"/");
  unsigned int iDepth = vec.size();

  ArchiveList_struct* pIterator;
  std::string strCompare = strPathInRar;
  if (!strCompare.empty() && strCompare[strCompare.size()-1] != '/')
    strCompare += '/';
  for( pIterator = pFileList; pIterator  ; pIterator ? pIterator = pIterator->next : NULL)
  {
    std::string strDirDelimiter = (pIterator->item.HostOS==3 ? "/":"\\"); // win32 or unix paths?
    std::string strName;

    /* convert to utf8 */
    if( pIterator->item.NameW && wcslen(pIterator->item.NameW) > 0)
      ;//g_charsetConverter.wToUTF8(pIterator->item.NameW, strName); // TODO
    else
    {
      kodi::UnknownToUTF8(pIterator->item.Name, strName);
    }

    /* replace back slashes into forward slashes */
    /* this could get us into troubles, file could two different files, one with / and one with \ */
    //StringUtils::Replace(strName, '\\', '/');

    if (bMask)
    {
      if (!strstr(strName.c_str(),strCompare.c_str()))
        continue;

      vec.clear();
      Tokenize(strName,vec,"/");
      if (vec.size() < iDepth)
        continue;
    }

    unsigned int iMask = (pIterator->item.HostOS==3 ? 0x0040000:16); // win32 or unix attribs?
    if (((pIterator->item.FileAttr & iMask) == iMask) || (vec.size() > iDepth+1 && bMask)) // we have a directory
    {
      if (!bMask) continue;
      if (vec.size() == iDepth)
        continue; // remove root of listing

      if (dirSet.find(vec[iDepth]) == dirSet.end())
      {
        kodi::vfs::CDirEntry entry;
        dirSet.insert(vec[iDepth]);
        entry.SetLabel(vec[iDepth]);
        entry.SetPath(vec[iDepth]+'/');
        entry.SetFolder(true);
        vecpItems.push_back(entry);
      }
    }
    else
    {
      if (vec.size() == iDepth+1 || !bMask)
      {
        kodi::vfs::CDirEntry entry;
        if (vec.size() == 0)
          entry.SetLabel(strName);
        else
          entry.SetLabel(vec[iDepth]);
        entry.SetPath(strName.c_str()+strPathInRar.size());
        entry.SetSize(pIterator->item.UnpSize);
        entry.SetFolder(false);

        char tmp[16];
        sprintf(tmp,"%i",pIterator->item.Method);
        entry.AddProperty("rarcompressionmethod", tmp);

        vecpItems.push_back(entry);
      }
    }
  }

  return true;
}

bool CRarManager::ListArchive(const std::string& strRarPath, ArchiveList_struct* &pArchiveList)
{
 return urarlib_list((char*) strRarPath.c_str(), &pArchiveList, NULL) == 1;
}

CFileInfo* CRarManager::GetFileInRar(const std::string& strRarPath, const std::string& strPathInRar)
{
  map<std::string,pair<ArchiveList_struct*,vector<CFileInfo> > >::iterator j = m_ExFiles.find(strRarPath);
  if (j == m_ExFiles.end())
    return NULL;

  for (vector<CFileInfo>::iterator it2=j->second.second.begin(); it2 != j->second.second.end(); ++it2)
    if (it2->m_strPathInRar == strPathInRar)
      return &(*it2);

  return NULL;
}

bool CRarManager::GetPathInCache(std::string& strPathInCache, const std::string& strRarPath, const std::string& strPathInRar)
{
  map<std::string,pair<ArchiveList_struct*,vector<CFileInfo> > >::iterator j = m_ExFiles.find(strRarPath);
  if (j == m_ExFiles.end())
    return false;

  for (vector<CFileInfo>::iterator it2=j->second.second.begin(); it2 != j->second.second.end(); ++it2)
    if (it2->m_strPathInRar == strPathInRar)
      return kodi::vfs::FileExists(it2->m_strCachedPath, true);

  return false;
}

bool CRarManager::IsFileInRar(bool& bResult, const std::string& strRarPath, const std::string& strPathInRar)
{
  bResult = false;
  std::vector<kodi::vfs::CDirEntry> ItemList;

  if (!GetFilesInRar(ItemList,strRarPath,false))
    return false;

  size_t it;
  for (it=0;it<ItemList.size();++it)
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
  P8PLATFORM::CLockObject lock(m_lock);
  map<std::string, pair<ArchiveList_struct*,vector<CFileInfo> > >::iterator j;
  for (j = m_ExFiles.begin() ; j != m_ExFiles.end() ; j++)
  {
    for (vector<CFileInfo>::iterator it2 = j->second.second.begin(); it2 != j->second.second.end(); ++it2)
    {
      CFileInfo* pFile = &(*it2);
      if (pFile->m_bAutoDel && (pFile->m_iUsed < 1 || force))
        kodi::vfs::DeleteFile(pFile->m_strCachedPath.c_str());
    }
    urarlib_freelist(j->second.first);
  }

  m_ExFiles.clear();
}

void CRarManager::ClearCachedFile(const std::string& strRarPath, const std::string& strPathInRar)
{
  P8PLATFORM::CLockObject lock(m_lock);
  map<std::string,pair<ArchiveList_struct*,vector<CFileInfo> > >::iterator j = m_ExFiles.find(strRarPath);
  if (j == m_ExFiles.end())
  {
    return; // no such subpath
  }

  for (vector<CFileInfo>::iterator it = j->second.second.begin(); it != j->second.second.end(); ++it)
  {
    if (it->m_strPathInRar == strPathInRar)
      if (it->m_iUsed > 0)
      {
        it->m_iUsed--;
        break;
      }
  }
}

void CRarManager::ExtractArchive(const std::string& strArchive, const std::string& strPath)
{
  std::string strPath2(strPath);
  if (!strPath2.empty() && strPath2[strPath2.size()-1] == '/')
    strPath2.erase(strPath2.end()-1);
  if (!urarlib_get(const_cast<char*>(strArchive.c_str()), const_cast<char*>(strPath2.c_str()),NULL))
    kodi::Log(ADDON_LOG_ERROR,"rarmanager::extractarchive error while extracting %s", strArchive.c_str());
}

int64_t CRarManager::CheckFreeSpace(const std::string& strDrive)
{
  return 100000000000000000;

  return 0;
}
