/*
 *      Copyright (C) 2005-2019 Team Kodi
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

#include <chrono>
#include <condition_variable>
#include <ctime>
#include <functional>
#include <kodi/General.h>
#include <mutex>
#include <thread>

inline const char* kodiTranslateLogLevel(const AddonLog logLevel)
{
  switch (logLevel)
  {
    case ADDON_LOG_DEBUG:
      return "LOG_DEBUG:   ";
    case ADDON_LOG_INFO:
      return "LOG_INFO:    ";
    case ADDON_LOG_NOTICE:
      return "LOG_NOTICE:  ";
    case ADDON_LOG_WARNING:
      return "LOG_WARNING: ";
    case ADDON_LOG_ERROR:
      return "LOG_ERROR:   ";
    case ADDON_LOG_SEVERE:
      return "LOG_SEVERE:  ";
    case ADDON_LOG_FATAL:
      return "LOG_FATAL:   ";
    default:
      break;
  }
  return "LOG_UNKNOWN: ";
}

inline void kodiLog(const AddonLog logLevel, const char* format, ...)
{
  char buffer[16384];
  va_list args;
  va_start(args, format);
  vsprintf(buffer, format, args);
  va_end(args);

  kodi::Log(logLevel, buffer);
#ifdef DEBUG
  fprintf(stderr, "%s%s\n", kodiTranslateLogLevel(logLevel), buffer);
#endif
}

inline std::string NowToString()
{
  std::chrono::system_clock::time_point p = std::chrono::system_clock::now();
  time_t t = std::chrono::system_clock::to_time_t(p);
  return std::ctime(&t);
}

class CTimeout
{
public:
  CTimeout(void) : m_iTarget(0) {}
  CTimeout(uint32_t iTimeout) { Init(iTimeout); }

  bool IsSet(void) const       { return m_iTarget > 0; }
  void Init(uint32_t iTimeout) { m_iTarget = static_cast<int64_t>(std::chrono::duration<double>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() * 1000.0) + iTimeout; }

  uint32_t TimeLeft(void) const
  {
    uint64_t iNow = static_cast<int64_t>(std::chrono::duration<double>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() * 1000.0);
    return (iNow > m_iTarget) ? 0 : (uint32_t)(m_iTarget - iNow);
  }

private:
  uint64_t m_iTarget;
};

namespace ThreadHelpers
{

typedef bool (*PredicateCallback) (void *param);

template <typename _Predicate>
  class CCondition
  {
  private:
    static bool _PredicateCallbackDefault ( void *param )
    {
      _Predicate *p = (_Predicate*)param;
      return (*p);
    }
  public:
    inline CCondition(void) {}
    inline ~CCondition(void)
    {
      m_condition.notify_all();
    }

    inline void Broadcast(void)
    {
      m_condition.notify_all();
    }

    inline void Signal(void)
    {
      m_condition.notify_one();
    }

    inline bool Wait(std::recursive_mutex &mutex, uint32_t iTimeout)
    {
      std::unique_lock<std::recursive_mutex> lck(mutex);
      return m_condition.wait_for(lck, std::chrono::milliseconds(iTimeout)) != std::cv_status::timeout;
    }

    inline bool Wait(std::recursive_mutex &mutex, PredicateCallback callback, void *param, uint32_t iTimeout)
    {
      bool bReturn(false);
      CTimeout timeout(iTimeout);

      while (!bReturn)
      {
        if ((bReturn = callback(param)) == true)
          break;
        uint32_t iMsLeft = timeout.TimeLeft();
        if ((iTimeout != 0) && (iMsLeft == 0))
          break;
        std::unique_lock<std::recursive_mutex> lck(mutex);
        m_condition.wait_for(lck, std::chrono::milliseconds(iMsLeft));
      }

      return bReturn;
    }

    inline bool Wait(std::recursive_mutex &mutex, _Predicate &predicate, uint32_t iTimeout = 0)
    {
      return Wait(mutex, _PredicateCallbackDefault, (void*)&predicate, iTimeout);
    }

  private:
    std::condition_variable_any m_condition;
  };

class CEvent
{
public:
  CEvent(bool bAutoReset = true) :
    m_bSignaled(false),
    m_bBroadcast(false),
    m_iWaitingThreads(0),
    m_bAutoReset(bAutoReset) {}
  virtual ~CEvent(void) {}

  void Broadcast(void)
  {
    Set(true);
    m_condition.Broadcast();
  }

  void Signal(void)
  {
    Set(false);
    m_condition.Signal();
  }

  bool Wait(void)
  {
    std::unique_lock<std::recursive_mutex> lck(m_mutex);
    ++m_iWaitingThreads;

    bool bReturn = m_condition.Wait(m_mutex, m_bSignaled);
    return ResetAndReturn() && bReturn;
  }

  bool Wait(uint32_t iTimeout)
  {
    if (iTimeout == 0)
      return Wait();

    std::unique_lock<std::recursive_mutex> lck(m_mutex);
    ++m_iWaitingThreads;
    bool bReturn = m_condition.Wait(m_mutex, m_bSignaled, iTimeout);
    return ResetAndReturn() && bReturn;
  }

  static void Sleep(uint32_t iTimeout)
  {
    CEvent event;
    event.Wait(iTimeout);
  }

  void Reset(void)
  {
    std::unique_lock<std::recursive_mutex> lck(m_mutex);
    m_bSignaled = false;
  }

private:
  void Set(bool bBroadcast = false)
  {
    m_bSignaled  = true;
    m_bBroadcast = bBroadcast;
  }

  bool ResetAndReturn(void)
  {
    bool bReturn(m_bSignaled);
    if (bReturn && (--m_iWaitingThreads == 0 || !m_bBroadcast) && m_bAutoReset)
      m_bSignaled = false;
    return bReturn;
  }

  volatile bool m_bSignaled;
  CCondition<volatile bool> m_condition;
  std::recursive_mutex m_mutex;
  volatile bool m_bBroadcast;
  unsigned int m_iWaitingThreads;
  bool m_bAutoReset;
};

} // namespace ThreadHelpers
