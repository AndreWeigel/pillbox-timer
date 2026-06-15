// Display module: turns a dose state plus two strings (headline + subtitle) into
// a rendered screen and manages the refresh policy. It owns the font, the layout
// (text + pill, or pixel-art face), and the partial-vs-full refresh decision.
// It sits between the app (which decides WHAT to show) and the epd driver (HOW
// to talk to the panel). The active look is chosen by DISPLAY_STYLE in style.h.
#pragma once
#include "style.h"

void displaySetup(void);                            // init the panel (call in setup)

// Show a screen for the given dose state. No-ops if nothing changed. Uses a
// no-flash partial refresh, with a full (flashing) refresh every Nth change to
// clear ghosting. `big`/`sub` are the headline/subtitle text; in pixel style the
// headline is replaced by a face (or the pill) and only `sub` is shown.
void displayShow(DoseState state, const char* big, const char* sub);
