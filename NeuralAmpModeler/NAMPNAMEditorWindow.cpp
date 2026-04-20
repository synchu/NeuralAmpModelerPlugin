#include "NAMPNAMEditorWindow.h"

// ============================================================
//  Windows implementation
// ============================================================
#if defined(OS_WIN)

#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <algorithm>

#include <windowsx.h>
#include <shellapi.h>
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")

#include "json.hpp"
using json = nlohmann::json;

// ---- Control IDs ----
#define IDC_FILE_PATH_LABEL  3001
#define IDC_NEW_FILE         3002
#define IDC_OPEN_FILE        3003
#define IDC_SAVE_FILE        3004
#define IDC_SAVE_AS_FILE     3005
#define IDC_SLOT_LIST        3006
#define IDC_ADD_SLOT         3007
#define IDC_REMOVE_SLOT      3008
#define IDC_MOVE_UP          3009
#define IDC_MOVE_DOWN        3010
#define IDC_GAIN_MIN_EDIT    3011
#define IDC_GAIN_MAX_EDIT    3012
#define IDC_NAM_PATH_EDIT    3013
#define IDC_BROWSE_NAM       3014
#define IDC_OV_OUTPUT_CHECK  3015
#define IDC_OV_OUTPUT_EDIT   3016
#define IDC_OV_BASS_CHECK    3017
#define IDC_OV_BASS_EDIT     3018
#define IDC_OV_MID_CHECK     3019
#define IDC_OV_MID_EDIT      3020
#define IDC_OV_TREBLE_CHECK  3021
#define IDC_OV_TREBLE_EDIT   3022
#define IDC_APPLY_SLOT       3023
#define IDC_CLOSE_BTN        3024
#define IDC_FONT_DEC         3025
#define IDC_FONT_INC         3026
#define IDC_PREVIEW_SLOT     3027
#define IDC_PREVIEW_CHAIN    3028
#define IDC_DISTRIBUTE_GAIN  3029

// ---- Dark theme colours (matching NAMLibraryBrowserWindow) ----
static const COLORREF kDarkBg     = RGB(30, 30, 30);
static const COLORREF kEditBg     = RGB(40, 40, 40);
static const COLORREF kTextColor  = RGB(220, 220, 220);
static const COLORREF kBtnBg      = RGB(60, 60, 60);
static const COLORREF kBtnBorder  = RGB(100, 100, 100);
static const COLORREF kBtnPressed = RGB(0, 120, 215);
static const COLORREF kDisabledTx = RGB(100, 100, 100);
static const COLORREF kSplitColor = RGB(55, 55, 55);

// ---- Sizing constants ----
static const int kInitWndW   = 920;
static const int kInitWndH   = 700;
static const int kMinWndW    = 750;
static const int kMinWndH    = 580;
static const int kMinSplitX  = 200;
static const int kCtrlH      = 36;
static const int kBtnH       = 36;
static const int kLabelH     = 26;
static const int kM          = 12;

static const wchar_t kWndClass[] = L"NAMPNAMEditorWindow";

namespace
{

std::wstring Utf8ToWide(const std::string& s)
{
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  if (n <= 1) return {};
  std::wstring r(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, r.data(), n);
  r.pop_back();
  return r;
}

std::string WideToUtf8(const wchar_t* s)
{
  if (!s || !s[0]) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
  if (n <= 1) return {};
  std::string r(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, s, -1, r.data(), n, nullptr, nullptr);
  r.pop_back();
  return r;
}

std::string GetWndText(HWND hw)
{
  int len = GetWindowTextLengthW(hw);
  if (len == 0) return {};
  std::wstring ws(len + 1, L'\0');
  GetWindowTextW(hw, ws.data(), len + 1);
  return WideToUtf8(ws.c_str());
}

double ParseDouble(HWND hw, double fallback = 0.0)
{
  try { return std::stod(GetWndText(hw)); }
  catch (...) { return fallback; }
}

void SetDoubleText(HWND hw, double v)
{
  std::ostringstream ss;
  ss << std::fixed << std::setprecision(2) << v;
  SetWindowTextW(hw, Utf8ToWide(ss.str()).c_str());
}

HWND MakeLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h, HFONT font)
{
  HWND hw = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
    x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
  SendMessage(hw, WM_SETFONT, (WPARAM)font, TRUE);
  return hw;
}

HWND MakeEdit(HWND parent, int id, int x, int y, int w, int h, HFONT font)
{
  HWND hw = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
    x, y, w, h, parent, (HMENU)(UINT_PTR)id, GetModuleHandleW(nullptr), nullptr);
  SendMessage(hw, WM_SETFONT, (WPARAM)font, TRUE);
  return hw;
}

HWND MakeButton(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h, HFONT font)
{
  HWND hw = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
    x, y, w, h, parent, (HMENU)(UINT_PTR)id, GetModuleHandleW(nullptr), nullptr);
  SendMessage(hw, WM_SETFONT, (WPARAM)font, TRUE);
  return hw;
}

HWND MakeCheckBox(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h, HFONT font)
{
  HWND hw = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, x, y, w, h, parent,
                            (HMENU)(UINT_PTR)id, GetModuleHandleW(nullptr), nullptr);
  SendMessage(hw, WM_SETFONT, (WPARAM)font, TRUE);
  SetWindowTheme(hw, L"", L""); // disable visual styles so WM_CTLCOLORBTN text colour is honoured
  return hw;
}

} // namespace

// ---- Overlap validation ----
static std::string CheckSlotOverlaps(const std::vector<ModelMapSlot>& slots)
{
  std::string warn;
  for (int i = 0; i < (int)slots.size(); ++i)
    for (int j = i + 1; j < (int)slots.size(); ++j)
      if (slots[i].ampGainMax > slots[j].ampGainMin && slots[j].ampGainMax > slots[i].ampGainMin)
        warn += "Slot " + std::to_string(i + 1) + " and slot " + std::to_string(j + 1) + " overlap.\n";
  return warn;
}

// ---- Constructor / Destructor ----

NAMPNAMEditorWindow::NAMPNAMEditorWindow() = default;

NAMPNAMEditorWindow::~NAMPNAMEditorWindow()
{
  Close();
  if (mHFont)       { DeleteObject(mHFont);       mHFont = nullptr; }
  if (mDarkBgBrush) { DeleteObject(mDarkBgBrush); mDarkBgBrush = nullptr; }
  if (mEditBgBrush) { DeleteObject(mEditBgBrush); mEditBgBrush = nullptr; }
}

// ---- Font sizing ----

void NAMPNAMEditorWindow::RecreateFont()
{
  if (mHFont)
    DeleteObject(mHFont);

  mHFont = CreateFontW(mFontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void NAMPNAMEditorWindow::UpdateChildFonts()
{
  if (!mHwnd) return;

  auto setFont = [this](HWND hw) {
    if (hw) SendMessage(hw, WM_SETFONT, (WPARAM)mHFont, TRUE);
  };

  setFont(mHwndFileLbl);
  setFont(mHwndFilePathLabel);
  setFont(mHwndNewBtn);
  setFont(mHwndOpenBtn);
  setFont(mHwndSaveBtn);
  setFont(mHwndSaveAsBtn);
  setFont(mHwndFontDecBtn);
  setFont(mHwndFontIncBtn);
  setFont(mHwndSlotsLbl);
  setFont(mHwndSlotList);
  setFont(mHwndAddBtn);
  setFont(mHwndRemoveBtn);
  setFont(mHwndUpBtn);
  setFont(mHwndDownBtn);
  setFont(mHwndSelLbl);
  setFont(mHwndGainLbl);
  setFont(mHwndGainMinEdit);
  setFont(mHwndGainMaxLbl);
  setFont(mHwndGainMaxEdit);
  setFont(mHwndNamLbl);
  setFont(mHwndNamPathEdit);
  setFont(mHwndBrowseBtn);
  setFont(mHwndOvLbl);
  setFont(mHwndOvOutputCheck);
  setFont(mHwndOvOutputEdit);
  setFont(mHwndOvBassCheck);
  setFont(mHwndOvBassEdit);
  setFont(mHwndOvMidCheck);
  setFont(mHwndOvMidEdit);
  setFont(mHwndOvTrebleCheck);
  setFont(mHwndOvTrebleEdit);
  setFont(mHwndApplyBtn);
  setFont(mHwndPreviewSlotBtn);
  setFont(mHwndPreviewChainBtn);
  setFont(mHwndCloseBtn);
  setFont(mHwndDistributeBtn);
}

void NAMPNAMEditorWindow::IncreaseFontSize()
{
  if (mFontSize >= kMaxFontSize) return;
  mFontSize += 2;
  RecreateFont();
  UpdateChildFonts();
  RECT rc;
  GetClientRect(mHwnd, &rc);
  ResizeControls(rc.right, rc.bottom);
  SaveSettings();
}

void NAMPNAMEditorWindow::DecreaseFontSize()
{
  if (mFontSize <= kMinFontSize) return;
  mFontSize -= 2;
  RecreateFont();
  UpdateChildFonts();
  RECT rc;
  GetClientRect(mHwnd, &rc);
  ResizeControls(rc.right, rc.bottom);
  SaveSettings();
}

// ---- Public API ----

void NAMPNAMEditorWindow::Open(void* pParentWindow)
{
  if (mIsOpen) { BringToFront(); return; }

  mParentHwnd = static_cast<HWND>(pParentWindow);

  LoadSettings();

  // If the plugin didn't pre-load a file, restore the last one the editor had open
  if (mSlots.empty() && !mLastOpenedPNAMPath.empty())
    LoadFile(mLastOpenedPNAMPath);

  INITCOMMONCONTROLSEX icex = {};
  icex.dwSize = sizeof(icex);
  icex.dwICC  = ICC_LISTVIEW_CLASSES;
  InitCommonControlsEx(&icex);

  WNDCLASSEXW wcex = {};
  wcex.cbSize        = sizeof(wcex);
  wcex.style         = CS_HREDRAW | CS_VREDRAW;
  wcex.lpfnWndProc   = WndProc;
  wcex.hInstance     = GetModuleHandleW(nullptr);
  wcex.hCursor       = LoadCursor(nullptr, IDC_ARROW);
  wcex.hbrBackground = nullptr;
  wcex.lpszClassName = kWndClass;

  static bool sRegistered = false;
  if (!sRegistered)
  {
    RegisterClassExW(&wcex);
    sRegistered = true;
  }

  HWND ownerHwnd = nullptr;
  if (mParentHwnd && IsWindow(mParentHwnd))
  {
    ownerHwnd = GetAncestor(mParentHwnd, GA_ROOT);
    if (!ownerHwnd || !IsWindow(ownerHwnd))
      ownerHwnd = mParentHwnd;
  }

  int posX = CW_USEDEFAULT, posY = CW_USEDEFAULT;
  if (mHasSavedBounds)
  {
    POINT centre = { mWindowX + mWindowW / 2, mWindowY + mWindowH / 2 };
    if (MonitorFromPoint(centre, MONITOR_DEFAULTTONULL) != nullptr)
    {
      posX = mWindowX;
      posY = mWindowY;
    }
  }

  mHwnd = CreateWindowExW(
    0,
    kWndClass,
    L"PNAM Chain Editor",
    WS_OVERLAPPEDWINDOW | WS_VISIBLE,
    posX, posY, mWindowW, mWindowH,
    ownerHwnd, nullptr, GetModuleHandleW(nullptr), this);

  if (!mHwnd) return;
  mIsOpen = true;

  if (!mSlots.empty())
  {
    RefreshSlotList();
    UpdateTitleBar();
    SetEditPanelEnabled(false);
    SelectSlotIndex(0);
  }
}

void NAMPNAMEditorWindow::Close()
{
  if (!mIsOpen) return;
  mIsOpen = false;

  if (mHwnd)
  {
    RECT rc{};
    if (GetWindowRect(mHwnd, &rc))
    {
      mWindowX = rc.left;
      mWindowY = rc.top;
      mWindowW = rc.right - rc.left;
      mWindowH = rc.bottom - rc.top;
      mHasSavedBounds = true;
    }

    SaveSettings();
    DestroyWindow(mHwnd);
    mHwnd = nullptr;
  }
}

void NAMPNAMEditorWindow::BringToFront()
{
  if (mHwnd) { SetForegroundWindow(mHwnd); SetFocus(mHwnd); }
}

void NAMPNAMEditorWindow::LoadFile(const std::string& pnamPath)
{
  std::ifstream f(pnamPath);
  if (!f.is_open()) return;

  json j;
  try { j = json::parse(f); }
  catch (...) { return; }

  mSlots.clear();
  mCurrentFilePath = pnamPath;
  mLastOpenedPNAMPath = pnamPath;  // remember for next session

  if (j.contains("slots") && j["slots"].is_array())
  {
    for (const auto& jSlot : j["slots"])
    {
      ModelMapSlot slot;
      slot.ampGainMin  = jSlot.value("amp_gain_min", 0.0);
      slot.ampGainMax  = jSlot.value("amp_gain_max", 10.0);
      slot.namFilePath = jSlot.value("nam_path", std::string{});
      if (jSlot.contains("overrides"))
      {
        const auto& jOv = jSlot["overrides"];
        if (jOv.contains("output_level")) slot.overrides.outputLevel = jOv["output_level"].get<double>();
        if (jOv.contains("tone_bass"))    slot.overrides.toneBass    = jOv["tone_bass"].get<double>();
        if (jOv.contains("tone_mid"))     slot.overrides.toneMid     = jOv["tone_mid"].get<double>();
        if (jOv.contains("tone_treble"))  slot.overrides.toneTreble  = jOv["tone_treble"].get<double>();
      }
      mSlots.push_back(std::move(slot));
    }
  }

  mDirty = false;
  if (mHwnd)
  {
    RefreshSlotList();
    UpdateTitleBar();
    SetEditPanelEnabled(false);
    if (!mSlots.empty()) SelectSlotIndex(0);
    SaveSettings();  // persist LastPNAMPath immediately
  }
}

// ---- Win32 WndProc ----

LRESULT CALLBACK NAMPNAMEditorWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  NAMPNAMEditorWindow* pSelf = nullptr;
  if (msg == WM_CREATE)
  {
    auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
    pSelf = reinterpret_cast<NAMPNAMEditorWindow*>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pSelf));
    pSelf->mHwnd = hwnd;
  }
  else
  {
    pSelf = reinterpret_cast<NAMPNAMEditorWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }

  if (pSelf) return pSelf->HandleMessage(msg, wParam, lParam);
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---- Controls creation ----

void NAMPNAMEditorWindow::InitializeControls()
{
  if (!mDarkBgBrush)
    mDarkBgBrush = CreateSolidBrush(kDarkBg);
  if (!mEditBgBrush)
    mEditBgBrush = CreateSolidBrush(kEditBg);

  SetClassLongPtr(mHwnd, GCLP_HBRBACKGROUND, (LONG_PTR)mDarkBgBrush);
  DragAcceptFiles(mHwnd, TRUE);   // accept .nam drops

  RecreateFont();

  // All controls created at (0,0,0,0) — ResizeControls positions them.
  mHwndFileLbl       = MakeLabel(mHwnd, L"File:", 0, 0, 0, 0, mHFont);

  mHwndFilePathLabel = CreateWindowExW(0, L"STATIC", L"(new file)",
    WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS,
    0, 0, 0, 0, mHwnd,
    reinterpret_cast<HMENU>(IDC_FILE_PATH_LABEL), GetModuleHandleW(nullptr), nullptr);
  SendMessage(mHwndFilePathLabel, WM_SETFONT, (WPARAM)mHFont, TRUE);

  mHwndNewBtn    = MakeButton(mHwnd, IDC_NEW_FILE,     L"New",              0, 0, 0, 0, mHFont);
  mHwndOpenBtn   = MakeButton(mHwnd, IDC_OPEN_FILE,    L"Open\u2026",      0, 0, 0, 0, mHFont);
  mHwndSaveBtn   = MakeButton(mHwnd, IDC_SAVE_FILE,    L"Save",            0, 0, 0, 0, mHFont);
  mHwndSaveAsBtn = MakeButton(mHwnd, IDC_SAVE_AS_FILE, L"Save As\u2026",   0, 0, 0, 0, mHFont);
  mHwndFontDecBtn = MakeButton(mHwnd, IDC_FONT_DEC,    L"A\u2212",         0, 0, 0, 0, mHFont);
  mHwndFontIncBtn = MakeButton(mHwnd, IDC_FONT_INC,    L"A+",              0, 0, 0, 0, mHFont);

  mHwndHSep = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
    0, 0, 0, 0, mHwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

  mHwndSlotsLbl = MakeLabel(mHwnd, L"Slots:", 0, 0, 0, 0, mHFont);

  mHwndSlotList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
    WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
    0, 0, 0, 0,
    mHwnd, reinterpret_cast<HMENU>(IDC_SLOT_LIST), GetModuleHandleW(nullptr), nullptr);
  ListView_SetExtendedListViewStyle(mHwndSlotList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
  SendMessage(mHwndSlotList, WM_SETFONT, (WPARAM)mHFont, TRUE);
  ListView_SetBkColor(mHwndSlotList, kDarkBg);
  ListView_SetTextBkColor(mHwndSlotList, kDarkBg);
  ListView_SetTextColor(mHwndSlotList, kTextColor);

  LVCOLUMNA col = {};
  col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
  col.iSubItem = 0; col.cx = 36;  col.pszText = const_cast<char*>("#");     SendMessageA(mHwndSlotList, LVM_INSERTCOLUMNA, 0, (LPARAM)&col);
  col.iSubItem = 1; col.cx = 76;  col.pszText = const_cast<char*>("Min");   SendMessageA(mHwndSlotList, LVM_INSERTCOLUMNA, 1, (LPARAM)&col);
  col.iSubItem = 2; col.cx = 76;  col.pszText = const_cast<char*>("Max");   SendMessageA(mHwndSlotList, LVM_INSERTCOLUMNA, 2, (LPARAM)&col);
  col.iSubItem = 3; col.cx = 150; col.pszText = const_cast<char*>("Model"); SendMessageA(mHwndSlotList, LVM_INSERTCOLUMNA, 3, (LPARAM)&col);

  mHwndAddBtn    = MakeButton(mHwnd, IDC_ADD_SLOT,    L"+ Add",       0, 0, 0, 0, mHFont);
  mHwndRemoveBtn = MakeButton(mHwnd, IDC_REMOVE_SLOT, L"- Remove",    0, 0, 0, 0, mHFont);
  mHwndUpBtn     = MakeButton(mHwnd, IDC_MOVE_UP,     L"\u25B2 Up",   0, 0, 0, 0, mHFont);
  mHwndDownBtn   = MakeButton(mHwnd, IDC_MOVE_DOWN,   L"\u25BC Down", 0, 0, 0, 0, mHFont);
  mHwndDistributeBtn = MakeButton(mHwnd, IDC_DISTRIBUTE_GAIN, L"Distribute Gain 0\u219210", 0, 0, 0, 0, mHFont);

  mHwndVSep = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDVERT,
    0, 0, 0, 0, mHwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

  mHwndSelLbl      = MakeLabel(mHwnd, L"Selected Slot:", 0, 0, 0, 0, mHFont);
  mHwndGainLbl     = MakeLabel(mHwnd, L"Gain Min (0\u201310):", 0, 0, 0, 0, mHFont);
  mHwndGainMinEdit = MakeEdit(mHwnd, IDC_GAIN_MIN_EDIT, 0, 0, 0, 0, mHFont);
  mHwndGainMaxLbl  = MakeLabel(mHwnd, L"Max:", 0, 0, 0, 0, mHFont);
  mHwndGainMaxEdit = MakeEdit(mHwnd, IDC_GAIN_MAX_EDIT, 0, 0, 0, 0, mHFont);
  mHwndNamLbl      = MakeLabel(mHwnd, L"NAM File:", 0, 0, 0, 0, mHFont);
  mHwndNamPathEdit = MakeEdit(mHwnd, IDC_NAM_PATH_EDIT, 0, 0, 0, 0, mHFont);
  mHwndBrowseBtn   = MakeButton(mHwnd, IDC_BROWSE_NAM, L"Browse\u2026", 0, 0, 0, 0, mHFont);
  mHwndOvLbl       = MakeLabel(mHwnd, L"Overrides (optional):", 0, 0, 0, 0, mHFont);

  mHwndOvOutputCheck = MakeCheckBox(mHwnd, IDC_OV_OUTPUT_CHECK, L"Output Level (dB):", 0, 0, 0, 0, mHFont);
  mHwndOvOutputEdit  = MakeEdit(mHwnd, IDC_OV_OUTPUT_EDIT, 0, 0, 0, 0, mHFont);
  mHwndOvBassCheck   = MakeCheckBox(mHwnd, IDC_OV_BASS_CHECK, L"Bass (0\u201310):", 0, 0, 0, 0, mHFont);
  mHwndOvBassEdit    = MakeEdit(mHwnd, IDC_OV_BASS_EDIT, 0, 0, 0, 0, mHFont);
  mHwndOvMidCheck    = MakeCheckBox(mHwnd, IDC_OV_MID_CHECK, L"Mid (0\u201310):", 0, 0, 0, 0, mHFont);
  mHwndOvMidEdit     = MakeEdit(mHwnd, IDC_OV_MID_EDIT, 0, 0, 0, 0, mHFont);
  mHwndOvTrebleCheck = MakeCheckBox(mHwnd, IDC_OV_TREBLE_CHECK, L"Treble (0\u201310):", 0, 0, 0, 0, mHFont);
  mHwndOvTrebleEdit  = MakeEdit(mHwnd, IDC_OV_TREBLE_EDIT, 0, 0, 0, 0, mHFont);

  mHwndApplyBtn = MakeButton(mHwnd, IDC_APPLY_SLOT, L"Apply to Slot \u2713", 0, 0, 0, 0, mHFont);

  mHwndPreviewSlotBtn  = MakeButton(mHwnd, IDC_PREVIEW_SLOT,  L"\u25BA Preview Slot",  0, 0, 0, 0, mHFont);
  mHwndPreviewChainBtn = MakeButton(mHwnd, IDC_PREVIEW_CHAIN, L"\u25BA Preview Chain", 0, 0, 0, 0, mHFont);

  mHwndCloseBtn = MakeButton(mHwnd, IDC_CLOSE_BTN,  L"Close",                0, 0, 0, 0, mHFont);

  RECT rc;
  GetClientRect(mHwnd, &rc);
  ResizeControls(rc.right, rc.bottom);

  SetEditPanelEnabled(false);
  UpdateTitleBar();
}

// ---- Resize all controls to fit current window ----

void NAMPNAMEditorWindow::ResizeControls(int w, int h)
{
  if (!mHwnd) return;

  SendMessageW(mHwnd, WM_SETREDRAW, FALSE, 0);

  const int m = kM;

  // Clamp split position to valid range
  const int maxSplitX = w - 300;
  mSplitX = std::clamp(mSplitX, kMinSplitX, std::max(kMinSplitX, maxSplitX));

  // ---- Row 1: toolbar ----
  int ty = m;
  const int toolbarBtnW = 100;
  const int fontBtnW    = 45;
  const int toolbarGap  = 6;
  int btnBlock = 4 * toolbarBtnW + 2 * fontBtnW + 5 * toolbarGap;
  int btnX = w - m - btnBlock;
  int fileLblW = 50;
  int filePathW = std::max(60, btnX - fileLblW - m - m);

  // ---- Row 2: separator ----
  int sepY = ty + kBtnH + m;

  // ---- Row 3+: content area ----
  int contentTop = sepY + 8;
  int contentBot = h - kBtnH - m * 2;

  // Left panel — reserve two button rows at the bottom
  int listX = m;
  int listW = mSplitX - 2 * m;
  int slotsLblY = contentTop;
  int listTop = slotsLblY + kLabelH + 4;
  int slotBtnY = contentBot - kBtnH * 2 - 6; // Add/Remove/Up/Down row
  int distribBtnY = contentBot - kBtnH; // Distribute row
  int listH = std::max(40, slotBtnY - listTop - m);

  // Right panel
  int rx = mSplitX + 16;
  int rw = std::max(100, w - rx - m);
  int ry = contentTop;

  int selLblY   = ry;                          ry += kLabelH + 8;
  int gainRowY  = ry;                          ry += kCtrlH + 10;
  int namLblY   = ry;                          ry += kLabelH + 4;
  int namEditY  = ry;                          ry += kCtrlH + 14;
  int ovLblY    = ry;                          ry += kLabelH + 6;
  int ovLabelW  = 190;
  int ovEditX   = rx + ovLabelW + 8;
  int ovEditW   = 90;
  int ov1Y      = ry;                          ry += kCtrlH + 6;
  int ov2Y      = ry;                          ry += kCtrlH + 6;
  int ov3Y      = ry;                          ry += kCtrlH + 6;
  int ov4Y      = ry;                          ry += kCtrlH + 14;
  int applyY    = ry;                          ry += kBtnH + 10;
  int previewY  = ry;

  int browseW   = 100;
  int namEditW  = std::max(60, rw - browseW - 8);

  HDWP hdwp = BeginDeferWindowPos(39);
  if (!hdwp) { SendMessageW(mHwnd, WM_SETREDRAW, TRUE, 0); return; }

  auto dw = [&](HWND hw, int x, int y, int cw, int ch) {
    if (hw) hdwp = DeferWindowPos(hdwp, hw, nullptr, x, y, cw, ch, SWP_NOZORDER);
  };

  // Toolbar
  dw(mHwndFileLbl,       m, ty + 6, fileLblW, kLabelH);
  dw(mHwndFilePathLabel, m + fileLblW + 4, ty + 6, filePathW, kLabelH);

  int bx = btnX;
  dw(mHwndNewBtn,    bx, ty, toolbarBtnW, kBtnH); bx += toolbarBtnW + toolbarGap;
  dw(mHwndOpenBtn,   bx, ty, toolbarBtnW, kBtnH); bx += toolbarBtnW + toolbarGap;
  dw(mHwndSaveBtn,   bx, ty, toolbarBtnW, kBtnH); bx += toolbarBtnW + toolbarGap;
  dw(mHwndSaveAsBtn, bx, ty, toolbarBtnW, kBtnH); bx += toolbarBtnW + toolbarGap;
  dw(mHwndFontDecBtn, bx, ty, fontBtnW, kBtnH);   bx += fontBtnW + toolbarGap;
  dw(mHwndFontIncBtn, bx, ty, fontBtnW, kBtnH);

  // Separator
  dw(mHwndHSep, m, sepY, w - 2 * m, 2);

  // Left panel
  dw(mHwndSlotsLbl, m, slotsLblY, 80, kLabelH);
  dw(mHwndSlotList, listX, listTop, listW, listH);

  // Auto-size the Model column to fill remaining list width
  if (mHwndSlotList)
  {
    int fixedCols = 0;
    for (int c = 0; c < 3; ++c)
    {
      LVCOLUMNA lvc = {};
      lvc.mask = LVCF_WIDTH;
      SendMessageA(mHwndSlotList, LVM_GETCOLUMNA, c, (LPARAM)&lvc);
      fixedCols += lvc.cx;
    }
    int modelColW = std::max(50, listW - fixedCols - 24);
    LVCOLUMNA lvc = {};
    lvc.mask = LVCF_WIDTH;
    lvc.cx = modelColW;
    SendMessageA(mHwndSlotList, LVM_SETCOLUMNA, 3, (LPARAM)&lvc);
  }

  int sbx = m;
  dw(mHwndAddBtn, sbx, slotBtnY, 90, kBtnH);
  sbx += 96;
  dw(mHwndRemoveBtn, sbx, slotBtnY, 100, kBtnH);
  sbx += 106;
  dw(mHwndUpBtn, sbx, slotBtnY, 84, kBtnH);
  sbx += 90;
  dw(mHwndDownBtn, sbx, slotBtnY, 90, kBtnH);
  dw(mHwndDistributeBtn, m, distribBtnY, listW, kBtnH);

  // Vertical separator (drawn at split position)
  dw(mHwndVSep, mSplitX + 4, contentTop, 2, contentBot - contentTop);

  // Right panel
  dw(mHwndSelLbl,      rx, selLblY, 200, kLabelH);
  dw(mHwndGainLbl,     rx, gainRowY + 6, 160, kLabelH);
  dw(mHwndGainMinEdit, rx + 164, gainRowY, 82, kCtrlH);
  dw(mHwndGainMaxLbl,  rx + 256, gainRowY + 6, 50, kLabelH);
  dw(mHwndGainMaxEdit, rx + 308, gainRowY, 82, kCtrlH);
  dw(mHwndNamLbl,      rx, namLblY, 100, kLabelH);
  dw(mHwndNamPathEdit, rx, namEditY, namEditW, kCtrlH);
  dw(mHwndBrowseBtn,   rx + namEditW + 8, namEditY, browseW, kBtnH);
  dw(mHwndOvLbl,       rx, ovLblY, 220, kLabelH);

  dw(mHwndOvOutputCheck, rx, ov1Y + 4, ovLabelW, kLabelH);
  dw(mHwndOvOutputEdit,  ovEditX, ov1Y, ovEditW, kCtrlH);
  dw(mHwndOvBassCheck,   rx, ov2Y + 4, ovLabelW, kLabelH);
  dw(mHwndOvBassEdit,    ovEditX, ov2Y, ovEditW, kCtrlH);
  dw(mHwndOvMidCheck,    rx, ov3Y + 4, ovLabelW, kLabelH);
  dw(mHwndOvMidEdit,     ovEditX, ov3Y, ovEditW, kCtrlH);
  dw(mHwndOvTrebleCheck, rx, ov4Y + 4, ovLabelW, kLabelH);
  dw(mHwndOvTrebleEdit,  ovEditX, ov4Y, ovEditW, kCtrlH);

  dw(mHwndApplyBtn,        rx,             applyY,   180, kBtnH);

  // Preview row
  int previewBtnW = std::max(60, (rw - 8) / 2);
  dw(mHwndPreviewSlotBtn,  rx,                       previewY, previewBtnW, kBtnH);
  dw(mHwndPreviewChainBtn, rx + previewBtnW + 8,     previewY, previewBtnW, kBtnH);

  // Close button — bottom-right
  dw(mHwndCloseBtn, w - m - 110, h - m - kBtnH, 110, kBtnH);

  EndDeferWindowPos(hdwp);

  SendMessageW(mHwnd, WM_SETREDRAW, TRUE, 0);
  RedrawWindow(mHwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

// ---- Slot list ----

void NAMPNAMEditorWindow::RefreshSlotList()
{
  if (!mHwndSlotList) return;
  SendMessageW(mHwndSlotList, LVM_DELETEALLITEMS, 0, 0);

  for (int i = 0; i < static_cast<int>(mSlots.size()); ++i)
  {
    const auto& s = mSlots[i];

    LVITEMW lvi = {};
    lvi.mask     = LVIF_TEXT;
    lvi.iItem    = i;

    std::wstring idx = std::to_wstring(i + 1);
    lvi.iSubItem = 0; lvi.pszText = idx.data();
    SendMessageW(mHwndSlotList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

    std::wostringstream mn, mx;
    mn << std::fixed << std::setprecision(2) << s.ampGainMin;
    mx << std::fixed << std::setprecision(2) << s.ampGainMax;
    std::wstring mnStr = mn.str(), mxStr = mx.str();

    lvi.iSubItem = 1; lvi.pszText = mnStr.data();
    SendMessageW(mHwndSlotList, LVM_SETITEMTEXTW, i, (LPARAM)&lvi);

    lvi.iSubItem = 2; lvi.pszText = mxStr.data();
    SendMessageW(mHwndSlotList, LVM_SETITEMTEXTW, i, (LPARAM)&lvi);

    std::wstring stem = std::filesystem::path(s.namFilePath).stem().wstring();
    if (stem.empty()) stem = L"(none)";
    lvi.iSubItem = 3; lvi.pszText = stem.data();
    SendMessageW(mHwndSlotList, LVM_SETITEMTEXTW, i, (LPARAM)&lvi);
  }
}

int NAMPNAMEditorWindow::GetSelectedSlotIndex() const
{
  if (!mHwndSlotList) return -1;
  return ListView_GetNextItem(mHwndSlotList, -1, LVNI_SELECTED);
}

void NAMPNAMEditorWindow::SelectSlotIndex(int idx)
{
  if (!mHwndSlotList || idx < 0 || idx >= static_cast<int>(mSlots.size())) return;
  ListView_SetItemState(mHwndSlotList, idx,
    LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
  ListView_EnsureVisible(mHwndSlotList, idx, FALSE);
  UpdateEditPanelFromSlot(idx);
  SetEditPanelEnabled(true);
}

// ---- Edit panel ----

void NAMPNAMEditorWindow::UpdateEditPanelFromSlot(int idx)
{
  if (idx < 0 || idx >= static_cast<int>(mSlots.size())) return;
  const auto& slot = mSlots[idx];

  SetDoubleText(mHwndGainMinEdit, slot.ampGainMin);
  SetDoubleText(mHwndGainMaxEdit, slot.ampGainMax);
  SetWindowTextW(mHwndNamPathEdit, Utf8ToWide(slot.namFilePath).c_str());

// --
  auto applyOv = [](HWND checkHw, HWND editHw, const std::optional<double>& v) {
    bool has = v.has_value();
    Button_SetCheck(checkHw, has ? BST_CHECKED : BST_UNCHECKED);
    if (has) SetDoubleText(editHw, *v); else SetWindowTextW(editHw, L"");
    EnableWindow(editHw, has);
  };

  applyOv(mHwndOvOutputCheck, mHwndOvOutputEdit, slot.overrides.outputLevel);
  applyOv(mHwndOvBassCheck,   mHwndOvBassEdit,   slot.overrides.toneBass);
  applyOv(mHwndOvMidCheck,    mHwndOvMidEdit,    slot.overrides.toneMid);
  applyOv(mHwndOvTrebleCheck, mHwndOvTrebleEdit, slot.overrides.toneTreble);
}

ModelMapSlot NAMPNAMEditorWindow::ReadEditPanelToSlot() const
{
  ModelMapSlot slot;
  slot.ampGainMin  = ParseDouble(mHwndGainMinEdit, 0.0);
  slot.ampGainMax  = ParseDouble(mHwndGainMaxEdit, 10.0);
  slot.namFilePath = GetWndText(mHwndNamPathEdit);

  if (Button_GetCheck(mHwndOvOutputCheck) == BST_CHECKED)
    slot.overrides.outputLevel = ParseDouble(mHwndOvOutputEdit, 0.0);
  if (Button_GetCheck(mHwndOvBassCheck) == BST_CHECKED)
    slot.overrides.toneBass = ParseDouble(mHwndOvBassEdit, 5.0);
  if (Button_GetCheck(mHwndOvMidCheck) == BST_CHECKED)
    slot.overrides.toneMid = ParseDouble(mHwndOvMidEdit, 5.0);
  if (Button_GetCheck(mHwndOvTrebleCheck) == BST_CHECKED)
    slot.overrides.toneTreble = ParseDouble(mHwndOvTrebleEdit, 5.0);

  return slot;
}

void NAMPNAMEditorWindow::SetEditPanelEnabled(bool enabled)
{
  auto en = [enabled](HWND hw) { if (hw) EnableWindow(hw, enabled); };
  en(mHwndGainMinEdit);
  en(mHwndGainMaxEdit);
  en(mHwndNamPathEdit);
  en(mHwndOvOutputCheck);
  en(mHwndOvBassCheck);
  en(mHwndOvMidCheck);
  en(mHwndOvTrebleCheck);

  if (enabled)
  {
    EnableWindow(mHwndOvOutputEdit, Button_GetCheck(mHwndOvOutputCheck) == BST_CHECKED);
    EnableWindow(mHwndOvBassEdit,   Button_GetCheck(mHwndOvBassCheck)   == BST_CHECKED);
    EnableWindow(mHwndOvMidEdit,    Button_GetCheck(mHwndOvMidCheck)    == BST_CHECKED);
    EnableWindow(mHwndOvTrebleEdit, Button_GetCheck(mHwndOvTrebleCheck) == BST_CHECKED);
  }
  else
  {
    en(mHwndOvOutputEdit);
    en(mHwndOvBassEdit);
    en(mHwndOvMidEdit);
    en(mHwndOvTrebleEdit);
  }

  en(mHwndApplyBtn);
  en(mHwndBrowseBtn);

  // Preview Slot requires a slot to be selected AND it has a callback
  if (mHwndPreviewSlotBtn)
    EnableWindow(mHwndPreviewSlotBtn, enabled && mOnPreviewSlot != nullptr);

  // Preview Chain is always available as long as there are slots and a callback
  if (mHwndPreviewChainBtn)
    EnableWindow(mHwndPreviewChainBtn, !mSlots.empty() && mOnPreviewChain != nullptr);
}

// ---- Event handlers ----

void NAMPNAMEditorWindow::OnSelectionChanged()
{
  int idx = GetSelectedSlotIndex();
  if (idx >= 0 && idx < static_cast<int>(mSlots.size()))
  { UpdateEditPanelFromSlot(idx); SetEditPanelEnabled(true); }
  else
  { SetEditPanelEnabled(false); }
}

void NAMPNAMEditorWindow::OnAddSlot()
{
  ModelMapSlot slot;
  if (!mSlots.empty())
  {
    slot.ampGainMin = mSlots.back().ampGainMax;
    slot.ampGainMax = std::min(10.0, slot.ampGainMin + 2.0);
  }
  mSlots.push_back(slot);
  mDirty = true;
  RefreshSlotList();
  SelectSlotIndex(static_cast<int>(mSlots.size()) - 1);
  UpdateTitleBar();
}

void NAMPNAMEditorWindow::OnRemoveSlot()
{
  int idx = GetSelectedSlotIndex();
  if (idx < 0 || idx >= static_cast<int>(mSlots.size())) return;
  mSlots.erase(mSlots.begin() + idx);
  mDirty = true;
  RefreshSlotList();
  int newSel = std::min(idx, static_cast<int>(mSlots.size()) - 1);
  if (newSel >= 0) SelectSlotIndex(newSel);
  else SetEditPanelEnabled(false);
  UpdateTitleBar();
}

void NAMPNAMEditorWindow::OnMoveUp()
{
  int idx = GetSelectedSlotIndex();
  if (idx <= 0 || idx >= static_cast<int>(mSlots.size())) return;
  std::swap(mSlots[idx], mSlots[idx - 1]);
  mDirty = true;
  RefreshSlotList();
  SelectSlotIndex(idx - 1);
  UpdateTitleBar();
}

void NAMPNAMEditorWindow::OnMoveDown()
{
  int idx = GetSelectedSlotIndex();
  if (idx < 0 || idx >= static_cast<int>(mSlots.size()) - 1) return;
  std::swap(mSlots[idx], mSlots[idx + 1]);
  mDirty = true;
  RefreshSlotList();
  SelectSlotIndex(idx + 1);
  UpdateTitleBar();
}

void NAMPNAMEditorWindow::OnBrowseNAM()
{
  wchar_t buf[MAX_PATH] = {};
  OPENFILENAMEW ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner   = mHwnd;
  ofn.lpstrFilter = L"NAM Model Files (*.nam)\0*.nam\0All Files (*.*)\0*.*\0";
  ofn.lpstrFile   = buf;
  ofn.nMaxFile    = MAX_PATH;
  ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  if (GetOpenFileNameW(&ofn))
    SetWindowTextW(mHwndNamPathEdit, buf);
}

void NAMPNAMEditorWindow::OnApplySlot()
{
  int idx = GetSelectedSlotIndex();
  if (idx < 0 || idx >= static_cast<int>(mSlots.size())) return;
  mSlots[idx] = ReadEditPanelToSlot();
  mDirty = true;
  RefreshSlotList();
  SelectSlotIndex(idx);
  UpdateTitleBar();
}

void NAMPNAMEditorWindow::OnDistributeGain()
{
  if (mSlots.empty())
    return;

  const int n = static_cast<int>(mSlots.size());
  const double stepRaw = 10.0 / n;

  for (int i = 0; i < n; ++i)
  {
    // Round to 2 dp; leave a 0.01 gap between non-last max and next min so
    // adjacent boundaries are visually distinct in the slot list.
    const double rawMin = i * stepRaw;
    const double rawMax = (i + 1) * stepRaw;
    mSlots[i].ampGainMin = std::round(rawMin * 100.0) / 100.0;
    mSlots[i].ampGainMax = (i < n - 1)
                             ? std::round(rawMax * 100.0) / 100.0 - 0.01
                             : 10.0;
  }

  mDirty = true;
  RefreshSlotList();

  // Refresh the edit panel if a slot is currently selected
  const int sel = GetSelectedSlotIndex();
  if (sel >= 0)
    UpdateEditPanelFromSlot(sel);

  UpdateTitleBar();
}

void NAMPNAMEditorWindow::OnPreviewSlot()
{
  if (!mOnPreviewSlot)
    return;
  int idx = GetSelectedSlotIndex();
  if (idx < 0 || idx >= static_cast<int>(mSlots.size()))
    return;

  // Read current edit-panel values so the preview reflects any unsaved edits
  ModelMapSlot slot = ReadEditPanelToSlot();
  if (slot.namFilePath.empty())
    return;

  mOnPreviewSlot(slot);
}

void NAMPNAMEditorWindow::OnPreviewChain()
{
  if (!mOnPreviewChain)
    return;
  if (mSlots.empty())
    return;

  // Ensure the chain is saved before handing the path to the plugin
  if (mCurrentFilePath.empty())
  {
    // No file yet — force a Save As
    OnSaveAs();
    if (mCurrentFilePath.empty())
      return; // user cancelled
  }
  else
  {
    // Save in place so the plugin picks up any unsaved edits
    if (!SaveToFile(mCurrentFilePath))
      return;
    mDirty = false;
    UpdateTitleBar();
  }

  mOnPreviewChain(mCurrentFilePath);
}

void NAMPNAMEditorWindow::OnDropFiles(HDROP hDrop)
{
  const UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);

  POINT dropPt{};
  DragQueryPoint(hDrop, &dropPt);

  std::vector<std::string> namPaths;
  namPaths.reserve(count);
  for (UINT i = 0; i < count; ++i)
  {
    wchar_t buf[MAX_PATH] = {};
    if (!DragQueryFileW(hDrop, i, buf, MAX_PATH))
      continue;
    if (std::filesystem::path(buf).extension() == L".nam")
      namPaths.push_back(WideToUtf8(buf));
  }
  DragFinish(hDrop);

  if (namPaths.empty())
    return;

  if (namPaths.size() == 1)
  {
    // Single drop: hit-test for slot replacement
    POINT lvPt = dropPt;
    MapWindowPoints(mHwnd, mHwndSlotList, &lvPt, 1);

    LVHITTESTINFO hti{};
    hti.pt = lvPt;
    const int hitIdx = ListView_HitTest(mHwndSlotList, &hti);

    if (hitIdx >= 0 && hitIdx < static_cast<int>(mSlots.size()))
    {
      mSlots[hitIdx].namFilePath = namPaths[0];
      mDirty = true;
      RefreshSlotList();
      SelectSlotIndex(hitIdx);
      UpdateTitleBar();
      return;
    }

    // Single file outside rows → append with auto-continued gain range
    ModelMapSlot slot;
    if (!mSlots.empty())
    {
      slot.ampGainMin = mSlots.back().ampGainMax;
      slot.ampGainMax = std::min(10.0, slot.ampGainMin + 2.0);
    }
    slot.namFilePath = namPaths[0];
    mSlots.push_back(slot);
  }
  else
  {
    // Multiple files → proportional gain intervals
    const double gainStart = mSlots.empty() ? 0.0 : mSlots.back().ampGainMax;
    const int n = static_cast<int>(namPaths.size());
    const double range = 10.0 - gainStart;
    const double step = (range > 0.0) ? range / n : 0.0;

    for (int i = 0; i < n; ++i)
    {
      ModelMapSlot slot;
      slot.namFilePath = namPaths[i];
      if (step > 0.0)
      {
        slot.ampGainMin = gainStart + i * step;
        slot.ampGainMax = gainStart + (i + 1) * step;
      }
      else
      {
        // Last slot was already at 10 — stack at 10→10
        slot.ampGainMin = 10.0;
        slot.ampGainMax = 10.0;
      }
      mSlots.push_back(slot);
    }
  }

  mDirty = true;
  RefreshSlotList();
  SelectSlotIndex(static_cast<int>(mSlots.size()) - 1);
  UpdateTitleBar();
}

void NAMPNAMEditorWindow::OnNewFile()
{
  if (!PromptSaveIfDirty()) return;
  mSlots.clear();
  mCurrentFilePath.clear();
  mDirty = false;
  RefreshSlotList();
  SetEditPanelEnabled(false);
  UpdateTitleBar();
}

void NAMPNAMEditorWindow::OnOpenFile()
{
  if (!PromptSaveIfDirty()) return;
  wchar_t buf[MAX_PATH] = {};
  OPENFILENAMEW ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner   = mHwnd;
  ofn.lpstrFilter = L"PNAM Chain Files (*.pnam)\0*.pnam\0All Files (*.*)\0*.*\0";
  ofn.lpstrFile   = buf;
  ofn.nMaxFile    = MAX_PATH;
  ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  if (GetOpenFileNameW(&ofn))
    LoadFile(WideToUtf8(buf));
}

void NAMPNAMEditorWindow::OnSave()
{
  const std::string overlaps = CheckSlotOverlaps(mSlots);
  if (!overlaps.empty())
  {
    std::wstring msg = L"Warning: the following slots have overlapping gain ranges.\n"
                       L"Earlier slots will shadow later ones in the overlap zone.\n\n"
                     + Utf8ToWide(overlaps)
                     + L"\nSave anyway?";
    if (MessageBoxW(mHwnd, msg.c_str(), L"Overlapping Slots", MB_YESNO | MB_ICONWARNING) != IDYES)
      return;
  }
  if (mCurrentFilePath.empty()) { OnSaveAs(); return; }
  if (SaveToFile(mCurrentFilePath))
  {
    mDirty = false;
    UpdateTitleBar();
    if (mOnSaved) mOnSaved(mCurrentFilePath);
  }
}

void NAMPNAMEditorWindow::OnSaveAs()
{
  wchar_t buf[MAX_PATH] = {};
  if (!mCurrentFilePath.empty())
    wcsncpy_s(buf, Utf8ToWide(mCurrentFilePath).c_str(), MAX_PATH - 1);

  OPENFILENAMEW ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner   = mHwnd;
  ofn.lpstrFilter = L"PNAM Chain Files (*.pnam)\0*.pnam\0All Files (*.*)\0*.*\0";
  ofn.lpstrFile   = buf;
  ofn.nMaxFile    = MAX_PATH;
  ofn.lpstrDefExt = L"pnam";
  ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
  if (GetSaveFileNameW(&ofn))
  {
    mCurrentFilePath = WideToUtf8(buf);
    if (SaveToFile(mCurrentFilePath))
    {
      mDirty = false;
      UpdateTitleBar();
      if (mOnSaved) mOnSaved(mCurrentFilePath);
    }
  }
}

bool NAMPNAMEditorWindow::SaveToFile(const std::string& path)
{
  json j;
  j["pnam_version"] = 1;
  j["slots"]        = json::array();

  for (const auto& slot : mSlots)
  {
    json jSlot;
    jSlot["amp_gain_min"] = slot.ampGainMin;
    jSlot["amp_gain_max"] = slot.ampGainMax;
    jSlot["nam_path"]     = slot.namFilePath;

    json jOv = json::object();
    if (slot.overrides.outputLevel.has_value()) jOv["output_level"] = *slot.overrides.outputLevel;
    if (slot.overrides.toneBass.has_value())    jOv["tone_bass"]    = *slot.overrides.toneBass;
    if (slot.overrides.toneMid.has_value())     jOv["tone_mid"]     = *slot.overrides.toneMid;
    if (slot.overrides.toneTreble.has_value())  jOv["tone_treble"]  = *slot.overrides.toneTreble;
    if (!jOv.empty()) jSlot["overrides"] = jOv;

    j["slots"].push_back(jSlot);
  }

  try
  {
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << j.dump(2);
    return true;
  }
  catch (...) { return false; }
}

bool NAMPNAMEditorWindow::PromptSaveIfDirty()
{
  if (!mDirty) return true;
  int res = MessageBoxW(mHwnd,
    L"You have unsaved changes. Save before continuing?",
    L"Unsaved Changes", MB_YESNOCANCEL | MB_ICONQUESTION);
  if (res == IDCANCEL) return false;
  if (res == IDYES) OnSave();
  return true;
}

void NAMPNAMEditorWindow::UpdateTitleBar()
{
  std::wstring title = L"PNAM Chain Editor";
  if (mDirty) title += L" *";
  if (mHwnd) SetWindowTextW(mHwnd, title.c_str());

  if (mHwndFilePathLabel)
  {
    SetWindowTextW(mHwndFilePathLabel,
      mCurrentFilePath.empty() ? L"(new file)" : Utf8ToWide(mCurrentFilePath).c_str());
  }
}

// ---- Settings persistence ----

std::string NAMPNAMEditorWindow::GetSettingsFilePath()
{
  namespace fs = std::filesystem;

  std::string baseDir;
  if (const char* appData = getenv("APPDATA"))
    baseDir = appData;

  if (baseDir.empty())
    return {};

  fs::path dir = fs::path(baseDir) / "NeuralAmpModeler";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return ec ? std::string{} : (dir / "PNAMEditor.settings").string();
}

void NAMPNAMEditorWindow::LoadSettings()
{
  const std::string path = GetSettingsFilePath();
  if (path.empty())
    return;

  std::ifstream file(path);
  if (!file.is_open())
    return;

  std::string line;
  while (std::getline(file, line))
  {
    const auto eq = line.find('=');
    if (eq == std::string::npos)
      continue;

    const std::string key = line.substr(0, eq);
    const std::string val = line.substr(eq + 1);

    try
    {
      if (key == "FontSize")
      {
        const int v = std::stoi(val);
        if (v >= kMinFontSize && v <= kMaxFontSize)
          mFontSize = v;
      }
      else if (key == "WindowX")
      {
        mWindowX = std::stoi(val);
        mHasSavedBounds = true;
      }
      else if (key == "WindowY")
      {
        mWindowY = std::stoi(val);
        mHasSavedBounds = true;
      }
      else if (key == "WindowW")
      {
        const int v = std::stoi(val);
        if (v >= kMinWndW)
          mWindowW = v;
      }
      else if (key == "WindowH")
      {
        const int v = std::stoi(val);
        if (v >= kMinWndH)
          mWindowH = v;
      }
      else if (key == "SplitX")
      {
        const int v = std::stoi(val);
        if (v >= kMinSplitX)
          mSplitX = v;
      }
      else if (key == "LastPNAMPath")
      {
        // Only restore if the file still exists on disk
        if (!val.empty() && std::filesystem::exists(val))
          mLastOpenedPNAMPath = val;
      }
    }
    catch (...)
    {
    }
  }
}

void NAMPNAMEditorWindow::SaveSettings()
{
  const std::string path = GetSettingsFilePath();
  if (path.empty())
    return;

  std::ofstream file(path, std::ios::trunc);
  if (!file.is_open())
    return;

  file << "FontSize=" << mFontSize << "\n";
  file << "WindowX=" << mWindowX << "\n";
  file << "WindowY=" << mWindowY << "\n";
  file << "WindowW=" << mWindowW << "\n";
  file << "WindowH=" << mWindowH << "\n";
  file << "SplitX=" << mSplitX << "\n";

  // Use current file path if open, otherwise keep whatever was last opened
  const std::string& lastPath = mCurrentFilePath.empty() ? mLastOpenedPNAMPath : mCurrentFilePath;
  if (!lastPath.empty())
    file << "LastPNAMPath=" << lastPath << "\n";
} // end of SaveSettings()



// ---- Message handler ----
LRESULT NAMPNAMEditorWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg)
  {
    case WM_CREATE:
      InitializeControls();
      return 0;

    case WM_SIZE:
    {
      if (wParam != SIZE_MINIMIZED)
        ResizeControls(LOWORD(lParam), HIWORD(lParam));
      return 0;
    }

    case WM_GETMINMAXINFO:
    {
      auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
      mmi->ptMinTrackSize.x = kMinWndW;
      mmi->ptMinTrackSize.y = kMinWndH;
      return 0;
    }

    // ---- Splitter drag ----
    case WM_LBUTTONDOWN:
    {
      int mx = GET_X_LPARAM(lParam);
      if (mx >= mSplitX - kSplitHitW && mx <= mSplitX + kSplitHitW + 8)
      {
        mDraggingSplitter = true;
        SetCapture(mHwnd);
      }
      return 0;
    }

    case WM_LBUTTONUP:
    {
      if (mDraggingSplitter)
      {
        mDraggingSplitter = false;
        ReleaseCapture();
      }
      return 0;
    }

    case WM_MOUSEMOVE:
    {
      int mx = GET_X_LPARAM(lParam);
      if (mx >= mSplitX - kSplitHitW && mx <= mSplitX + kSplitHitW + 8)
        SetCursor(LoadCursor(nullptr, IDC_SIZEWE));

      if (mDraggingSplitter)
      {
        RECT rc;
        GetClientRect(mHwnd, &rc);
        int newSplit = std::clamp(mx, kMinSplitX, std::max(kMinSplitX, (int)rc.right - 300));
        if (newSplit != mSplitX)
        {
          mSplitX = newSplit;
          ResizeControls(rc.right, rc.bottom);
        }
      }
      return 0;
    }

    case WM_SETCURSOR:
    {
      if (LOWORD(lParam) == HTCLIENT)
      {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(mHwnd, &pt);
        if (pt.x >= mSplitX - kSplitHitW && pt.x <= mSplitX + kSplitHitW + 8)
        {
          SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
          return TRUE;
        }
      }
      break;
    }

    // ---- Dark theme colour handlers ----
    case WM_CTLCOLORSTATIC:
    {
      HDC hdcStatic = (HDC)wParam;
      SetTextColor(hdcStatic, kTextColor);
      SetBkColor(hdcStatic, kDarkBg);
      return (LRESULT)mDarkBgBrush;
    }

    case WM_CTLCOLOREDIT:
    {
      HDC hdcEdit = (HDC)wParam;
      SetTextColor(hdcEdit, kTextColor);
      SetBkColor(hdcEdit, kEditBg);
      return (LRESULT)mEditBgBrush;
    }

    case WM_CTLCOLORLISTBOX:
    {
      HDC hdcList = (HDC)wParam;
      SetTextColor(hdcList, kTextColor);
      SetBkColor(hdcList, kDarkBg);
      return (LRESULT)mDarkBgBrush;
    }

    case WM_CTLCOLORBTN:
    {
      HDC hdcBtn = (HDC)wParam;
      SetTextColor(hdcBtn, kTextColor);
      SetBkColor(hdcBtn, kDarkBg);
      return (LRESULT)mDarkBgBrush;
    }

    case WM_DRAWITEM:
    {
      LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lParam;
      if (pDIS->CtlType == ODT_BUTTON)
      {
        wchar_t buttonText[256] = {};
        GetWindowTextW(pDIS->hwndItem, buttonText, 256);

        COLORREF bgColor = kBtnBg;
        COLORREF textColor = kTextColor;

        if (pDIS->itemState & ODS_DISABLED)
        {
          bgColor = kEditBg;
          textColor = kDisabledTx;
        }
        else if (pDIS->itemState & ODS_SELECTED)
        {
          bgColor = kBtnPressed;
          textColor = RGB(255, 255, 255);
        }
        else if (pDIS->itemState & ODS_FOCUS)
        {
          bgColor = RGB(80, 80, 80);
        }

        HBRUSH hBrush = CreateSolidBrush(bgColor);
        FillRect(pDIS->hDC, &pDIS->rcItem, hBrush);
        DeleteObject(hBrush);

        HPEN hPen = CreatePen(PS_SOLID, 1, kBtnBorder);
        HPEN hOldPen = (HPEN)SelectObject(pDIS->hDC, hPen);
        MoveToEx(pDIS->hDC, pDIS->rcItem.left, pDIS->rcItem.bottom - 1, nullptr);
        LineTo(pDIS->hDC, pDIS->rcItem.left, pDIS->rcItem.top);
        LineTo(pDIS->hDC, pDIS->rcItem.right - 1, pDIS->rcItem.top);
        LineTo(pDIS->hDC, pDIS->rcItem.right - 1, pDIS->rcItem.bottom - 1);
        LineTo(pDIS->hDC, pDIS->rcItem.left, pDIS->rcItem.bottom - 1);
        SelectObject(pDIS->hDC, hOldPen);
        DeleteObject(hPen);

        HFONT hOldFont = (HFONT)SelectObject(pDIS->hDC, mHFont);
        SetTextColor(pDIS->hDC, textColor);
        SetBkMode(pDIS->hDC, TRANSPARENT);
        DrawTextW(pDIS->hDC, buttonText, -1, &pDIS->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(pDIS->hDC, hOldFont);

        if (pDIS->itemState & ODS_FOCUS)
        {
          RECT focusRect = pDIS->rcItem;
          InflateRect(&focusRect, -2, -2);
          DrawFocusRect(pDIS->hDC, &focusRect);
        }

        return TRUE;
      }
      break;
    }

    case WM_COMMAND:
    {
      const int id = LOWORD(wParam);
      switch (id)
      {
        case IDC_NEW_FILE:        OnNewFile();        break;
        case IDC_OPEN_FILE:       OnOpenFile();       break;
        case IDC_SAVE_FILE:       OnSave();           break;
        case IDC_SAVE_AS_FILE:    OnSaveAs();         break;
        case IDC_ADD_SLOT:        OnAddSlot();        break;
        case IDC_REMOVE_SLOT:     OnRemoveSlot();     break;
        case IDC_MOVE_UP:         OnMoveUp();         break;
        case IDC_MOVE_DOWN:       OnMoveDown();       break;
        case IDC_BROWSE_NAM:      OnBrowseNAM();      break;
        case IDC_APPLY_SLOT:      OnApplySlot();      break;
        case IDC_FONT_DEC:        DecreaseFontSize(); break;
        case IDC_FONT_INC:        IncreaseFontSize(); break;
        case IDC_CLOSE_BTN:
          if (PromptSaveIfDirty())
          {
            mIsOpen = false;
            RECT rc{};
            if (GetWindowRect(mHwnd, &rc))
            {
              mWindowX = rc.left;
              mWindowY = rc.top;
              mWindowW = rc.right - rc.left;
              mWindowH = rc.bottom - rc.top;
              mHasSavedBounds = true;
            }
            SaveSettings();
            DestroyWindow(mHwnd);
            mHwnd = nullptr;
          }
          break;
        case IDC_OV_OUTPUT_CHECK:
          EnableWindow(mHwndOvOutputEdit, Button_GetCheck(mHwndOvOutputCheck) == BST_CHECKED); break;
        case IDC_OV_BASS_CHECK:
          EnableWindow(mHwndOvBassEdit,   Button_GetCheck(mHwndOvBassCheck)   == BST_CHECKED); break;
        case IDC_OV_MID_CHECK:
          EnableWindow(mHwndOvMidEdit,    Button_GetCheck(mHwndOvMidCheck)    == BST_CHECKED); break;
        case IDC_OV_TREBLE_CHECK:
          EnableWindow(mHwndOvTrebleEdit, Button_GetCheck(mHwndOvTrebleCheck) == BST_CHECKED); break;
        case IDC_PREVIEW_SLOT:    OnPreviewSlot();    break;
        case IDC_PREVIEW_CHAIN:   OnPreviewChain();   break;
        case IDC_DISTRIBUTE_GAIN: OnDistributeGain(); break;
      }
      return 0;
    }

    case WM_NOTIFY:
    {
      auto* hdr = reinterpret_cast<NMHDR*>(lParam);
      if (hdr->idFrom == IDC_SLOT_LIST && hdr->code == LVN_ITEMCHANGED)
      {
        auto* nm = reinterpret_cast<NMLISTVIEW*>(lParam);
        if (nm->uNewState & LVIS_SELECTED) OnSelectionChanged();
      }

      if (hdr->code == NM_CUSTOMDRAW && hdr->idFrom == IDC_SLOT_LIST)
      {
        auto* lvcd = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
        switch (lvcd->nmcd.dwDrawStage)
        {
          case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;
          case CDDS_ITEMPREPAINT:
            lvcd->clrText   = kTextColor;
            lvcd->clrTextBk = kDarkBg;
            return CDRF_DODEFAULT;
        }
      }

      return 0;
    }

    case WM_CLOSE:
      if (!PromptSaveIfDirty()) return 0;
      mIsOpen = false;
      {
        RECT rc{};
        if (GetWindowRect(mHwnd, &rc))
        {
          mWindowX = rc.left;
          mWindowY = rc.top;
          mWindowW = rc.right - rc.left;
          mWindowH = rc.bottom - rc.top;
          mHasSavedBounds = true;
        }
        SaveSettings();
      }
      if (mOnWindowClosed) mOnWindowClosed();
      DestroyWindow(mHwnd);
      mHwnd = nullptr;
      return 0;

    case WM_DESTROY:
      mIsOpen = false;
      return 0;

    case WM_DROPFILES:
      OnDropFiles(reinterpret_cast<HDROP>(wParam));
      return 0;
  }
  return DefWindowProcW(mHwnd, msg, wParam, lParam);
}

#endif