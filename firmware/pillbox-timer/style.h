// Shared display-style selector and dose-state enum, used by both the app
// (which picks the wording) and the display module (which picks the visuals).
#pragma once

// Three selectable looks for the dose screen (a button could cycle these later):
//   STYLE_PLAIN  big caps headline + pill icon       (DONE / TAKE / OVERDUE)
//   STYLE_FUN    same layout, cheeky wording          (NAILED / OI / OI AGAIN)
//   STYLE_PIXEL  pixel-art face (or pill) + subtitle  (happy / pill / worried)
#define STYLE_PLAIN 0
#define STYLE_FUN   1
#define STYLE_PIXEL 2

// State of the current dose cycle. STATE_INFO is the non-dose case (last-opened
// mode), which has no face and just shows text.
enum DoseState { STATE_DONE, STATE_TAKE, STATE_OVERDUE, STATE_INFO };
