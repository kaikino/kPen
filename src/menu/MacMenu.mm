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
#include "version.h"

// ── User-event codes (must match MacMenu.h) ───────────────────────────────────
// These are placed in SDL_UserEvent.code so kPen::run() can switch on them.

// ── Menu action target ────────────────────────────────────────────────────────

@interface KPenMenuTarget : NSObject
- (void)menuAction:(NSMenuItem*)sender;
- (void)checkForUpdatesShowingUpToDateMessage:(BOOL)showUpToDate;
- (void)runBrewUpdateAndRelaunch;
@end

@implementation KPenMenuTarget
- (void)menuAction:(NSMenuItem*)sender {
    if (sender.tag == MacMenu::CHECK_FOR_UPDATES) {
        [self checkForUpdatesShowingUpToDateMessage:YES];
        return;
    }
    SDL_Event ev;
    SDL_zero(ev);
    ev.type       = SDL_USEREVENT;
    ev.user.code  = (Sint32)sender.tag;
    SDL_PushEvent(&ev);
}

// Trim whitespace and newlines from both ends.
static NSString* trimVersion(NSString* s) {
    return [s stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
}

// Compare "1.2.0" vs "1.1.1"; returns NSOrderedDescending if a > b, NSOrderedAscending if a < b, NSOrderedSame if equal.
static NSComparisonResult compareVersions(NSString* a, NSString* b) {
    a = trimVersion(a);
    b = trimVersion(b);
    NSArray<NSString*>* pa = [a componentsSeparatedByString:@"."];
    NSArray<NSString*>* pb = [b componentsSeparatedByString:@"."];
    for (NSUInteger i = 0; i < pa.count || i < pb.count; i++) {
        int va = (i < pa.count) ? [trimVersion(pa[i]) intValue] : 0;
        int vb = (i < pb.count) ? [trimVersion(pb[i]) intValue] : 0;
        if (va > vb) return NSOrderedDescending;
        if (va < vb) return NSOrderedAscending;
    }
    return NSOrderedSame;
}

- (void)checkForUpdatesShowingUpToDateMessage:(BOOL)showUpToDate {
    NSString* currentVersion = trimVersion([NSString stringWithUTF8String:KPEN_VERSION_STRING]);
    NSURL* url = [NSURL URLWithString:@"https://api.github.com/repos/kaikino/kpen/releases/latest"];
    NSURLSessionDataTask* task = [[NSURLSession sharedSession] dataTaskWithURL:url completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (error || !data) {
                NSAlert* alert = [[NSAlert alloc] init];
                alert.messageText = @"Could not check for updates";
                alert.informativeText = [error localizedDescription] ?: @"Please check your network connection.";
                [alert runModal];
                return;
            }
            NSError* jsonError = nil;
            id json = [NSJSONSerialization JSONObjectWithData:data options:0 error:&jsonError];
            if (![json isKindOfClass:[NSDictionary class]]) {
                NSAlert* alert = [[NSAlert alloc] init];
                alert.messageText = @"Could not check for updates";
                alert.informativeText = @"Invalid response from server.";
                [alert runModal];
                return;
            }
            NSString* tagName = [(NSDictionary*)json objectForKey:@"tag_name"];
            if (![tagName isKindOfClass:[NSString class]] || tagName.length == 0) {
                NSAlert* alert = [[NSAlert alloc] init];
                alert.messageText = @"Could not check for updates";
                alert.informativeText = @"No version information in response.";
                [alert runModal];
                return;
            }
            NSString* latestVersion = trimVersion([tagName hasPrefix:@"v"] ? [tagName substringFromIndex:1] : tagName);
            // If we couldn't determine our version at build time (e.g. no tags, shallow clone), don't prompt to update.
            BOOL unknownVersion = (currentVersion.length == 0 || [currentVersion isEqualToString:@"0.0.0"]);
            if (unknownVersion || compareVersions(latestVersion, currentVersion) == NSOrderedDescending) {
                if (unknownVersion) {
                    // Don't claim an update when we don't know our version; only show if user asked.
                    if (showUpToDate) {
                        NSAlert* alert = [[NSAlert alloc] init];
                        alert.messageText = @"You're on the latest version";
                        alert.informativeText = [NSString stringWithFormat:@"Latest release: %@", latestVersion];
                        [alert runModal];
                    }
                    return;
                }
                NSAlert* alert = [[NSAlert alloc] init];
                alert.messageText = [NSString stringWithFormat:@"Update available: %@", latestVersion];
                alert.informativeText = @"Update now with Homebrew.";
                [alert addButtonWithTitle:@"Update"];
                [alert addButtonWithTitle:@"Open releases page"];
                [alert addButtonWithTitle:@"Later"];
                NSModalResponse response = [alert runModal];
                if (response == NSAlertFirstButtonReturn) {
                    [self runBrewUpdateAndRelaunch];
                } else if (response == NSAlertSecondButtonReturn) {
                    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"https://github.com/kaikino/kpen/releases"]];
                }
            } else {
                if (showUpToDate) {
                    NSAlert* alert = [[NSAlert alloc] init];
                    alert.messageText = @"You're on the latest version";
                    alert.informativeText = [NSString stringWithFormat:@"kPen %@", currentVersion];
                    [alert runModal];
                }
            }
        });
    }];
    [task resume];
}

- (void)runBrewUpdateAndRelaunch {
    NSString* brewPath = nil;
    if ([[NSFileManager defaultManager] isExecutableFileAtPath:@"/opt/homebrew/bin/brew"])
        brewPath = @"/opt/homebrew/bin/brew";
    else if ([[NSFileManager defaultManager] isExecutableFileAtPath:@"/usr/local/bin/brew"])
        brewPath = @"/usr/local/bin/brew";
    if (!brewPath) {
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = @"Homebrew not found";
        alert.informativeText = @"Install Homebrew from https://brew.sh, or use \"Open releases page\" to download manually.";
        [alert addButtonWithTitle:@"Open releases page"];
        [alert addButtonWithTitle:@"OK"];
        if ([alert runModal] == NSAlertFirstButtonReturn)
            [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"https://github.com/kaikino/kpen/releases"]];
        return;
    }

    NSWindow* progressWin = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 280, 80)
                                                        styleMask:NSWindowStyleMaskTitled
                                                          backing:NSBackingStoreBuffered
                                                            defer:NO];
    progressWin.title = @"Updating kPen";
    progressWin.level = NSStatusWindowLevel;
    NSView* content = progressWin.contentView;
    NSTextField* label = [[NSTextField alloc] initWithFrame:NSMakeRect(20, 48, 240, 20)];
    label.stringValue = @"Running Homebrew update…";
    label.editable = NO; label.bordered = NO; label.drawsBackground = NO;
    label.font = [NSFont systemFontOfSize:[NSFont systemFontSize]];
    [content addSubview:label];
    NSProgressIndicator* spinner = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(20, 16, 24, 24)];
    spinner.style = NSProgressIndicatorStyleSpinning;
    spinner.indeterminate = YES;
    [spinner startAnimation:nil];
    [content addSubview:spinner];
    [progressWin center];
    [progressWin makeKeyAndOrderFront:nil];

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSTask* brewTask = [[NSTask alloc] init];
        brewTask.launchPath = brewPath;
        brewTask.arguments = @[ @"install", @"--cask", @"kaikino/kpen/kpen" ];
        brewTask.standardOutput = [NSPipe pipe];
        brewTask.standardError = [NSPipe pipe];
        [brewTask launch];
        [brewTask waitUntilExit];
        int status = brewTask.terminationStatus;
        NSData* errData = [brewTask.standardError fileHandleForReading].readDataToEndOfFile;
        NSString* errStr = errData.length ? [[NSString alloc] initWithData:errData encoding:NSUTF8StringEncoding] : @"";

        dispatch_async(dispatch_get_main_queue(), ^{
            [progressWin orderOut:nil];

            if (status == 0) {
                NSAlert* successAlert = [[NSAlert alloc] init];
                successAlert.messageText = @"Update installed successfully";
                successAlert.informativeText = @"kPen will restart.";
                [successAlert addButtonWithTitle:@"OK"];
                [successAlert runModal];
                NSString* appPath = @"/Applications/kPen.app";
                if ([[NSFileManager defaultManager] fileExistsAtPath:appPath]) {
                    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
                        NSTask* openTask = [[NSTask alloc] init];
                        openTask.launchPath = @"/usr/bin/open";
                        openTask.arguments = @[ appPath ];
                        [openTask launch];
                        [openTask waitUntilExit];
                        dispatch_async(dispatch_get_main_queue(), ^{
                            [NSApp terminate:nil];
                        });
                    });
                } else {
                    [NSApp terminate:nil];
                }
            } else {
                NSAlert* alert = [[NSAlert alloc] init];
                alert.messageText = @"Update failed";
                alert.informativeText = errStr.length > 0 ? errStr : @"Homebrew could not install the update.";
                [alert addButtonWithTitle:@"Open releases page"];
                [alert addButtonWithTitle:@"OK"];
                if ([alert runModal] == NSAlertFirstButtonReturn)
                    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"https://github.com/kaikino/kpen/releases"]];
            }
        });
    });
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

void MacMenu::checkForUpdatesAsync() {
    if (!gTarget) return;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.5 * NSEC_PER_SEC)),
                  dispatch_get_main_queue(), ^{
        [gTarget checkForUpdatesShowingUpToDateMessage:NO];  // no dialog when already up to date at launch
    });
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
        NSMenuItem* updateItem = [[NSMenuItem alloc]
            initWithTitle:@"Check for Updates…"
                   action:@selector(menuAction:)
            keyEquivalent:@""];
        updateItem.target = gTarget;
        updateItem.tag = MacMenu::CHECK_FOR_UPDATES;
        [appleMenu addItem:updateItem];
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
