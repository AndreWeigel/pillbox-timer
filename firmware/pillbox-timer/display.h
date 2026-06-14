// Display module: turns two strings (headline + subtitle) into a rendered screen
// and manages the refresh policy. It owns the font, the layout (headline, pill
// icon, subtitle), and the decision of when to use a partial vs full refresh.
// It sits between the app (which decides WHAT to show) and the epd driver (which
// knows HOW to talk to the panel).
#pragma once

void displaySetup(void);                            // init the panel (call in setup)

// Show a headline + subtitle. No-ops if unchanged. Picks a no-flash partial
// refresh, with a full (flashing) refresh every Nth change to clear ghosting.
// Pass "" for sub to leave the subtitle blank.
void displayShow(const char* big, const char* sub);
