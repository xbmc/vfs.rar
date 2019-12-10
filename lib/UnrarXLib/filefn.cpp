#include "rar.hpp"

MKDIR_CODE MakeDir(const char *Name,const wchar *NameW,bool SetAttr,uint Attr)
{
#ifdef _WIN_32
  int Success;
  if (WinNT() && NameW!=NULL && *NameW!=0)
    Success=CreateDirectoryW(NameW,NULL);
  else
    Success=CreateDirectory(Name,NULL);
  if (Success)
  {
    if (SetAttr)
      SetFileAttr(Name,NameW,Attr);
    return(MKDIR_SUCCESS);
  }
  int ErrCode=GetLastError();
  if (ErrCode==ERROR_FILE_NOT_FOUND || ErrCode==ERROR_PATH_NOT_FOUND)
    return(MKDIR_BADPATH);
  return(MKDIR_ERROR);
#endif
#ifdef _EMX
#ifdef _DJGPP
  if (mkdir(Name,(Attr & FA_RDONLY) ? 0:S_IWUSR)==0)
#else
  if (__mkdir(Name)==0)
#endif
  {
    if (SetAttr)
      SetFileAttr(Name,NameW,Attr);
    return(MKDIR_SUCCESS);
  }
  return(errno==ENOENT ? MKDIR_BADPATH:MKDIR_ERROR);
#endif
#ifdef _UNIX
  mode_t uattr=SetAttr ? (mode_t)Attr:0777;
  int ErrCode=Name==NULL ? -1:mkdir(Name,uattr);
  if (ErrCode==-1)
    return(errno==ENOENT ? MKDIR_BADPATH:MKDIR_ERROR);
  return(MKDIR_SUCCESS);
#endif
}


bool CreatePath(const char *Path,const wchar *PathW,bool SkipLastName)
{
#if defined(_WIN_32) || defined(_EMX)
  uint DirAttr=0;
#else
  uint DirAttr=0777;
#endif
#ifdef UNICODE_SUPPORTED
  bool Wide=PathW!=NULL && *PathW!=0 && UnicodeEnabled();
#else
  bool Wide=false;
#endif
  bool IgnoreAscii=false;
  bool Success=true;

  const char *s=Path;
  for (int PosW=0;;PosW++)
  {
    if (s==NULL || s-Path>=NM || *s==0)
      IgnoreAscii=true;
    if (Wide && (PosW>=NM || PathW[PosW]==0) || !Wide && IgnoreAscii)
      break;
    if (Wide && PathW[PosW]==CPATHDIVIDER || !Wide && *s==CPATHDIVIDER)
    {
      wchar *DirPtrW=NULL,DirNameW[NM];
      if (Wide)
      {
        strncpyw(DirNameW,PathW,PosW);
        DirNameW[PosW]=0;
        DirPtrW=DirNameW;
      }
      char DirName[NM];
      if (IgnoreAscii)
        WideToChar(DirPtrW,DirName);
      else
      {
#ifndef DBCS_SUPPORTED
        if (*s!=CPATHDIVIDER)
          for (const char *n=s;*n!=0 && n-Path<NM;n++)
            if (*n==CPATHDIVIDER)
            {
              s=n;
              break;
            }
#endif
        strncpy(DirName,Path,s-Path);
        DirName[s-Path]=0;
      }
      if (MakeDir(DirName,DirPtrW,true,DirAttr)==MKDIR_SUCCESS)
      {
#ifndef GUI
        mprintf(St(MCreatDir),DirName);
        mprintf(" %s",St(MOk));
#endif
      }
      else
        Success=false;
    }
    if (!IgnoreAscii)
      s=charnext(s);
  }
  if (!SkipLastName && !IsPathDiv(*PointToLastChar(Path)))
    if (MakeDir(Path,PathW,true,DirAttr)!=MKDIR_SUCCESS)
      Success=false;
  return(Success);
}


void SetDirTime(const char *Name,const wchar *NameW,RarTime *ftm,RarTime *ftc,RarTime *fta)
{
#ifdef _WIN_32
  if (!WinNT())
    return;

  bool sm=ftm!=NULL && ftm->IsSet();
  bool sc=ftc!=NULL && ftc->IsSet();
  bool sa=fta!=NULL && fta->IsSet();

  unsigned int DirAttr=GetFileAttr(Name,NameW);
  bool ResetAttr=(DirAttr!=0xffffffff && (DirAttr & FA_RDONLY)!=0);
  if (ResetAttr)
    SetFileAttr(Name,NameW,0);

  wchar DirNameW[NM];
  GetWideName(Name,NameW,DirNameW);
  HANDLE hFile=CreateFileW(DirNameW,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
                          NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);
  if (hFile==INVALID_HANDLE_VALUE)
    return;
  FILETIME fm,fc,fa;
  if (sm)
    ftm->GetWin32(&fm);
  if (sc)
    ftc->GetWin32(&fc);
  if (sa)
    fta->GetWin32(&fa);
  SetFileTime(hFile,sc ? &fc:NULL,sa ? &fa:NULL,sm ? &fm:NULL);
  CloseHandle(hFile);
  if (ResetAttr)
    SetFileAttr(Name,NameW,DirAttr);
#endif
#if defined(_UNIX) || defined(_EMX)
  File::SetCloseFileTimeByName(Name,ftm,fta);
#endif
}


bool IsRemovable(const char *Name)
{
#ifdef _WIN_32
  char Root[NM];
  GetPathRoot(Name,Root);
  int Type=GetDriveType(*Root ? Root:NULL);
  return(Type==DRIVE_REMOVABLE || Type==DRIVE_CDROM);
#elif defined(_EMX)
  char Drive=etoupper(Name[0]);
  return((Drive=='A' || Drive=='B') && Name[1]==':');
#else
  return(false);
#endif
}


#ifndef SFX_MODULE
int64 GetFreeDisk(const char *Name)
{
#ifdef _WIN_32
  char Root[NM];
  GetPathRoot(Name,Root);

  typedef BOOL (WINAPI *GETDISKFREESPACEEX)(
    LPCTSTR,PULARGE_INTEGER,PULARGE_INTEGER,PULARGE_INTEGER
   );
  static GETDISKFREESPACEEX pGetDiskFreeSpaceEx=NULL;

  if (pGetDiskFreeSpaceEx==NULL)
  {
    HMODULE hKernel=GetModuleHandle("kernel32.dll");
    if (hKernel!=NULL)
      pGetDiskFreeSpaceEx=(GETDISKFREESPACEEX)GetProcAddress(hKernel,"GetDiskFreeSpaceExA");
  }
  if (pGetDiskFreeSpaceEx!=NULL)
  {
    GetFilePath(Name,Root,ASIZE(Root));
    ULARGE_INTEGER uiTotalSize,uiTotalFree,uiUserFree;
    uiUserFree.u.LowPart=uiUserFree.u.HighPart=0;
    if (pGetDiskFreeSpaceEx(*Root ? Root:NULL,&uiUserFree,&uiTotalSize,&uiTotalFree) &&
        uiUserFree.u.HighPart<=uiTotalFree.u.HighPart)
      return(INT32TO64(uiUserFree.u.HighPart,uiUserFree.u.LowPart));
  }

  // We are here if we failed to load GetDiskFreeSpaceExA.
  DWORD SectorsPerCluster,BytesPerSector,FreeClusters,TotalClusters;
  if (!GetDiskFreeSpace(*Root ? Root:NULL,&SectorsPerCluster,&BytesPerSector,&FreeClusters,&TotalClusters))
    return(1457664);
  int64 FreeSize=SectorsPerCluster*BytesPerSector;
  FreeSize=FreeSize*FreeClusters;
  return(FreeSize);
#elif defined(_BEOS)
  char Root[NM];
  GetFilePath(Name,Root,ASIZE(Root));
  dev_t Dev=dev_for_path(*Root ? Root:".");
  if (Dev<0)
    return(1457664);
  fs_info Info;
  if (fs_stat_dev(Dev,&Info)!=0)
    return(1457664);
  int64 FreeSize=Info.block_size;
  FreeSize=FreeSize*Info.free_blocks;
  return(FreeSize);
#elif defined(_UNIX)
  return(1457664);
#elif defined(_EMX)
  int Drive=IsDiskLetter(Name) ? etoupper(Name[0])-'A'+1:0;
#ifndef _DJGPP
  if (_osmode == OS2_MODE)
  {
    FSALLOCATE fsa;
    if (DosQueryFSInfo(Drive,1,&fsa,sizeof(fsa))!=0)
      return(1457664);
    int64 FreeSize=fsa.cSectorUnit*fsa.cbSector;
    FreeSize=FreeSize*fsa.cUnitAvail;
    return(FreeSize);
  }
  else
#endif
  {
    union REGS regs,outregs;
    memset(&regs,0,sizeof(regs));
    regs.h.ah=0x36;
    regs.h.dl=Drive;
#ifdef _DJGPP
    int86 (0x21,&regs,&outregs);
#else
    _int86 (0x21,&regs,&outregs);
#endif
    if (outregs.x.ax==0xffff)
      return(1457664);
    int64 FreeSize=outregs.x.ax*outregs.x.cx;
    FreeSize=FreeSize*outregs.x.bx;
    return(FreeSize);
  }
#else
  #define DISABLEAUTODETECT
  return(1457664);
#endif
}
#endif


bool FileExist(const char *Name,const wchar *NameW)
{
#ifdef _WIN_32
    if (WinNT() && NameW!=NULL && *NameW!=0)
      return(GetFileAttributesW(NameW)!=0xffffffff);
    else
      return(GetFileAttributes(Name)!=0xffffffff);
#elif defined(ENABLE_ACCESS)
  return(access(Name,0)==0);
#else
  struct FindData FD;
  return(FindFile::FastFind(Name,NameW,&FD));
#endif
}


bool WildFileExist(const char *Name,const wchar *NameW)
{
  if (IsWildcard(Name,NameW))
  {
    FindFile Find;
    Find.SetMask(Name);
    Find.SetMaskW(NameW);
    struct FindData fd;
    return(Find.Next(&fd));
  }
  return(FileExist(Name,NameW));
}


bool IsDir(uint Attr)
{
#if defined (_WIN_32) || defined(_EMX)
  return(Attr!=0xffffffff && (Attr & 0x10)!=0);
#endif
#if defined(_UNIX)
  return((Attr & 0xF000)==0x4000);
#endif
}


bool IsUnreadable(uint Attr)
{
#if defined(_UNIX) && defined(S_ISFIFO) && defined(S_ISSOCK) && defined(S_ISCHR)
  return(S_ISFIFO(Attr) || S_ISSOCK(Attr) || S_ISCHR(Attr));
#endif
  return(false);
}


bool IsLabel(uint Attr)
{
#if defined (_WIN_32) || defined(_EMX)
  return((Attr & 8)!=0);
#else
  return(false);
#endif
}


bool IsLink(uint Attr)
{
#ifdef _UNIX
  return((Attr & 0xF000)==0xA000);
#else
  return(false);
#endif
}






bool IsDeleteAllowed(uint FileAttr)
{
#if defined(_WIN_32) || defined(_EMX)
  return((FileAttr & (FA_RDONLY|FA_SYSTEM|FA_HIDDEN))==0);
#else
  return((FileAttr & (S_IRUSR|S_IWUSR))==(S_IRUSR|S_IWUSR));
#endif
}


void PrepareToDelete(const char *Name,const wchar *NameW)
{
#if defined(_WIN_32) || defined(_EMX)
  SetFileAttr(Name,NameW,0);
#endif
#ifdef _UNIX
  chmod(Name,S_IRUSR|S_IWUSR|S_IXUSR);
#endif
}


uint GetFileAttr(const char *Name,const wchar *NameW)
{
#ifdef _WIN_32
    if (WinNT() && NameW!=NULL && *NameW!=0)
      return(GetFileAttributesW(NameW));
    else
      return(GetFileAttributes(Name));
#elif defined(_DJGPP)
  return(_chmod(Name,0));
#else
  struct stat st;
  if (stat(Name,&st)!=0)
    return(0);
#ifdef _EMX
  return(st.st_attr);
#else
  return(st.st_mode);
#endif
#endif
}


bool SetFileAttr(const char *Name,const wchar *NameW,uint Attr)
{
  bool Success;
#ifdef _WIN_32
    if (WinNT() && NameW!=NULL && *NameW!=0)
      Success=SetFileAttributesW(NameW,Attr)!=0;
    else
      Success=SetFileAttributes(Name,Attr)!=0;
#elif defined(_DJGPP)
  Success=_chmod(Name,1,Attr)!=-1;
#elif defined(_EMX)
  Success=__chmod(Name,1,Attr)!=-1;
#elif defined(_UNIX)
  Success=chmod(Name,(mode_t)Attr)==0;
#else
  Success=false;
#endif
  return(Success);
}


void ConvertNameToFull(const char *Src,char *Dest)
{
#ifdef _WIN_32
#ifndef _WIN_CE
  char FullName[NM],*NamePtr;
  if (GetFullPathName(Src,sizeof(FullName),FullName,&NamePtr))
    strcpy(Dest,FullName);
  else
#endif
    if (Src!=Dest)
      strcpy(Dest,Src);
#else
  char FullName[NM];
  if (IsPathDiv(*Src) || IsDiskLetter(Src))
    strcpy(FullName,Src);
  else
  {
    if (getcwd(FullName,sizeof(FullName))==NULL)
      *FullName=0;
    else
      AddEndSlash(FullName);
    strcat(FullName,Src);
  }
  strcpy(Dest,FullName);
#endif
}


#ifndef SFX_MODULE
void ConvertNameToFull(const wchar *Src,wchar *Dest)
{
  if (Src==NULL || *Src==0)
  {
    *Dest=0;
    return;
  }
#ifdef _WIN_32
#ifndef _WIN_CE
  if (WinNT())
#endif
  {
#ifndef _WIN_CE
    wchar FullName[NM],*NamePtr;
    if (GetFullPathNameW(Src,sizeof(FullName)/sizeof(FullName[0]),FullName,&NamePtr))
      strcpyw(Dest,FullName);
    else
#endif
      if (Src!=Dest)
        strcpyw(Dest,Src);
  }
#ifndef _WIN_CE
  else
  {
    char AnsiName[NM];
    WideToChar(Src,AnsiName);
    ConvertNameToFull(AnsiName,AnsiName);
    CharToWide(AnsiName,Dest);
  }
#endif
#else
  char AnsiName[NM];
  WideToChar(Src,AnsiName);
  ConvertNameToFull(AnsiName,AnsiName);
  CharToWide(AnsiName,Dest);
#endif
}
#endif


#ifndef SFX_MODULE
char *MkTemp(char *Name)
{
  size_t Length=strlen(Name);
  if (Length<=6)
    return(NULL);
  int Random=clock();
  for (int Attempt=0;;Attempt++)
  {
    sprintf(Name+Length-6,"%06u",Random+Attempt);
    Name[Length-4]='.';
    if (!FileExist(Name))
      break;
    if (Attempt==1000)
      return(NULL);
  }
  return(Name);
}
#endif




#ifndef SFX_MODULE
uint CalcFileCRC(File *SrcFile,int64 Size,CALCCRC_SHOWMODE ShowMode)
{
  SaveFilePos SavePos(*SrcFile);
  const size_t BufSize=0x10000;
  Array<byte> Data(BufSize);
  int64 BlockCount=0;
  uint DataCRC=0xffffffff;

#if !defined(SILENT) && !defined(_WIN_CE)
  int64 FileLength=SrcFile->FileLength();
  if (ShowMode!=CALCCRC_SHOWNONE)
  {
    mprintf(St(MCalcCRC));
    mprintf("     ");
  }

#endif

  SrcFile->Seek(0,SEEK_SET);
  while (true)
  {
    size_t SizeToRead;
    if (Size==INT64NDF)   // If we process the entire file.
      SizeToRead=BufSize; // Then always attempt to read the entire buffer.
    else
      SizeToRead=(size_t)Min((int64)BufSize,Size);
    int ReadSize=SrcFile->Read(&Data[0],SizeToRead);
    if (ReadSize==0)
      break;

    ++BlockCount;
    if ((BlockCount & 15)==0)
    {
#if !defined(SILENT) && !defined(_WIN_CE)
      if (ShowMode==CALCCRC_SHOWALL)
        mprintf("\b\b\b\b%3d%%",ToPercent(BlockCount*int64(BufSize),FileLength));
#endif
      Wait();
    }
    DataCRC=CRC(DataCRC,&Data[0],ReadSize);
    if (Size!=INT64NDF)
      Size-=ReadSize;
  }
#if !defined(SILENT) && !defined(_WIN_CE)
  if (ShowMode==CALCCRC_SHOWALL)
    mprintf("\b\b\b\b    ");
#endif
  return(DataCRC^0xffffffff);
}
#endif


bool RenameFile(const char *SrcName,const wchar *SrcNameW,const char *DestName,const wchar *DestNameW)
{
  return(rename(SrcName,DestName)==0);
}


bool DelFile(const char *Name)
{
  return(DelFile(Name,NULL));
}


bool DelFile(const char *Name,const wchar *NameW)
{
  return(remove(Name)==0);
}






#if defined(_WIN_32) && !defined(_WIN_CE) && !defined(SFX_MODULE)
bool SetFileCompression(char *Name,wchar *NameW,bool State)
{
  wchar FileNameW[NM];
  GetWideName(Name,NameW,FileNameW);
  HANDLE hFile=CreateFileW(FileNameW,FILE_READ_DATA|FILE_WRITE_DATA,
                 FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,
                 FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_SEQUENTIAL_SCAN,NULL);
  if (hFile==INVALID_HANDLE_VALUE)
    return(false);
  SHORT NewState=State ? COMPRESSION_FORMAT_DEFAULT:COMPRESSION_FORMAT_NONE;
  DWORD Result;
  int RetCode=DeviceIoControl(hFile,FSCTL_SET_COMPRESSION,&NewState,
                              sizeof(NewState),NULL,0,&Result,NULL);
  CloseHandle(hFile);
  return(RetCode!=0);
}
#endif








