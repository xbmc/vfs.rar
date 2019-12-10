#include "rar.hpp"

CommandData::CommandData()
{
  FileArgs=ExclArgs=InclArgs=StoreArgs=ArcNames=NULL;
  Init();
}


CommandData::~CommandData()
{
  Close();
}


void CommandData::Init()
{
  Close();

  *Command=0;
  *ArcName=0;
  *ArcNameW=0;
  FileLists=false;
  NoMoreSwitches=false;

  FileArgs=new StringList;
  ExclArgs=new StringList;
  InclArgs=new StringList;
  StoreArgs=new StringList;
  ArcNames=new StringList;
}


void CommandData::Close()
{
  delete FileArgs;
  delete ExclArgs;
  delete InclArgs;
  delete StoreArgs;
  delete ArcNames;
  FileArgs=ExclArgs=InclArgs=StoreArgs=ArcNames=NULL;
  NextVolSizes.Reset();
}


#if !defined(SFX_MODULE)
void CommandData::ParseArg(char *Arg,wchar *ArgW)
{
  if (IsSwitch(*Arg) && !NoMoreSwitches)
    if (Arg[1]=='-')
      NoMoreSwitches=true;
    else
      ProcessSwitch(&Arg[1],(ArgW!=NULL && *ArgW!=0 ? &ArgW[1]:NULL));
  else
    if (*Command==0)
    {
      strncpyz(Command,Arg,ASIZE(Command));
      if (ArgW!=NULL)
        strncpyw(CommandW,ArgW,sizeof(CommandW)/sizeof(CommandW[0]));
      if (etoupper(*Command)=='S')
      {
        const char *SFXName=Command[1] ? Command+1:DefSFXName;
        if (PointToName(SFXName)!=SFXName || FileExist(SFXName))
          strcpy(SFXModule,SFXName);
        else
          GetConfigName(SFXName,SFXModule,true);
      }
#ifndef GUI
      *Command=etoupper(*Command);
      if (*Command!='I' && *Command!='S')
        strupper(Command);
#endif
    }
    else
      if (*ArcName==0)
      {
        strncpyz(ArcName,Arg,ASIZE(ArcName));
        if (ArgW!=NULL)
          strncpyzw(ArcNameW,ArgW,ASIZE(ArcNameW));
      }
      else
      {
        size_t Length=strlen(Arg);
        char EndChar=Length==0 ? 0:Arg[Length-1];
        char CmdChar=etoupper(*Command);
        bool Add=strchr("AFUM",CmdChar)!=NULL;
        bool Extract=CmdChar=='X' || CmdChar=='E';
        if ((IsDriveDiv(EndChar) || IsPathDiv(EndChar)) && !Add)
        {
          strncpyz(ExtrPath,Arg,ASIZE(ExtrPath));
          if (ArgW!=NULL)
            strncpyzw(ExtrPathW,ArgW,ASIZE(ExtrPathW));
        }
        else
          if ((Add || CmdChar=='T') && *Arg!='@')
            FileArgs->AddString(Arg);
          else
          {
            struct FindData FileData;
            bool Found=FindFile::FastFind(Arg,NULL,&FileData);
            if (!Found && *Arg=='@' && !IsWildcard(Arg))
            {
              FileLists=true;

              RAR_CHARSET Charset=FilelistCharset;

#if defined(_WIN_32) && !defined(GUI)
              // for compatibility reasons we use OEM encoding
              // in Win32 console version by default

              if (Charset==RCH_DEFAULT)
                Charset=RCH_OEM;
#endif

              ReadTextFile(Arg+1,FileArgs,false,true,Charset,true,true,true);
            }
            else
              if (Found && FileData.IsDir && Extract && *ExtrPath==0)
              {
                strcpy(ExtrPath,Arg);
                AddEndSlash(ExtrPath);
              }
              else
                FileArgs->AddString(Arg);
          }
      }
}
#endif


void CommandData::ParseDone()
{
  if (FileArgs->ItemsCount()==0 && !FileLists)
    FileArgs->AddString(MASKALL);
  char CmdChar=etoupper(*Command);
  bool Extract=CmdChar=='X' || CmdChar=='E';
  if (Test && Extract)
    Test=false;
  BareOutput=(CmdChar=='L' || CmdChar=='V') && Command[1]=='B';
}


#if !defined(SFX_MODULE) && !defined(_WIN_CE)
void CommandData::ParseEnvVar()
{
  char *EnvStr=getenv("RAR");
  if (EnvStr!=NULL)
    ProcessSwitchesString(EnvStr);
}
#endif



// return 'false' if -cfg- is present and preprocess switches
// which must be processed before the rest of command line

#ifndef SFX_MODULE
bool CommandData::IsConfigEnabled(int argc,char *argv[])
{
  bool ConfigEnabled=true;
  for (int I=1;I<argc;I++)
    if (IsSwitch(*argv[I]))
    {
      if (stricomp(&argv[I][1],"-")==0)
        break;
      if (stricomp(&argv[I][1],"cfg-")==0)
        ConfigEnabled=false;
#ifndef GUI
      if (strnicomp(&argv[I][1],"ilog",4)==0)
      {
        // ensure that correct log file name is already set
        // if we need to report an error when processing the command line
        ProcessSwitch(&argv[I][1]);
        InitLogOptions(LogName);
      }
#endif
      if (strnicomp(&argv[I][1],"sc",2)==0)
      {
        // Process -sc before reading any file lists.
        ProcessSwitch(&argv[I][1]);
      }
    }
  return(ConfigEnabled);
}
#endif


#if !defined(GUI) && !defined(SFX_MODULE)
void CommandData::ReadConfig(int argc,char *argv[])
{
  StringList List;
  if (ReadTextFile(DefConfigName,&List,true))
  {
    char *Str;
    while ((Str=List.GetString())!=NULL)
    {
      while (isspace(*Str))
        Str++;
      if (strnicomp(Str,"switches=",9)==0)
        ProcessSwitchesString(Str+9);
    }
  }
}
#endif


#if !defined(SFX_MODULE) && !defined(_WIN_CE)
void CommandData::ProcessSwitchesString(char *Str)
{
  while (*Str)
  {
    while (!IsSwitch(*Str) && *Str!=0)
      Str++;
    if (*Str==0)
      break;
    char *Next=Str;
    while (!(Next[0]==' ' && IsSwitch(Next[1])) && *Next!=0)
      Next++;
    char NextChar=*Next;
    *Next=0;
    ProcessSwitch(Str+1);
    *Next=NextChar;
    Str=Next;
  }
}
#endif


#if !defined(SFX_MODULE)
void CommandData::ProcessSwitch(char *Switch,wchar *SwitchW)
{

  switch(etoupper(Switch[0]))
  {
    case 'I':
      if (strnicomp(&Switch[1],"LOG",3)==0)
      {
        strncpyz(LogName,Switch[4] ? Switch+4:DefLogName,ASIZE(LogName));
        break;
      }
      if (stricomp(&Switch[1],"SND")==0)
      {
        Sound=true;
        break;
      }
      if (stricomp(&Switch[1],"ERR")==0)
      {
        MsgStream=MSG_STDERR;
        break;
      }
      if (strnicomp(&Switch[1],"EML",3)==0)
      {
        strncpyz(EmailTo,Switch[4] ? Switch+4:"@",ASIZE(EmailTo));
        EmailTo[sizeof(EmailTo)-1]=0;
        break;
      }
      if (stricomp(&Switch[1],"NUL")==0)
      {
        MsgStream=MSG_NULL;
        break;
      }
      if (etoupper(Switch[1])=='D')
      {
        for (int I=2;Switch[I]!=0;I++)
          switch(etoupper(Switch[I]))
          {
            case 'Q':
              MsgStream=MSG_ERRONLY;
              break;
            case 'C':
              DisableCopyright=true;
              break;
            case 'D':
              DisableDone=true;
              break;
            case 'P':
              DisablePercentage=true;
              break;
          }
        break;
      }
      if (stricomp(&Switch[1],"OFF")==0)
      {
        Shutdown=true;
        break;
      }
      break;
    case 'T':
      switch(etoupper(Switch[1]))
      {
        case 'K':
          ArcTime=ARCTIME_KEEP;
          break;
        case 'L':
          ArcTime=ARCTIME_LATEST;
          break;
        case 'O':
          FileTimeBefore.SetAgeText(Switch+2);
          break;
        case 'N':
          FileTimeAfter.SetAgeText(Switch+2);
          break;
        case 'B':
          FileTimeBefore.SetIsoText(Switch+2);
          break;
        case 'A':
          FileTimeAfter.SetIsoText(Switch+2);
          break;
        case 'S':
          {
            EXTTIME_MODE Mode=EXTTIME_HIGH3;
            bool CommonMode=Switch[2]>='0' && Switch[2]<='4';
            if (CommonMode)
              Mode=(EXTTIME_MODE)(Switch[2]-'0');
            if (Switch[2]=='-')
              Mode=EXTTIME_NONE;
            if (CommonMode || Switch[2]=='-' || Switch[2]=='+' || Switch[2]==0)
              xmtime=xctime=xatime=Mode;
            else
            {
              if (Switch[3]>='0' && Switch[3]<='4')
                Mode=(EXTTIME_MODE)(Switch[3]-'0');
              if (Switch[3]=='-')
                Mode=EXTTIME_NONE;
              switch(etoupper(Switch[2]))
              {
                case 'M':
                  xmtime=Mode;
                  break;
                case 'C':
                  xctime=Mode;
                  break;
                case 'A':
                  xatime=Mode;
                  break;
                case 'R':
                  xarctime=Mode;
                  break;
              }
            }
          }
          break;
        case '-':
          Test=false;
          break;
        case 0:
          Test=true;
          break;
        default:
          BadSwitch(Switch);
          break;
      }
      break;
    case 'A':
      switch(etoupper(Switch[1]))
      {
        case 'C':
          ClearArc=true;
          break;
        case 'D':
          AppendArcNameToPath=true;
          break;
        case 'G':
          if (Switch[2]=='-' && Switch[3]==0)
            GenerateArcName=0;
          else
          {
            GenerateArcName=true;
            strncpyz(GenerateMask,Switch+2,ASIZE(GenerateMask));
          }
          break;
        case 'I':
          IgnoreGeneralAttr=true;
          break;
        case 'N': //reserved for archive name
          break;
        case 'O':
          AddArcOnly=true;
          break;
        case 'P':
          strcpy(ArcPath,Switch+2);
          if (SwitchW!=NULL && *SwitchW!=0)
            strcpyw(ArcPathW,SwitchW+2);
          break;
        case 'S':
          SyncFiles=true;
          break;
        default:
          BadSwitch(Switch);
          break;
      }
      break;
    case 'D':
      if (Switch[2]==0)
        switch(etoupper(Switch[1]))
        {
          case 'S':
            DisableSortSolid=true;
            break;
          case 'H':
            OpenShared=true;
            break;
          case 'F':
            DeleteFiles=true;
            break;
        }
      break;
    case 'O':
      switch(etoupper(Switch[1]))
      {
        case '+':
          Overwrite=OVERWRITE_ALL;
          break;
        case '-':
          Overwrite=OVERWRITE_NONE;
          break;
        case 0:
          Overwrite=OVERWRITE_FORCE_ASK;
          break;
        case 'R':
          Overwrite=OVERWRITE_AUTORENAME;
          break;
        case 'W':
          ProcessOwners=true;
          break;
#ifdef SAVE_LINKS
        case 'L':
          SaveLinks=true;
          break;
#endif
#ifdef _WIN_32
        case 'S':
          SaveStreams=true;
          break;
        case 'C':
          SetCompressedAttr=true;
          break;
#endif
        default :
          BadSwitch(Switch);
          break;
      }
      break;
    case 'R':
      switch(etoupper(Switch[1]))
      {
        case 0:
          Recurse=RECURSE_ALWAYS;
          break;
        case '-':
          Recurse=RECURSE_DISABLE;
          break;
        case '0':
          Recurse=RECURSE_WILDCARDS;
          break;
#ifndef _WIN_CE
        case 'I':
          {
            Priority=atoi(Switch+2);
            char *ChPtr=strchr(Switch+2,':');
            if (ChPtr!=NULL)
            {
              SleepTime=atoi(ChPtr+1);
              InitSystemOptions(SleepTime);
            }
            SetPriority(Priority);
          }
          break;
#endif
      }
      break;
    case 'Y':
      AllYes=true;
      break;
    case 'N':
    case 'X':
      if (Switch[1]!=0)
      {
        StringList *Args=etoupper(Switch[0])=='N' ? InclArgs:ExclArgs;
        if (Switch[1]=='@' && !IsWildcard(Switch))
        {
          RAR_CHARSET Charset=FilelistCharset;

#if defined(_WIN_32) && !defined(GUI)
          // for compatibility reasons we use OEM encoding
          // in Win32 console version by default

          if (Charset==RCH_DEFAULT)
            Charset=RCH_OEM;
#endif

          ReadTextFile(Switch+2,Args,false,true,Charset,true,true,true);
        }
        else
          Args->AddString(Switch+1);
      }
      break;
    case 'E':
      switch(etoupper(Switch[1]))
      {
        case 'P':
          switch(Switch[2])
          {
            case 0:
              ExclPath=EXCL_SKIPWHOLEPATH;
              break;
            case '1':
              ExclPath=EXCL_BASEPATH;
              break;
            case '2':
              ExclPath=EXCL_SAVEFULLPATH;
              break;
            case '3':
              ExclPath=EXCL_ABSPATH;
              break;
          }
          break;
        case 'D':
          ExclEmptyDir=true;
          break;
        case 'E':
          ProcessEA=false;
          break;
        case 'N':
          NoEndBlock=true;
          break;
        default:
          if (Switch[1]=='+')
          {
            InclFileAttr=GetExclAttr(&Switch[2]);
            InclAttrSet=true;
          }
          else
            ExclFileAttr=GetExclAttr(&Switch[1]);
          break;
      }
      break;
    case 'P':
      if (Switch[1]==0)
      {
        GetPassword(PASSWORD_GLOBAL,NULL,Password,sizeof(Password));
        eprintf("\n");
      }
      else
        strncpyz(Password,Switch+1,ASIZE(Password));
      break;
    case 'H':
      if (etoupper(Switch[1])=='P')
      {
        EncryptHeaders=true;
        if (Switch[2]!=0)
          strncpyz(Password,Switch+2,ASIZE(Password));
        else
          if (*Password==0)
          {
            GetPassword(PASSWORD_GLOBAL,NULL,Password,sizeof(Password));
            eprintf("\n");
          }
      }
      break;
    case 'Z':
      strncpyz(CommentFile,Switch[1]!=0 ? Switch+1:"stdin",ASIZE(CommentFile));
      break;
    case 'M':
      switch(etoupper(Switch[1]))
      {
        case 'C':
          {
            char *Str=Switch+2;
            if (*Str=='-')
              for (int I=0;I<sizeof(FilterModes)/sizeof(FilterModes[0]);I++)
                FilterModes[I].State=FILTER_DISABLE;
            else
              while (*Str)
              {
                int Param1=0,Param2=0;
                FilterState State=FILTER_AUTO;
                FilterType Type=FILTER_NONE;
                if (isdigit(*Str))
                {
                  Param1=atoi(Str);
                  while (isdigit(*Str))
                    Str++;
                }
                if (*Str==':' && isdigit(Str[1]))
                {
                  Param2=atoi(++Str);
                  while (isdigit(*Str))
                    Str++;
                }
                switch(etoupper(*(Str++)))
                {
                  case 'T': Type=FILTER_PPM;         break;
                  case 'E': Type=FILTER_E8;          break;
                  case 'D': Type=FILTER_DELTA;       break;
                  case 'A': Type=FILTER_AUDIO;       break;
                  case 'C': Type=FILTER_RGB;         break;
                  case 'I': Type=FILTER_ITANIUM;     break;
                  case 'L': Type=FILTER_UPCASETOLOW; break;
                }
                if (*Str=='+' || *Str=='-')
                  State=*(Str++)=='+' ? FILTER_FORCE:FILTER_DISABLE;
                FilterModes[Type].State=State;
                FilterModes[Type].Param1=Param1;
                FilterModes[Type].Param2=Param2;
              }
            }
          break;
        case 'M':
          break;
        case 'D':
          {
            if ((WinSize=atoi(&Switch[2]))==0)
              WinSize=0x10000<<(etoupper(Switch[2])-'A');
            else
              WinSize*=1024;
            if (!CheckWinSize())
              BadSwitch(Switch);
          }
          break;
        case 'S':
          {
            char *Names=Switch+2,DefNames[512];
            if (*Names==0)
            {
              strcpy(DefNames,DefaultStoreList);
              Names=DefNames;
            }
            while (*Names!=0)
            {
              char *End=strchr(Names,';');
              if (End!=NULL)
                *End=0;
              if (*Names=='.')
                Names++;
              char Mask[NM];
              if (strpbrk(Names,"*?.")==NULL)
                sprintf(Mask,"*.%s",Names);
              else
                strcpy(Mask,Names);
              StoreArgs->AddString(Mask);
              if (End==NULL)
                break;
              Names=End+1;
            }
          }
          break;
#ifdef PACK_SMP
        case 'T':
          Threads=atoi(Switch+2);
          if (Threads>16)
            BadSwitch(Switch);
          else
          {
          }
          break;
#endif
        default:
          Method=Switch[1]-'0';
          if (Method>5 || Method<0)
            BadSwitch(Switch);
          break;
      }
      break;
    case 'V':
      switch(etoupper(Switch[1]))
      {
#ifdef _WIN_32
        case 'D':
          EraseDisk=true;
          break;
#endif
        case 'N':
          OldNumbering=true;
          break;
        case 'P':
          VolumePause=true;
          break;
        case 'E':
          if (etoupper(Switch[2])=='R')
            VersionControl=atoi(Switch+3)+1;
          break;
        case '-':
          VolSize=0;
          break;
        default:
          {
            int64 NewVolSize=atoil(&Switch[1]);

            if (NewVolSize==0)
              NewVolSize=INT64NDF; // Autodetecting volume size.
            else
              switch (Switch[strlen(Switch)-1])
              {
                case 'f':
                case 'F':
                  switch(NewVolSize)
                  {
                    case 360:
                      NewVolSize=362496;
                      break;
                    case 720:
                      NewVolSize=730112;
                      break;
                    case 1200:
                      NewVolSize=1213952;
                      break;
                    case 1440:
                      NewVolSize=1457664;
                      break;
                    case 2880:
                      NewVolSize=2915328;
                      break;
                  }
                  break;
                case 'k':
                  NewVolSize*=1024;
                  break;
                case 'm':
                  NewVolSize*=1024*1024;
                  break;
                case 'M':
                  NewVolSize*=1000*1000;
                  break;
                case 'g':
                  NewVolSize*=1024*1024;
                  NewVolSize*=1024;
                  break;
                case 'G':
                  NewVolSize*=1000*1000;
                  NewVolSize*=1000;
                  break;
                case 'b':
                case 'B':
                  break;
                default:
                  NewVolSize*=1000;
                  break;
              }
            if (VolSize==0)
              VolSize=NewVolSize;
            else
              NextVolSizes.Push(NewVolSize);
          }
          break;
      }
      break;
    case 'F':
      if (Switch[1]==0)
        FreshFiles=true;
      else
        BadSwitch(Switch);
      break;
    case 'U':
      if (Switch[1]==0)
        UpdateFiles=true;
      else
        BadSwitch(Switch);
      break;
    case 'W':
      strncpyz(TempPath,&Switch[1],ASIZE(TempPath));
      AddEndSlash(TempPath);
      break;
    case 'S':
      if (strnicomp(Switch,"SFX",3)==0)
      {
        const char *SFXName=Switch[3] ? Switch+3:DefSFXName;
        if (PointToName(SFXName)!=SFXName || FileExist(SFXName))
          strcpy(SFXModule,SFXName);
        else
          GetConfigName(SFXName,SFXModule,true);
      }
      if (isdigit(Switch[1]))
      {
        Solid|=SOLID_COUNT;
        SolidCount=atoi(&Switch[1]);
      }
      else
        switch(etoupper(Switch[1]))
        {
          case 0:
            Solid|=SOLID_NORMAL;
            break;
          case '-':
            Solid=SOLID_NONE;
            break;
          case 'E':
            Solid|=SOLID_FILEEXT;
            break;
          case 'V':
            Solid|=Switch[2]=='-' ? SOLID_VOLUME_DEPENDENT:SOLID_VOLUME_INDEPENDENT;
            break;
          case 'D':
            Solid|=SOLID_VOLUME_DEPENDENT;
            break;
          case 'L':
            if (isdigit(Switch[2]))
              FileSizeLess=atoil(Switch+2);
            break;
          case 'M':
            if (isdigit(Switch[2]))
              FileSizeMore=atoil(Switch+2);
            break;
          case 'C':
            {
              // Switch is already found bad, avoid reporting it several times.
              bool AlreadyBad=false;

              RAR_CHARSET rch=RCH_DEFAULT;
              switch(etoupper(Switch[2]))
              {
                case 'A':
                  rch=RCH_ANSI;
                  break;
                case 'O':
                  rch=RCH_OEM;
                  break;
                case 'U':
                  rch=RCH_UNICODE;
                  break;
                default :
                  BadSwitch(Switch);
                  AlreadyBad=true;
                  break;
              };
              if (!AlreadyBad)
                if (Switch[3]==0)
                  CommentCharset=FilelistCharset=rch;
                else
                  for (int I=3;Switch[I]!=0 && !AlreadyBad;I++)
                    switch(etoupper(Switch[I]))
                    {
                      case 'C':
                        CommentCharset=rch;
                        break;
                      case 'L':
                        FilelistCharset=rch;
                        break;
                      default:
                        BadSwitch(Switch);
                        AlreadyBad=true;
                        break;
                    }
            }
            break;

        }
      break;
    case 'C':
      if (Switch[2]==0)
        switch(etoupper(Switch[1]))
        {
          case '-':
            DisableComment=true;
            break;
          case 'U':
            ConvertNames=NAMES_UPPERCASE;
            break;
          case 'L':
            ConvertNames=NAMES_LOWERCASE;
            break;
        }
      break;
    case 'K':
      switch(etoupper(Switch[1]))
      {
        case 'B':
          KeepBroken=true;
          break;
        case 0:
          Lock=true;
          break;
      }
      break;
#ifndef GUI
    case '?' :
      OutHelp();
      break;
#endif
    default :
      BadSwitch(Switch);
      break;
  }
}
#endif


#ifndef SFX_MODULE
void CommandData::BadSwitch(char *Switch)
{
  mprintf(St(MUnknownOption),Switch);
  ErrHandler.Exit(USER_ERROR);
}
#endif


#ifndef GUI
void CommandData::OutTitle()
{
  if (BareOutput || DisableCopyright)
    return;
#if defined(__GNUC__) && defined(SFX_MODULE)
  mprintf(St(MCopyrightS));
#else
#ifndef SILENT
  static bool TitleShown=false;
  if (TitleShown)
    return;
  TitleShown=true;
  char Version[50];
  int Beta=RARVER_BETA;
  if (Beta!=0)
    sprintf(Version,"%d.%02d %s %d",RARVER_MAJOR,RARVER_MINOR,St(MBeta),RARVER_BETA);
  else
    sprintf(Version,"%d.%02d",RARVER_MAJOR,RARVER_MINOR);
#ifdef UNRAR
  mprintf(St(MUCopyright),Version,RARVER_YEAR);
#else
#endif
#endif
#endif
}
#endif


inline bool CmpMSGID(MSGID i1,MSGID i2)
{
#ifdef MSGID_INT
  return(i1==i2);
#else
  // If MSGID is const char*, we cannot compare pointers only.
  // Pointers to different instances of same strings can differ,
  // so we need to compare complete strings.
  return(strcmp(i1,i2)==0);
#endif
}

void CommandData::OutHelp()
{
#if !defined(GUI) && !defined(SILENT)
  OutTitle();
  static MSGID Help[]={
#ifdef SFX_MODULE
    // Console SFX switches definition.
    MCHelpCmd,MSHelpCmdE,MSHelpCmdT,MSHelpCmdV
#elif defined(UNRAR)
    // UnRAR switches definition.
    MUNRARTitle1,MRARTitle2,MCHelpCmd,MCHelpCmdE,MCHelpCmdL,
    MCHelpCmdP,MCHelpCmdT,MCHelpCmdV,MCHelpCmdX,MCHelpSw,
    MCHelpSwm,MCHelpSwAC,MCHelpSwAD,MCHelpSwAI,MCHelpSwAP,
    MCHelpSwCm,MCHelpSwCFGm,MCHelpSwCL,MCHelpSwCU,
    MCHelpSwDH,MCHelpSwEP,MCHelpSwEP3,MCHelpSwF,MCHelpSwIDP,MCHelpSwIERR,
    MCHelpSwINUL,MCHelpSwIOFF,MCHelpSwKB,MCHelpSwN,MCHelpSwNa,MCHelpSwNal,
    MCHelpSwO,MCHelpSwOC,MCHelpSwOR,MCHelpSwOW,MCHelpSwP,
    MCHelpSwPm,MCHelpSwR,MCHelpSwRI,MCHelpSwSL,MCHelpSwSM,MCHelpSwTA,
    MCHelpSwTB,MCHelpSwTN,MCHelpSwTO,MCHelpSwTS,MCHelpSwU,MCHelpSwVUnr,
    MCHelpSwVER,MCHelpSwVP,MCHelpSwX,MCHelpSwXa,MCHelpSwXal,MCHelpSwY
#else
    // RAR switches definition.
    MRARTitle1,MRARTitle2,MCHelpCmd,MCHelpCmdA,MCHelpCmdC,MCHelpCmdCF,
    MCHelpCmdCH,MCHelpCmdCW,MCHelpCmdD,MCHelpCmdE,MCHelpCmdF,MCHelpCmdI,
    MCHelpCmdK,MCHelpCmdL,MCHelpCmdM,MCHelpCmdP,MCHelpCmdR,MCHelpCmdRC,
    MCHelpCmdRN,MCHelpCmdRR,MCHelpCmdRV,MCHelpCmdS,MCHelpCmdT,MCHelpCmdU,
    MCHelpCmdV,MCHelpCmdX,MCHelpSw,MCHelpSwm,MCHelpSwAC,MCHelpSwAD,MCHelpSwAG,
    MCHelpSwAI,MCHelpSwAO,MCHelpSwAP,MCHelpSwAS,MCHelpSwAV,MCHelpSwAVm,
    MCHelpSwCm,MCHelpSwCFGm,MCHelpSwCL,MCHelpSwCU,MCHelpSwDF,MCHelpSwDH,
    MCHelpSwDR,MCHelpSwDS,MCHelpSwDW,MCHelpSwEa,MCHelpSwED,MCHelpSwEE,
    MCHelpSwEN,MCHelpSwEP,MCHelpSwEP1,MCHelpSwEP2,MCHelpSwEP3,MCHelpSwF,
    MCHelpSwHP,MCHelpSwIDP,MCHelpSwIEML,MCHelpSwIERR,MCHelpSwILOG,MCHelpSwINUL,
    MCHelpSwIOFF,MCHelpSwISND,MCHelpSwK,MCHelpSwKB,MCHelpSwMn,MCHelpSwMC,
    MCHelpSwMD,MCHelpSwMS,MCHelpSwMT,MCHelpSwN,MCHelpSwNa,MCHelpSwNal,
    MCHelpSwO,MCHelpSwOC,MCHelpSwOL,MCHelpSwOR,MCHelpSwOS,MCHelpSwOW,
    MCHelpSwP,MCHelpSwPm,MCHelpSwR,MCHelpSwRm,MCHelpSwR0,MCHelpSwRI,
    MCHelpSwRR,MCHelpSwRV,MCHelpSwS,MCHelpSwSm,MCHelpSwSC,MCHelpSwSFX,
    MCHelpSwSI,MCHelpSwSL,MCHelpSwSM,MCHelpSwT,MCHelpSwTA,MCHelpSwTB,
    MCHelpSwTK,MCHelpSwTL,MCHelpSwTN,MCHelpSwTO,MCHelpSwTS,MCHelpSwU,
    MCHelpSwV,MCHelpSwVn,MCHelpSwVD,MCHelpSwVER,MCHelpSwVN,MCHelpSwVP,
    MCHelpSwW,MCHelpSwX,MCHelpSwXa,MCHelpSwXal,MCHelpSwY,MCHelpSwZ
#endif
  };

  for (int I=0;I<sizeof(Help)/sizeof(Help[0]);I++)
  {
#ifndef SFX_MODULE
#ifdef DISABLEAUTODETECT
    if (Help[I]==MCHelpSwV)
      continue;
#endif
#ifndef _WIN_32
    static MSGID Win32Only[]={
      MCHelpSwIEML,MCHelpSwVD,MCHelpSwAO,MCHelpSwOS,MCHelpSwIOFF,
      MCHelpSwEP2,MCHelpSwOC,MCHelpSwDR,MCHelpSwRI
    };
    bool Found=false;
    for (int J=0;J<sizeof(Win32Only)/sizeof(Win32Only[0]);J++)
      if (CmpMSGID(Help[I],Win32Only[J]))
      {
        Found=true;
        break;
      }
    if (Found)
      continue;
#endif
#if !defined(_UNIX) && !defined(_WIN_32)
    if (CmpMSGID(Help[I],MCHelpSwOW))
      continue;
#endif
#if !defined(_WIN_32) && !defined(_EMX)
    if (CmpMSGID(Help[I],MCHelpSwAC))
      continue;
#endif
#ifndef SAVE_LINKS
    if (CmpMSGID(Help[I],MCHelpSwOL))
      continue;
#endif
#ifndef PACK_SMP
    if (CmpMSGID(Help[I],MCHelpSwMT))
      continue;
#endif
#ifndef _BEOS
    if (CmpMSGID(Help[I],MCHelpSwEE))
    {
#if defined(_EMX) && !defined(_DJGPP)
      if (_osmode != OS2_MODE)
        continue;
#else
      continue;
#endif
    }
#endif
#endif
    mprintf(St(Help[I]));
  }
  mprintf("\n");
  ErrHandler.Exit(USER_ERROR);
#endif
}


bool CommandData::ExclCheckArgs(StringList *Args,char *CheckName,bool CheckFullPath,int MatchMode)
{
  char *Name=ConvertPath(CheckName,NULL);
  char FullName[NM],*CurName;
  *FullName=0;
  Args->Rewind();
  while ((CurName=Args->GetString())!=NULL)
#ifndef SFX_MODULE
    if (CheckFullPath && IsFullPath(CurName))
    {
      if (*FullName==0)
        ConvertNameToFull(CheckName,FullName);
      if (CmpName(CurName,FullName,MatchMode))
        return(true);
    }
    else
#endif
      if (CmpName(ConvertPath(CurName,NULL),Name,MatchMode))
        return(true);
  return(false);
}


bool CommandData::ExclCheck(char *CheckName,bool CheckFullPath)
{
  if (ExclCheckArgs(ExclArgs,CheckName,CheckFullPath,MATCH_WILDSUBPATH))
    return(true);
  if (InclArgs->ItemsCount()==0)
    return(false);
  if (ExclCheckArgs(InclArgs,CheckName,false,MATCH_WILDSUBPATH))
    return(false);
  return(true);
}




#ifndef SFX_MODULE
bool CommandData::TimeCheck(RarTime &ft)
{
  if (FileTimeBefore.IsSet() && ft>=FileTimeBefore)
    return(true);
  if (FileTimeAfter.IsSet() && ft<=FileTimeAfter)
    return(true);
  return(false);
}
#endif


#ifndef SFX_MODULE
bool CommandData::SizeCheck(int64 Size)
{
  if (FileSizeLess!=INT64NDF && Size>=FileSizeLess)
    return(true);
  if (FileSizeMore!=INT64NDF && Size<=FileSizeMore)
    return(true);
  return(false);
}
#endif




int CommandData::IsProcessFile(FileHeader &NewLhd,bool *ExactMatch,int MatchType)
{
  if (strlen(NewLhd.FileName)>=NM || strlenw(NewLhd.FileNameW)>=NM)
    return(0);
  if (ExclCheck(NewLhd.FileName,false))
    return(0);
#ifndef SFX_MODULE
  if (TimeCheck(NewLhd.mtime))
    return(0);
  if ((NewLhd.FileAttr & ExclFileAttr)!=0 || InclAttrSet && (NewLhd.FileAttr & InclFileAttr)==0)
    return(0);
  if ((NewLhd.Flags & LHD_WINDOWMASK)!=LHD_DIRECTORY && SizeCheck(NewLhd.FullUnpSize))
    return(0);
#endif
  char *ArgName;
  wchar *ArgNameW;
  FileArgs->Rewind();
  for (int StringCount=1;FileArgs->GetString(&ArgName,&ArgNameW);StringCount++)
  {
#ifndef SFX_MODULE
    bool Unicode=(NewLhd.Flags & LHD_UNICODE) || ArgNameW!=NULL;
    if (Unicode)
    {
      wchar NameW[NM],ArgW[NM],*NamePtr=NewLhd.FileNameW;
      bool CorrectUnicode=true;
      if (ArgNameW==NULL)
      {
        if (!CharToWide(ArgName,ArgW) || *ArgW==0)
          CorrectUnicode=false;
        ArgNameW=ArgW;
      }
      if ((NewLhd.Flags & LHD_UNICODE)==0)
      {
        if (!CharToWide(NewLhd.FileName,NameW) || *NameW==0)
          CorrectUnicode=false;
        NamePtr=NameW;
      }
      if (CmpName(ArgNameW,NamePtr,MatchType))
      {
        if (ExactMatch!=NULL)
          *ExactMatch=stricompcw(ArgNameW,NamePtr)==0;
        return(StringCount);
      }
      if (CorrectUnicode)
        continue;
    }
#endif
    if (CmpName(ArgName,NewLhd.FileName,MatchType))
    {
      if (ExactMatch!=NULL)
        *ExactMatch=stricompc(ArgName,NewLhd.FileName)==0;
      return(StringCount);
    }
  }
  return(0);
}


#ifndef GUI
void CommandData::ProcessCommand()
{
#ifndef SFX_MODULE

  const char *SingleCharCommands="FUADPXETK";
  if (Command[1] && strchr(SingleCharCommands,*Command)!=NULL || *ArcName==0)
    OutHelp();

#ifdef _UNIX
  if (GetExt(ArcName)==NULL && (!FileExist(ArcName) || IsDir(GetFileAttr(ArcName))))
    strcat(ArcName,".rar");
#else
  if (GetExt(ArcName)==NULL)
    strcat(ArcName,".rar");
#endif

  if (strchr("AFUMD",*Command)==NULL)
  {
    StringList ArcMasks;
    ArcMasks.AddString(ArcName);
    ScanTree Scan(&ArcMasks,Recurse,SaveLinks,SCAN_SKIPDIRS);
    FindData FindData;
    while (Scan.GetNext(&FindData)==SCAN_SUCCESS)
      AddArcName(FindData.Name,FindData.NameW);
  }
  else
    AddArcName(ArcName,NULL);
#endif

  switch(Command[0])
  {
    case 'P':
    case 'X':
    case 'E':
    case 'T':
    case 'I':
      {
        CmdExtract Extract;
        Extract.DoExtract(this);
      }
      break;
#ifndef SILENT
    case 'V':
    case 'L':
      ListArchive(this);
      break;
    default:
      OutHelp();
#endif
  }
  if (!BareOutput)
    mprintf("\n");
}
#endif


void CommandData::AddArcName(char *Name,wchar *NameW)
{
  ArcNames->AddString(Name,NameW);
}


bool CommandData::GetArcName(char *Name,wchar *NameW,int MaxSize)
{
  if (!ArcNames->GetString(Name,NameW,NM))
    return(false);
  return(true);
}


bool CommandData::IsSwitch(int Ch)
{
#if defined(_WIN_32) || defined(_EMX)
  return(Ch=='-' || Ch=='/');
#else
  return(Ch=='-');
#endif
}


#ifndef SFX_MODULE
uint CommandData::GetExclAttr(char *Str)
{
  if (isdigit(*Str))
    return(strtol(Str,NULL,0));
  else
  {
    uint Attr;
    for (Attr=0;*Str;Str++)
      switch(etoupper(*Str))
      {
#ifdef _UNIX
        case 'D':
          Attr|=S_IFDIR;
          break;
        case 'V':
          Attr|=S_IFCHR;
          break;
#elif defined(_WIN_32) || defined(_EMX)
        case 'R':
          Attr|=0x1;
          break;
        case 'H':
          Attr|=0x2;
          break;
        case 'S':
          Attr|=0x4;
          break;
        case 'D':
          Attr|=0x10;
          break;
        case 'A':
          Attr|=0x20;
          break;
#endif
      }
    return(Attr);
  }
}
#endif




#ifndef SFX_MODULE
bool CommandData::CheckWinSize()
{
  static int ValidSize[]={
    0x10000,0x20000,0x40000,0x80000,0x100000,0x200000,0x400000
  };
  for (int I=0;I<sizeof(ValidSize)/sizeof(ValidSize[0]);I++)
    if (WinSize==ValidSize[I])
      return(true);
  WinSize=0x400000;
  return(false);
}
#endif
