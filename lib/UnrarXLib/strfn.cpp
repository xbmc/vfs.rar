#include "rar.hpp"
#include "unicode.hpp"

const char *NullToEmpty(const char *Str)
{
  return(Str==NULL ? "":Str);
}


const wchar *NullToEmpty(const wchar *Str)
{
  return(Str==NULL ? L"":Str);
}


char *IntNameToExt(const char *Name)
{
  static char OutName[NM];
  IntToExt(Name,OutName);
  return(OutName);
}


void ExtToInt(const char *Src,char *Dest)
{
#if defined(_WIN_32)
  CharToOem(Src,Dest);
#else
  if (Dest!=Src)
    strcpy(Dest,Src);
#endif
}


void IntToExt(const char *Src,char *Dest)
{
#if defined(_WIN_32)
  OemToChar(Src,Dest);
#else
  if (Dest!=Src)
    strcpy(Dest,Src);
#endif
}


char* strlower(char *Str)
{
#ifdef _WIN_32
#if defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_FAMILY_APP)
  wchar *strW = new wchar[strlen(Str) + 1];
  wchar *lowerW = new wchar[strlen(Str) + 1];
  CharToWide(Str, strW, strlen(Str));
  LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, strW, strlen(Str), lowerW, strlen(Str), nullptr, nullptr, 0);
  char *lower = new char[wcslen(lowerW)];
  WideToChar(lowerW, lower, wcslen(lowerW));
  memcpy(Str, lower, strlen(lower));
  delete[] strW;
  delete[] lowerW;
  delete[] lower;
#else
  CharLower((LPTSTR)Str);
#endif
#else
  for (char *ChPtr=Str;*ChPtr;ChPtr++)
    *ChPtr=(char)loctolower(*ChPtr);
#endif
  return(Str);
}


char* strupper(char *Str)
{
#ifdef _WIN_32
#if defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_FAMILY_APP)
  wchar *strW = new wchar[strlen(Str) + 1];
  wchar *upperW = new wchar[strlen(Str) + 1];
  CharToWide(Str, strW, strlen(Str));
  LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, strW, strlen(Str), upperW, strlen(Str), nullptr, nullptr, 0);
  char *upper = new char[wcslen(upperW)];
  WideToChar(upperW, upper, wcslen(upperW));
  memcpy(Str, upper, strlen(upper));
  delete[] strW;
  delete[] upperW;
  delete[] upper;
#else
  CharUpper((LPTSTR)Str);
#endif
#else
  for (char *ChPtr=Str;*ChPtr;ChPtr++)
    *ChPtr=(char)loctoupper(*ChPtr);
#endif
  return(Str);
}


int stricomp(const char *Str1,const char *Str2)
{
  char S1[NM*2],S2[NM*2];
  strncpy(S1,Str1,sizeof(S1));
  strncpy(S2,Str2,sizeof(S2));
  return(strcmp(strupper(S1),strupper(S2)));
}


int strnicomp(const char *Str1,const char *Str2,int N)
{
  char S1[512],S2[512];
  strncpy(S1,Str1,sizeof(S1));
  strncpy(S2,Str2,sizeof(S2));
  return(strncmp(strupper(S1),strupper(S2),N));
}


char* RemoveEOL(char *Str)
{
  for (int I=strlen(Str)-1;I>=0 && (Str[I]=='\r' || Str[I]=='\n' || Str[I]==' ' || Str[I]=='\t');I--)
    Str[I]=0;
  return(Str);
}


char* RemoveLF(char *Str)
{
  for (int I=strlen(Str)-1;I>=0 && (Str[I]=='\r' || Str[I]=='\n');I--)
    Str[I]=0;
  return(Str);
}


unsigned int loctolower(byte ch)
{
#ifdef _WIN_32
#if defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_FAMILY_APP)
  wchar chW[2], mapped_tmp[2];
  char lower[2];
  CharToWide((char*)&ch, chW, 1);
  LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, chW, 1, mapped_tmp, 2, nullptr, nullptr, 0);
  WideToChar(chW, lower, 1);
  return((unsigned int)lower[0]);
#else
  return((int)CharLower((LPTSTR)ch));
#endif
#else
  return(tolower(ch));
#endif
}


unsigned int loctoupper(byte ch)
{
#ifdef _WIN_32
#if defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_FAMILY_APP)
  wchar chW[2], mapped_tmp[2];
  char upper[2];
  CharToWide((char*)&ch, chW, 1);
  LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, chW, 1, mapped_tmp, 2, nullptr, nullptr, 0);
  WideToChar(chW, upper, 1);
  return((unsigned int)upper[0]);
#else
  return((int)CharUpper((LPTSTR)ch));
#endif
#else
  return(toupper(ch));
#endif
}





bool LowAscii(const char *Str)
{
  for (int I=0;Str[I]!=0;I++)
    if ((byte)Str[I]<32 || (byte)Str[I]>127)
      return(false);
  return(true);
}


bool LowAscii(const wchar *Str)
{
  for (int I=0;Str[I]!=0;I++)
    if (Str[I]<32 || Str[I]>127)
      return(false);
  return(true);
}
