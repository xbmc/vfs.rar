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

#include "libXBMC_addon.h"
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

ADDON::CHelper_libXBMC_addon *XBMC           = NULL;

extern "C" {

#include "kodi_vfs_dll.h"
#include "IFileTypes.h"

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

//-- Create -------------------------------------------------------------------
// Called on load. Addon should fully initalize or return error status
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_Create(void* hdl, void* props)
{
  if (!XBMC)
    XBMC = new ADDON::CHelper_libXBMC_addon;

  if (!XBMC->RegisterMe(hdl))
  {
    delete XBMC, XBMC=NULL;
    return ADDON_STATUS_PERMANENT_FAILURE;
  }

  return ADDON_STATUS_OK;
}

//-- Stop ---------------------------------------------------------------------
// This dll must cease all runtime activities
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
void ADDON_Stop()
{
}

//-- Destroy ------------------------------------------------------------------
// Do everything before unload of this add-on
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
void ADDON_Destroy()
{
  XBMC=NULL;
}

//-- HasSettings --------------------------------------------------------------
// Returns true if this add-on use settings
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
bool ADDON_HasSettings()
{
  return false;
}

//-- GetStatus ---------------------------------------------------------------
// Returns the current Status of this visualisation
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_GetStatus()
{
  return ADDON_STATUS_OK;
}

//-- GetSettings --------------------------------------------------------------
// Return the settings for XBMC to display
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
unsigned int ADDON_GetSettings(ADDON_StructSetting ***sSet)
{
  return 0;
}

//-- FreeSettings --------------------------------------------------------------
// Free the settings struct passed from XBMC
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------

void ADDON_FreeSettings()
{
}

//-- SetSetting ---------------------------------------------------------------
// Set a specific Setting value (called from XBMC)
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_SetSetting(const char *strSetting, const void* value)
{
  return ADDON_STATUS_OK;
}

//-- Announce -----------------------------------------------------------------
// Receive announcements from XBMC
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
void ADDON_Announce(const char *flag, const char *sender, const char *message, const void *data)
{
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
  void* file;
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

  void Init(VFSURL* url)
  {
    cachedir = "special://temp/";
    rarpath = url->hostname;
    password = url->password;
    pathinrar = url->filename;
    std::vector<std::string> options;
    std::string options2(url->options);
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
            char* tnew = XBMC->UnknownToUTF8(archive->NewLhd.FileName);
            strFileName = tnew;
            XBMC->FreeString(tnew);
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
      XBMC->Log(ADDON::LOG_ERROR,"filerar failed in UnrarXLib while CFileRar::OpenInArchive with an UnrarXLib error code of %d",rarErrCode);
      return false;
    }
    catch (...)
    {
      XBMC->Log(ADDON::LOG_ERROR,"filerar failed in UnrarXLib while CFileRar::OpenInArchive with an Unknown exception");
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
      XBMC->Log(ADDON::LOG_ERROR,"filerar failed in UnrarXLib while deleting CFileRar with an UnrarXLib error code of %d",rarErrCode);
    }
    catch (...)
    {
      XBMC->Log(ADDON::LOG_ERROR,"filerar failed in UnrarXLib while deleting CFileRar with an Unknown exception");
    }
  }
};

void* Open(VFSURL* url)
{
  RARContext* result = new RARContext;
  result->Init(url);
  std::vector<VFSDirEntry>* itms = new std::vector<VFSDirEntry>();
  std::vector<VFSDirEntry>& items = *itms;
  CRarManager::Get().GetFilesInRar(items, result->rarpath, false);
  size_t i;
  for (i=0;i<items.size();++i)
  {
    if (result->pathinrar == items[i].label)
      break;
  }

  if (i<items.size())
  {
    if (items[i].num_props > 0 && atoi(items[i].properties->val) == 0x30) // stored
    {
      if (!result->OpenInArchive())
      {
        delete result;
        FreeDirectory(itms);
        return NULL;
      }

      result->size = items[i].size;

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
      FreeDirectory(itms);
      return result;
    }
    else
    {
      CFileInfo* info = CRarManager::Get().GetFileInRar(result->rarpath, result->pathinrar);
      if ((!info || !XBMC->FileExists(info->m_strCachedPath.c_str(), true)) && result->fileoptions & EXFILE_NOCACHE)
      {
        FreeDirectory(itms);
        delete result;
        return NULL;
      }
      std::string strPathInCache;

      if (!CRarManager::Get().CacheRarredFile(strPathInCache, result->rarpath, result->pathinrar,
                                              EXFILE_AUTODELETE | result->fileoptions, result->cachedir,
                                              items[i].size))
      {
        XBMC->Log(ADDON::LOG_ERROR,"filerar::open failed to cache file %s",result->pathinrar.c_str());
        FreeDirectory(itms);
        delete result;
        return NULL;
      }

      if (!(result->file=XBMC->OpenFile(strPathInCache.c_str(), 0)))
      {
        XBMC->Log(ADDON::LOG_ERROR,"filerar::open failed to open file in cache: %s",strPathInCache.c_str());
        FreeDirectory(itms);
        delete result;
        return NULL;
      }

      FreeDirectory(itms);
      return result;
    }
  }

  FreeDirectory(itms);
  delete result;
  return NULL;
}

ssize_t Read(void* context, void* lpBuf, size_t uiBufSize)
{
  RARContext* ctx = (RARContext*)context;

  if (ctx->file)
    return XBMC->ReadFile(ctx->file,lpBuf,uiBufSize);

  if (ctx->fileposition >= GetLength(context)) // we are done
    return 0;

  if( !ctx->extract->GetDataIO().hBufferEmpty->Wait(5000))
  {
    XBMC->Log(ADDON::LOG_ERROR, "%s - Timeout waiting for buffer to empty", __FUNCTION__);
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
      XBMC->Log(ADDON::LOG_ERROR, "CRarFile::Read - Data buffer in inconsistent state");
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

bool Close(void* context)
{
  RARContext* ctx = (RARContext*)context;
  if (!ctx)
    return true;

  if (ctx->file)
  {
    XBMC->CloseFile(ctx->file);
    CRarManager::Get().ClearCachedFile(ctx->rarpath, ctx->pathinrar);
  }
  else
  {
    ctx->CleanUp();
  }
  delete ctx;

  return true;
}

int64_t GetLength(void* context)
{
  RARContext* ctx = (RARContext*)context;

  if (ctx->file)
    return XBMC->GetFileLength(ctx->file);

  return ctx->size;
}

//*********************************************************************************************
int64_t GetPosition(void* context)
{
  RARContext* ctx = (RARContext*)context;
  if (ctx->file)
    return XBMC->GetFilePosition(ctx->file);

  return ctx->fileposition;
}


int64_t Seek(void* context, int64_t iFilePosition, int iWhence)
{
  RARContext* ctx = (RARContext*)context;
  if (!ctx->seekable)
    return -1;

  if (ctx->file)
    return XBMC->SeekFile(ctx->file, iFilePosition, iWhence);

  if( !ctx->extract->GetDataIO().hBufferEmpty->Wait(SEEKTIMOUT) )
  {
    XBMC->Log(ADDON::LOG_ERROR, "%s - Timeout waiting for buffer to empty", __FUNCTION__);
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
      XBMC->Log(ADDON::LOG_ERROR, "%s - Timeout waiting for buffer to empty", __FUNCTION__);
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
    XBMC->Log(ADDON::LOG_ERROR, "%s - Timeout waiting for seek to finish", __FUNCTION__);
    return -1;
  }

  if (ctx->extract->GetDataIO().NextVolumeMissing)
  {
    ctx->fileposition = ctx->size;
    return -1;
  }

  if( !ctx->extract->GetDataIO().hBufferEmpty->Wait(SEEKTIMOUT) )
  {
    XBMC->Log(ADDON::LOG_ERROR, "%s - Timeout waiting for buffer to empty", __FUNCTION__);
    return -1;
  }
  ctx->inbuffer = ctx->extract->GetDataIO().m_iSeekTo; // keep data
  ctx->bufferstart = ctx->extract->GetDataIO().m_iStartOfBuffer;

  if (ctx->inbuffer < 0 || ctx->inbuffer > MAXWINMEMSIZE)
  {
    // invalid data returned by UnrarXLib, prevent a crash
    XBMC->Log(ADDON::LOG_ERROR, "CRarFile::Seek - Data buffer in inconsistent state");
    ctx->inbuffer = 0;
    return -1;
  }

  ctx->head = ctx->buffer+MAXWINMEMSIZE-ctx->inbuffer;
  ctx->fileposition = iFilePosition;

  return ctx->fileposition;
}

bool Exists(VFSURL* url)
{
  RARContext ctx;
  ctx.Init(url);
  
  // First step:
  // Make sure that the archive exists in the filesystem.
  if (!XBMC->FileExists(ctx.rarpath.c_str(), false)) 
    return false;

  // Second step:
  // Make sure that the requested file exists in the archive.
  bool bResult;

  if (!CRarManager::Get().IsFileInRar(bResult, ctx.rarpath, ctx.pathinrar))
    return false;

  return bResult;
}

int Stat(VFSURL* url, struct __stat64* buffer)
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

int IoControl(void* context, XFILE::EIoControl request, void* param)
{
  RARContext* ctx = (RARContext*)context;

  if(request == XFILE::IOCTRL_SEEK_POSSIBLE)
    return ctx->seekable?1:0;

  return -1;
}

void ClearOutIdle()
{
}

void DisconnectAll()
{
  CRarManager::Get().ClearCache(true);
}

bool DirectoryExists(VFSURL* url)
{
  VFSDirEntry* dir;
  int num_items;
  void* ctx = GetDirectory(url, &dir, &num_items, NULL);
  if (ctx)
  {
    FreeDirectory(ctx);
    return true;
  }

  return false;
}

void* GetDirectory(VFSURL* url, VFSDirEntry** items,
                   int* num_items, VFSCallbacks* callbacks)
{
  std::string strPath(url->url);
  size_t pos;
  if ((pos=strPath.find("?")) != std::string::npos)
    strPath.erase(strPath.begin()+pos, strPath.end());

  // the RAR code depends on things having a "\" at the end of the path
  if (strPath[strPath.size()-1] != '/')
    strPath += '/';

  std::string strArchive = url->hostname;
  std::string strOptions = url->options;
  std::string strPathInArchive = url->filename;

  std::vector<VFSDirEntry>* itms = new std::vector<VFSDirEntry>;
  if (CRarManager::Get().GetFilesInRar(*itms,strArchive,true,strPathInArchive))
  {
    // fill in paths
    for (size_t iEntry=0;iEntry<itms->size();++iEntry)
    {
      std::stringstream str;
      str << strPath << (*itms)[iEntry].path << url->options;
      char* tofree = (*itms)[iEntry].path;
      (*itms)[iEntry].path = strdup(str.str().c_str());
      free(tofree);
      (*itms)[iEntry].title = NULL;
    }
    *items = &(*itms)[0];
    *num_items = itms->size();

    return itms;
  }
  else
  {
    XBMC->Log(ADDON::LOG_ERROR,"%s: rar lib returned no files in archive %s, likely corrupt",__FUNCTION__,strArchive.c_str());
    return NULL;
  }
}

void FreeDirectory(void* items)
{
  std::vector<VFSDirEntry>& ctx = *(std::vector<VFSDirEntry>*)items;
  for (size_t i=0;i<ctx.size();++i)
  {
    free(ctx[i].label);
    for (size_t j=0;j<ctx[i].num_props;++j)
    {
      free(ctx[i].properties[j].name);
      free(ctx[i].properties[j].val);
    }
    delete ctx[i].properties;
    free(ctx[i].path);
  }
  delete &ctx;
}

bool CreateDirectory(VFSURL* url)
{
  return false;
}

bool RemoveDirectory(VFSURL* url)
{
  return false;
}

int Truncate(void* context, int64_t size)
{
  return -1;
}

ssize_t Write(void* context, const void* lpBuf, size_t uiBufSize)
{
  return -1;
}

bool Delete(VFSURL* url)
{
  return false;
}

bool Rename(VFSURL* url, VFSURL* url2)
{
  return false;
}

void* OpenForWrite(VFSURL* url, bool bOverWrite)
{ 
  return NULL;
}

void* ContainsFiles(VFSURL* url, VFSDirEntry** items, int* num_items, char* rootpath)
{
  const char* sub;
  if ((sub=strstr(url->filename, ".part")))
  {
    if (url->filename+strlen(url->filename)-sub > 6)
    {
      if (*(sub+5) == '0')
      {
        if (!((*(sub+5) == '0'  && *(sub+6) == '1') ||  // .part0x
              (*(sub+6) == '0' && *(sub + 7) == '1'))) //  .part00x
          return nullptr;
      }
      else if (*(sub+6) == '.')
      {
        if (*(sub+5) != '1')
          return nullptr;
      }
    }
  }
  std::vector<VFSDirEntry>* itms = new std::vector<VFSDirEntry>();
  if (CRarManager::Get().GetFilesInRar(*itms, url->url))
  {
    if (itms->size() == 1 && atoi((*itms)[0].properties->val) != 0x30)
    {
      delete itms;
      return NULL;
    }
    // fill in paths
    std::string strPath(url->url);
    size_t pos;
    if ((pos=strPath.find("?")) != std::string::npos)
      strPath.erase(strPath.begin()+pos, strPath.end());

    if (strPath[strPath.size()-1] == '/')
      strPath.erase(strPath.end()-1);
    std::string encoded = URLEncode(strPath);
    std::stringstream str;
    str << "rar://" << encoded << "/";
    strcpy(rootpath, str.str().c_str());
    for (size_t iEntry=0;iEntry<itms->size();++iEntry)
    {
      char* tofree = (*itms)[iEntry].path;
      std::stringstream str;
      str << "rar://" << encoded << "/" << (*itms)[iEntry].path << url->options;
      (*itms)[iEntry].path = strdup(str.str().c_str());
      (*itms)[iEntry].title = NULL;
      free(tofree);
    }
    *items = &(*itms)[0];
    *num_items = itms->size();
    return itms;
  }
  delete itms;
  return NULL;
}

int GetStartTime(void* ctx)
{
  return 0;
}

int GetTotalTime(void* ctx)
{
  return 0;
}

bool NextChannel(void* context, bool preview)
{
  return false;
}

bool PrevChannel(void* context, bool preview)
{
  return false;
}

bool SelectChannel(void* context, unsigned int uiChannel)
{
  return false;
}

bool UpdateItem(void* context)
{
  return false;
}

int GetChunkSize(void* context)
{
  return 0;
}

}
