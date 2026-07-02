# Pluma action migration matrix

This document is the behavioural baseline for replacing `GtkAction` and
`GtkUIManager`.  A migration is complete only when the new action preserves
the callback, accelerator, state and sensitivity rule listed here.

## Window and document actions

| Legacy action | New action | Scope | Accelerator | State | Sensitivity rule |
|---|---|---|---|---|---|
| `FileNew` | `win.new` | window | `<Ctrl>N` | stateless | Always enabled |
| `FileOpen` | `win.open` | window | `<Ctrl>O` | stateless | Always enabled |
| `FileOpenFolder` | `win.open-folder` | window | — | stateless | Always enabled |
| `FileSave` | `win.save` | window | `<Ctrl>S` | stateless | Active document exists and can be saved |
| `FileSaveAs` | `win.save-as` | window | `<Ctrl><Shift>S` | stateless | Active document exists |
| `FileSaveAll` | `win.save-all` | window | `<Ctrl><Shift>L` | stateless | At least one document exists |
| `FileRevert` | `win.revert` | window | — | stateless | Active document has a location and is not loading/saving |
| `FilePrintPreview` | `win.print-preview` | window | `<Ctrl><Shift>P` | stateless | Active document exists and is printable |
| `FilePrint` | `win.print` | window | `<Ctrl>P` | stateless | Active document exists and is printable |
| `FileClose` | `win.close` | window | `<Ctrl>W` | stateless | Active document exists |
| `FileCloseAll` | `win.close-all` | window | `<Ctrl><Shift>W` | stateless | At least one document exists |
| `FileCloseTabsLeft` | `win.close-tabs-left` | window | — | stateless | Active tab has a tab to its left |
| `FileCloseTabsRight` | `win.close-tabs-right` | window | — | stateless | Active tab has a tab to its right |
| `FileCloseOtherTabs` | `win.close-other-tabs` | window | — | stateless | More than one tab exists |
| `FileQuit` | `app.quit` | application | `<Ctrl>Q` | stateless | Always enabled |
| `DocumentsPreviousDocument` | `win.previous-document` | window | `<Ctrl><Alt>Page_Up` | stateless | More than one tab exists |
| `DocumentsNextDocument` | `win.next-document` | window | `<Ctrl><Alt>Page_Down` | stateless | More than one tab exists |
| `DocumentsMoveToNewWindow` | `win.move-to-new-window` | window | — | stateless | More than one tab exists |

## Edit, search and view actions

| Legacy action | New action | Scope | Accelerator | State | Sensitivity rule |
|---|---|---|---|---|---|
| `EditUndo` | `win.undo` | window | `<Ctrl>Z` | stateless | Active document can undo and view is editable |
| `EditRedo` | `win.redo` | window | `<Ctrl><Shift>Z` | stateless | Active document can redo and view is editable |
| `EditCut` | `win.cut` | window | `<Ctrl>X` | stateless | Editable view has a selection |
| `EditCopy` | `win.copy` | window | `<Ctrl>C` | stateless | Active view has a selection |
| `EditPaste` | `win.paste` | window | `<Ctrl>V` | stateless | View is editable and clipboard contains text |
| `EditDelete` | `win.delete` | window | — | stateless | Editable view has a selection |
| `EditSelectAll` | `win.select-all` | window | `<Ctrl>A` | stateless | Active document exists |
| `EditZoomIn` | `win.zoom-in` | window | `<Ctrl>plus` | stateless | Active view exists |
| `EditZoomOut` | `win.zoom-out` | window | `<Ctrl>minus` | stateless | Active view exists |
| `EditZoomReset` | `win.zoom-reset` | window | `<Ctrl>equal` | stateless | Active view exists |
| `SearchFind` | `win.find` | window | `<Ctrl>F` | stateless | Active document exists |
| `SearchFindInFiles` | `win.find-in-files` | window | `<Ctrl><Shift>F` | stateless | Project/folder context exists |
| `SearchFindNext` | `win.find-next` | window | `<Ctrl>G` | stateless | Active document has search text |
| `SearchFindPrevious` | `win.find-previous` | window | `<Ctrl><Shift>G` | stateless | Active document has search text |
| `SearchReplace` | `win.replace` | window | `<Ctrl>H` | stateless | Active view is editable |
| `SearchClearHighlight` | `win.clear-highlight` | window | `<Ctrl><Shift>K` | stateless | Active document exists |
| `SearchGoToLine` | `win.goto-line` | window | `<Ctrl>I` | stateless | Active document exists |
| `SearchIncrementalSearch` | `win.incremental-search` | window | `<Ctrl>K` | stateless | Active document exists |
| `ViewToolbar` | `win.show-toolbar` | window | — | boolean | Always enabled; mirrors window setting |
| `ViewStatusbar` | `win.show-statusbar` | window | — | boolean | Always enabled; mirrors window setting |
| `ViewSidePane` | `win.show-side-pane` | window | `F9` | boolean | Enabled when side pane has items |
| `ViewBottomPane` | `win.show-bottom-pane` | window | `<Ctrl>F9` | boolean | Enabled when bottom pane has items |
| `ViewRightPane` | `win.show-right-pane` | window | `<Shift>F9` | boolean | Enabled when right pane has items |
| `ViewFullscreen` | `win.fullscreen` | window | `F11` | boolean | Always enabled; mirrors window fullscreen state |
| `ViewHighlightMode` | `win.highlight-mode` | window | — | string | Active document exists; value is language id |

## Plugin action groups

| Component | New scope | Lifecycle requirement |
|---|---|---|
| Time | `plugin-time.*` | Insert on activation and remove on deactivation |
| Sort | `plugin-sort.*` | Insert on activation and remove on deactivation |
| Document statistics | `plugin-docinfo.*` | Insert on activation and remove on deactivation |
| Spell checker | `plugin-spell.*` | Stateful inline-spell action must follow the active document |
| External Tools | `plugin-externaltools.*` | Rebuild dynamic menu and accelerators when tools change |

## Baseline scenarios

1. Open, modify, save, save-as, revert and close local and remote documents.
2. Switch between modified, read-only, untitled and loading tabs.
3. Exercise undo/redo, selection, clipboard and search state transitions.
4. Toggle every pane, toolbar, statusbar and fullscreen from menu and shortcut.
5. Activate and deactivate each plugin repeatedly, then trigger its former shortcut.
6. Verify keyboard traversal of the tag-list panel before changing its focus chain.

