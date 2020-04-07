/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "Helpers.h"
#include "RarPassword.h"
#include "encryption/encrypt.h"

#include <kodi/Filesystem.h>
#include <kodi/General.h>
#include <tinyxml.h>

void CRARPasswordControl::CleanupPasswordList()
{
  TiXmlDocument xmlDoc;
  std::string strSettingsFile = kodi::GetBaseUserPath("rar-control.xml");

  // Check file present and available, if not do nothing
  if (!kodi::vfs::FileExists(strSettingsFile))
    return;

  if (!xmlDoc.LoadFile(strSettingsFile))
  {
    kodiLog(ADDON_LOG_ERROR, "CRARControl::%s: invalid data (no/invalid data file found at '%s')", __func__, strSettingsFile.c_str());
    return;
  }

  bool containsChanges = false;

  TiXmlElement* pElementBase = xmlDoc.FirstChildElement("data");
  if (pElementBase)
  {
    TiXmlElement* pElement = pElementBase->FirstChildElement("path");
    for (pElement; pElement; pElement = pElement->NextSiblingElement())
    {
      const TiXmlNode* pNode = pElement->FirstChild();
      if (pNode != nullptr)
      {
        const char* added = pElement->Attribute("added");
        if (!added)
          return;
        std::string path = decrypt(pNode->Value(), added);
        if (!kodi::vfs::FileExists(path))
        {
          pElementBase->RemoveChild(pElement);
          containsChanges = true;
        }
      }
    }
  }

  if (containsChanges)
  {
    if (!xmlDoc.SaveFile(strSettingsFile))
    {
      kodiLog(ADDON_LOG_ERROR, "CRARControl::%s: failed to write settings data", __func__);
      return;
    }
  }
}

bool CRARPasswordControl::GetPassword(const std::string& path, std::string& password, bool& passwordSeemsBad)
{
  TiXmlDocument xmlDoc;
  std::string strSettingsFile = kodi::GetBaseUserPath("rar-control.xml");

  if (!kodi::vfs::FileExists(strSettingsFile))
    return false;

  if (!xmlDoc.LoadFile(strSettingsFile))
  {
    kodiLog(ADDON_LOG_ERROR, "CRARControl::%s: invalid data (no/invalid data file found at '%s')", __func__, strSettingsFile.c_str());
    return false;
  }

  TiXmlElement* pElement = xmlDoc.FirstChildElement("data");
  if (pElement)
  {
    pElement = pElement->FirstChildElement("path");
    for (pElement; pElement; pElement = pElement->NextSiblingElement())
    {
      const TiXmlNode* pNode = pElement->FirstChild();
      if (pNode != nullptr)
      {
        const char* added = pElement->Attribute("added");
        if (!added)
          return false;
        if (path == decrypt(pNode->Value(), added))
        {
          const char* attr;

          attr = pElement->Attribute("pw");
          if (!attr)
            return false;
          password = decrypt(attr, added);

          attr = pElement->Attribute("bad");
          if (!attr)
            return false;
          passwordSeemsBad = std::string(attr) == "true" ? true : false;
          return true;
        }
      }
    }
  }

  return false;
}

bool CRARPasswordControl::SavePassword(const std::string& path, const std::string& password, const bool& passwordSeemsBad)
{
  TiXmlDocument xmlDoc;
  std::string strSettingsFile = kodi::GetBaseUserPath("rar-control.xml");

  if (kodi::vfs::FileExists(strSettingsFile))
  {
    if (!xmlDoc.LoadFile(strSettingsFile))
    {
      kodiLog(ADDON_LOG_ERROR, "invalid data (no/invalid data file found at '%s')", strSettingsFile.c_str());
      return false;
    }
  }
  else
    kodi::vfs::CreateDirectory(kodi::GetBaseUserPath());

  bool isUpdated = false;
  TiXmlElement* pElement = xmlDoc.FirstChildElement("data");
  if (pElement)
  {
    pElement = pElement->FirstChildElement("path");
    for (pElement; pElement; pElement = pElement->NextSiblingElement())
    {
      const TiXmlNode* pNode = pElement->FirstChild();
      if (pNode != nullptr)
      {
        const char* attr = pElement->Attribute("added");
        if (!attr)
          return false;
        if (path == decrypt(pNode->Value(), attr))
        {
          pElement->SetAttribute("pw", encrypt(password, attr).c_str());
          pElement->SetAttribute("bad", passwordSeemsBad ? "true" : "false");
          isUpdated = true;
          break;
        }
      }
    }
  }

  if (!isUpdated)
  {
    TiXmlNode* pNode;
    TiXmlElement *pElement = xmlDoc.FirstChildElement("data");
    if (!pElement)
    {
      TiXmlElement xmlSetting("data");
      pNode = xmlDoc.InsertEndChild(xmlSetting);
    }
    else
    {
      pNode = pElement;
    }

    if (pNode)
    {
      std::string addTime = NowToString();
      TiXmlElement newElement("path");
      newElement.SetAttribute("pw", encrypt(password, addTime).c_str());
      newElement.SetAttribute("added", addTime.c_str());
      newElement.SetAttribute("bad", passwordSeemsBad ? "true" : "false");
      TiXmlNode *pNewNode = pNode->InsertEndChild(newElement);
      if (pNewNode)
      {
        TiXmlText value(encrypt(path, addTime).c_str());
        pNewNode->InsertEndChild(value);
      }
    }
  }

  if (!xmlDoc.SaveFile(strSettingsFile))
  {
    kodiLog(ADDON_LOG_ERROR, "CRARControl::%s: failed to write settings data", __func__);
    return false;
  }

  return true;
}
