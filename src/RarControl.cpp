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

#include "RarControl.h"
#include "RarExtractThread.h"
#include "RarManager.h"
#include "RarPassword.h"
#include "Helpers.h"

#include <kodi/General.h>
#include <kodi/gui/dialogs/Keyboard.h>
#include <thread>
#include <tinyxml.h>

#define STOP_PROCESSING -1
#define CONTINUE_PROCESSING 1
#define SUCCESS 0

// Amount of standard passwords where can available on settings
#define MAX_STANDARD_PASSWORDS 5

CRARControl::CRARControl(const std::string& rarPath)
  : m_path(rarPath)
{
  SetCallback(reinterpret_cast<UNRARCALLBACK>(UnRarCallback), reinterpret_cast<LPARAM>(this));
  m_passwordAskAllowed = kodi::GetSettingBoolean("usercheck_for_password");
}

void CRARControl::SetCallback(UNRARCALLBACK callback, LPARAM userdata)
{
  m_callback = callback;
  m_userData = userdata;
}

bool CRARControl::ArchiveList(std::vector<RARHeaderDataEx>& list)
{
  if (!kodi::vfs::FileExists(m_path))
  {
    kodiLog(ADDON_LOG_DEBUG, "CRARControl::%s: Request file %s not present", __func__, m_path.c_str());
    return false;
  }

  bool needPassword = false;
  bool firstTry = true;
  bool ret = false;

  m_passwordStandardCheck = 0;

  while (firstTry || (needPassword && m_passwordStandardCheck < MAX_STANDARD_PASSWORDS))
  {
    RAROpenArchiveDataEx archiveData = {0};
    archiveData.OpenMode = RAR_OM_LIST;
    archiveData.ArcName = const_cast<char*>(m_path.c_str());
    archiveData.CmtBuf = nullptr;
    archiveData.CmtBufSize = 0;

    HANDLE archive = RAROpenArchiveEx(&archiveData);
    if (!archive)
    {
      RarErrorLog(__func__, archiveData.OpenResult);
      return 0;
    }
    RARSetCallback(archive, m_callback, m_userData);

    std::string currentPw = m_password;

    needPassword = archiveData.Flags && ROADF_ENCHEADERS;
    if (needPassword)
    {
      CRARPasswordControl::GetPassword(m_path, m_password, m_passwordSeemsBad);
      currentPw = m_password;
    }

    int result;
    RARHeaderDataEx fileHeader = {0};
    while ((result = RARReadHeaderEx(archive, &fileHeader)) == SUCCESS)
    {
      if (firstTry)
        kodiLog(ADDON_LOG_DEBUG, "CRARControl::%s: List file from %s: %s (encrypted: %s)", __func__, fileHeader.ArcName, fileHeader.FileName, fileHeader.Flags & ROADF_LOCK ? "yes" : "no");

      result = RARProcessFile(archive, RAR_SKIP, nullptr, nullptr);
      if (result != SUCCESS)
      {
        kodiLog(ADDON_LOG_DEBUG, "CRARControl::%s: Error processing file %s", __func__, m_path.c_str());
        RARCloseArchive(archive);
        break;
      }

      list.push_back(fileHeader);
    }

    firstTry = false;

    m_passwordStandardCheck++;

    if (m_xmlWasAsked && list.empty())
    {
      m_passwordSeemsBad = true;
      CRARPasswordControl::SavePassword(m_path, m_password, m_passwordSeemsBad);
    }

    if (result != ERAR_END_ARCHIVE)
    {
      RarErrorLog(__func__, result);
      RARCloseArchive(archive);
      ret = false;
      continue;
    }
    else if ((needPassword && currentPw != m_password) || m_passwordSeemsBad)
    {
      m_passwordSeemsBad = false;
      CRARPasswordControl::SavePassword(m_path, m_password, m_passwordSeemsBad);
    }

    RARCloseArchive(archive);
    ret = true;
    break;
  }
  return ret;
}

int CRARControl::ArchiveExtract(const std::string& targetPath, const std::string& fileToExtract, bool showProgress/* = false*/)
{
  int retValue = 0;

  if (!kodi::vfs::FileExists(m_path))
  {
    kodiLog(ADDON_LOG_DEBUG, "CRARControl::%s: Request file %s not present", __func__, m_path.c_str());
    return retValue;
  }

  bool all = (fileToExtract.empty() || fileToExtract == "*");

  // Check how many files in RAR, on solid archives need it longer to get
  // wanted if it is more on end of files. This then used for progress
  // amount.
  unsigned int amount = 0;
  unsigned int current = 0;
  bool solid = false;
  if (!all)
  {
    std::vector<RARHeaderDataEx> list;
    ArchiveList(list);
    for (const auto& entry : list)
    {
      if (entry.FileName == fileToExtract)
      {
        solid = entry.Flags & RHDF_SOLID;
        break;
      }
      amount++;
    }
  }

  if (!kodi::vfs::DirectoryExists(targetPath))
    kodi::vfs::CreateDirectory(targetPath);

  if (showProgress)
  {
    // Init show progress with the RAR scan, on solid RAR files must be
    // everything extracted until wanted file to become it right, this
    // is to see that something works
    m_progress = std::make_shared<kodi::gui::dialogs::CExtendedProgress>();
    m_progress->SetPercentage(0.0f);
    m_progress->SetTitle(kodi::GetLocalizedString(solid ? 30002 : 30001));
    m_progress->SetText(m_path);
  }

  bool needPassword = false;
  bool firstTry = true;

  m_passwordStandardCheck = 0;

  while (firstTry || (needPassword && m_passwordStandardCheck < MAX_STANDARD_PASSWORDS))
  {

    RAROpenArchiveDataEx archiveData = {0};
    archiveData.OpenMode = RAR_OM_EXTRACT;
    archiveData.ArcName = const_cast<char*>(m_path.c_str());
    archiveData.CmtBuf = nullptr;
    archiveData.CmtBufSize = 0;

    HANDLE archive = RAROpenArchiveEx(&archiveData);
    if (!archive)
    {
      RarErrorLog(__func__, archiveData.OpenResult);
      return retValue;
    }
    RARSetCallback(archive, m_callback, m_userData);

    std::string currentPw = m_password;

    needPassword = archiveData.Flags && ROADF_ENCHEADERS;
    if (currentPw.empty() && needPassword)
    {
      CRARPasswordControl::GetPassword(m_path, m_password, m_passwordSeemsBad);
      currentPw = m_password;
    }

    int result;
    RARHeaderDataEx fileHeader = {0};
    retValue = 1;
    while ((result = RARReadHeaderEx(archive, &fileHeader)) == SUCCESS)
    {
      if (firstTry)
      {
        kodiLog(ADDON_LOG_DEBUG, "CRARControl::%s: List file from %s: %s", __func__, fileHeader.ArcName, fileHeader.FileName);

        needPassword = fileHeader.Flags & ROADF_LOCK;
        if (needPassword)
        {
          CRARPasswordControl::GetPassword(m_path, m_password, m_passwordSeemsBad);
          currentPw = m_password;
        }
      }

      int operation = (all || fileToExtract == fileHeader.FileName) ? RAR_EXTRACT : RAR_SKIP;
      if (operation == RAR_EXTRACT)
      {
        m_extractedFileSize = 0;
        m_extractFileSize = fileHeader.UnpSize;
        if (m_progress)
        {
          // After wanted file is found to progress with his real extract
          m_progress->SetTitle(kodi::GetLocalizedString(30000));
          m_progress->SetText(fileHeader.FileName);
          m_progress->SetProgress(m_extractedFileSize, m_extractFileSize);
        }
      }

      result = RARProcessFile(archive, operation, const_cast<char*>(targetPath.c_str()), nullptr);

      if (m_progress && operation == RAR_EXTRACT)
      {
        m_progress->MarkFinished();
      }

      if (result != SUCCESS)
      {
        kodiLog(ADDON_LOG_DEBUG, "CRARControl::%s: Error processing file %s", __func__, m_path.c_str());
        result = ERAR_END_ARCHIVE;
        retValue = 0;
        break;
      }

      if (operation != RAR_SKIP && !all)
      {
        result = ERAR_END_ARCHIVE;
        retValue = 1;
        break;
      }

      current++;
      if (m_progress)
        m_progress->SetProgress(current, amount);
    }

    firstTry = false;

    m_passwordStandardCheck++;

    if (result != ERAR_END_ARCHIVE)
    {
      if (m_xmlWasAsked)
      {
        m_passwordSeemsBad = true;
        CRARPasswordControl::SavePassword(m_path, m_password, m_passwordSeemsBad);
      }

      RarErrorLog(__func__, result);
      retValue = 0;
      continue;
    }
    else if ((needPassword && currentPw != m_password) || m_passwordSeemsBad)
    {
      m_passwordSeemsBad = false;
      CRARPasswordControl::SavePassword(m_path, m_password, m_passwordSeemsBad);
    }

    RARCloseArchive(archive);
    break;
  }

  m_progress = nullptr;

  return retValue;
}

int CRARControl::UnRarCallback(UINT msg, LPARAM UserData, LPARAM P1, LPARAM P2)
{
  CRARControl* thisPtr = reinterpret_cast<CRARControl*>(UserData);
  switch (msg)
  {
    case UCM_CHANGEVOLUME:
    {
      char* nextName = reinterpret_cast<char*>(P1);
      int message = static_cast<int>(P2);
      return thisPtr->VolumeChange(nextName, message);
    }
    case UCM_PROCESSDATA:
    {
      uint8_t* data = reinterpret_cast<uint8_t*>(P1);
      size_t length = static_cast<size_t>(P2);
      return thisPtr->ProcessData(data, length);
    }
    case UCM_NEEDPASSWORD:
    {
      char* password = reinterpret_cast<char*>(P1);
      size_t passwordSize = static_cast<size_t>(P2);
      return thisPtr->NeedPassword(password, passwordSize);
    }
    case UCM_CHANGEVOLUMEW:
    case UCM_NEEDPASSWORDW:
    {
      return CONTINUE_PROCESSING;
    }
    default:
    {
      kodiLog(ADDON_LOG_DEBUG, "CRARControl::%s: Unknown message passed to RAR callback function (0x%X)", __func__, msg);
      break;
    }
  }
  return STOP_PROCESSING;
}

int CRARControl::VolumeChange(const char* nextArchiveName, int mode)
{
  if (mode = RAR_VOL_ASK)
  {
    kodiLog(ADDON_LOG_DEBUG, "CRARControl::%s: Volume not found %s", __func__, nextArchiveName);
    return STOP_PROCESSING;
  }
  else // RAR_VOL_NOTIFY
  {
    if (!kodi::vfs::FileExists(nextArchiveName))
    {
      kodiLog(ADDON_LOG_DEBUG, "CRARControl::%s: Next volume %s is missing", __func__, nextArchiveName);
      return STOP_PROCESSING;
    }

    kodiLog(ADDON_LOG_DEBUG, "CRARControl::%s: Next volume is %s", __func__, nextArchiveName);
    return CONTINUE_PROCESSING;
  }
}

int CRARControl::ProcessData(uint8_t* block, size_t size)
{
  if (m_progress)
  {
    m_extractedFileSize += size;
    m_progress->SetProgress(m_extractedFileSize, m_extractFileSize);
    kodiLog(ADDON_LOG_DEBUG, "CRARControl::%s: Processing data (%li / %li)", __func__, m_extractedFileSize, m_extractFileSize);
  }
  return CONTINUE_PROCESSING;
}

int CRARControl::NeedPassword(char* password, size_t size)
{
  bool xmlPwPresent = false;
  std::string pw;

  if (!m_xmlWasAsked || !m_passwordSeemsBad)
  {
    m_xmlWasAsked = true;
    if (CRARPasswordControl::GetPassword(m_path, m_password, m_passwordSeemsBad) && !m_passwordSeemsBad)
    {
      pw = m_password;
      xmlPwPresent = true;
    }
  }

  if (pw.empty())
  {
    // Prevent ask and stop if no user ask enabled and nothing as standard inside settings
    if (!m_passwordAskAllowed && m_passwordStandardCheck >= MAX_STANDARD_PASSWORDS)
      return STOP_PROCESSING;

    // Check about standard passwords defined in settings.xml
    for (unsigned int i = m_passwordStandardCheck; i < MAX_STANDARD_PASSWORDS; ++i)
    {
      pw = kodi::GetSettingString("standard_password_" + std::to_string(i+1));
      if (!pw.empty())
      {
        strncpy(password, pw.c_str(), size);
        m_password = pw;
        return CONTINUE_PROCESSING;
      }
    }
  }

  // Break if nothing defined inside settings
  if (!m_passwordAskAllowed && pw.empty())
    return STOP_PROCESSING;

  std::string header = StringFormat(kodi::GetLocalizedString(30003).c_str(), m_path.length() > 45 ? kodi::vfs::GetFileName(m_path).c_str() : m_path.c_str());
  if (!pw.empty() || kodi::gui::dialogs::Keyboard::ShowAndGetInput(pw, header, false, true))
  {
    strncpy(password, pw.c_str(), size);
    m_password = pw;
    if (!xmlPwPresent)
      CRARPasswordControl::SavePassword(m_path, m_password, m_passwordSeemsBad);
    return CONTINUE_PROCESSING;
  }

  return STOP_PROCESSING;
}

void CRARControl::RarErrorLog(const std::string& func, int ErrCode)
{
  switch(ErrCode)
  {
    case RARX_FATAL:
      kodiLog(ADDON_LOG_FATAL, "CRARControl::%s: unrar lib created a fatal error on '%s'", func.c_str(), m_path.c_str());
      break;
    case RARX_CRC:
      kodiLog(ADDON_LOG_ERROR, "CRARControl::%s: CRC check on '%s' failed", func.c_str(), m_path.c_str());
      break;
    case RARX_WRITE:
      kodiLog(ADDON_LOG_ERROR, "CRARControl::%s: write error by process on '%s'", func.c_str(), m_path.c_str());
      break;
    case RARX_OPEN:
      kodiLog(ADDON_LOG_ERROR, "CRARControl::%s: failed to open on '%s'", func.c_str(), m_path.c_str());
      break;
    case RARX_CREATE:
      kodiLog(ADDON_LOG_ERROR, "CRARControl::%s: creation error on '%s'", func.c_str(), m_path.c_str());
      break;
    case RARX_MEMORY:
      kodiLog(ADDON_LOG_ERROR, "CRARControl::%s: memory error on '%s'", func.c_str(), m_path.c_str());
      break;
    case ERAR_MISSING_PASSWORD:
      kodiLog(ADDON_LOG_WARNING, "CRARControl::%s: missing password on '%s'", func.c_str(), m_path.c_str());
      break;
    case RARX_BADPWD:
      kodiLog(ADDON_LOG_WARNING, "CRARControl::%s: wrong password on '%s'", func.c_str(), m_path.c_str());
      break;
    case ERAR_EOPEN:
      kodiLog(ADDON_LOG_WARNING, "CRARControl::%s: failed to open '%s'", func.c_str(), m_path.c_str());
      break;
    case RARX_SUCCESS:
      break; // nothing
    default:
      kodiLog(ADDON_LOG_ERROR, "CRARControl::%s: unknown error %i on '%s'", func.c_str(), ErrCode, m_path.c_str());
      break;
  }
}

//------------------------------------------------------------------------------

RARContext::RARContext(const VFSURL& url)
  : CRARControl(url.hostname)
  , m_arc(&m_cmd)
  , m_extract(&m_cmd)
{
  m_cachedir = kodi::GetTempAddonPath("/");
  m_password = url.password;
  m_pathinrar = url.filename;
  std::vector<std::string> options;
  std::string options2(url.options);
  if (!options2.empty())
    CRarManager::Tokenize(options2.substr(1), options, "&");
  m_fileoptions = 0;
  for (const auto& it : options)
  {
    size_t iEqual = it.find('=');
    if(iEqual != std::string::npos)
    {
      std::string strOption = it.substr(0, iEqual);
      std::string strValue = it.substr(iEqual+1);

      if (strOption == "flags")
        m_fileoptions = atoi(strValue.c_str());
      else if (strOption == "cache")
        m_cachedir = strValue;
    }
  }
}

RARContext::~RARContext()
{
  if (m_file)
    delete m_file;
  delete m_buffer;
}

bool RARContext::OpenInArchive()
{
  try
  {
    int iHeaderSize;

    // Set the arguments for the extract command
    ErrHandler.Clean();

    m_cmd.DllError = 0;

    wcsncpyz(m_cmd.Command, L"X", ASIZE(m_cmd.Command));
    char ExtrPathA[MAX_PATH_LENGTH];
    strncpyz(ExtrPathA, m_cachedir.c_str(), ASIZE(ExtrPathA)-2);
#if defined(_WIN_ALL) && (!defined(WINAPI_FAMILY) || (WINAPI_FAMILY != WINAPI_FAMILY_APP))
    // We must not apply OemToCharBuffA directly to DestPath,
    // because we do not know DestPath length and OemToCharBuffA
    // does not stop at 0.
    OemToCharA(ExtrPathA,ExtrPathA);
#endif
    CharToWide(ExtrPathA, m_cmd.ExtrPath, ASIZE(m_cmd.ExtrPath));
    AddEndSlash(m_cmd.ExtrPath, ASIZE(m_cmd.ExtrPath));
    m_cmd.ParseArg(const_cast<wchar*>(L"-va"));
    m_cmd.DllOpMode = RAR_EXTRACT;

    char AnsiArcName[MAX_PATH_LENGTH];
    *AnsiArcName=0;
    if (!m_path.empty())
    {
      strncpyz(AnsiArcName, m_path.c_str(), ASIZE(AnsiArcName));
#if defined(_WIN_ALL) && (!defined(WINAPI_FAMILY) || (WINAPI_FAMILY != WINAPI_FAMILY_APP))
      if (!AreFileApisANSI())
      {
        OemToCharBuffA(m_path.c_str(), AnsiArcName, ASIZE(AnsiArcName));
        AnsiArcName[ASIZE(AnsiArcName)-1] = 0;
      }
#endif
    }

    wchar ArcName[MAX_PATH_LENGTH];
    GetWideName(AnsiArcName, nullptr, ArcName, ASIZE(ArcName));

    m_cmd.AddArcName(ArcName);
    m_cmd.Overwrite = OVERWRITE_ALL;
    m_cmd.VersionControl = 1;

    m_cmd.Callback = reinterpret_cast<UNRARCALLBACK>(UnRarCallback);
    m_cmd.UserData = reinterpret_cast<LPARAM>(this);

    bool xmlPwPresent = false;
    std::string pw;
    if (!m_xmlWasAsked || !m_passwordSeemsBad)
    {
      m_xmlWasAsked = true;
      if (CRARPasswordControl::GetPassword(m_path, m_password, m_passwordSeemsBad) && !m_passwordSeemsBad)
      {
        // Set password for encrypted archives
        if ((!m_password.empty()) &&
            (m_password.size() < sizeof (m_cmd.Password)))
        {
          wchar PasswordW[MAXPASSWORD];
          *PasswordW=0;
          GetWideName(m_password.c_str(),nullptr,PasswordW,ASIZE(PasswordW));
          m_cmd.Password.Set(PasswordW);
        }
      }
    }

    m_cmd.ParseDone();

    if (!m_arc.WOpen(ArcName))
    {
      CleanUp();
      return false;
    }

    uint FileCount = 0;
    int iArchive = 0;
    bool found = false;
    char name[MAX_PATH_LENGTH];
    while (1)
    {
      if (!(m_arc.IsOpened() && m_arc.IsArchive(true)))
      {
        CleanUp();
        return false;
      }

      if (m_arc.Volume && !m_arc.FirstVolume && iArchive == 0)
      {
        CleanUp();
        return false;
      }

      m_extract.GetDataIO().Init();
      m_extract.GetDataIO().SetUnpackToMemory(m_buffer, 0);
      m_extract.GetDataIO().SetCurrentCommand(*(m_cmd.Command));
      struct FindData FD;
      if (FindFile::FastFind(ArcName, &FD))
        m_extract.GetDataIO().TotalArcSize += FD.Size;
      m_extract.ExtractArchiveInit(m_arc);

      while ((iHeaderSize = m_arc.ReadHeader()) > 0)
      {
        if (m_arc.GetHeaderType() == HEAD_FILE)
        {
          WideToUtf(m_arc.FileHead.FileName, name, sizeof(name));
          std::string strFileName = name;

          /* replace back slashes into forward slashes */
          /* this could get us into troubles, file could two different files, one with / and one with \ */
          size_t index = 0;
          std::string oldStr = "\\";
          std::string newStr = "/";
          while (index < strFileName.size() && (index = strFileName.find(oldStr, index)) != std::string::npos)
          {
            strFileName.replace(index, oldStr.size(), newStr);
            index += newStr.size();
          }
          if (strFileName == m_pathinrar)
          {
            found = true;
            break;
          }
        }

        m_arc.SeekToNext();
      }

      if (found == true)
        break;
      if (found==false && /*m_cmd.VolSize != 0 && */((m_arc.FileHead.Flags & LHD_SPLIT_AFTER) ||
                              ((m_arc.GetHeaderType() == HEAD_ENDARC || m_arc.GetHeaderType() == HEAD3_ENDARC) &&
                              (m_arc.EndArcHead.Flags & EARC_NEXT_VOLUME)!=0)))
      {
        if (/*FileCount == 1 && */iArchive == 0)
        {
          wchar nextName[MAX_PATH_LENGTH];
          char nextNameA[MAX_PATH_LENGTH];
          wchar lastName[MAX_PATH_LENGTH];
          wcsncpyz(nextName, m_arc.FileName, ASIZE(nextName));
          WideToUtf(nextName, nextNameA, ASIZE(nextNameA));

          while (kodi::vfs::FileExists(nextNameA, true))
          {
            wcsncpyz(lastName, nextName, ASIZE(lastName));
            NextVolumeName(nextName, ASIZE(nextName), (m_arc.MainHead.Flags & MHD_NEWNUMBERING)==0 || m_arc.Format == RARFMT14);
            WideToUtf(nextName, nextNameA, ASIZE(nextNameA));
          }
          Archive arc;
          if (arc.WOpen(lastName))
          {
            bool bBreak = false;
            while (arc.ReadHeader()>0)
            {
              if (arc.GetHeaderType() == HEAD_FILE || arc.GetHeaderType() == HEAD3_FILE)
              {
                WideToUtf(arc.FileHead.FileName, name, sizeof(name));
                std::string check = name;

                /* replace back slashes into forward slashes */
                /* this could get us into troubles, file could two different files, one with / and one with \ */
                //          StringUtils::Replace(strFileName, '\\', '/');
                size_t index = 0;
                std::string oldStr = "\\";
                std::string newStr = "/";
                while (index < check.size() && (index = check.find(oldStr, index)) != std::string::npos)
                {
                  check.replace(index, oldStr.size(), newStr);
                  index += newStr.size();
                }
                if (check == m_pathinrar)
                {
                  break;
                }
              }
              //                  iOffset = pArc->Tell();
              arc.SeekToNext();
            }
            if (bBreak)
            {
              break;
            }
          }
        }

        if (MergeArchive(m_arc, nullptr, false, *m_cmd.Command))
        {
          iArchive++;
          m_arc.Seek(0, SEEK_SET);
        }
        else
          break;
      }
    }
    m_head = m_buffer;
    m_extract.GetDataIO().SetUnpackToMemory(m_buffer,0);
    m_inbuffer = -1;
    m_fileposition = 0;
    m_bufferstart = 0;

    m_extract_thread = new CRarFileExtractThread();
    m_extract_thread->Start(&m_arc,&m_cmd,&m_extract,iHeaderSize);

    return true;
  }
  catch (int rarErrCode)
  {
    kodiLog(ADDON_LOG_ERROR, "CRARControl::%s: failed in UnrarXLib while CFileRar::OpenInArchive with an UnrarXLib error code of %d", __func__, rarErrCode);
    return false;
  }
  catch (...)
  {
    kodiLog(ADDON_LOG_ERROR, "CRARControl::%s: failed in UnrarXLib while CFileRar::OpenInArchive with an Unknown exception", __func__);
    return false;
  }
}

void RARContext::CleanUp()
{
  try
  {
    if (m_extract_thread)
    {
      if (m_extract_thread->hRunning.Wait(1))
      {
        m_extract.GetDataIO().hQuit->Broadcast();
        while (m_extract_thread->hRunning.Wait(1))
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      delete m_extract.GetDataIO().hBufferFilled;
      delete m_extract.GetDataIO().hBufferEmpty;
      delete m_extract.GetDataIO().hSeek;
      delete m_extract.GetDataIO().hSeekDone;
      delete m_extract.GetDataIO().hQuit;
    }
  }
  catch (int rarErrCode)
  {
    kodiLog(ADDON_LOG_ERROR, "CRARControl::%s: filerar failed in UnrarXLib while deleting CFileRar with an UnrarXLib error code of %d", __func__, rarErrCode);
  }
  catch (...)
  {
    kodiLog(ADDON_LOG_ERROR, "CRARControl::%s: filerar failed in UnrarXLib while deleting CFileRar with an Unknown exception", __func__);
  }
}
