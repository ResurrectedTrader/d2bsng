#pragma once

namespace d2bs::framework::drawing {

// Draw the always-on "d2bsng <version>" banner in the bottom-right screen
// corner, mirroring reference d2bs's DrawLogo. Shown both in-game and in menus
// (the onDraw hook covers both render paths). When the background update
// checker has found a newer release, the banner shifts left and a
// "(<version> available)" notice is drawn to its right. Called once per frame
// from GameLoop::OnDraw on the game thread.
void DrawVersionBanner();

}  // namespace d2bs::framework::drawing
