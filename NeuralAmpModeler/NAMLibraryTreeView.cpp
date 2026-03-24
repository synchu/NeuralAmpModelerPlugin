#include "NAMLibraryTreeView.h"
#include "Colors.h"
#include <algorithm>

NAMLibraryTreeView::NAMLibraryTreeView(const IRECT& bounds, const IVStyle& style)
: IContainerBase(bounds)
, mStyle(style)
, mSelectedIndex(-1)
{
}

void NAMLibraryTreeView::SetRootNode(std::shared_ptr<NAMLibraryTreeNode> root)
{
  mRootNode = root;
  RefreshTree();
}

void NAMLibraryTreeView::SetOnModelSelected(OnTreeModelSelectedFunc func)
{
  mOnModelSelected = func;
}

void NAMLibraryTreeView::OnAttached()
{
  RefreshTree();
}

void NAMLibraryTreeView::RefreshTree()
{
  mDisplayList.clear();
  mSelectedIndex = -1;
  
  if (!mRootNode)
    return;
  
  float yPos = 0;
  BuildDisplayList(mRootNode, mDisplayList, yPos);
  
  // Calculate max scroll
  float totalHeight = yPos;
  float visibleHeight = mRECT.H();
  mMaxScroll = std::max(0.0f, totalHeight - visibleHeight);
  
  SetDirty(true);
}

void NAMLibraryTreeView::BuildDisplayList(
  std::shared_ptr<NAMLibraryTreeNode> node,
  std::vector<TreeItemUI>& displayList,
  float& yPos)
{
  if (!node)
    return;

  if (node->depth > 0)
  {
    TreeItemUI item;
    item.node = node;
    item.displayIndex = static_cast<int>(displayList.size());
    item.yPos = yPos;
    displayList.push_back(item);
    yPos += ITEM_HEIGHT;
  }

  if (node->expanded)
  {
    for (const auto& child : node->children)
    {
      BuildDisplayList(child, displayList, yPos);
    }
  }
}

void NAMLibraryTreeView::Draw(IGraphics& g)
{
  // Background
  g.FillRect(COLOR_BLACK.WithOpacity(0.9f), mRECT);
  g.DrawRect(COLOR_GRAY.WithOpacity(0.5f), mRECT, nullptr, 2.0f);

  // Clip to bounds for scrolling support
  g.PathClipRegion(mRECT);

  for (size_t i = 0; i < mDisplayList.size(); ++i)
  {
    bool isSelected = (static_cast<int>(i) == mSelectedIndex);
    
    // Adjust Y position by scroll offset
    auto item = mDisplayList[i];
    item.yPos -= mScrollOffset;
    
    // Only draw items that are visible
    if (item.yPos + ITEM_HEIGHT >= 0 && item.yPos <= mRECT.H())
    {
      DrawItem(g, item, isSelected);
    }
  }
  
  g.PathClipRegion(); // Reset clipping
}

void NAMLibraryTreeView::DrawItem(IGraphics& g, const TreeItemUI& item, bool isSelected)
{
  // Calculate indentation based on depth
  float baseIndent = 10.0f;
  float xPos = mRECT.L + baseIndent + (item.node->depth * INDENT_PER_LEVEL);
  IRECT itemBounds(mRECT.L, mRECT.T + item.yPos, mRECT.R, mRECT.T + item.yPos + ITEM_HEIGHT);

  if (itemBounds.B > mRECT.B || itemBounds.T < mRECT.T)
    return;

  // Draw selection highlight
  if (isSelected)
  {
    g.FillRect(PluginColors::NAM_THEMECOLOR.WithOpacity(0.4f), itemBounds);
  }
  
  // Draw hover highlight
  if (mMouseIsOver && itemBounds.Contains(mMouseX, mMouseY))
  {
    g.FillRect(PluginColors::MOUSEOVER.WithOpacity(0.2f), itemBounds);
  }

  // Draw depth guide lines to show hierarchy
  if (item.node->depth > 1)
  {
    for (int d = 1; d < item.node->depth; d++)
    {
      float lineX = mRECT.L + baseIndent + (d * INDENT_PER_LEVEL) + 8.0f;
      g.DrawLine(COLOR_GRAY.WithOpacity(0.3f), lineX, itemBounds.T, lineX, itemBounds.B, nullptr, 1.0f);
    }
  }

  // Draw expand/collapse button for folders
  if (item.node->IsFolder() && !item.node->children.empty())
  {
    IRECT expandBounds(xPos, mRECT.T + item.yPos + (ITEM_HEIGHT - EXPAND_BUTTON_SIZE) / 2.0f, 
                       xPos + EXPAND_BUTTON_SIZE, mRECT.T + item.yPos + (ITEM_HEIGHT + EXPAND_BUTTON_SIZE) / 2.0f);
    
    // Draw expand/collapse triangle
    float midX = expandBounds.MW();
    float midY = expandBounds.MH();
    
    IColor expandColor = COLOR_WHITE.WithOpacity(0.8f);
    
    if (item.node->expanded)
    {
      // Down-pointing triangle (expanded)
      g.PathTriangle(expandBounds.L + 3, expandBounds.T + 4,
                     expandBounds.R - 3, expandBounds.T + 4,
                     midX, expandBounds.B - 4);
      g.PathFill(expandColor);
    }
    else
    {
      // Right-pointing triangle (collapsed)
      g.PathTriangle(expandBounds.L + 4, expandBounds.T + 3,
                     expandBounds.L + 4, expandBounds.B - 3,
                     expandBounds.R - 4, midY);
      g.PathFill(expandColor);
    }
    
    xPos += EXPAND_BUTTON_SIZE + 5.0f;
  }
  else if (item.node->IsFolder())
  {
    xPos += EXPAND_BUTTON_SIZE + 5.0f;
  }

  // Draw folder/model icon
  const float iconSize = 14.0f;
  IRECT iconRect(xPos, mRECT.T + item.yPos + (ITEM_HEIGHT - iconSize) / 2.0f,
                 xPos + iconSize, mRECT.T + item.yPos + (ITEM_HEIGHT + iconSize) / 2.0f);
  
  if (item.node->IsFolder())
  {
    // Folder icon
    IColor folderColor = item.node->expanded ? 
      PluginColors::NAM_THEMECOLOR.WithOpacity(0.8f) : 
      COLOR_GRAY.WithOpacity(0.6f);
    g.FillRect(folderColor, iconRect.GetPadded(-2.0f));
    g.DrawRect(COLOR_WHITE.WithOpacity(0.5f), iconRect.GetPadded(-2.0f));
  }
  else
  {
    // Model icon (small circle)
    float centerX = iconRect.MW();
    float centerY = iconRect.MH();
    g.FillCircle(PluginColors::NAM_THEMECOLOR, centerX, centerY, 4.0f);
    g.DrawCircle(COLOR_WHITE.WithOpacity(0.8f), centerX, centerY, 4.0f);
  }

  xPos += iconSize + 8.0f;

  // Draw text with proper truncation
  IRECT textBounds(xPos, mRECT.T + item.yPos, mRECT.R - 10, mRECT.T + item.yPos + ITEM_HEIGHT);
  
  IColor textColor = item.node->IsFolder() ? 
    COLOR_WHITE : 
    COLOR_WHITE.WithOpacity(0.9f);
  
  IText textStyle(13, textColor, "Roboto-Regular", EAlign::Near, EVAlign::Middle, 0);
  
  std::string displayName = item.node->GetDisplayName();
  g.DrawText(textStyle, displayName.c_str(), textBounds);
  
  // Show metadata for models on hover
  if (!item.node->IsFolder() && mMouseIsOver && itemBounds.Contains(mMouseX, mMouseY))
  {
    std::string metadata;
    if (!item.node->gear_make.empty() || !item.node->gear_model.empty())
    {
      metadata = item.node->gear_make + " " + item.node->gear_model;
      IRECT metadataRect = textBounds.GetVShifted(2.0f);
      IText metadataStyle(10, COLOR_LIGHT_GRAY, "Roboto-Regular", EAlign::Near, EVAlign::Middle, 0);
      // This would need to be rendered in a separate line, but we're limited by ITEM_HEIGHT
    }
  }
}

int NAMLibraryTreeView::GetItemAtY(float y) const
{
  if (y < mRECT.T || y > mRECT.B)
    return -1;

  float relativeY = y - mRECT.T + mScrollOffset;
  
  // Linear search through display list
  for (size_t i = 0; i < mDisplayList.size(); ++i)
  {
    float itemTop = mDisplayList[i].yPos;
    float itemBottom = itemTop + ITEM_HEIGHT;
    
    if (relativeY >= itemTop && relativeY < itemBottom)
      return static_cast<int>(i);
  }

  return -1;
}

void NAMLibraryTreeView::OnMouseDown(float x, float y, const IMouseMod& mod)
{
  int itemIndex = GetItemAtY(y);
  if (itemIndex >= 0)
  {
    OnItemClick(itemIndex);
  }
  IContainerBase::OnMouseDown(x, y, mod);
}

void NAMLibraryTreeView::OnMouseDblClick(float x, float y, const IMouseMod& mod)
{
  int itemIndex = GetItemAtY(y);
  if (itemIndex >= 0)
  {
    OnItemDoubleClick(itemIndex);
  }
  IContainerBase::OnMouseDblClick(x, y, mod);
}

void NAMLibraryTreeView::OnMouseOver(float x, float y, const IMouseMod& mod)
{
  mMouseIsOver = true;
  mMouseX = x;
  mMouseY = y;
  SetDirty(false);
  IContainerBase::OnMouseOver(x, y, mod);
}

void NAMLibraryTreeView::OnMouseOut()
{
  mMouseIsOver = false;
  SetDirty(false);
  IContainerBase::OnMouseOut();
}

void NAMLibraryTreeView::OnItemClick(int itemIndex)
{
  if (itemIndex < 0 || itemIndex >= static_cast<int>(mDisplayList.size()))
    return;

  mSelectedIndex = itemIndex;
  const auto& item = mDisplayList[itemIndex];

  if (item.node->IsFolder())
  {
    ToggleFolder(item.node);
    RefreshTree();
  }

  SetDirty(true);
}

void NAMLibraryTreeView::OnItemDoubleClick(int itemIndex)
{
  if (itemIndex < 0 || itemIndex >= static_cast<int>(mDisplayList.size()))
    return;

  const auto& item = mDisplayList[itemIndex];

  if (item.node->IsModel())
  {
    if (mOnModelSelected)
    {
      mOnModelSelected(item.node);
    }
  }
  else if (item.node->IsFolder())
  {
    // Double-click on folder also toggles it
    ToggleFolder(item.node);
    RefreshTree();
  }
}

void NAMLibraryTreeView::ToggleFolder(std::shared_ptr<NAMLibraryTreeNode> node)
{
  if (node && node->IsFolder())
  {
    node->expanded = !node->expanded;
  }
}

void NAMLibraryTreeView::OnMouseWheel(float x, float y, const IMouseMod& mod, float d)
{
  // Scroll the tree view
  const float scrollSpeed = 40.0f;
  mScrollOffset += d * scrollSpeed;
  
  // Clamp scroll offset
  mScrollOffset = std::max(0.0f, std::min(mScrollOffset, mMaxScroll));
  
  SetDirty(false);
  IContainerBase::OnMouseWheel(x, y, mod, d);
}