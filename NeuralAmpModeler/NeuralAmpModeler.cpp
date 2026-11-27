#include <algorithm> // std::clamp, std::min
#include <cctype>
#include <cmath> // pow
#include <cfenv>
#include <filesystem>
#include <iostream>
#include <limits>
#include <utility>
#include <cstdlib>
#ifdef __APPLE__
#include <pwd.h>
#include <unistd.h>
#endif
#ifdef __linux__
#include <pwd.h>
#include <unistd.h>
#endif

#include "Colors.h"
#include "../NeuralAmpModelerCore/NAM/activations.h"
#include "../NeuralAmpModelerCore/NAM/get_dsp.h"
#include "NAMLibraryBrowserWindow.h"
// clang-format off
// These includes need to happen in this order or else the latter won't know
// a bunch of stuff.
#include "NeuralAmpModeler.h"
#include "IPlug_include_in_plug_src.h"
// clang-format on
#include "architecture.hpp"

#include "NAMLibraryManager.h"

#include "NeuralAmpModelerControls.h"

// Cross-platform debug output
#ifdef _WIN32
#define NAM_DBGMSG(msg) OutputDebugStringA(msg)
#else
#define NAM_DBGMSG(msg) fprintf(stderr, "%s", (msg))
#endif

using namespace iplug;
using namespace igraphics;

const double kDCBlockerFrequency = 5.0;

// Styles
const IVColorSpec colorSpec{
  DEFAULT_BGCOLOR, // Background
  PluginColors::NAM_THEMECOLOR, // Foreground
  PluginColors::NAM_THEMECOLOR.WithOpacity(0.3f), // Pressed
  PluginColors::NAM_THEMECOLOR.WithOpacity(0.4f), // Frame
  PluginColors::MOUSEOVER, // Highlight
  DEFAULT_SHCOLOR, // Shadow
  PluginColors::NAM_THEMECOLOR, // Extra 1
  COLOR_RED, // Extra 2 --> color for clipping in meters
  PluginColors::NAM_THEMECOLOR.WithContrast(0.1f), // Extra 3
};

const IVStyle style =
  IVStyle{true, // Show label
          true, // Show value
          colorSpec,
          {DEFAULT_TEXT_SIZE + 3.f, EVAlign::Middle, PluginColors::NAM_THEMEFONTCOLOR}, // Knob label text5
          {DEFAULT_TEXT_SIZE + 3.f, EVAlign::Bottom, PluginColors::NAM_THEMEFONTCOLOR}, // Knob value text
          DEFAULT_HIDE_CURSOR,
          DEFAULT_DRAW_FRAME,
          false,
          DEFAULT_EMBOSS,
          0.2f,
          2.f,
          DEFAULT_SHADOW_OFFSET,
          DEFAULT_WIDGET_FRAC,
          DEFAULT_WIDGET_ANGLE};
const IVStyle titleStyle =
  DEFAULT_STYLE.WithValueText(IText(30, COLOR_WHITE, "Michroma-Regular")).WithDrawFrame(false).WithShadowOffset(2.f);
const IVStyle radioButtonStyle =
  style
    .WithColor(EVColor::kON, PluginColors::NAM_THEMECOLOR) // Pressed buttons and their labels
    .WithColor(EVColor::kOFF, PluginColors::NAM_THEMECOLOR.WithOpacity(0.1f)) // Unpressed buttons
    .WithColor(EVColor::kX1, PluginColors::NAM_THEMECOLOR.WithOpacity(0.6f)); // Unpressed buttons' labels

EMsgBoxResult _ShowMessageBox(iplug::igraphics::IGraphics* pGraphics, const char* str, const char* caption,
                              EMsgBoxType type)
{
#ifdef OS_MAC
  // macOS is backwards?
  return pGraphics->ShowMessageBox(caption, str, type);
#else
  return pGraphics->ShowMessageBox(str, caption, type);
#endif
}

const std::string kCalibrateInputParamName = "CalibrateInput";
const bool kDefaultCalibrateInput = false;
const std::string kInputCalibrationLevelParamName = "InputCalibrationLevel";
const double kDefaultInputCalibrationLevel = 12.0;


NeuralAmpModeler::NeuralAmpModeler(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  _InitToneStack();
  nam::activations::Activation::enable_fast_tanh();
  GetParam(kInputLevel)->InitGain("Input", 0.0, -20.0, 20.0, 0.1);
  GetParam(kToneBass)->InitDouble("Bass", 5.0, 0.0, 10.0, 0.1);
  GetParam(kToneMid)->InitDouble("Middle", 5.0, 0.0, 10.0, 0.1);
  GetParam(kToneTreble)->InitDouble("Treble", 5.0, 0.0, 10.0, 0.1);
  GetParam(kOutputLevel)->InitGain("Output", 0.0, -40.0, 40.0, 0.1);
  GetParam(kNoiseGateThreshold)->InitGain("Threshold", -80.0, -100.0, 0.0, 0.1);
  GetParam(kNoiseGateActive)->InitBool("NoiseGateActive", true);
  GetParam(kEQActive)->InitBool("ToneStack", true);
  GetParam(kOutputMode)->InitEnum("OutputMode", 1, {"Raw", "Normalized", "Calibrated"}); // TODO DRY w/ control
  GetParam(kCabToggle)->InitBool("CabToggle", true);
  GetParam(kCalibrateInput)->InitBool(kCalibrateInputParamName.c_str(), kDefaultCalibrateInput);
  GetParam(kInputCalibrationLevel)
    ->InitDouble(kInputCalibrationLevelParamName.c_str(), kDefaultInputCalibrationLevel, -60.0, 60.0, 0.1, "dBu");
  GetParam(kSlim)->InitDouble("Slim", 0.0, 0.0, 1.0, 0.01);
  GetParam(kAmpGain)->InitDouble("Voice", 0.1, 0.0, 10.0, 0.01, "", IParam::kFlagsNone, "AmpGain", IParam::ShapePowCurve(1.0));

  mNoiseGateTrigger.AddListener(&mNoiseGateGain);
  mNoiseGateTrigger.AddListener(&mNoiseGateGainRight);

  mMakeGraphicsFunc = [&]() {

#ifdef OS_IOS
    auto scaleFactor = GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT) * 0.85f;
#else
    auto scaleFactor = 1.0f;
#endif

    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, scaleFactor);
  };

  mLayoutFunc = [&](IGraphics* pGraphics) {
    pGraphics->AttachCornerResizer(EUIResizerMode::Scale, false);
    pGraphics->AttachTextEntryControl();
    pGraphics->EnableMouseOver(true);
    pGraphics->EnableTooltips(true);
    pGraphics->EnableMultiTouch(true);

    pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);
    pGraphics->LoadFont("Michroma-Regular", MICHROMA_FN);

    const auto gearSVG = pGraphics->LoadSVG(GEAR_FN);
    const auto fileSVG = pGraphics->LoadSVG(FILE_FN);
    const auto globeSVG = pGraphics->LoadSVG(GLOBE_ICON_FN);
    const auto crossSVG = pGraphics->LoadSVG(CLOSE_BUTTON_FN);
    const auto rightArrowSVG = pGraphics->LoadSVG(RIGHT_ARROW_FN);
    const auto leftArrowSVG = pGraphics->LoadSVG(LEFT_ARROW_FN);
    const auto modelIconSVG = pGraphics->LoadSVG(MODEL_ICON_FN);
    const auto cabIconOnSVG = pGraphics->LoadSVG(IR_ICON_ON_FN);
    const auto cabIconOffSVG = pGraphics->LoadSVG(IR_ICON_OFF_FN);
    const auto slimIconSVG = pGraphics->LoadSVG(SLIMMABLE_ICON_FN);
    const auto libraryIconSVG = pGraphics->LoadSVG(LIBRARY_ICON_FN);  

    const auto backgroundBitmap = pGraphics->LoadBitmap(BACKGROUND_FN);
    const auto fileBackgroundBitmap = pGraphics->LoadBitmap(FILEBACKGROUND_FN);
    const auto inputLevelBackgroundBitmap = pGraphics->LoadBitmap(INPUTLEVELBACKGROUND_FN);
    const auto linesBitmap = pGraphics->LoadBitmap(LINES_FN);
    const auto knobBackgroundBitmap = pGraphics->LoadBitmap(KNOBBACKGROUND_FN);
    const auto switchHandleBitmap = pGraphics->LoadBitmap(SLIDESWITCHHANDLE_FN);
    const auto meterBackgroundBitmap = pGraphics->LoadBitmap(METERBACKGROUND_FN);

    const auto b = pGraphics->GetBounds();
    const auto mainArea = b.GetPadded(-20);
    const auto contentArea = mainArea.GetPadded(-10);
    const auto titleHeight = 50.0f;
    const auto titleArea = contentArea.GetFromTop(titleHeight);

    // Areas for knobs — 7-column grid, slightly taller row to avoid crowding
    const auto knobsPad = 20.0f;
    const auto knobsExtraSpaceBelowTitle = 25.0f;
    const auto singleKnobPad = 2.0f;   // ~8% smaller than before (was -2.0f → GetPadded(2.0f))
    const auto knobsArea = contentArea.GetFromTop(NAM_KNOB_HEIGHT)  // slightly taller row
                             .GetReducedFromLeft(knobsPad)
                             .GetReducedFromRight(knobsPad)
                             .GetVShifted(titleHeight + knobsExtraSpaceBelowTitle);

    const int kKnobCols = 7;
    const auto inputKnobArea   = knobsArea.GetGridCell(0, 0, 1, kKnobCols).GetPadded(singleKnobPad);
    const auto noiseGateArea   = knobsArea.GetGridCell(0, 1, 1, kKnobCols).GetPadded(singleKnobPad);
    const auto bassKnobArea    = knobsArea.GetGridCell(0, 2, 1, kKnobCols).GetPadded(singleKnobPad);
    const auto midKnobArea     = knobsArea.GetGridCell(0, 3, 1, kKnobCols).GetPadded(singleKnobPad);
    const auto trebleKnobArea  = knobsArea.GetGridCell(0, 4, 1, kKnobCols).GetPadded(singleKnobPad);
    const auto outputKnobArea  = knobsArea.GetGridCell(0, 5, 1, kKnobCols).GetPadded(singleKnobPad);
    const auto ampGainKnobArea = knobsArea.GetGridCell(0, 6, 1, kKnobCols).GetPadded(singleKnobPad);

    const auto ngToggleArea =
      noiseGateArea.GetVShifted(noiseGateArea.H()).SubRectVertical(2, 0).GetReducedFromTop(10.0f);
    const auto eqToggleArea =
      midKnobArea.GetVShifted(midKnobArea.H()).SubRectVertical(2, 0).GetReducedFromTop(10.0f);

    // Areas for model and cab
    const auto fileWidth = 200.0f;
    const auto fileHeight = 30.0f;
    const auto cabYOffset = 38.0f;
    const auto modelArea =
      contentArea.GetFromBottom((2.0f * fileHeight)).GetFromTop(fileHeight).GetMidHPadded(fileWidth).GetVShifted(-1);
    const auto slimIconArea =
      IRECT(modelArea.R + 2.f, modelArea.MH() - 10.f, modelArea.R + 4.f + 2.f * 22.f, modelArea.MH() + 10.f);
    const auto modelIconArea = modelArea.GetFromLeft(30).GetTranslated(-40, 10);
    const auto pnamEditIconArea = IRECT(slimIconArea.R + 2.f, modelArea.T + 4.f,
                                        slimIconArea.R + 22.f, modelArea.B - 4.f);
    const auto cabArea = modelArea.GetVShifted(cabYOffset);
    const auto cabSwitchArea = cabArea.GetFromLeft(30.0f).GetHShifted(-40.0f).GetScaledAboutCentre(0.6f);

    // Areas for meters
    const auto inputMeterArea = contentArea.GetFromLeft(30).GetHShifted(-20).GetMidVPadded(100).GetVShifted(-25);
    const auto outputMeterArea = contentArea.GetFromRight(30).GetHShifted(20).GetMidVPadded(100).GetVShifted(-25);

    // Misc Areas
    const auto settingsButtonArea = CornerButtonArea(b);

    // Model loader button
    auto loadModelCompletionHandler = [&](const WDL_String& fileName, const WDL_String& path) {
      if (fileName.GetLength())
      {
        auto extension = std::filesystem::u8path(fileName.Get()).extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".pnam")
        {
          _LoadPNAMFile(fileName.Get());
        }
        else
        {
          const std::string msg = _StageModel(fileName);
          if (msg.size())
          {
            std::stringstream ss;
            ss << "Failed to load NAM model. Message:\n\n" << msg;
            _ShowMessageBox(GetUI(), ss.str().c_str(), "Failed to load model!", kMB_OK);
          }
          std::cout << "Loaded: " << fileName.Get() << std::endl;
        }
      }
    };

    // Cab loader button
    auto loadCabCompletionHandler = [&](const WDL_String& fileName, const WDL_String& path) {
      if (fileName.GetLength())
      {
        const std::string error = _StageCab(fileName);
        if (!error.empty())
        {
          std::stringstream message;
          message << "Failed to load cab file " << fileName.Get() << ":\n" << error;
          _ShowMessageBox(GetUI(), message.str().c_str(), "Failed to load cab!", kMB_OK);
        }
      }
    };

    pGraphics->AttachBackground(BACKGROUND_FN);
    pGraphics->AttachControl(new WithFileDrop<IBitmapControl>(b, linesBitmap));
    pGraphics->AttachControl(new WithFileDrop<IVLabelControl>(titleArea, "NEURAL AMP MODELER", titleStyle));
    //pGraphics->AttachControl(new WithFileDrop<ISVGControl>(modelIconArea, modelIconSVG));

    // PNAM chain editor icon — right side, symmetric to model icon
    pGraphics->AttachControl(new NAMTextCircleButtonControl(
      pnamEditIconArea,
      [](IControl* pCaller) {
        auto* pPlugin = dynamic_cast<NeuralAmpModeler*>(pCaller->GetDelegate());
        if (pPlugin) pPlugin->OpenPNAMEditorWindow();
      }),
      kCtrlTagPNAMEditorBtn);
    pGraphics->GetControlWithTag(kCtrlTagPNAMEditorBtn)->SetDisabled(true);
    pGraphics->GetControlWithTag(kCtrlTagPNAMEditorBtn)->SetTooltip("Open PNAM Chain Editor");

#ifdef NAM_PICK_DIRECTORY
    const std::string defaultNamFileString = "Select model directory...";
    const std::string defaultCabString = "Select cab directory...";
#else
    const std::string defaultNamFileString = "Select model...";
    const std::string defaultCabString = "Select cab...";
#endif
    // Getting started page listing additional resources
    const char* const getUrl = "https://www.neuralampmodeler.com/users#comp-marb84o5";
    /* pGraphics->AttachControl(
      new NAMFileBrowserControl(modelArea, kMsgTagClearModel, defaultNamFileString.c_str(), "nam pnam", "nam",
                                loadModelCompletionHandler, style, fileSVG, crossSVG, leftArrowSVG, rightArrowSVG,
                                libraryIconSVG, fileBackgroundBitmap, globeSVG, "Get NAM Models", getUrl),
      kCtrlTagModelFileBrowser);*/

    auto hideSlimOverlay = [](IControl* pCaller) {
      IGraphics* ui = pCaller->GetUI();
      if (auto* backdrop = ui->GetControlWithTag(kCtrlTagSlimOverlayBackdrop))
        backdrop->Hide(true);
      if (auto* knob = ui->GetControlWithTag(kCtrlTagSlimKnob))
        knob->Hide(true);
      ui->SetAllControlsDirty();
    };
    auto showSlimOverlay = [](IControl* pCaller) {
      IGraphics* ui = pCaller->GetUI();
      if (auto* backdrop = ui->GetControlWithTag(kCtrlTagSlimOverlayBackdrop))
        backdrop->Hide(false);
      if (auto* knob = ui->GetControlWithTag(kCtrlTagSlimKnob))
        knob->Hide(false);
      ui->SetAllControlsDirty();
    };

    pGraphics
      ->AttachControl(
        new NAMSquareButtonControl(slimIconArea, DefaultClickActionFunc, slimIconSVG), kCtrlTagSlimmableIcon)
      ->SetAnimationEndActionFunction(showSlimOverlay)
      ->Hide(true);

    pGraphics->AttachControl(new ISVGSwitchControl(cabSwitchArea, {cabIconOffSVG, cabIconOnSVG}, kCabToggle));
    pGraphics->AttachControl(new NAMFileBrowserControl(modelArea, kMsgTagClearModel, defaultNamFileString.c_str(),
                                                       "nam pnam", "nam",   // dialog filter, scan extension
                                                       loadModelCompletionHandler, style, fileSVG, crossSVG,
                                                       leftArrowSVG, rightArrowSVG, libraryIconSVG, fileBackgroundBitmap, globeSVG, "Get NAM Models", getUrl),
                             kCtrlTagModelFileBrowser);

    pGraphics->AttachControl(
      new NAMFileBrowserControl(cabArea, kMsgTagClearCab, defaultCabString.c_str(), "wav nam", "wav nam", loadCabCompletionHandler,
                                style, fileSVG, crossSVG, leftArrowSVG, rightArrowSVG, fileSVG, fileBackgroundBitmap, globeSVG,
                                "Get cabs", getUrl),
      kCtrlTagCabFileBrowser);
    pGraphics->AttachControl(
      new NAMSwitchControl(ngToggleArea, kNoiseGateActive, "Noise Gate", style, switchHandleBitmap));
    pGraphics->AttachControl(new NAMSwitchControl(eqToggleArea, kEQActive, "EQ", style, switchHandleBitmap));

    // The knobs
    pGraphics->AttachControl(new NAMKnobControl(inputKnobArea, kInputLevel, "", style, knobBackgroundBitmap));
    pGraphics->AttachControl(new NAMKnobControl(noiseGateArea, kNoiseGateThreshold, "", style, knobBackgroundBitmap));
    pGraphics->AttachControl(
      new NAMKnobControl(bassKnobArea, kToneBass, "", style, knobBackgroundBitmap), -1, "EQ_KNOBS");
    pGraphics->AttachControl(
      new NAMKnobControl(midKnobArea, kToneMid, "", style, knobBackgroundBitmap), -1, "EQ_KNOBS");
    pGraphics->AttachControl(
      new NAMKnobControl(trebleKnobArea, kToneTreble, "", style, knobBackgroundBitmap), -1, "EQ_KNOBS");
    pGraphics->AttachControl(new NAMKnobControl(outputKnobArea, kOutputLevel, "", style, knobBackgroundBitmap));

    // Amp Gain knob — always visible, disabled until .pnam is loaded
    pGraphics->AttachControl(
      new NAMKnobControl(ampGainKnobArea, kAmpGain, "", style, knobBackgroundBitmap), kCtrlTagAmpGain);
    pGraphics->GetControlWithTag(kCtrlTagAmpGain)->SetDisabled(true);

    // The meters
    pGraphics->AttachControl(new NAMMeterControl<1>(inputMeterArea, meterBackgroundBitmap, style), kCtrlTagInputMeter);
    pGraphics->AttachControl(
      new NAMMeterControl<2>(outputMeterArea, meterBackgroundBitmap, style), kCtrlTagOutputMeter);

    // Settings/help/about box
    pGraphics->AttachControl(new NAMCircleButtonControl(
      settingsButtonArea,
      [pGraphics](IControl* pCaller) {
        pGraphics->GetControlWithTag(kCtrlTagSettingsBox)->As<NAMSettingsPageControl>()->HideAnimated(false);
      },
      gearSVG));

    // Recall back / forward buttons — placed where the model icon was,
    // i.e. the 40px slot directly to the left of the model file browser.
    {
      const float recallL = modelArea.L - 40.f;
      const float recallR = modelArea.L - 2.f;
      const float recallT = modelArea.T + 7.f;
      const float recallB = modelArea.B - 7.f;
      const float btnW = (recallR - recallL - 2.f) * 0.5f; // ~19px each, 2px gap

      const IRECT backRect(recallL, recallT, recallL + btnW, recallB);
      const IRECT fwdRect(recallR - btnW, recallT, recallR, recallB);

      pGraphics->AttachControl(
        new NAMRecallButtonControl(
          backRect, [](IControl* pCaller) { static_cast<NeuralAmpModeler*>(pCaller->GetDelegate())->RecallBack(); },
          "<<"),
        kCtrlTagRecallBack);

      pGraphics->AttachControl(
        new NAMRecallButtonControl(
          fwdRect, [](IControl* pCaller) { static_cast<NeuralAmpModeler*>(pCaller->GetDelegate())->RecallForward(); },
          ">>"),
        kCtrlTagRecallForward);

      pGraphics->GetControlWithTag(kCtrlTagRecallBack)->SetDisabled(true);
      pGraphics->GetControlWithTag(kCtrlTagRecallForward)->SetDisabled(true);
      pGraphics->GetControlWithTag(kCtrlTagRecallBack)->SetTooltip("Recall previous configuration");
      pGraphics->GetControlWithTag(kCtrlTagRecallForward)->SetTooltip("Recall next configuration");
    }


    pGraphics
      ->AttachControl(new NAMSettingsPageControl(b, backgroundBitmap, inputLevelBackgroundBitmap, switchHandleBitmap,
                                                 crossSVG, style, radioButtonStyle),
                      kCtrlTagSettingsBox)
      ->Hide(true);

    const auto slimKnobArea = b.GetCentredInside(100.f, NAM_KNOB_HEIGHT + 24.f);
    pGraphics->AttachControl(new NAMSlimOverlayBackdropControl(b, hideSlimOverlay), kCtrlTagSlimOverlayBackdrop)
      ->Hide(true);
    pGraphics
      ->AttachControl(new NAMKnobControl(slimKnobArea, kSlim, "Slim", style, knobBackgroundBitmap), kCtrlTagSlimKnob)
      ->Hide(true);

    

    pGraphics->ForAllControlsFunc([](IControl* pControl) {
      pControl->SetMouseEventsWhenDisabled(true);
      pControl->SetMouseOverWhenDisabled(true);
    });
  };
}

NeuralAmpModeler::~NeuralAmpModeler()
{
  // Join the preload worker before its completion callback loses access to this instance.
  mModelMapper.ClearSlots();
  // Clean up library browser window
  if (mLibraryBrowserWindow)
  {
    mLibraryBrowserWindow->GetCurrentUIState(
      mLibraryBrowserSearchQuery, mLibraryBrowserSelectedTag, mLibraryBrowserExpandedState);
    mLibraryBrowserWindow->Close();
    mLibraryBrowserWindow.reset();
  }
}

void NeuralAmpModeler::OnUIClose()
{
  // Close library browser window when plugin UI closes
  if (mLibraryBrowserWindow)
  {
    mLibraryBrowserWindow = std::make_unique<NAMLibraryBrowserWindow>(&mLibraryManager, mLibraryRootNode);

    mLibraryBrowserWindow->SetInitialUIState(
      mLibraryBrowserSearchQuery, mLibraryBrowserSelectedTag, mLibraryBrowserExpandedState);
    mLibraryBrowserWindow->Close();
  }

   Plugin::OnUIClose();
}


void NeuralAmpModeler::OpenLibraryBrowserWindow()
{
  // If a browser window is already open, don't create another.
  if (mLibraryBrowserWindow && mLibraryBrowserWindow->IsOpen())
  {
    mLibraryBrowserWindow->BringToFront();
    return;
  }

  // Refresh metadata (fast if unchanged; reloads only if data.json changed).
  if (!InitializeLibraryManager() || !mLibraryRootNode)
  {
    const std::string jsonPath = GetNAMLibraryDataJsonPath();

    std::stringstream ss;
    ss << "The NAM Library data file could not be loaded.\n\n";
    if (!jsonPath.empty())
      ss << "Expected location:\n    " << jsonPath << "\n\n";
    ss << "To use the Library Browser, please install the NAM Model Manager "
          "and ensure it has created its library data at the location above.";

    NAM_DBGMSG(ss.str().c_str());
    _ShowMessageBox(GetUI(), ss.str().c_str(), "NAM Library Not Found", kMB_OK);
    return;
  }

  // Recreate the window object so it uses the latest root node.
  mLibraryBrowserWindow.reset();
  mLibraryBrowserWindow = std::make_unique<NAMLibraryBrowserWindow>(&mLibraryManager, mLibraryRootNode);

  mLibraryBrowserWindow->SetInitialUIState(
    mLibraryBrowserSearchQuery, mLibraryBrowserSelectedTag, mLibraryBrowserExpandedState);
  mLibraryBrowserWindow->SetOnWindowClosed([this]() {
    if (mLibraryBrowserWindow)
    {
      mLibraryBrowserWindow->GetCurrentUIState(
        mLibraryBrowserSearchQuery, mLibraryBrowserSelectedTag, mLibraryBrowserExpandedState);
    }
  });

  mLibraryBrowserWindow->SetOnModelSelected([this](const std::shared_ptr<NAMLibraryTreeNode>& node) {
    if (!node || !node->IsModel())
      return;

    // Route .pnam files through the chain loader
    if (node->path.size() > 5 && node->path.substr(node->path.size() - 5) == ".pnam")
    {
      _LoadPNAMFile(node->path);
      return;
    }

    WDL_String modelPath(node->path.c_str());

    const std::string msg = _StageModel(modelPath);

    if (msg.size())
    {
      std::stringstream ss;
      ss << "Failed to load NAM model. Message:\n\n" << msg;
      _ShowMessageBox(GetUI(), ss.str().c_str(), "Failed to load model!", kMB_OK);
    }
    else
    {
      std::cout << "Loaded from library: " << node->path << std::endl;
    }
  });

  void* pParentWindow = GetUI() ? GetUI()->GetWindow() : nullptr;
  mLibraryBrowserWindow->Open(pParentWindow);
}

void NeuralAmpModeler::OpenPNAMEditorWindow()
{
  if (mPNAMEditorWindow && mPNAMEditorWindow->IsOpen())
  {
    mPNAMEditorWindow->BringToFront();
    return;
  }

  mPNAMEditorWindow = std::make_unique<NAMPNAMEditorWindow>();

  // Pre-load the currently active .pnam file if one is loaded
  if (mPNAMPath.GetLength())
    mPNAMEditorWindow->LoadFile(mPNAMPath.Get());

  mPNAMEditorWindow->SetOnSaved([this](const std::string& savedPath) {
    _LoadPNAMFile(savedPath);
  });

  // Preview a single slot: load just that NAM and apply its overrides to the knobs
  mPNAMEditorWindow->SetOnPreviewSlot([this](const ModelMapSlot& slot) {
    WDL_String namPath(slot.namFilePath.c_str());
    const auto error = _StageModel(namPath);
    if (!error.empty())
    {
      _ShowMessageBox(GetUI(), error.c_str(), "Failed to preview model", kMB_OK);
      return;
    }

    // Apply optional overrides to plugin parameters so the knobs reflect them
    if (slot.overrides.outputLevel.has_value())
      SetParameterValue(kOutputLevel, *slot.overrides.outputLevel);
    if (slot.overrides.toneBass.has_value())
      SetParameterValue(kToneBass, *slot.overrides.toneBass);
    if (slot.overrides.toneMid.has_value())
      SetParameterValue(kToneMid, *slot.overrides.toneMid);
    if (slot.overrides.toneTreble.has_value())
      SetParameterValue(kToneTreble, *slot.overrides.toneTreble);

    // Deactivate the chain mapper so the single model is heard directly
    mModelMapper.SetActive(false);
    // Clear boundary ticks since no pnam chain is active
    if (auto* pGraphics = GetUI())
      if (auto* pKnob = pGraphics->GetControlWithTag(kCtrlTagAmpGain))
        static_cast<NAMKnobControl*>(pKnob)->SetPNAMBoundaries({});
    SendParameterValueFromDelegate(kOutputLevel, GetParam(kOutputLevel)->Value(), true);
    SendParameterValueFromDelegate(kToneBass,    GetParam(kToneBass)->Value(),    true);
    SendParameterValueFromDelegate(kToneMid,     GetParam(kToneMid)->Value(),     true);
    SendParameterValueFromDelegate(kToneTreble,  GetParam(kToneTreble)->Value(),  true);
   
  });

  // Preview whole chain: reload from the saved file
  mPNAMEditorWindow->SetOnPreviewChain([this](const std::string& filePath) {
    _LoadPNAMFile(filePath);
  });

  mPNAMEditorWindow->mOnWindowClosed = []() {};

  void* pParent = GetUI() ? GetUI()->GetWindow() : nullptr;
  mPNAMEditorWindow->Open(pParent);
}

void NeuralAmpModeler::ProcessBlock(iplug::sample** inputs, iplug::sample** outputs, int nFrames)
{
  const size_t numChannelsExternalIn = (size_t)NInChansConnected();
  const size_t numChannelsExternalOut = (size_t)NOutChansConnected();
  _ApplyDSPStaging();

  // The host buses have at most two channels. Offset each channel within the host block.
  std::array<sample*, kMaxNumChannelsInternal> chunkInputs{}, chunkOutputs{};
  for (int offset = 0; offset < nFrames;)
  {
    const int chunkFrames = std::min(nFrames - offset, kMaxInternalBlockSize);
    for (size_t ch = 0; ch < numChannelsExternalIn; ++ch)
      chunkInputs[ch] = inputs[ch] + offset;
    for (size_t ch = 0; ch < numChannelsExternalOut; ++ch)
      chunkOutputs[ch] = outputs[ch] + offset;

    _ProcessBlock(chunkInputs.data(), chunkOutputs.data(), chunkFrames, numChannelsExternalIn, numChannelsExternalOut);
    offset += chunkFrames;
  }
}

void NeuralAmpModeler::_ProcessBlock(iplug::sample** inputs, iplug::sample** outputs, int nFrames,
                                     size_t numChannelsExternalIn, size_t numChannelsExternalOut)
{
  const size_t numModelOutputs = mModel != nullptr ? mModel->NumOutputChannels() : 1;
  const size_t numFrames = (size_t)nFrames;
  const double sampleRate = GetSampleRate();

  // Disable floating point denormals
  std::fenv_t fe_state;
  std::feholdexcept(&fe_state);
  disable_denormals();

  _ProcessInput(inputs, numFrames, numChannelsExternalIn);
  sample* modelInput[]{mInputBuffer.data()};
  sample* modelOutput[]{mModelOutput[0].data(), mModelOutput[1].data()};
  const bool noiseGateActive = GetParam(kNoiseGateActive)->Value();
  const bool toneStackActive = GetParam(kEQActive)->Value();

  // Noise gate trigger
  sample** triggerOutput = modelInput;
  if (noiseGateActive)
  {
    const double time = 0.01;
    const double threshold = GetParam(kNoiseGateThreshold)->Value(); // GetParam...
    const double ratio = 0.1; // Quadratic...
    const double openTime = 0.005;
    const double holdTime = 0.01;
    const double closeTime = 0.05;
    const dsp::noise_gate::TriggerParams triggerParams(time, threshold, ratio, openTime, holdTime, closeTime);
    mNoiseGateTrigger.SetParams(triggerParams);
    mNoiseGateTrigger.SetSampleRate(sampleRate);
    triggerOutput = mNoiseGateTrigger.Process(modelInput, 1, numFrames);
  }

  if (mModel != nullptr)
  {
    mModel->process(triggerOutput, modelOutput, nFrames);
  }
  else
  {
    _FallbackDSP(triggerOutput, modelOutput, 1, numFrames); // Fallback is mono-mono
  }
  // Apply the same input-triggered gate envelope to both amp output channels.
  sample* gateGainPointers[]{modelOutput[0], modelOutput[1]};
  if (noiseGateActive)
  {
    gateGainPointers[0] = mNoiseGateGain.Process(modelOutput, 1, numFrames)[0];
    if (numModelOutputs == 2)
      gateGainPointers[1] = mNoiseGateGainRight.Process(modelOutput + 1, 1, numFrames)[0];
  }

  sample** toneStackOutPointers = (toneStackActive && mToneStack != nullptr)
                                    ? mToneStack->Process(gateGainPointers, numModelOutputs, nFrames)
                                    : gateGainPointers;

  size_t numOutputChannels = numModelOutputs;
  sample** cabPointers = toneStackOutPointers;
  sample* cabinetOutputPointers[]{mCabinetOutput[0].data(), mCabinetOutput[1].data()};
  if (mCab.model != nullptr && mCabEnabled)
  {
    if (mCab.model->NumOutputChannels() == 2)
    {
      sample* cabInputPointers[]{toneStackOutPointers[0]};
      if (numModelOutputs == 2)
      {
        for (size_t i = 0; i < numFrames; ++i)
          mCabinetInput[i] = 0.5 * (toneStackOutPointers[0][i] + toneStackOutPointers[1][i]);
        cabInputPointers[0] = mCabinetInput.data();
      }
      mCab.model->process(cabInputPointers, cabinetOutputPointers, nFrames);
      numOutputChannels = 2;
    }
    else
    {
      // Mono cabs process stereo amp outputs independently, preserving L/R.
      mCab.model->process(toneStackOutPointers, cabinetOutputPointers, nFrames);
      if (numModelOutputs == 2)
        mCab.right->process(toneStackOutPointers + 1, cabinetOutputPointers + 1, nFrames);
    }
    cabPointers = cabinetOutputPointers;
  }

  // And the HPF for DC offset (Issue 271)
  const double highPassCutoffFreq = kDCBlockerFrequency;
  const recursive_linear_filter::HighPassParams highPassParams(sampleRate, highPassCutoffFreq);
  mHighPass.SetParams(highPassParams);
  sample** hpfPointers = mHighPass.Process(cabPointers, numOutputChannels, numFrames);

  // Smooth fade for mapper model switches — applied per-sample to avoid pop
  if (mOutputFadeDir != 0 || mOutputFadeGain < 1.0f)
  {
    const float step = 1.0f / static_cast<float>(kModelFadeSamples);
    for (size_t s = 0; s < numFrames; s++)
    {
      if (mOutputFadeDir == -1)
      {
        mOutputFadeGain = std::max(0.0f, mOutputFadeGain - step);
      }
      else if (mOutputFadeDir == 1)
      {
        mOutputFadeGain = std::min(1.0f, mOutputFadeGain + step);
        if (mOutputFadeGain >= 1.0f)
          mOutputFadeDir = 0;
      }

      for (size_t c = 0, nc = numOutputChannels; c < nc; c++)
        hpfPointers[c][s] *= mOutputFadeGain;
    }
  }
  
  // restore previous floating point state
  std::feupdateenv(&fe_state);

  // Let's get outta here
  // Adapt the internal mono/stereo result to the host output bus.
  _ProcessOutput(hpfPointers, outputs, numFrames, numOutputChannels, numChannelsExternalOut);
  _UpdateMeters(modelInput, outputs, numFrames, numChannelsExternalOut);
}

void NeuralAmpModeler::OnReset()
{
  const auto sampleRate = GetSampleRate();

  mInputSender.Reset(sampleRate);
  mOutputSender.Reset(sampleRate);
  // If there is a model or cab loaded, they need to be checked for resampling.
  _ResetModelAndCab(sampleRate);
  mToneStack->Reset(sampleRate, kMaxInternalBlockSize);
  mCabEnabled = GetParam(kCabToggle)->Bool();
  _UpdateLatencyAndTail();
}

void NeuralAmpModeler::OnIdle()
{
  mInputSender.TransmitData(*this);
  mOutputSender.TransmitData(*this);
  _ApplySlimParamToLoadedNAMs();

  if (mNewModelLoadedInDSP)
  {
    if (auto* pGraphics = GetUI())
    {
      (void)pGraphics;
      _UpdateControlsFromModel();
      mNewModelLoadedInDSP = false;
    }
  }
  if (mModelCleared)
  {
    if (auto* pGraphics = GetUI())
    {
      static_cast<NAMSettingsPageControl*>(pGraphics->GetControlWithTag(kCtrlTagSettingsBox))->ClearModelInfo();

      // Disable Voice knob and PNAM editor button, reset knob counter
      if (auto* pKnob = static_cast<NAMKnobControl*>(pGraphics->GetControlWithTag(kCtrlTagAmpGain)))
      {
        pKnob->SetDisabled(true);
        pKnob->SetSlotInfo(-1, 0);
        pKnob->SetPNAMBoundaries({});
        pKnob->SetTooltip("");
        pKnob->SetDirty(false);
      }
      if (auto* pBtn = pGraphics->GetControlWithTag(kCtrlTagPNAMEditorBtn))
      {
        pBtn->SetDisabled(true);
        pBtn->SetDirty(false);
      }

      mModelCleared = false;
    }
  }

  // Activate the model mapper once background preloading has finished.
  if (mPNAMLoadPending.load())
  {
    mPNAMLoadPending.store(false);

    if (!mPNAMPath.GetLength())
      return; // The chain was cleared while loading.
    if (mPNAMLoadedSlots.load() == 0)
    {
      SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadFailed);
      _ShowMessageBox(GetUI(), "No slot contains a compatible model.", "Failed to load chain", kMB_OK);
      mPNAMPath.Set("");
      return;
    }
    {
      const std::lock_guard lock(mDSPStagingMutex);
      mModelMapper.PrepareModels(mDSPStagingSampleRate.load(), kMaxInternalBlockSize);
      mModelMapper.SetActive(true);
    }

    // Show Amp Gain knob (Input knob stays visible)
    if (auto* pGraphics = GetUI())
    {
      if (auto* pCtrl = pGraphics->GetControlWithTag(kCtrlTagAmpGain))
        pCtrl->SetDisabled(false);
      if (auto* pCtrl = pGraphics->GetControlWithTag(kCtrlTagPNAMEditorBtn))
        pCtrl->SetDisabled(false);

      // Push slot-boundary tick positions to the Amp Gain knob
      const auto& slots = mModelMapper.GetSlots();
      std::vector<float> boundaries;
      boundaries.reserve(slots.size() * 2);
      for (const auto& slot : slots)
      {
        // Collect interior boundaries only (skip 0.0 and 10.0 endpoints)
        if (slot.ampGainMin > 0.0)
          boundaries.push_back(static_cast<float>(slot.ampGainMin / 10.0));
        if (slot.ampGainMax < 10.0)
          boundaries.push_back(static_cast<float>(slot.ampGainMax / 10.0));
      }
      // Deduplicate (adjacent slots share a boundary value)
      std::sort(boundaries.begin(), boundaries.end());
      boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());

      if (auto* pKnob = pGraphics->GetControlWithTag(kCtrlTagAmpGain))
        static_cast<NAMKnobControl*>(pKnob)->SetPNAMBoundaries(std::move(boundaries));
    }

    WDL_String displayStr(mPNAMPendingDisplayName.c_str());
    SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadedModel,
                               displayStr.GetLength(), displayStr.Get());

    // Use Amp Gain param for initial slot selection
    const double ampGain = GetParam(kAmpGain)->Value();
    auto initialModel = mModelMapper.EvaluateInputGain(ampGain);
    if (initialModel)
    {
      {
        const std::lock_guard lock(mDSPStagingMutex);
        mMapperRequestedModel.reset();
        mStagedModel = initialModel;
        mSlimDirty = true;
      }

      auto overrides = mModelMapper.GetActiveOverrides();
      if (overrides.has_value())
      {
        if (overrides->outputLevel.has_value()) GetParam(kOutputLevel)->Set(*overrides->outputLevel);
        if (overrides->toneBass.has_value())    GetParam(kToneBass)->Set(*overrides->toneBass);
        if (overrides->toneMid.has_value())     GetParam(kToneMid)->Set(*overrides->toneMid);
        if (overrides->toneTreble.has_value())  GetParam(kToneTreble)->Set(*overrides->toneTreble);

        SendParameterValueFromDelegate(kOutputLevel, GetParam(kOutputLevel)->GetNormalized(), true);
        SendParameterValueFromDelegate(kToneBass,    GetParam(kToneBass)->GetNormalized(),    true);
        SendParameterValueFromDelegate(kToneMid,     GetParam(kToneMid)->GetNormalized(),     true);
        SendParameterValueFromDelegate(kToneTreble,  GetParam(kToneTreble)->GetNormalized(),  true);
        mDSPGainsAndTimingDirty = true;
        mToneStackDirty = true;
      }
    }
  } 

  // --- Voice knob: slot counter (A) + tooltip + browser label (D) ---
  if (auto* pGraphics = GetUI())
  {
    auto* pKnob = static_cast<NAMKnobControl*>(pGraphics->GetControlWithTag(kCtrlTagAmpGain));
    if (!pKnob) return;

    if (mModelMapper.IsActive())
    {
      const int  slotIdx    = mModelMapper.GetActiveSlotIndex();
      const auto& slots     = mModelMapper.GetSlots();
      const int  totalSlots = static_cast<int>(slots.size());
      const bool hovered    = pKnob->IsMouseOver();
      const bool slotChanged = (slotIdx != mLastTooltipSlotIndex);

      // Build slot stem once for reuse
      std::string stem;
      if (slotIdx >= 0 && slotIdx < totalSlots)
        stem = std::filesystem::path(slots[slotIdx].namFilePath).stem().string();

      if (slotChanged)
      {
        mLastTooltipSlotIndex = slotIdx;

        // Tooltip
        const std::string tip = (!stem.empty())
          ? "Slot " + std::to_string(slotIdx + 1) + ": " + stem
          : "Open PNAM Chain Editor";
        //pKnob->SetTooltip(tip.c_str());

        // Knob counter
        pKnob->SetSlotInfo(slotIdx >= 0 ? slotIdx + 1 : -1, totalSlots);
        pKnob->SetDirty(false);
      }

      // Browser label: show slot name while rotating OR hovering, restore otherwise
      const bool shouldShowSlotName = (slotChanged || hovered) && !stem.empty();

      if (shouldShowSlotName && !mBrowserShowingSlotName)
      {
        const std::string label = "\u25B6 " + stem;
        WDL_String msg(label.c_str());
        SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagSetBrowserLabel,
                                   msg.GetLength() + 1, msg.Get());
        mBrowserShowingSlotName = true;
      }
      else if (shouldShowSlotName && slotChanged)
      {
        // Already showing — but slot changed while rotating, update to new name
        const std::string label = "\u25B6 " + stem;
        WDL_String msg(label.c_str());
        SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagSetBrowserLabel,
                                   msg.GetLength() + 1, msg.Get());
      }
      else if (!shouldShowSlotName && mBrowserShowingSlotName)
      {
        // Not rotating, not hovering — restore chain name
        SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagRestoreBrowserLabel, 1, "");
        mBrowserShowingSlotName = false;
      }
    }
    else
    {
      // PNAM deactivated
      if (mLastTooltipSlotIndex != -2)
      {
        mLastTooltipSlotIndex = -2;
        pKnob->SetTooltip("Open PNAM Chain Editor");
        pKnob->SetSlotInfo(-1, 0);
        pKnob->SetDirty(false);
      }
      if (mBrowserShowingSlotName)
      {
        SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagRestoreBrowserLabel, 1, "");
        mBrowserShowingSlotName = false;
      }
      mLastKnobHoverState = false;
    }
  }   


}    

bool NeuralAmpModeler::InitializeLibraryManager()
{
  const std::string jsonPath = GetNAMLibraryDataJsonPath();

  char debugMsg[512];

  if (jsonPath.empty())
  {
    NAM_DBGMSG("NAM Library: Failed to determine data.json path\n");
    return false;
  }

  // If the file doesn't exist, don't clobber any previously loaded metadata.
  if (!std::filesystem::exists(jsonPath))
  {
    snprintf(debugMsg, sizeof(debugMsg), "NAM Library: File does not exist: %s\n", jsonPath.c_str());
    NAM_DBGMSG(debugMsg);
    return false;
  }


  std::error_code ec;
  const auto writeTime = std::filesystem::last_write_time(jsonPath, ec);
  if (ec)
  {
    snprintf(debugMsg, sizeof(debugMsg), "NAM Library: last_write_time failed: %s\n", jsonPath.c_str());
    NAM_DBGMSG(debugMsg);
    return false;
  }

  // If already loaded and unchanged, skip reload.
  if (mLibraryManager.GetRootNode() &&
      mLibraryDataJsonPath == jsonPath &&
      mLibraryDataJsonWriteTime == writeTime)
  {
    mLibraryRootNode = mLibraryManager.GetRootNode();
    NAM_DBGMSG("NAM Library: Up-to-date (skipping reload)\n");
    return (mLibraryRootNode != nullptr);
  }

  snprintf(debugMsg, sizeof(debugMsg), "NAM Library: Loading/reloading from: %s\n", jsonPath.c_str());
  NAM_DBGMSG(debugMsg);

  // Load into a temporary manager so we don't destroy the existing one if parsing fails.
  NAMLibraryManager candidate;
  if (!candidate.LoadMetadata(jsonPath))
  {
    NAM_DBGMSG("NAM Library: LoadMetadata failed (keeping existing metadata)\n");
    // Keep whatever was already loaded.
    mLibraryRootNode = mLibraryManager.GetRootNode();
    return (mLibraryRootNode != nullptr);
  }

  // Commit.
  mLibraryManager = candidate;
  mLibraryRootNode = mLibraryManager.GetRootNode();
  mLibraryDataJsonPath = jsonPath;
  mLibraryDataJsonWriteTime = writeTime;

  if (mLibraryRootNode)
  {
    snprintf(debugMsg, sizeof(debugMsg), "NAM Library: Loaded successfully with %zu top-level items\n",
             mLibraryRootNode->children.size());
    NAM_DBGMSG(debugMsg);
    return true;
  }

  NAM_DBGMSG("NAM Library: LoadMetadata succeeded but root node is null\n");
  return false;
}

std::string NeuralAmpModeler::GetNAMLibraryDataJsonPath() const
{
#ifdef _WIN32
  return GetNAMLibraryPathWindows();
#elif defined(__APPLE__)
  return GetNAMLibraryPathMac();
#else
  return GetNAMLibraryPathLinux();
#endif
}

std::string NeuralAmpModeler::GetNAMLibraryPathWindows() const
{
  const char* appData = std::getenv("APPDATA");
  if (!appData) return "";
  
  std::string path = appData;
  path += "\\nam-model-manager\\data.json";
  return path;
}

std::string NeuralAmpModeler::GetNAMLibraryPathMac() const
{
  const char* home = std::getenv("HOME");
  
  if (!home)
  {
#ifdef __APPLE__
    struct passwd* pw = getpwuid(getuid());
    if (pw) home = pw->pw_dir;
    else return "";
#else
    return "";
#endif
  }

  std::string path = home;
  path += "/Library/Application Support/nam-model-manager/data.json";
  return path;
}

std::string NeuralAmpModeler::GetNAMLibraryPathLinux() const
{
  const char* home = std::getenv("HOME");
  
  if (!home)
  {
#ifndef _WIN32
    struct passwd* pw = getpwuid(getuid());
    if (pw) home = pw->pw_dir;
    else return "";
#else
    return "";
#endif
  }

  const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
  std::string path;
  
  if (xdgConfig && *xdgConfig)
  {
    path = xdgConfig;
  }
  else
  {
    path = home;
    path += "/.config";
  }
  
  path += "/nam-model-manager/data.json";
  return path;
}

// Private methods ============================================================

void NeuralAmpModeler::_ApplyDSPStaging()
{
  std::shared_ptr<ResamplingNAM> retiredModel;
  Cabinet retiredCab;
  bool modelChanged = false, cabChanged = false;
  {
    // The audio thread keeps using the current modules if a publisher holds the lock.
    const std::unique_lock lock(mDSPStagingMutex, std::try_to_lock);
    if (lock.owns_lock())
    {
      // A staged replacement takes precedence over an earlier clear request.
      if (mStagedModel != nullptr || mShouldRemoveModel)
      {
        retiredModel = std::exchange(mModel, std::move(mStagedModel));
        mShouldRemoveModel = false;
        mMapperPendingModel.reset();
        mOutputFadeDir = 0;
        mOutputFadeGain = 1.0f;
        mNewModelLoadedInDSP = mModel != nullptr;
        mModelCleared = mModel == nullptr;
        modelChanged = true;
      }
      // Switch at a block boundary while silent, so the new model's channel
      // layout and gain are in effect for the entire block.
      if (mMapperRequestedModel)
      {
        mMapperPendingModel = std::move(mMapperRequestedModel);
        mOutputFadeDir = -1;
      }
      if (mMapperPendingModel && mOutputFadeGain <= 0.0f)
      {
        retiredModel = std::exchange(mModel, std::move(mMapperPendingModel));
        mOutputFadeDir = 1;
        mNewModelLoadedInDSP = true;
        mModelCleared = false;
        modelChanged = true;
        mSlimDirty = true;
      }
      if (mStagedCab.model != nullptr || mShouldRemoveCab)
      {
        std::swap(retiredCab, mCab);
        mCab = std::exchange(mStagedCab, {});
        mShouldRemoveCab = false;
        cabChanged = true;
      }
    }
  }

  // State recall and host automation can arrive off the audio thread.
  if (mToneStackDirty.exchange(false))
  {
    mToneStack->SetParam("bass", GetParam(kToneBass)->Value());
    mToneStack->SetParam("middle", GetParam(kToneMid)->Value());
    mToneStack->SetParam("treble", GetParam(kToneTreble)->Value());
  }
  if (mDSPGainsAndTimingDirty.exchange(false) || modelChanged || cabChanged)
  {
    mCabEnabled = GetParam(kCabToggle)->Bool();
    _SetInputGain();
    _SetOutputGain();
    _UpdateLatencyAndTail();
  }
}

void NeuralAmpModeler::_FallbackDSP(iplug::sample** inputs, iplug::sample** outputs, const size_t numChannels,
                                    const size_t numFrames)
{
  for (auto c = 0; c < numChannels; c++)
    for (auto s = 0; s < numFrames; s++)
      outputs[c][s] = inputs[c][s];
}

void NeuralAmpModeler::Cabinet::Reset(double sampleRate)
{
  if (model != nullptr)
    model->Reset(sampleRate, kMaxInternalBlockSize);
  if (right != nullptr)
    right->Reset(sampleRate, kMaxInternalBlockSize);
}

void NeuralAmpModeler::_ResetModelAndCab(const double sampleRate)
{
  const std::lock_guard lock(mDSPStagingMutex);
  mDSPStagingSampleRate = sampleRate;
  // The active module may still render if the pending handoff is delayed.
  if (mModel != nullptr)
    mModel->Reset(sampleRate, kMaxInternalBlockSize);
  if (mStagedModel != nullptr)
    mStagedModel->Reset(sampleRate, kMaxInternalBlockSize);
  mCab.Reset(sampleRate);
  mStagedCab.Reset(sampleRate);
  mModelMapper.PrepareModels(sampleRate, kMaxInternalBlockSize);
}

void NeuralAmpModeler::_SetInputGain()
{
  iplug::sample inputGainDB = GetParam(kInputLevel)->Value();
  // Input calibration
  if ((mModel != nullptr) && (mModel->HasInputLevel()) && GetParam(kCalibrateInput)->Bool())
  {
    inputGainDB += GetParam(kInputCalibrationLevel)->Value() - mModel->GetInputLevel();
  }
  mInputGain = DBToAmp(inputGainDB);
}

void NeuralAmpModeler::_SetOutputGain()
{
  double gainDB = GetParam(kOutputLevel)->Value();
  if (mModel != nullptr)
  {
    const int outputMode = GetParam(kOutputMode)->Int();
    switch (outputMode)
    {
      case 1: // Normalized
        if (mModel->HasLoudness())
        {
          const double loudness = mModel->GetLoudness();
          const double targetLoudness = -18.0;
          gainDB += (targetLoudness - loudness);
        }
        break;
      case 2: // Calibrated
        if (mModel->HasOutputLevel())
        {
          const double inputLevel = GetParam(kInputCalibrationLevel)->Value();
          const double outputLevel = mModel->GetOutputLevel();
          gainDB += (outputLevel - inputLevel);
        }
        break;
      case 0: // Raw
      default: break;
    }
  }
  if (mCab.model != nullptr && mCab.isWav && mCabEnabled)
  {
    // Core compensates by sourceRate / hostRate when resampling IR coefficients.
    // This remaining factor preserves the legacy -18 dB * 48000 / hostRate level.
    gainDB += -18.0 + 20.0 * std::log10(48000.0 / mCab.model->GetEncapsulatedSampleRate());
  }
  mOutputGain = DBToAmp(gainDB);
}

void NeuralAmpModeler::_ApplySlimParamToLoadedNAMs()
{
  if (!mSlimDirty)
    return;
  const std::unique_lock lock(mDSPStagingMutex, std::try_to_lock);
  if (!lock.owns_lock() || !mSlimDirty.exchange(false))
    return;

  const double v = GetParam(kSlim)->Value();
  auto apply = [v](ResamplingNAM* p) {
    if (p == nullptr)
      return;
    if (nam::SlimmableModel* s = p->GetSlimmableModel())
      s->SetSlimmableSize(v);
  };
  apply(mModel.get());
  apply(mStagedModel.get());
  mDSPGainsAndTimingDirty = true;
}

std::string NeuralAmpModeler::_StageModel(const WDL_String& modelPath)
{
  try
  {
    auto model = nam::get_dsp(std::filesystem::u8path(modelPath.Get()));
    ValidatePluginModel(model.get());

    const double sampleRate = mDSPStagingSampleRate.load();
    auto staged = std::make_shared<ResamplingNAM>(std::move(model), sampleRate);
    auto* slimmable = staged->GetSlimmableModel();
    const double slim = GetParam(kSlim)->Value();
    if (slimmable != nullptr)
      slimmable->SetSlimmableSize(slim);

    // Keep the current chain until the replacement has loaded and passed validation.
    const bool hadChain = mModelMapper.IsActive() || mModelMapper.IsPreloading();
    mModelMapper.SetActive(false);
    if (hadChain)
      mModelMapper.ClearSlots();
    mPNAMLoadPending = false;
    {
      const std::lock_guard lock(mDSPStagingMutex);
      const double currentRate = mDSPStagingSampleRate.load();
      if (currentRate != sampleRate)
        staged->Reset(currentRate, kMaxInternalBlockSize);
      const double currentSlim = GetParam(kSlim)->Value();
      if (slimmable != nullptr && currentSlim != slim)
        slimmable->SetSlimmableSize(currentSlim);
      mPNAMPath.Set("");
      mMapperRequestedModel.reset();
      mNAMPath = modelPath;
      std::swap(mStagedModel, staged);
    }
    if (auto* ui = GetUI())
    {
      ui->GetControlWithTag(kCtrlTagAmpGain)->SetDisabled(true);
      ui->GetControlWithTag(kCtrlTagPNAMEditorBtn)->SetDisabled(true);
    }
    _PushRecallSnapshot();
    SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadedModel, modelPath.GetLength(), modelPath.Get());
  }
  catch (const std::exception& e)
  {
    SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadFailed);
    std::cerr << "Failed to read DSP module" << std::endl;
    std::cerr << e.what() << std::endl;
    return e.what();
  }
  return "";
}

std::string NeuralAmpModeler::_StageCab(const WDL_String& cabPath)
{
  try
  {
    // Finish loading before publishing the model, so a failed load preserves the current cab and path.
    const auto path = std::filesystem::u8path(cabPath.Get());
    nam::dspData config;
    auto model = nam::get_dsp(path, config);
    ValidatePluginModel(model.get());

    const double sampleRate = mDSPStagingSampleRate.load();
    Cabinet staged;
    staged.model = std::make_unique<ResamplingNAM>(std::move(model), sampleRate);
    if (staged.model->NumOutputChannels() == 1)
    {
      // Use the same decoded data so both instances match even if the file changes.
      // Prepare the right instance here so changing amp width needs no model loading in ProcessBlock.
      staged.right = std::make_unique<ResamplingNAM>(nam::get_dsp(config), sampleRate);
    }
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    staged.isWav = extension == ".wav";

    {
      const std::lock_guard lock(mDSPStagingMutex);
      const double currentRate = mDSPStagingSampleRate.load();
      if (currentRate != sampleRate)
        staged.Reset(currentRate);
      mCabPath = cabPath;
      std::swap(mStagedCab, staged);
    }
    _PushRecallSnapshot();
    SendControlMsgFromDelegate(kCtrlTagCabFileBrowser, kMsgTagLoadedCab, cabPath.GetLength(), cabPath.Get());
  }
  catch (const std::exception& error)
  {
    SendControlMsgFromDelegate(kCtrlTagCabFileBrowser, kMsgTagLoadFailed);
    std::cerr << "Failed to load cab: " << error.what() << std::endl;
    return error.what();
  }
  return "";
}

void NeuralAmpModeler::_InitToneStack()
{
  // If you want to customize the tone stack, then put it here!
  mToneStack = std::make_unique<dsp::tone_stack::BasicNamToneStack>();
}
void NeuralAmpModeler::_ProcessInput(iplug::sample** inputs, const size_t nFrames, const size_t nChansIn)
{
  // On the standalone, we can probably assume that the user has plugged into only one input and they expect it to be
  // carried straight through. Don't apply any division over nChansIn because we're just "catching anything out there."
  // However, in a DAW, it's probably something providing stereo, and we want to take the average in order to avoid
  // doubling the loudness. (This would change w/ double mono processing)
  double gain = mInputGain;
#ifndef APP_API
  if (nChansIn > 0)
    gain /= static_cast<double>(nChansIn);
#endif
  // Also handle a disconnected input without replaying the previous block.
  std::fill_n(mInputBuffer.data(), nFrames, 0.0);
  for (size_t c = 0; c < nChansIn; c++)
    for (size_t s = 0; s < nFrames; s++)
      if (c == 0)
        mInputBuffer[s] = gain * inputs[c][s];
      else
        mInputBuffer[s] += gain * inputs[c][s];
}

void NeuralAmpModeler::_ProcessOutput(iplug::sample** inputs, iplug::sample** outputs, const size_t nFrames,
                                      const size_t nChansIn, const size_t nChansOut)
{
  const double gain = mOutputGain;
  for (size_t cout = 0; cout < nChansOut; ++cout)
    for (size_t s = 0; s < nFrames; ++s)
    {
      const double signal =
        nChansOut == 1 && nChansIn == 2 ? 0.5 * (inputs[0][s] + inputs[1][s]) : inputs[cout % nChansIn][s];
#ifdef APP_API // Ensure valid output to interface
      outputs[cout][s] = std::clamp(gain * signal, -1.0, 1.0);
#else
      outputs[cout][s] = gain * signal;
#endif
    }
}

void NeuralAmpModeler::_UpdateControlsFromModel()
{
  if (auto* pGraphics = GetUI())
  {
    ModelInfo modelInfo;
    bool slimmable, hasLoudness;
    {
      const std::lock_guard lock(mDSPStagingMutex);
      if (mModel == nullptr)
        return;
      modelInfo.sampleRate.known = true;
      modelInfo.sampleRate.value = mModel->GetEncapsulatedSampleRate();
      modelInfo.inputCalibrationLevel.known = mModel->HasInputLevel();
      modelInfo.inputCalibrationLevel.value = mModel->HasInputLevel() ? mModel->GetInputLevel() : 0.0;
      modelInfo.outputCalibrationLevel.known = mModel->HasOutputLevel();
      modelInfo.outputCalibrationLevel.value = mModel->HasOutputLevel() ? mModel->GetOutputLevel() : 0.0;
      slimmable = mModel->GetSlimmableModel() != nullptr;
      hasLoudness = mModel->HasLoudness();
    }

    static_cast<NAMSettingsPageControl*>(pGraphics->GetControlWithTag(kCtrlTagSettingsBox))->SetModelInfo(modelInfo);

    const bool disableInputCalibrationControls = !modelInfo.inputCalibrationLevel.known;
    pGraphics->GetControlWithTag(kCtrlTagCalibrateInput)->SetDisabled(disableInputCalibrationControls);
    pGraphics->GetControlWithTag(kCtrlTagInputCalibrationLevel)->SetDisabled(disableInputCalibrationControls);
    {
      auto* c = static_cast<OutputModeControl*>(pGraphics->GetControlWithTag(kCtrlTagOutputMode));
      c->SetNormalizedDisable(!hasLoudness);
      c->SetCalibratedDisable(!modelInfo.outputCalibrationLevel.known);
    }

    if (auto* pSlimIcon = pGraphics->GetControlWithTag(kCtrlTagSlimmableIcon))
      pSlimIcon->Hide(!slimmable);
  }
}

void NeuralAmpModeler::_UpdateLatencyAndTail()
{
  int latency = 0;
  if (mModel)
  {
    latency += mModel->GetLatency();
  }
  const bool cabActive = mCab.model != nullptr && mCabEnabled;
  if (cabActive)
    latency += mCab.model->GetLatency();

  // Both wrappers report their models' history lengths in host-rate samples.
  // Allow another two seconds (ten cycles at 5 Hz) for the DC blocker.
  const double tail = 2.0 * GetSampleRate() + (mModel ? mModel->GetPrewarmSamples() : 0)
                      + (cabActive ? mCab.model->GetPrewarmSamples() : 0);
  SetTailSize(static_cast<int>(std::min(tail, static_cast<double>(std::numeric_limits<int>::max()))));

  // Feels weird to have to do this.
  if (GetLatency() != latency)
  {
    SetLatency(latency);
  }
}

void NeuralAmpModeler::_UpdateMeters(sample** inputPointer, sample** outputPointer, const size_t nFrames,
                                     const size_t nChansOut)
{
  mInputSender.ProcessBlock(inputPointer, (int)nFrames, kCtrlTagInputMeter, 1);
  if (nChansOut > 0)
  {
    // Mirror the host's mono output on both bars.
    sample* meterOutput[]{outputPointer[0], outputPointer[nChansOut == 1 ? 0 : 1]};
    mOutputSender.ProcessBlock(meterOutput, (int)nFrames, kCtrlTagOutputMeter);
  }
}

bool NeuralAmpModeler::SerializeState(IByteChunk& chunk) const
{
  WDL_String header("###NeuralAmpModeler###");
  chunk.PutStr(header.Get());
  WDL_String version(PLUG_VERSION_STR);
  chunk.PutStr(version.Get());
  const std::lock_guard lock(mDSPStagingMutex);
  chunk.PutStr(mNAMPath.Get());
  chunk.PutStr(mCabPath.Get());
  bool ok = SerializeParams(chunk); // all param doubles first
  chunk.PutStr(mPNAMPath.Get()); // pnam path appended after params
  return ok;
}

int NeuralAmpModeler::UnserializeState(const IByteChunk& chunk, int startPos)
{
  WDL_String header;
  int pos = startPos;
  pos = chunk.GetStr(header, pos);

  const char* kExpectedHeader = "###NeuralAmpModeler###";
  if (strcmp(header.Get(), kExpectedHeader) == 0)
  {
    return _UnserializeStateWithKnownVersion(chunk, pos);
  }
  else
  {
    return _UnserializeStateWithUnknownVersion(chunk, startPos);
  }
}

void NeuralAmpModeler::OnUIOpen()
{
  Plugin::OnUIOpen();

  InitializeLibraryManager();

  // Reset hover-tracking so OnIdle treats first tick as a slot change
  mLastTooltipSlotIndex = -2;
  mBrowserShowingSlotName = false;
  mLastKnobHoverState = false;

  WDL_String modelPath, cabPath;
  bool modelLoaded, cabLoaded;
  {
    const std::lock_guard lock(mDSPStagingMutex);
    modelPath = mNAMPath;
    cabPath = mCabPath;
    modelLoaded = mModel != nullptr || mStagedModel != nullptr;
    cabLoaded = mCab.model != nullptr || mStagedCab.model != nullptr;
  }
  if (modelPath.GetLength())
  {
    SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadedModel, modelPath.GetLength(), modelPath.Get());
    // If it's not loaded yet, then mark as failed.
    // If it's yet to be loaded, then the completion handler will set us straight once it runs.
    if (!modelLoaded)
      SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadFailed);
  }

  if (cabPath.GetLength())
  {
    SendControlMsgFromDelegate(kCtrlTagCabFileBrowser, kMsgTagLoadedCab, cabPath.GetLength(), cabPath.Get());
    if (!cabLoaded)
      SendControlMsgFromDelegate(kCtrlTagCabFileBrowser, kMsgTagLoadFailed);
  }

  if (mPNAMPath.GetLength() && !mModelMapper.IsActive() && !mModelMapper.IsPreloading())
  {
    // First open after deserialization — load the chain from disk.
    _LoadPNAMFile(mPNAMPath.Get());
  }
  else if (mPNAMPath.GetLength() && mModelMapper.IsActive())
  {
    // UI was closed and reopened while the mapper is already running.
    // The controls are brand-new instances; restore their state without
    // re-loading anything from disk.

    // 1. Send the chain display name with no directory path so the file-browser
    //    control takes the "display-only" branch and sets mRestingLabel correctly.
    const std::string displayName = "[Chain] " + std::filesystem::path(mPNAMPath.Get()).stem().string();
    WDL_String displayStr(displayName.c_str());
    SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadedModel,
                               displayStr.GetLength(), displayStr.Get());

    // 2. Enable the Voice knob and editor button.
    if (auto* pGraphics = GetUI())
    {
      if (auto* pCtrl = pGraphics->GetControlWithTag(kCtrlTagAmpGain))
        pCtrl->SetDisabled(false);
      if (auto* pCtrl = pGraphics->GetControlWithTag(kCtrlTagPNAMEditorBtn))
        pCtrl->SetDisabled(false);

      // 3. Rebuild and push slot-boundary tick marks to the fresh knob instance.
      const auto& slots = mModelMapper.GetSlots();
      std::vector<float> boundaries;
      boundaries.reserve(slots.size() * 2);
      for (const auto& slot : slots)
      {
        if (slot.ampGainMin > 0.0)
          boundaries.push_back(static_cast<float>(slot.ampGainMin / 10.0));
        if (slot.ampGainMax < 10.0)
          boundaries.push_back(static_cast<float>(slot.ampGainMax / 10.0));
      }
      std::sort(boundaries.begin(), boundaries.end());
      boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());

      if (auto* pKnob = static_cast<NAMKnobControl*>(pGraphics->GetControlWithTag(kCtrlTagAmpGain)))
        pKnob->SetPNAMBoundaries(std::move(boundaries));
    }
  }

  _UpdateControlsFromModel();

  // Restore Amp Gain knob visibility based on mapper state
  if (auto* pGraphics = GetUI())
  {
    if (auto* pCtrl = pGraphics->GetControlWithTag(kCtrlTagAmpGain))
      pCtrl->SetDisabled(!mModelMapper.IsActive());
    if (auto* pCtrl = pGraphics->GetControlWithTag(kCtrlTagPNAMEditorBtn))
      pCtrl->SetDisabled(!mModelMapper.IsActive());
  }

  _UpdateRecallButtonStates();

}

void NeuralAmpModeler::OnParamChange(int paramIdx)
{
  switch (paramIdx)
  {
    case kCalibrateInput:
    case kInputCalibrationLevel:
    case kInputLevel:
    case kOutputLevel:
    case kOutputMode:
    case kCabToggle: mDSPGainsAndTimingDirty = true; break;
    case kToneBass:
    case kToneMid:
    case kToneTreble: mToneStackDirty = true; break;
    case kSlim: mSlimDirty = true; break;
    case kAmpGain:
    {
      if (mModelMapper.IsActive())
      {
        const double ampGain = GetParam(kAmpGain)->Value();
        auto newModel = mModelMapper.EvaluateInputGain(ampGain);
        if (newModel)
        {
          // Don't stage directly — fade out first to avoid pop
          {
            const std::lock_guard lock(mDSPStagingMutex);
            if (mModelMapper.IsActive())
              mMapperRequestedModel = std::move(newModel);
          }

          // Stage overrides immediately (gain/tone params don't pop)
          auto overrides = mModelMapper.GetActiveOverrides();
          if (overrides.has_value())
          {
            if (overrides->outputLevel.has_value()) GetParam(kOutputLevel)->Set(*overrides->outputLevel);
            if (overrides->toneBass.has_value())    GetParam(kToneBass)->Set(*overrides->toneBass);
            if (overrides->toneMid.has_value())     GetParam(kToneMid)->Set(*overrides->toneMid);
            if (overrides->toneTreble.has_value())  GetParam(kToneTreble)->Set(*overrides->toneTreble);

            SendParameterValueFromDelegate(kOutputLevel, GetParam(kOutputLevel)->GetNormalized(), true);
            SendParameterValueFromDelegate(kToneBass, GetParam(kToneBass)->GetNormalized(),       true);
            SendParameterValueFromDelegate(kToneMid, GetParam(kToneMid)->GetNormalized(),         true);
            SendParameterValueFromDelegate(kToneTreble, GetParam(kToneTreble)->GetNormalized(),  true);
            mDSPGainsAndTimingDirty = true;
        mToneStackDirty = true;
          }
        }
      }
      break;
    }
    default: break;
  }
}

void NeuralAmpModeler::OnParamChangeUI(int paramIdx, EParamSource source)
{
  if (auto pGraphics = GetUI())
  {
    bool active = GetParam(paramIdx)->Bool();

    switch (paramIdx)
    {
      case kNoiseGateActive: pGraphics->GetControlWithParamIdx(kNoiseGateThreshold)->SetDisabled(!active); break;
      case kEQActive:
        pGraphics->ForControlInGroup("EQ_KNOBS", [active](IControl* pControl) { pControl->SetDisabled(!active); });
        break;
      case kCabToggle: pGraphics->GetControlWithTag(kCtrlTagCabFileBrowser)->SetDisabled(!active); break;
      default: break;
    }
  }
  // Keep the current recall slot's param snapshot up to date whenever the
  // user moves a knob while a model/chain is loaded.
  // Skip: during a recall apply, when no slot exists yet, and for AmpGain
  // (which drives slot switching rather than the slot's own state).
  if (!mIsApplyingRecall && paramIdx != kAmpGain && mRecallIndex >= 0
      && mRecallIndex < static_cast<int>(mRecallHistory.size()) && (mNAMPath.GetLength() || mPNAMPath.GetLength()))
  {
    _UpdateCurrentRecallParams();
  }
}

bool NeuralAmpModeler::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  switch (msgTag)
  {
    case kMsgTagClearModel:
    {
      std::shared_ptr<ResamplingNAM> superseded;
      const std::lock_guard lock(mDSPStagingMutex);
      superseded = std::move(mStagedModel);
      mNAMPath.Set("");
      mPNAMPath.Set("");
      mMapperRequestedModel.reset();
      mModelMapper.SetActive(false);
      mPNAMLoadPending = false;
      mShouldRemoveModel = true;
      return true;
    }
    case kMsgTagClearCab:
    {
      Cabinet superseded;
      const std::lock_guard lock(mDSPStagingMutex);
      superseded = std::exchange(mStagedCab, {});
      mCabPath.Set("");
      mShouldRemoveCab = true;
      return true;
    }
    case kMsgTagHighlightColor:
    {
      mHighLightColor.Set((const char*)pData);

      if (GetUI())
      {
        GetUI()->ForStandardControlsFunc([&](IControl* pControl) {
          if (auto* pVectorBase = pControl->As<IVectorBase>())
          {
            IColor color = IColor::FromColorCodeStr(mHighLightColor.Get());

            pVectorBase->SetColor(kX1, color);
            pVectorBase->SetColor(kPR, color.WithOpacity(0.3f));
            pVectorBase->SetColor(kFR, color.WithOpacity(0.4f));
            pVectorBase->SetColor(kX3, color.WithContrast(0.1f));
          }
          pControl->GetUI()->SetAllControlsDirty();
        });
      }

      return true;
    }
    default: return false;
  }
}

// ---------------------------------------------------------------------------
// Recall history
// ---------------------------------------------------------------------------

void NeuralAmpModeler::_PushRecallSnapshot()
{
  if (mIsApplyingRecall)
    return;

  // Before creating a new entry, flush any knob adjustments made since the
  // last load into the CURRENT slot. This is the only reliable place to do
  // this because it runs synchronously on every model/IR load, regardless of
  // whether OnParamChangeUI fired for every knob movement.
  if (mRecallIndex >= 0 && mRecallIndex < static_cast<int>(mRecallHistory.size()))
  {
    RecallSnapshot& current = mRecallHistory[mRecallIndex];
    for (int i = 0; i < kNumParams; i++)
      current.params[i] = GetParam(i)->Value();
  }

  RecallSnapshot snap;
  {
    const std::lock_guard lock(mDSPStagingMutex);
    snap.namPath  = mNAMPath;
    snap.irPath   = mCabPath;
    snap.pnamPath = mPNAMPath;
  }
  for (int i = 0; i < kNumParams; i++)
    snap.params[i] = GetParam(i)->Value();

  // Truncate forward history when a new load happens while not at the end
  if (mRecallIndex + 1 < static_cast<int>(mRecallHistory.size()))
    mRecallHistory.erase(mRecallHistory.begin() + mRecallIndex + 1, mRecallHistory.end());

  mRecallHistory.push_back(std::move(snap));
  if (static_cast<int>(mRecallHistory.size()) > kMaxRecallHistory)
    mRecallHistory.erase(mRecallHistory.begin());

  mRecallIndex = static_cast<int>(mRecallHistory.size()) - 1;
  _UpdateRecallButtonStates();
}

void NeuralAmpModeler::_UpdateCurrentRecallParams()
{
  if (mRecallIndex < 0 || mRecallIndex >= static_cast<int>(mRecallHistory.size()))
    return;

  RecallSnapshot& snap = mRecallHistory[mRecallIndex];
  for (int i = 0; i < kNumParams; i++)
    snap.params[i] = GetParam(i)->Value();
}

void NeuralAmpModeler::_ApplyRecallSnapshot(const RecallSnapshot& snap)
{
  mIsApplyingRecall = true;

  // 1. Restore param values in the param store
  ENTER_PARAMS_MUTEX
  for (int i = 0; i < kNumParams; i++)
    GetParam(i)->Set(snap.params[i]);
  LEAVE_PARAMS_MUTEX

  // 2. Apply DSP side-effects (gain, tone stack, etc.)
  for (int i = 0; i < kNumParams; i++)
    OnParamChange(i);

  // 3. Update knob/switch visuals directly on the UI thread
  if (auto* pGraphics = GetUI())
  {
    for (int i = 0; i < kNumParams; i++)
    {
      IControl* pCtrl = pGraphics->GetControlWithParamIdx(i);
      if (pCtrl)
        pCtrl->SetValueFromDelegate(GetParam(i)->GetNormalized());
    }

    // 4. Restore enabled/disabled states of dependent controls
    OnParamChangeUI(kNoiseGateActive, iplug::EParamSource::kPresetRecall);
    OnParamChangeUI(kEQActive,        iplug::EParamSource::kPresetRecall);
    OnParamChangeUI(kCabToggle,        iplug::EParamSource::kPresetRecall);

    pGraphics->SetAllControlsDirty();
  }

  // 5. Reload model / IR / chain
  if (snap.pnamPath.GetLength())
  {
    mNAMPath.Set("");
    _LoadPNAMFile(snap.pnamPath.Get());
  }
  else
  {
    // Deactivate mapper if currently active
    if (mModelMapper.IsActive())
    {
      mModelMapper.SetActive(false);
      mModelMapper.ClearSlots();
      mPNAMPath.Set("");
      if (auto* pGraphics = GetUI())
      {
        if (auto* c = pGraphics->GetControlWithTag(kCtrlTagAmpGain))       c->SetDisabled(true);
        if (auto* c = pGraphics->GetControlWithTag(kCtrlTagPNAMEditorBtn)) c->SetDisabled(true);
      }
    }

    if (snap.namPath.GetLength())
      _StageModel(snap.namPath);
    else
      OnMessage(kMsgTagClearModel, 0, 0, nullptr);
  }

  if (snap.irPath.GetLength())
    _StageCab(snap.irPath);
  else
    OnMessage(kMsgTagClearCab, 0, 0, nullptr);

  mIsApplyingRecall = false;
  _UpdateRecallButtonStates();
}

void NeuralAmpModeler::RecallBack()
{
  if (mRecallIndex <= 0)
    return;

  // Save any knob adjustments made since the last load into the current slot
  // before leaving it — mirrors what _PushRecallSnapshot does on a new load.
  _UpdateCurrentRecallParams();

  --mRecallIndex;
  _ApplyRecallSnapshot(mRecallHistory[mRecallIndex]);
}

void NeuralAmpModeler::RecallForward()
{
  if (mRecallIndex + 1 >= static_cast<int>(mRecallHistory.size()))
    return;

  // Same: flush current knob state before moving forward
  _UpdateCurrentRecallParams();

  ++mRecallIndex;
  _ApplyRecallSnapshot(mRecallHistory[mRecallIndex]);
}

void NeuralAmpModeler::_UpdateRecallButtonStates()
{
  if (auto* pGraphics = GetUI())
  {
    if (auto* c = pGraphics->GetControlWithTag(kCtrlTagRecallBack))
      c->SetDisabled(mRecallIndex <= 0);
    if (auto* c = pGraphics->GetControlWithTag(kCtrlTagRecallForward))
      c->SetDisabled(mRecallIndex + 1 >= static_cast<int>(mRecallHistory.size()));
  }
}

// HACK
#include "Unserialization.cpp"


void NeuralAmpModeler::HandleFileDrop(const char* str, bool droppedOnCabBrowser)
{
  if (!str || !strlen(str))
    return;

  auto extension = std::filesystem::u8path(str).extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  if (extension == ".pnam")
    _LoadPNAMFile(str);
  else if (extension == ".nam" || extension == ".wav")
  {
    const WDL_String path(str);
    const bool loadCab = extension == ".wav" || droppedOnCabBrowser;
    const auto error = loadCab ? _StageCab(path) : _StageModel(path);
    if (!error.empty())
      _ShowMessageBox(GetUI(), error.c_str(), "Failed to load dropped file", kMB_OK);
  }
}

void NeuralAmpModeler::_LoadPNAMFile(const std::string& pnamPath)
{
  // If a preload is already running, ignore.
  if (mModelMapper.IsPreloading())
    return;

  // Build a resolver that shows a message box for each missing .nam file.
  // Runs on the UI thread so ShowMessageBox is safe.
  auto resolver = [this](const std::string& missingPath, int slotIndex) -> std::optional<std::string>
  {
    std::stringstream ss;
    ss << "Model file for slot " << (slotIndex + 1) << " was not found:\n\n"
       << missingPath << "\n\n"
       << "Skip this slot and continue loading?";
    const auto result = _ShowMessageBox(GetUI(), ss.str().c_str(), "Model File Missing", kMB_YESNO);
    if (result == kYES)
      return std::string{}; // skip slot
    return std::nullopt;    // abort
  };

  mModelMapper.ClearSlots();

  // Synchronous phase: parse JSON and resolve missing files (may show dialogs).
  // Pass preload=false so heavy model loading happens off the UI thread below.
  PNAMLoadResult parseResult = mModelMapper.LoadFromFile(pnamPath, GetSampleRate(), GetBlockSize(), resolver, false);

  if (!parseResult.success)
  {
    std::stringstream ss;
    ss << "Failed to load .pnam file:\n\n" << parseResult.errorMessage;
    _ShowMessageBox(GetUI(), ss.str().c_str(), "Failed to load chain", kMB_OK);
    return;
  }

  // Warn about skipped files now (still on UI thread, before async).
  if (!parseResult.skippedFiles.empty())
  {
    std::stringstream ss;
    ss << parseResult.skippedFiles.size() << " slot(s) were skipped due to missing files.\n"
       << "The remaining slots will be loaded in the background.";
    _ShowMessageBox(GetUI(), ss.str().c_str(), "Chain Loaded with Warnings", kMB_OK);
  }

  // Store the path for OnIdle to finish staging after preload.
  {
    const std::lock_guard lock(mDSPStagingMutex);
    mModelMapper.SetActive(false);
    mMapperRequestedModel.reset();
    mPNAMPath.Set(pnamPath.c_str());
  }
  _PushRecallSnapshot();
  const auto p = std::filesystem::u8path(pnamPath);
  mPNAMPendingDisplayName = "[Chain] " + p.stem().string();

  // Show a "loading" label immediately so the UI gives feedback.
  const std::string loadingLabel = mPNAMPendingDisplayName + " (loading...)";
  WDL_String loadingStr(loadingLabel.c_str());
  SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadedModel,
                             loadingStr.GetLength(), loadingStr.Get());

  // Asynchronous phase: load NAM files on a background thread.
  // OnIdle will pick up mPNAMLoadPending and activate the mapper.
  mModelMapper.PreloadAllAsync(mDSPStagingSampleRate.load(), kMaxInternalBlockSize, [this](int loadedSlots) {
    mPNAMLoadedSlots.store(loadedSlots);
    mPNAMLoadPending.store(true); // signal OnIdle — written last as the memory barrier
  });
}

















































































































































