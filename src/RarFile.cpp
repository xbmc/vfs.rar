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

#include <kodi/addon-instance/VFS.h>
#include <kodi/General.h>
#include "p8-platform/threads/mutex.h"
#if defined(CreateDirectory)
#undef CreateDirectory
#endif
#if defined(RemoveDirectory)
#undef RemoveDirectory
#endif
#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#include <fcntl.h>

#include "rar.hpp"
#include "RarExtractThread.h"
#include "RarManager.h"

#define SEEKTIMOUT 30000

static std::string URLEncode(const std::string& strURLData)
{
  std::string strResult;

  /* wonder what a good value is here is, depends on how often it occurs */
  strResult.reserve( strURLData.length() * 2 );

  for (size_t i = 0; i < strURLData.size(); ++i)
  {
    const char kar = strURLData[i];

    // Don't URL encode "-_.!()" according to RFC1738
    //! @todo Update it to "-_.~" after Gotham according to RFC3986
    if (std::isalnum(kar) || kar == '-' || kar == '.' || kar == '_' || kar == '!' || kar == '(' || kar == ')')
      strResult.push_back(kar);
    else
    {
      char temp[128];
      sprintf(temp,"%%%2.2X", (unsigned int)((unsigned char)kar));
      strResult += temp;
    }
  }

  return strResult;
}


struct RARContext
{
  Archive* archive;
  CommandData* cmd;
  CmdExtract* extract;
  CRarFileExtractThread* extract_thread;
  uint8_t buffer[MAXWINMEMSIZE];
  uint8_t* head;
  int64_t inbuffer;
  std::string cachedir;
  std::string rarpath;
  std::string password;
  std::string pathinrar;
  int8_t fileoptions;
  int64_t size;
  kodi::vfs::CFile* file;
  int64_t fileposition;
  int64_t bufferstart;
  bool seekable;

  RARContext()
  {
    file = NULL;
    head = buffer;
    inbuffer = 0;
    archive = NULL;
    cmd = NULL;
    extract = NULL;
    extract_thread = NULL;
    fileposition = 0;
    bufferstart = 0;
    seekable = true;
  }

  ~RARContext()
  {
    if (file)
      delete file;
  }
  
  void Init(const VFSURL& url)
  {
    cachedir = "special://temp/";
    rarpath = url.hostname;
    password = url.password;
    pathinrar = url.filename;
    std::vector<std::string> options;
    std::string options2(url.options);
    if (!options2.empty())
      CRarManager::Tokenize(options2.substr(1), options, "&");
    fileoptions = 0;
    for( std::vector<std::string>::iterator it = options.begin();it != options.end(); it++)
    {
      size_t iEqual = (*it).find('=');
      if(iEqual != std::string::npos)
      {
        std::string strOption = it->substr(0, iEqual);
        std::string strValue = it->substr(iEqual+1);

        if (strOption == "flags")
          fileoptions = atoi(strValue.c_str());
        else if (strOption == "cache")
          cachedir = strValue;
      }
    }
  }

  bool OpenInArchive()
  {
    try
    {
      int iHeaderSize;

      InitCRC();

      cmd = new CommandData;
      if (!cmd)
      {
        CleanUp();
        return false;
      }

      // Set the arguments for the extract command
      strcpy(cmd->Command, "X");

      cmd->AddArcName(const_cast<char*>(rarpath.c_str()),NULL);

      strncpy(cmd->ExtrPath, cachedir.c_str(), sizeof (cmd->ExtrPath) - 2);
      cmd->ExtrPath[sizeof (cmd->ExtrPath) - 2] = 0;
      cmd->ExtrPath[sizeof (cmd->ExtrPath) - 1] = 0;
      if (cmd->ExtrPath[strlen(cmd->ExtrPath)-1] != '/')
      {
        int pos = strlen(cmd->ExtrPath)-1;
        cmd->ExtrPath[pos] = '/';
        cmd->ExtrPath[pos+1] = 0;
      }

      // Set password for encrypted archives
      if ((!password.empty()) &&
          (password.size() < sizeof (cmd->Password)))
      {
        strcpy(cmd->Password, password.c_str());
      }

      cmd->ParseDone();

      // Open the archive
      archive = new Archive(cmd);
      if (!archive)
      {
        CleanUp();
        return false;
      }
      if (!archive->WOpen(rarpath.c_str(),NULL))
      {
        CleanUp();
        return false;
      }
      if (!(archive->IsOpened() && archive->IsArchive(true)))
      {
        CleanUp();
        return false;
      }

      extract = new CmdExtract;
      if (!extract)
      {
        CleanUp();
        return false;
      }
      extract->GetDataIO().SetUnpackToMemory(buffer,0);
      extract->GetDataIO().SetCurrentCommand(*(cmd->Command));
      struct FindData FD;
      if (FindFile::FastFind(rarpath.c_str(),NULL,&FD))
          extract->GetDataIO().TotalArcSize+=FD.Size;
      extract->ExtractArchiveInit(cmd,*archive);

      while (true)
      {
        if ((iHeaderSize = archive->ReadHeader()) <= 0)
        {
          CleanUp();
          return false;
        }

        if (archive->GetHeaderType() == FILE_HEAD)
        {
          std::string strFileName;

//          if (wcslen(archive->NewLhd.FileNameW) > 0)
//          {
//            g_charsetConverter.wToUTF8(m_pArc->NewLhd.FileNameW, strFileName);
//          }
//          else
          {
            kodi::UnknownToUTF8(archive->NewLhd.FileName, strFileName);
          }

          /* replace back slashes into forward slashes */
          /* this could get us into troubles, file could two different files, one with / and one with \ */
//          StringUtils::Replace(strFileName, '\\', '/');

          if (strFileName == pathinrar)
          {
            break;
          }
        }

        archive->SeekToNext();
      }

      head = buffer;
      extract->GetDataIO().SetUnpackToMemory(buffer,0);
      inbuffer = -1;
      fileposition = 0;
      bufferstart = 0;

      extract_thread = new CRarFileExtractThread();
      extract_thread->Start(archive,cmd,extract,iHeaderSize);

      return true;
    }
    catch (int rarErrCode)
    {
      kodi::Log(ADDON_LOG_ERROR,"filerar failed in UnrarXLib while CFileRar::OpenInArchive with an UnrarXLib error code of %d",rarErrCode);
      return false;
    }
    catch (...)
    {
      kodi::Log(ADDON_LOG_ERROR,"filerar failed in UnrarXLib while CFileRar::OpenInArchive with an Unknown exception");
      return false;
    }
  }

  void CleanUp()
  {
    try
    {
      if (extract_thread)
      {
        if (extract_thread->hRunning.Wait(1))
        {
          extract->GetDataIO().hQuit->Broadcast();
          while (extract_thread->hRunning.Wait(1))
            P8PLATFORM::CEvent::Sleep(1);
        }
        delete extract->GetDataIO().hBufferFilled;
        delete extract->GetDataIO().hBufferEmpty;
        delete extract->GetDataIO().hSeek;
        delete extract->GetDataIO().hSeekDone;
        delete extract->GetDataIO().hQuit;
      }
      if (extract)
      {
        delete extract;
        extract = NULL;
      }
      if (archive)
      {
        delete archive;
        archive = NULL;
      }
      if (cmd)
      {
        delete cmd;
        cmd = NULL;
      }
    }
    catch (int rarErrCode)
    {
      kodi::Log(ADDON_LOG_ERROR,"filerar failed in UnrarXLib while deleting CFileRar with an UnrarXLib error code of %d",rarErrCode);
    }
    catch (...)
    {
      kodi::Log(ADDON_LOG_ERROR,"filerar failed in UnrarXLib while deleting CFileRar with an Unknown exception");
    }
  }
};

class CRARFile
  : public kodi::addon::CInstanceVFS
{
public:
  CRARFile(KODI_HANDLE instance) : CInstanceVFS(instance) { }

  virtual void* Open(const VFSURL& url) override;
  virtual ssize_t Read(void* context, void* buffer, size_t uiBufSize) override;
  virtual int64_t Seek(void* context, int64_t position, int whence) override;
  virtual int64_t GetLength(void* context) override;
  virtual int64_t GetPosition(void* context) override;
  virtual int IoControl(void* context, XFILE::EIoControl request, void* param) override;
  virtual int Stat(const VFSURL& url, struct __stat64* buffer) override;
  virtual bool Close(void* context) override;
  virtual bool Exists(const VFSURL& url) override;
  virtual void ClearOutIdle() override;
  virtual void DisconnectAll() override;
  virtual bool DirectoryExists(const VFSURL& url) override;
  virtual bool GetDirectory(const VFSURL& url, std::vector<kodi::vfs::CDirEntry>& items, CVFSCallbacks callbacks) override;
  virtual bool ContainsFiles(const VFSURL& url, std::vector<kodi::vfs::CDirEntry>& items, std::string& rootpath) override;
};


void* CRARFile::Open(const VFSURL& url)
{
  RARContext* result = new RARContext;
  result->Init(url);
  std::vector<kodi::vfs::CDirEntry> items;

  CRarManager::Get().GetFilesInRar(items, result->rarpath, false);
  size_t i;
  for (i=0;i<items.size();++i)
  {
    if (result->pathinrar == items[i].Label())
      break;
  }

  if (i<items.size())
  {
    if (items[i].GetProperties().size() == 1 && atoi(items[i].GetProperties().begin()->second.c_str()) == 0x30)
    {
      if (!result->OpenInArchive())
      {
        delete result;
        return NULL;
      }

      result->size = items[i].Size();

      // perform 'noidx' check
      CFileInfo* pFile = CRarManager::Get().GetFileInRar(result->rarpath, result->pathinrar);
      if (pFile)
      {
        if (pFile->m_iIsSeekable == -1)
        {
          if (Seek(result,-1,SEEK_END) == -1)
          {
            result->seekable = false;
            pFile->m_iIsSeekable = 0;
          }
        }
        else
          result->seekable = (pFile->m_iIsSeekable == 1);
      }
      return result;
    }
    else
    {
      CFileInfo* info = CRarManager::Get().GetFileInRar(result->rarpath, result->pathinrar);
      if ((!info || !kodi::vfs::FileExists(info->m_strCachedPath.c_str(), true)) && result->fileoptions & EXFILE_NOCACHE)
      {
        delete result;
        return NULL;
      }
      std::string strPathInCache;

      if (!CRarManager::Get().CacheRarredFile(strPathInCache, result->rarpath, result->pathinrar,
                                              EXFILE_AUTODELETE | result->fileoptions, result->cachedir,
                                              items[i].Size()))
      {
        kodi::Log(ADDON_LOG_ERROR,"filerar::open failed to cache file %s",result->pathinrar.c_str());
        delete result;
        return NULL;
      }

      result->file = new kodi::vfs::CFile;
      if (!result->file->OpenFile(strPathInCache.c_str(), 0))
      {
        kodi::Log(ADDON_LOG_ERROR,"filerar::open failed to open file in cache: %s",strPathInCache.c_str());
        delete result;
        return NULL;
      }

      return result;
    }
  }

  delete result;
  return NULL;
}

ssize_t CRARFile::Read(void* context, void* lpBuf, size_t uiBufSize)
{
  RARContext* ctx = (RARContext*)context;

  if (ctx->file)
    return ctx->file->Read(lpBuf,uiBufSize);

  if (ctx->fileposition >= GetLength(context)) // we are done
    return 0;

  if( !ctx->extract->GetDataIO().hBufferEmpty->Wait(5000))
  {
    kodi::Log(ADDON_LOG_ERROR, "%s - Timeout waiting for buffer to empty", __FUNCTION__);
    return -1;
  }

  uint8_t* pBuf = (uint8_t*)lpBuf;
  ssize_t uicBufSize = uiBufSize;
  if (ctx->inbuffer > 0)
  {
    ssize_t iCopy = uiBufSize<ctx->inbuffer?uiBufSize:ctx->inbuffer;
    memcpy(lpBuf,ctx->head,size_t(iCopy));
    ctx->head += iCopy;
    ctx->inbuffer -= int(iCopy);
    pBuf += iCopy;
    uicBufSize -= iCopy;
    ctx->fileposition += iCopy;
  }

  while ((uicBufSize > 0) && ctx->fileposition < GetLength(context) )
  {
    if (ctx->inbuffer <= 0)
    {
      ctx->extract->GetDataIO().SetUnpackToMemory(ctx->buffer,MAXWINMEMSIZE);
      ctx->head = ctx->buffer;
      ctx->bufferstart = ctx->fileposition;
    }

    ctx->extract->GetDataIO().hBufferFilled->Signal();
    ctx->extract->GetDataIO().hBufferEmpty->Wait();

    if (ctx->extract->GetDataIO().NextVolumeMissing)
      break;

    ctx->inbuffer = MAXWINMEMSIZE-ctx->extract->GetDataIO().UnpackToMemorySize;

    if (ctx->inbuffer < 0 ||
        ctx->inbuffer > MAXWINMEMSIZE - (ctx->head - ctx->buffer))
    {
      // invalid data returned by UnrarXLib, prevent a crash
      kodi::Log(ADDON_LOG_ERROR, "CRarFile::Read - Data buffer in inconsistent state");
      ctx->inbuffer = 0;
    }

    if (ctx->inbuffer == 0)
      break;

    ssize_t copy = std::min(static_cast<ssize_t>(ctx->inbuffer), uicBufSize);
    memcpy(pBuf, ctx->head, copy);
    ctx->head += copy;
    pBuf += copy;
    ctx->fileposition += copy;
    ctx->inbuffer -= copy;
    uicBufSize -= copy;
  }

  ctx->extract->GetDataIO().hBufferEmpty->Signal();

  return uiBufSize-uicBufSize;
}

bool CRARFile::Close(void* context)
{
  RARContext* ctx = (RARContext*)context;
  if (!ctx)
    return true;

  if (ctx->file)
  {
    delete ctx->file;
    ctx->file = nullptr;
    CRarManager::Get().ClearCachedFile(ctx->rarpath, ctx->pathinrar);
  }
  else
  {
    ctx->CleanUp();
  }
  delete ctx;

  return true;
}

int64_t CRARFile::GetLength(void* context)
{
  RARContext* ctx = (RARContext*)context;

  if (ctx->file)
    return ctx->file->GetLength();

  return ctx->size;
}

//*********************************************************************************************
int64_t CRARFile::GetPosition(void* context)
{
  RARContext* ctx = (RARContext*)context;
  if (ctx->file)
    return ctx->file->GetPosition();

  return ctx->fileposition;
}


int64_t CRARFile::Seek(void* context, int64_t iFilePosition, int iWhence)
{
  RARContext* ctx = (RARContext*)context;
  if (!ctx->seekable)
    return -1;

  if (ctx->file)
    return ctx->file->Seek(iFilePosition, iWhence);

  if( !ctx->extract->GetDataIO().hBufferEmpty->Wait(SEEKTIMOUT) )
  {
    kodi::Log(ADDON_LOG_ERROR, "%s - Timeout waiting for buffer to empty", __FUNCTION__);
    return -1;
  }

  ctx->extract->GetDataIO().hBufferEmpty->Signal();

  switch (iWhence)
  {
    case SEEK_CUR:
      if (iFilePosition == 0)
        return ctx->fileposition; // happens sometimes

      iFilePosition += ctx->fileposition;
      break;
    case SEEK_END:
      if (iFilePosition == 0) // do not seek to end
      {
        ctx->fileposition = GetLength(context);
        ctx->inbuffer = 0;
        ctx->bufferstart = GetLength(context);

        return GetLength(context);
      }

      iFilePosition += GetLength(context);
    case SEEK_SET:
      break;
    default:
      return -1;
  }

  if (iFilePosition > GetLength(context))
    return -1;

  if (iFilePosition == ctx->fileposition) // happens a lot
    return ctx->fileposition;

  if ((iFilePosition >= ctx->bufferstart) && (iFilePosition < ctx->bufferstart+MAXWINMEMSIZE)
                                          && (ctx->inbuffer > 0)) // we are within current buffer
  {
    ctx->inbuffer = MAXWINMEMSIZE-(iFilePosition-ctx->bufferstart);
    ctx->head = ctx->buffer+MAXWINMEMSIZE-ctx->inbuffer;
    ctx->fileposition = iFilePosition;

    return ctx->fileposition;
  }

  if (iFilePosition < ctx->bufferstart)
  {
    ctx->CleanUp();
    if (!ctx->OpenInArchive())
      return -1;

    if( !ctx->extract->GetDataIO().hBufferEmpty->Wait(SEEKTIMOUT) )
    {
      kodi::Log(ADDON_LOG_ERROR, "%s - Timeout waiting for buffer to empty", __FUNCTION__);
      return -1;
    }
    ctx->extract->GetDataIO().hBufferEmpty->Signal();
    ctx->extract->GetDataIO().m_iSeekTo = iFilePosition;
  }
  else
    ctx->extract->GetDataIO().m_iSeekTo = iFilePosition;

  ctx->extract->GetDataIO().SetUnpackToMemory(ctx->buffer,MAXWINMEMSIZE);
  ctx->extract->GetDataIO().hSeek->Signal();
  ctx->extract->GetDataIO().hBufferFilled->Signal();
  if( !ctx->extract->GetDataIO().hSeekDone->Wait(SEEKTIMOUT))
  {
    kodi::Log(ADDON_LOG_ERROR, "%s - Timeout waiting for seek to finish", __FUNCTION__);
    return -1;
  }

  if (ctx->extract->GetDataIO().NextVolumeMissing)
  {
    ctx->fileposition = ctx->size;
    return -1;
  }

  if( !ctx->extract->GetDataIO().hBufferEmpty->Wait(SEEKTIMOUT) )
  {
    kodi::Log(ADDON_LOG_ERROR, "%s - Timeout waiting for buffer to empty", __FUNCTION__);
    return -1;
  }
  ctx->inbuffer = ctx->extract->GetDataIO().m_iSeekTo; // keep data
  ctx->bufferstart = ctx->extract->GetDataIO().m_iStartOfBuffer;

  if (ctx->inbuffer < 0 || ctx->inbuffer > MAXWINMEMSIZE)
  {
    // invalid data returned by UnrarXLib, prevent a crash
    kodi::Log(ADDON_LOG_ERROR, "CRarFile::Seek - Data buffer in inconsistent state");
    ctx->inbuffer = 0;
    return -1;
  }

  ctx->head = ctx->buffer+MAXWINMEMSIZE-ctx->inbuffer;
  ctx->fileposition = iFilePosition;

  return ctx->fileposition;
}

bool CRARFile::Exists(const VFSURL& url)
{
  RARContext ctx;
  ctx.Init(url);

  // First step:
  // Make sure that the archive exists in the filesystem.
  if (!kodi::vfs::FileExists(ctx.rarpath.c_str(), false)) 
    return false;

  // Second step:
  // Make sure that the requested file exists in the archive.
  bool bResult;

  if (!CRarManager::Get().IsFileInRar(bResult, ctx.rarpath, ctx.pathinrar))
    return false;

  return bResult;
}

int CRARFile::Stat(const VFSURL& url, struct __stat64* buffer)
{
  memset(buffer, 0, sizeof(struct __stat64));
  RARContext* ctx = (RARContext*)Open(url);
  if (ctx)
  {
    buffer->st_size = ctx->size;
    buffer->st_mode = S_IFREG;
    Close(ctx);
    errno = 0;
    return 0;
  }

  Close(ctx);
  if (DirectoryExists(url))
  {
    buffer->st_mode = S_IFDIR;
    return 0;
  }

  errno = ENOENT;
  return -1;
}

int CRARFile::IoControl(void* context, XFILE::EIoControl request, void* param)
{
  RARContext* ctx = (RARContext*)context;

  if(request == XFILE::IOCTRL_SEEK_POSSIBLE)
    return ctx->seekable?1:0;

  return -1;
}

void CRARFile::ClearOutIdle()
{
}

void CRARFile::DisconnectAll()
{
  CRarManager::Get().ClearCache(true);
}

bool CRARFile::DirectoryExists(const VFSURL& url)
{
  std::vector<kodi::vfs::CDirEntry> items;
  return GetDirectory(url, items, nullptr);
}

bool CRARFile::GetDirectory(const VFSURL& url, std::vector<kodi::vfs::CDirEntry>& items, CVFSCallbacks callbacks)
{
  std::string strPath(url.url);
  size_t pos;
  if ((pos=strPath.find("?")) != std::string::npos)
    strPath.erase(strPath.begin()+pos, strPath.end());

  // the RAR code depends on things having a "\" at the end of the path
  if (strPath[strPath.size()-1] != '/')
    strPath += '/';

  std::string strArchive = url.hostname;
  std::string strOptions = url.options;
  std::string strPathInArchive = url.filename;

  if (CRarManager::Get().GetFilesInRar(items, strArchive, true, strPathInArchive))
  {
    // fill in paths
    for (auto& entry : items)
    {
      std::stringstream str;
      str << strPath << entry.Path() << url.options;
      entry.SetPath(str.str());
    }
    return true;
  }
  else
  {
    kodi::Log(ADDON_LOG_ERROR,"%s: rar lib returned no files in archive %s, likely corrupt",__FUNCTION__,strArchive.c_str());
    return false;
  }
}

bool CRARFile::ContainsFiles(const VFSURL& url, std::vector<kodi::vfs::CDirEntry>& items, std::string& rootpath)
{
  const char* sub;
  if ((sub=strstr(url.filename, ".part")))
  {
    if (url.filename+strlen(url.filename)-sub > 6)
    {
      if (*(sub+5) == '0')
      {
        if (!((*(sub+5) == '0'  && *(sub+6) == '1') ||  // .part0x
              (*(sub+6) == '0' && *(sub + 7) == '1'))) //  .part00x
        {
          return false;
        }
      }
      else if (*(sub+6) == '.')
      {
        if (*(sub+5) != '1')
        {
          return false;
        }
      }
    }
  }

  if (CRarManager::Get().GetFilesInRar(items, url.url))
  {
    if (items[0].GetProperties().size() == 1 && atoi(items[0].GetProperties().begin()->second.c_str()) != 0x30)
      return false;

    // fill in paths
    std::string strPath(url.url);
    size_t pos;
    if ((pos=strPath.find("?")) != std::string::npos)
      strPath.erase(strPath.begin()+pos, strPath.end());

    if (strPath[strPath.size()-1] == '/')
      strPath.erase(strPath.end()-1);
    std::string encoded = URLEncode(strPath);
    std::stringstream str;
    str << "rar://" << encoded << "/";
    rootpath = str.str();
    for (auto& entry : items)
    {
      std::stringstream str;
      str << "rar://" << encoded << "/" << entry.Path() << url.options;
      entry.SetPath(str.str());
    }
  }

  return !items.empty();
}


class CMyAddon : public kodi::addon::CAddonBase
{
public:
  CMyAddon() { }
  virtual ADDON_STATUS CreateInstance(int instanceType, std::string instanceID, KODI_HANDLE instance, KODI_HANDLE& addonInstance) override
  {
    addonInstance = new CRARFile(instance);
    return ADDON_STATUS_OK;
  }
};

ADDONCREATOR(CMyAddon);
