#pragma once

#include "IControls.h"
#include "NAMLibraryManager.h"
#include "NAMLibraryTreeView.h"
#include <functional>
#include <memory>

using namespace iplug;
using namespace igraphics;

using OnLibraryModelSelectedFunc = std::function<void(const std::shared_ptr<NAMLibraryTreeNode>& model)>;

class NAMLibraryBrowserPanel : public IContainerBase
{
public:
  NAMLibraryBrowserPanel(const IRECT& bounds, 
                         NAMLibraryManager* pLibraryMgr, 
                         std::shared_ptr<NAMLibraryTreeNode> rootNode,
                         const IVStyle& style)
  : IContainerBase(bounds)
  , mpLibraryManager(pLibraryMgr)
  , mRootNode(rootNode)
  , mStyle(style)
  , mShouldClose(false)
  {
  }

  void SetOnModelSelected(OnLibraryModelSelectedFunc func) { mOnModelSelected = func; }
  void OnAttached() override;
  void Draw(IGraphics& g) override;
  void OnResize();  // Add this

private:
  void OnSearchTextChanged(const char* text);
  void PerformSearch(const std::string& query);
  
  NAMLibraryManager* mpLibraryManager = nullptr;
  std::shared_ptr<NAMLibraryTreeNode> mRootNode;
  IVStyle mStyle;
  OnLibraryModelSelectedFunc mOnModelSelected;
  NAMLibraryTreeView* mpTreeView = nullptr;
  IControl* mpSearchDisplay = nullptr;
  bool mShouldClose;
};