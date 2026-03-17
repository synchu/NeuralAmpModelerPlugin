#pragma once

#include "IControls.h"
#include "NAMLibraryTreeNode.h"
#include <functional>
#include <memory>
#include <vector>

using namespace iplug;
using namespace igraphics;

using OnTreeModelSelectedFunc = std::function<void(const std::shared_ptr<NAMLibraryTreeNode>& model)>;

class NAMLibraryTreeView : public IContainerBase
{
public:
  NAMLibraryTreeView(const IRECT& bounds, const IVStyle& style);
  
  void SetRootNode(std::shared_ptr<NAMLibraryTreeNode> root);
  void SetOnModelSelected(OnTreeModelSelectedFunc func);
  void RefreshTree();
  
  void OnAttached() override;
  void Draw(IGraphics& g) override;
  void OnMouseDown(float x, float y, const IMouseMod& mod) override;
  void OnMouseDblClick(float x, float y, const IMouseMod& mod) override;
  void OnMouseOver(float x, float y, const IMouseMod& mod) override;
  void OnMouseOut() override;
  void OnMouseWheel(float x, float y, const IMouseMod& mod, float d) override;

private:
  struct TreeItemUI
  {
    std::shared_ptr<NAMLibraryTreeNode> node;
    int displayIndex = 0;
    float yPos = 0;
  };

  void BuildDisplayList(std::shared_ptr<NAMLibraryTreeNode> node, std::vector<TreeItemUI>& displayList, float& yPos);
  void DrawItem(IGraphics& g, const TreeItemUI& item, bool isSelected);
  int GetItemAtY(float y) const;
  void OnItemClick(int itemIndex);
  void OnItemDoubleClick(int itemIndex);
  void ToggleFolder(std::shared_ptr<NAMLibraryTreeNode> node);

  std::shared_ptr<NAMLibraryTreeNode> mRootNode;
  std::vector<TreeItemUI> mDisplayList;
  int mSelectedIndex = -1;
  IVStyle mStyle;
  OnTreeModelSelectedFunc mOnModelSelected;
  
  // Mouse tracking for hover effects
  bool mMouseIsOver = false;
  float mMouseX = 0;
  float mMouseY = 0;

  // Scroll handling
  float mScrollOffset = 0.0f;
  float mMaxScroll = 0.0f;
  
  static constexpr float ITEM_HEIGHT = 26.0f;
  static constexpr float INDENT_PER_LEVEL = 24.0f;
  static constexpr float EXPAND_BUTTON_SIZE = 14.0f;
};