// MacMenu.mm — Native macOS menu bar for kPen
//
// Builds a minimal File menu (New, Open, Save, Save As, Quit) and an Edit menu
// (Undo, Redo, Cut, Copy, Paste) that mirror the keyboard shortcuts already
// handled in kPen's SDL run loop.  Each item posts an SDL_USEREVENT back into
// the SDL queue so kPen::run() handles everything in one place.
//
// Build: add MacMenu.mm to your Makefile/CMake and link with -framework Cocoa.
// The SDL main target must be compiled as an Objective-C++ translation unit OR
// you can compile MacMenu.mm separately (it only needs Cocoa + SDL2 headers).
//
// Usage:
//   #include "MacMenu.h"
//   // After SDL_CreateWindow:
//   MacMenu::install();

#ifdef __APPLE__
#import  <Cocoa/Cocoa.h>
#include <SDL2/SDL.h>
#include "MacMenu.h"

// ── User-event codes (must match MacMenu.h) ───────────────────────────────────
// These are placed in SDL_UserEvent.code so kPen::run() can switch on them.

// ── Menu action target ────────────────────────────────────────────────────────

@interface KPenMenuTarget : NSObject
- (void)menuAction:(NSMenuItem*)sender;
@end

@implementation KPenMenuTarget
- (void)menuAction:(NSMenuItem*)sender {
    SDL_Event ev;
    SDL_zero(ev);
    ev.type       = SDL_USEREVENT;
    ev.user.code  = (Sint32)sender.tag;
    SDL_PushEvent(&ev);
}
@end

// ── install() ─────────────────────────────────────────────────────────────────

static KPenMenuTarget* gTarget = nil;

static NSMenuItem* makeItem(NSString* title, NSString* key,
                             NSEventModifierFlags mods, int tag) {
    NSMenuItem* item = [[NSMenuItem alloc]
        initWithTitle:title
               action:@selector(menuAction:)
        keyEquivalent:key];
    item.keyEquivalentModifierMask = mods;
    item.target = gTarget;
    item.tag    = tag;
    return item;
}

void MacMenu::useArrowCursor() {
    [[NSCursor arrowCursor] set];
}

void MacMenu::install() {
    gTarget = [[KPenMenuTarget alloc] init];

    NSMenu* mainMenu = [[NSMenu alloc] init];

    // ── Apple menu (required; SDL creates one but we replace it cleanly) ──────
    {
        NSMenuItem* appleItem = [[NSMenuItem alloc] init];
        NSMenu* appleMenu = [[NSMenu alloc] initWithTitle:@"kPen"];
        [appleMenu addItemWithTitle:@"About kPen"
                             action:@selector(orderFrontStandardAboutPanel:)
                      keyEquivalent:@""];
        [appleMenu addItem:[NSMenuItem separatorItem]];
        NSMenuItem* quit = [[NSMenuItem alloc]
            initWithTitle:@"Quit kPen"
                   action:@selector(menuAction:)
            keyEquivalent:@"q"];
        quit.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        quit.target = gTarget;
        quit.tag    = MacMenu::QUIT;
        [appleMenu addItem:quit];
        appleItem.submenu = appleMenu;
        [mainMenu addItem:appleItem];
    }

    // ── File menu ─────────────────────────────────────────────────────────────
    {
        NSMenuItem* fileItem = [[NSMenuItem alloc] init];
        NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:@"File"];

        [fileMenu addItem:makeItem(@"New",     @"n", NSEventModifierFlagCommand,              MacMenu::FILE_NEW)];
        [fileMenu addItem:makeItem(@"Open…",   @"o", NSEventModifierFlagCommand,              MacMenu::FILE_OPEN)];
        [fileMenu addItem:[NSMenuItem separatorItem]];
        [fileMenu addItem:makeItem(@"Save",    @"s", NSEventModifierFlagCommand,              MacMenu::FILE_SAVE)];
        [fileMenu addItem:makeItem(@"Save As…",@"s", NSEventModifierFlagCommand|NSEventModifierFlagShift, MacMenu::FILE_SAVE_AS)];
        [fileMenu addItem:[NSMenuItem separatorItem]];
        [fileMenu addItem:makeItem(@"Close",   @"w", NSEventModifierFlagCommand,              MacMenu::FILE_CLOSE)];

        fileItem.submenu = fileMenu;
        [mainMenu addItem:fileItem];
    }

    // ── Edit menu ─────────────────────────────────────────────────────────────
    {
        NSMenuItem* editItem = [[NSMenuItem alloc] init];
        NSMenu* editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];

        [editMenu addItem:makeItem(@"Undo",  @"z", NSEventModifierFlagCommand,               MacMenu::EDIT_UNDO)];
        [editMenu addItem:makeItem(@"Redo",  @"z", NSEventModifierFlagCommand|NSEventModifierFlagShift, MacMenu::EDIT_REDO)];
        [editMenu addItem:[NSMenuItem separatorItem]];
        [editMenu addItem:makeItem(@"Cut",   @"x", NSEventModifierFlagCommand,               MacMenu::EDIT_CUT)];
        [editMenu addItem:makeItem(@"Copy",  @"c", NSEventModifierFlagCommand,               MacMenu::EDIT_COPY)];
        [editMenu addItem:makeItem(@"Paste", @"v", NSEventModifierFlagCommand,               MacMenu::EDIT_PASTE)];
        [editMenu addItem:[NSMenuItem separatorItem]];
        [editMenu addItem:makeItem(@"Select All", @"a", NSEventModifierFlagCommand,          MacMenu::EDIT_SELECT_ALL)];

        editItem.submenu = editMenu;
        [mainMenu addItem:editItem];
    }

    [NSApp setMainMenu:mainMenu];
}

#endif // __APPLE__
