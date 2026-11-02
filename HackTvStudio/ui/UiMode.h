#ifndef UIMODE_H
#define UIMODE_H

/*
 * Compact "head-unit" layout on every platform: spectrum + waterfall on top,
 * one row of large controls per tab, everything else in the tab's ⚙ dialog.
 * It started on Android (1280x720 in-dash screens) and the desktop adopted it
 * for Full HD so that nothing is clipped or scrolled. Platform-specific
 * behaviour (microphone-only TX, defaults, GL setup) still keys on Q_OS_ANDROID.
 */
#define HACKTV_COMPACT_UI 1

#endif // UIMODE_H
