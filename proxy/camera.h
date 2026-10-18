#pragma once
namespace tymod {
    // No-Clip chase camera. Replaces the game's view-projection so Ty stays
    // centred on screen while flying. Returns true if 'out' was written.
    bool CameraApply(const float* in, float* out);

    // Record the game's own main-camera eye + forward (No-Clip move heading;
    // also the ESP's view eye/forward when 'recordView' — i.e. the game camera
    // is the one on screen).
    void CameraNoteGame(const float* in, bool recordView);

    // Get the recorded main-camera forward. Returns false until one is seen.
    bool CameraGetGameFwd(float out[3]);

    // Eye position + right/up/forward basis of the camera currently being
    // rendered. Returns false until a main camera has been seen.
    bool CameraGetViewEyeBasis(float eye[3], float right[3], float up[3], float fwd[3]);
}
