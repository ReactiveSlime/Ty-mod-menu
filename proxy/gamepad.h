#pragma once
namespace tymod {
    void  GamepadInit();        // resolve + hook XInput (call once from ModInit)
    void  GamepadTick();        // per-frame: R3+L3 menu toggle, menu cursor, fast cache
    bool  GamepadCaptured();    // menu open or No-Clip -> the game must not read pad movement
    bool  GamepadFastHeld();    // RB held -> No-Clip 3x (like Shift)

    // Live movement axes for No-Clip flight (raw stick/trigger, menu or not):
    //   move[0]=fwd/back (LY), move[1]=strafe (LX), move[2]=up/down (triggers),
    //   and yaw for the two shoulder buttons. Values in [-1, 1].
    void  GamepadMoveAxes(float move[3]);  // fwd(-1..1), strafe(-1..1), up(-1..1)
}
