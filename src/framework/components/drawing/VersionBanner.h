#pragma once

namespace d2bs::framework::drawing {

// Draw the always-on "d2bsng <version>" banner in the bottom-right screen
// corner, mirroring reference d2bs's DrawLogo. Shown both in-game and in menus
// (the onDraw hook covers both render paths). When the background update
// checker has found a newer release, an attention-colored marker is drawn just
// to the left of the banner. Called once per frame from GameLoop::OnDraw on the
// game thread.
void DrawVersionBanner();

}  // namespace d2bs::framework::drawing
