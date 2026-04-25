#pragma once

#include <string>
#include <vector>
#include <functional>
#include <optional>

#include "NAMModelMapper.h"

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

class NAMPNAMEditorWindow
{
public:
  NAMPNAMEditorWindow();
  ~NAMPNAMEditorWindow();

  void Open(void* pParentWindow);
  void Close();
  bool IsOpen() const { return mIsOpen; }
  void BringToFront();

  void LoadFile(const std::string& pnamPath);
  void SetOnSaved(std::function<void(const std::string&)> callback) { mOnSaved = callback; }

  void SetOnPreviewSlot(std::function<void(const ModelMapSlot&)> callback) { mOnPreviewSlot = callback; }
  void SetOnPreviewChain(std::function<void(const std::string&)> callback) { mOnPreviewChain = callback; }

  std::function<void()> mOnWindowClosed;

private:
  bool mIsOpen = false;
  bool mDirty  = false;

  std::string               mCurrentFilePath;
  std::vector<ModelMapSlot> mSlots;

  std::function<void(const std::string&)>  mOnSaved;
  std::function<void(const ModelMapSlot&)> mOnPreviewSlot;
  std::function<void(const std::string&)>  mOnPreviewChain;

  // Settings persisted across sessions
  std::string mLastOpenedPNAMPath;
  int  mFontSize        = 22;
  int  mWindowX         = 0;
  int  mWindowY         = 0;
  int  mWindowW         = 920;
  int  mWindowH         = 700;
  bool mHasSavedBounds  = false;

  static std::string GetSettingsFilePath();
  void LoadSettings();
  void SaveSettings();

#if defined(OS_WIN)
  // Main window
  HWND mHwnd               = nullptr;
  HWND mParentHwnd         = nullptr;
  HFONT mHFont             = nullptr;
  HBRUSH mDarkBgBrush      = nullptr;
  HBRUSH mEditBgBrush      = nullptr;

  // Toolbar
  HWND mHwndFileLbl        = nullptr;
  HWND mHwndFilePathLabel  = nullptr;
  HWND mHwndNewBtn         = nullptr;
  HWND mHwndOpenBtn        = nullptr;
  HWND mHwndSaveBtn        = nullptr;
  HWND mHwndSaveAsBtn      = nullptr;
  HWND mHwndFontDecBtn     = nullptr;
  HWND mHwndFontIncBtn     = nullptr;
  HWND mHwndHSep           = nullptr;

  // Left panel
  HWND mHwndSlotsLbl       = nullptr;
  HWND mHwndSlotList       = nullptr;
  HWND mHwndAddBtn         = nullptr;
  HWND mHwndRemoveBtn      = nullptr;
  HWND mHwndUpBtn          = nullptr;
  HWND mHwndDownBtn        = nullptr;
  HWND mHwndVSep           = nullptr;

  // Right panel
  HWND mHwndSelLbl           = nullptr;
  HWND mHwndGainLbl          = nullptr;
  HWND mHwndGainMinEdit      = nullptr;
  HWND mHwndGainMaxLbl       = nullptr;
  HWND mHwndGainMaxEdit      = nullptr;
  HWND mHwndNamLbl           = nullptr;
  HWND mHwndNamPathEdit      = nullptr;
  HWND mHwndBrowseBtn        = nullptr;
  HWND mHwndOvLbl            = nullptr;
  HWND mHwndOvOutputCheck    = nullptr;
  HWND mHwndOvOutputEdit     = nullptr;
  HWND mHwndOvBassCheck      = nullptr;
  HWND mHwndOvBassEdit       = nullptr;
  HWND mHwndOvMidCheck       = nullptr;
  HWND mHwndOvMidEdit        = nullptr;
  HWND mHwndOvTrebleCheck    = nullptr;
  HWND mHwndOvTrebleEdit     = nullptr;
  HWND mHwndApplyBtn         = nullptr;
  HWND mHwndPreviewSlotBtn   = nullptr;
  HWND mHwndPreviewChainBtn  = nullptr;
  HWND mHwndDistributeBtn    = nullptr;
  HWND mHwndCloseBtn         = nullptr;

  // Splitter state
  int  mSplitX             = 360;
  bool mDraggingSplitter   = false;
  static const int kSplitHitW = 6;

  // Font sizing
  static const int kMinFontSize = 14;
  static const int kMaxFontSize = 40;

  void InitializeControls();
  void ResizeControls(int width, int height);
  void RecreateFont();
  void UpdateChildFonts();
  void IncreaseFontSize();
  void DecreaseFontSize();
  void RefreshSlotList();
  int  GetSelectedSlotIndex() const;
  void SelectSlotIndex(int idx);
  void UpdateEditPanelFromSlot(int idx);
  ModelMapSlot ReadEditPanelToSlot() const;
  void SetEditPanelEnabled(bool enabled);
  void UpdateTitleBar();

  void OnSelectionChanged();
  void OnAddSlot();
  void OnRemoveSlot();
  void OnMoveUp();
  void OnMoveDown();
  void OnBrowseNAM();
  void OnApplySlot();
  void OnPreviewSlot();
  void OnDistributeGain();
  void OnPreviewChain();
  void OnNewFile();
  void OnOpenFile();
  void OnSave();
  void OnSaveAs();
  bool SaveToFile(const std::string& path);
  bool PromptSaveIfDirty();

  void OnDropFiles(HDROP hDrop);

    std::vector<int> GetSelectedSlotIndices() const; // all selected rows
  LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

#elif defined(OS_MAC)
  void* mpWindowController = nullptr;
#endif
};