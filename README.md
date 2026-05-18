# STUDY-LOCK
A lock to focus on studying.

Study Focus Lock — Code Overview
Purpose:
A full-screen focus lock application for Windows. Once activated, it covers the entire screen and restricts keyboard input until the set timer expires or the correct password is entered.

Core Features:
Screen Lock
The program creates a fullscreen topmost window with a dark blue gradient background, preventing access to the desktop until unlocked.
Keyboard Hook
A low-level keyboard hook (WH_KEYBOARD_LL) blocks all keys except numbers, colon, backspace, enter, tab, arrow keys, and escape. This prevents the user from bypassing the lock.
Timer
The user sets a duration using SS, MM:SS, or HH:MM:SS format (max 99:59:59). A smooth progress bar at the top fills left to right using millisecond precision. After time expires, the display counts negative (capped at -99:59:59) and shows TIME OVER — Press ESC to Exit.
Security Options
On startup, the program hides the logout button (NoLogoff) and fast user switching (HideFastUserSwitching) via registry. These are restored on exit through all code paths.
Sleep/Screensaver Prevention
SetThreadExecutionState prevents the monitor from sleeping or the screensaver from activating during the session.
Clock Info Popup
Clicking anywhere on the screen (except the UNLOCK button) opens a popup showing Start Time, End Time, Set Time, Elapsed Time, and Left Time.
Current Time Display
The current time in AM/PM H:MM format is displayed in the bottom-left corner, mirroring the UNLOCK button position.
Unlock Methods

Correct password entry via the UNLOCK button
ESC key after timer expires
