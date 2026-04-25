#include "NAMPNAMEditorWindow.h"

#if defined(OS_MAC)

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cstdlib>

#include "json.hpp"
using json = nlohmann::json;

// ============================================================
//  Helpers
// ============================================================
namespace
{
  NSString* ToNS(const std::string& s)
  {
    return [NSString stringWithUTF8String:s.c_str()];
  }
  std::string FromNS(NSString* s)
  {
    return s ? std::string(s.UTF8String) : std::string{};
  }
  std::string DoubleStr(double v)
  {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << v;
    return ss.str();
  }
  double ParseDouble(NSTextField* tf, double fallback)
  {
    try { return std::stod(FromNS(tf.stringValue)); }
    catch (...) { return fallback; }
  }
  NSColor* kDarkBg()   { return [NSColor colorWithCalibratedRed:30/255.f green:30/255.f blue:30/255.f alpha:1]; }
  NSColor* kEditBg()   { return [NSColor colorWithCalibratedRed:40/255.f green:40/255.f blue:40/255.f alpha:1]; }
  NSColor* kTextClr()  { return [NSColor colorWithCalibratedWhite:220/255.f alpha:1]; }
  NSColor* kBtnBg()    { return [NSColor colorWithCalibratedRed:60/255.f green:60/255.f blue:60/255.f alpha:1]; }
  NSColor* kAccent()   { return [NSColor colorWithCalibratedRed:0/255.f green:120/255.f blue:215/255.f alpha:1]; }

  std::string CheckSlotOverlaps(const std::vector<ModelMapSlot>& slots)
  {
    std::string warn;
    for (int i = 0; i < (int)slots.size(); ++i)
      for (int j = i + 1; j < (int)slots.size(); ++j)
        if (slots[i].ampGainMax > slots[j].ampGainMin && slots[j].ampGainMax > slots[i].ampGainMin)
          warn += "Slot " + std::to_string(i + 1) + " and slot " + std::to_string(j + 1) + " overlap.\n";
    return warn;
  }

  // Convert absolute path to portable relative path (relative to a base directory).
  // Uses forward slashes so the file is cross-platform readable.
  std::string MakePortablePath(const std::string& absPath, const std::string& baseDir)
  {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path rel = fs::relative(fs::path(absPath), fs::path(baseDir), ec);
    if (ec || rel.empty()) return absPath; // fallback: store as-is
    // Normalise to forward slashes
    std::string s = rel.generic_string();
    return s;
  }

  // Resolve a stored path (possibly relative with forward slashes) against a base directory.
  std::string ResolvePath(const std::string& stored, const std::string& baseDir)
  {
    if (stored.empty()) return stored;
    namespace fs = std::filesystem;
    fs::path p(stored); // generic_string forward-slash path parses fine on all platforms
    if (p.is_absolute()) return stored;
    fs::path resolved = fs::path(baseDir) / p;
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(resolved, ec);
    return ec ? resolved.string() : canonical.string();
  }
}

// ============================================================
//  Flipped view for top-down layout in scroll views
// ============================================================
@interface NSFlippedView : NSView
@end

@implementation NSFlippedView
- (BOOL)isFlipped { return YES; }
- (instancetype)initWithFrame:(NSRect)frame {
  self = [super initWithFrame:frame];
  if (self) self.translatesAutoresizingMaskIntoConstraints = NO;
  return self;
}
@end

// ============================================================
//  Forward declaration
// ============================================================
@class NAMPNAMEditorController;


// ============================================================
//  Helper: styled controls
// ============================================================
static NSTextField* MakeLabel(NSString* text)
{
  NSTextField* tf = [NSTextField labelWithString:text];
  tf.translatesAutoresizingMaskIntoConstraints = NO;
  tf.textColor = kTextClr();
  tf.backgroundColor = [NSColor clearColor];
  tf.drawsBackground = NO;
  return tf;
}

static NSTextField* MakeEdit(NSString* placeholder)
{
  NSTextField* tf = [NSTextField new];
  tf.translatesAutoresizingMaskIntoConstraints = NO;
  tf.placeholderString = placeholder;
  tf.drawsBackground = YES;
  tf.backgroundColor = kEditBg();
  tf.textColor = kTextClr();
  tf.bezelStyle = NSTextFieldSquareBezel;
  tf.bordered = YES;
  return tf;
}

static NSButton* MakeButton(NSString* title, id target, SEL action)
{
  NSButton* btn = [NSButton buttonWithTitle:title target:target action:action];
  btn.translatesAutoresizingMaskIntoConstraints = NO;
  btn.bezelStyle = NSBezelStyleRounded;
  btn.wantsLayer = YES;
  btn.layer.backgroundColor = kBtnBg().CGColor;
  btn.layer.cornerRadius = 4.0;
  btn.contentTintColor = kTextClr();
  return btn;
}

static NSButton* MakeCheckbox(NSString* title, id target, SEL action)
{
  NSButton* cb = [NSButton checkboxWithTitle:title target:target action:action];
  cb.translatesAutoresizingMaskIntoConstraints = NO;
  cb.contentTintColor = kTextClr();
  return cb;
}

// ============================================================
//  Window controller
// ============================================================
@interface NAMPNAMEditorController : NSWindowController
    <NSWindowDelegate, NSTableViewDataSource, NSTableViewDelegate, NSDraggingDestination>

// Callbacks to C++ side
@property (nonatomic, assign) std::function<void()>                         onWindowClosed;
@property (nonatomic, assign) std::function<void(const std::string&)>       onSaved;
@property (nonatomic, assign) std::function<void(const ModelMapSlot&)>      onPreviewSlot;
@property (nonatomic, assign) std::function<void(const std::string&)>       onPreviewChain;

// Shared state handed in from C++
@property (nonatomic) std::vector<ModelMapSlot>* slots;
@property (nonatomic) std::string*               currentFilePath;
@property (nonatomic) bool*                      dirty;

// Toolbar
@property (nonatomic, strong) NSTextField*  filePathLabel;
@property (nonatomic, strong) NSButton*     newBtn;
@property (nonatomic, strong) NSButton*     openBtn;
@property (nonatomic, strong) NSButton*     saveBtn;
@property (nonatomic, strong) NSButton*     saveAsBtn;
@property (nonatomic, strong) NSButton*     fontDecBtn;
@property (nonatomic, strong) NSButton*     fontIncBtn;
@property (nonatomic, strong) NSButton*     closeBtn;

@property (nonatomic, strong) NSTableView*  slotTable;
@property (nonatomic, strong) NSButton*     addBtn;
@property (nonatomic, strong) NSButton*     removeBtn;
@property (nonatomic, strong) NSButton*     upBtn;
@property (nonatomic, strong) NSButton*     downBtn;
@property (nonatomic, strong) NSButton*     distributeBtn;

// Right panel
@property (nonatomic, strong) NSTextField*  gainMinEdit;
@property (nonatomic, strong) NSTextField*  gainMaxEdit;
@property (nonatomic, strong) NSTextField*  namPathEdit;
@property (nonatomic, strong) NSButton*     browseBtn;
@property (nonatomic, strong) NSButton*     ovOutputCheck;
@property (nonatomic, strong) NSTextField*  ovOutputEdit;
@property (nonatomic, strong) NSButton*     ovBassCheck;
@property (nonatomic, strong) NSTextField*  ovBassEdit;
@property (nonatomic, strong) NSButton*     ovMidCheck;
@property (nonatomic, strong) NSTextField*  ovMidEdit;
@property (nonatomic, strong) NSButton*     ovTrebleCheck;
@property (nonatomic, strong) NSTextField*  ovTrebleEdit;
@property (nonatomic, strong) NSButton*     applyBtn;
@property (nonatomic, strong) NSButton*     previewSlotBtn;
@property (nonatomic, strong) NSButton*     previewChainBtn;

@property (nonatomic) int  currentFontSize;
@property (nonatomic) int* fontSizePtr;      // ← add this line

- (instancetype)initWithSlots:(std::vector<ModelMapSlot>*)slots
               currentFilePath:(std::string*)filePath
                         dirty:(bool*)dirty
                      fontSize:(int)fontSize
                  fontSizePtr:(int*)fontSizePtr;   // ← add this

- (void)refreshSlotTable;
- (void)selectSlotIndex:(int)idx;
- (void)updateEditPanelFromSlot:(int)idx;
- (void)setEditPanelEnabled:(BOOL)enabled;
- (void)updateTitleBar;
- (void)onSaveAsSkipOverlapCheck:(id)s;
- (ModelMapSlot)readEditPanelToSlot;

@end

// ============================================================
@implementation NAMPNAMEditorController

- (instancetype)initWithSlots:(std::vector<ModelMapSlot>*)slots
               currentFilePath:(std::string*)filePath
                         dirty:(bool*)dirty
                      fontSize:(int)fontSize
                  fontSizePtr:(int*)fontSizePtr
{
  NSWindow* win = [[NSWindow alloc]
    initWithContentRect:NSMakeRect(0, 0, 920, 700)
              styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                        NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                backing:NSBackingStoreBuffered
                  defer:YES];

  win.title = @"PNAM Chain Editor";
  win.minSize = NSMakeSize(750, 580);
  win.releasedWhenClosed = NO;
  if (@available(macOS 10.14, *))
    win.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
  // Do NOT set NSFloatingWindowLevel — it hides close button and keeps window on top of all apps
  [win setCollectionBehavior:NSWindowCollectionBehaviorMoveToActiveSpace];

  self = [super initWithWindow:win];
  if (!self) return nil;

  self.slots = slots;
  self.currentFilePath = filePath;
  self.dirty = dirty;
  self.currentFontSize = fontSize;
  self.fontSizePtr = fontSizePtr;   // ← add this line

  NSView* cv = win.contentView;
  cv.wantsLayer = YES;
  cv.layer.backgroundColor = kDarkBg().CGColor;

  [self buildUI:cv];
  [self updateTitleBar];
  [self setEditPanelEnabled:NO];

  // Accept .nam drops
  [win registerForDraggedTypes:@[NSFilenamesPboardType]];
  win.delegate = self;

  return self;
}

// ============================================================
//  UI Construction
// ============================================================
- (void)buildUI:(NSView*)cv
{
  const int fs = self.currentFontSize;
  NSFont* font = [NSFont systemFontOfSize:fs];
  NSFont* boldFont = [NSFont boldSystemFontOfSize:fs];

  // ---- Toolbar row ----
  _filePathLabel = MakeLabel(@"(new file)");
  _filePathLabel.font = font;
  _filePathLabel.lineBreakMode = NSLineBreakByTruncatingMiddle;
  [_filePathLabel setContentCompressionResistancePriority:200 forOrientation:NSLayoutConstraintOrientationHorizontal];
  [cv addSubview:_filePathLabel];

  _newBtn     = MakeButton(@"New",        self, @selector(onNew:));
  _openBtn    = MakeButton(@"Open…",      self, @selector(onOpen:));
  _saveBtn    = MakeButton(@"Save",       self, @selector(onSave:));
  _saveAsBtn  = MakeButton(@"Save As…",   self, @selector(onSaveAs:));
  _fontDecBtn = MakeButton(@"A−",         self, @selector(onFontDec:));
  _fontIncBtn = MakeButton(@"A+",         self, @selector(onFontInc:));
  _closeBtn    = MakeButton(@"Close",       self, @selector(onClose:));
  for (NSButton* b in @[_newBtn, _openBtn, _saveBtn, _saveAsBtn, _fontDecBtn, _fontIncBtn, _closeBtn])
  {
    b.font = font;
    [cv addSubview:b];
  }

  // ---- Splitter ----
  NSSplitView* split = [NSSplitView new];
  split.translatesAutoresizingMaskIntoConstraints = NO;
  split.vertical = YES;
  split.dividerStyle = NSSplitViewDividerStyleThin;
  [cv addSubview:split];

  // ---- Left panel ----
  NSView* leftPanel = [NSView new];
  leftPanel.translatesAutoresizingMaskIntoConstraints = NO;

  NSScrollView* scroll = [NSScrollView new];
  scroll.translatesAutoresizingMaskIntoConstraints = NO;
  scroll.hasVerticalScroller = YES;
  scroll.autohidesScrollers = YES;
  scroll.drawsBackground = YES;
  scroll.backgroundColor = kDarkBg();
  scroll.borderType = NSBezelBorder;

  _slotTable = [NSTableView new];
  _slotTable.dataSource = self;
  _slotTable.delegate = self;
  _slotTable.backgroundColor = kDarkBg();
  _slotTable.rowHeight = fs + 10;
  _slotTable.usesAlternatingRowBackgroundColors = NO;
  _slotTable.gridStyleMask = NSTableViewSolidHorizontalGridLineMask | NSTableViewSolidVerticalGridLineMask;
  _slotTable.gridColor = [NSColor colorWithCalibratedWhite:80/255.f alpha:1];
  _slotTable.allowsMultipleSelection = YES;
  _slotTable.intercellSpacing = NSMakeSize(6, 4);
  _slotTable.target = self;
  _slotTable.action = @selector(slotTableClicked:);
  [_slotTable registerForDraggedTypes:@[NSFilenamesPboardType]];

  // Style the header
  _slotTable.headerView.wantsLayer = YES;

  for (NSArray* colDef in @[@[@"#", @"36"], @[@"Min", @"64"], @[@"Max", @"64"], @[@"Model", @"200"]])
  {
    NSTableColumn* col = [[NSTableColumn alloc] initWithIdentifier:colDef[0]];
    col.title = colDef[0];
    col.width = [colDef[1] floatValue];
    col.minWidth = [colDef[1] floatValue] * 0.6;
    col.editable = NO;
    // Last column expands
    if ([colDef[0] isEqualToString:@"Model"])
      col.resizingMask = NSTableColumnAutoresizingMask;
    else
      col.resizingMask = NSTableColumnUserResizingMask;
    [_slotTable addTableColumn:col];
  }

  scroll.documentView = _slotTable;
  [leftPanel addSubview:scroll];

  _addBtn        = MakeButton(@"+ Add",                  self, @selector(onAddSlot:));
  _removeBtn     = MakeButton(@"− Remove",               self, @selector(onRemoveSlot:));
  _upBtn         = MakeButton(@"▲ Up",                   self, @selector(onMoveUp:));
  _downBtn       = MakeButton(@"▼ Down",                 self, @selector(onMoveDown:));
  _distributeBtn = MakeButton(@"Distribute Gain 0→10",   self, @selector(onDistribute:));
  for (NSButton* b in @[_addBtn, _removeBtn, _upBtn, _downBtn, _distributeBtn])
  {
    b.font = font;
    [leftPanel addSubview:b];
  }

  // Left layout
  [NSLayoutConstraint activateConstraints:@[
    [scroll.topAnchor constraintEqualToAnchor:leftPanel.topAnchor],
    [scroll.leadingAnchor constraintEqualToAnchor:leftPanel.leadingAnchor],
    [scroll.trailingAnchor constraintEqualToAnchor:leftPanel.trailingAnchor],

    [_addBtn.topAnchor constraintEqualToAnchor:scroll.bottomAnchor constant:6],
    [_addBtn.leadingAnchor constraintEqualToAnchor:leftPanel.leadingAnchor],
    [_removeBtn.leadingAnchor constraintEqualToAnchor:_addBtn.trailingAnchor constant:4],
    [_removeBtn.centerYAnchor constraintEqualToAnchor:_addBtn.centerYAnchor],
    [_upBtn.leadingAnchor constraintEqualToAnchor:_removeBtn.trailingAnchor constant:4],  // comma added
    [_upBtn.centerYAnchor constraintEqualToAnchor:_addBtn.centerYAnchor],
    [_downBtn.leadingAnchor constraintEqualToAnchor:_upBtn.trailingAnchor constant:4],
    [_downBtn.centerYAnchor constraintEqualToAnchor:_addBtn.centerYAnchor],

    [_distributeBtn.topAnchor constraintEqualToAnchor:_addBtn.bottomAnchor constant:6],
    [_distributeBtn.leadingAnchor constraintEqualToAnchor:leftPanel.leadingAnchor],
    [_distributeBtn.trailingAnchor constraintEqualToAnchor:leftPanel.trailingAnchor],
    [_distributeBtn.bottomAnchor constraintEqualToAnchor:leftPanel.bottomAnchor constant:-8],
  ]];

  // ---- Right panel (scrollable) ----
  NSScrollView* rightScroll = [NSScrollView new];
  rightScroll.translatesAutoresizingMaskIntoConstraints = NO;
  rightScroll.hasVerticalScroller = YES;
  rightScroll.autohidesScrollers = YES;
  rightScroll.drawsBackground = YES;
  rightScroll.backgroundColor = kDarkBg();
  rightScroll.borderType = NSNoBorder;

  // Use a flipped view so content lays out top-down
  NSView* rContent = [[NSFlippedView alloc] initWithFrame:NSZeroRect];
  rContent.translatesAutoresizingMaskIntoConstraints = NO;
  rightScroll.documentView = rContent;

  // Pin rContent edges to the scroll view's clip view so it sizes properly
  NSClipView* clipView = rightScroll.contentView;
  [NSLayoutConstraint activateConstraints:@[
    [rContent.topAnchor constraintEqualToAnchor:clipView.topAnchor],
    [rContent.leadingAnchor constraintEqualToAnchor:clipView.leadingAnchor],
    [rContent.trailingAnchor constraintEqualToAnchor:clipView.trailingAnchor],
    // Don't pin bottom — let content define its own height for scrolling
  ]];

  // Create all edit-panel controls
  _gainMinEdit   = MakeEdit(@"0.00");
  _gainMaxEdit   = MakeEdit(@"10.00");
  _namPathEdit   = MakeEdit(@"");
  _browseBtn     = MakeButton(@"Browse…", self, @selector(onBrowseNAM:));
  _ovOutputCheck = MakeCheckbox(@"Output Level (dB):", self, @selector(onOverrideToggle:));
  _ovOutputEdit  = MakeEdit(@"0.00");
  _ovBassCheck   = MakeCheckbox(@"Bass (0–10):", self, @selector(onOverrideToggle:));
  _ovBassEdit    = MakeEdit(@"5.00");
  _ovMidCheck    = MakeCheckbox(@"Mid (0–10):", self, @selector(onOverrideToggle:));
  _ovMidEdit     = MakeEdit(@"5.00");
  _ovTrebleCheck = MakeCheckbox(@"Treble (0–10):", self, @selector(onOverrideToggle:));
  _ovTrebleEdit  = MakeEdit(@"5.00");
  _applyBtn      = MakeButton(@"Apply to Slot ✓", self, @selector(onApplySlot:));
  _previewSlotBtn  = MakeButton(@"▶ Preview Slot",  self, @selector(onPreviewSlot:));
  _previewChainBtn = MakeButton(@"▶ Preview Chain", self, @selector(onPreviewChain:));

  // Section labels
  NSTextField* selLbl     = MakeLabel(@"Selected Slot");        selLbl.font = boldFont;
  NSTextField* gainMinLbl = MakeLabel(@"Gain Min (0–10):");     gainMinLbl.font = font;
  NSTextField* gainMaxLbl = MakeLabel(@"Gain Max (0–10):");     gainMaxLbl.font = font;
  NSTextField* namLbl     = MakeLabel(@"NAM File:");            namLbl.font = font;
  NSTextField* ovLbl      = MakeLabel(@"Overrides (optional)"); ovLbl.font = boldFont;

  // Separator lines
  NSBox* sep1 = [NSBox new]; sep1.translatesAutoresizingMaskIntoConstraints = NO;
  sep1.boxType = NSBoxSeparator;
  NSBox* sep2 = [NSBox new]; sep2.translatesAutoresizingMaskIntoConstraints = NO;
  sep2.boxType = NSBoxSeparator;

  // Set fonts on all editable controls
  for (NSControl* c in @[_gainMinEdit, _gainMaxEdit, _namPathEdit, _browseBtn,
                          _ovOutputCheck, _ovOutputEdit, _ovBassCheck, _ovBassEdit,
                          _ovMidCheck, _ovMidEdit, _ovTrebleCheck, _ovTrebleEdit,
                          _applyBtn, _previewSlotBtn, _previewChainBtn])
    c.font = font;

  // Add all subviews to rContent
  for (NSView* v in @[selLbl, sep1, gainMinLbl, _gainMinEdit, gainMaxLbl, _gainMaxEdit,
                      namLbl, _namPathEdit, _browseBtn,
                      sep2, ovLbl,
                      _ovOutputCheck, _ovOutputEdit, _ovBassCheck, _ovBassEdit,
                      _ovMidCheck, _ovMidEdit, _ovTrebleCheck, _ovTrebleEdit,
                      _applyBtn, _previewSlotBtn, _previewChainBtn])
    [rContent addSubview:v];

  // ---- Right panel layout using explicit anchors ----
  const CGFloat m = 12, rowGap = 10, labelW = 170, editW = 90;

  NSMutableArray<NSLayoutConstraint*>* rc = [NSMutableArray array];

  // Row 1: "Selected Slot" header
  [rc addObjectsFromArray:@[
    [selLbl.topAnchor constraintEqualToAnchor:rContent.topAnchor constant:m],
    [selLbl.leadingAnchor constraintEqualToAnchor:rContent.leadingAnchor constant:m],
    [selLbl.trailingAnchor constraintEqualToAnchor:rContent.trailingAnchor constant:-m],
  ]];

  // Separator
  [rc addObjectsFromArray:@[
    [sep1.topAnchor constraintEqualToAnchor:selLbl.bottomAnchor constant:6],
    [sep1.leadingAnchor constraintEqualToAnchor:rContent.leadingAnchor constant:m],
    [sep1.trailingAnchor constraintEqualToAnchor:rContent.trailingAnchor constant:-m],
  ]];

  // Gain Min row
  [rc addObjectsFromArray:@[
    [gainMinLbl.topAnchor constraintEqualToAnchor:sep1.bottomAnchor constant:rowGap],
    [gainMinLbl.leadingAnchor constraintEqualToAnchor:rContent.leadingAnchor constant:m],
    [gainMinLbl.widthAnchor constraintEqualToConstant:labelW],
    [_gainMinEdit.centerYAnchor constraintEqualToAnchor:gainMinLbl.centerYAnchor],
    [_gainMinEdit.leadingAnchor constraintEqualToAnchor:gainMinLbl.trailingAnchor constant:4],
    [_gainMinEdit.widthAnchor constraintEqualToConstant:editW],
  ]];

  // Gain Max row
  [rc addObjectsFromArray:@[
    [gainMaxLbl.topAnchor constraintEqualToAnchor:gainMinLbl.bottomAnchor constant:rowGap],
    [gainMaxLbl.leadingAnchor constraintEqualToAnchor:rContent.leadingAnchor constant:m],
    [gainMaxLbl.widthAnchor constraintEqualToConstant:labelW],
    [_gainMaxEdit.centerYAnchor constraintEqualToAnchor:gainMaxLbl.centerYAnchor],
    [_gainMaxEdit.leadingAnchor constraintEqualToAnchor:gainMaxLbl.trailingAnchor constant:4],
    [_gainMaxEdit.widthAnchor constraintEqualToConstant:editW],
  ]];

  // NAM File row
  [rc addObjectsFromArray:@[
    [namLbl.topAnchor constraintEqualToAnchor:gainMaxLbl.bottomAnchor constant:rowGap],
    [namLbl.leadingAnchor constraintEqualToAnchor:rContent.leadingAnchor constant:m],
    [_namPathEdit.topAnchor constraintEqualToAnchor:namLbl.bottomAnchor constant:4],
    [_namPathEdit.leadingAnchor constraintEqualToAnchor:rContent.leadingAnchor constant:m],
    [_namPathEdit.trailingAnchor constraintEqualToAnchor:_browseBtn.leadingAnchor constant:-4],
    [_browseBtn.centerYAnchor constraintEqualToAnchor:_namPathEdit.centerYAnchor],
    [_browseBtn.trailingAnchor constraintEqualToAnchor:rContent.trailingAnchor constant:-m],
    [_browseBtn.widthAnchor constraintEqualToConstant:80],
  ]];

  // Separator 2
  [rc addObjectsFromArray:@[
    [sep2.topAnchor constraintEqualToAnchor:_namPathEdit.bottomAnchor constant:rowGap + 4],
    [sep2.leadingAnchor constraintEqualToAnchor:rContent.leadingAnchor constant:m],
    [sep2.trailingAnchor constraintEqualToAnchor:rContent.trailingAnchor constant:-m],
  ]];

  // "Overrides" header
  [rc addObjectsFromArray:@[
    [ovLbl.topAnchor constraintEqualToAnchor:sep2.bottomAnchor constant:6],
    [ovLbl.leadingAnchor constraintEqualToAnchor:rContent.leadingAnchor constant:m],
    [ovLbl.trailingAnchor constraintEqualToAnchor:rContent.trailingAnchor constant:-m],
  ]];

  // Override rows: checkbox + edit field
    // Override rows: checkbox + edit field
  NSView* prevAnchor = ovLbl;
  NSArray<NSArray*>* ovRows = @[
    @[_ovOutputCheck, _ovOutputEdit],
    @[_ovBassCheck,   _ovBassEdit],
    @[_ovMidCheck,    _ovMidEdit],
    @[_ovTrebleCheck, _ovTrebleEdit],
  ];
  for (NSArray* row in ovRows)
  {
    NSButton* cb = row[0];
    NSTextField* tf = row[1];
    [rc addObjectsFromArray:@[
      [cb.topAnchor constraintEqualToAnchor:prevAnchor.bottomAnchor constant:rowGap],
      [cb.leadingAnchor constraintEqualToAnchor:rContent.leadingAnchor constant:m],
      [cb.widthAnchor constraintEqualToConstant:labelW],
      [tf.centerYAnchor constraintEqualToAnchor:cb.centerYAnchor],
      [tf.leadingAnchor constraintEqualToAnchor:cb.trailingAnchor constant:4],
      [tf.widthAnchor constraintEqualToConstant:editW],
    ]];

    // Align multi-line checkboxes to the top
    if ([cb.title containsString:@"\n"])
      [rc addObject:[cb.topAnchor constraintEqualToAnchor:prevAnchor.bottomAnchor constant:rowGap + 2]];

    prevAnchor = cb;
  }

  // Apply button
  [rc addObjectsFromArray:@[
    [_applyBtn.topAnchor constraintEqualToAnchor:prevAnchor.bottomAnchor constant:rowGap + 6],
    [_applyBtn.leadingAnchor constraintEqualToAnchor:rContent.leadingAnchor constant:m],
    [_applyBtn.widthAnchor constraintEqualToConstant:280],
  ]];

  // Preview buttons
  [rc addObjectsFromArray:@[
    [_previewSlotBtn.topAnchor constraintEqualToAnchor:_applyBtn.bottomAnchor constant:rowGap],
    [_previewSlotBtn.leadingAnchor constraintEqualToAnchor:rContent.leadingAnchor constant:m],
    [_previewChainBtn.leadingAnchor constraintEqualToAnchor:_previewSlotBtn.trailingAnchor constant:4],
    [_previewChainBtn.centerYAnchor constraintEqualToAnchor:_previewSlotBtn.centerYAnchor],
    // Pin bottom so scroll content has a defined height
    [_previewChainBtn.bottomAnchor constraintLessThanOrEqualToAnchor:rContent.bottomAnchor constant:-m],
    [_previewSlotBtn.bottomAnchor constraintLessThanOrEqualToAnchor:rContent.bottomAnchor constant:-m],
  ]];

  // Minimum width for right content
  [rc addObject:[rContent.widthAnchor constraintGreaterThanOrEqualToConstant:360]];

  [NSLayoutConstraint activateConstraints:rc];

  // Add panels to split view
  [split addSubview:leftPanel];
  [split addSubview:rightScroll];
  [split setHoldingPriority:NSLayoutPriorityDefaultLow forSubviewAtIndex:0];

  // ---- Root layout (toolbar + split + close button) ----
  [NSLayoutConstraint activateConstraints:@[
    // Toolbar horizontal: path label stretches, buttons on right
    [_filePathLabel.leadingAnchor constraintEqualToAnchor:cv.leadingAnchor constant:8],
    [_newBtn.leadingAnchor constraintGreaterThanOrEqualToAnchor:_filePathLabel.trailingAnchor constant:8],
    [_openBtn.leadingAnchor constraintEqualToAnchor:_newBtn.trailingAnchor constant:4],
    [_saveBtn.leadingAnchor constraintEqualToAnchor:_openBtn.trailingAnchor constant:4],
    [_saveAsBtn.leadingAnchor constraintEqualToAnchor:_saveBtn.trailingAnchor constant:4],
    [_fontDecBtn.leadingAnchor constraintEqualToAnchor:_saveAsBtn.trailingAnchor constant:4],
    [_fontIncBtn.leadingAnchor constraintEqualToAnchor:_fontDecBtn.trailingAnchor constant:4],
    [_fontIncBtn.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor constant:-8],

    // Fixed widths for toolbar buttons
    [_newBtn.widthAnchor constraintEqualToConstant:70],
    [_openBtn.widthAnchor constraintEqualToConstant:70],
    [_saveBtn.widthAnchor constraintEqualToConstant:70],
    [_saveAsBtn.widthAnchor constraintEqualToConstant:80],
    [_fontDecBtn.widthAnchor constraintEqualToConstant:36],
    [_fontIncBtn.widthAnchor constraintEqualToConstant:36],

    // Toolbar vertical: all buttons on the same row
    [_newBtn.topAnchor constraintEqualToAnchor:cv.topAnchor constant:8],
    [_filePathLabel.centerYAnchor constraintEqualToAnchor:_newBtn.centerYAnchor],
    [_openBtn.centerYAnchor constraintEqualToAnchor:_newBtn.centerYAnchor],
    [_saveBtn.centerYAnchor constraintEqualToAnchor:_newBtn.centerYAnchor],
    [_saveAsBtn.centerYAnchor constraintEqualToAnchor:_newBtn.centerYAnchor],
    [_fontDecBtn.centerYAnchor constraintEqualToAnchor:_newBtn.centerYAnchor],
    [_fontIncBtn.centerYAnchor constraintEqualToAnchor:_newBtn.centerYAnchor],

    // Split view: below toolbar, above the close button
    [split.topAnchor constraintEqualToAnchor:_newBtn.bottomAnchor constant:8],
    [split.leadingAnchor constraintEqualToAnchor:cv.leadingAnchor constant:8],
    [split.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor constant:-8],
    [split.bottomAnchor constraintEqualToAnchor:_closeBtn.topAnchor constant:-8],

    // Close button — pinned to lower-right corner
    [_closeBtn.trailingAnchor constraintEqualToAnchor:cv.trailingAnchor constant:-8],
    [_closeBtn.bottomAnchor constraintEqualToAnchor:cv.bottomAnchor constant:-8],
    [_closeBtn.widthAnchor constraintEqualToConstant:80],
  ]];

  [split setPosition:340 ofDividerAtIndex:0];
}

// ============================================================
//  NSTableView data source / delegate
// ============================================================
- (NSInteger)numberOfRowsInTableView:(NSTableView*)tv { return (NSInteger)self.slots->size(); }

- (id)tableView:(NSTableView*)tv objectValueForTableColumn:(NSTableColumn*)col row:(NSInteger)row
{
  if (row < 0 || row >= (NSInteger)self.slots->size()) return @"";
  const auto& s = (*self.slots)[(size_t)row];
  NSString* ident = col.identifier;
  if ([ident isEqualToString:@"#"])     return ToNS(std::to_string(row + 1));
  if ([ident isEqualToString:@"Min"])   return ToNS(DoubleStr(s.ampGainMin));
  if ([ident isEqualToString:@"Max"])   return ToNS(DoubleStr(s.ampGainMax));
  if ([ident isEqualToString:@"Model"]) {
    std::string stem = std::filesystem::path(s.namFilePath).stem().string();
    return ToNS(stem.empty() ? "(none)" : stem);
  }
  return @"";
}

- (void)tableView:(NSTableView*)tv willDisplayCell:(id)cell forTableColumn:(NSTableColumn*)col row:(NSInteger)row
{
  if ([cell isKindOfClass:[NSTextFieldCell class]])
  {
    NSTextFieldCell* tc = (NSTextFieldCell*)cell;
    tc.textColor = kTextClr();
    tc.font = [NSFont systemFontOfSize:self.currentFontSize];
    tc.drawsBackground = NO;
  }
}

- (void)tableViewSelectionDidChange:(NSNotification*)note
{
  NSIndexSet* sel = _slotTable.selectedRowIndexes;

  if (sel.count == 0)
  {
    [self setEditPanelEnabled:NO];
    _applyBtn.title = @"Apply to Slot ✓";
    return;
  }

  // Load the focused (clicked) row into the edit panel
  NSInteger focused = _slotTable.clickedRow;
  if (focused < 0 || ![sel containsIndex:(NSUInteger)focused])
    focused = (NSInteger)sel.firstIndex;

  if (focused >= 0 && focused < (NSInteger)self.slots->size())
  {
    [self updateEditPanelFromSlot:(int)focused];
    [self setEditPanelEnabled:YES];
  }

  _applyBtn.title = (sel.count > 1) ? @"Apply to Selected ✓" : @"Apply to Slot ✓";
}

- (void)slotTableClicked:(id)sender { /* handled via selection change */ }

// ---- Drag-and-drop into table ----
- (NSDragOperation)tableView:(NSTableView*)tv validateDrop:(id<NSDraggingInfo>)info proposedRow:(NSInteger)row proposedDropOperation:(NSTableViewDropOperation)op
{
  NSArray* files = [info.draggingPasteboard propertyListForType:NSFilenamesPboardType];
  for (NSString* path in files)
    if ([[path pathExtension].lowercaseString isEqualToString:@"nam"])
      return NSDragOperationCopy;
  return NSDragOperationNone;
}

- (BOOL)tableView:(NSTableView*)tv acceptDrop:(id<NSDraggingInfo>)info row:(NSInteger)row dropOperation:(NSTableViewDropOperation)op
{
  NSArray* files = [info.draggingPasteboard propertyListForType:NSFilenamesPboardType];
  NSMutableArray* namFiles = [NSMutableArray array];
  for (NSString* path in files)
    if ([[path pathExtension].lowercaseString isEqualToString:@"nam"])
      [namFiles addObject:path];

  if (namFiles.count == 0) return NO;

  if (namFiles.count == 1 && row >= 0 && row < (NSInteger)self.slots->size())
  {
    (*self.slots)[(size_t)row].namFilePath = FromNS(namFiles[0]);
    *self.dirty = true;
    [self refreshSlotTable];
    [self selectSlotIndex:(int)row];
    [self updateTitleBar];
    return YES;
  }

  // Append with proportional gain ranges
  const double gainStart = self.slots->empty() ? 0.0 : self.slots->back().ampGainMax;
  const int n = (int)namFiles.count;
  const double range = 10.0 - gainStart;
  const double step = (range > 0.0) ? range / n : 0.0;

  for (int i = 0; i < n; ++i)
  {
    ModelMapSlot slot;
    slot.namFilePath = FromNS(namFiles[i]);
    slot.ampGainMin = gainStart + i * step;
    slot.ampGainMax = gainStart + (i + 1) * step;
    self.slots->push_back(slot);
  }
  *self.dirty = true;
  [self refreshSlotTable];
  [self selectSlotIndex:(int)self.slots->size() - 1];
  [self updateTitleBar];
  return YES;
}

- (BOOL)tableView:(NSTableView*)tv writeRows:(NSArray*)rows toPasteboard:(NSPasteboard*)pb { return NO; }

// ============================================================
//  Slot panel helpers
// ============================================================
- (void)refreshSlotTable
{
  [_slotTable reloadData];
}

- (void)selectSlotIndex:(int)idx
{
  if (idx < 0 || idx >= (int)self.slots->size()) return;
  [_slotTable selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)idx]
          byExtendingSelection:NO];
  [_slotTable scrollRowToVisible:idx];
  [self updateEditPanelFromSlot:idx];
  [self setEditPanelEnabled:YES];
}

- (void)updateEditPanelFromSlot:(int)idx
{
  if (idx < 0 || idx >= (int)self.slots->size()) return;
  const auto& s = (*self.slots)[(size_t)idx];
  _gainMinEdit.stringValue = ToNS(DoubleStr(s.ampGainMin));
  _gainMaxEdit.stringValue = ToNS(DoubleStr(s.ampGainMax));
  _namPathEdit.stringValue = ToNS(s.namFilePath);

  auto applyOv = [](NSButton* cb, NSTextField* tf, const std::optional<double>& v) {
    cb.state = v.has_value() ? NSControlStateValueOn : NSControlStateValueOff;
    tf.stringValue = v.has_value() ? ToNS(DoubleStr(*v)) : @"";
    tf.enabled = v.has_value();
  };
  applyOv(_ovOutputCheck, _ovOutputEdit, s.overrides.outputLevel);
  applyOv(_ovBassCheck,   _ovBassEdit,   s.overrides.toneBass);
  applyOv(_ovMidCheck,    _ovMidEdit,    s.overrides.toneMid);
  applyOv(_ovTrebleCheck, _ovTrebleEdit, s.overrides.toneTreble);
}

- (ModelMapSlot)readEditPanelToSlot
{
  ModelMapSlot slot;
  slot.ampGainMin  = ParseDouble(_gainMinEdit, 0.0);
  slot.ampGainMax  = ParseDouble(_gainMaxEdit, 10.0);
  slot.namFilePath = FromNS(_namPathEdit.stringValue);
  if (_ovOutputCheck.state == NSControlStateValueOn) slot.overrides.outputLevel = ParseDouble(_ovOutputEdit, 0.0);
  if (_ovBassCheck.state   == NSControlStateValueOn) slot.overrides.toneBass    = ParseDouble(_ovBassEdit,   5.0);
  if (_ovMidCheck.state    == NSControlStateValueOn) slot.overrides.toneMid     = ParseDouble(_ovMidEdit,    5.0);
  if (_ovTrebleCheck.state == NSControlStateValueOn) slot.overrides.toneTreble  = ParseDouble(_ovTrebleEdit, 5.0);
  return slot;
}

- (void)setEditPanelEnabled:(BOOL)enabled
{
  for (NSControl* c in @[_gainMinEdit, _gainMaxEdit, _namPathEdit, _browseBtn,
                          _ovOutputCheck, _ovBassCheck, _ovMidCheck, _ovTrebleCheck,
                          _applyBtn, _previewSlotBtn])
    c.enabled = enabled;

  if (enabled)
  {
    _ovOutputEdit.enabled = (_ovOutputCheck.state == NSControlStateValueOn);
    _ovBassEdit.enabled   = (_ovBassCheck.state   == NSControlStateValueOn);
    _ovMidEdit.enabled    = (_ovMidCheck.state     == NSControlStateValueOn);
    _ovTrebleEdit.enabled = (_ovTrebleCheck.state  == NSControlStateValueOn);
  }
  else
  {
    for (NSTextField* tf in @[_ovOutputEdit, _ovBassEdit, _ovMidEdit, _ovTrebleEdit])
      tf.enabled = NO;
  }

  _previewChainBtn.enabled = (!self.slots->empty() && self.onPreviewChain != nullptr);
}

- (void)updateTitleBar
{
  NSString* title = @"PNAM Chain Editor";
  if (*self.dirty) title = [title stringByAppendingString:@" *"];
  self.window.title = title;
  _filePathLabel.stringValue = self.currentFilePath->empty()
    ? @"(new file)" : ToNS(*self.currentFilePath);
}

// ============================================================
//  Button actions
// ============================================================
- (void)onNew:(id)s
{
  if (![self promptSaveIfDirty]) return;
  self.slots->clear();
  self.currentFilePath->clear();
  *self.dirty = false;
  [self refreshSlotTable];
  [self setEditPanelEnabled:NO];
  [self updateTitleBar];
}

- (void)onOpen:(id)s
{
  if (![self promptSaveIfDirty]) return;
  NSOpenPanel* op = [NSOpenPanel openPanel];
  op.allowedFileTypes = @[@"pnam"];
  op.title = @"Open PNAM Chain";
  if ([op runModal] == NSModalResponseOK)
  {
    // Route through C++ LoadFile via a temporary notification — instead,
    // use a callback pattern mirroring the library browser.
    // We post the path back via the onSaved callback repurposed as onOpen.
    // Actually: store path and signal C++ side via a dedicated open callback.
    // For simplicity, parse inline (matches C++ LoadFile logic).
    [self loadFileFromPath:FromNS(op.URL.path)];
  }
}

- (void)loadFileFromPath:(const std::string&)path
{
  namespace fs = std::filesystem;
  const std::string baseDir = fs::path(path).parent_path().string();

  std::ifstream f(path);
  if (!f.is_open()) return;
  json j;
  try { j = json::parse(f); } catch (...) { return; }

  self.slots->clear();
  *self.currentFilePath = path;
  *self.dirty = false;

  if (j.contains("slots") && j["slots"].is_array())
  {
    for (const auto& jSlot : j["slots"])
    {
      ModelMapSlot slot;
      slot.ampGainMin  = jSlot.value("amp_gain_min", 0.0);
      slot.ampGainMax  = jSlot.value("amp_gain_max", 10.0);
      // Resolve stored (possibly relative) path against this .pnam's directory
      slot.namFilePath = ResolvePath(jSlot.value("nam_path", std::string{}), baseDir);
      if (jSlot.contains("overrides"))
      {
        const auto& jOv = jSlot["overrides"];
        if (jOv.contains("output_level")) slot.overrides.outputLevel = jOv["output_level"].get<double>();
        if (jOv.contains("tone_bass"))    slot.overrides.toneBass    = jOv["tone_bass"].get<double>();
        if (jOv.contains("tone_mid"))     slot.overrides.toneMid     = jOv["tone_mid"].get<double>();
        if (jOv.contains("tone_treble"))  slot.overrides.toneTreble  = jOv["tone_treble"].get<double>();
      }
      self.slots->push_back(std::move(slot));
    }
  }

  [self refreshSlotTable];
  [self updateTitleBar];
  [self setEditPanelEnabled:NO];
  if (!self.slots->empty())
    [self selectSlotIndex:0];
}

- (void)onSave:(id)s
{
  if (![self warnIfOverlapping]) return;
  if (self.currentFilePath->empty()) { [self onSaveAsSkipOverlapCheck:s]; return; }
  if ([self saveToFile:*self.currentFilePath])
  {
    *self.dirty = false;
    [self updateTitleBar];
    if (self.onSaved) self.onSaved(*self.currentFilePath);
  }
}

- (void)onSaveAs:(id)s
{
  if (![self warnIfOverlapping]) return;
  [self onSaveAsSkipOverlapCheck:s];
}

- (void)onSaveAsSkipOverlapCheck:(id)s
{
  NSSavePanel* sp = [NSSavePanel savePanel];
  sp.allowedFileTypes = @[@"pnam"];
  sp.title = @"Save PNAM Chain";
  if (!self.currentFilePath->empty())
    sp.nameFieldStringValue = ToNS(std::filesystem::path(*self.currentFilePath).filename().string());

  if ([sp runModal] == NSModalResponseOK)
  {
    *self.currentFilePath = FromNS(sp.URL.path);
    if ([self saveToFile:*self.currentFilePath])
    {
      *self.dirty = false;
      [self updateTitleBar];
      if (self.onSaved) self.onSaved(*self.currentFilePath);
    }
  }
}

- (BOOL)saveToFile:(const std::string&)path
{
  namespace fs = std::filesystem;
  

  json j;
  j["pnam_version"] = 1;
  j["slots"] = json::array();
  for (const auto& slot : *self.slots)
  {
    json jSlot;
    jSlot["amp_gain_min"] = slot.ampGainMin;
    jSlot["amp_gain_max"] = slot.ampGainMax;
    // Store as portable relative path
    jSlot["nam_path"] = slot.namFilePath;
    json jOv = json::object();
    if (slot.overrides.outputLevel.has_value()) jOv["output_level"] = *slot.overrides.outputLevel;
    if (slot.overrides.toneBass.has_value())    jOv["tone_bass"]    = *slot.overrides.toneBass;
    if (slot.overrides.toneMid.has_value())     jOv["tone_mid"]     = *slot.overrides.toneMid;
    if (slot.overrides.toneTreble.has_value())  jOv["tone_treble"]  = *slot.overrides.toneTreble;
    if (!jOv.empty()) jSlot["overrides"] = jOv;
    j["slots"].push_back(jSlot);
  }
  try
  {
    std::ofstream f(path);
    if (!f.is_open()) return NO;
    f << j.dump(2);
    return YES;
  }
  catch (...) { return NO; }
}

- (BOOL)warnIfOverlapping
{
  const std::string warn = CheckSlotOverlaps(*self.slots);
  if (warn.empty()) return YES;

  NSAlert* alert = [NSAlert new];
  alert.alertStyle = NSAlertStyleWarning;
  alert.messageText = @"Overlapping Slots";
  alert.informativeText = [NSString stringWithFormat:
    @"The following slots have overlapping gain ranges.\n"
     "Earlier slots will shadow later ones in the overlap zone.\n\n%s\nSave anyway?",
    warn.c_str()];
  [alert addButtonWithTitle:@"Save Anyway"];
  [alert addButtonWithTitle:@"Cancel"];
  return ([alert runModal] == NSAlertFirstButtonReturn);
}

- (BOOL)promptSaveIfDirty
{
  if (!*self.dirty) return YES;
  NSAlert* alert = [NSAlert new];
  alert.messageText = @"Unsaved Changes";
  alert.informativeText = @"You have unsaved changes. Save before continuing?";
  [alert addButtonWithTitle:@"Save"];
  [alert addButtonWithTitle:@"Don't Save"];
  [alert addButtonWithTitle:@"Cancel"];
  NSModalResponse resp = [alert runModal];
  if (resp == NSAlertThirdButtonReturn) return NO;
  if (resp == NSAlertFirstButtonReturn) [self onSave:nil];
  return YES;
}

- (void)onAddSlot:(id)s
{
  ModelMapSlot slot;
  if (!self.slots->empty())
  {
    slot.ampGainMin = self.slots->back().ampGainMax;
    slot.ampGainMax = std::min(10.0, slot.ampGainMin + 2.0);
  }
  self.slots->push_back(slot);
  *self.dirty = true;
  [self refreshSlotTable];
  [self selectSlotIndex:(int)self.slots->size() - 1];
  [self updateTitleBar];
}

- (void)onRemoveSlot:(id)s
{
  NSInteger idx = _slotTable.selectedRow;
  if (idx < 0 || idx >= (NSInteger)self.slots->size()) return;
  self.slots->erase(self.slots->begin() + idx);
  *self.dirty = true;
  [self refreshSlotTable];
  int newSel = (int)std::min((NSInteger)idx, (NSInteger)self.slots->size() - 1);
  if (newSel >= 0) [self selectSlotIndex:newSel];
  else [self setEditPanelEnabled:NO];
  [self updateTitleBar];
}

- (void)onMoveUp:(id)s
{
  NSIndexSet* sel = _slotTable.selectedRowIndexes;
  if (sel.count == 0) return;

  // If the topmost selected row is already at 0 there is nowhere to go
  if (sel.firstIndex == 0) return;

  // Collect sorted indices and swap each with the row above (ascending order)
  NSMutableArray<NSNumber*>* indices = [NSMutableArray array];
  [sel enumerateIndexesUsingBlock:^(NSUInteger idx, BOOL* stop) {
    [indices addObject:@(idx)];
  }];
  // indices are already ascending from enumerateIndexesUsingBlock

  for (NSNumber* n in indices)
  {
    NSUInteger idx = n.unsignedIntegerValue;
    if (idx > 0 && idx < self.slots->size())
      std::swap((*self.slots)[idx], (*self.slots)[idx - 1]);
  }

  *self.dirty = true;
  [self refreshSlotTable];

  // Re-select rows in new positions
  NSMutableIndexSet* newSel = [NSMutableIndexSet indexSet];
  [sel enumerateIndexesUsingBlock:^(NSUInteger idx, BOOL* stop) {
    [newSel addIndex:idx - 1];
  }];
  [_slotTable selectRowIndexes:newSel byExtendingSelection:NO];
  [_slotTable scrollRowToVisible:(NSInteger)newSel.firstIndex];
  [self updateTitleBar];
}

- (void)onMoveDown:(id)s
{
  NSIndexSet* sel = _slotTable.selectedRowIndexes;
  if (sel.count == 0) return;

  const NSUInteger lastSlot = self.slots->size() - 1;
  // If the bottommost selected row is already at the end there is nowhere to go
  if (sel.lastIndex >= lastSlot) return;

  // Collect sorted indices; swap in reverse (descending) order so earlier swaps
  // don't overwrite positions that will be moved later
  NSMutableArray<NSNumber*>* indices = [NSMutableArray array];
  [sel enumerateIndexesWithOptions:NSEnumerationReverse usingBlock:^(NSUInteger idx, BOOL* stop) {
    [indices addObject:@(idx)];
  }];

  for (NSNumber* n in indices)
  {
    NSUInteger idx = n.unsignedIntegerValue;
    if (idx < self.slots->size() - 1)
      std::swap((*self.slots)[idx], (*self.slots)[idx + 1]);
  }

  *self.dirty = true;
  [self refreshSlotTable];

  // Re-select rows in new positions
  NSMutableIndexSet* newSel = [NSMutableIndexSet indexSet];
  [sel enumerateIndexesUsingBlock:^(NSUInteger idx, BOOL* stop) {
    [newSel addIndex:idx + 1];
  }];
  [_slotTable selectRowIndexes:newSel byExtendingSelection:NO];
  [_slotTable scrollRowToVisible:(NSInteger)newSel.lastIndex];
  [self updateTitleBar];
}

- (void)onDistribute:(id)s
{
  if (self.slots->empty()) return;
  const int n = (int)self.slots->size();
  const double step = 10.0 / n;
  for (int i = 0; i < n; ++i)
  {
    (*self.slots)[i].ampGainMin = i * step;
    (*self.slots)[i].ampGainMax = (i + 1) * step;
  }
  *self.dirty = true;
  [self refreshSlotTable];
  NSInteger sel = _slotTable.selectedRow;
  if (sel >= 0) [self updateEditPanelFromSlot:(int)sel];
  [self updateTitleBar];
}

- (void)onBrowseNAM:(id)s
{
  NSOpenPanel* op = [NSOpenPanel openPanel];
  op.allowedFileTypes = @[@"nam"];
  op.title = @"Select NAM Model";
  if ([op runModal] == NSModalResponseOK)
    _namPathEdit.stringValue = op.URL.path;
}

- (void)onApplySlot:(id)s
{
  NSIndexSet* sel = _slotTable.selectedRowIndexes;
  if (sel.count == 0) return;

  if (sel.count == 1)
  {
    // Single selection: write all fields as before
    NSInteger idx = (NSInteger)sel.firstIndex;
    if (idx < 0 || idx >= (NSInteger)self.slots->size()) return;
    (*self.slots)[(size_t)idx] = [self readEditPanelToSlot];
  }
  else
  {
    // Multi-selection: apply only the override fields to every selected slot
    std::optional<double> outputLevel, toneBass, toneMid, toneTreble;
    if (_ovOutputCheck.state == NSControlStateValueOn) outputLevel = ParseDouble(_ovOutputEdit, 0.0);
    if (_ovBassCheck.state   == NSControlStateValueOn) toneBass    = ParseDouble(_ovBassEdit,   5.0);
    if (_ovMidCheck.state    == NSControlStateValueOn) toneMid     = ParseDouble(_ovMidEdit,    5.0);
    if (_ovTrebleCheck.state == NSControlStateValueOn) toneTreble  = ParseDouble(_ovTrebleEdit, 5.0);

    [sel enumerateIndexesUsingBlock:^(NSUInteger idx, BOOL* stop) {
      if (idx >= self.slots->size()) return;
      (*self.slots)[idx].overrides.outputLevel = outputLevel;
      (*self.slots)[idx].overrides.toneBass    = toneBass;
      (*self.slots)[idx].overrides.toneMid     = toneMid;
      (*self.slots)[idx].overrides.toneTreble  = toneTreble;
    }];
  }

  *self.dirty = true;
  [self refreshSlotTable];

  // Restore the selection after reload
  [_slotTable selectRowIndexes:sel byExtendingSelection:NO];
  [_slotTable scrollRowToVisible:(NSInteger)sel.firstIndex];
  [self updateTitleBar];
}

- (void)onOverrideToggle:(id)sender
{
  _ovOutputEdit.enabled = (_ovOutputCheck.state == NSControlStateValueOn);
  _ovBassEdit.enabled   = (_ovBassCheck.state   == NSControlStateValueOn);
  _ovMidEdit.enabled    = (_ovMidCheck.state     == NSControlStateValueOn);
  _ovTrebleEdit.enabled = (_ovTrebleCheck.state  == NSControlStateValueOn);
}

- (void)onPreviewSlot:(id)s
{
  if (!self.onPreviewSlot) return;
  NSInteger idx = _slotTable.selectedRow;
  if (idx < 0 || idx >= (NSInteger)self.slots->size()) return;
  ModelMapSlot slot = [self readEditPanelToSlot];
  if (slot.namFilePath.empty()) return;
  self.onPreviewSlot(slot);
}

- (void)onPreviewChain:(id)s
{
  if (!self.onPreviewChain) return;
  if (self.slots->empty()) return;
  if (self.currentFilePath->empty())
  {
    [self onSaveAs:nil];
    if (self.currentFilePath->empty()) return;
  }
  else
  {
    if (![self saveToFile:*self.currentFilePath]) return;
    *self.dirty = false;
    [self updateTitleBar];
  }
  self.onPreviewChain(*self.currentFilePath);
}

- (void)onFontDec:(id)s
{
  if (self.currentFontSize <= 12) return;
  self.currentFontSize -= 2;
  [self applyFont];
}

- (void)onFontInc:(id)s
{
  if (self.currentFontSize >= 40) return;
  self.currentFontSize += 2;
  [self applyFont];
}

- (void)onClose:(id)s
{
  [self.window performClose:nil];
}

- (void)applyFont
{
  NSFont* font = [NSFont systemFontOfSize:self.currentFontSize];
  for (NSControl* c in @[_filePathLabel, _newBtn, _openBtn, _saveBtn, _saveAsBtn,
                          _fontDecBtn, _fontIncBtn, _closeBtn,
                          _addBtn, _removeBtn, _upBtn, _downBtn,
                          _distributeBtn, _gainMinEdit, _gainMaxEdit, _namPathEdit, _browseBtn,
                          _ovOutputCheck, _ovOutputEdit, _ovBassCheck, _ovBassEdit,
                          _ovMidCheck, _ovMidEdit, _ovTrebleCheck, _ovTrebleEdit,
                          _applyBtn, _previewSlotBtn, _previewChainBtn])
    c.font = font;
  _slotTable.rowHeight = self.currentFontSize + 8;
  if (self.fontSizePtr) *self.fontSizePtr = self.currentFontSize;  // ← add this line
  [_slotTable reloadData];
}

// ============================================================
//  Window delegate
// ============================================================
- (BOOL)windowShouldClose:(NSWindow*)sender
{
  return [self promptSaveIfDirty];
}

- (void)windowWillClose:(NSNotification*)note
{
  if (self.onWindowClosed) self.onWindowClosed();
}

// ============================================================
//  NSWindow drag destination (window-level drop)
// ============================================================
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender
{
  NSArray* files = [sender.draggingPasteboard propertyListForType:NSFilenamesPboardType];
  for (NSString* p in files)
    if ([[p pathExtension].lowercaseString isEqualToString:@"nam"])
      return NSDragOperationCopy;
  return NSDragOperationNone;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{
  NSArray* files = [sender.draggingPasteboard propertyListForType:NSFilenamesPboardType];
  NSMutableArray* namFiles = [NSMutableArray array];
  for (NSString* p in files)
    if ([[p pathExtension].lowercaseString isEqualToString:@"nam"])
      [namFiles addObject:p];
  if (namFiles.count == 0) return NO;

  const double gainStart = self.slots->empty() ? 0.0 : self.slots->back().ampGainMax;
  const int n = (int)namFiles.count;
  const double range = 10.0 - gainStart;
  const double step = (range > 0.0) ? range / n : 0.0;
  for (int i = 0; i < n; ++i)
  {
    ModelMapSlot slot;
    slot.namFilePath = FromNS(namFiles[i]);
    slot.ampGainMin  = gainStart + i * step;
    slot.ampGainMax  = gainStart + (i + 1) * step;
    self.slots->push_back(slot);
  }
  *self.dirty = true;
  [self refreshSlotTable];
  [self selectSlotIndex:(int)self.slots->size() - 1];
  [self updateTitleBar];
  return YES;
}

@end

// ============================================================
//  C++ class implementation (Mac)
/// ============================================================

static std::string GetPNAMSettingsPath()
{
  const char* home = std::getenv("HOME");
  if (!home) return {};
  namespace fs = std::filesystem;
  fs::path dir = fs::path(home) / "Library" / "Application Support" / "NeuralAmpModeler";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return ec ? std::string{} : (dir / "PNAMEditor.settings").string();
}

NAMPNAMEditorWindow::NAMPNAMEditorWindow()
{
  LoadSettings();
}

NAMPNAMEditorWindow::~NAMPNAMEditorWindow()
{
  Close();
}

void NAMPNAMEditorWindow::Open(void* pParentWindow)
{
  if (mIsOpen) { BringToFront(); return; }

  @autoreleasepool
  {
    NAMPNAMEditorController* ctrl =
      [[NAMPNAMEditorController alloc] initWithSlots:&mSlots
                                     currentFilePath:&mCurrentFilePath
                                               dirty:&mDirty
                                            fontSize:mFontSize
                                        fontSizePtr:&mFontSize];

    NSWindow* parentWin = nil;
    if (pParentWindow)
      parentWin = ((__bridge NSView*)pParentWindow).window;

    // Restore saved window position
    if (mHasSavedBounds)
    {
      NSScreen* scr = [NSScreen mainScreen];
      CGFloat screenTop = NSMaxY(scr.frame);
      NSRect frame = NSMakeRect(mWindowX, screenTop - mWindowY - mWindowH, mWindowW, mWindowH);
      BOOL onScreen = NO;
      for (NSScreen* s in [NSScreen screens])
        if (NSIntersectsRect(frame, s.frame)) { onScreen = YES; break; }
      if (onScreen)
        [ctrl.window setFrame:frame display:NO];
    }
    else if (parentWin)
    {
      NSRect pf = parentWin.frame;
      [ctrl.window setFrame:NSMakeRect(pf.origin.x + pf.size.width + 10,
                                       pf.origin.y, mWindowW, mWindowH)
                    display:NO];
    }

    ctrl.onWindowClosed = [this]() {
      if (mpWindowController)
      {
        NAMPNAMEditorController* c = (__bridge NAMPNAMEditorController*)mpWindowController;
        // Detach from parent before close cleans up
        if (c.window.parentWindow)
          [c.window.parentWindow removeChildWindow:c.window];
      }
      // Save bounds
      if (mpWindowController)
      {
        NSRect frame = ((__bridge NAMPNAMEditorController*)mpWindowController).window.frame;
        NSScreen* scr = [NSScreen mainScreen];
        mWindowX = (int)frame.origin.x;
        mWindowY = (int)(NSMaxY(scr.frame) - NSMaxY(frame));
        mWindowW = (int)frame.size.width;
        mWindowH = (int)frame.size.height;
        mHasSavedBounds = true;
      }
      SaveSettings();
      mIsOpen = false;
      if (mOnWindowClosed) mOnWindowClosed();
    };
    ctrl.onSaved        = mOnSaved;
    ctrl.onPreviewSlot  = mOnPreviewSlot;
    ctrl.onPreviewChain = mOnPreviewChain;

    if (!mSlots.empty())
    {
      [ctrl refreshSlotTable];
      [ctrl updateTitleBar];
      [ctrl selectSlotIndex:0];
    }

    mpWindowController = (__bridge_retained void*)ctrl;
    [ctrl showWindow:nil];

    // Attach as child so it follows the parent window in z-order
    // (moves behind/in-front of parent together, hides when parent miniaturises)
    if (parentWin)
      [parentWin addChildWindow:ctrl.window ordered:NSWindowAbove];

    mIsOpen = true;
  }
}

void NAMPNAMEditorWindow::Close()
{
  if (!mIsOpen && !mpWindowController) return;

  @autoreleasepool
  {
    if (mpWindowController)
    {
      NAMPNAMEditorController* ctrl = (__bridge_transfer NAMPNAMEditorController*)mpWindowController;
      mpWindowController = nullptr;
      if (ctrl.window.parentWindow)
        [ctrl.window.parentWindow removeChildWindow:ctrl.window];
      [ctrl close];
    }
  }
  mIsOpen = false;
}

void NAMPNAMEditorWindow::BringToFront()
{
  if (mpWindowController)
    [(__bridge NAMPNAMEditorController*)mpWindowController showWindow:nil];
}

void NAMPNAMEditorWindow::LoadFile(const std::string& pnamPath)
{
  mLastOpenedPNAMPath = pnamPath;

  if (mpWindowController)
  {
    @autoreleasepool
    {
      [(__bridge NAMPNAMEditorController*)mpWindowController loadFileFromPath:pnamPath];
    }
  }
  else
  {
    namespace fs = std::filesystem;
    const std::string baseDir = fs::path(pnamPath).parent_path().string();

    std::ifstream f(pnamPath);
    if (!f.is_open()) return;
    json j;
    try { j = json::parse(f); } catch (...) { return; }
    mSlots.clear();
    mCurrentFilePath = pnamPath;
    mDirty = false;
    if (j.contains("slots") && j["slots"].is_array())
    {
      for (const auto& jSlot : j["slots"])
      {
        ModelMapSlot slot;
        slot.ampGainMin  = jSlot.value("amp_gain_min", 0.0);
        slot.ampGainMax  = jSlot.value("amp_gain_max", 10.0);
        slot.namFilePath = ResolvePath(jSlot.value("nam_path", std::string{}), baseDir);
        if (jSlot.contains("overrides"))
        {
          const auto& jOv = jSlot["overrides"];
          if (jOv.contains("output_level")) slot.overrides.outputLevel = jOv["output_level"].get<double>();
          if (jOv.contains("tone_bass"))    slot.overrides.toneBass    = jOv["tone_bass"].get<double>();
          if (jOv.contains("tone_mid"))     slot.overrides.toneMid     = jOv["tone_mid"].get<double>();
          if (jOv.contains("tone_treble"))  slot.overrides.toneTreble  = jOv["tone_treble"].get<double>();
        }
        mSlots.push_back(std::move(slot));
      }
    }
  }
}

std::string NAMPNAMEditorWindow::GetSettingsFilePath()
{
  return GetPNAMSettingsPath();
}

void NAMPNAMEditorWindow::LoadSettings()
{
  const std::string path = GetSettingsFilePath();
  if (path.empty()) return;
  std::ifstream file(path);
  if (!file.is_open()) return;
  std::string line;
  while (std::getline(file, line))
  {
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = line.substr(0, eq);
    const std::string val = line.substr(eq + 1);
    try
    {
      if (key == "FontSize")   { int v = std::stoi(val); if (v >= 12 && v <= 40) mFontSize = v; }
      else if (key == "WindowX") { mWindowX = std::stoi(val); mHasSavedBounds = true; }
      else if (key == "WindowY") { mWindowY = std::stoi(val); mHasSavedBounds = true; }
      else if (key == "WindowW") { int v = std::stoi(val); if (v >= 750) mWindowW = v; }
      else if (key == "WindowH") { int v = std::stoi(val); if (v >= 580) mWindowH = v; }
      else if (key == "LastPNAMPath")
        if (!val.empty() && std::filesystem::exists(val)) mLastOpenedPNAMPath = val;
    }
    catch (...) {}
  }
}

void NAMPNAMEditorWindow::SaveSettings()
{
  const std::string path = GetSettingsFilePath();
  if (path.empty()) return;
  std::ofstream file(path, std::ios::trunc);
  if (!file.is_open()) return;
  file << "FontSize=" << mFontSize << "\n";
  file << "WindowX=" << mWindowX << "\n";
  file << "WindowY=" << mWindowY << "\n";
  file << "WindowW=" << mWindowW << "\n";
  file << "WindowH=" << mWindowH << "\n";
  const std::string& lastPath = mCurrentFilePath.empty() ? mLastOpenedPNAMPath : mCurrentFilePath;
  if (!lastPath.empty()) file << "LastPNAMPath=" << lastPath << "\n";
}





#endif // OS_MAC