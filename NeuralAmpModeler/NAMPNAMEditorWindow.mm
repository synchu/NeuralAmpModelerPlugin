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
}

// ============================================================
//  Forward declaration
// ============================================================
@class NAMPNAMEditorController;
static std::string CheckSlotOverlaps(const std::vector<ModelMapSlot>& slots);

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

// Left panel
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
  [win setLevel:NSFloatingWindowLevel];
  [win setHidesOnDeactivate:NO];
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

  // ---- Toolbar row ----
  _filePathLabel = MakeLabel(@"(new file)");
  _filePathLabel.font = font;
  _filePathLabel.lineBreakMode = NSLineBreakByTruncatingMiddle;
  [cv addSubview:_filePathLabel];

  _newBtn     = MakeButton(@"New",        self, @selector(onNew:));
  _openBtn    = MakeButton(@"Open…",      self, @selector(onOpen:));
  _saveBtn    = MakeButton(@"Save",       self, @selector(onSave:));
  _saveAsBtn  = MakeButton(@"Save As…",   self, @selector(onSaveAs:));
  _fontDecBtn = MakeButton(@"A−",         self, @selector(onFontDec:));
  _fontIncBtn = MakeButton(@"A+",         self, @selector(onFontInc:));
  for (NSButton* b in @[_newBtn, _openBtn, _saveBtn, _saveAsBtn, _fontDecBtn, _fontIncBtn])
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

  _slotTable = [NSTableView new];
  _slotTable.dataSource = self;
  _slotTable.delegate = self;
  _slotTable.backgroundColor = kDarkBg();
  _slotTable.rowHeight = fs + 8;
  _slotTable.usesAlternatingRowBackgroundColors = NO;
  _slotTable.gridStyleMask = NSTableViewSolidVerticalGridLineMask;
  _slotTable.gridColor = [NSColor colorWithCalibratedWhite:55/255.f alpha:1];
  _slotTable.allowsMultipleSelection = NO;
  _slotTable.target = self;
  _slotTable.action = @selector(slotTableClicked:);
  // Register for drops
  [_slotTable registerForDraggedTypes:@[NSFilenamesPboardType]];

  for (NSArray* colDef in @[@[@"#", @"40"], @[@"Min", @"60"], @[@"Max", @"60"], @[@"Model", @"180"]])
  {
    NSTableColumn* col = [[NSTableColumn alloc] initWithIdentifier:colDef[0]];
    col.title = colDef[0];
    col.width = [colDef[1] floatValue];
    col.editable = NO;
    NSTableHeaderCell* hc = col.headerCell;
    hc.textColor = kTextClr();
    [_slotTable addTableColumn:col];
  }

  scroll.documentView = _slotTable;
  [leftPanel addSubview:scroll];

  _addBtn       = MakeButton(@"+ Add",           self, @selector(onAddSlot:));
  _removeBtn    = MakeButton(@"− Remove",         self, @selector(onRemoveSlot:));
  _upBtn        = MakeButton(@"▲ Up",             self, @selector(onMoveUp:));
  _downBtn      = MakeButton(@"▼ Down",           self, @selector(onMoveDown:));
  _distributeBtn = MakeButton(@"Distribute Gain 0→10", self, @selector(onDistribute:));
  for (NSButton* b in @[_addBtn, _removeBtn, _upBtn, _downBtn, _distributeBtn])
  {
    b.font = font;
    [leftPanel addSubview:b];
  }

  // Left layout
  NSDictionary* lv = NSDictionaryOfVariableBindings(scroll, _addBtn, _removeBtn, _upBtn, _downBtn, _distributeBtn);
  [leftPanel addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"H:|[scroll]|" options:0 metrics:nil views:lv]];
  [leftPanel addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"H:|[_addBtn]-4-[_removeBtn]-4-[_upBtn]-4-[_downBtn]" options:0 metrics:nil views:lv]];
  [leftPanel addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"H:|[_distributeBtn]|" options:0 metrics:nil views:lv]];
  [leftPanel addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"V:|[scroll]-4-[_addBtn]-4-[_distributeBtn]-8-|" options:0 metrics:nil views:lv]];
  [NSLayoutConstraint activateConstraints:@[
    [_removeBtn.topAnchor constraintEqualToAnchor:_addBtn.topAnchor],
    [_upBtn.topAnchor constraintEqualToAnchor:_addBtn.topAnchor],
    [_downBtn.topAnchor constraintEqualToAnchor:_addBtn.topAnchor],
  ]];

  // ---- Right panel ----
  NSView* rightPanel = [NSScrollView new];
  NSScrollView* rightScroll = (NSScrollView*)rightPanel;
  rightScroll.translatesAutoresizingMaskIntoConstraints = NO;
  rightScroll.drawsBackground = YES;
  rightScroll.backgroundColor = kDarkBg();
  NSView* rContent = [NSView new];
  rContent.translatesAutoresizingMaskIntoConstraints = NO;
  rightScroll.documentView = rContent;

  auto addRow = [&](NSString* labelStr, NSTextField* edit, NSButton* checkbox) -> void {
    if (checkbox)
    {
      checkbox.font = font;
      [rContent addSubview:checkbox];
    }
    else
    {
      NSTextField* lbl = MakeLabel(labelStr);
      lbl.font = font;
      [rContent addSubview:lbl];
    }
    edit.font = font;
    [rContent addSubview:edit];
  };

  _gainMinEdit   = MakeEdit(@"0.00");
  _gainMaxEdit   = MakeEdit(@"10.00");
  _namPathEdit   = MakeEdit(@"");
  _browseBtn     = MakeButton(@"Browse…", self, @selector(onBrowseNAM:));
  _browseBtn.font = font;
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

  NSTextField* selLbl   = MakeLabel(@"Selected Slot:");
  NSTextField* gainLbl  = MakeLabel(@"Gain Min (0–10):");
  NSTextField* gainMaxLbl = MakeLabel(@"Max:");
  NSTextField* namLbl   = MakeLabel(@"NAM File:");
  NSTextField* ovLbl    = MakeLabel(@"Overrides (optional):");
  selLbl.font = gainLbl.font = gainMaxLbl.font = namLbl.font = ovLbl.font = font;
  _applyBtn.font = _previewSlotBtn.font = _previewChainBtn.font = font;
  _namPathEdit.font = font;
  _browseBtn.font = font;

  for (NSView* v in @[selLbl, gainLbl, gainMaxLbl, namLbl, ovLbl,
                      _gainMinEdit, _gainMaxEdit, _namPathEdit, _browseBtn,
                      _ovOutputCheck, _ovOutputEdit, _ovBassCheck, _ovBassEdit,
                      _ovMidCheck, _ovMidEdit, _ovTrebleCheck, _ovTrebleEdit,
                      _applyBtn, _previewSlotBtn, _previewChainBtn])
    [rContent addSubview:v];

  // Layout right panel with visual format
  const int lw = 180, ew = 90;
  NSDictionary* rv = NSDictionaryOfVariableBindings(
    selLbl, gainLbl, gainMaxLbl, namLbl, ovLbl,
    _gainMinEdit, _gainMaxEdit, _namPathEdit, _browseBtn,
    _ovOutputCheck, _ovOutputEdit, _ovBassCheck, _ovBassEdit,
    _ovMidCheck, _ovMidEdit, _ovTrebleCheck, _ovTrebleEdit,
    _applyBtn, _previewSlotBtn, _previewChainBtn);
  NSDictionary* metrics = @{@"lw": @(lw), @"ew": @(ew), @"m": @8};

  NSMutableArray* cs = [NSMutableArray array];
  auto vf = [&](NSString* fmt) {
    [cs addObjectsFromArray:[NSLayoutConstraint constraintsWithVisualFormat:fmt options:0 metrics:metrics views:rv]];
  };

  vf(@"V:|-m-[selLbl]-m-[gainLbl]-m-[namLbl]-m-[_namPathEdit]-m-[ovLbl]-m-[_ovOutputCheck]-m-[_ovBassCheck]-m-[_ovMidCheck]-m-[_ovTrebleCheck]-m-[_applyBtn]-m-[_previewSlotBtn]-m-|");
  vf(@"H:|-m-[selLbl]-|");
  vf(@"H:|-m-[gainLbl(lw)]-4-[_gainMinEdit(ew)]-8-[gainMaxLbl]-4-[_gainMaxEdit(ew)]");
  vf(@"H:|-m-[namLbl]-|");
  vf(@"H:|-m-[_namPathEdit]-4-[_browseBtn(80)]|");
  vf(@"H:|-m-[ovLbl]-|");
  vf(@"H:|-m-[_ovOutputCheck(lw)]-4-[_ovOutputEdit(ew)]");
  vf(@"H:|-m-[_ovBassCheck(lw)]-4-[_ovBassEdit(ew)]");
  vf(@"H:|-m-[_ovMidCheck(lw)]-4-[_ovMidEdit(ew)]");
  vf(@"H:|-m-[_ovTrebleCheck(lw)]-4-[_ovTrebleEdit(ew)]");
  vf(@"H:|-m-[_applyBtn(180)]");
  vf(@"H:|-m-[_previewSlotBtn]-4-[_previewChainBtn]");

  [cs addObjectsFromArray:@[
    [gainLbl.centerYAnchor constraintEqualToAnchor:_gainMinEdit.centerYAnchor],
    [gainMaxLbl.centerYAnchor constraintEqualToAnchor:_gainMinEdit.centerYAnchor],
    [_gainMaxEdit.centerYAnchor constraintEqualToAnchor:_gainMinEdit.centerYAnchor],
    [_previewChainBtn.topAnchor constraintEqualToAnchor:_previewSlotBtn.topAnchor],
    [_browseBtn.centerYAnchor constraintEqualToAnchor:_namPathEdit.centerYAnchor],
    [_ovOutputEdit.centerYAnchor constraintEqualToAnchor:_ovOutputCheck.centerYAnchor],
    [_ovBassEdit.centerYAnchor constraintEqualToAnchor:_ovBassCheck.centerYAnchor],
    [_ovMidEdit.centerYAnchor constraintEqualToAnchor:_ovMidCheck.centerYAnchor],
    [_ovTrebleEdit.centerYAnchor constraintEqualToAnchor:_ovTrebleCheck.centerYAnchor],
    [rContent.widthAnchor constraintGreaterThanOrEqualToConstant:340],
  ]];
  [NSLayoutConstraint activateConstraints:cs];

  // Add to split view
  [split addSubview:leftPanel];
  [split addSubview:rightScroll];
  [split setHoldingPriority:NSLayoutPriorityDefaultLow forSubviewAtIndex:0];

  // Root layout
  NSDictionary* tv = NSDictionaryOfVariableBindings(
    split, _filePathLabel, _newBtn, _openBtn, _saveBtn, _saveAsBtn, _fontDecBtn, _fontIncBtn);
  NSDictionary* tm = @{@"m": @8, @"bw": @90, @"fbw": @40};
  [cv addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
    @"H:|-m-[_filePathLabel]-m-[_newBtn(bw)]-4-[_openBtn(bw)]-4-[_saveBtn(bw)]-4-[_saveAsBtn(bw)]-4-[_fontDecBtn(fbw)]-4-[_fontIncBtn(fbw)]-m-|"
    options:0 metrics:tm views:tv]];
  [cv addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
    @"V:|-m-[_newBtn]-m-[split]-m-|"
    options:0 metrics:tm views:tv]];
  [cv addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"H:|-m-[split]-m-|"
    options:0 metrics:tm views:tv]];
  [cv addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:
    @"V:|-m-[_filePathLabel]"
    options:0 metrics:tm views:tv]];
  [NSLayoutConstraint activateConstraints:@[
    [_filePathLabel.centerYAnchor constraintEqualToAnchor:_newBtn.centerYAnchor],
  ]];

  // Set initial splitter position
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

- (void)tableViewSelectionDidChange:(NSNotification*)note
{
  NSInteger sel = _slotTable.selectedRow;
  if (sel >= 0 && sel < (NSInteger)self.slots->size())
  {
    [self updateEditPanelFromSlot:(int)sel];
    [self setEditPanelEnabled:YES];
  }
  else
  {
    [self setEditPanelEnabled:NO];
  }
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
      slot.namFilePath = jSlot.value("nam_path", std::string{});
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
  json j;
  j["pnam_version"] = 1;
  j["slots"] = json::array();
  for (const auto& slot : *self.slots)
  {
    json jSlot;
    jSlot["amp_gain_min"] = slot.ampGainMin;
    jSlot["amp_gain_max"] = slot.ampGainMax;
    jSlot["nam_path"]     = slot.namFilePath;
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
  NSInteger idx = _slotTable.selectedRow;
  if (idx <= 0 || idx >= (NSInteger)self.slots->size()) return;
  std::swap((*self.slots)[idx], (*self.slots)[idx - 1]);
  *self.dirty = true;
  [self refreshSlotTable];
  [self selectSlotIndex:(int)idx - 1];
  [self updateTitleBar];
}

- (void)onMoveDown:(id)s
{
  NSInteger idx = _slotTable.selectedRow;
  if (idx < 0 || idx >= (NSInteger)self.slots->size() - 1) return;
  std::swap((*self.slots)[idx], (*self.slots)[idx + 1]);
  *self.dirty = true;
  [self refreshSlotTable];
  [self selectSlotIndex:(int)idx + 1];
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
  NSInteger idx = _slotTable.selectedRow;
  if (idx < 0 || idx >= (NSInteger)self.slots->size()) return;
  (*self.slots)[(size_t)idx] = [self readEditPanelToSlot];
  *self.dirty = true;
  [self refreshSlotTable];
  [self selectSlotIndex:(int)idx];
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

- (void)applyFont
{
  NSFont* font = [NSFont systemFontOfSize:self.currentFontSize];
  for (NSControl* c in @[_filePathLabel, _newBtn, _openBtn, _saveBtn, _saveAsBtn,
                          _fontDecBtn, _fontIncBtn, _addBtn, _removeBtn, _upBtn, _downBtn,
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
                                        fontSizePtr:&mFontSize];   // ← add this

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
    else if (pParentWindow)
    {
      if (NSWindow* parentWin = ((__bridge NSView*)pParentWindow).window)
      {
        NSRect pf = parentWin.frame;
        [ctrl.window setFrame:NSMakeRect(pf.origin.x + pf.size.width + 10,
                                         pf.origin.y, mWindowW, mWindowH)
                      display:NO];
      }
    }

    ctrl.onWindowClosed = [this]() {
      // Save bounds before closing
      NSRect frame = ((__bridge NAMPNAMEditorController*)mpWindowController).window.frame;
      NSScreen* scr = [NSScreen mainScreen];
      mWindowX = (int)frame.origin.x;
      mWindowY = (int)(NSMaxY(scr.frame) - NSMaxY(frame));
      mWindowW = (int)frame.size.width;
      mWindowH = (int)frame.size.height;
      mHasSavedBounds = true;
      SaveSettings();
      mIsOpen = false;
      if (mOnWindowClosed) mOnWindowClosed();
    };
    ctrl.onSaved       = mOnSaved;
    ctrl.onPreviewSlot = mOnPreviewSlot;
    ctrl.onPreviewChain = mOnPreviewChain;

    if (!mSlots.empty())
    {
      [ctrl refreshSlotTable];
      [ctrl updateTitleBar];
      [ctrl selectSlotIndex:0];
    }

    mpWindowController = (__bridge_retained void*)ctrl;
    [ctrl showWindow:nil];
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
    // Window not open yet — parse into mSlots directly (mirrors C++ LoadFile logic)
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
        slot.namFilePath = jSlot.value("nam_path", std::string{});
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