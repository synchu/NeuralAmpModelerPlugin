#pragma once

#include <cmath> // std::round
#include <sstream> // std::stringstream
#include <unordered_map> // std::unordered_map
#include "IControls.h"
#include "NAMLibraryBrowserPanel.h"

#define PLUG() static_cast<PLUG_CLASS_NAME*>(GetDelegate())
#define NAM_KNOB_HEIGHT 120.0f
#define NAM_SWTICH_HEIGHT 50.0f

using namespace iplug;
using namespace igraphics;

// Template mixin: injects OnDrop into any IControl-derived class
// so that file drag-and-drop routes to the plugin's HandleFileDrop().
// Replaces the IControl.h hack (OnDropFunc member).
template <typename T>
class WithFileDrop : public T
{
public:
  using T::T; // inherit all constructors
  void OnDrop(const char* str) override
  {
    static_cast<PLUG_CLASS_NAME*>(this->GetDelegate())->HandleFileDrop(str);
  }
};

// Where the corner button on the plugin (settings, close settings) goes
// :param rect: Rect for the whole plugin's UI
IRECT CornerButtonArea(const IRECT& rect)
{
  const auto mainArea = rect.GetPadded(-20);
  return mainArea.GetFromTRHC(50, 50).GetCentredInside(20, 20);
};

class NAMSquareButtonControl : public WithFileDrop<ISVGButtonControl>
{
public:
  NAMSquareButtonControl(const IRECT& bounds, IActionFunction af, const ISVG& svg)
  : WithFileDrop<ISVGButtonControl>(bounds, af, svg, svg)
  {
  }

  void Draw(IGraphics& g) override
  {
    if (mMouseIsOver)
      g.FillRoundRect(PluginColors::MOUSEOVER, mRECT, 2.f);

    ISVGButtonControl::Draw(g);
  }
};

class NAMCircleButtonControl : public ISVGButtonControl
{
public:
  NAMCircleButtonControl(const IRECT& bounds, IActionFunction af, const ISVG& svg)
  : ISVGButtonControl(bounds, af, svg, svg)
  {
  }

  void Draw(IGraphics& g) override
  {
    if (mMouseIsOver)
      g.FillEllipse(PluginColors::MOUSEOVER, mRECT);

    ISVGButtonControl::Draw(g);
  }
};

class NAMTextCircleButtonControl : public IButtonControlBase
{
public:
  NAMTextCircleButtonControl(const IRECT& bounds, IActionFunction af)
  : IButtonControlBase(bounds, af)
  {
  }

  void Draw(IGraphics& g) override
  {
    const IColor col = mDisabled ? PluginColors::NAM_3 : PluginColors::NAM_THEMECOLOR;

    if (!mDisabled && mMouseIsOver)
      g.FillEllipse(PluginColors::MOUSEOVER, mRECT);

    g.DrawEllipse(col, mRECT, nullptr, 1.5f);

    const float cx  = mRECT.MW();
    const float cy  = mRECT.MH();
    const float hw  = mRECT.W() * 0.27f;
    const float vsp = mRECT.H() * 0.13f;

    for (int i = -1; i <= 1; ++i)
      g.DrawLine(col, cx - hw, cy + i * vsp, cx + hw, cy + i * vsp, nullptr, 1.8f);
  }
};

class NAMKnobControl : public WithFileDrop<IVKnobControl>, public IBitmapBase
{
public:
  NAMKnobControl(const IRECT& bounds, int paramIdx, const char* label, const IVStyle& style, IBitmap bitmap)
  : WithFileDrop<IVKnobControl>(bounds, paramIdx, label, style, true)
  , IBitmapBase(bitmap)
  {
    mInnerPointerFrac = 0.55;
  }

  void OnRescale() override { mBitmap = GetUI()->GetScaledBitmap(mBitmap); }

  // Called from the delegate (OnIdle) to set slot-boundary positions.
  // Each value is normalised [0, 1] matching the knob's param range.
  void SetPNAMBoundaries(std::vector<float> boundaries)
  {
    mPNAMBoundaries = std::move(boundaries);
    SetDirty(false);
  }

  // Called from OnIdle to update the "2/5" counter drawn inside the knob.
  // Pass active=-1 to clear (no PNAM active).
  void SetSlotInfo(int active, int total)
  {
    if (active == mActiveSlot && total == mTotalSlots) return;
    mActiveSlot  = active;
    mTotalSlots  = total;
    SetDirty(false);
  }

  bool IsMouseOver() const { return mMouseIsOver; }

  void DrawWidget(IGraphics& g) override
  {
    float widgetRadius = GetRadius() * 0.73;
    auto knobRect = mWidgetBounds.GetCentredInside(mWidgetBounds.W(), mWidgetBounds.W());
    const float cx = knobRect.MW(), cy = knobRect.MH();
    const float angle = mAngle1 + (static_cast<float>(GetValue()) * (mAngle2 - mAngle1));
    DrawIndicatorTrack(g, angle, cx + 0.5, cy, widgetRadius);
    g.DrawFittedBitmap(mBitmap, knobRect);

    // ---- PNAM slot-boundary tick marks ----
    if (!mPNAMBoundaries.empty())
    {
      const float kPi = 3.14159265f;
      const float r0 = widgetRadius * 1.06f;
      const float r1 = widgetRadius * 1.22f;
      const IColor tickColor = PluginColors::NAM_THEMECOLOR.WithOpacity(0.85f);
      for (float b : mPNAMBoundaries)
      {
        const float tickAngle = mAngle1 + b * (mAngle2 - mAngle1);
        const float rad = tickAngle * kPi / 180.f;
        const float sinA = std::sin(rad), cosA = std::cos(rad);
        g.DrawLine(tickColor,
                   cx + r0 * sinA, cy - r0 * cosA,
                   cx + r1 * sinA, cy - r1 * cosA,
                   &mBlend, 2.f);
      }
    }

    float data[2][2];
    RadialPoints(angle, cx, cy, mInnerPointerFrac * widgetRadius, mInnerPointerFrac * widgetRadius, 2, data);
    g.PathCircle(data[1][0], data[1][1], 3);
    g.PathFill(IPattern::CreateRadialGradient(data[1][0], data[1][1], 4.0f,
                                              {{GetColor(mMouseIsOver ? kX3 : kX1), 0.f},
                                               {GetColor(mMouseIsOver ? kX3 : kX1), 0.8f},
                                               {COLOR_TRANSPARENT, 1.0f}}),
               {}, &mBlend);
    g.DrawCircle(COLOR_BLACK.WithOpacity(0.5f), data[1][0], data[1][1], 3, &mBlend);

    // ---- Hover tooltip: active slot NAM name ----
    if (mMouseIsOver && mTooltip.GetLength())
    {
      const IText tipText(12.f, COLOR_WHITE, "Roboto-Regular", EAlign::Center, EVAlign::Middle);
      const float pad  = 6.f;
      const float tipH = 18.f;

      IRECT measured(0, 0, 400, tipH);
      g.MeasureText(tipText, mTooltip.Get(), measured);
      const float tipW = measured.W() + pad * 2.f;

      const IRECT gb = g.GetBounds();
      float tipL = mRECT.MW() - tipW * 0.5f;
      float tipT = mRECT.T - tipH - 4.f;
      tipL = std::max(tipL, gb.L + 2.f);
      tipL = std::min(tipL, gb.R - tipW - 2.f);
      if (tipT < gb.T + 2.f)
        tipT = mRECT.B + 4.f;

      const IRECT tipRect(tipL, tipT, tipL + tipW, tipT + tipH);
      g.FillRoundRect(IColor(220, 30, 30, 30), tipRect, 3.f);
      g.DrawRoundRect(IColor(180, 100, 100, 100), tipRect, 3.f, nullptr, 1.f);
      g.DrawText(tipText, mTooltip.Get(), tipRect);
    }
  }

  void Draw(IGraphics& g) override
  {
    WithFileDrop<IVKnobControl>::Draw(g);

    if (mActiveSlot >= 0 && mTotalSlots > 0)
    {
      const std::string slotStr = std::to_string(mActiveSlot) + "/" + std::to_string(mTotalSlots);
      const IText slotText(12.f, COLOR_WHITE, "Roboto-Regular", EAlign::Center, EVAlign::Middle);

      const auto knobRect = mWidgetBounds.GetCentredInside(mWidgetBounds.W(), mWidgetBounds.W());
      const float cx = knobRect.MW();
      const float cy = knobRect.MH();

      const float pillW = 34.f;
      const float pillH = 16.f;
      const IRECT pillRect(cx - pillW * 0.5f, cy - pillH * 0.5f,
                           cx + pillW * 0.5f, cy + pillH * 0.5f);

      g.DrawText(slotText, slotStr.c_str(), pillRect);
    }
  }

private:
  std::vector<float> mPNAMBoundaries; // normalised [0,1] slot boundaries; empty = no pnam active
  int mActiveSlot  = -1; // 1-based active slot index; -1 = no PNAM active
  int mTotalSlots  =  0;
};

class NAMSwitchControl : public WithFileDrop<IVSlideSwitchControl>, public IBitmapBase
{
public:
  NAMSwitchControl(const IRECT& bounds, int paramIdx, const char* label, const IVStyle& style, IBitmap bitmap)
  : WithFileDrop<IVSlideSwitchControl>(bounds, paramIdx, label,
                         style.WithRoundness(0.666f)
                           .WithShowValue(false)
                           .WithEmboss(true)
                           .WithShadowOffset(1.5f)
                           .WithDrawShadows(false)
                           .WithColor(kFR, COLOR_BLACK)
                           .WithFrameThickness(0.5f)
                           .WithWidgetFrac(0.5f)
                           .WithLabelOrientation(EOrientation::South))
  , IBitmapBase(bitmap)
  {
  }

  void DrawWidget(IGraphics& g) override
  {
    DrawTrack(g, mWidgetBounds);
    DrawHandle(g, mHandleBounds);
  }

  void DrawTrack(IGraphics& g, const IRECT& bounds) override
  {
    IRECT handleBounds = GetAdjustedHandleBounds(bounds);
    handleBounds = IRECT(handleBounds.L, handleBounds.T, handleBounds.R, handleBounds.T + mBitmap.H());
    IRECT centreBounds = handleBounds.GetPadded(-mStyle.shadowOffset);
    IRECT shadowBounds = handleBounds.GetTranslated(mStyle.shadowOffset, mStyle.shadowOffset);
    //    const float contrast = mDisabled ? -GRAYED_ALPHA : 0.f;
    float cR = 7.f;
    const float tlr = cR;
    const float trr = cR;
    const float blr = cR;
    const float brr = cR;

    // outer shadow
    if (mStyle.drawShadows)
      g.FillRoundRect(GetColor(kSH), shadowBounds, tlr, trr, blr, brr, &mBlend);

    // Embossed style unpressed
    if (mStyle.emboss)
    {
      // Positive light
      g.FillRoundRect(GetColor(kPR), handleBounds, tlr, trr, blr, brr /*, &blend*/);

      // Negative light
      g.FillRoundRect(GetColor(kSH), shadowBounds, tlr, trr, blr, brr /*, &blend*/);

      // Fill in foreground
      g.FillRoundRect(GetValue() > 0.5 ? GetColor(kX1) : COLOR_BLACK, centreBounds, tlr, trr, blr, brr, &mBlend);

      // Shade when hovered
      if (mMouseIsOver)
        g.FillRoundRect(GetColor(kHL), centreBounds, tlr, trr, blr, brr, &mBlend);
    }
    else
    {
      g.FillRoundRect(GetValue() > 0.5 ? GetColor(kX1) : COLOR_BLACK, handleBounds, tlr, trr, blr, brr /*, &blend*/);

      // Shade when hovered
      if (mMouseIsOver)
        g.FillRoundRect(GetColor(kHL), handleBounds, tlr, trr, blr, brr, &mBlend);
    }

    if (mStyle.drawFrame)
      g.DrawRoundRect(GetColor(kFR), handleBounds, tlr, trr, blr, brr, &mBlend, mStyle.frameThickness);
  }

  void DrawHandle(IGraphics& g, const IRECT& filledArea) override
  {
    IRECT r;
    if (GetSelectedIdx() == 0)
    {
      r = filledArea.GetFromLeft(mBitmap.W());
    }
    else
    {
      r = filledArea.GetFromRight(mBitmap.W());
    }

    g.DrawBitmap(mBitmap, r, 0, 0, nullptr);
  }
};

class NAMFileNameControl : public WithFileDrop<IVButtonControl>
{
public:
  NAMFileNameControl(const IRECT& bounds, const char* label, const IVStyle& style)
  : WithFileDrop<IVButtonControl>(bounds, DefaultClickActionFunc, label, style)
  {
  }

  void DrawBackground(IGraphics& g, const IRECT& r) override
  {
  }

  void DrawWidget(IGraphics& g) override
  {
  }

  void SetLabelAndTooltip(const char* str)
  {
    SetLabelStr(str);
    SetTooltip(str);
  }

  void SetLabelAndTooltipEllipsizing(const WDL_String& fileName, size_t maxLength)
  {
    auto EllipsizeMiddle = [](const char* s, size_t maxLen) -> std::string {
      if (!s)
        return {};

      std::string str{s};
      if (str.size() <= maxLen)
        return str;

      const std::string ellipses = "...";
      if (maxLen <= ellipses.size())
        return str.substr(0, maxLen);

      const size_t keep = maxLen - ellipses.size();
      const size_t prefix = keep / 2;
      const size_t suffix = keep - prefix;

      return str.substr(0, prefix) + ellipses + str.substr(str.size() - suffix);
    };

    const char* baseName = fileName.get_filepart();
    auto ellipsized = EllipsizeMiddle(baseName, maxLength);
    SetLabelStr(ellipsized.c_str());
    SetTooltip(baseName);
  }
};

class NAMFileBrowserControl : public WithFileDrop<IDirBrowseControlBase>
{
public:
  NAMFileBrowserControl(const IRECT& bounds, int clearMsgTag, const char* labelStr, const char* fileExtension,
                    IFileDialogCompletionHandlerFunc ch, const IVStyle& style, const ISVG& loadSVG,
                    const ISVG& clearSVG, const ISVG& leftSVG, const ISVG& rightSVG, 
                    const ISVG& librarySVG,
                    const IBitmap& bitmap)
  : WithFileDrop<IDirBrowseControlBase>(bounds, "nam", false, false)  // "nam" only for ScanDirectory
  , mClearMsgTag(clearMsgTag)
  , mDefaultLabelStr(labelStr)
  , mCompletionHandlerFunc(ch)
  , mDialogExtension(fileExtension)   // file-open dialog filter (e.g. "nam pnam")
  , mStyle(style.WithDrawFrame(false))
  , mBitmap(bitmap)
  , mLoadSVG(loadSVG)
  , mClearSVG(clearSVG)
  , mLeftSVG(leftSVG)
  , mRightSVG(rightSVG)
  , mLibrarySVG(librarySVG)
  {
    mIgnoreMouse = true;
  }

  void Draw(IGraphics& g) override { g.DrawFittedBitmap(mBitmap, mRECT); }

  void OnPopupMenuSelection(IPopupMenu* pSelectedMenu, int valIdx) override
  {
    if (pSelectedMenu)
    {
      IPopupMenu::Item* pItem = pSelectedMenu->GetChosenItem();

      if (pItem)
      {
        mSelectedItemIndex = mItems.Find(pItem);
        LoadFileAtCurrentIndex();
      }
    }
  }

  void OnAttached() override
  {
    auto prevFileFunc = [&](IControl* pCaller) {
      const auto nItems = NItems();
      if (nItems == 0)
        return;
      mSelectedItemIndex--;

      if (mSelectedItemIndex < 0)
        mSelectedItemIndex = nItems - 1;

      LoadFileAtCurrentIndex();
    };

    auto nextFileFunc = [&](IControl* pCaller) {
      const auto nItems = NItems();
      if (nItems == 0)
        return;
      mSelectedItemIndex++;

      if (mSelectedItemIndex >= nItems)
        mSelectedItemIndex = 0;

      LoadFileAtCurrentIndex();
    };

    auto loadFileFunc = [&](IControl* pCaller) {
      WDL_String fileName;
      WDL_String path;
      GetSelectedFileDirectory(path);
#ifdef NAM_PICK_DIRECTORY
      pCaller->GetUI()->PromptForDirectory(path, [&](const WDL_String& fileName, const WDL_String& path) {
        if (path.GetLength())
        {
          ClearPathList();
          AddPath(path.Get(), "");
          SetupMenu();
          SelectFirstFile();
          LoadFileAtCurrentIndex();
        }
      });
#else
      pCaller->GetUI()->PromptForFile(
        fileName, path, EFileAction::Open, mDialogExtension.Get(), [&](const WDL_String& fileName, const WDL_String& path) {
          if (fileName.GetLength())
          {
            // If the dialog didn't return a directory (e.g. UNC/network paths),
            // derive it from the selected file path.
            const char* pathStr = path.Get();
            WDL_String derivedPath;
            if (!pathStr || !pathStr[0])
            {
              std::filesystem::path fp = std::filesystem::u8path(fileName.Get());
              std::string parentStr = fp.parent_path().string();
              derivedPath.Set(parentStr.c_str());
              pathStr = derivedPath.Get();
            }

            ClearPathList();
            if (pathStr && pathStr[0])
            {
              AddPath(pathStr, "");
              SetupMenu();
              SetSelectedFile(fileName.Get());
              LoadFileAtCurrentIndex();
            }
            else
            {
              // No directory available — call the completion handler directly.
              mCompletionHandlerFunc(fileName, path);
            }
          }
        });
#endif
    };

    auto clearFileFunc = [&](IControl* pCaller) {
      pCaller->GetDelegate()->SendArbitraryMsgFromUI(mClearMsgTag);
      mFileNameControl->SetLabelAndTooltip(mDefaultLabelStr.Get());
      // FIXME disabling output mode...
      //      pCaller->GetUI()->GetControlWithTag(kCtrlTagOutputMode)->SetDisabled(false);
    };

    auto chooseFileFunc = [&, loadFileFunc](IControl* pCaller) {
      if (std::string_view(pCaller->As<IVButtonControl>()->GetLabelStr()) == mDefaultLabelStr.Get())
      {
        loadFileFunc(pCaller);
      }
      else
      {
        CheckSelectedItem();

        if (!mMainMenu.HasSubMenus())
        {
          mMainMenu.SetChosenItemIdx(mSelectedItemIndex);
        }
        pCaller->GetUI()->CreatePopupMenu(*this, mMainMenu, pCaller->GetRECT());
      }
    };

    IRECT padded = mRECT.GetPadded(-6.f).GetHPadded(-2.f);
    const auto buttonWidth = padded.H();
    const auto loadFileButtonBounds = padded.ReduceFromLeft(buttonWidth);
    const auto clearButtonBounds = padded.ReduceFromRight(buttonWidth);
    const auto leftButtonBounds = padded.ReduceFromLeft(buttonWidth);
    const auto rightButtonBounds = padded.ReduceFromLeft(buttonWidth);
    const auto libraryButtonBounds = padded.ReduceFromLeft(buttonWidth);
    const auto fileNameButtonBounds = padded;

    AddChildControl(new NAMSquareButtonControl(loadFileButtonBounds, DefaultClickActionFunc, mLoadSVG))
      ->SetAnimationEndActionFunction(loadFileFunc);
    AddChildControl(new NAMSquareButtonControl(leftButtonBounds, DefaultClickActionFunc, mLeftSVG))
      ->SetAnimationEndActionFunction(prevFileFunc);
    AddChildControl(new NAMSquareButtonControl(rightButtonBounds, DefaultClickActionFunc, mRightSVG))
      ->SetAnimationEndActionFunction(nextFileFunc);
    
    // Only add library browser button for model browser (not IR browser)
    if (strstr(mExtension.Get(), "nam") != nullptr)
    {
      AddChildControl(new NAMSquareButtonControl(libraryButtonBounds, [this](IControl* pCaller) {
        auto* pPlugin = PLUG();
        
        // Use new separate window approach
        pPlugin->OpenLibraryBrowserWindow();
        
        /* OLD APPROACH: Modal panel overlay (kept for reference)
        auto rootNode = pPlugin->GetLibraryRootNode();
        
        if (!rootNode)
        {
          pCaller->GetUI()->ShowMessageBox(
            "NAM library not loaded.\n\nMake sure you have nam-model-manager installed.",
            "Library Not Found", kMB_OK);
          return;
        }
        
        // Make the library browser fill most of the plugin window
        const IRECT& pluginBounds = pCaller->GetUI()->GetBounds();
        const float modalPadding = 40.0f;
        IRECT modalBounds = pluginBounds.GetPadded(-modalPadding);
        
        auto* pBrowserPanel = new NAMLibraryBrowserPanel(
          modalBounds,
          &pPlugin->GetLibraryManager(),
          rootNode,
          mStyle
        );
        
        pBrowserPanel->SetOnModelSelected([this, pCaller](const std::shared_ptr<NAMLibraryTreeNode>& model) {
          if (model && model->IsModel())
          {
            WDL_String modelPath(model->path.c_str());
            mFileNameControl->SetLabelAndTooltipEllipsizing(WDL_String(model->name.c_str()));
            mCompletionHandlerFunc(modelPath, WDL_String());
            
            // Close the browser panel after selection
            pCaller->GetUI()->ForAllControlsFunc([](IControl* pControl) {
              if (auto* panel = dynamic_cast<NAMLibraryBrowserPanel*>(pControl)) {
                pControl->GetUI()->RemoveControl(pControl);
              }
            });
          }
        });
        
        pCaller->GetUI()->AttachControl(pBrowserPanel);
        */
      }, mLibrarySVG))->SetTooltip("Browse NAM Library");
    }
    
    AddChildControl(mFileNameControl = new NAMFileNameControl(fileNameButtonBounds, mDefaultLabelStr.Get(), mStyle))
      ->SetAnimationEndActionFunction(chooseFileFunc);
    AddChildControl(new NAMSquareButtonControl(clearButtonBounds, DefaultClickActionFunc, mClearSVG))
      ->SetAnimationEndActionFunction(clearFileFunc);

    mFileNameControl->SetLabelAndTooltip(mDefaultLabelStr.Get());
  }

  void LoadFileAtCurrentIndex()
  {
    if (mSelectedItemIndex > -1 && mSelectedItemIndex < NItems())
    {
      WDL_String fileName, path;
      GetSelectedFile(fileName);

      const size_t maxChars = (strstr(mExtension.Get(), "nam") != nullptr) ? 35 : 45;
      mFileNameControl->SetLabelAndTooltipEllipsizing(fileName, maxChars);

      mCompletionHandlerFunc(fileName, path);
    }
  }

  void OnMsgFromDelegate(int msgTag, int dataSize, const void* pData) override
  {
    switch (msgTag)
    {
      case kMsgTagLoadFailed:
        {
          std::string label(std::string("(FAILED) ") + std::string(mFileNameControl->GetLabelStr()));
          mFileNameControl->SetLabelAndTooltip(label.c_str());
        }
        break;
      case kMsgTagLoadedModel:
      case kMsgTagLoadedIR:
      {
        WDL_String fileName, directory;
        fileName.Set(reinterpret_cast<const char*>(pData));
        directory.Set(reinterpret_cast<const char*>(pData));
        directory.remove_filepart(true);

        const size_t maxChars = (strstr(mExtension.Get(), "nam") != nullptr) ? 35 : 45;

        // Only populate the prev/next file navigation if we have a valid directory.
        // Display-only strings (e.g. "[Chain] MyChain" from .pnam loads) have no directory.
        if (directory.GetLength())
        {
          ClearPathList();
          AddPath(directory.Get(), "");
          SetupMenu();
          SetSelectedFile(fileName.Get());
          mFileNameControl->SetLabelAndTooltipEllipsizing(fileName, maxChars);
        }
        else
        {
          // No directory — this is a display-only label (e.g. a .pnam chain name).
          // Just update the label directly; don't touch the file navigation list.
          mFileNameControl->SetLabelAndTooltip(fileName.Get());
          // Cache this as the "resting" label so hover-restore knows what to return to
          mRestingLabel.Set(fileName.Get());
        }
        break;
      }
      // Option D: hover on Voice knob — temporarily show active slot name in model browser
      case kMsgTagSetBrowserLabel:
      {
        // Ellipsize to the same limit as regular model names so it never
        // overflows into the library browser icon button area
        WDL_String label(reinterpret_cast<const char*>(pData));
        mFileNameControl->SetLabelAndTooltipEllipsizing(label, 40);
        break;
      }
      // Option D: mouse-out from Voice knob — restore chain name
      case kMsgTagRestoreBrowserLabel:
      {
        mFileNameControl->SetLabelAndTooltip(mRestingLabel.Get());
        break;
      }
      default: break;
    }
  }

private:
  void SelectFirstFile() { mSelectedItemIndex = mFiles.GetSize() ? 0 : -1; }

  void GetSelectedFileDirectory(WDL_String& path)
  {
    GetSelectedFile(path);
    path.remove_filepart();
    return;
  }

  int mClearMsgTag;
  WDL_String mDefaultLabelStr;
  WDL_String mRestingLabel;      // last chain/model display name, for hover-restore
  IFileDialogCompletionHandlerFunc mCompletionHandlerFunc;
  WDL_String mDialogExtension;
  IVStyle mStyle;
  NAMFileNameControl* mFileNameControl = nullptr;
  IBitmap mBitmap;
  ISVG mLoadSVG;
  ISVG mClearSVG;
  ISVG mLeftSVG;
  ISVG mRightSVG;
  ISVG mLibrarySVG;
};

class NAMMeterControl : public WithFileDrop<IVPeakAvgMeterControl<>>, public IBitmapBase
{
  static constexpr float KMeterMin = -70.0f;
  static constexpr float KMeterMax = -0.01f;

public:
  NAMMeterControl(const IRECT& bounds, const IBitmap& bitmap, const IVStyle& style)
  : WithFileDrop<IVPeakAvgMeterControl<>>(bounds, "", style.WithShowValue(false).WithDrawFrame(false).WithWidgetFrac(0.8),
                            EDirection::Vertical, {}, 0, KMeterMin, KMeterMax, {})
  , IBitmapBase(bitmap)
  {
    SetPeakSize(1.0f);
  }

  void OnRescale() override { mBitmap = GetUI()->GetScaledBitmap(mBitmap); }

  virtual void OnResize() override
  {
    SetTargetRECT(MakeRects(mRECT));
    mWidgetBounds = mWidgetBounds.GetMidHPadded(5).GetVPadded(10);
    MakeTrackRects(mWidgetBounds);
    MakeStepRects(mWidgetBounds, mNSteps);
    SetDirty(false);
  }

  void DrawBackground(IGraphics& g, const IRECT& r) override { g.DrawFittedBitmap(mBitmap, r); }

  void DrawTrackHandle(IGraphics& g, const IRECT& r, int chIdx, bool aboveBaseValue) override
  {
    if (r.H() > 2)
      g.FillRect(GetColor(kX1), r, &mBlend);
  }

  void DrawPeak(IGraphics& g, const IRECT& r, int chIdx, bool aboveBaseValue) override
  {
    g.DrawGrid(COLOR_BLACK, mTrackBounds.Get()[chIdx], 10, 2);
    g.FillRect(GetColor(kX3), r, &mBlend);
  }
};

// Container where we can refer to children by names instead of indices
class IContainerBaseWithNamedChildren : public WithFileDrop<IContainerBase>
{
public:
  IContainerBaseWithNamedChildren(const IRECT& bounds)
  : WithFileDrop<IContainerBase>(bounds){};
  ~IContainerBaseWithNamedChildren() = default;

protected:
  IControl* AddNamedChildControl(IControl* control, std::string name, int ctrlTag = kNoTag, const char* group = "")
  {
    // Make sure we haven't already used this name
    assert(mChildNameIndexMap.find(name) == mChildNameIndexMap.end());
    mChildNameIndexMap[name] = NChildren();
    return AddChildControl(control, ctrlTag, group);
  };

  IControl* GetNamedChild(std::string name)
  {
    const int index = mChildNameIndexMap[name];
    return GetChild(index);
  };


private:
  std::unordered_map<std::string, int> mChildNameIndexMap;
}; // class IContainerBaseWithNamedChildren


struct PossiblyKnownParameter
{
  bool known = false;
  double value = 0.0;
};

struct ModelInfo
{
  PossiblyKnownParameter sampleRate;
  PossiblyKnownParameter inputCalibrationLevel;
  PossiblyKnownParameter outputCalibrationLevel;
};

class ModelInfoControl : public IContainerBaseWithNamedChildren
{
public:
  ModelInfoControl(const IRECT& bounds, const IVStyle& style)
  : IContainerBaseWithNamedChildren(bounds)
  , mStyle(style){};

  void ClearModelInfo()
  {
    static_cast<IVLabelControl*>(GetNamedChild(mControlNames.sampleRate))->SetStr("");
    mHasInfo = false;
  };

  void Hide(bool hide) override
  {
    // Don't show me unless I have info to show!
    IContainerBase::Hide(hide || (!mHasInfo));
  };

  void OnAttached() override
  {
    AddChildControl(new IVLabelControl(GetRECT().SubRectVertical(4, 0), "Model information:", mStyle));
    AddNamedChildControl(new IVLabelControl(GetRECT().SubRectVertical(4, 1), "", mStyle), mControlNames.sampleRate);
    AddNamedChildControl(new IVLabelControl(GetRECT().SubRectVertical(4, 2), "", mStyle), mControlNames.inputCalibrationLevel);
     AddNamedChildControl(
       new IVLabelControl(GetRECT().SubRectVertical(4, 3), "", mStyle), mControlNames.outputCalibrationLevel);
  };

  void SetModelInfo(const ModelInfo& modelInfo)
  {
    auto SetControlStr = [&](const std::string& name, const PossiblyKnownParameter& p, const std::string& units,
                             const std::string& childName) {
      std::stringstream ss;
      ss << name << ": ";
      if (p.known)
      {
        ss << p.value << " " << units;
      }
      else
      {
        ss << "(Unknown)";
      }
      static_cast<IVLabelControl*>(GetNamedChild(childName))->SetStr(ss.str().c_str());
    };

    SetControlStr("Sample rate", modelInfo.sampleRate, "Hz", mControlNames.sampleRate);
    SetControlStr(
       "Input calibration level", modelInfo.inputCalibrationLevel, "dBu", mControlNames.inputCalibrationLevel);
     SetControlStr(
       "Output calibration level", modelInfo.outputCalibrationLevel, "dBu", mControlNames.outputCalibrationLevel);

    mHasInfo = true;
  };

private:
  const IVStyle mStyle;
  struct
  {
    const std::string sampleRate = "sampleRate";
    const std::string inputCalibrationLevel = "inputCalibrationLevel";
    const std::string outputCalibrationLevel = "outputCalibrationLevel";
  } mControlNames;
  // Do I have info?
  bool mHasInfo = false;
};

class OutputModeControl : public WithFileDrop<IVRadioButtonControl>
{
public:
  OutputModeControl(const IRECT& bounds, int paramIdx, const IVStyle& style, float buttonSize)
  : WithFileDrop<IVRadioButtonControl>(
    bounds, paramIdx, {}, "Output Mode", style, EVShape::Ellipse, EDirection::Vertical, buttonSize){};

  void SetNormalizedDisable(const bool disable)
  {
    // HACK non-DRY string and hard-coded indices
    std::stringstream ss;
    ss << "Normalized";
    if (disable)
    {
      ss << " [Not supported by model]";
    }
    mTabLabels.Get(1)->Set(ss.str().c_str());
  };
  void SetCalibratedDisable(const bool disable)
  {
    // HACK non-DRY string and hard-coded indices
    std::stringstream ss;
    ss << "Calibrated";
    if (disable)
    {
      ss << " [Not supported by model]";
    }
    mTabLabels.Get(2)->Set(ss.str().c_str());
  };
};

class NAMSettingsPageControl : public IContainerBaseWithNamedChildren
{
public:
  NAMSettingsPageControl(const IRECT& bounds, const IBitmap& bitmap, const IBitmap& inputLevelBackgroundBitmap,
                         const IBitmap& switchBitmap, ISVG closeSVG, const IVStyle& style,
                         const IVStyle& radioButtonStyle)
  : IContainerBaseWithNamedChildren(bounds)
  , mAnimationTime(0)
  , mBitmap(bitmap)
  , mInputLevelBackgroundBitmap(inputLevelBackgroundBitmap)
  , mSwitchBitmap(switchBitmap)
  , mStyle(style)
  , mRadioButtonStyle(radioButtonStyle)
  , mCloseSVG(closeSVG)
  {
    mIgnoreMouse = false;
  }

  void ClearModelInfo()
  {
    auto* modelInfoControl = static_cast<ModelInfoControl*>(GetNamedChild(mControlNames.modelInfo));
    assert(modelInfoControl != nullptr);
    modelInfoControl->ClearModelInfo();
  }

  bool OnKeyDown(float x, float y, const IKeyPress& key) override
  {
    if (key.VK == kVK_ESCAPE)
    {
      HideAnimated(true);
      return true;
    }

    return false;
  }

  void HideAnimated(bool hide)
  {
    mWillHide = hide;

    if (hide == false)
    {
      mHide = false;
    }
    else // hide subcontrols immediately
    {
      ForAllChildrenFunc([hide](int childIdx, IControl* pChild) { pChild->Hide(hide); });
    }

    SetAnimation(
      [&](IControl* pCaller) {
        auto progress = static_cast<float>(pCaller->GetAnimationProgress());

        if (mWillHide)
          SetBlend(IBlend(EBlend::Default, 1.0f - progress));
        else
          SetBlend(IBlend(EBlend::Default, progress));

        if (progress > 1.0f)
        {
          pCaller->OnEndAnimation();
          IContainerBase::Hide(mWillHide);
          GetUI()->SetAllControlsDirty();
          return;
        }
      },
      mAnimationTime);

    SetDirty(true);
  }

  void OnAttached() override
  {
    const float pad = 20.0f;
    const IVStyle titleStyle = DEFAULT_STYLE.WithValueText(IText(30, COLOR_WHITE, "Michroma-Regular"))
                                 .WithDrawFrame(false)
                                 .WithShadowOffset(2.f);
    const auto text = IText(DEFAULT_TEXT_SIZE, EAlign::Center, PluginColors::HELP_TEXT);
    const auto leftText = text.WithAlign(EAlign::Near);
    const auto style = mStyle.WithDrawFrame(false).WithValueText(text);
    const IVStyle leftStyle = style.WithValueText(leftText);

    AddNamedChildControl(new IBitmapControl(GetRECT(), mBitmap), mControlNames.bitmap)->SetIgnoreMouse(true);
    const auto titleArea = GetRECT().GetPadded(-(pad + 10.0f)).GetFromTop(50.0f);
    AddNamedChildControl(new IVLabelControl(titleArea, "SETTINGS", titleStyle), mControlNames.title);

    // Attach input/output calibration controls
    {
      const float height = NAM_KNOB_HEIGHT + NAM_SWTICH_HEIGHT + 10.0f;
      const float width = titleArea.W();
      const auto inputOutputArea = titleArea.GetFromBottom(height).GetTranslated(0.0f, height);
      const auto inputArea = inputOutputArea.GetFromLeft(0.5f * width);
      const auto outputArea = inputOutputArea.GetFromRight(0.5f * width);

      const float knobWidth = 87.0f; // HACK based on looking at the main page knobs.
      const auto inputLevelArea =
        inputArea.GetFromTop(NAM_KNOB_HEIGHT).GetFromBottom(25.0f).GetMidHPadded(0.5f * knobWidth);
      const auto inputSwitchArea = inputArea.GetFromBottom(NAM_SWTICH_HEIGHT).GetMidHPadded(0.5f * knobWidth);

      auto* inputLevelControl = AddNamedChildControl(
        new InputLevelControl(inputLevelArea, kInputCalibrationLevel, mInputLevelBackgroundBitmap, text),
        mControlNames.inputCalibrationLevel, kCtrlTagInputCalibrationLevel);
      inputLevelControl->SetTooltip(
        "The analog level, in dBu RMS, that corresponds to digital level of 0 dBFS peak in the host as its signal "
        "enters this plugin.");
      AddNamedChildControl(
        new NAMSwitchControl(inputSwitchArea, kCalibrateInput, "Calibrate Input", mStyle, mSwitchBitmap),
        mControlNames.calibrateInput, kCtrlTagCalibrateInput);

      // Same-ish height & width as input controls
      const auto outputRadioArea = outputArea.GetFromBottom(
        1.1f * (inputLevelArea.H() + inputSwitchArea.H())); // .GetMidHPadded(0.55f * knobWidth);
      const float buttonSize = 10.0f;
      auto* outputModeControl =
        AddNamedChildControl(new OutputModeControl(outputRadioArea, kOutputMode, mRadioButtonStyle, buttonSize),
                             mControlNames.outputMode, kCtrlTagOutputMode);
      outputModeControl->SetTooltip(
        "How to adjust the level of the output.\nRaw=No adjustment.\nNormalized=Adjust the level so that all models "
        "are about the same loudness.\nCalibrated=Match the input's digital-analog calibration.");
    }

    const float halfWidth = PLUG_WIDTH / 2.0f - pad;
    const auto bottomArea = GetRECT().GetPadded(-pad).GetFromBottom(78.0f);
    const float lineHeight = 15.0f;
    const auto modelInfoArea = bottomArea.GetFromLeft(halfWidth).GetFromTop(4 * lineHeight);
    const auto aboutArea = bottomArea.GetFromRight(halfWidth).GetFromTop(5 * lineHeight);
    AddNamedChildControl(new ModelInfoControl(modelInfoArea, leftStyle), mControlNames.modelInfo);
    AddNamedChildControl(new AboutControl(aboutArea, leftStyle, leftText), mControlNames.about);

    auto closeAction = [&](IControl* pCaller) {
      static_cast<NAMSettingsPageControl*>(pCaller->GetParent())->HideAnimated(true);
    };
    AddNamedChildControl(
      new NAMSquareButtonControl(CornerButtonArea(GetRECT()), closeAction, mCloseSVG), mControlNames.close);

    OnResize();
  }

  void SetModelInfo(const ModelInfo& modelInfo)
  {
    auto* modelInfoControl = static_cast<ModelInfoControl*>(GetNamedChild(mControlNames.modelInfo));
    assert(modelInfoControl != nullptr);
    modelInfoControl->SetModelInfo(modelInfo);
  };

private:
  IBitmap mBitmap;
  IBitmap mInputLevelBackgroundBitmap;
  IBitmap mSwitchBitmap;
  IVStyle mStyle;
  IVStyle mRadioButtonStyle;
  ISVG mCloseSVG;
  int mAnimationTime = 200;
  bool mWillHide = false;

  // Names for controls
  // Make sure that these are all unique and that you use them with AddNamedChildControl
  struct ControlNames
  {
    const std::string about = "About";
    const std::string bitmap = "Bitmap";
    const std::string calibrateInput = "CalibrateInput";
    const std::string close = "Close";
    const std::string inputCalibrationLevel = "InputCalibrationLevel";
    const std::string modelInfo = "ModelInfo";
    const std::string outputMode = "OutputMode";
    const std::string title = "Title";
  } mControlNames;

  class InputLevelControl : public WithFileDrop<IEditableTextControl>
  {
  public:
    InputLevelControl(const IRECT& bounds, int paramIdx, const IBitmap& bitmap, const IText& text = DEFAULT_TEXT,
                      const IColor& BGColor = DEFAULT_BGCOLOR)
    : WithFileDrop<IEditableTextControl>(bounds, "", text, BGColor)
    , mBitmap(bitmap)
    {
      SetParamIdx(paramIdx);
    };

    void Draw(IGraphics& g) override
    {
      g.DrawFittedBitmap(mBitmap, mRECT);
      ITextControl::Draw(g);
    };

    void SetValueFromUserInput(double normalizedValue, int valIdx) override
    {
      IControl::SetValueFromUserInput(normalizedValue, valIdx);
      const std::string s = ConvertToString(normalizedValue);
      OnTextEntryCompletion(s.c_str(), valIdx);
    };

    void SetValueFromDelegate(double normalizedValue, int valIdx) override
    {
      IControl::SetValueFromDelegate(normalizedValue, valIdx);
      const std::string s = ConvertToString(normalizedValue);
      OnTextEntryCompletion(s.c_str(), valIdx);
    };

  private:
    std::string ConvertToString(const double normalizedValue)
    {
      const double naturalValue = GetParam()->FromNormalized(normalizedValue);
      // And make the value to display
      std::stringstream ss;
      ss << naturalValue << " dBu";
      std::string s = ss.str();
      return s;
    };

    IBitmap mBitmap;
  };

  class AboutControl : public IContainerBase
  {
  public:
    AboutControl(const IRECT& bounds, const IVStyle& style, const IText& text)
    : IContainerBase(bounds)
    , mStyle(style)
    , mText(text){};

    void OnAttached() override
    {
      WDL_String verStr, buildInfoStr;
      PLUG()->GetPluginVersionStr(verStr);

      buildInfoStr.SetFormatted(100, "Version %s %s %s", verStr.Get(), PLUG()->GetArchStr(), PLUG()->GetAPIStr());

      AddChildControl(new IVLabelControl(GetRECT().SubRectVertical(5, 0), "NEURAL AMP MODELER", mStyle));
      AddChildControl(new IVLabelControl(GetRECT().SubRectVertical(5, 1), "By Steven Atkinson", mStyle));
      AddChildControl(new IVLabelControl(GetRECT().SubRectVertical(5, 2), buildInfoStr.Get(), mStyle));
      AddChildControl(new IURLControl(GetRECT().SubRectVertical(5, 3),
                                      "Plug-in development: Steve Atkinson, Oli Larkin, ... ",
                                      "https://github.com/sdatkinson/NeuralAmpModelerPlugin/graphs/contributors", mText,
                                      COLOR_TRANSPARENT, PluginColors::HELP_TEXT_MO, PluginColors::HELP_TEXT_CLICKED));
      AddChildControl(new IURLControl(GetRECT().SubRectVertical(5, 4), "www.neuralampmodeler.com",
                                      "https://www.neuralampmodeler.com", mText, COLOR_TRANSPARENT,
                                      PluginColors::HELP_TEXT_MO, PluginColors::HELP_TEXT_CLICKED));
      
    };

  private:
    IVStyle mStyle;
    IText mText;
  };
};



