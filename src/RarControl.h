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

#include "rar.hpp"

#include <kodi/addon-instance/VFS.h>
#include <kodi/gui/dialogs/ExtendedProgress.h>
#include <memory>
#include <string>
#include <vector>

class CRarFileExtractThread;

class CRARControl
{
public:
  CRARControl(const std::string& rarPath);
  virtual ~CRARControl() = default;

  void SetCallback(UNRARCALLBACK callback, LPARAM userdata);

  /**
   * @brief List the files in a RAR file
   *
   * @param[out] list A list of file data of the files in the archive.
   * @return true if successed and files in list
   */
  bool ArchiveList(std::vector<RARHeaderDataEx>& list);

  /**
   * @brief Extract a RAR file
   *
   * @param[in] targetPath The path to which we want to uncompress
   * @param[in] fileToExtract The file inside the archive we want to uncompress,
   *                          or empty for all files.
   * @param[in] showProgress To show a progress bar during extract
   * @return Return 0 on error, 1 on success and 2 if extract was cancelled
   *
   * @note It becomes CExtendedProgress (without buttons) used, by Kodi's dimmed
   * black screen is not possible to press a cancel button on CProgress dialog.
   * For that reason 2 becomes never returned.
   */
  int ArchiveExtract(const std::string& targetPath, const std::string& fileToExtract, bool showProgress = false);

  /**
   * @brief Get used RAR file path
   *
   * @return Path to RAR file
   */
  const std::string& GetPath() const { return m_path; }

protected:
  int VolumeChange(const char* nextArchiveName, int mode);
  int ProcessData(uint8_t* block, size_t size);
  int NeedPassword(char* password, size_t size);
  void RarErrorLog(const std::string& func, int errCode);
  static int UnRarCallback(UINT msg, LPARAM UserData, LPARAM P1, LPARAM P2);

  bool GetPassword();
  bool SavePassword();

  std::string m_path;
  std::string m_password;
  bool m_passwordSeemsBad = false;
  LPARAM m_userData = 0;
  UNRARCALLBACK m_callback = nullptr;
  std::shared_ptr<kodi::gui::dialogs::CExtendedProgress> m_progress = nullptr;
  size_t m_extractFileSize = 0;
  size_t m_extractedFileSize = 0;
  bool m_xmlWasAsked = false;
};

class RARContext : public CRARControl
{
public:
  RARContext(const VFSURL& url);
  ~RARContext() override;

  bool OpenInArchive();
  void CleanUp();

  CommandData m_cmd;
  Archive m_arc;
  CmdExtract m_extract;

  CRarFileExtractThread* m_extract_thread = nullptr;
  uint8_t* m_buffer = new uint8_t[File::CopyBufferSize()];
  uint8_t* m_head = m_buffer;
  int64_t m_inbuffer = 0;
  std::string m_cachedir;
  std::string m_pathinrar;
  int8_t m_fileoptions = 0;
  int64_t m_size = 0;
  kodi::vfs::CFile* m_file = nullptr;
  int64_t m_fileposition = 0;
  int64_t m_bufferstart = 0;
  bool m_seekable = true;
};











