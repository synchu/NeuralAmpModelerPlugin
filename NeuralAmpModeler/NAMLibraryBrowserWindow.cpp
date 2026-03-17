#include "NAMLibraryBrowserWindow.h"
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <set>
#include <unordered_set>
#include <cctype>
#include <cstdlib> // getenv
#include <cstdio>  // snprintf

namespace
{
  std::string ToLowerAscii(std::string value)
  {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    return value;
  }


  std::string Trim(const std::string& str)
  {
    const auto strBegin = str.find_first_not_of(" \t\n\r");
    if (strBegin == std::string::npos)
      return "";
    const auto strEnd = str.find_last_not_of(" \t\n\r");
    return str.substr(strBegin, strEnd - strBegin + 1);
  }
}

// FIX: Check for both OS_WIN and standard _WIN32 macro
#if defined(OS_WIN) || defined(_WIN32)
#ifndef OS_WIN
#define OS_WIN
#endif

#include <windowsx.h>
#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")

namespace
{
  std::wstring Utf8ToWide(const std::string& text)
  {
    if (text.empty())
      return {};

    const int wideLen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (wideLen <= 1)
      return {};

    std::wstring result(static_cast<size_t>(wideLen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), wideLen);
    result.pop_back();
    return result;
  }

  std::string WideToUtf8(const wchar_t* text)
  {
    if (!text || text[0] == L'\0')
      return {};

    const int utf8Len = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 1)
      return {};

    std::string result(static_cast<size_t>(utf8Len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), utf8Len, nullptr, nullptr);
    result.pop_back();
    return result;
  }

  // In-memory (process lifetime) UI state. No settings file persistence.
  struct NAMLibraryBrowserSessionState
  {
    std::string lastSearchQuery;
    std::string lastSelectedTag;
    std::unordered_map<std::string, bool> expandedState; // node->id -> expanded
  };

  NAMLibraryBrowserSessionState& GetBrowserSessionState()
  {
    static NAMLibraryBrowserSessionState s;
    return s;
  }
}

// Control IDs
#define IDC_TREEVIEW       1001
#define IDC_SEARCH_EDIT    1002
#define IDC_LOAD_BUTTON    1003
#define IDC_CANCEL_BUTTON  1004
#define IDC_SEARCH_LABEL   1005
#define IDC_FONT_INC       1006
#define IDC_FONT_DEC       1007
#define IDC_TAG_LABEL      1008
#define IDC_TAG_COMBO      1009
#define IDC_TAG_RESET      1010  

NAMLibraryBrowserWindow::NAMLibraryBrowserWindow(NAMLibraryManager* pLibraryMgr,
                                                 std::shared_ptr<NAMLibraryTreeNode> rootNode)
: mpLibraryManager(pLibraryMgr)
, mRootNode(rootNode)
{
  LoadSettings(); // still used for font + window size

  // Restore in-memory UI state (process lifetime; safe with multiple plugin instances vs shared file).
  auto& s = GetBrowserSessionState();
  mPendingSearchQuery = s.lastSearchQuery;
  mSelectedTag = s.lastSelectedTag;
  mExpandedState = s.expandedState;
}

NAMLibraryBrowserWindow::~NAMLibraryBrowserWindow()
{
  Close();

  if (mHFont)
  {
    DeleteObject(mHFont);
    mHFont = nullptr;
  }

  if (mDarkBgBrush)
  {
    DeleteObject(mDarkBgBrush);
    mDarkBgBrush = nullptr;
  }
  if (mEditBgBrush)
  {
    DeleteObject(mEditBgBrush);
    mEditBgBrush = nullptr;
  }
}

void NAMLibraryBrowserWindow::Open(void* pParentWindow)
{
  if (mIsOpen)
    return;

  mParentHwnd = (HWND)pParentWindow;

  // Initialize common controls
  INITCOMMONCONTROLSEX icex;
  icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
  icex.dwICC = ICC_TREEVIEW_CLASSES;
  InitCommonControlsEx(&icex);

  // Register custom window class for separate window
  WNDCLASSEXW wcex = {};
  wcex.cbSize = sizeof(WNDCLASSEX);
  wcex.style = CS_HREDRAW | CS_VREDRAW;
  wcex.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
    NAMLibraryBrowserWindow* pWindow = nullptr;

    if (msg == WM_CREATE)
    {
      CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
      pWindow = reinterpret_cast<NAMLibraryBrowserWindow*>(pCreate->lpCreateParams);
      SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pWindow));
      pWindow->mHwndDlg = hwnd;
    }
    else
    {
      pWindow = reinterpret_cast<NAMLibraryBrowserWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (pWindow)
    {
      INT_PTR result = pWindow->HandleMessage(msg, wParam, lParam);
      if (msg == WM_CLOSE || msg == WM_DESTROY)
        return 0;
      if (result)
        return result;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
  };
  wcex.hInstance = GetModuleHandle(nullptr);
  wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wcex.lpszClassName = L"NAMLibraryBrowserWindow";

  static bool registered = false;
  if (!registered)
  {
    RegisterClassExW(&wcex);
    registered = true;
  }

  // Calculate position next to parent
  RECT rcParent = {};
  if (mParentHwnd)
    GetWindowRect(mParentHwnd, &rcParent);

  int x = rcParent.right + 10;
  int y = rcParent.top;
  int w = mWindowW;
  int h = mWindowH;

  if (mHasSavedBounds)
  {
    POINT centre = { mWindowX + w / 2, mWindowY + h / 2 };
    if (MonitorFromPoint(centre, MONITOR_DEFAULTTONULL) != nullptr)
    {
      x = mWindowX;
      y = mWindowY;
    }
  }

  // For a top-level window (no WS_CHILD), this handle is treated as the *owner*.
  const HWND ownerHwnd = (mParentHwnd && IsWindow(mParentHwnd)) ? mParentHwnd : nullptr;

  mHwndDlg = CreateWindowExW(
    0,
    L"NAMLibraryBrowserWindow",
    L"NAM Library Browser",
    WS_OVERLAPPEDWINDOW | WS_VISIBLE,
    x, y, w, h,
    ownerHwnd, // owned by the main NeuralAmpModeler window
    nullptr,
    GetModuleHandle(nullptr),
    this
  );

  if (mHwndDlg)
  {
    InitializeControls();
    PopulateTagComboBox();
    // PopulateTreeView();

    // Restore saved search text
    if (mHwndSearchEdit && !mPendingSearchQuery.empty())
      SetWindowTextW(mHwndSearchEdit, Utf8ToWide(mPendingSearchQuery).c_str());

    // Restore saved tag selection
    int selIndex = 0;
    if (mHwndTagCombo && !mSelectedTag.empty())
    {
      std::wstring wtag = Utf8ToWide(mSelectedTag);
      int idx = (int)SendMessageW(mHwndTagCombo, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)wtag.c_str());
      if (idx != CB_ERR)
      {
        selIndex = idx;
        ComboBox_SetCurSel(mHwndTagCombo, selIndex);
        wchar_t buffer[256] = {};
        SendMessageW(mHwndTagCombo, CB_GETLBTEXT, selIndex, (LPARAM)buffer);
        mSelectedTag = WideToUtf8(buffer);
      }
      else
      {
        mSelectedTag.clear();
        ComboBox_SetCurSel(mHwndTagCombo, 0);
      }
    }
    else if (mHwndTagCombo)
    {
      ComboBox_SetCurSel(mHwndTagCombo, 0);
    }

    // Rebuild tree according to saved filter/search
    PerformSearch(mPendingSearchQuery);

    ShowWindow(mHwndDlg, SW_SHOW);
    UpdateWindow(mHwndDlg);

    mIsOpen = true;
  }
}

// ---- Cross-platform settings persistence ----

std::string NAMLibraryBrowserWindow::GetSettingsFilePath()
{
  namespace fs = std::filesystem;

  std::string baseDir;

#if defined(OS_WIN) || defined(_WIN32)
  if (const char* appData = getenv("APPDATA"))
    baseDir = appData;
#elif defined(OS_MAC)
  if (const char* home = getenv("HOME"))
    baseDir = std::string(home) + "/Library/Application Support";
#else
#error "Unsupported platform - need OS_WIN or OS_MAC defined"
#endif

  if (baseDir.empty())
    return {};

  fs::path dir = fs::path(baseDir) / "NeuralAmpModeler";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return ec ? std::string{} : (dir / "LibraryBrowser.settings").string();
}

void NAMLibraryBrowserWindow::LoadSettings()
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
        if (v >= mMinFontSize && v <= mMaxFontSize)
          mFontSize = v;
      }
      else if (key == "WindowW")
      {
        const int v = std::stoi(val);
        if (v >= mMinWidth)
          mWindowW = v;
      }
      else if (key == "WindowH")
      {
        const int v = std::stoi(val);
        if (v >= mMinHeight)
          mWindowH = v;
      }
    }
    catch (...) {}
  }
}

void NAMLibraryBrowserWindow::SaveSettings()
{
  const std::string path = GetSettingsFilePath();
  if (path.empty())
    return;

  std::ofstream file(path, std::ios::trunc);
  if (!file.is_open())
    return;

  file << "FontSize=" << mFontSize << "\n";
  file << "WindowW=" << mWindowW << "\n";
  file << "WindowH=" << mWindowH << "\n";
}

void NAMLibraryBrowserWindow::Close()
{
  if (!mIsOpen)
    return;

  // Snapshot current UI state into members so it survives DestroyWindow.
#if defined(OS_WIN) || defined(_WIN32)
  if (mHwndSearchEdit)
  {
    const int len = GetWindowTextLengthW(mHwndSearchEdit);
    if (len <= 0)
    {
      mPendingSearchQuery.clear();
    }
    else
    {
      std::wstring buffer(static_cast<size_t>(len) + 1, L'\0');
      GetWindowTextW(mHwndSearchEdit, buffer.data(), len + 1);
      buffer.resize(static_cast<size_t>(len));
      mPendingSearchQuery = WideToUtf8(buffer.c_str());
    }
  }

  if (mHwndTagCombo)
  {
    const int sel = ComboBox_GetCurSel(mHwndTagCombo);
    if (sel <= 0)
    {
      mSelectedTag.clear(); // "All tags"
    }
    else
    {
      wchar_t buffer[256] = {};
      SendMessageW(mHwndTagCombo, CB_GETLBTEXT, sel, (LPARAM)buffer);
      mSelectedTag = WideToUtf8(buffer);
    }
  }

  // Persist Search/Tag/Expansion in memory only (do NOT write to settings file).
  {
    auto& s = GetBrowserSessionState();
    s.lastSearchQuery = mPendingSearchQuery;
    s.lastSelectedTag = mSelectedTag;
    s.expandedState = mExpandedState;
  }

  // Update bounds from the actual window *in-memory*.
  // Persist only size + font (X/Y remain instance-lifetime only).
  if (mHwndDlg)
  {
    RECT rc{};
    if (GetWindowRect(mHwndDlg, &rc))
    {
      mWindowX = rc.left;
      mWindowY = rc.top;
      mWindowW = rc.right - rc.left;
      mWindowH = rc.bottom - rc.top;
      mHasSavedBounds = true; // instance-lifetime (not written)
    }
  }
#endif

  SaveSettings(); // keep this for FontSize/WindowW/WindowH only

  if (mHwndDlg)
  {
    if (mSearchTimerId != 0)
    {
      KillTimer(mHwndDlg, SEARCH_TIMER_ID);
      mSearchTimerId = 0;
    }

    DestroyWindow(mHwndDlg);
    mHwndDlg = nullptr;
  }

  mTreeItemMap.clear();
  mIsOpen = false;
}

void NAMLibraryBrowserWindow::InitializeControls()
{
  RECT clientRect{};
  GetClientRect(mHwndDlg, &clientRect);
  const int width = clientRect.right;
  const int height = clientRect.bottom;

  // Avoid leaking brushes when the window is reopened.
  if (!mDarkBgBrush)
    mDarkBgBrush = CreateSolidBrush(RGB(30, 30, 30));
  if (!mEditBgBrush)
    mEditBgBrush = CreateSolidBrush(RGB(40, 40, 40));

  SetClassLongPtr(mHwndDlg, GCLP_HBRBACKGROUND, (LONG_PTR)mDarkBgBrush);
  RecreateFont();

  const int searchEditW = std::max(50, width - 520);

  mHwndSearchLabel = CreateWindowW(
    L"STATIC", L"Search:",
    WS_CHILD | WS_VISIBLE | SS_LEFT,
    10, 10, 70, 35,
    mHwndDlg, (HMENU)IDC_SEARCH_LABEL,
    GetModuleHandle(nullptr), nullptr
  );
  SendMessage(mHwndSearchLabel, WM_SETFONT, (WPARAM)mHFont, TRUE);

  mHwndSearchEdit = CreateWindowExW(
    WS_EX_CLIENTEDGE, L"EDIT", L"",
    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
    80, 10, searchEditW, 35,
    mHwndDlg, (HMENU)IDC_SEARCH_EDIT,
    GetModuleHandle(nullptr), nullptr
  );
  SendMessage(mHwndSearchEdit, WM_SETFONT, (WPARAM)mHFont, TRUE);

  mHwndTagLabel = CreateWindowW(
    L"STATIC", L"Tag:",
    WS_CHILD | WS_VISIBLE | SS_LEFT,
    width - 430, 10, 40, 35,
    mHwndDlg, (HMENU)IDC_TAG_LABEL,
    GetModuleHandle(nullptr), nullptr
  );
  SendMessage(mHwndTagLabel, WM_SETFONT, (WPARAM)mHFont, TRUE);

  mHwndTagCombo = CreateWindowExW(
    WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
    width - 385, 10, 220, 300,
    mHwndDlg, (HMENU)IDC_TAG_COMBO,
    GetModuleHandle(nullptr), nullptr
  );
  SendMessage(mHwndTagCombo, WM_SETFONT, (WPARAM)mHFont, TRUE);

  HWND hwndReset = CreateWindowW(
    L"BUTTON", L"X",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
    width - 160, 10, 35, 35,
    mHwndDlg, (HMENU)IDC_TAG_RESET,
    GetModuleHandle(nullptr), nullptr
  );
  SendMessage(hwndReset, WM_SETFONT, (WPARAM)mHFont, TRUE);

  mHwndFontDecButton = CreateWindowW(
    L"BUTTON", L"A-",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
    width - 110, 10, 45, 35,
    mHwndDlg, (HMENU)IDC_FONT_DEC,
    GetModuleHandle(nullptr), nullptr
  );
  SendMessage(mHwndFontDecButton, WM_SETFONT, (WPARAM)mHFont, TRUE);

  mHwndFontIncButton = CreateWindowW(
    L"BUTTON", L"A+",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
    width - 60, 10, 45, 35,
    mHwndDlg, (HMENU)IDC_FONT_INC,
    GetModuleHandle(nullptr), nullptr
  );
  SendMessage(mHwndFontIncButton, WM_SETFONT, (WPARAM)mHFont, TRUE);

  mHwndTreeView = CreateWindowExW(
    WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
    WS_CHILD | WS_VISIBLE | TVS_HASLINES |  // FIX: was TVS_HAS_LINES
    TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS | TVS_FULLROWSELECT,
    10, 55, width - 20, height - 110,
    mHwndDlg, (HMENU)IDC_TREEVIEW,
    GetModuleHandle(nullptr), nullptr
  );
  SendMessage(mHwndTreeView, WM_SETFONT, (WPARAM)mHFont, TRUE);
  UpdateFontSize();
  SetWindowTheme(mHwndTreeView, L"Explorer", nullptr);

  TreeView_SetBkColor(mHwndTreeView, RGB(30, 30, 30));
  TreeView_SetTextColor(mHwndTreeView, RGB(220, 220, 220));
  TreeView_SetLineColor(mHwndTreeView, RGB(80, 80, 80));

  mHwndLoadButton = CreateWindowW(
    L"BUTTON", L"Load Selected Model",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
    width - 400, height - 50, 240, 40,
    mHwndDlg, (HMENU)IDC_LOAD_BUTTON,
    GetModuleHandle(nullptr), nullptr
  );
  EnableWindow(mHwndLoadButton, FALSE);
  SendMessage(mHwndLoadButton, WM_SETFONT, (WPARAM)mHFont, TRUE);

  mHwndCancelButton = CreateWindowW(
    L"BUTTON", L"Cancel",
    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
    width - 150, height - 50, 140, 40,
    mHwndDlg, (HMENU)IDC_CANCEL_BUTTON,
    GetModuleHandle(nullptr), nullptr
  );
  SendMessage(mHwndCancelButton, WM_SETFONT, (WPARAM)mHFont, TRUE);
}

void NAMLibraryBrowserWindow::RecreateFont()
{
  if (mHFont) DeleteObject(mHFont);
  mHFont = CreateFontW(mFontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void NAMLibraryBrowserWindow::UpdateFontSize()
{
  if (!mHwndTreeView)
    return;

  // Calculate item height based on font size (add ~30% padding), but clamp for readability at small sizes.
  const int minItemHeight = mFontSize + 4;
  const int scaledHeight = static_cast<int>(mFontSize * 1.3f);
  const int itemHeight = std::max(minItemHeight, scaledHeight);

  TreeView_SetItemHeight(mHwndTreeView, itemHeight);
}

// Helper to push font to all children
void  NAMLibraryBrowserWindow::UpdateChildFonts()
{
  if (!mHwndDlg) return;
  // Common controls
  if (mHwndSearchLabel) SendMessage(mHwndSearchLabel, WM_SETFONT, (WPARAM)mHFont, TRUE);
  if (mHwndSearchEdit) SendMessage(mHwndSearchEdit, WM_SETFONT, (WPARAM)mHFont, TRUE);
  if (mHwndTagLabel) SendMessage(mHwndTagLabel, WM_SETFONT, (WPARAM)mHFont, TRUE);
  if (mHwndTagCombo) SendMessage(mHwndTagCombo, WM_SETFONT, (WPARAM)mHFont, TRUE);
  if (mHwndFontDecButton) SendMessage(mHwndFontDecButton, WM_SETFONT, (WPARAM)mHFont, TRUE);
  if (mHwndFontIncButton) SendMessage(mHwndFontIncButton, WM_SETFONT, (WPARAM)mHFont, TRUE);
  if (mHwndTreeView) SendMessage(mHwndTreeView, WM_SETFONT, (WPARAM)mHFont, TRUE);
  if (mHwndLoadButton) SendMessage(mHwndLoadButton, WM_SETFONT, (WPARAM)mHFont, TRUE);
  if (mHwndCancelButton) SendMessage(mHwndCancelButton, WM_SETFONT, (WPARAM)mHFont, TRUE);
  // Get Reset button dynamically
  HWND hReset = GetDlgItem(mHwndDlg, IDC_TAG_RESET);
  if (hReset) SendMessage(hReset, WM_SETFONT, (WPARAM)mHFont, TRUE);
}

void NAMLibraryBrowserWindow::IncreaseFontSize()
{
  if (mFontSize < mMaxFontSize) {
    mFontSize += 2;
    RecreateFont();
    UpdateChildFonts(); // FIX: Propagate Font
    UpdateFontSize();
    InvalidateRect(mHwndDlg, nullptr, TRUE);
    SaveSettings();
  }
}

void NAMLibraryBrowserWindow::DecreaseFontSize()
{
  if (mFontSize > mMinFontSize) {
    mFontSize -= 2;
    RecreateFont();
    UpdateChildFonts(); // FIX: Propagate Font
    UpdateFontSize();
    InvalidateRect(mHwndDlg, nullptr, TRUE);
    SaveSettings();
  }
}

void NAMLibraryBrowserWindow::AddTreeNode(HTREEITEM hParent, const std::shared_ptr<NAMLibraryTreeNode>& node, bool ancestorsExpanded)
{
  if (!node || !mHwndTreeView)
    return;

  std::string displayName = node->name;

  if (node->IsModel())
  {
    std::vector<std::string> metaParts;
    metaParts.reserve(3);

    if (!node->gear_make.empty() || !node->gear_model.empty())
    {
      std::string gearInfo;
      if (!node->gear_make.empty() && !node->gear_model.empty())
        gearInfo = node->gear_make + " " + node->gear_model;
      else if (!node->gear_make.empty())
        gearInfo = node->gear_make;
      else
        gearInfo = node->gear_model;

      metaParts.push_back(std::move(gearInfo));
    }

    auto addLevel = [&](const char* label, double value)
    {
      if (value == 0.0)
        return;

      char buffer[32] = {};
      snprintf(buffer, sizeof(buffer), "%s: %.1f", label, value);
      metaParts.emplace_back(buffer);
    };

    addLevel("in", node->input_level_dbu);
    addLevel("out", node->output_level_dbu);

    if (!metaParts.empty())
    {
      std::ostringstream ss;
      ss << displayName << " [";
      for (size_t i = 0; i < metaParts.size(); ++i)
      {
        if (i > 0)
          ss << ", ";
        ss << metaParts[i];
      }
      ss << "]";
      displayName = ss.str();
    }
  }

  const std::wstring wname = Utf8ToWide(displayName);
  if (wname.empty())
    return;

  TVINSERTSTRUCTW tvis = {};
  tvis.hParent = hParent;
  tvis.hInsertAfter = TVI_LAST;
  tvis.item.mask = TVIF_TEXT | TVIF_PARAM;
  tvis.item.pszText = const_cast<LPWSTR>(wname.c_str());
  tvis.item.lParam = reinterpret_cast<LPARAM>(node.get());

  HTREEITEM hItem = reinterpret_cast<HTREEITEM>(
    SendMessageW(mHwndTreeView, TVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&tvis))
  );
  if (!hItem)
    return;

  mTreeItemMap[hItem] = node;

  const bool isFolder = !node->children.empty();
  const bool wantExpanded = isFolder ? GetFolderExpandedFromState(node) : false;

  // Effective expansion: ancestors must be expanded; leaf nodes don't care.
  const bool thisExpanded = ancestorsExpanded && (!isFolder || wantExpanded);

  // Insert children first (so the TreeView item is actually expandable), using top-down semantics via thisExpanded.
  for (const auto& child : node->children)
    AddTreeNode(hItem, child, thisExpanded);

  // Expand AFTER children exist, otherwise TreeView_Expand often has no effect.
  if (ancestorsExpanded && isFolder && wantExpanded)
    TreeView_Expand(mHwndTreeView, hItem, TVE_EXPAND);
}

void NAMLibraryBrowserWindow::PopulateTreeView()
{
  if (!mHwndTreeView)
    return;

  TreeView_DeleteAllItems(mHwndTreeView);
  mTreeItemMap.clear();

  const auto rootToDisplay = mSearchRoot ? mSearchRoot : mRootNode;
  if (!rootToDisplay)
    return;

  for (const auto& child : rootToDisplay->children)
    AddTreeNode(TVI_ROOT, child, true);

  const HTREEITEM hFirst = TreeView_GetRoot(mHwndTreeView);
  if (hFirst)
  {
    TreeView_SelectItem(mHwndTreeView, hFirst);
    TreeView_EnsureVisible(mHwndTreeView, hFirst);
  }
}

void NAMLibraryBrowserWindow::OnTreeViewSelectionChanged()
{
  if (!mHwndTreeView || !mHwndLoadButton)
    return;

  const HTREEITEM hSelected = TreeView_GetSelection(mHwndTreeView);
  if (hSelected)
  {
    auto it = mTreeItemMap.find(hSelected);
    mSelectedNode = (it != mTreeItemMap.end()) ? it->second : nullptr;
  }
  else
  {
    mSelectedNode = nullptr;
  }

  EnableWindow(mHwndLoadButton, (mSelectedNode && mSelectedNode->IsModel()) ? TRUE : FALSE);
}

void NAMLibraryBrowserWindow::OnTreeViewDoubleClick()
{
  if (mSelectedNode && mSelectedNode->IsModel())
    OnLoadButtonClicked();
}

void NAMLibraryBrowserWindow::OnLoadButtonClicked()
{
  if (!mSelectedNode || !mSelectedNode->IsModel() || !mOnModelSelected)
    return;

  mOnModelSelected(mSelectedNode);
  Close();
}

void NAMLibraryBrowserWindow::OnSearchTextChanged()
{
  if (!mHwndDlg || !mHwndSearchEdit)
    return;

  if (mSearchTimerId != 0)
  {
    KillTimer(mHwndDlg, SEARCH_TIMER_ID);
    mSearchTimerId = 0;
  }

  const int len = GetWindowTextLengthW(mHwndSearchEdit);
  if (len <= 0)
  {
    mPendingSearchQuery.clear();
  }
  else
  {
    std::wstring buffer(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(mHwndSearchEdit, buffer.data(), len + 1);
    buffer.resize(static_cast<size_t>(len)); // drop null terminator
    mPendingSearchQuery = WideToUtf8(buffer.c_str());
  }

  mSearchTimerId = SetTimer(mHwndDlg, SEARCH_TIMER_ID, SEARCH_DELAY_MS, nullptr);
}

// Replace PerformSearch()
void NAMLibraryBrowserWindow::PerformSearch(const std::string& query)
{
  if (!mpLibraryManager)
    return;

  const bool hasQuery = !query.empty();
  const std::string selectedTagTrimmed = Trim(mSelectedTag);
  const bool hasTag = !selectedTagTrimmed.empty();

  if (!hasQuery && !hasTag)
  {
    mSearchRoot = nullptr;
    PopulateTagComboBox();
    PopulateTreeView();
    return;
  }

  std::vector<std::shared_ptr<NAMLibraryTreeNode>> searchResults =
    hasQuery ? mpLibraryManager->SearchModels(query) : mpLibraryManager->GetAllModels();

  if (hasTag)
  {
    const std::string selectedTagLower = ToLowerAscii(selectedTagTrimmed);

    searchResults.erase(
      std::remove_if(searchResults.begin(), searchResults.end(),
        [&](const std::shared_ptr<NAMLibraryTreeNode>& model) {
          if (!model)
            return true;

          for (const auto& tag : model->tags)
          {
            if (ToLowerAscii(Trim(tag)) == selectedTagLower)
              return false;
          }

          return true;
        }),
      searchResults.end());
  }

  mSearchRoot = std::make_shared<NAMLibraryTreeNode>();
  mSearchRoot->name = "Filtered Results (" + std::to_string(searchResults.size()) + " models)";
  mSearchRoot->id = "search_root";
  mSearchRoot->depth = 0;
  mSearchRoot->expanded = true;

  std::unordered_map<std::string, std::shared_ptr<NAMLibraryTreeNode>> nodeMap;
  std::unordered_map<std::string, std::unordered_set<std::string>> childrenAdded;

  auto addUniqueChild = [&](const std::shared_ptr<NAMLibraryTreeNode>& parentCopy,
                            const std::shared_ptr<NAMLibraryTreeNode>& childCopy) {
    if (!parentCopy || !childCopy)
      return;

    auto& set = childrenAdded[parentCopy->id];
    if (set.insert(childCopy->id).second)
      parentCopy->children.push_back(childCopy);
  };

  std::function<std::shared_ptr<NAMLibraryTreeNode>(const std::shared_ptr<NAMLibraryTreeNode>&)> BuildAncestorChain;
  BuildAncestorChain = [&](const std::shared_ptr<NAMLibraryTreeNode>& node) -> std::shared_ptr<NAMLibraryTreeNode> {
    if (!node)
      return nullptr;

    if (auto it = nodeMap.find(node->id); it != nodeMap.end())
      return it->second;

    auto nodeCopy = std::make_shared<NAMLibraryTreeNode>(*node);
    nodeCopy->children.clear();
    nodeCopy->expanded = true;
    nodeMap.emplace(node->id, nodeCopy);

    if (node->parent)
    {
      auto parentCopy = BuildAncestorChain(node->parent);
      nodeCopy->parent = parentCopy;
      if (parentCopy)
      {
        nodeCopy->depth = parentCopy->depth + 1;
        addUniqueChild(parentCopy, nodeCopy);
      }
      else
      {
        nodeCopy->parent = mSearchRoot;
        nodeCopy->depth = 1;
        addUniqueChild(mSearchRoot, nodeCopy);
      }
    }
    else
    {
      nodeCopy->parent = mSearchRoot;
      nodeCopy->depth = 1;
      addUniqueChild(mSearchRoot, nodeCopy);
    }

    return nodeCopy;
  };

  for (const auto& model : searchResults)
    BuildAncestorChain(model);

  // Repopulate tags to show only those in filtered results
  PopulateTagComboBox(&searchResults);

  // When filtering/searching: everything shown should be expanded.
  // Persist this in-memory only (do not modify data.json).
  SetExpandedStateRecursive(mSearchRoot, true);

  PopulateTreeView();
}

void NAMLibraryBrowserWindow::ResizeControls(int width, int height)
{
  if (mHwndSearchEdit)
    SetWindowPos(mHwndSearchEdit, nullptr, 80, 10, width - 520, 35, SWP_NOZORDER);

  if (mHwndTagLabel)
    SetWindowPos(mHwndTagLabel, nullptr, width - 430, 10, 40, 35, SWP_NOZORDER);

  if (mHwndTagCombo)
    SetWindowPos(mHwndTagCombo, nullptr, width - 385, 10, 220, 300, SWP_NOZORDER);

  // Use GetDlgItem for the new button since we didn't add a member variable
  HWND hwndReset = GetDlgItem(mHwndDlg, IDC_TAG_RESET);
  if (hwndReset)
    SetWindowPos(hwndReset, nullptr, width - 160, 10, 35, 35, SWP_NOZORDER);

  if (mHwndFontDecButton)
    SetWindowPos(mHwndFontDecButton, nullptr, width - 110, 10, 45, 35, SWP_NOZORDER);

  if (mHwndFontIncButton)
    SetWindowPos(mHwndFontIncButton, nullptr, width - 60, 10, 45, 35, SWP_NOZORDER);

  if (mHwndTreeView)
    SetWindowPos(mHwndTreeView, nullptr, 10, 55, width - 20, height - 110, SWP_NOZORDER);

  if (mHwndLoadButton)
    SetWindowPos(mHwndLoadButton, nullptr, width - 400, height - 50, 240, 40, SWP_NOZORDER);

  if (mHwndCancelButton)
    SetWindowPos(mHwndCancelButton, nullptr, width - 150, height - 50, 140, 40, SWP_NOZORDER);
}

INT_PTR CALLBACK NAMLibraryBrowserWindow::DialogProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
  NAMLibraryBrowserWindow* pWindow = nullptr;

  if (msg == WM_INITDIALOG)
  {
    pWindow = reinterpret_cast<NAMLibraryBrowserWindow*>(lParam);
    SetWindowLongPtr(hwndDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pWindow));
    return TRUE;
  }
  else
  {
    pWindow = reinterpret_cast<NAMLibraryBrowserWindow*>(GetWindowLongPtr(hwndDlg, GWLP_USERDATA));
  }

  if (pWindow)
    return pWindow->HandleMessage(msg, wParam, lParam);

  return FALSE;
}

INT_PTR NAMLibraryBrowserWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg)
  {
    case WM_CTLCOLORSTATIC:
    {
      HDC hdcStatic = (HDC)wParam;
      SetTextColor(hdcStatic, RGB(220, 220, 220));
      SetBkColor(hdcStatic, RGB(30, 30, 30));
      return (INT_PTR)mDarkBgBrush;  // Reuse brush
    }

    // NEW: Handle dropdown list style
    case WM_CTLCOLORLISTBOX:
    {
      HDC hdcList = (HDC)wParam;
      SetTextColor(hdcList, RGB(220, 220, 220));
      SetBkColor(hdcList, RGB(30, 30, 30));
      return (INT_PTR)mDarkBgBrush;
    }

    case WM_CTLCOLOREDIT:
    {
      HDC hdcEdit = (HDC)wParam;
      SetTextColor(hdcEdit, RGB(220, 220, 220));
      SetBkColor(hdcEdit, RGB(40, 40, 40));
      return (INT_PTR)mEditBgBrush;  // Reuse brush
    }

    // Custom button drawing for dark theme
    case WM_DRAWITEM:
    {
      LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lParam;
      if (pDIS->CtlType == ODT_BUTTON)
      {
        // Get button text
        wchar_t buttonText[256] = {};
        GetWindowTextW(pDIS->hwndItem, buttonText, 256);

        // Determine colors based on state
        COLORREF bgColor = RGB(60, 60, 60);  // Default
        COLORREF textColor = RGB(220, 220, 220);

        if (pDIS->itemState & ODS_DISABLED)
        {
          bgColor = RGB(40, 40, 40);
          textColor = RGB(100, 100, 100);
        }
        else if (pDIS->itemState & ODS_SELECTED)
        {
          bgColor = RGB(0, 120, 215);  // Blue when pressed
          textColor = RGB(255, 255, 255);
        }
        else if (pDIS->itemState & ODS_FOCUS)
        {
          bgColor = RGB(80, 80, 80);  // Lighter gray on focus
        }

        // Fill background
        HBRUSH hBrush = CreateSolidBrush(bgColor);
        FillRect(pDIS->hDC, &pDIS->rcItem, hBrush);
        DeleteObject(hBrush);

        // Draw border for depth
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(100, 100, 100));
        HPEN hOldPen = (HPEN)SelectObject(pDIS->hDC, hPen);
        MoveToEx(pDIS->hDC, pDIS->rcItem.left, pDIS->rcItem.bottom - 1, nullptr);
        LineTo(pDIS->hDC, pDIS->rcItem.left, pDIS->rcItem.top);
        LineTo(pDIS->hDC, pDIS->rcItem.right - 1, pDIS->rcItem.top);
        LineTo(pDIS->hDC, pDIS->rcItem.right - 1, pDIS->rcItem.bottom - 1);
        LineTo(pDIS->hDC, pDIS->rcItem.left, pDIS->rcItem.bottom - 1);
        SelectObject(pDIS->hDC, hOldPen);
        DeleteObject(hPen);

        // Draw text with our custom font
        HFONT hOldFont = (HFONT)SelectObject(pDIS->hDC, mHFont);
        SetTextColor(pDIS->hDC, textColor);
        SetBkMode(pDIS->hDC, TRANSPARENT);
        DrawTextW(pDIS->hDC, buttonText, -1, &pDIS->rcItem,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(pDIS->hDC, hOldFont);

        // Draw focus rectangle
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

    case WM_TIMER:
    {
      if (wParam == SEARCH_TIMER_ID)
      {
        KillTimer(mHwndDlg, SEARCH_TIMER_ID);
        mSearchTimerId = 0;
        PerformSearch(mPendingSearchQuery);
        return TRUE;
      }
      break;
    }

    case WM_COMMAND:
    {
      int wmId = LOWORD(wParam);
      int wmEvent = HIWORD(wParam);

      if (wmId == IDC_LOAD_BUTTON)
      {
        OnLoadButtonClicked();
        return TRUE;
      }
      else if (wmId == IDC_CANCEL_BUTTON)
      {
        Close();
        return TRUE;
      }
      else if (wmId == IDC_FONT_INC)
      {
        IncreaseFontSize();
        return TRUE;
      }
      else if (wmId == IDC_FONT_DEC)
      {
        DecreaseFontSize();
        return TRUE;
      }
      // NEW: Handle reset button click
      else if (wmId == IDC_TAG_RESET)
      {
        if (mHwndTagCombo)
        {
          ComboBox_SetCurSel(mHwndTagCombo, 0); // Select "All tags"
          mSelectedTag.clear();

          wchar_t searchText[256] = {};
          GetWindowTextW(mHwndSearchEdit, searchText, 256); // FIX: mHwndSearchEdit
          mPendingSearchQuery = WideToUtf8(searchText);

          PerformSearch(mPendingSearchQuery);
        }
        return TRUE;
      }
      else if (wmId == IDC_SEARCH_EDIT && wmEvent == EN_CHANGE)
      {
        OnSearchTextChanged();
        return TRUE;
      }
      else if (wmId == IDC_TAG_COMBO && wmEvent == CBN_SELCHANGE)
      {
        if (!mIsPopulatingTags)
          OnTagSelectionChanged();
        return TRUE;
      }
      break;
    }

    case WM_NOTIFY:
    {
      LPNMHDR pnmh = (LPNMHDR)lParam;

      if (pnmh->idFrom == IDC_TREEVIEW)
      {
        if (pnmh->code == NM_CUSTOMDRAW)
        {
          LPNMTVCUSTOMDRAW pCustomDraw = (LPNMTVCUSTOMDRAW)lParam;

          switch (pCustomDraw->nmcd.dwDrawStage)
          {
            case CDDS_PREPAINT:
              return CDRF_NOTIFYITEMDRAW;

            case CDDS_ITEMPREPAINT:
            {
              if (pCustomDraw->nmcd.uItemState & CDIS_SELECTED)
              {
                pCustomDraw->clrText = RGB(255, 255, 255);
                pCustomDraw->clrTextBk = RGB(0, 120, 215);
              }
              else
              {
                pCustomDraw->clrText = RGB(220, 220, 220);
                pCustomDraw->clrTextBk = RGB(30, 30, 30);
              }
              return CDRF_NEWFONT;
            }
          }
        }
        else if (pnmh->code == TVN_SELCHANGEDW)
        {
          OnTreeViewSelectionChanged();
          return TRUE;
        }
        else if (pnmh->code == NM_DBLCLK)
        {
          TVHITTESTINFO ht = {};
          DWORD dwpos = GetMessagePos();
          ht.pt.x = GET_X_LPARAM(dwpos);
          ht.pt.y = GET_Y_LPARAM(dwpos);
          ScreenToClient(mHwndTreeView, &ht.pt);

          HTREEITEM hItem = TreeView_HitTest(mHwndTreeView, &ht);
          if (hItem && (ht.flags & TVHT_ONITEM))
          {
            TreeView_SelectItem(mHwndTreeView, hItem);
            OnTreeViewDoubleClick();
          }
          return TRUE;
        }
        else if (pnmh->code == TVN_ITEMEXPANDEDW)
        {
          auto* pnmtv = (LPNMTREEVIEWW)lParam;

          auto it = mTreeItemMap.find(pnmtv->itemNew.hItem);
          if (it != mTreeItemMap.end() && it->second && !it->second->children.empty())
          {
            const bool expanded = (pnmtv->action == TVE_EXPAND);
            if (pnmtv->action == TVE_EXPAND || pnmtv->action == TVE_COLLAPSE)
              SetFolderExpandedInState(it->second, expanded);
          }

          // If user expands a folder, auto-expand descendants that are marked expanded (in-memory first, json fallback).
          if (pnmtv->action == TVE_EXPAND && !mIsAutoExpanding)
          {
            mIsAutoExpanding = true;
            AutoExpandDescendantsFromFlags(pnmtv->itemNew.hItem);
            mIsAutoExpanding = false;
          }

          return TRUE;
        }
      }

      break;
    }

    case WM_SIZE:
    {
      int width = LOWORD(lParam);
      int height = HIWORD(lParam);
      ResizeControls(width, height);
      return TRUE;
    }

    case WM_GETMINMAXINFO:
    {
      LPMINMAXINFO lpMMI = (LPMINMAXINFO)lParam;
      lpMMI->ptMinTrackSize.x = mMinWidth;
      lpMMI->ptMinTrackSize.y = mMinHeight;
      return 0;
    }

    case WM_CLOSE:
      Close();
      return TRUE;
  }

  return FALSE;
}

void NAMLibraryBrowserWindow::PopulateTagComboBox(const std::vector<std::shared_ptr<NAMLibraryTreeNode>>* pModelsForTags)
{
  if (!mHwndTagCombo || !mpLibraryManager)
    return;

  struct FlagGuard
  {
    bool& Flag;
    explicit FlagGuard(bool& f) : Flag(f) { Flag = true; }
    ~FlagGuard() { Flag = false; }
  } guard(mIsPopulatingTags);

  ComboBox_ResetContent(mHwndTagCombo);
  SendMessageW(mHwndTagCombo, CB_ADDSTRING, 0, (LPARAM)L"All tags");

  std::set<std::string> uniqueTags;

  const auto* modelsToUse = pModelsForTags ? pModelsForTags : &mpLibraryManager->GetAllModels();
  for (const auto& model : *modelsToUse)
  {
    if (!model)
      continue;

    for (const auto& tag : model->tags)
    {
      std::string trimmed = Trim(tag);
      if (!trimmed.empty())
        uniqueTags.insert(trimmed);
    }
  }

  for (const auto& tag : uniqueTags)
  {
    const std::wstring wtag = Utf8ToWide(tag);
    SendMessageW(mHwndTagCombo, CB_ADDSTRING, 0, (LPARAM)wtag.c_str());
  }

  if (!mSelectedTag.empty())
  {
    const std::wstring wtag = Utf8ToWide(mSelectedTag);
    int idx = (int)SendMessageW(mHwndTagCombo, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)wtag.c_str());
    if (idx != CB_ERR)
      ComboBox_SetCurSel(mHwndTagCombo, idx);
    else
    {
      mSelectedTag.clear();
      ComboBox_SetCurSel(mHwndTagCombo, 0);
    }
  }
  else
  {
    ComboBox_SetCurSel(mHwndTagCombo, 0);
  }
}

void NAMLibraryBrowserWindow::OnTagSelectionChanged()
{
  if (!mHwndTagCombo)
    return;

  int sel = ComboBox_GetCurSel(mHwndTagCombo);
  if (sel <= 0)
  {
    mSelectedTag.clear();
  }
  else
  {
    wchar_t buffer[256] = {};
    SendMessageW(mHwndTagCombo, CB_GETLBTEXT, sel, (LPARAM)buffer);
    mSelectedTag = WideToUtf8(buffer);
  }

  wchar_t searchText[256] = {};
  GetWindowTextW(mHwndSearchEdit, searchText, 256);
  mPendingSearchQuery = WideToUtf8(searchText);

  if (mSearchTimerId != 0)
  {
    KillTimer(mHwndDlg, SEARCH_TIMER_ID);
    mSearchTimerId = 0;
  }

  PerformSearch(mPendingSearchQuery);
}

void NAMLibraryBrowserWindow::AutoExpandDescendantsFromFlags(HTREEITEM hParentItem)
{
  if (!mHwndTreeView || !hParentItem)
    return;

  for (HTREEITEM hChild = TreeView_GetChild(mHwndTreeView, hParentItem);
       hChild != nullptr;
       hChild = TreeView_GetNextSibling(mHwndTreeView, hChild))
  {
    auto it = mTreeItemMap.find(hChild);
    if (it == mTreeItemMap.end())
      continue;

    const auto& node = it->second;
    if (!node)
      continue;

    // Only folders participate.
    if (node->children.empty())
      continue;

    if (GetFolderExpandedFromState(node))
    {
      // Persist (so a json-true-but-no-state entry becomes state-true once it actually expands in the UI).
      SetFolderExpandedInState(node, true);

      TreeView_Expand(mHwndTreeView, hChild, TVE_EXPAND);
      AutoExpandDescendantsFromFlags(hChild);
    }
  }
}
#elif defined(OS_MAC)

#import <Cocoa/Cocoa.h>

// ---- ObjC wrapper around a C++ tree node -------------------
@interface NAMNodeWrapper : NSObject
@property (nonatomic) std::shared_ptr<NAMLibraryTreeNode> node;
+ (instancetype)wrap:(std::shared_ptr<NAMLibraryTreeNode>)n;
@end
@implementation NAMNodeWrapper
+ (instancetype)wrap:(std::shared_ptr<NAMLibraryTreeNode>)n
{
  NAMNodeWrapper* w = [NAMNodeWrapper new];
  w.node = n;
  return w;
}
@end

// ---- NSOutlineView data source ------------------------------
//@implementation NAMOutlineDataSource : NSObject <NSOutlineViewDataSource>
@interface NAMOutlineDataSource : NSObject <NSOutlineViewDataSource, NSOutlineViewDelegate>
@property (nonatomic) std::shared_ptr<NAMLibraryTreeNode> displayRoot;
@end

@implementation NAMOutlineDataSource

- (NSInteger)outlineView:(NSOutlineView*)ov numberOfChildrenOfItem:(id)item
{
  if (!self.displayRoot) return 0;
  auto& parent = item ? ((NAMNodeWrapper*)item).node : self.displayRoot;
  return (NSInteger)parent->children.size();
}

- (id)outlineView:(NSOutlineView*)ov child:(NSInteger)idx ofItem:(id)item
{
  auto& parent = item ? ((NAMNodeWrapper*)item).node : self.displayRoot;
  if (idx < (NSInteger)parent->children.size())
    return [NAMNodeWrapper wrap:parent->children[(size_t)idx]];
  return nil;
}

- (BOOL)outlineView:(NSOutlineView*)ov isItemExpandable:(id)item
{
  return !((NAMNodeWrapper*)item).node->children.empty();
}

- (id)outlineView:(NSOutlineView*)ov objectValueForTableColumn:(NSTableColumn*)col byItem:(id)item
{
  auto& n = ((NAMNodeWrapper*)item).node;
  std::string label = n->name;
  if (n->IsModel())
  {
    std::string gear;
    if (!n->gear_make.empty() && !n->gear_model.empty())
      gear = n->gear_make + " " + n->gear_model;
    else
      gear = n->gear_make + n->gear_model;
    if (!gear.empty())
      label += " [" + gear + "]";
  }
  return [NSString stringWithUTF8String:label.c_str()];
}

@end

using VoidFn   = std::function<void()>;

//@interface NAMLibraryWindowController : NSWindowController <NSWindowDelegate>
@interface NAMLibraryWindowController : NSWindowController <NSWindowDelegate, NSOutlineViewDelegate>
@property (nonatomic, strong) NAMOutlineDataSource* dataSource;
@property (nonatomic, strong) NSOutlineView*        outlineView;
@property (nonatomic, strong) NSButton*             loadButton;
@property (nonatomic, strong) NSTextField*          searchField;
@property (nonatomic, strong) NSPopUpButton*        tagPopup;
@property (nonatomic) VoidFn   onLoad;
@property (nonatomic) VoidFn   onCancel;
@property (nonatomic) FilterFn onFilterChanged;
@property (nonatomic) VoidFn   onFontInc;
@property (nonatomic) VoidFn   onFontDec;
@property (nonatomic) VoidFn   onWindowClose;
- (instancetype)initWithFontSize:(int)fontSize;
- (void)setDisplayRoot:(std::shared_ptr<NAMLibraryTreeNode>)root;
- (std::shared_ptr<NAMLibraryTreeNode>)selectedNode;
- (void)setAvailableTags:(const std::vector<std::string>&)tags;
@end

@implementation NAMLibraryWindowController

- (instancetype)initWithFontSize:(int)fontSize
{
  NSPanel* panel = [[NSPanel alloc]
    initWithContentRect:NSMakeRect(0, 0, 800, 600)
              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                        NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                backing:NSBackingStoreBuffered
                  defer:YES];
  panel.title    = @"NAM Library Browser";
  panel.minSize  = NSMakeSize(600, 400);
  self = [super initWithWindow:panel];
  if (!self) return nil;
  panel.delegate = self;

  NSView* cv = panel.contentView;

  self.searchField = [NSTextField new];
  self.searchField.placeholderString = @"Search models...";
  self.searchField.translatesAutoresizingMaskIntoConstraints = NO;
  [[NSNotificationCenter defaultCenter] addObserver:self
    selector:@selector(searchChanged:)
        name:NSControlTextDidChangeNotification
      object:self.searchField];
  [cv addSubview:self.searchField];

  self.tagPopup = [NSPopUpButton new];
  self.tagPopup.translatesAutoresizingMaskIntoConstraints = NO;
  self.tagPopup.target = self;
  self.tagPopup.action = @selector(tagChanged:);
  [cv addSubview:self.tagPopup];

  NSButton* fInc = [NSButton buttonWithTitle:@"A+" target:self action:@selector(fontInc:)];
  NSButton* fDec = [NSButton buttonWithTitle:@"A-" target:self action:@selector(fontDec:)];
  fInc.translatesAutoresizingMaskIntoConstraints = NO;
  fDec.translatesAutoresizingMaskIntoConstraints = NO;
  [cv addSubview:fInc];
  [cv addSubview:fDec];

  NSScrollView* scroll = [NSScrollView new];
  scroll.hasVerticalScroller  = YES;
  scroll.autohidesScrollers   = YES;
  scroll.translatesAutoresizingMaskIntoConstraints = NO;

  self.outlineView = [NSOutlineView new];
  self.outlineView.rowHeight               = fontSize * 1.4f;
  self.outlineView.allowsMultipleSelection = NO;
  self.outlineView.headerView              = nil;
  self.outlineView.target                  = self;
  self.outlineView.doubleAction            = @selector(doubleClicked:);
  NSTableColumn* col = [[NSTableColumn alloc] initWithIdentifier:@"name"];
  [self.outlineView addTableColumn:col];
  self.outlineView.outlineTableColumn = col;
  self.dataSource = [NAMOutlineDataSource new];
  self.outlineView.dataSource = self.dataSource;
  [[NSNotificationCenter defaultCenter] addObserver:self
    selector:@selector(selectionChanged:)
        name:NSOutlineViewSelectionDidChangeNotification
      object:self.outlineView];
  scroll.documentView = self.outlineView;
  [cv addSubview:scroll];

  self.loadButton = [NSButton buttonWithTitle:@"Load Selected Model"
                                       target:self action:@selector(loadClicked:)];
  self.loadButton.translatesAutoresizingMaskIntoConstraints = NO;
  self.loadButton.enabled = NO;
  [cv addSubview:self.loadButton];

  NSButton* cancel = [NSButton buttonWithTitle:@"Cancel"
                                        target:self action:@selector(cancelClicked:)];
  cancel.translatesAutoresizingMaskIntoConstraints = NO;
  [cv addSubview:cancel];

  NSDictionary* v = @{@"s":self.searchField, @"t":self.tagPopup, @"scroll":scroll,
                      @"load":self.loadButton, @"cancel":cancel,
                      @"fInc":fInc, @"fDec":fDec};

  [cv addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
    @"H:|-10-[s]-6-[t(170)]-6-[fDec(40)]-4-[fInc(40)]-10-|" options:0 metrics:nil views:v]];
  [cv addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
    @"H:|-10-[scroll]-10-|" options:0 metrics:nil views:v]];
  [cv addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
    @"H:[cancel(120)]-10-[load(210)]-10-|" options:0 metrics:nil views:v]];
  [cv addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
    @"V:|-10-[s(28)]-6-[scroll]-6-[load(30)]-10-|" options:0 metrics:nil views:v]];
  [cv addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
    @"V:|-10-[t(28)]" options:0 metrics:nil views:v]];
  [cv addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
    @"V:|-10-[fInc(28)]" options:0 metrics:nil views:v]];
  [cv addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
    @"V:|-10-[fDec(28)]" options:0 metrics:nil views:v]];
  [cv addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
    @"V:[cancel(30)]-10-|" options:0 metrics:nil views:v]];
  [cv addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
    @"V:|-10-[scroll]-10-|" options:0 metrics:nil views:v]];
  return self;
}

- (void)setDisplayRoot:(std::shared_ptr<NAMLibraryTreeNode>)root
{
  self.dataSource.displayRoot = root;
  [self.outlineView reloadData];
  NSInteger n = [self.outlineView numberOfRows];
  for (NSInteger i = 0; i < n; ++i)
    [self.outlineView expandItem:[self.outlineView itemAtRow:i]];
}

- (std::shared_ptr<NAMLibraryTreeNode>)selectedNode
{
  NSInteger row = self.outlineView.selectedRow;
  if (row < 0) return nullptr;
  id item = [self.outlineView itemAtRow:row];
  return item ? ((NAMNodeWrapper*)item).node : nullptr;
}

- (void)setAvailableTags:(const std::vector<std::string>&)tags
{
  [self.tagPopup removeAllItems];
  [self.tagPopup addItemWithTitle:@"All tags"];
  for (const auto& tag : tags)
  {
    if (!tag.empty())
    {
      NSString* title = [NSString stringWithUTF8String:tag.c_str()];
      if (title)
        [self.tagPopup addItemWithTitle:title];
    }
  }
  [self.tagPopup selectItemAtIndex:0];
}

- (std::string)currentQuery
{
  NSString* s = self.searchField.stringValue;
  return s.UTF8String ? s.UTF8String : "";
}

- (std::string)currentTag
{
  NSInteger idx = self.tagPopup.indexOfSelectedItem;
  if (idx <= 0)
    return "";
  NSString* s = self.tagPopup.selectedItem.title;
  return s.UTF8String ? s.UTF8String : "";
}

- (void)notifyFilterChanged
{
  if (self.onFilterChanged)
    self.onFilterChanged([self currentQuery], [self currentTag]);
}

- (void)searchChanged:(NSNotification*)n
{
  (void)n;
  [self notifyFilterChanged];
}

- (void)tagChanged:(id)sender
{
  (void)sender;
  [self notifyFilterChanged];
}

- (void)selectionChanged:(NSNotification*)n
{
  (void)n;
  auto node = [self selectedNode];
  self.loadButton.enabled = (node && node->IsModel()) ? YES : NO;
}

- (void)doubleClicked:(id)sender
{
  (void)sender;
  if ([self selectedNode] && [self selectedNode]->IsModel() && self.onLoad)
    self.onLoad();
}

- (void)loadClicked:(id)sender   { (void)sender; if (self.onLoad)    self.onLoad();    }
- (void)cancelClicked:(id)sender { (void)sender; if (self.onCancel)  self.onCancel();  }
- (void)fontInc:(id)sender       { (void)sender; if (self.onFontInc) self.onFontInc(); }
- (void)fontDec:(id)sender       { (void)sender; if (self.onFontDec) self.onFontDec(); }

- (void)windowWillClose:(NSNotification*)n
{
  (void)n;
  if (self.onWindowClose) self.onWindowClose();
}

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
}

@end

NAMLibraryBrowserWindow::NAMLibraryBrowserWindow(NAMLibraryManager* pLibraryMgr,
                                                 std::shared_ptr<NAMLibraryTreeNode> rootNode)
: mpLibraryManager(pLibraryMgr)
, mRootNode(rootNode)
, mpWindowController(nullptr)
{
  LoadSettings();
}

NAMLibraryBrowserWindow::~NAMLibraryBrowserWindow()
{
  Close();
}

void NAMLibraryBrowserWindow::Open(void* pParentWindow)
{
  if (mIsOpen)
    return;

  @autoreleasepool
  {
    NAMLibraryWindowController* ctrl =
      [[NAMLibraryWindowController alloc] initWithFontSize:mFontSize];

    if (mHasSavedBounds)
    {
      CGFloat screenH = [NSScreen mainScreen].frame.size.height;
      NSRect frame = NSMakeRect(mWindowX, screenH - mWindowY - mWindowH, mWindowW, mWindowH);
      BOOL onScreen = NO;
      for (NSScreen* scr in [NSScreen screens])
      {
        if (NSIntersectsRect(frame, scr.frame))
        {
          onScreen = YES;
          break;
        }
      }
      if (onScreen)
        [ctrl.window setFrame:frame display:NO];
    }
    else if (pParentWindow)
    {
      if (NSWindow* parentWin = ((__bridge NSView*)pParentWindow).window)
      {
        NSRect pf = parentWin.frame;
        [ctrl.window setFrame:NSMakeRect(pf.origin.x + pf.size.width + 10,
                                         pf.origin.y, mWindowW, mWindowH)
                      display:NO];
      }
    }

    __weak NAMLibraryWindowController* weak = ctrl;

    ctrl.onLoad = [this, weak]() {
      if (auto c = weak)
        if (auto node = [c selectedNode]; node && node->IsModel() && mOnModelSelected)
        {
          mOnModelSelected(node);
          Close();
        }
    };

    ctrl.onCancel = [this]() { Close(); };

    ctrl.onFontInc = [this, weak]() {
      if (mFontSize < mMaxFontSize)
      {
        mFontSize += 2;
        if (auto c = weak)
          c.outlineView.rowHeight = mFontSize * 1.4f;
        SaveSettings();
      }
    };

    ctrl.onFontDec = [this, weak]() {
      if (mFontSize > mMinFontSize)
      {
        mFontSize -= 2;
        if (auto c = weak)
          c.outlineView.rowHeight = mFontSize * 1.4f;
        SaveSettings();
      }
    };

    ctrl.onWindowClose = [this]() {
      if (mIsOpen)
        Close();
    };

    ctrl.onFilterChanged = [this, weak](const std::string& query, const std::string& selectedTag) {
      if (!mpLibraryManager)
      {
        mSearchRoot = nullptr;
      }
      else
      {
        const std::string queryTrimmed = Trim(query);
        const std::string selectedTagTrimmed = Trim(selectedTag);

        if (queryTrimmed.empty() && selectedTagTrimmed.empty())
        {
          mSearchRoot = nullptr;
        }
        else
        {
          std::vector<std::shared_ptr<NAMLibraryTreeNode>> results =
            queryTrimmed.empty() ? mpLibraryManager->GetAllModels() : mpLibraryManager->SearchModels(queryTrimmed);

          if (!selectedTagTrimmed.empty())
          {
            const std::string selectedLower = ToLowerAscii(selectedTagTrimmed);

            results.erase(
              std::remove_if(results.begin(), results.end(),
                [&](const std::shared_ptr<NAMLibraryTreeNode>& model) {
                  if (!model)
                    return true;

                  for (const auto& tag : model->tags)
                  {
                    if (ToLowerAscii(Trim(tag)) == selectedLower)
                      return false;
                  }

                  return true;
                }),
              results.end());
          }

          mSearchRoot = std::make_shared<NAMLibraryTreeNode>();
          mSearchRoot->name = "Filtered Results (" + std::to_string(results.size()) + " models)";
          mSearchRoot->id = "search_root";
          mSearchRoot->depth = 0;
          mSearchRoot->expanded = true;

          std::unordered_map<std::string, std::shared_ptr<NAMLibraryTreeNode>> nodeMap;
          std::unordered_map<std::string, std::unordered_set<std::string>> childrenAdded;

          auto addUniqueChild = [&](const std::shared_ptr<NAMLibraryTreeNode>& parentCopy,
                                    const std::shared_ptr<NAMLibraryTreeNode>& childCopy) {
            if (!parentCopy || !childCopy)
              return;

            auto& set = childrenAdded[parentCopy->id];
            if (set.insert(childCopy->id).second)
              parentCopy->children.push_back(childCopy);
          };

          std::function<std::shared_ptr<NAMLibraryTreeNode>(const std::shared_ptr<NAMLibraryTreeNode>&)> BuildAncestorChain;
          BuildAncestorChain = [&](const std::shared_ptr<NAMLibraryTreeNode>& node) -> std::shared_ptr<NAMLibraryTreeNode> {
            if (!node)
              return nullptr;

            if (auto it = nodeMap.find(node->id); it != nodeMap.end())
              return it->second;

            auto nodeCopy = std::make_shared<NAMLibraryTreeNode>(*node);
            nodeCopy->children.clear();
            nodeCopy->expanded = true;
            nodeMap.emplace(node->id, nodeCopy);

            if (node->parent)
            {
              auto parentCopy = BuildAncestorChain(node->parent);
              nodeCopy->parent = parentCopy;

              if (parentCopy)
              {
                nodeCopy->depth = parentCopy->depth + 1;
                addUniqueChild(parentCopy, nodeCopy);
              }
              else
              {
                nodeCopy->parent = mSearchRoot;
                nodeCopy->depth = 1;
                addUniqueChild(mSearchRoot, nodeCopy);
              }
            }
            else
            {
              nodeCopy->parent = mSearchRoot;
              nodeCopy->depth = 1;
              addUniqueChild(mSearchRoot, nodeCopy);
            }

            return nodeCopy;
          };

          for (const auto& model : results)
            BuildAncestorChain(model);
        }
      }

      if (auto c = weak)
        [c setDisplayRoot:mSearchRoot ? mSearchRoot : mRootNode];
    };

    std::set<std::string> tagSet;
    if (mpLibraryManager)
    {
      const auto& allModels = mpLibraryManager->GetAllModels();
      for (const auto& model : allModels)
      {
        if (!model)
          continue;
        for (const auto& tag : model->tags)
          if (!tag.empty())
            tagSet.insert(tag);
      }
    }
    std::vector<std::string> sortedTags(tagSet.begin(), tagSet.end());
    [ctrl setAvailableTags:sortedTags];

    [ctrl showWindow:nil];
    [ctrl.window makeKeyAndOrderFront:nil];

    mpWindowController = (__bridge_retained void*)ctrl;
    mIsOpen = true;
  }
}

void NAMLibraryBrowserWindow::Close()
{
  if (!mIsOpen)
    return;

  // Update bounds from the actual window *in-memory*.
  // Persist only size + font (X/Y remain instance-lifetime only).
#if defined(OS_WIN) || defined(_WIN32)
  if (mHwndDlg)
  {
    RECT rc{};
    if (GetWindowRect(mHwndDlg, &rc))
    {
      mWindowX = rc.left;
      mWindowY = rc.top;
      mWindowW = rc.right - rc.left;
      mWindowH = rc.bottom - rc.top;
      mHasSavedBounds = true; // instance-lifetime (not written)
    }
  }
#endif

  SaveSettings();

  if (mHwndDlg)
  {
    if (mSearchTimerId != 0)
    {
      KillTimer(mHwndDlg, SEARCH_TIMER_ID);
      mSearchTimerId = 0;
    }

    DestroyWindow(mHwndDlg);
    mHwndDlg = nullptr;
  }

  mTreeItemMap.clear();
  mIsOpen = false;
}
#else
  #error "Unsupported platform - need OS_WIN or OS_MAC defined"
#endif

bool NAMLibraryBrowserWindow::GetFolderExpandedFromState(const std::shared_ptr<NAMLibraryTreeNode>& node) const
{
  if (!node)
    return false;

  // Only folders (nodes with children) participate.
  if (node->children.empty())
    return false;

  if (!node->id.empty())
  {
    auto it = mExpandedState.find(node->id);
    if (it != mExpandedState.end())
      return it->second;
  }

  // Fall back to data.json only when we have no in-memory override.
  return node->expanded;
}

void NAMLibraryBrowserWindow::SetFolderExpandedInState(const std::shared_ptr<NAMLibraryTreeNode>& node, bool expanded)
{
  if (!node)
    return;

  if (node->children.empty())
    return;

  if (node->id.empty())
    return;

  mExpandedState[node->id] = expanded;
}

void NAMLibraryBrowserWindow::SetExpandedStateRecursive(const std::shared_ptr<NAMLibraryTreeNode>& node, bool expanded)
{
  if (!node)
    return;

  if (!node->children.empty())
    SetFolderExpandedInState(node, expanded);

  for (const auto& child : node->children)
    SetExpandedStateRecursive(child, expanded);
}