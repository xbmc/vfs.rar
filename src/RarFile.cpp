/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "RarFile.h"
#include "RarExtractThread.h"
#include "RarManager.h"
#include "RarPassword.h"
#include "Helpers.h"

#include "rar.hpp"

#include <kodi/General.h>
#include <kodi/gui/dialogs/Keyboard.h>
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
#include <locale>
#include <regex>

#define SEEKTIMOUT 30000

static std::string URLEncode(const std::string& strURLData)
{
  std::string strResult;

  /* wonder what a good value is here is, depends on how often it occurs */
  strResult.reserve( strURLData.length() * 2 );

  for (size_t i = 0; i < strURLData.size(); ++i)
  {
    const unsigned char kar = strURLData[i];

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

void* CRARFile::Open(const VFSURL& url)
{
  RARContext* result = new RARContext(url);

  std::vector<kodi::vfs::CDirEntry> items;
  CRarManager::Get().GetFilesInRar(items, result->GetPath(), false);

  size_t i;
  for (i = 0; i < items.size(); ++i)
  {
    if (result->m_pathinrar == items[i].Label())
      break;
  }

  if (i < items.size())
  {
    if (items[i].GetProperties().size() == 1 && atoi(items[i].GetProperties().begin()->second.c_str()) == 0x30)
    {
      if (!result->OpenInArchive())
      {
        delete result;
        return nullptr;
      }

      result->m_size = items[i].Size();

      // perform 'noidx' check
      CFileInfo* pFile = CRarManager::Get().GetFileInRar(result->GetPath(), result->m_pathinrar);
      if (pFile)
      {
        if (pFile->m_iIsSeekable == -1)
        {
          if (Seek(result, -1, SEEK_END) == -1)
          {
            result->m_seekable = false;
            pFile->m_iIsSeekable = 0;
          }
        }
        else
          result->m_seekable = (pFile->m_iIsSeekable == 1);
      }
      return result;
    }
    else
    {
      CFileInfo* info = CRarManager::Get().GetFileInRar(result->GetPath(), result->m_pathinrar);
      if ((!info || !kodi::vfs::FileExists(info->m_strCachedPath, true)) && result->m_fileoptions & EXFILE_NOCACHE)
      {
        delete result;
        return nullptr;
      }
      std::string strPathInCache;

      if (!CRarManager::Get().CacheRarredFile(strPathInCache, result->GetPath(), result->m_pathinrar,
                                              EXFILE_AUTODELETE | result->m_fileoptions, result->m_cachedir,
                                              items[i].Size()))
      {
        kodiLog(ADDON_LOG_ERROR,"CRarFile::%s: Open failed to cache file %s", __func__, result->m_pathinrar.c_str());
        delete result;
        return nullptr;
      }

      result->m_file = new kodi::vfs::CFile;
      if (!result->m_file->OpenFile(strPathInCache, 0))
      {
        kodiLog(ADDON_LOG_ERROR,"CRarFile::%s: Open failed to open file in cache: %s", __func__, strPathInCache.c_str());
        delete result;
        return nullptr;
      }

      return result;
    }
  }

  delete result;
  return nullptr;
}

ssize_t CRARFile::Read(void* context, void* lpBuf, size_t uiBufSize)
{
  RARContext* ctx = static_cast<RARContext*>(context);

  if (ctx->m_file)
    return ctx->m_file->Read(lpBuf, uiBufSize);

  if (ctx->m_fileposition >= ctx->m_size) // we are done
  {
    kodiLog(ADDON_LOG_DEBUG, "CRarFile::%s: Read reached end of file", __func__);
    return 0;
  }

  if( !ctx->m_extract.GetDataIO().hBufferEmpty->Wait(5000))
  {
    kodiLog(ADDON_LOG_ERROR, "CRarFile::%s: Timeout waiting for buffer to empty", __func__);
    return -1;
  }

  size_t bufferSize = File::CopyBufferSize();
  uint8_t* pBuf = static_cast<uint8_t*>(lpBuf);
  ssize_t uicBufSize = uiBufSize;
  if (ctx->m_inbuffer > 0)
  {
    ssize_t copy = std::min(static_cast<ssize_t>(ctx->m_inbuffer), uicBufSize);
    if (copy + ctx->m_fileposition >= ctx->m_size)
      copy = ctx->m_size - ctx->m_fileposition;
    memcpy(lpBuf, ctx->m_head, size_t(copy));
    ctx->m_head += copy;
    ctx->m_inbuffer -= copy;
    ctx->m_fileposition += copy;
    pBuf += copy;
    uicBufSize -= copy;
  }

  while ((uicBufSize > 0) && ctx->m_fileposition < ctx->m_size)
  {
    if (ctx->m_inbuffer <= 0)
    {
      ctx->m_extract.GetDataIO().SetUnpackToMemory(ctx->m_buffer, bufferSize);
      ctx->m_head = ctx->m_buffer;
      ctx->m_bufferstart = ctx->m_fileposition;
    }

    ctx->m_extract.GetDataIO().hBufferFilled->Signal();
    ctx->m_extract.GetDataIO().hBufferEmpty->Wait();

    if (ctx->m_extract.GetDataIO().NextVolumeMissing)
      break;

    ctx->m_inbuffer = bufferSize - ctx->m_extract.GetDataIO().UnpackToMemorySize;

    if (ctx->m_inbuffer < 0 ||
        ctx->m_inbuffer > bufferSize - (ctx->m_head - ctx->m_buffer))
    {
      // invalid data returned by UnrarXLib, prevent a crash
      kodiLog(ADDON_LOG_ERROR, "CRarFile::%s: Data buffer in inconsistent state", __func__);
      ctx->m_inbuffer = 0;
    }

    if (ctx->m_inbuffer == 0)
      break;

    ssize_t copy = std::min(static_cast<ssize_t>(ctx->m_inbuffer), uicBufSize);
    if (copy + ctx->m_fileposition >= ctx->m_size)
      copy = ctx->m_size - ctx->m_fileposition;
    memcpy(pBuf, ctx->m_head, copy);
    ctx->m_head += copy;
    ctx->m_inbuffer -= copy;
    ctx->m_fileposition += copy;
    pBuf += copy;
    uicBufSize -= copy;
  }

  ctx->m_extract.GetDataIO().hBufferEmpty->Signal();

  return uiBufSize-uicBufSize;
}

bool CRARFile::Close(void* context)
{
  RARContext* ctx = static_cast<RARContext*>(context);
  if (!ctx)
    return true;

  if (ctx->m_file)
  {
    delete ctx->m_file;
    ctx->m_file = nullptr;
    CRarManager::Get().ClearCachedFile(ctx->GetPath(), ctx->m_pathinrar);
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
  RARContext* ctx = static_cast<RARContext*>(context);

  if (ctx->m_file)
    return ctx->m_file->GetLength();

  return ctx->m_size;
}

//*********************************************************************************************
int64_t CRARFile::GetPosition(void* context)
{
  RARContext* ctx = static_cast<RARContext*>(context);

  if (ctx->m_file)
    return ctx->m_file->GetPosition();

  return ctx->m_fileposition;
}


int64_t CRARFile::Seek(void* context, int64_t iFilePosition, int iWhence)
{
  RARContext* ctx = static_cast<RARContext*>(context);

  kodiLog(ADDON_LOG_DEBUG, "CRarFile::%s: Started seek to position %li with whence %i", __func__, iFilePosition, iWhence);

  if (!ctx->m_seekable)
  {
    kodiLog(ADDON_LOG_DEBUG, "CRarFile::%s: Seek not supported", __func__);
    return -1;
  }

  if (ctx->m_file)
    return ctx->m_file->Seek(iFilePosition, iWhence);

  if (!ctx->m_extract.GetDataIO().hBufferEmpty->Wait(SEEKTIMOUT))
  {
    kodiLog(ADDON_LOG_ERROR, "CRarFile::%s: Timeout waiting for buffer to empty", __func__);
    return -1;
  }

  size_t bufferSize = File::CopyBufferSize();

  ctx->m_extract.GetDataIO().hBufferEmpty->Signal();

  switch (iWhence)
  {
    case SEEK_CUR:
      if (iFilePosition == 0)
        return ctx->m_fileposition; // happens sometimes

      iFilePosition += ctx->m_fileposition;
      break;
    case SEEK_END:
      if (iFilePosition == 0) // do not seek to end
      {
        ctx->m_fileposition = ctx->m_size;
        ctx->m_inbuffer = 0;
        ctx->m_bufferstart = ctx->m_size;

        kodiLog(ADDON_LOG_DEBUG, "CRarFile::%s: Seek to end size %li", __func__, iFilePosition, ctx->m_size);
        return ctx->m_size;
      }

      iFilePosition += ctx->m_size;
    case SEEK_SET:
      break;
    default:
      kodiLog(ADDON_LOG_ERROR, "CRarFile::%s: Not maintened seek whence called: %i", __func__, iWhence);
      return -1;
  }

  if (iFilePosition > ctx->m_size)
  {
    kodiLog(ADDON_LOG_DEBUG, "CRarFile::%s: Seek position %li higher as file position %li", __func__, iFilePosition, ctx->m_size);
    return -1;
  }

  if (iFilePosition == ctx->m_fileposition) // happens a lot
    return ctx->m_fileposition;

  // In case of encryption we need to align read size to encryption
  // block size. We can do it by simple masking, because unpack read code
  // always reads more than CRYPT_BLOCK_SIZE, so we do not risk to make it 0.
  int64_t iFilePositionRest = 0;
  bool decryption = ctx->m_extract.GetDataIO().Decryption;
  if (decryption)
  {
    iFilePositionRest = iFilePosition & CRYPT_BLOCK_MASK;
    iFilePosition &= ~CRYPT_BLOCK_MASK;

    kodiLog(ADDON_LOG_DEBUG, "CRarFile::%s: Seek on enrypted package with corrected size to %li and rest process with %li", __func__, iFilePosition, iFilePositionRest);
  }

  if ((iFilePosition >= ctx->m_bufferstart) && (iFilePosition < ctx->m_bufferstart+bufferSize)
                                            && (ctx->m_inbuffer > 0)) // we are within current buffer
  {
    ctx->m_inbuffer = bufferSize-(iFilePosition - ctx->m_bufferstart);
    ctx->m_head = ctx->m_buffer + bufferSize - ctx->m_inbuffer;
    ctx->m_fileposition = iFilePosition;

    kodiLog(ADDON_LOG_DEBUG, "CRarFile::%s: Seek by buffered file position to %li", __func__, iFilePosition, ctx->m_fileposition);
    return ctx->m_fileposition;
  }

  if (iFilePosition < ctx->m_bufferstart)
  {
    ctx->CleanUp();
    if (!ctx->OpenInArchive())
    {
      kodiLog(ADDON_LOG_ERROR, "CRarFile::%s: Failed to call OpenInArchive", __func__);
      return -1;
    }

    if( !ctx->m_extract.GetDataIO().hBufferEmpty->Wait(SEEKTIMOUT) )
    {
      kodiLog(ADDON_LOG_ERROR, "CRarFile::%s: Timeout waiting for buffer to empty", __func__);
      return -1;
    }

    ctx->m_extract.GetDataIO().hBufferEmpty->Signal();
    ctx->m_extract.GetDataIO().m_iSeekTo = iFilePosition;
  }
  else
    ctx->m_extract.GetDataIO().m_iSeekTo = iFilePosition;

  ctx->m_extract.GetDataIO().SetUnpackToMemory(ctx->m_buffer, bufferSize);
  ctx->m_extract.GetDataIO().hSeek->Signal();
  ctx->m_extract.GetDataIO().hBufferFilled->Signal();
  if( !ctx->m_extract.GetDataIO().hSeekDone->Wait(SEEKTIMOUT))
  {
    kodiLog(ADDON_LOG_ERROR, "CRarFile::%s: Timeout waiting for seek to finish", __func__);
    return -1;
  }

  if (ctx->m_extract.GetDataIO().NextVolumeMissing)
  {
    ctx->m_fileposition = ctx->m_size;
    kodiLog(ADDON_LOG_ERROR, "CRarFile::%s: Next RAR volume is missing", __func__);
    return -1;
  }

  if( !ctx->m_extract.GetDataIO().hBufferEmpty->Wait(SEEKTIMOUT) )
  {
    kodiLog(ADDON_LOG_ERROR, "CRarFile::%s: Timeout waiting for buffer to empty", __func__);
    return -1;
  }
  ctx->m_inbuffer = ctx->m_extract.GetDataIO().m_iSeekTo; // keep data
  ctx->m_bufferstart = ctx->m_extract.GetDataIO().m_iStartOfBuffer;

  if (ctx->m_inbuffer < 0 || ctx->m_inbuffer > bufferSize)
  {
    // invalid data returned by UnrarXLib, prevent a crash
    kodiLog(ADDON_LOG_ERROR, "CRarFile::%s: - Data buffer in inconsistent state", __func__);
    ctx->m_inbuffer = 0;
    return -1;
  }

  ctx->m_head = ctx->m_buffer + bufferSize - ctx->m_inbuffer;
  ctx->m_fileposition = iFilePosition;

  // Process the the rest outside of encrypted block place by dummy read, to
  // become correct seek position.
  if (decryption && iFilePositionRest > 0)
  {
    uint8_t* lpBuf = new uint8_t[iFilePositionRest+1];
    Read(context, lpBuf, iFilePositionRest);
    delete[] lpBuf;
  }

  kodiLog(ADDON_LOG_DEBUG, "CRarFile::%s: Seek completed to file position %li", __func__, ctx->m_fileposition);
  return ctx->m_fileposition;
}

bool CRARFile::Exists(const VFSURL& url)
{
  RARContext ctx(url);

  // First step:
  // Make sure that the archive exists in the filesystem.
  if (!kodi::vfs::FileExists(ctx.GetPath(), false))
    return false;

  // Second step:
  // Make sure that the requested file exists in the archive.
  bool bResult;

  if (!CRarManager::Get().IsFileInRar(bResult, ctx.GetPath(), ctx.m_pathinrar))
    return false;

  return bResult;
}

int CRARFile::Stat(const VFSURL& url, struct __stat64* buffer)
{
  memset(buffer, 0, sizeof(struct __stat64));
  RARContext* ctx = static_cast<RARContext*>(Open(url));
  if (ctx)
  {
    buffer->st_size = ctx->m_size;
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
  RARContext* ctx = static_cast<RARContext*>(context);

  if(request == XFILE::IOCTRL_SEEK_POSSIBLE)
    return ctx->m_seekable ? 1 : 0;

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
  CVFSCallbacks callbacks(nullptr);
  return GetDirectory(url, items, callbacks);
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
    kodiLog(ADDON_LOG_ERROR,"CRarFile::%s: rar lib returned no files in archive %s, likely corrupt", __func__, strArchive.c_str());
    return false;
  }
}

bool CRARFile::ContainsFiles(const VFSURL& url, std::vector<kodi::vfs::CDirEntry>& items, std::string& rootpath)
{
  // only list .part1.rar
  std::string fname(url.filename);
  size_t spos = fname.rfind('/');
  if (spos == std::string::npos)
    spos = fname.rfind('\\');
  fname.erase(0, spos);
  std::regex part_re("\\.part([0-9]+)\\.rar$");
  std::smatch match;
  if (std::regex_search(fname, match, part_re))
  {
    if (std::stoul(match[1].str()) != 1)
      return false;
  }

  if (CRarManager::Get().GetFilesInRar(items, url.url))
  {
    if (items.size() == 1 && items[0].GetProperties().size() == 1 &&
        atoi(items[0].GetProperties().begin()->second.c_str()) < 0x30 &&
        atoi(items[0].GetProperties().begin()->second.c_str()) > 0x35)
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


class ATTRIBUTE_HIDDEN CMyAddon : public kodi::addon::CAddonBase
{
public:
  CMyAddon() = default;
  ~CMyAddon()
  {
    CRARPasswordControl::CleanupPasswordList();
  }

  ADDON_STATUS CreateInstance(int instanceType, std::string instanceID, KODI_HANDLE instance, KODI_HANDLE& addonInstance) override
  {
    addonInstance = new CRARFile(instance);
    return ADDON_STATUS_OK;
  }
};

ADDONCREATOR(CMyAddon);
