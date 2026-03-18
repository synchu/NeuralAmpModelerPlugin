#include "NAMLibraryBrowserWindow.h"

#if defined(OS_MAC)

#import <Cocoa/Cocoa.h>
#import <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <unordered_set>

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

  struct NAMLibraryBrowserSessionState
  {
    std::string lastSearchQuery;
    std::string lastSelectedTag;
    std::unordered_map<std::string, bool> expandedState;
  };

  NAMLibraryBrowserSessionState& GetBrowserSessionState()
  {
    static NAMLibraryBrowserSessionState s;
    return s;
  }
}

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

@interface NAMOutlineDataSource : NSObject <NSOutlineViewDataSource, NSOutlineViewDelegate>
@property (nonatomic) std::shared_ptr<NAMLibraryTreeNode> displayRoot;
@end

@implementation NAMOutlineDataSource

- (NSInteger)outlineView:(NSOutlineView*)ov numberOfChildrenOfItem:(id)item
{
  (void) ov;

  if (!self.displayRoot)
    return 0;

  auto parent = item ? ((NAMNodeWrapper*) item).node : self.displayRoot;
  return (NSInteger) parent->children.size();
}

- (id)outlineView:(NSOutlineView*)ov child:(NSInteger)idx ofItem:(id)item
{
  (void) ov;

  auto parent = item ? ((NAMNodeWrapper*) item).node : self.displayRoot;
  if (idx < (NSInteger) parent->children.size())
    return [NAMNodeWrapper wrap:parent->children[(size_t) idx]];

  return nil;
}

- (BOOL)outlineView:(NSOutlineView*)ov isItemExpandable:(id)item
{
  (void) ov;
  return !((NAMNodeWrapper*) item).node->children.empty();
}

- (id)outlineView:(NSOutlineView*)ov objectValueForTableColumn:(NSTableColumn*)col byItem:(id)item
{
  (void) ov;
  (void) col;

  auto n = ((NAMNodeWrapper*) item).node;
  std::string label = n->name;

  if (n->IsModel())
  {
    std::vector<std::string> metaParts;
    metaParts.reserve(3);

    if (!n->gear_make.empty() || !n->gear_model.empty())
    {
      std::string gear;
      if (!n->gear_make.empty() && !n->gear_model.empty())
        gear = n->gear_make + " " + n->gear_model;
      else
        gear = n->gear_make + n->gear_model;

      if (!gear.empty())
        metaParts.push_back(std::move(gear));
    }

    auto addLevel = [&](const char* prefix, double value) {
      if (value == 0.0)
        return;

      char buf[32] = {};
      snprintf(buf, sizeof(buf), "%s: %.1f", prefix, value);
      metaParts.emplace_back(buf);
    };

    addLevel("in", n->input_level_dbu);
    addLevel("out", n->output_level_dbu);

    if (!metaParts.empty())
    {
      label += " [";
      for (size_t i = 0; i < metaParts.size(); ++i)
      {
        if (i > 0)
          label += ", ";
        label += metaParts[i];
      }
      label += "]";
    }
  }

  return [NSString stringWithUTF8String:label.c_str()];
}

@end

using VoidFn = std::function<void()>;
using FilterFn = std::function<void(const std::string&, const std::string&)>;
using ExpandChangedFn = std::function<void(const std::shared_ptr<NAMLibraryTreeNode>&, bool)>;

@interface NAMLibraryWindowController : NSWindowController <NSWindowDelegate>
@property (nonatomic, strong) NAMOutlineDataSource* dataSource;
@property (nonatomic, strong) NSOutlineView* outlineView;
@property (nonatomic, strong) NSButton* loadButton;
@property (nonatomic, strong) NSTextField* searchField;
@property (nonatomic, strong) NSPopUpButton* tagPopup;
@property (nonatomic) VoidFn onLoad;
@property (nonatomic) VoidFn onCancel;
@property (nonatomic) FilterFn onFilterChanged;
@property (nonatomic) VoidFn onFontInc;
@property (nonatomic) VoidFn onFontDec;
@property (nonatomic) VoidFn onWindowClose;
@property (nonatomic) ExpandChangedFn onExpandedStateChanged;
- (instancetype)initWithFontSize:(int)fontSize;
- (void)setDisplayRoot:(std::shared_ptr<NAMLibraryTreeNode>)root;
- (std::shared_ptr<NAMLibraryTreeNode>)selectedNode;
- (void)setAvailableTags:(const std::vector<std::string>&)tags selectedTag:(const std::string&)selectedTag;
- (void)setSearchTextFromUtf8:(const std::string&)query;
- (std::string)currentQuery;
- (std::string)currentTag;
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

  panel.title = @"NAM Library Browser";
  panel.minSize = NSMakeSize(600, 400);

  self = [super initWithWindow:panel];
  if (!self)
    return nil;

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
  scroll.hasVerticalScroller = YES;
  scroll.autohidesScrollers = YES;
  scroll.translatesAutoresizingMaskIntoConstraints = NO;

  self.outlineView = [NSOutlineView new];
  self.outlineView.rowHeight = fontSize * 1.4f;
  self.outlineView.allowsMultipleSelection = NO;
  self.outlineView.headerView = nil;
  self.outlineView.target = self;
  self.outlineView.doubleAction = @selector(doubleClicked:);

  NSTableColumn* col = [[NSTableColumn alloc] initWithIdentifier:@"name"];
  [self.outlineView addTableColumn:col];
  self.outlineView.outlineTableColumn = col;

  self.dataSource = [NAMOutlineDataSource new];
  self.outlineView.dataSource = self.dataSource;
  self.outlineView.delegate = self;

  [[NSNotificationCenter defaultCenter] addObserver:self
                                           selector:@selector(selectionChanged:)
                                               name:NSOutlineViewSelectionDidChangeNotification
                                             object:self.outlineView];

  scroll.documentView = self.outlineView;
  [cv addSubview:scroll];

  self.loadButton = [NSButton buttonWithTitle:@"Load Selected Model" target:self action:@selector(loadClicked:)];
  self.loadButton.translatesAutoresizingMaskIntoConstraints = NO;
  self.loadButton.enabled = NO;
  [cv addSubview:self.loadButton];

  NSButton* cancel = [NSButton buttonWithTitle:@"Cancel" target:self action:@selector(cancelClicked:)];
  cancel.translatesAutoresizingMaskIntoConstraints = NO;
  [cv addSubview:cancel];

  NSDictionary* v = @{
    @"s": self.searchField,
    @"t": self.tagPopup,
    @"scroll": scroll,
    @"load": self.loadButton,
    @"cancel": cancel,
    @"fInc": fInc,
    @"fDec": fDec
  };

  [cv addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
    @"H:|-10-[s]-6-[t(170)]-6-[fDec(40)]-4-[fInc(40)]-10-|"
                                                                 options:0
                                                                 metrics:nil
                                                                   views:v]];
  [cv addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
    @"H:|-10-[scroll]-10-|"
                                                                 options:0
                                                                 metrics:nil
                                                                   views:v]];
  [cv addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
    @"H:[cancel(120)]-10-[load(210)]-10-|"
                                                                 options:0
                                                                 metrics:nil
                                                                   views:v]];
  [cv addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
    @"V:|-10-[s(28)]-6-[scroll]-6-[load(30)]-10-|"
                                                                 options:0
                                                                 metrics:nil
                                                                   views:v]];
  [cv addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
    @"V:|-10-[t(28)]"
                                                                 options:0
                                                                 metrics:nil
                                                                   views:v]];
  [cv addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
    @"V:|-10-[fInc(28)]"
                                                                 options:0
                                                                 metrics:nil
                                                                   views:v]];
  [cv addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
    @"V:|-10-[fDec(28)]"
                                                                 options:0
                                                                 metrics:nil
                                                                   views:v]];
  [cv addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
    @"V:[cancel(30)]-10-|"
                                                                 options:0
                                                                 metrics:nil
                                                                   views:v]];

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
  if (row < 0)
    return nullptr;

  id item = [self.outlineView itemAtRow:row];
  return item ? ((NAMNodeWrapper*) item).node : nullptr;
}

- (void)setAvailableTags:(const std::vector<std::string>&)tags selectedTag:(const std::string&)selectedTag
{
  [self.tagPopup removeAllItems];
  [self.tagPopup addItemWithTitle:@"All tags"];

  NSInteger selectedIndex = 0;
  NSInteger idx = 1;

  for (const auto& tag : tags)
  {
    if (tag.empty())
      continue;

    NSString* title = [NSString stringWithUTF8String:tag.c_str()];
    if (!title)
      continue;

    [self.tagPopup addItemWithTitle:title];

    if (!selectedTag.empty() && tag == selectedTag)
      selectedIndex = idx;

    ++idx;
  }

  [self.tagPopup selectItemAtIndex:selectedIndex];
}

- (void)setSearchTextFromUtf8:(const std::string&)query
{
  NSString* str = [NSString stringWithUTF8String:query.c_str()];
  self.searchField.stringValue = str ? str : @"";
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
  (void) n;
  [self notifyFilterChanged];
}

- (void)tagChanged:(id)sender
{
  (void) sender;
  [self notifyFilterChanged];
}

- (void)selectionChanged:(NSNotification*)n
{
  (void) n;
  auto node = [self selectedNode];
  self.loadButton.enabled = (node && node->IsModel()) ? YES : NO;
}

- (void)doubleClicked:(id)sender
{
  (void) sender;
  if ([self selectedNode] && [self selectedNode]->IsModel() && self.onLoad)
    self.onLoad();
}

- (void)loadClicked:(id)sender   { (void) sender; if (self.onLoad) self.onLoad(); }
- (void)cancelClicked:(id)sender { (void) sender; if (self.onCancel) self.onCancel(); }
- (void)fontInc:(id)sender       { (void) sender; if (self.onFontInc) self.onFontInc(); }
- (void)fontDec:(id)sender       { (void) sender; if (self.onFontDec) self.onFontDec(); }

- (void)windowWillClose:(NSNotification*)n
{
  (void) n;
  if (self.onWindowClose)
    self.onWindowClose();
}

- (void)outlineViewItemDidExpand:(NSNotification*)notification
{
  if (!self.onExpandedStateChanged)
    return;

  id item = notification.userInfo[@"NSObject"];
  if (!item)
    return;

  auto node = ((NAMNodeWrapper*) item).node;
  if (node)
    self.onExpandedStateChanged(node, true);
}

- (void)outlineViewItemDidCollapse:(NSNotification*)notification
{
  if (!self.onExpandedStateChanged)
    return;

  id item = notification.userInfo[@"NSObject"];
  if (!item)
    return;

  auto node = ((NAMNodeWrapper*) item).node;
  if (node)
    self.onExpandedStateChanged(node, false);
}

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
#if !__has_feature(objc_arc)
  [super dealloc];
#endif
}

@end

bool NAMLibraryBrowserWindow::GetFolderExpandedFromState(const std::shared_ptr<NAMLibraryTreeNode>& node) const
{
  if (!node)
    return false;

  if (node->children.empty())
    return false;

  if (!node->id.empty())
  {
    auto it = mExpandedState.find(node->id);
    if (it != mExpandedState.end())
      return it->second;
  }

  return node->expanded;
}

void NAMLibraryBrowserWindow::SetFolderExpandedInState(const std::shared_ptr<NAMLibraryTreeNode>& node, bool expanded)
{
  if (!node || node->children.empty() || node->id.empty())
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

std::string NAMLibraryBrowserWindow::GetSettingsFilePath()
{
  namespace fs = std::filesystem;

  std::string baseDir;
  if (const char* home = getenv("HOME"))
    baseDir = std::string(home) + "/Library/Application Support";

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
    catch (...)
    {
    }
  }

  auto& s = GetBrowserSessionState();
  mPendingSearchQuery = s.lastSearchQuery;
  mSelectedTag = s.lastSelectedTag;
  mExpandedState = s.expandedState;
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
      NSScreen* targetScreen = [NSScreen mainScreen];
      CGFloat screenTop = NSMaxY(targetScreen.frame);
      NSRect frame = NSMakeRect(mWindowX, screenTop - mWindowY - mWindowH, mWindowW, mWindowH);

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
      if (NSWindow* parentWin = ((__bridge NSView*) pParentWindow).window)
      {
        NSRect pf = parentWin.frame;
        [ctrl.window setFrame:NSMakeRect(pf.origin.x + pf.size.width + 10,
                                         pf.origin.y,
                                         mWindowW,
                                         mWindowH)
                      display:NO];
      }
    }

    [ctrl setSearchTextFromUtf8:mPendingSearchQuery];

#if __has_feature(objc_arc)
    __weak NAMLibraryWindowController* weak = ctrl;
#else
    NAMLibraryWindowController* weak = ctrl;
#endif

    ctrl.onLoad = [this, weak]() {
      NAMLibraryWindowController* c = weak;
      if (c)
      {
        auto node = [c selectedNode];
        if (node && node->IsModel() && mOnModelSelected)
        {
          mOnModelSelected(node);
          Close();
        }
      }
    };

    ctrl.onCancel = [this]() {
      Close();
    };

    ctrl.onFontInc = [this, weak]() {
      if (mFontSize < mMaxFontSize)
      {
        mFontSize += 2;
        NAMLibraryWindowController* c = weak;
        if (c)
          c.outlineView.rowHeight = mFontSize * 1.4f;
        SaveSettings();
      }
    };

    ctrl.onFontDec = [this, weak]() {
      if (mFontSize > mMinFontSize)
      {
        mFontSize -= 2;
        NAMLibraryWindowController* c = weak;
        if (c)
          c.outlineView.rowHeight = mFontSize * 1.4f;
        SaveSettings();
      }
    };

    ctrl.onWindowClose = [this]() {
      if (mIsOpen)
        Close();
    };

    ctrl.onExpandedStateChanged = [this](const std::shared_ptr<NAMLibraryTreeNode>& node, bool expanded) {
      SetFolderExpandedInState(node, expanded);
    };

    ctrl.onFilterChanged = [this, weak](const std::string& query, const std::string& selectedTag) {
      mPendingSearchQuery = query;
      mSelectedTag = selectedTag;

      NAMLibraryWindowController* c = weak;
      if (!c)
        return;

      const std::string queryTrimmed = Trim(query);
      const std::string selectedTagTrimmed = Trim(selectedTag);

      if (!mpLibraryManager)
      {
        mSearchRoot = nullptr;
        [c setDisplayRoot:mRootNode];
        return;
      }

      if (queryTrimmed.empty() && selectedTagTrimmed.empty())
      {
        mSearchRoot = nullptr;

        std::set<std::string> allTagSet;
        const auto& allModels = mpLibraryManager->GetAllModels();
        for (const auto& model : allModels)
        {
          if (!model)
            continue;

          for (const auto& tag : model->tags)
          {
            std::string trimmed = Trim(tag);
            if (!trimmed.empty())
              allTagSet.insert(trimmed);
          }
        }

        std::vector<std::string> allTags(allTagSet.begin(), allTagSet.end());
        [c setAvailableTags:allTags selectedTag:mSelectedTag];
        [c setDisplayRoot:mRootNode];
        return;
      }

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

      std::set<std::string> filteredTagSet;
      for (const auto& model : results)
      {
        if (!model)
          continue;

        for (const auto& tag : model->tags)
        {
          std::string trimmed = Trim(tag);
          if (!trimmed.empty())
            filteredTagSet.insert(trimmed);
        }
      }

      std::vector<std::string> filteredTags(filteredTagSet.begin(), filteredTagSet.end());

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

      SetExpandedStateRecursive(mSearchRoot, true);

      [c setAvailableTags:filteredTags selectedTag:selectedTagTrimmed];
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
        {
          std::string trimmed = Trim(tag);
          if (!trimmed.empty())
            tagSet.insert(trimmed);
        }
      }
    }

    std::vector<std::string> sortedTags(tagSet.begin(), tagSet.end());
    [ctrl setAvailableTags:sortedTags selectedTag:mSelectedTag];

    if (!mPendingSearchQuery.empty() || !mSelectedTag.empty())
      ctrl.onFilterChanged(mPendingSearchQuery, mSelectedTag);
    else
      [ctrl setDisplayRoot:mRootNode];

    [ctrl showWindow:nil];
    [ctrl.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

#if __has_feature(objc_arc)
    mpWindowController = (__bridge_retained void*) ctrl;
#else
    mpWindowController = (void*) [ctrl retain];
#endif

    mIsOpen = true;
  }
}

void NAMLibraryBrowserWindow::BringToFront()
{
  if (!mpWindowController)
    return;

  @autoreleasepool
  {
    NAMLibraryWindowController* ctrl = (__bridge NAMLibraryWindowController*) mpWindowController;
    if (!ctrl)
      return;

    [ctrl showWindow:nil];
    [ctrl.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
  }
}

void NAMLibraryBrowserWindow::Close()
{
  if (!mIsOpen)
    return;

  @autoreleasepool
  {
    NAMLibraryWindowController* ctrl =
#if __has_feature(objc_arc)
      (__bridge NAMLibraryWindowController*) mpWindowController;
#else
      (NAMLibraryWindowController*) mpWindowController;
#endif

    if (ctrl)
    {
      mPendingSearchQuery = [ctrl currentQuery];
      mSelectedTag = [ctrl currentTag];

      auto& s = GetBrowserSessionState();
      s.lastSearchQuery = mPendingSearchQuery;
      s.lastSelectedTag = mSelectedTag;
      s.expandedState = mExpandedState;

      NSRect frame = ctrl.window.frame;
      NSScreen* screen = ctrl.window.screen ?: [NSScreen mainScreen];
      CGFloat screenTop = NSMaxY(screen.frame);

      mWindowX = (int) frame.origin.x;
      mWindowY = (int) (screenTop - frame.origin.y - frame.size.height);
      mWindowW = (int) frame.size.width;
      mWindowH = (int) frame.size.height;
      mHasSavedBounds = true;

      ctrl.onWindowClose = nullptr;
      [ctrl close];

#if __has_feature(objc_arc)
      CFRelease((__bridge CFTypeRef) ctrl);
#else
      [ctrl release];
#endif

      mpWindowController = nullptr;
    }
  }

  SaveSettings();
  mIsOpen = false;
}

#else

// Intentionally empty on non-macOS builds.

#endif