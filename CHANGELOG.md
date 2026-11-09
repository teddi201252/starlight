# Changelog

All notable changes to this module are documented in this file.

## [0.16.0] - 2026-11-09
- The music: what plays in a pill of the top bar, the mini player (cover, progress, previous/play/next, volume, "Open Orfeus"), Orfeus's dot in the dock while its window is closed, the media keys and Super + arrows with a toast
- Opening a file in another app (SLUI SDK 1.0.12): Curiosity opens pictures in Orion
- Hover redraws only the controls it touches; typed characters reach SLUI apps without pixels (SLUI SDK 1.0.11)
- The dock shows, before anything is pinned, only the apps with `dock=yes` in their app.ini (it was every app but those with `dock=no`): a new app goes to the app manager
- The icons of Orion and Orfeus; the fonts Orfeus needs

## [0.15.0] - 2026-11-03
- A compositor that keeps to the damage: windows, the dialogs' veil, menus and the cursor no longer leave pieces of one another on screen
- The layers in one table; the dock's label above the launcher, notifications and menus
- Whole shadows for app controls, desktop widgets, title buttons, the launcher and notifications
- Windows stay below the top bar and inside the screen; dragging follows the app, not a moving pointer
- An app's window is drawn again only where it changed: its pixels (PIXELS_DAMAGE), a SLUI frame compared with the one before, a key in a text field
- --check-damage: every frame composed again in full and compared (development builds and make test-starlight-damage)

## [0.14.0] - 2026-10-30
- Pixels of an app's own beside its SLUI controls (SLUI SDK 1.0.7), with the mouse and typed text on them (1.0.8), the pointer's shape and the clipboard (1.0.9), Caps+A and Caps+S (1.0.10)
- Lines an SLUI app writes outside the protocol go to the system log
- Orbit: a cursor in the line and 5000 lines of scrollback
- Consigli: the first start's notification; Pegasus in the dock

## [0.13.1] - 2026-10-29
- New startup sound, Alba: a vibraphone climbing to E major add9 over a warm pad, synthesised from scratch (tools/startup-sound/alba.py)
- The PeOS sun replaces the star in the top bar

## [0.13.0] - 2026-10-28
- Soft UI: the redesigned desktop (pearl grey and night/neon), dock on the left, app manager, top bar, clock, calendar and widgets, night theme and accents, notifications, cursors
- Windows: resizable, with fast surface rendering; title bar and veil for dialogs
- SLUI SDK 1.0.6: soft controls and drawing (surface, face, ring, graph, veil), SLUI_EVENT_RESIZE, languages
- System menu: About PeOS, Restart, Shut Down; performance card
- Apps from /apps too (installed with appget); app titles from the bundle's language
- Languages: English (default) and Italian, chosen in Settings and applied at once
- Absolute pointer positions and one button change per frame (clicks in slow VMs)
- A closed app's state is freed, and a reopened app is told its size again

## [0.12.0] - 2026-09-14
- Enforce non-executable stacks for Starlight binaries
- Clean up invalid source pragmas

## [0.11.2] - 2026-07-14
- Fix literal minus translation in US and Italian keyboard layouts
- Wrap long Orbit terminal lines instead of scrolling horizontally
- Refresh the launcher catalog when application bundles change
- Fix launcher tooltip and context-menu damage invalidation

## [0.11.1] - 2026-07-14
- Add animated expandable launcher grid with external toggle and icon-centered hover labels

## [0.11.0] - 2026-07-14
- Better window rendering
- Optimize Starlight graphics pipeline
- Add dynamic application catalog

## [0.10.3] - 2026-06-05
- Patch maintenance release.

## [0.10.2] - 2026-06-05
- Add clipboard copy feedback effects

## [0.10.1] - 2026-05-26
- Add shared-surface Orbit framebuffer transport

## [0.10.0] - 2026-05-26
- Add configurable keyboard layouts

## [0.9.0] - 2026-05-25
- Add TrueType/OpenType vector font rendering

## [0.8.2] - 2026-05-25
- Add SLUI focus requests and new desktop backgrounds

## [0.8.1] - 2026-03-06
- Add mipmapped bilinear image downscaling for higher-quality UI icons

## [0.8.0] - 2026-03-06
- Use dynamic host-side UI text buffers for long SLUI payloads
- Improve text input event delivery for updated app behavior

## [0.7.4] - 2026-03-05
- Configurable key repeat and click-to-caret behavior

## [0.7.3] - 2026-03-05
- Context menu integration and launcher context-menu fixes

## [0.7.2] - 2026-03-05
- UI keyboard navigation and scroll behavior fixes

## [0.7.1] - 2026-03-05
- Dropdown overlay UX fixes (shadow, scroll, keyboard nav) and ESC handling cleanup

## [0.7.0] - 2026-03-04
- Add tween-based window animations, runtime animation config, and performance fixes

## [0.6.0] - 2026-03-04
- Optimize Starlight UI dirty invalidation for hover/drag/scroll and fix input regressions

## [0.5.0] - 2026-03-03
- App host pipe polling and SLUI event path compatibility fixes

## [0.4.4] - 2026-03-03
- Maintenance update.

## [0.4.3] - 2026-03-03
- Font atlas rendering tuning and offset controls.

## [0.4.2] - 2026-03-03
- Add UI panel input and gradient rendering support

## [0.4.1] - 2026-03-03
- Theme token rendering alignment and BG_CANVAS host surface

## [0.4.0] - 2026-03-03
- UI SDK themed controls, runtime theme config/load, themed SLUI protocol

## [0.3.2] - 2026-03-02
- Add keyboard event pipeline and text-input modifier handling

## [0.3.1] - 2026-02-25
- UI renderer: clip confinement and alpha rect rendering support

## [0.3.0] - 2026-02-25
- Host SLUI: supporto comandi state-aware, separator/list item e rendering SDK aggiornato

## [0.2.0] - 2026-02-25
- Added mouse wheel handling in Starlight UI, including scroll-view hover scrolling.

## [0.1.4] - 2026-02-25
- resources: migrate default backgrounds to /backgrounds and update fallback paths

## [0.1.3] - 2026-02-25
- UI renderer: clip rect and SLUI protocol extensions for scroll clipping and new widgets

## [0.1.2] - 2026-02-24
- Launcher: aggiunto placeholder icon per app senza icona
- Launcher: nome app mostrato in hover fuori dal pannello, allineato al bottone
- Hover gestito su movimento mouse senza click

## [0.1.1] - 2026-02-24
- Launcher: supporto icone da manifest (icon= e [resources]) con rendering dedicato
- Migliorata UI launcher: icona piu grande e titolo nascosto quando l'icona e presente

## [0.1.0] - 2026-02-24
- Aggiunto il versioning.

