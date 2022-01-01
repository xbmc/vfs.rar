/*
 *  Copyright (C) 2005-2021 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include <kodi/addon-instance/VFS.h>

class CRARFile : public kodi::addon::CInstanceVFS
{
public:
  CRARFile(const kodi::addon::IInstanceInfo& instance) : CInstanceVFS(instance) { }

  kodi::addon::VFSFileHandle Open(const kodi::addon::VFSUrl& url) override;
  bool Close(kodi::addon::VFSFileHandle context) override;
  ssize_t Read(kodi::addon::VFSFileHandle context, uint8_t* buffer, size_t uiBufSize) override;
  int64_t Seek(kodi::addon::VFSFileHandle context, int64_t position, int whence) override;
  int64_t GetLength(kodi::addon::VFSFileHandle context) override;
  int64_t GetPosition(kodi::addon::VFSFileHandle context) override;
  bool IoControlGetSeekPossible(kodi::addon::VFSFileHandle context) override;
  int Stat(const kodi::addon::VFSUrl& url, kodi::vfs::FileStatus& buffer) override;
  bool Exists(const kodi::addon::VFSUrl& url) override;
  void ClearOutIdle() override;
  void DisconnectAll() override;
  bool DirectoryExists(const kodi::addon::VFSUrl& url) override;
  bool GetDirectory(const kodi::addon::VFSUrl& url, std::vector<kodi::vfs::CDirEntry>& items, CVFSCallbacks callbacks) override;
  bool ContainsFiles(const kodi::addon::VFSUrl& url, std::vector<kodi::vfs::CDirEntry>& items, std::string& rootpath) override;

private:
  std::string URLEncode(const std::string& strURLData);
};
