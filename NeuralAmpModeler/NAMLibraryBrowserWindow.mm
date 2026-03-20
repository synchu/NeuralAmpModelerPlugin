#include "NAMLibraryBrowserWindow.h"

#if defined(OS_MAC)

#import <Cocoa/Cocoa.h>
#import <CoreFoundation/CoreFoundation.h>
#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <unordered_set>
#include <vector>
#include <string>
#include <functional>

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

  static NSString* NodeCacheKey(const std::shared_ptr<NAMLibraryTreeNode>& node)
  {
    if (!node)
      return @"<null>";

    if (!node->id.empty())
      return [NSString stringWithUTF8String:node->id.c_str()];

    if (!node->path.empty())
      return [NSString stringWithUTF8String:node->path.c_str()];

    std::string fallback = node->name + "|" + std::to_string(node->depth);
    return [NSString stringWithUTF8String:fallback.c_str()];
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

@interface NAMOutlineDataSource : NSObject <NSOutlineViewDataSource>
@property (nonatomic) std::shared_ptr<NAMLibraryTreeNode> displayRoot;
@property (nonatomic, strong) NSMutableDictionary<NSString*, NAMNodeWrapper*>* wrapperCache;
- (void)resetWrapperCache;
- (NAMNodeWrapper*)wrapperForNode:(std::shared_ptr<NAMLibraryTreeNode>)node;
@end

@implementation NAMOutlineDataSource

- (instancetype)init
{
  self = [super init];
  if (self)
    self.wrapperCache = [NSMutableDictionary dictionary];
  return self;
}

- (void)resetWrapperCache
{
  [self.wrapperCache removeAllObjects];
}

- (NAMNodeWrapper*)wrapperForNode:(std::shared_ptr<NAMLibraryTreeNode>)node
{
  if (!node)
    return nil;

  NSString* key = NodeCacheKey(node);
  NAMNodeWrapper* wrapper = self.wrapperCache[key];
  if (!wrapper)
  {
    wrapper = [NAMNodeWrapper wrap:node];
    self.wrapperCache[key] = wrapper;
  }

  return wrapper;
}

- (NSInteger)outlineView:(NSOutlineView*)ov numberOfChildrenOfItem:(id)item
{
  (void) ov;

  if (!self.displayRoot)
    return 0;

  auto parent = item ? ((NAMNodeWrapper*) item).node : self.displayRoot;
  return parent ? (NSInteger) parent->children.size() : 0;
}

- (id)outlineView:(NSOutlineView*)ov child:(NSInteger)idx ofItem:(id)item
{
  (void) ov;

  auto parent = item ? ((NAMNodeWrapper*) item).node : self.displayRoot;
  if (!parent)
    return nil;

  if (idx < (NSInteger) parent->children.size())
    return [self wrapperForNode:parent->children[(size_t) idx]];

  return nil;
}

- (BOOL)outlineView:(NSOutlineView*)ov isItemExpandable:(id)item
{
  (void) ov;
  if (!item)
    return NO;

  auto node = ((NAMNodeWrapper*) item).node;
  return node ? !node->children.empty() : NO;
}

@end

using VoidFn = std::function<void()>;
using FilterFn = std::function<void(const std::string&, const std::string&)>;
using ExpandChangedFn = std::function<void(const std::shared_ptr<NAMLibraryTreeNode>&, bool)>;
using ShouldExpandFn = std::function<bool(const std::shared_ptr<NAMLibraryTreeNode>&)>;

@interface NAMLibraryWindowController : NSWindowController <NSWindowDelegate, NSOutlineViewDelegate>
@property (nonatomic, strong) NAMOutlineDataSource* dataSource;
@property (nonatomic, strong) NSOutlineView* outlineView;
@property (nonatomic, strong) NSButton* loadButton;
@property (nonatomic, strong) NSTextField* searchField;
@property (nonatomic, strong) NSPopUpButton* tagPopup;
@property (nonatomic, strong) NSButton* tagResetButton;
@property (nonatomic, strong) NSTextField* searchLabel;
@property (nonatomic, strong) NSTextField* tagLabel;
@property (nonatomic, strong) NSButton* fontIncButton;
@property (nonatomic, strong) NSButton* fontDecButton;
@property (nonatomic, strong) NSButton* cancelButton;
@property (nonatomic, strong) NSTimer* searchTimer;
@property (nonatomic) BOOL suppressFilterCallbacks;
@property (nonatomic) BOOL restoringExpansion;
@property (nonatomic) BOOL displayRootIsFiltered;
@property (nonatomic) int currentFontSize;

@property (nonatomic) VoidFn onLoad;
@property (nonatomic) VoidFn onCancel;
@property (nonatomic) FilterFn onFilterChanged;
@property (nonatomic) VoidFn onFontInc;
@property (nonatomic) VoidFn onFontDec;
@property (nonatomic) VoidFn onWindowClose;
@property (nonatomic) ExpandChangedFn onExpandedStateChanged;
@property (nonatomic) ShouldExpandFn shouldExpandNode;

- (instancetype)initWithFontSize:(int)fontSize;
- (void)setDisplayRoot:(std::shared_ptr<NAMLibraryTreeNode>)root;
- (std::shared_ptr<NAMLibraryTreeNode>)selectedNode;
- (void)setAvailableTags:(const std::vector<std::string>&)tags selectedTag:(const std::string&)selectedTag;
- (void)setSearchTextFromUtf8:(const std::string&)query;
- (std::string)currentQuery;
- (std::string)currentTag;
- (void)applyFontSize:(int)fontSize;
@end

@implementation NAMLibraryWindowController

- (NSTextField*)makeLabel:(NSString*)title
{
  NSTextField* label = [[NSTextField alloc] initWithFrame:NSZeroRect];
  label.translatesAutoresizingMaskIntoConstraints = NO;
  label.stringValue = title ? title : @"";
  label.bezeled = NO;
  label.drawsBackground = NO;
  label.editable = NO;
  label.selectable = NO;
  label.textColor = [NSColor colorWithCalibratedWhite:220.0/255.0 alpha:1.0];
  label.backgroundColor = [NSColor clearColor];
  return label;
}

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
  panel.delegate = self;

  if (@available(macOS 10.14, *))
    panel.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];

  self = [super initWithWindow:panel];
  if (!self)
    return nil;

  self.currentFontSize = fontSize;
  self.suppressFilterCallbacks = NO;
  self.restoringExpansion = NO;
  self.displayRootIsFiltered = NO;

  NSView* cv = panel.contentView;
  cv.wantsLayer = YES;
  cv.layer.backgroundColor = [NSColor colorWithCalibratedRed:30.0/255.0
                                                       green:30.0/255.0
                                                        blue:30.0/255.0
                                                       alpha:1.0].CGColor;

  self.searchLabel = [self makeLabel:@"Search:"];
  [cv addSubview:self.searchLabel];

  self.searchField = [NSTextField new];
  self.searchField.translatesAutoresizingMaskIntoConstraints = NO;
  self.searchField.placeholderString = @"Search models...";
  self.searchField.drawsBackground = YES;
  self.searchField.backgroundColor = [NSColor colorWithCalibratedWhite:40.0/255.0 alpha:1.0];
  self.searchField.textColor = [NSColor colorWithCalibratedWhite:220.0/255.0 alpha:1.0];
  self.searchField.bezelStyle = NSTextFieldRoundedBezel;
  [[NSNotificationCenter defaultCenter] addObserver:self
                                           selector:@selector(searchChanged:)
                                               name:NSControlTextDidChangeNotification
                                             object:self.searchField];
  [cv addSubview:self.searchField];

  self.tagLabel = [self makeLabel:@"Tag:"];
  [cv addSubview:self.tagLabel];

  self.tagPopup = [NSPopUpButton new];
  self.tagPopup.translatesAutoresizingMaskIntoConstraints = NO;
  self.tagPopup.target = self;
  self.tagPopup.action = @selector(tagChanged:);
  [cv addSubview:self.tagPopup];

  self.tagResetButton = [NSButton buttonWithTitle:@"X" target:self action:@selector(resetTag:)];
  self.tagResetButton.translatesAutoresizingMaskIntoConstraints = NO;
  [cv addSubview:self.tagResetButton];

  self.fontDecButton = [NSButton buttonWithTitle:@"A-" target:self action:@selector(fontDec:)];
  self.fontIncButton = [NSButton buttonWithTitle:@"A+" target:self action:@selector(fontInc:)];
  self.fontIncButton.translatesAutoresizingMaskIntoConstraints = NO;
  self.fontDecButton.translatesAutoresizingMaskIntoConstraints = NO;
  [cv addSubview:self.fontIncButton];
  [cv addSubview:self.fontDecButton];

  NSScrollView* scroll = [NSScrollView new];
  scroll.hasVerticalScroller = YES;
  scroll.autohidesScrollers = YES;
  scroll.translatesAutoresizingMaskIntoConstraints = NO;
  scroll.borderType = NSBezelBorder;
  scroll.drawsBackground = YES;
  scroll.backgroundColor = [NSColor colorWithCalibratedWhite:30.0/255.0 alpha:1.0];

  self.outlineView = [NSOutlineView new];
  self.outlineView.rowHeight = std::max((CGFloat)(fontSize + 4), (CGFloat)(fontSize * 1.3f));
  self.outlineView.allowsMultipleSelection = NO;
  self.outlineView.headerView = nil;
  self.outlineView.target = self;
  self.outlineView.doubleAction = @selector(doubleClicked:);
  self.outlineView.backgroundColor = [NSColor colorWithCalibratedWhite:30.0/255.0 alpha:1.0];
  self.outlineView.selectionHighlightStyle = NSTableViewSelectionHighlightStyleRegular;
  self.outlineView.focusRingType = NSFocusRingTypeNone;

  NSTableColumn* col = [[NSTableColumn alloc] initWithIdentifier:@"name"];
  col.editable = NO;
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

  self.cancelButton = [NSButton buttonWithTitle:@"Cancel" target:self action:@selector(cancelClicked:)];
  self.cancelButton.translatesAutoresizingMaskIntoConstraints = NO;
  [cv addSubview:self.cancelButton];

  NSDictionary* v = @{
    @"searchLabel": self.searchLabel,
    @"searchField": self.searchField,
    @"tagLabel": self.tagLabel,
    @"tagPopup": self.tagPopup,
    @"tagReset": self.tagResetButton,
    @"scroll": scroll,
    @"load": self.loadButton,
    @"cancel": self.cancelButton,
    @"fInc": self.fontIncButton,
    @"fDec": self.fontDecButton
  };

  [cv addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
    @"H:|-10-[searchLabel(60)]-6-[searchField]-6-[tagLabel(32)]-6-[tagPopup(170)]-6-[tagReset(34)]-10-[fDec(40)]-4-[fInc(40)]-10-|"
                                                                 options:NSLayoutFormatAlignAllCenterY
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
    @"V:|-10-[searchField(28)]-10-[scroll]-10-[load(30)]-10-|"
                                                                 options:0
                                                                 metrics:nil
                                                                   views:v]];

  [cv addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
    @"V:[cancel(30)]-10-|"
                                                                 options:0
                                                                 metrics:nil
                                                                   views:v]];

  [self applyFontSize:fontSize];

  return self;
}

- (void)applyFontSize:(int)fontSize
{
  self.currentFontSize = fontSize;

  NSFont* uiFont = [NSFont systemFontOfSize:fontSize];
  if (!uiFont)
    uiFont = [NSFont systemFontOfSize:13.0];

  self.searchLabel.font = uiFont;
  self.tagLabel.font = uiFont;
  self.searchField.font = uiFont;
  self.tagPopup.font = uiFont;
  self.tagResetButton.font = uiFont;
  self.fontIncButton.font = uiFont;
  self.fontDecButton.font = uiFont;
  self.loadButton.font = uiFont;
  self.cancelButton.font = uiFont;

  self.outlineView.rowHeight = std::max((CGFloat)(fontSize + 4), (CGFloat)(fontSize * 1.3f));
  [self.outlineView reloadData];
}

- (void)restoreExpansionStateForItem:(id)item
{
  NSInteger childCount = [self.outlineView numberOfChildrenOfItem:item];
  for (NSInteger i = 0; i < childCount; ++i)
  {
    id child = [self.outlineView child:i ofItem:item];
    if (!child)
      continue;

    auto node = ((NAMNodeWrapper*) child).node;
    if (node && !node->children.empty() && self.shouldExpandNode && self.shouldExpandNode(node))
      [self.outlineView expandItem:child];

    [self restoreExpansionStateForItem:child];
  }
}

- (void)expandAllItemsForNode:(std::shared_ptr<NAMLibraryTreeNode>)node
{
  if (!node)
    return;

  for (const auto& childNode : node->children)
  {
    if (!childNode)
      continue;

    if (!childNode->children.empty())
    {
      NAMNodeWrapper* wrapper = [self.dataSource wrapperForNode:childNode];
      if (wrapper)
        [self.outlineView expandItem:wrapper];

      [self expandAllItemsForNode:childNode];
    }
  }
}

- (void)setDisplayRoot:(std::shared_ptr<NAMLibraryTreeNode>)root
{
  self.dataSource.displayRoot = root;
  [self.dataSource resetWrapperCache];
  [self.outlineView reloadData];

  self.restoringExpansion = YES;

  if (self.displayRootIsFiltered)
  {
    [self expandAllItemsForNode:root];
  }
  else
  {
    [self restoreExpansionStateForItem:nil];
  }

  self.restoringExpansion = NO;

  NSInteger rows = [self.outlineView numberOfRows];
  if (rows > 0)
  {
    [self.outlineView selectRowIndexes:[NSIndexSet indexSetWithIndex:0] byExtendingSelection:NO];
    [self.outlineView scrollRowToVisible:0];
  }
  else
  {
    self.loadButton.enabled = NO;
  }
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
  self.suppressFilterCallbacks = YES;

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
  self.suppressFilterCallbacks = NO;
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
  if (self.suppressFilterCallbacks)
    return;

  if (self.onFilterChanged)
    self.onFilterChanged([self currentQuery], [self currentTag]);
}

- (void)searchChanged:(NSNotification*)n
{
  (void) n;

  [self.searchTimer invalidate];
  self.searchTimer = [NSTimer scheduledTimerWithTimeInterval:0.3
                                                      target:self
                                                    selector:@selector(debouncedFilterFire:)
                                                    userInfo:nil
                                                     repeats:NO];
}

- (void)debouncedFilterFire:(NSTimer*)timer
{
  (void) timer;
  self.searchTimer = nil;
  [self notifyFilterChanged];
}

- (void)tagChanged:(id)sender
{
  (void) sender;
  [self.searchTimer invalidate];
  self.searchTimer = nil;
  [self notifyFilterChanged];
}

- (void)resetTag:(id)sender
{
  (void) sender;
  self.suppressFilterCallbacks = YES;
  [self.tagPopup selectItemAtIndex:0];
  self.suppressFilterCallbacks = NO;

  [self.searchTimer invalidate];
  self.searchTimer = nil;

  if (self.onFilterChanged)
    self.onFilterChanged([self currentQuery], "");
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
  auto node = [self selectedNode];
  if (node && node->IsModel() && self.onLoad)
    self.onLoad();
}

- (void)loadClicked:(id)sender   { (void) sender; if (self.onLoad) self.onLoad(); }
- (void)cancelClicked:(id)sender { (void) sender; if (self.onCancel) self.onCancel(); }
- (void)fontInc:(id)sender       { (void) sender; if (self.onFontInc) self.onFontInc(); }
- (void)fontDec:(id)sender       { (void) sender; if (self.onFontDec) self.onFontDec(); }

- (void)windowWillClose:(NSNotification*)n
{
  (void) n;
  [self.searchTimer invalidate];
  self.searchTimer = nil;

  if (self.onWindowClose)
    self.onWindowClose();
}

- (void)outlineViewItemDidExpand:(NSNotification*)notification
{
  if (self.restoringExpansion || !self.onExpandedStateChanged)
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
  if (self.restoringExpansion || !self.onExpandedStateChanged)
    return;

  id item = notification.userInfo[@"NSObject"];
  if (!item)
    return;

  auto node = ((NAMNodeWrapper*) item).node;
  if (node)
    self.onExpandedStateChanged(node, false);
}

- (NSView*)outlineView:(NSOutlineView*)outlineView
    viewForTableColumn:(NSTableColumn*)tableColumn
                  item:(id)item
{
  (void) tableColumn;

  NSTableCellView* cell = [outlineView makeViewWithIdentifier:@"NAMCell" owner:self];
  if (!cell)
  {
    cell = [[NSTableCellView alloc] initWithFrame:NSMakeRect(0, 0, 100, 20)];
    cell.identifier = @"NAMCell";

    NSTextField* tf = [[NSTextField alloc] initWithFrame:cell.bounds];
    tf.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    tf.bordered = NO;
    tf.editable = NO;
    tf.selectable = NO;
    tf.drawsBackground = NO;
    tf.focusRingType = NSFocusRingTypeNone;
    cell.textField = tf;
    [cell addSubview:tf];
  }

  auto n = ((NAMNodeWrapper*) item).node;
  std::string label = n ? n->name : "";

  if (n && n->IsModel())
  {
    std::vector<std::string> metaParts;
    metaParts.reserve(3);

    if (!n->gear_make.empty() || !n->gear_model.empty())
    {
      std::string gear;
      if (!n->gear_make.empty() && !n->gear_model.empty())
        gear = n->gear_make + " " + n->gear_model;
      else if (!n->gear_make.empty())
        gear = n->gear_make;
      else
        gear = n->gear_model;

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

  cell.textField.stringValue = [NSString stringWithUTF8String:label.c_str()] ?: @"";
  cell.textField.textColor = [NSColor colorWithCalibratedWhite:220.0/255.0 alpha:1.0];
  cell.textField.font = [NSFont systemFontOfSize:self.currentFontSize];

  return cell;
}

- (BOOL)outlineView:(NSOutlineView*)outlineView shouldSelectItem:(id)item
{
  (void) outlineView;
  return item != nil;
}

- (void)dealloc
{
  [self.searchTimer invalidate];
  self.searchTimer = nil;
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

    ctrl.shouldExpandNode = [this](const std::shared_ptr<NAMLibraryTreeNode>& node) {
      return GetFolderExpandedFromState(node);
    };

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
          [c applyFontSize:mFontSize];
        SaveSettings();
      }
    };

    ctrl.onFontDec = [this, weak]() {
      if (mFontSize > mMinFontSize)
      {
        mFontSize -= 2;
        NAMLibraryWindowController* c = weak;
        if (c)
          [c applyFontSize:mFontSize];
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
        c.displayRootIsFiltered = NO;
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
        c.displayRootIsFiltered = NO;
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

      [c setAvailableTags:filteredTags selectedTag:selectedTagTrimmed];
      c.displayRootIsFiltered = (mSearchRoot != nullptr);
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
    {
      ctrl.displayRootIsFiltered = YES;
      ctrl.onFilterChanged(mPendingSearchQuery, mSelectedTag);
    }
    else
    {
      ctrl.displayRootIsFiltered = NO;
      [ctrl setDisplayRoot:mRootNode];
    }

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

void NAMLibraryBrowserWindow::SetInitialUIState(
  const std::string& searchQuery,
  const std::string& selectedTag,
  const std::unordered_map<std::string, bool>& expandedState)
{
  mPendingSearchQuery = searchQuery;
  mSelectedTag = selectedTag;
  mExpandedState = expandedState;
}

void NAMLibraryBrowserWindow::GetCurrentUIState(
  std::string& searchQuery,
  std::string& selectedTag,
  std::unordered_map<std::string, bool>& expandedState) const
{
  searchQuery = mPendingSearchQuery;
  selectedTag = mSelectedTag;
  expandedState = mExpandedState;
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

      if (mOnWindowClosed)
        mOnWindowClosed();

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