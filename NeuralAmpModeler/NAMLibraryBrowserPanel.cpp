#include "NAMLibraryBrowserPanel.h"
#include "../Colors.h"
#include <filesystem>
#include <cctype>

// Custom editable text control with callback
class SearchTextControl : public IEditableTextControl
{
public:
  SearchTextControl(const IRECT& bounds, const IText& text, 
                    std::function<void(const char*)> onTextChange)
  : IEditableTextControl(bounds, "Type to search...", text, COLOR_DARK_GRAY)
  , mOnTextChange(onTextChange)
  {
  }
  
  void Draw(IGraphics& g) override
  {
    // Draw background with border
    g.FillRoundRect(COLOR_DARK_GRAY, mRECT, 3.0f);
    g.DrawRoundRect(COLOR_GRAY, mRECT, 3.0f, nullptr, 2.0f);
    
    // Draw text
    IRECT textRect = mRECT.GetPadded(-5.0f);
    
    if (strlen(GetStr()) > 0)
    {
      g.DrawText(mText, GetStr(), textRect);
    }
    else
    {
      // Draw placeholder
      IText placeholderText = mText;
      placeholderText.mFGColor = mText.mFGColor.WithOpacity(0.5f);
      g.DrawText(placeholderText, "Type to search...", textRect);
    }
  }
  
  void OnTextEntryCompletion(const char* str, int valIdx) override
  {
    SetStr(str);
    SetDirty(true);
    
    if (mOnTextChange)
      mOnTextChange(str);
  }
  
private:
  std::function<void(const char*)> mOnTextChange;
};

void NAMLibraryBrowserPanel::OnAttached()
{
  const float pad = 15.0f;
  const float searchHeight = 35.0f;
  const float buttonHeight = 35.0f;
  const float titleHeight = 50.0f;
  
  IRECT contentArea = GetRECT().GetPadded(-30.0f);
  IRECT titleArea = contentArea.GetFromTop(titleHeight);
  IRECT searchArea = contentArea.GetFromTop(searchHeight).GetVShifted(titleHeight + pad);
  IRECT buttonsArea = contentArea.GetFromBottom(buttonHeight);
  
  IRECT treeArea = IRECT(
    contentArea.L,
    searchArea.B + pad,
    contentArea.R,
    buttonsArea.T - pad
  );

  // Title
  IText titleText = IText(20, COLOR_WHITE, "Roboto-Regular", EAlign::Center, EVAlign::Middle, 0);
  auto* pTitle = new ITextControl(titleArea, "NAM Library Browser", titleText);
  AddChildControl(pTitle);

  // Search label
  IText searchLabelText = IText(14, COLOR_LIGHT_GRAY, "Roboto-Regular", EAlign::Near, EVAlign::Middle, 0);
  IRECT searchLabelArea = searchArea.GetFromLeft(120.0f);
  auto* pSearchLabel = new ITextControl(searchLabelArea, "Search:", searchLabelText);
  AddChildControl(pSearchLabel);
  
  // Search input using IEditableTextControl
  IRECT searchInputArea = searchArea.GetReducedFromLeft(120.0f).GetPadded(-5.0f);
  
  IText searchTextStyle(16, COLOR_WHITE, "Roboto-Regular", EAlign::Near, EVAlign::Middle, 0);
  auto* pSearchInput = new SearchTextControl(
    searchInputArea, 
    searchTextStyle,
    [this](const char* str) {
      OnSearchTextChanged(str);
    }
  );
  
  mpSearchDisplay = pSearchInput;
  AddChildControl(pSearchInput);

  // Create tree view
  mpTreeView = new NAMLibraryTreeView(treeArea, mStyle);
  AddChildControl(mpTreeView);
  
  // Model selection callback
  mpTreeView->SetOnModelSelected([this](const std::shared_ptr<NAMLibraryTreeNode>& node) {
    if (!node || !node->IsModel())
      return;
    
    auto* pGraphics = GetUI();
    if (!pGraphics)
      return;
    
    if (node->path.empty())
    {
      pGraphics->ShowMessageBox("Model path is empty.", "Cannot Load Model", kMB_OK);
      return;
    }
    
    std::error_code ec;
    bool fileExists = std::filesystem::exists(node->path, ec);
    
    if (ec || !fileExists)
    {
      std::string errorMsg = "Cannot access model file:\n\n" + node->path;
      if (ec)
        errorMsg += "\n\nError: " + ec.message();
      
      pGraphics->ShowMessageBox(errorMsg.c_str(), "Model File Not Accessible", kMB_OK);
      return;
    }
    
    uintmax_t fileSize = std::filesystem::file_size(node->path, ec);
    if (ec || fileSize == 0)
    {
      pGraphics->ShowMessageBox("Model file is empty or corrupted.", "Invalid Model File", kMB_OK);
      return;
    }
    
    auto nodeCopy = std::make_shared<NAMLibraryTreeNode>(*node);
    mShouldClose = true;
    Hide(true);
    
    if (mOnModelSelected)
      mOnModelSelected(nodeCopy);
  });

  mpTreeView->SetRootNode(mRootNode);

  // Close button
  float buttonWidth = 100.0f;
  IRECT closeButtonBounds(
    buttonsArea.MW() - buttonWidth / 2.0f, 
    buttonsArea.T, 
    buttonsArea.MW() + buttonWidth / 2.0f, 
    buttonsArea.B
  );
  
  IVButtonControl* pCloseButton = new IVButtonControl(closeButtonBounds, [this](IControl* pCaller) {
    mShouldClose = true;
    SetDirty(false);
  }, "Close", mStyle);
  
  AddChildControl(pCloseButton);
}

void NAMLibraryBrowserPanel::OnSearchTextChanged(const char* text)
{
  if (!mpLibraryManager || !text)
    return;
    
  std::string query(text);
  PerformSearch(query);
}

void NAMLibraryBrowserPanel::PerformSearch(const std::string& query)
{
  if (!mpTreeView || !mpLibraryManager)
    return;

  if (query.empty() || query == "Type to search...")
  {
    mpTreeView->SetRootNode(mRootNode);
  }
  else
  {
    auto searchResults = mpLibraryManager->SearchModels(query);
    
    auto searchRoot = std::make_shared<NAMLibraryTreeNode>();
    searchRoot->name = "Search Results (" + std::to_string(searchResults.size()) + " models)";
    searchRoot->id = "search_root";
    searchRoot->depth = 0;
    searchRoot->expanded = true;
    
    for (const auto& result : searchResults)
    {
      auto resultCopy = std::make_shared<NAMLibraryTreeNode>();
      *resultCopy = *result;
      resultCopy->depth = 1;
      resultCopy->parent = searchRoot;
      resultCopy->children.clear();
      
      searchRoot->children.push_back(resultCopy);
    }
    
    mpTreeView->SetRootNode(searchRoot);
  }
  
  mpTreeView->RefreshTree();
}

void NAMLibraryBrowserPanel::Draw(IGraphics& g)
{
  if (mShouldClose)
  {
    if (auto* pGraphics = GetUI())
    {
      auto* panelToRemove = this;
      GetDelegate()->BeginInformHostOfParamChangeFromUI(kNoParameter);
      GetDelegate()->EndInformHostOfParamChangeFromUI(kNoParameter);
      pGraphics->RemoveControl(panelToRemove);
      return;
    }
  }
  
  // Only draw overlay if not in separate window (check if bounds match screen)
  IRECT screenBounds = g.GetBounds();
  bool isInSeparateWindow = (mRECT.L == 0 && mRECT.T == 0 && 
                             mRECT.W() == screenBounds.W() && 
                             mRECT.H() == screenBounds.H());
  
  if (!isInSeparateWindow)
  {
    g.FillRect(COLOR_BLACK.WithOpacity(0.85f), screenBounds);
    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.95f), mRECT, 10.0f);
    g.DrawRoundRect(PluginColors::NAM_THEMECOLOR, mRECT, 10.0f, nullptr, 3.0f);
  }
  
  IContainerBase::Draw(g);
}

// Add OnResize handler
void NAMLibraryBrowserPanel::OnResize()
{
  if (!mpTreeView || !mpSearchDisplay)
    return;
    
  const float pad = 15.0f;
  const float searchHeight = 35.0f;
  const float buttonHeight = 35.0f;
  const float titleHeight = 50.0f;
  
  IRECT contentArea = mRECT.GetPadded(-30.0f);
  IRECT searchArea = contentArea.GetFromTop(searchHeight).GetVShifted(titleHeight + pad);
  IRECT buttonsArea = contentArea.GetFromBottom(buttonHeight);
  
  IRECT treeArea = IRECT(
    contentArea.L,
    searchArea.B + pad,
    contentArea.R,
    buttonsArea.T - pad
  );
  
  // Update tree view bounds
  mpTreeView->SetTargetAndDrawRECTs(treeArea);
  mpTreeView->RefreshTree();
  
  SetDirty(false);
}