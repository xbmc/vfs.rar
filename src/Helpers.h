/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include <chrono>
#include <condition_variable>
#include <ctime>
#include <functional>
#include <kodi/General.h>
#include <mutex>
#include <thread>

namespace
{

inline const char* kodiTranslateLogLevel(const AddonLog logLevel)
{
  switch (logLevel)
  {
    case ADDON_LOG_DEBUG:
      return "LOG_DEBUG:   ";
    case ADDON_LOG_INFO:
      return "LOG_INFO:    ";
    case ADDON_LOG_WARNING:
      return "LOG_WARNING: ";
    case ADDON_LOG_ERROR:
      return "LOG_ERROR:   ";
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

inline std::string StringFormatV(const char* fmt, va_list args)
{
  if (fmt == nullptr)
    return "";

  int size = 2048;
  va_list argCopy;

  char *cstr = reinterpret_cast<char*>(malloc(sizeof(char) * size));
  if (cstr == nullptr)
    return "";

  while (1)
  {
    va_copy(argCopy, args);

    int nActual = vsnprintf(cstr, size, fmt, argCopy);
    va_end(argCopy);

    if (nActual > -1 && nActual < size) // We got a valid result
    {
      std::string str(cstr, nActual);
      free(cstr);
      return str;
    }
    if (nActual > -1)                   // Exactly what we will need (glibc 2.1)
      size = nActual + 1;
    else                                // Let's try to double the size (glibc 2.0)
      size *= 2;

    char *new_cstr = reinterpret_cast<char*>(realloc(cstr, sizeof(char) * size));
    if (new_cstr == nullptr)
    {
      free(cstr);
      return "";
    }

    cstr = new_cstr;
  }

  free(cstr);
  return "";
}

inline std::string StringFormat(const char* fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  std::string str = StringFormatV(fmt, args);
  va_end(args);

  return str;
}

} // namespace

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
