#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

#include "NAMLibraryManager.h"
#include "NAMLibraryTreeNode.h"

#if defined(_WIN32) && !defined(OS_WIN)
  #define OS_WIN
#endif

#if defined(__APPLE__) && !defined(OS_MAC)
  #define OS_MAC
#endif

#if defined(OS_WIN)
  #include <windows.h>
  #include <CommCtrl.h>
#endif

class NAMLibraryBrowserWindow
{
public:
  NAMLibraryBrowserWindow(NAMLibraryManager* pLibraryMgr, std::shared_ptr<NAMLibraryTreeNode> rootNode);
  ~NAMLibraryBrowserWindow();

  void Open(void* pParentWindow);
  void Close();
  bool IsOpen() const { return mIsOpen; }

  void SetOnModelSelected(std::function<void(const std::shared_ptr<NAMLibraryTreeNode>&)> callback)
  {
    mOnModelSelected = callback;
  }

private:
#if defined(OS_WIN)
  void InitializeControls();
  void PopulateTreeView();
  void AddTreeNode(HTREEITEM hParent, const std::shared_ptr<NAMLibraryTreeNode>& node, bool ancestorsExpanded);
  void AutoExpandDescendantsFromFlags(HTREEITEM hParentItem);

  void OnTreeViewSelectionChanged();
  void OnTreeViewDoubleClick();
  void OnLoadButtonClicked();
  void OnSearchTextChanged();
  void OnTagSelectionChanged();
  void PopulateTagComboBox(const std::vector<std::shared_ptr<NAMLibraryTreeNode>>* pModelsForTags = nullptr);
  void PerformSearch(const std::string& query);
  void ResizeControls(int width, int height);

  void IncreaseFontSize();
  void DecreaseFontSize();
  void UpdateFontSize();
  void RecreateFont();
  void UpdateChildFonts();

  static INT_PTR CALLBACK DialogProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam);
  INT_PTR HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

  bool mIsAutoExpanding = false;
#endif

  // Cross-platform in-memory folder expansion state helpers
  bool GetFolderExpandedFromState(const std::shared_ptr<NAMLibraryTreeNode>& node) const;
  void SetFolderExpandedInState(const std::shared_ptr<NAMLibraryTreeNode>& node, bool expanded);
  void SetExpandedStateRecursive(const std::shared_ptr<NAMLibraryTreeNode>& node, bool expanded);

  void LoadSettings();
  void SaveSettings();

  static std::string GetSettingsFilePath();

  NAMLibraryManager* mpLibraryManager = nullptr;
  std::shared_ptr<NAMLibraryTreeNode> mRootNode;
  std::shared_ptr<NAMLibraryTreeNode> mSearchRoot;
  std::shared_ptr<NAMLibraryTreeNode> mSelectedNode;

  std::function<void(const std::shared_ptr<NAMLibraryTreeNode>&)> mOnModelSelected;

#if defined(OS_WIN)
  HWND mParentHwnd = nullptr;
  HWND mHwndDlg = nullptr;
  HWND mHwndTreeView = nullptr;
  HWND mHwndSearchEdit = nullptr;
  HWND mHwndSearchLabel = nullptr;
  HWND mHwndTagLabel = nullptr;
  HWND mHwndTagCombo = nullptr;
  HWND mHwndLoadButton = nullptr;
  HWND mHwndCancelButton = nullptr;
  HWND mHwndFontIncButton = nullptr;
  HWND mHwndFontDecButton = nullptr;

  HFONT mHFont = nullptr;
  HBRUSH mDarkBgBrush = nullptr;
  HBRUSH mEditBgBrush = nullptr;

  std::unordered_map<HTREEITEM, std::shared_ptr<NAMLibraryTreeNode>> mTreeItemMap;

  UINT_PTR mSearchTimerId = 0;
  std::string mPendingSearchQuery;
  std::string mSelectedTag;

  static constexpr UINT_PTR SEARCH_TIMER_ID = 1;
  static constexpr UINT SEARCH_DELAY_MS = 300;

  bool mIsPopulatingTags = false;

#elif defined(OS_MAC)
  void* mpWindowController = nullptr;
#endif

  // Cross-platform process-lifetime UI state
  std::unordered_map<std::string, bool> mExpandedState;

  int mFontSize = 30;
  const int mMinFontSize = 12;
  const int mMaxFontSize = 48;

  bool mIsOpen = false;

  int mMinWidth = 600;
  int mMinHeight = 400;

  int mWindowX = 0;
  int mWindowY = 0;
  int mWindowW = 800;
  int mWindowH = 600;
  bool mHasSavedBounds = false;
};