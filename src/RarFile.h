/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include <kodi/addon-instance/VFS.h>

class CRARFile : public kodi::addon::CInstanceVFS
{
public:
  CRARFile(KODI_HANDLE instance, const std::string& version) : CInstanceVFS(instance, version) { }

  void* Open(const VFSURL& url) override;
  ssize_t Read(void* context, void* buffer, size_t uiBufSize) override;
  int64_t Seek(void* context, int64_t position, int whence) override;
  int64_t GetLength(void* context) override;
  int64_t GetPosition(void* context) override;
  int IoControl(void* context, VFS_IOCTRL request, void* param) override;
  int Stat(const VFSURL& url, struct __stat64* buffer) override;
  bool Close(void* context) override;
  bool Exists(const VFSURL& url) override;
  void ClearOutIdle() override;
  void DisconnectAll() override;
  bool DirectoryExists(const VFSURL& url) override;
  bool GetDirectory(const VFSURL& url, std::vector<kodi::vfs::CDirEntry>& items, CVFSCallbacks callbacks) override;
  bool ContainsFiles(const VFSURL& url, std::vector<kodi::vfs::CDirEntry>& items, std::string& rootpath) override;

private:
  std::string URLEncode(const std::string& strURLData);
};
