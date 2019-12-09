#include "rar.hpp"

#ifdef _WIN_32
int WinNT()
{
#if defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_FAMILY_APP)
  return true;
#else
  static int dwPlatformId=-1,dwMajorVersion;
  if (dwPlatformId==-1)
  {
    OSVERSIONINFO WinVer;
    WinVer.dwOSVersionInfoSize=sizeof(WinVer);
    GetVersionEx(&WinVer);
    dwPlatformId=WinVer.dwPlatformId;
    dwMajorVersion=WinVer.dwMajorVersion;
  }
  return(dwPlatformId==VER_PLATFORM_WIN32_NT ? dwMajorVersion:0);
#endif
}
#endif
