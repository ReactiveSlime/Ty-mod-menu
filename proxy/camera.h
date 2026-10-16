#pragma once
namespace tymod {
    // No-Clip chase camera. Replaces the game's view-projection so Ty stays
    // centred on screen while flying. Returns true if 'out' was written.
    bool CameraApply(const float* in, float* out);

    // Record the game's own main-camera forward (used as the No-Clip move heading).
    void CameraNoteGame(const float* in);

    // Get the recorded main-camera forward. Returns false until one is seen.
    bool CameraGetGameFwd(float out[3]);
}
