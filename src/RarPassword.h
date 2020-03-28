/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

class CRARPasswordControl
{
public:
  static void CleanupPasswordList();
  static bool GetPassword(const std::string& path, std::string& password, bool& passwordSeemsBad);
  static bool SavePassword(const std::string& path, const std::string& password, const bool& passwordSeemsBad);
};
