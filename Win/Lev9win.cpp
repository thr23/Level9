/***********************************************************************\
*
* Level 9 interpreter
* Version 5.2
* Copyright (c) 1996-2025 Glen Summers and contributors.
* Contributions from David Kinder, Alan Staniforth, Simon Baldwin,
* Dieter Baron and Andreas Scherrer.
*
* Level9 32 bit Windows version by Glen Summers and David Kinder,
* with contributions from Stefano Bodrato.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA.
*
\***********************************************************************/

#include <mywin.h>
#pragma hdrstop
#include <htmlhelp.h>
#include <ctype.h>

#include "level9.h"

extern "C" {
extern L9BYTE* startdata;
extern L9UINT32 FileSize;
void show_picture(int pic);
extern L9BYTE* gfxa5;
extern int gintcolour,option,reflectflag,scale;
extern int drawx,drawy;
extern int l9textmode,screencalled,gfx_mode;
extern int GfxA5StackPos,GfxScaleStackPos;
}

// define application name, main window title
#define AppName "Level9"
#define MainWinTitle "Level9"
 
// help file name and ini file are set from AppName
char HelpFileName[] = AppName".chm";

#ifdef WIN16
char Ini[] = AppName".ini";
#else
char Ini[] = "Software\\Level 9\\Interpreter";
#endif

#ifdef __BORLANDC__
#include "level9.rh"
#endif

#ifdef _MSC_VER
#include "resource.h"
#endif

String Output="";
int Line=0;
int LineOffset=0;
int LineStart=0;
int LastWordEnd=0;
HWND hWndMain=0;
HRGN hClip=0;
int GfxMode=0;
int GfxHeight=0;
int GfxPicWidth=0,GfxPicHeight=0;
BOOL GfxDither=FALSE;
BOOL GfxFitToWindow=FALSE;
BOOL GfxPreserveAspect=FALSE;
HBITMAP hGfx=0,hGfxDraw=0;
HDC hGfxDC=0,hGfxDrawDC=0;
int FontHeight=0,LineSpacing=0;
LOGFONT lf;
HFONT Font=0;
COLORREF FontColour,BackColour;
UINT dpi=0;
int PageWidth=0,PageHeight=0,WndHeight=0;
int Margin=0;
SimpleList<int> InputChars;
int iPos=0,Input=0;
String Hash(20);
FName LastFile;
FName GfxDir;
BitmapType GfxBmapType = NO_BITMAPS;
BYTE* GfxBits = NULL;
int GfxBmapWidth = 0, GfxBmapHeight = 0;
int LastBitmap = -1, DelayBitmap = 0;

static HBITMAP ErikPanelLeft = 0;
static HBITMAP ErikPanelRight = 0;
static int ErikPanelCachedPanelW=0,ErikPanelCachedBarW=0,ErikPanelCachedH=0;
static BOOL ErikCapturing = FALSE;

/*#define L9PRINT*/

void LogPrint(char *Str,int Len)
{
#ifdef L9PRINT
  static FILE* log = NULL;
  if (log == NULL)
    log = fopen("c:\\temp\\level9.txt","wt");
  fprintf(log,"%.*s\n",Len,Str);
#endif
}

void DrawPicture(void);

static BYTE* ErikPanelBits = NULL;
static int ErikPanelWidth = 0, ErikPanelHeight = 0;

#define LineGfxPreserveAspect() (GfxMode == 1 && GfxPreserveAspect && GfxPicWidth > 0 && GfxPicHeight > 0)
#define ErikDelBmp(b) do { if (b) { DeleteObject(b); (b) = 0; } } while (0)
#define ClampPictureVerticallyInGfx(y,h) do { if ((y) < 0) (y) = 0; if ((y)+(h) > GfxHeight) (h) = max(1, GfxHeight - (y)); } while (0)

void DisplayLine(int Line,char *Str,int Len)
{
  HDC dc=GetDC(hWndMain);
  HFONT OldFont=(HFONT) SelectObject(dc,Font);
  COLORREF OldCol=SetTextColor(dc,FontColour);
  COLORREF OldBk=SetBkColor(dc,BackColour);
  if(hClip) SelectClipRgn(dc,hClip);
  TextOut(dc,Margin,Line*LineSpacing-LineOffset+GfxHeight,Str,Len);
  SelectObject(dc,OldFont);
  SetTextColor(dc,OldCol);
  SetBkColor(dc,OldBk);
  ReleaseDC(hWndMain,dc);
}

void DisplayLineJust(int Line,char *Str,int Len)
{
  HDC dc=GetDC(hWndMain);
  HFONT OldFont=(HFONT) SelectObject(dc,Font);
  COLORREF OldCol=SetTextColor(dc,FontColour);
  COLORREF OldBk=SetBkColor(dc,BackColour);
  if(hClip) SelectClipRgn(dc,hClip);

  SIZE Size;
#ifdef WIN32
  GetTextExtentPoint32(dc,Str,Len,&Size);
#else
  GetTextExtentPoint(dc,Str,Len,&Size);
#endif

  // count spaces
  int nBreaks=0;
  char *Ptr=Str;
  for (int i=0;i<Len;i++) if (*Ptr++==' ') nBreaks++;
  if (nBreaks) SetTextJustification(dc,PageWidth-Size.cx-2*Margin,nBreaks);

  TextOut(dc,Margin,Line*LineSpacing-LineOffset+GfxHeight,Str,Len);

  SelectObject(dc,OldFont);
  SetTextColor(dc,OldCol);
  SetBkColor(dc,OldBk);
  ReleaseDC(hWndMain,dc);
}

int LineLength(char*str,int n)
{
  SIZE Size;
  HDC dc=GetDC(hWndMain);
  HFONT OldFont=(HFONT) SelectObject(dc,Font);
#ifdef WIN32
  GetTextExtentPoint32(dc,str,n,&Size);
#else
  GetTextExtentPoint(dc,str,n,&Size);
#endif
  SelectObject(dc,OldFont);
  ReleaseDC(hWndMain,dc);
  return Size.cx;
}

BOOL Caret=FALSE;
int Cursorx,Cursory;

void MakeCaret()
{
  if (GetFocus()==hWndMain && Caret)
  {
    CreateCaret(hWndMain,NULL,2,FontHeight);
    SetCaretPos(Cursorx,Cursory+GfxHeight);
    ShowCaret(hWndMain);
  }
}

void KillCaret()
{
  if (Caret) DestroyCaret();
}

void SetCaret(int x,int y)
{
  Cursorx=x;
  Cursory=y;
  SetCaretPos(x,y+GfxHeight);
}

void Wait(int millis)
{
  if (hGfxDC)
  {
    int gfx_status = TRUE;
    int gfx_count = 0;
    int gfx_limit = 2*millis;

    while (gfx_status && (gfx_count < gfx_limit))
    {
      gfx_status = RunGraphics();
      if (gfx_status)
        gfx_count++;
    }

    if (gfx_count > 0)
      DrawPicture();
  }

#ifdef WIN32
  Sleep(millis);
#endif
}

char os_readchar(int millis)
{
  SimpleNode<int> *C;
  App::PeekLoop();
  if ((C=InputChars.GetFirst())==NULL)
  {
    Wait(millis);
    App::PeekLoop();
    if ((C=InputChars.GetFirst())==NULL) return 0;
  }
  char InputChar=(char)C->Item;
  delete C;
  return InputChar;
}

L9BOOL os_stoplist(void)
{
  return (os_readchar(0) != 0);
}

SimpleList<String> History;
int HistoryLines=0;
#define NUMHIST 20

void Erase(int Start,int End)
{
  RECT rc;
  rc.left=Margin+Start;
  rc.right=Margin+End;
  rc.top=Line*LineSpacing-LineOffset+GfxHeight;
  rc.bottom=Line*LineSpacing+FontHeight-LineOffset+GfxHeight;
  HDC dc=GetDC(hWndMain);
  if(hClip) SelectClipRgn(dc,hClip);
#ifdef WIN32
  FillRect(dc,&rc,(HBRUSH) GetClassLong(hWndMain,GCL_HBRBACKGROUND));
#else
  FillRect(dc,&rc,(HBRUSH) GetClassLong(hWndMain,GCW_HBRBACKGROUND));
#endif
  ReleaseDC(hWndMain,dc);
}

void CancelInput()
{
  if (Caret) InputChars.AddTail(-1);
}

void HashCommand(char *h)
{
  if (Caret)
  {
    Hash=h;
    InputChars.AddTail(-2);
  }
}

BOOL os_input(char*ibuff,int size)
{
  slIterator<String> HistPtr(History);
  if (HistPtr()) HistPtr--; // force out

  Input=Output.Len();
  iPos=0;
  Caret=TRUE;
  MakeCaret();
  SetCaret(Margin+LineLength(Output+LineStart,Input-LineStart),Line*LineSpacing-LineOffset);

  while (TRUE)
  {
    SimpleNode<int> *C;
    while ((C=InputChars.GetFirst())==NULL)
    {
      Wait(20);
      App::PeekLoop();
    }
    int InputChar=C->Item;
    delete C;
    if (InputChar<0)
    {
      strcpy(ibuff,Hash);
      KillCaret();
      Caret=FALSE;
      return InputChar<-1;
    }
    else if (InputChar=='\r')
    {
      strcpy(ibuff,Output+Input); // >500 chrs??
      KillCaret();
      Caret=FALSE;
      HistPtr.Last();
      if (*ibuff && (!HistPtr() || HistPtr.Get()!=ibuff))
      {
        History.AddTailRef(String(ibuff));
        if (HistoryLines==NUMHIST)
        delete History.GetFirst();
        else HistoryLines++;
      }
      return TRUE;
    }
    else switch (InputChar)
    {
      case 256+VK_DELETE:
        if (iPos>=Output.Len()-Input) break;
        iPos++;
      case 8:
        if (iPos>0)
        {
          int OldLen=LineLength(Output+LineStart,Output.Len()-LineStart);
          Output.Remove(Input+--iPos,1);
          int Len=LineLength(Output+LineStart,Output.Len()-LineStart);
          Erase(Len,OldLen);
        }
        break;
      case 256+VK_UP:
        if (!HistPtr()) HistPtr.Last();
        else
        {
          --HistPtr;
          if (!HistPtr()) HistPtr.First();
        }
        if (HistPtr())
        {
          int OldLen=LineLength(Output+LineStart,Output.Len()-LineStart);
          Output.Len(Input);
          Output.Insert(HistPtr.Get(),Input);
          iPos=Output.Len()-Input;
          int Len=LineLength(Output+LineStart,Output.Len()-LineStart);
          Erase(Len,OldLen);
        }
        break;
      case 256+VK_DOWN:
        if (HistPtr())
        {
          ++HistPtr;
          if (!HistPtr()) HistPtr.Last();
          else
          {
            int OldLen=LineLength(Output+LineStart,Output.Len()-LineStart);
            Output.Len(Input);
            Output.Insert(HistPtr.Get(),Input);
            iPos=Output.Len()-Input;
            int Len=LineLength(Output+LineStart,Output.Len()-LineStart);
            Erase(Len,OldLen);
          }
        }
        break;
      case 256+VK_LEFT:
        if (iPos>0) iPos--;
        break;
      case 256+VK_RIGHT:
        if (iPos<Output.Len()-Input) iPos++;
        break;
      case 256+VK_END:
        iPos=Output.Len()-Input;
        break;
      case 256+VK_HOME:
        iPos=0;
        break;
      case 26: // escape (clear?)
        break;

      default:
        // insert char at Pos;
        if (InputChar<256) Output.Insert((char)InputChar,Input+iPos++);
        break;
    }

    HideCaret(hWndMain);
    DisplayLine(Line,Output+LineStart,Output.Len()-LineStart);
    SetCaret(Margin+LineLength(Output+LineStart,Input-LineStart+iPos),Line*LineSpacing-LineOffset);
    ShowCaret(hWndMain);
  }
}

int FindLineLength(int LineStart)
{
  int LastWordEnd=0;
  int Pos=LineStart;
  char c;
  while (TRUE)
  {
    c= Output[Pos++];
    if (c=='\r' || c==' ' || c==0)
    {
      if (LineLength((char*)Output+LineStart,Pos-LineStart-1)>PageWidth-2*Margin) return LastWordEnd+1;
      else if (c==0) return Pos-1;
      else if (c=='\r') return Pos;
      LastWordEnd=Pos-LineStart-1;
    }
  }
}

#define SCROLLBACK 2000

void NewLine()
{
  while (Output.Len()>SCROLLBACK)
  {
    int Len=FindLineLength(0);
    Output.Remove(0,Len);
    LineStart-=Len;
    LineOffset-=LineSpacing;
    Line--;
  }
  Line++;

  if (Line*LineSpacing+FontHeight-LineOffset>PageHeight)
  {
    int Shift=Line*LineSpacing+FontHeight-LineOffset-PageHeight;
    RECT rc;
    rc.left=0;
    rc.top=GfxHeight;
    rc.right=PageWidth;
    rc.bottom=PageHeight+GfxHeight;
    ScrollWindow(hWndMain,0,-Shift,NULL,&rc);
    LineOffset+=Shift;
    UpdateWindow(hWndMain);
  }
}

void os_flush()
{
  if (LineLength((char*)Output+LineStart,Output.Len()-LineStart)>PageWidth-2*Margin)
  {
    LogPrint((char*) Output+LineStart,LastWordEnd);
    DisplayLineJust(Line,(char*) Output+LineStart,LastWordEnd);
    LineStart+=LastWordEnd+1;
    NewLine();
  }
  DisplayLine(Line,(char*) Output+LineStart,Output.Len()-LineStart);
  LastWordEnd=Output.Len()-LineStart;
}

void os_printchar(char c)
{
  Output << c;
  if (c=='\r' || c==' ')
  {
    if (LineLength((char*)Output+LineStart,Output.Len()-LineStart-1)>PageWidth-2*Margin)
    {
      LogPrint((char*) Output+LineStart,LastWordEnd);
      DisplayLineJust(Line,(char*) Output+LineStart,LastWordEnd);
      LineStart+=LastWordEnd+1;
      NewLine();
    }
    if (c=='\r')
    {
      LogPrint((char*) Output+LineStart,Output.Len()-LineStart-1);
      DisplayLine(Line,(char*) Output+LineStart,Output.Len()-LineStart-1);
      LineStart=Output.Len();
      NewLine();
    }
    LastWordEnd=Output.Len()-LineStart-1;
  }
}

void Redraw()
{
  int l=0;
  int LineStart=0,LastWordEnd=0;
  int Pos=0;
  char c;
  do
  {
    c= Output[Pos++];
    if (c=='\r' || c==' ' || c==0)
    {
      if (LineLength((char*)Output+LineStart,Pos-LineStart-1)>PageWidth-2*Margin)
      {
        DisplayLineJust(l,(char*) Output+LineStart,LastWordEnd);
        LineStart+=LastWordEnd+1;
        l++;
      }
      if (c=='\r' || c==0)
      {
        DisplayLine(l,(char*) Output+LineStart,Pos-LineStart-1);
        if (c=='\r')
        {
          LineStart=Pos;
          l++;
        }
      }
      LastWordEnd=Pos-LineStart-1;
    }
  } while (c);
  DrawPicture();
}

void Paginate()
{
  int l=0;
  int LineStart=0,LastWordEnd=0;
  int Pos=0;
  char c;
  do
  {
    c= Output[Pos++];
    if (c=='\r' || c==' ' || c==0)
    {
      if (LineLength((char*)Output+LineStart,Pos-LineStart-1)>PageWidth-2*Margin)
      {
        LineStart+=LastWordEnd+1;
        l++;
      }
      if (c=='\r')
      {
        LineStart=Pos;
        l++;
      }
      LastWordEnd=Pos-LineStart-1;
    }
  } while (c);
  Line=l;
  LineOffset=max(0,Line*LineSpacing+FontHeight-PageHeight);

  if (Caret) SetCaret(Margin+LineLength(Output+LineStart,Input-LineStart+iPos),Line*LineSpacing-LineOffset);
}

void Resize()
{
  PageHeight = WndHeight;
  if (GfxMode)
  {
    GfxHeight = WndHeight/2;
    PageHeight -= GfxHeight;
  }
  else
    GfxHeight = 0;

  if (hClip)
    DeleteObject(hClip);
  hClip=CreateRectRgn(0,GfxHeight,PageWidth,WndHeight);

  if (hGfxDrawDC)
    DeleteDC(hGfxDrawDC);
  hGfxDrawDC = 0;
  if (hGfxDraw)
    DeleteObject(hGfxDraw);
  hGfxDraw = 0;

  Paginate();
  InvalidateRect(hWndMain,NULL,TRUE);
}

const char Filters[]=
  "All Supported Files (*.dat;*.l9;*.sna)\0*.dat;*.l9;*.sna\0"
  "Level 9 Game Files (*.dat;*.l9)\0*.dat;*.l9\0"
  "Spectrum Snapshots (*.sna)\0*.sna\0"
  "All Files (*.*)\0*.*\0\0";
int FiltIndex;
const char GameFilters[]=
  "Saved game file (*.sav)\0*.sav\0"
  "All Files (*.*)\0*.*\0\0";
FName LastGameFile;
int GameFiltIndex;

void os_set_filenumber(char *NewName,int Size,int n)
{
  FName fn(NewName);
  String S;
  fn.GetBaseName(S);
  while (isdigit(S.Last())) S.Remove(S.Len()-1,1);
  fn.NewBaseName(S << n);
  strcpy(NewName,fn);
}

BOOL os_get_game_file(char* Name,int Size)
{
  return CustFileDlg(App::MainWindow,-1,Name,Size,"Load Level 9 Game File",Filters,&FiltIndex).Execute(FALSE);
}

BOOL os_save_file(BYTE *Ptr,int Bytes)
{
  CancelInput();
  if (CustFileDlg(App::MainWindow,-1,LastGameFile,LastGameFile.Size(),"Save Current Position",GameFilters,&GameFiltIndex,OFN_OVERWRITEPROMPT | OFN_EXPLORER).Execute(FALSE))
  {
    LastGameFile.Update();
    if (!LastGameFile.GetExt()) LastGameFile.NewExt("sav");

    FILE *f=fopen(LastGameFile,"wb");
    if (f)
    {
      fwrite(Ptr,1,Bytes,f);
      fclose(f);
      return TRUE;
    }
  }
  return FALSE;
}

BOOL os_load_file(BYTE *Ptr,int *Bytes,int Max)
{
  CancelInput();
  if (CustFileDlg(App::MainWindow,-1,LastGameFile,LastGameFile.Size(),"Restore Saved Position",GameFilters,&GameFiltIndex,OFN_FILEMUSTEXIST | OFN_EXPLORER).Execute(FALSE))
  {
    LastGameFile.Update();
    FILE *f=fopen(LastGameFile,"rb");
    if (f)
    {
      *Bytes=filelength(f);
      if (*Bytes>Max)
        MessageBox(App::MainWindow->hWnd,"Not a valid saved game file","Load Error",MB_OK | MB_ICONEXCLAMATION);
      else
      {
        fread(Ptr,1,*Bytes,f);
        fclose(f);
        return TRUE;
      }
    }
  }
  return FALSE;
}

const char AllFilters[]="All Files (*.*)\0*.*\0\0";
FName LastScriptFile;
int ScriptFiltIndex;

FILE* os_open_script_file(void)
{
  CancelInput();
  if (CustFileDlg(App::MainWindow,-1,LastScriptFile,LastScriptFile.Size(),"Play Back Script File",AllFilters,&ScriptFiltIndex,OFN_FILEMUSTEXIST | OFN_EXPLORER).Execute(FALSE))
  {
    LastScriptFile.Update();
    return fopen(LastScriptFile,"rt");
  }
  return FALSE;
}

L9BOOL os_find_file(char* NewName)
{
  return (GetFileAttributes(NewName) != INVALID_FILE_ATTRIBUTES) ? TRUE : FALSE;
}

typedef struct tagBITMAPINFO32
{
  BITMAPINFOHEADER bmiHeader;
  RGBQUAD bmiColors[32];
}
BITMAPINFO32;

RGBQUAD Colours[8] =
{
  { 0x00,0x00,0x00,0 },
  { 0x00,0x00,0xFF,0 },
  { 0x30,0xE8,0x30,0 },
  { 0x00,0xFF,0xFF,0 },
  { 0xFF,0x00,0x00,0 },
  { 0x00,0x68,0xA0,0 },
  { 0xFF,0xFF,0x00,0 },
  { 0xFF,0xFF,0xFF,0 }
};
RGBQUAD Palette[32];

COLORREF GetIndexColour(int index)
{
  int x = 8*index;
  return RGB(x,x,x);
}

void SetIndexPalette(RGBQUAD* pal)
{
  for (int i = 0; i < 32; i++)
  {
    pal[i].rgbBlue = 8*i;
    pal[i].rgbGreen = 8*i;
    pal[i].rgbRed = 8*i;
  }
}

static HBITMAP CreateIndexedDib(HDC dc,int width,int height,BYTE** outBits)
{
  BITMAPINFO32 info;
  ZeroMemory(&info,sizeof(BITMAPINFO32));
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 8;
  info.bmiHeader.biCompression = BI_RGB;
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;
  info.bmiHeader.biClrUsed = 32;
  info.bmiHeader.biClrImportant = 32;
  SetIndexPalette(info.bmiColors);
  return CreateDIBSection(dc,(BITMAPINFO*)&info,DIB_RGB_COLORS,(VOID**)outBits,NULL,0);
}

static void DrawMainWithGutters(HDC dc,HBRUSH bkBr,HDC srcDC,
  int gutterL,int gutterR,int mx,int my,int mw,int mh)
{
  int bot = my + mh;
  if (my > 0)
  {
    RECT r = {0,0,PageWidth,my};
    FillRect(dc,&r,bkBr);
  }
  if (bot < GfxHeight)
  {
    RECT r = {0,bot,PageWidth,GfxHeight};
    FillRect(dc,&r,bkBr);
  }
  if (mh > 0)
  {
    if (gutterL > 0)
    {
      RECT r = {0,my,gutterL,bot};
      FillRect(dc,&r,bkBr);
    }
    if (gutterR < PageWidth)
    {
      RECT r = {gutterR,my,PageWidth,bot};
      FillRect(dc,&r,bkBr);
    }
    BitBlt(dc,mx,my,mw,mh,srcDC,mx,my,SRCCOPY);
  }
}

static double MinScaleToFit(double availW, double availH, double srcW, double srcH)
{
  double sW = availW / srcW;
  double sH = availH / srcH;
  return sW < sH ? sW : sH;
}

#define ScalePictureToBox(BW,BH,SW,SH,PW,PH) do { \
  double _s = MinScaleToFit((double)(BW),(double)(BH),(double)(SW),(double)(SH)); \
  *(PW) = max(1,(int)((SW) * _s + 0.5)); \
  *(PH) = max(1,(int)((SH) * _s + 0.5)); \
  if (*(PW) > (BW)) *(PW) = (BW); \
  if (*(PH) > (BH)) *(PH) = (BH); \
} while (0)

static BOOL L9IsErik(void)
{
  int i;
  int isV2;
  L9UINT16 len;
  L9BYTE sum;

  if (!startdata || FileSize < 30)
    return FALSE;

  isV2 = ((startdata[4] == 0x20 && startdata[5] == 0x00)
          || (startdata[6] == 0x20 && startdata[7] == 0x00))
      && ((startdata[10] == 0x00 && startdata[11] == 0x80)
          || (startdata[8] == 0x00 && startdata[9] == 0x80))
      && startdata[20] == startdata[22]
      && startdata[21] == startdata[23];

  len = isV2 ? (L9UINT16)(startdata[28] | (startdata[29] << 8))
             : (L9UINT16)(startdata[0] | (startdata[1] << 8));

  if (len >= FileSize)
    return FALSE;

  if (isV2)
  {
    sum = 0;
    for (i = 0; i < (int)len + 1; i++)
      sum += startdata[i];
  }
  else
    sum = startdata[len];

  return len == 0x34b3 && sum == 0x53;
}

static void FreeErikPanelResources(void)
{
  ErikDelBmp(ErikPanelLeft);
  ErikDelBmp(ErikPanelRight);

  if (ErikPanelBits)
    free(ErikPanelBits);
  ErikPanelBits = NULL;
  ErikPanelWidth = 0;
  ErikPanelHeight = 0;

  ErikPanelCachedPanelW = 0;
  ErikPanelCachedBarW = 0;
  ErikPanelCachedH = 0;
}

static void ErikPanelStretchStripDib(HDC destDC,
  int destX, int destY, int destWidth, int destHeight,
  int capturedPixelW, int capturedPixelH,
  int coverWidth, int coverHeight,
  BYTE* dibBits, BITMAPINFO* dibInfo)
{
  double coverScaleW = (double)coverWidth / capturedPixelW;
  double coverScaleH = (double)coverHeight / capturedPixelH;
  double coverScale = coverScaleH > coverScaleW ? coverScaleH : coverScaleW;
  int scaledTotalW = max(1,(int)(capturedPixelW * coverScale + 0.5));
  int scaledTotalH = max(1,(int)(capturedPixelH * coverScale + 0.5));
  int cropOriginX = scaledTotalW > coverWidth ? (scaledTotalW - coverWidth) / 2 : 0;
  int cropOriginY = scaledTotalH > coverHeight ? (scaledTotalH - coverHeight) / 2 : 0;
  int srcX = (cropOriginX * capturedPixelW) / scaledTotalW;
  int srcY = (cropOriginY * capturedPixelH) / scaledTotalH;
  int srcW = (coverWidth * capturedPixelW + scaledTotalW / 2) / scaledTotalW;
  int srcH = (coverHeight * capturedPixelH + scaledTotalH / 2) / scaledTotalH;
  if (srcW < 1) srcW = 1;
  if (srcH < 1) srcH = 1;
  if (srcX + srcW > capturedPixelW) srcW = capturedPixelW - srcX;
  if (srcY + srcH > capturedPixelH) srcH = capturedPixelH - srcY;
  if (srcW < 1 || srcH < 1) return;
  StretchDIBits(destDC,destX,destY,destWidth,destHeight,srcX,srcY,srcW,srcH,
    dibBits,dibInfo,DIB_RGB_COLORS,SRCCOPY);
}

static void DrawErikPanelStrip(HDC dc,int x,int y,int width,int height)
{
  if (!ErikPanelBits || width <= 0 || height <= 0)
    return;

  BITMAPINFO info;
  ZeroMemory(&info,sizeof(info));
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  int capturedPixelW = ErikPanelWidth, capturedPixelH = ErikPanelHeight;
  if (capturedPixelW <= 0 || capturedPixelH <= 0)
    return;

  info.bmiHeader.biWidth = capturedPixelW;
  info.bmiHeader.biHeight = -capturedPixelH;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 24;
  info.bmiHeader.biCompression = BI_RGB;

  if (!LineGfxPreserveAspect())
  {
    ErikPanelStretchStripDib(dc,x,y,width,height,capturedPixelW,capturedPixelH,
      max(1,(int)(GfxPicWidth * 0.2977 + 0.5)),max(1,GfxPicHeight),ErikPanelBits,&info);
    return;
  }

  ErikPanelStretchStripDib(dc,x,y,width,height,capturedPixelW,capturedPixelH,
    width,height,ErikPanelBits,&info);
}

static void ErikPanelArtEnsure(HDC screenDC,int panelW,int barW,int h)
{
  if (!ErikPanelBits || panelW <= 0 || barW <= 0 || h <= 0)
    return;

  if (ErikPanelLeft && ErikPanelRight
    && ErikPanelCachedPanelW == panelW && ErikPanelCachedBarW == barW
    && ErikPanelCachedH == h)
    return;

  int stripeW = panelW + barW;

  HBITMAP newSideStripBitmaps[2] = {
    CreateCompatibleBitmap(screenDC, stripeW, h),
    CreateCompatibleBitmap(screenDC, stripeW, h),
  };
  if (!newSideStripBitmaps[0] || !newSideStripBitmaps[1])
  {
    if (newSideStripBitmaps[0]) DeleteObject(newSideStripBitmaps[0]);
    if (newSideStripBitmaps[1]) DeleteObject(newSideStripBitmaps[1]);
    return;
  }

  HDC mem = CreateCompatibleDC(screenDC);
  if (!mem)
  {
    DeleteObject(newSideStripBitmaps[0]);
    DeleteObject(newSideStripBitmaps[1]);
    return;
  }

  HBITMAP old = (HBITMAP)SelectObject(mem,newSideStripBitmaps[0]);
  RECT rPanel = {0,0,panelW,h};
  RECT rBar = {panelW,0,stripeW,h};
  HBRUSH panelYellowBrush = CreateSolidBrush(RGB(255,255,0));
  FillRect(mem,&rPanel,panelYellowBrush);
  FillRect(mem,&rBar,(HBRUSH)GetStockObject(BLACK_BRUSH));
  DrawErikPanelStrip(mem,0,0,panelW,h);

  SelectObject(mem,newSideStripBitmaps[1]);
  RECT rBarR = {0,0,barW,h};
  RECT rPanR = {barW,0,stripeW,h};
  FillRect(mem,&rBarR,(HBRUSH)GetStockObject(BLACK_BRUSH));
  FillRect(mem,&rPanR,panelYellowBrush);
  DrawErikPanelStrip(mem,barW,0,panelW,h);
  DeleteObject(panelYellowBrush);

  SelectObject(mem,old);
  DeleteDC(mem);

  ErikDelBmp(ErikPanelLeft);
  ErikDelBmp(ErikPanelRight);
  ErikPanelLeft = newSideStripBitmaps[0];
  ErikPanelRight = newSideStripBitmaps[1];

  ErikPanelCachedPanelW = panelW;
  ErikPanelCachedBarW = barW;
  ErikPanelCachedH = h;
}

static void CaptureErikPanel(void)
{
  if (ErikPanelBits || !hGfxDC || !GfxBits)
    return;

  if (ErikCapturing)
    return;
  ErikCapturing = TRUE;

  HDC tempDC = 0;
  HBITMAP tempBmp = 0;
  HBITMAP oldTempBmp = 0;
  BOOL captured = TRUE;

  if (!(GfxPicWidth > 0 && GfxPicHeight > 0 && GfxBmapWidth > 0))
    goto done;

  int savedGfxMode = GfxMode;
  BitmapType savedGfxBmapType = GfxBmapType;
  int savedL9TextMode = l9textmode;
  int savedScreencalled = screencalled;
  int savedGfxModeCore = gfx_mode;

  int savedWidth = GfxPicWidth;
  int savedHeight = GfxPicHeight;
  int savedBitmap = LastBitmap;
  int savedDelay = DelayBitmap;
  int savedBmapWidth = GfxBmapWidth;
  int savedBmapHeight = GfxBmapHeight;

  HDC savedGfxDC = hGfxDC;
  HBITMAP savedGfx = hGfx;
  BYTE* savedBits = GfxBits;

  RGBQUAD savedPalette[32];
  memcpy(savedPalette,Palette,sizeof(Palette));

  L9BYTE* savedGfxa5 = gfxa5;
  int savedGintColour = gintcolour;
  int savedOption = option;
  int savedReflect = reflectflag;
  int savedScale = scale;
  int savedDrawX = drawx;
  int savedDrawY = drawy;
  int savedA5StackPos = GfxA5StackPos;
  int savedScaleStackPos = GfxScaleStackPos;

  tempDC = CreateCompatibleDC(savedGfxDC);
  if (!tempDC)
    goto done;

  BYTE* bits = NULL;
  tempBmp = CreateIndexedDib(tempDC,160,128,&bits);
  if (!tempBmp || !bits)
  {
    if (tempBmp)
      DeleteObject(tempBmp);
    tempBmp = 0;
    DeleteDC(tempDC);
    tempDC = 0;
    goto done;
  }
  oldTempBmp = (HBITMAP)SelectObject(tempDC,tempBmp);

  hGfxDC = tempDC;
  hGfx = tempBmp;
  GfxBits = bits;
  GfxBmapWidth = 160;
  GfxBmapHeight = 128;
  GfxPicWidth = 160;
  GfxPicHeight = 128;
  LastBitmap = -1;
  DelayBitmap = 0;

  show_picture(500);
  while (RunGraphics())
    ;

  if (GfxPicWidth <= 0 || GfxPicHeight <= 0)
    captured = FALSE;

  if (captured)
  {
    ErikPanelWidth = GfxPicWidth;
    ErikPanelHeight = GfxPicHeight;
    int pixelCount = ErikPanelWidth * ErikPanelHeight;

    ErikPanelBits = (BYTE*)malloc(pixelCount * 3);
    if (!ErikPanelBits)
    {
      ErikPanelWidth = ErikPanelHeight = 0;
      captured = FALSE;
    }
  }

  if (captured)
  {
    int panelPixelWidth = ErikPanelWidth;
    int captureStride = GfxBmapWidth;
    int totalPixels = panelPixelWidth * ErikPanelHeight;
    for (int pixelIndex = 0; pixelIndex < totalPixels; pixelIndex++)
    {
      BYTE paletteIndex = GfxBits[(pixelIndex / panelPixelWidth) * captureStride
        + (pixelIndex % panelPixelWidth)];
      RGBQUAD rgbFromPalette = Palette[(paletteIndex < 32) ? paletteIndex : 0];
      int brightnessSum = (int)rgbFromPalette.rgbRed + (int)rgbFromPalette.rgbGreen + (int)rgbFromPalette.rgbBlue;
      BYTE yellowOrBlack = (BYTE)(brightnessSum > 96 ? 255 : 0);
      BYTE *destBgr = ErikPanelBits + pixelIndex * 3;
      destBgr[0] = 0;
      destBgr[1] = yellowOrBlack;
      destBgr[2] = yellowOrBlack;
    }
  }
  else
    ErikPanelWidth = ErikPanelHeight = 0;

  hGfxDC = savedGfxDC;
  hGfx = savedGfx;
  GfxBits = savedBits;
  GfxBmapWidth = savedBmapWidth;
  GfxBmapHeight = savedBmapHeight;
  GfxPicWidth = savedWidth;
  GfxPicHeight = savedHeight;
  LastBitmap = savedBitmap;
  DelayBitmap = savedDelay;
  memcpy(Palette,savedPalette,sizeof(Palette));
  gfxa5 = savedGfxa5;
  gintcolour = savedGintColour;
  option = savedOption;
  reflectflag = savedReflect;
  scale = savedScale;
  drawx = savedDrawX;
  drawy = savedDrawY;
  GfxA5StackPos = savedA5StackPos;
  GfxScaleStackPos = savedScaleStackPos;
  GfxMode = savedGfxMode;
  GfxBmapType = savedGfxBmapType;
  l9textmode = savedL9TextMode;
  screencalled = savedScreencalled;
  gfx_mode = savedGfxModeCore;
done:
  if (tempDC && oldTempBmp)
    SelectObject(tempDC,oldTempBmp);
  if (tempBmp)
    DeleteObject(tempBmp);
  if (tempDC)
    DeleteDC(tempDC);
  ErikCapturing = FALSE;
}

void DrawPicture(void)
{
  if (hGfxDC)
  {
    int erikPanelX=0,erikPanelY=0,erikPanelW=0,erikPanelH=0,erikBarW=0,erikMainW=0;

    if (hGfxDrawDC == 0)
    {
      hGfxDrawDC = CreateCompatibleDC(hGfxDC);
      SetStretchBltMode(hGfxDrawDC,COLORONCOLOR);
      BYTE* bits = NULL;
      hGfxDraw = CreateIndexedDib(hGfxDrawDC,PageWidth,GfxHeight,&bits);
      if (hGfxDraw)
        SelectObject(hGfxDrawDC,hGfxDraw);
    }

    RGBQUAD pal[32];
    SetIndexPalette(pal);
    SetDIBColorTable(hGfxDrawDC,0,32,pal);

    RECT rc = {0,0,PageWidth,GfxHeight};
    FillRect(hGfxDrawDC,&rc,(HBRUSH)GetStockObject(BLACK_BRUSH));

    BOOL useErikSidePanelLayout = L9IsErik();
    BOOL preserveLineGfxAspect = LineGfxPreserveAspect();
    if (useErikSidePanelLayout)
      CaptureErikPanel();

    int w = PageWidth,h = GfxHeight,x = 0,y = 0;
    if (GfxMode == 2)
    {
      w = GfxPicWidth;
      h = GfxPicHeight;

      if (w < 512)
      {
        w *= 2;
        h *= 2;
      }

      if (GfxFitToWindow)
        ScalePictureToBox(PageWidth,GfxHeight,w,h,&w,&h);

      x = (PageWidth-w)/2;
      y = (GfxHeight-h)/2;
      if (x < 0)
        x = 0;
      ClampPictureVerticallyInGfx(y,h);
    }
    else if (preserveLineGfxAspect && useErikSidePanelLayout)
    {
      double scaleErikRowToWindow = MinScaleToFit((double)PageWidth, (double)GfxHeight,
        (double)GfxPicWidth * (1.0 + 364.0 / 598.0), (double)GfxPicHeight);
      w = max(1,(int)(GfxPicWidth * scaleErikRowToWindow + 0.5));
      h = max(1,(int)(GfxPicHeight * scaleErikRowToWindow + 0.5));
      y = (GfxHeight - h) / 2;
      ClampPictureVerticallyInGfx(y,h);
    }
    else if (useErikSidePanelLayout)
      w = max(1,(int)(PageWidth / (1.0 + 364.0 / 598.0) + 0.5));

    int stretchDstX = x;
    if (useErikSidePanelLayout)
    {
      int panelW = max(1,(int)(w * 0.2977 + 0.5));
      int barW = max(1,(int)(w * 0.0067 + 0.5));
      int erikLayoutTotalWidth = w + 2 * panelW + 2 * barW;
      int startX = (PageWidth - erikLayoutTotalWidth) / 2;
      if (startX < 0) startX = 0;
      erikPanelX = startX;
      erikPanelY = y;
      erikPanelW = panelW;
      erikPanelH = h;
      erikBarW = barW;
      erikMainW = w;
      stretchDstX = startX + panelW + barW;
    }

    int stretchDstY = y;
    int stretchW = w;
    int stretchH = h;
    BOOL useLineGfxLetterbox = preserveLineGfxAspect && !useErikSidePanelLayout;
    if (useLineGfxLetterbox)
    {
      ScalePictureToBox(w,h,GfxPicWidth,GfxPicHeight,&stretchW,&stretchH);
      stretchDstX += (w - stretchW) / 2;
      stretchDstY += (h - stretchH) / 2;
    }

    StretchBlt(hGfxDrawDC,stretchDstX,stretchDstY,stretchW,stretchH,
      hGfxDC,0,0,GfxPicWidth,GfxPicHeight,SRCCOPY);

    SetDIBColorTable(hGfxDrawDC,0,32,Palette);

    HDC wdc = GetDC(hWndMain);
    HBRUSH gutterBrush = 0;
    if (useErikSidePanelLayout && ErikPanelBits)
    {
      ErikPanelArtEnsure(wdc,erikPanelW,erikBarW,erikPanelH);
      int rightPanelX = erikPanelX + erikPanelW + erikBarW + erikMainW;
      int panelArtW = erikPanelW + erikBarW;
      HDC mem = CreateCompatibleDC(wdc);
      if (mem)
      {
        HBITMAP sidePanelBitmaps[2] = { ErikPanelLeft, ErikPanelRight };
        int sidePanelScreenX[2] = { erikPanelX, rightPanelX };
        int sideIndex;
        for (sideIndex = 0; sideIndex < 2; sideIndex++)
          if (sidePanelBitmaps[sideIndex])
          {
            SelectObject(mem,sidePanelBitmaps[sideIndex]);
            BitBlt(wdc,sidePanelScreenX[sideIndex],erikPanelY,panelArtW,erikPanelH,mem,0,0,SRCCOPY);
          }
        DeleteDC(mem);
      }
      gutterBrush = CreateSolidBrush(BackColour);
      DrawMainWithGutters(wdc,gutterBrush,hGfxDrawDC,erikPanelX,rightPanelX + panelArtW,
        stretchDstX,stretchDstY,stretchW,stretchH);
    }
    else if (useLineGfxLetterbox || GfxMode == 2)
    {
      gutterBrush = CreateSolidBrush(BackColour);
      DrawMainWithGutters(wdc,gutterBrush,hGfxDrawDC,stretchDstX,stretchDstX+stretchW,
        stretchDstX,stretchDstY,stretchW,stretchH);
    }
    else
      BitBlt(wdc,0,0,PageWidth,GfxHeight,hGfxDrawDC,0,0,SRCCOPY);
    if (gutterBrush)
      DeleteObject(gutterBrush);
    ReleaseDC(hWndMain,wdc);
  }
}

static void ShowDelayedBitmapNow(void)
{
  if (DelayBitmap > 0)
  {
    KillTimer(hWndMain,1);
    int pic = DelayBitmap;
    DelayBitmap = 0;
    os_show_bitmap(pic,0,0);
  }
}

void os_graphics(int mode)
{
  GfxBmapType = NO_BITMAPS;
  LastBitmap = -1;
  FreeErikPanelResources();

  switch (mode)
  {
  case 0:
    GfxMode = 0;
    break;
  case 1:
    GfxMode = 1;
    break;
  case 2:
    LastFile.GetDir(GfxDir);
    GfxDir.AddChar('\\');
    GfxBmapType = DetectBitmaps(GfxDir);
    GfxMode = (GfxBmapType != NO_BITMAPS) ? 2 : 0;
    break;
  }

  Resize();

  if (hGfxDC)
    DeleteDC(hGfxDC);
  hGfxDC = 0;
  if (hGfx)
    DeleteObject(hGfx);
  hGfx = 0;
  GfxBits = NULL;

  if (GfxMode)
  {
    HDC dc = GetDC(hWndMain);
    hGfxDC = CreateCompatibleDC(dc);
    SetStretchBltMode(hGfxDC,COLORONCOLOR);
    ReleaseDC(hWndMain,dc);

    if (GfxMode == 2)
    {
      GfxBmapWidth = MAX_BITMAP_WIDTH;
      GfxBmapHeight = MAX_BITMAP_HEIGHT;
      GfxPicWidth = 0;
      GfxPicHeight = 0;
    }
    else
    {
      GetPictureSize(&GfxBmapWidth,&GfxBmapHeight);
      GfxPicWidth = GfxBmapWidth;
      GfxPicHeight = GfxBmapHeight;
    }

    BYTE* bits = NULL;
    hGfx = CreateIndexedDib(hGfxDC,GfxBmapWidth,GfxBmapHeight,&bits);
    GfxBits = bits;
    SelectObject(hGfxDC,hGfx);
  }
}

void os_cleargraphics(void)
{
  if (hGfxDC)
  {
    LastBitmap = -1;

    RECT rc;
    rc.left = 0;
    rc.right = GfxBmapWidth;
    rc.top = 0;
    rc.bottom = GfxBmapHeight;

    HBRUSH br = CreateSolidBrush(GetIndexColour(0));
    HGDIOBJ old = SelectObject(hGfxDC,br);
    FillRect(hGfxDC,&rc,br);
    SelectObject(hGfxDC,old);
    DeleteObject(br);
  }
}

void os_setcolour(int colour, int index)
{
  Palette[colour] = Colours[index];
}

COLORREF LineColour1 = 0;
COLORREF LineColour2 = 0;

VOID CALLBACK LineProc(int x, int y, LPARAM)
{
  if (GetPixel(hGfxDC,x,y) == LineColour2)
  {
    SetPixel(hGfxDC,x,y,LineColour1);
    if (GfxDither)
    {
      SetPixel(hGfxDC,x+1,y,LineColour1);
      SetPixel(hGfxDC,x,y+1,LineColour1);
      SetPixel(hGfxDC,x+1,y+1,LineColour1);
      SetPixel(hGfxDC,x+2,y,LineColour1);
      SetPixel(hGfxDC,x,y+2,LineColour1);
      SetPixel(hGfxDC,x+2,y+2,LineColour1);
    }
  }
}

void os_drawline(int x1, int y1, int x2, int y2, int colour1, int colour2)
{
  if (hGfxDC)
  {
    LineColour1 = GetIndexColour(colour1);
    LineColour2 = GetIndexColour(colour2);
    LineDDA(x1,y1,x2,y2,LineProc,NULL);
    LineProc(x2,y2,NULL);
  }
}

void os_fill(int x, int y, int colour1, int colour2)
{
  if (hGfxDC)
  {
    COLORREF colour = GetIndexColour(colour2);
    if (GetPixel(hGfxDC,x,y) == colour)
    {
      HBRUSH br = CreateSolidBrush(GetIndexColour(colour1));
      HGDIOBJ old = SelectObject(hGfxDC,br);
      ExtFloodFill(hGfxDC,x,y,colour,FLOODFILLSURFACE);
      SelectObject(hGfxDC,old);
      DeleteObject(br);
    }
  }
}

void os_show_bitmap(int pic, int x, int y)
{
  if ((DelayBitmap == -1) && (pic > 0) && (x == 0) && (y == 0))
  {
    DelayBitmap = pic;
    return;
  }
  DelayBitmap = 0;

  if (LastBitmap == pic)
    return;

  if ((GfxMode == 2) && GfxBits)
  {
    Bitmap* bitmap = DecodeBitmap(GfxDir,GfxBmapType,pic,x,y);
    if (bitmap)
    {
      GfxPicWidth = bitmap->width;
      GfxPicHeight = bitmap->height;

      memset(GfxBits,0,GfxBmapWidth*GfxBmapHeight);
      for (int y = 0; y < GfxPicHeight; y++)
        memcpy(GfxBits+(y*GfxBmapWidth),bitmap->bitmap+(y*GfxPicWidth),GfxPicWidth);

      for (int i = 0; i < bitmap->npalette; i++)
      {
        Palette[i].rgbRed = bitmap->palette[i].red;
        Palette[i].rgbGreen = bitmap->palette[i].green;
        Palette[i].rgbBlue = bitmap->palette[i].blue;
        Palette[i].rgbReserved = 0;
      }

      LastBitmap = pic;
      DrawPicture();

      if (pic == 0)
      {
        SetTimer(hWndMain,1,2000,NULL);
        DelayBitmap = -1;
      }
    }
  }
}

// About Dialog ***************************************

class AboutDialog : public Dialog
{
public:
  AboutDialog(Object *Parent) : Dialog(Parent,IDD_ABOUT,"AboutDialog") {}

  BOOL SetupWindow()
  {
    HWND logoWnd = GetDlgItem(hWnd,IDC_LOGO);
    RECT logoRect;
    GetWindowRect(logoWnd,&logoRect);
    ScreenToClient(hWnd,(LPPOINT)&logoRect);
    ScreenToClient(hWnd,((LPPOINT)&logoRect)+1);
    double aspect = ((double)(logoRect.right-logoRect.left))/(logoRect.bottom-logoRect.top);

    HWND groupWnd = GetDlgItem(hWnd,IDC_GROUP);
    RECT groupRect;
    GetWindowRect(groupWnd,&groupRect);
    ScreenToClient(hWnd,(LPPOINT)&groupRect);
    ScreenToClient(hWnd,((LPPOINT)&groupRect)+1);

    logoRect.right = groupRect.left-logoRect.left;
    logoRect.bottom = logoRect.top+(int)((logoRect.right-logoRect.left)/aspect);
    MoveWindow(logoWnd,logoRect.left,logoRect.top,
      logoRect.right-logoRect.left,logoRect.bottom-logoRect.top,TRUE);

    SetDarkMode();
    return TRUE;
  }

  void SetDarkMode(void)
  {
    SetDarkTheme(GetDlgItem(hWnd,IDOK));
    SetDarkTitle(hWnd);
  }

  BOOL EV_FIND(TMSG& Msg)
  {
    switch (Msg.Msg)
    {
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
      return DarkCtlColour(Msg);
    default:
      return FALSE;
    }
  }
};

// MainWindow *****************************************

class MainWindow : public HashWindow
{
public:
  MainWindow(Object *Parent,char *Title);
  ~MainWindow();

  BOOL Playing;

  void Destroy();
  BOOL SetupWindow();
  void OpenFile(char *name);

  void CmHelpContents();
  void CmAbout();
  void CmExit();
  void CmOpen();
  void CmSelectFont();
  void CmSelectTextColour();
  void CmSelectBackColour();
  void CmToggleDither();
  void CmToggleFitToWindow();
  void CmTogglePreserveAspect();
  void CmRestore() { HashCommand("#restore"); }
  void CmSave() { HashCommand("save"); }
  void CmDictionary() { HashCommand("#dictionary"); }
  void CmPaste();

  void SetFont();
  void DelFonts();
  void UpdateFont();

// message response functions

  BOOL LButtonDown(TMSG &);
  BOOL RButtonDown(TMSG &);
  BOOL LButtonUp(TMSG &);
  BOOL WMMouseMove(TMSG &);
  BOOL WMSize(TMSG &);

  BOOL WMKeyDown(TMSG &);
  BOOL WMChar(TMSG &);
  BOOL WMSetFocus(TMSG&);
  BOOL WMKillFocus(TMSG&);
  BOOL WMTimer(TMSG&);
  BOOL WMDpiChanged(TMSG&);
  BOOL WMDrawMenuBar(TMSG&);
  BOOL WMSettingChange(TMSG&);

// window paint request
  void Paint(HDC, BOOL, RECT&);

// enable message response
  HASH_EV_ENABLE(MainWindow)

private:
  AboutDialog* m_dialog;
};

// define response functions
EV_START(MainWindow)
// command messages
  EV_COMMAND(CM_ABOUT, CmAbout)
  EV_COMMAND(CM_HELPCONTENTS, CmHelpContents)
  EV_COMMAND(CM_EXIT, CmExit)
  EV_COMMAND(CM_OPEN, CmOpen)
  EV_COMMAND(CM_FONT, CmSelectFont)
  EV_COMMAND(CM_TEXTCOLOUR, CmSelectTextColour)
  EV_COMMAND(CM_BACKCOLOUR, CmSelectBackColour)
  EV_COMMAND(CM_DITHER, CmToggleDither)
  EV_COMMAND(CM_FITTOWINDOW, CmToggleFitToWindow)
  EV_COMMAND(CM_PRESERVEASPECT, CmTogglePreserveAspect)
  EV_COMMAND(CM_FILELOAD, CmRestore)
  EV_COMMAND(CM_FILESAVE, CmSave)
  EV_COMMAND(CM_DICTIONARY, CmDictionary)
  EV_COMMAND(CM_PASTE, CmPaste)

// windows messages
  EV_MESSAGE(WM_LBUTTONDOWN, LButtonDown)
  EV_MESSAGE(WM_RBUTTONDOWN, RButtonDown)
  EV_MESSAGE(WM_MOUSEMOVE, WMMouseMove)
  EV_MESSAGE(WM_LBUTTONUP, LButtonUp)
  EV_MESSAGE(WM_KEYDOWN, WMKeyDown)
  EV_MESSAGE(WM_CHAR, WMChar)
  EV_MESSAGE(WM_SIZE, WMSize)
  EV_MESSAGE(WM_SETFOCUS, WMSetFocus)
  EV_MESSAGE(WM_KILLFOCUS, WMKillFocus)
  EV_MESSAGE(WM_TIMER, WMTimer)
  EV_MESSAGE(WM_DPICHANGED, WMDpiChanged)
  EV_MESSAGE(WM_NCACTIVATE, WMDrawMenuBar)
  EV_MESSAGE(WM_NCPAINT, WMDrawMenuBar)
  EV_MESSAGE(WM_UAHDRAWMENU, WMDrawMenuBar)
  EV_MESSAGE(WM_UAHDRAWMENUITEM, WMDrawMenuBar)
  EV_MESSAGE(WM_SETTINGCHANGE, WMSettingChange)

EV_END

// main window constructor
MainWindow::MainWindow(Object *Parent,char *Title) : HashWindow(Parent,Title,"MainWindow")
{
  // set window style
  Style=WS_OVERLAPPEDWINDOW;

#ifdef __BORLANDC__
  // give it a menu and icon
  AssignMenu(IDM_MENU);
  SetIcon(IDI_ICON);
#endif

  Flags|=W_OVERSCROLL;

  m_dialog=NULL;
}

// this is called to setup the window
BOOL MainWindow::SetupWindow()
{
#ifdef WIN32
  SetWindowLong(hWnd,GWL_EXSTYLE,WS_EX_OVERLAPPEDWINDOW);
#endif

#ifdef _MSC_VER
  AssignMenu(IDM_MENU);
  SetIcon(IDI_ICON);
#endif

  dpi=call_GetDpiForWindow(hWnd);
  LONG lfHeight=0;
  ReadIniInt("Font","PointSize",lfHeight);
  if (lfHeight>0)
    lf.lfHeight=-MulDiv(lfHeight,dpi,72);
  else
  {
    lfHeight=0;
    ReadIniInt("Font","Size",lfHeight);
    if (lfHeight>0)
      lf.lfHeight=-MulDiv(lfHeight,dpi,72);
    else if (lfHeight<0)
      lf.lfHeight=lfHeight;
  }

  SetDarkTitle(hWnd);

  // load window pos from ini file (will also be automatically saved on exit)
  GetWindowState();
  SetMru(hWnd,CM_EXIT);
  hWndMain=hWnd;
  SetFont();
  SetBkColor(BackColour);
  CheckMenuItem(CM_DITHER,GfxDither);
  CheckMenuItem(CM_FITTOWINDOW,GfxFitToWindow);
  CheckMenuItem(CM_PRESERVEASPECT,GfxPreserveAspect);
  Playing=FALSE;

  return TRUE;
}

void MainWindow::CmSelectFont()
{
  LOGFONT intLf;
  memcpy(&intLf,&lf,sizeof(LOGFONT));
  intLf.lfHeight=MulDiv(intLf.lfHeight,call_GetDpiForSystem(),dpi);

  CHOOSEFONT cf;
  cf.lStructSize=sizeof cf;
  cf.hwndOwner=hWnd;
  cf.lpLogFont=&intLf;
  cf.Flags=CF_INITTOLOGFONTSTRUCT | CF_SCREENFONTS;

  BOOL chosen=FALSE;
  {
    DpiContextSystem dpiSys;
    chosen=ChooseFont(&cf);
  }

  if (chosen)
  {
    memcpy(&lf,&intLf,sizeof(LOGFONT));
    lf.lfHeight=-MulDiv(cf.iPointSize,dpi,720);
    UpdateFont();
  }
}

static bool initCustomColours=true;
static COLORREF customColours[16];

void MainWindow::CmSelectTextColour()
{
  if (initCustomColours)
  {
    for (int i=0; i<16; i++)
      customColours[i]=RGB(255,255,255);
    initCustomColours=false;
  }

  CHOOSECOLOR cc={0};
  cc.lStructSize=sizeof cc;
  cc.hwndOwner=hWnd;
  cc.lpCustColors=(LPDWORD)customColours;
  cc.rgbResult=FontColour;
  cc.Flags=CC_FULLOPEN | CC_RGBINIT;
  if (ChooseColor(&cc))
  {
    FontColour=cc.rgbResult;
    InvalidateRect(hWnd,NULL,TRUE);
  }
}

void MainWindow::CmSelectBackColour()
{
  if (initCustomColours)
  {
    for (int i=0; i<16; i++)
      customColours[i]=RGB(255,255,255);
    initCustomColours=false;
  }

  CHOOSECOLOR cc={0};
  cc.lStructSize=sizeof cc;
  cc.hwndOwner=hWnd;
  cc.lpCustColors=(LPDWORD)customColours;
  cc.rgbResult=BackColour;
  cc.Flags=CC_FULLOPEN | CC_RGBINIT;
  if (ChooseColor(&cc))
  {
    BackColour=cc.rgbResult;
    SetBkColor(BackColour);
    InvalidateRect(hWnd,NULL,TRUE);
  }
}

void MainWindow::CmToggleDither()
{
  GfxDither = !GfxDither;
  CheckMenuItem(CM_DITHER,GfxDither);
}

void MainWindow::CmToggleFitToWindow()
{
  GfxFitToWindow = !GfxFitToWindow;
  CheckMenuItem(CM_FITTOWINDOW, GfxFitToWindow);
  if (GfxMode == 2)
  {
    DrawPicture();
  }
}

void MainWindow::CmTogglePreserveAspect()
{
  GfxPreserveAspect = !GfxPreserveAspect;
  CheckMenuItem(CM_PRESERVEASPECT, GfxPreserveAspect);
  if (GfxMode == 1)
    DrawPicture();
}

void MainWindow::SetFont()
{
  Font=CreateFontIndirect(&lf);
  HDC dc=GetDC(hWnd);
  HFONT OldFont=(HFONT) SelectObject(dc,Font);
  TEXTMETRIC tm;
  GetTextMetrics(dc,&tm);
  FontHeight=tm.tmHeight;
  Margin=tm.tmAveCharWidth;
  LineSpacing=(int) 1.1*FontHeight;
  SelectObject(dc,OldFont);
  ReleaseDC(hWnd,dc);
}

void MainWindow::DelFonts()
{
  DeleteObject(Font);
}

void MainWindow::UpdateFont()
{
  DelFonts();
  SetFont();
  KillCaret();
  Paginate();
  InvalidateRect(hWnd,NULL,TRUE);
  MakeCaret();
}

// this is called when the window is destroyed
void MainWindow::Destroy()
{
  // close help if open
  FreeErikPanelResources();
  DelFonts();
  StopGame();
  Playing=FALSE;
  FreeMemory();
  CancelInput();
}

MainWindow::~MainWindow()
{
}

void MainWindow::OpenFile(char *name)
{
  // in input routine?, cause fall through
  CancelInput();
  // clear buffers etc..., even if fails as invalidates game memory
  Output="";
  LineStart=LastWordEnd=0;
  Paginate();
  InvalidateRect(hWnd,NULL,TRUE);

  // look for a picture datafile
  FName picname(name);
  picname.NewExt("pic");
  if (!picname.Exist()) picname.NewExt("cga");
  if (!picname.Exist()) picname.NewExt("hrc");
  if (!picname.Exist()) picname.NewName("picture.dat");

  if (!LoadGame(name,picname)) MessageBox(hWnd,"Unable to load game file","Load Error",MB_OK | MB_ICONEXCLAMATION);
  else
  {
    LastFile=name;
    AddToMru(LastFile);

    if (Playing) return;
    Playing=TRUE;
    while (Playing && RunGame()) App::PeekLoop();
    Playing=FALSE;
  }
}

void MainWindow::CmOpen()
{
  FName fn=LastFile;
  if (os_get_game_file(fn,fn.Size()))
    OpenFile(fn);
}

void MainWindow::CmExit()
{
  DestroyWindow();
}

void MainWindow::CmHelpContents()
{
  FName HelpFile;
  GetModuleFileName(App::hInstance,HelpFile,HelpFile.Size());
  HelpFile.Update();
  HelpFile.NewName(HelpFileName);
  HtmlHelp(hWnd, HelpFile, HH_DISPLAY_TOPIC, 0L);
}

void MainWindow::Paint(HDC, BOOL, RECT&)
{
  Redraw();
}

void MainWindow::CmAbout()
{
  AboutDialog dialog(this);
  m_dialog = &dialog;
  dialog.ExecuteWithFont();
  m_dialog = NULL;
}

void MainWindow::CmPaste()
{
  if (OpenClipboard(hWndMain))
  {
    HGLOBAL handle = ::GetClipboardData(CF_TEXT);
    if (handle)
    {
      LPTSTR text = (LPTSTR)::GlobalLock(handle); 
      if (text) 
      {
        while (*text != 0)
        {
          if (isprint(*text))
            InputChars.AddTail(*text);
          text++;
        }
        ::GlobalUnlock(handle); 
      }
    }
    CloseClipboard();
  }
}

BOOL MainWindow::WMKeyDown(TMSG &Msg)
{
  if (DelayBitmap > 0)
    ShowDelayedBitmapNow();

  bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
  bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;

  switch ((int) Msg.wParam)   // the virtual key code
  {
    case VK_F1:
      CmHelpContents();
      break;
    case VK_F2: CmOpen(); break;
    case VK_F3: CmRestore(); break;
    case VK_F4: CmSave(); break;
    case VK_F5: CmDictionary(); break;

    case 'V':
      if (ctrl)
        CmPaste();
      break;
    case VK_INSERT:
      if (shift)
        CmPaste();
      break;

    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_DELETE:
    case VK_END:
    case VK_HOME:
      InputChars.AddTail(256+Msg.wParam);
      break;
    }
  return TRUE; // message handled
}

BOOL MainWindow::WMChar(TMSG &Msg)
{
  if (DelayBitmap > 0)
    ShowDelayedBitmapNow();

  if (isprint(Msg.wParam) || (Msg.wParam==8) || (Msg.wParam==13))
    InputChars.AddTail(Msg.wParam);
  return TRUE;
}

BOOL MainWindow::LButtonDown(TMSG &)
{
  return TRUE;
}

BOOL MainWindow::WMMouseMove(TMSG &)
{
  return TRUE;
}

BOOL MainWindow::LButtonUp(TMSG &)
{
  return TRUE;
}

BOOL MainWindow::RButtonDown(TMSG &)
{
  return TRUE;
}

BOOL MainWindow::WMSize(TMSG &Msg)
{
  if (Msg.wParam != SIZE_MINIMIZED)
  {
    PageWidth = LOWORD(Msg.lParam);
    WndHeight = HIWORD(Msg.lParam);
    Resize();
  }
  return TRUE;
}

BOOL MainWindow::WMSetFocus(TMSG&)
{
  MakeCaret();
  return TRUE;
}

BOOL MainWindow::WMKillFocus(TMSG&)
{
  KillCaret();
  return TRUE;
}

BOOL MainWindow::WMTimer(TMSG& Msg)
{
  if (Msg.wParam == 1)
  {
    ShowDelayedBitmapNow();
  }
  return TRUE;
}

BOOL MainWindow::WMDpiChanged(TMSG& Msg)
{
  LPRECT newRect = (LPRECT)Msg.lParam;
  MoveWindow(hWndMain,newRect->left,newRect->top,
    newRect->right-newRect->left,newRect->bottom-newRect->top,TRUE);
  UINT newDpi = HIWORD(Msg.wParam);
  if (dpi != newDpi)
  {
    long FontHeight = abs(MulDiv(lf.lfHeight,72,dpi));
    lf.lfHeight = -MulDiv(FontHeight,newDpi,72);
    dpi = newDpi;
    UpdateFont();
  }
  return TRUE;
}

BOOL MainWindow::WMDrawMenuBar(TMSG& Msg)
{
  return DarkDrawMenuBar(Msg,hWnd);
}

BOOL MainWindow::WMSettingChange(TMSG& Msg)
{
  bool UpdateFont = (FontColour == GetSysOrDarkColour(COLOR_WINDOWTEXT));
  bool UpdateBack = (BackColour == GetSysOrDarkColour(COLOR_WINDOW));

  if (SetDarkMode(false))
  {
    SetDarkTitle(hWnd);
    if (m_dialog)
      m_dialog->SetDarkMode();
    if (UpdateFont)
      FontColour = GetSysOrDarkColour(COLOR_WINDOWTEXT);
    if (UpdateBack)
    {
      BackColour = GetSysOrDarkColour(COLOR_WINDOW);
      SetBkColor(BackColour);
    }
  }
  return FALSE;
}

// App *****************************************

class MyApp : public App
{
public:
  MyApp(char *Name);
  ~MyApp();
  
private:
  void InitMainWindow();
  void SetDefs();
  void ReadIni();
  void WriteIni();
  void FirstIn();
};

void MyApp::SetDefs()
{
  // Set application default settings here
  LastFile="";
  FiltIndex=0;
  LastGameFile="";
  GameFiltIndex=0;
  LastScriptFile="";

  NONCLIENTMETRICS ncm;
  memset(&ncm,0,sizeof(ncm));
  ncm.cbSize=sizeof(ncm);
  SystemParametersInfo(SPI_GETNONCLIENTMETRICS,ncm.cbSize,&ncm,0);
  dpi=call_GetDpiForSystem();

  memset(&lf,0,sizeof(lf));
  lf.lfHeight=-MulDiv(10,dpi,72);
  lf.lfWeight=FW_NORMAL;
  lf.lfCharSet=ANSI_CHARSET;
  lf.lfOutPrecision=OUT_TT_PRECIS;
  lf.lfClipPrecision=CLIP_DEFAULT_PRECIS;
  lf.lfQuality=PROOF_QUALITY;
  lf.lfPitchAndFamily=4;
  strcpy(lf.lfFaceName,ncm.lfMessageFont.lfFaceName);

  FontColour=GetSysOrDarkColour(COLOR_WINDOWTEXT);
  BackColour=GetSysOrDarkColour(COLOR_WINDOW);

  GfxDither=0;
  GfxPreserveAspect=FALSE;
}

void MyApp::ReadIni()
{
  // read information from ini file
  SetDefs();
  ReadIniString("General","LastFile",(String&)LastFile);
  ReadIniInt("General","FiltIndex",FiltIndex);
  ReadIniString("General","LastGameFile",(String&)LastGameFile);
  ReadIniInt("General","GameFiltIndex",GameFiltIndex);
  ReadIniInt("General","BackColour",(long&)BackColour);

  String S(LF_FACESIZE);
  ReadIniString("Font","Name",S);
  if (*S) strcpy(lf.lfFaceName,S);
  ReadIniInt("Font","Colour",(long&) FontColour);

  ReadIniBool("Graphics","Dither",GfxDither);
  ReadIniBool("Graphics","FitToWindow",GfxFitToWindow);
  ReadIniBool("Graphics","PreserveAspectRatio",GfxPreserveAspect);

  int wasDark = 0;
  ReadIniInt("General","WasInDarkMode",wasDark);
  BackColour = UpdateColour(COLOR_WINDOW,BackColour,wasDark != 0);
  FontColour = UpdateColour(COLOR_WINDOWTEXT,FontColour,wasDark != 0);
}

void MyApp::WriteIni()
{
  // write information to ini file
  WriteIniString("General","LastFile",LastFile);
  WriteIniInt("General","FiltIndex",FiltIndex);
  WriteIniString("General","LastGameFile",LastGameFile);
  WriteIniInt("General","GameFiltIndex",GameFiltIndex);
  WriteIniInt("General","BackColour",(long)BackColour);
  WriteIniInt("General","WasInDarkMode",g_DarkMode ? 1 : 0);

  long FontHeight=abs(MulDiv(lf.lfHeight,72,dpi));
  WriteIniInt("Font","PointSize",FontHeight);
  WriteIniString("Font","Name",lf.lfFaceName);
  WriteIniInt("Font","Colour",(long) FontColour);

  WriteIniBool("Graphics","Dither",GfxDither);
  WriteIniBool("Graphics","FitToWindow",GfxFitToWindow);
  WriteIniBool("Graphics","PreserveAspectRatio",GfxPreserveAspect);
}

MyApp::MyApp(char *Name) : App(Name,Ini)
{
  // enable 3d dialog boxes
#ifdef WIN16
  EnableCtl3d();
#endif

  SetDarkMode(true);

  // read from ini file
  ReadMru(4);
  ReadIni();
};

void MyApp::InitMainWindow()
{
  MainWindow=new ::MainWindow(0,MainWinTitle);
}

MyApp::~MyApp()
{
  // write information to ini file
  WriteMru();
  WriteIni();
}

void MyApp::FirstIn()
{
  if (__argc > 1)
  {
    // Convert to a full path name
    char fullname[_MAX_PATH];
    char* part;
    if (::GetFullPathName(__argv[1],_MAX_PATH,fullname,&part) != 0)
      ((::MainWindow*) MainWindow)->OpenFile(fullname);
  }
  else
    ((::MainWindow*) MainWindow)->CmOpen();
}

// main function, called from WinMain()
int Main()
{
  // create and run application
  return MyApp(AppName).Run();
}

