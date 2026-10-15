# Touch overlay for Android: design

Status: phases 1 to 5 are built (`src/Platform/Touch/`, on for Android with
`TOUCHUI_ENABLED`), apart from the saber wheel. Next: the CAMERA and SABER
buttons (phase 6 and the saber colours), which wait for that engine code to
leave the iOS-only folder. In the menus a touch still acts as a mouse click.

## Goal

One touch overlay, written once in C, used by Android now and possibly by iOS
later. It replaces what `src/Platform/iOS/iosTouch.m` (about 5,200 lines of
Objective-C and UIKit) does on iOS, and it keeps the shared touch controls
standard exactly (the same layout, timings and feel as the iOS app and the
owner's other ports).

The overlay is drawn with the engine's own OpenGL ES context, on top of the
game frame, and it reads SDL3 finger events. Nothing in it is UIKit or Android
Java, apart from small platform adapters (haptics, saving photos, the gyro's
orientation).

## How iOS does it today (what to keep, what to replace)

iosTouch.m is a UIKit view laid over SDL's window. Per frame:

- `iosTouch_Update()` (Window.c, before `SDL_PollEvent`) shows or hides the
  overlay (shown while `stdControl_bControlsActive` and no cutscene) and adds
  the drag's look movement to `Window_lastXRel` / `Window_lastYRel`.
- `iosTouch_UpdateGyro()` (Window.c, after events) adds the gyro's turn the same
  way, as mouse movement scaled to the game's mouse settings.
- `iosTouch_IsScancodeDown()` (stdControl.c) ORs the overlay's held keys into
  the keyboard state: FIRE is LCTRL, JUMP is X, and so on.
- Game state and actions come from `iosGame.c` (power and weapon lists, levels,
  ammo, quick load, holding the game still while a wheel is open, items, force
  meter, third person, flight). That file is plain C and reusable as it is.

So the engine side is already small and plain C. What is UIKit-only is the
drawing (UIView layers, CAShapeLayer rings, UILabel text), the touch handling
(UITouch), the gyro (CoreMotion), haptics (UIFeedbackGenerator), settings
(NSUserDefaults) and saving photos (PhotoKit).

## Architecture

Three layers, in a new folder `src/Platform/Touch/` (name to settle with the
Enhanced extraction work, which is moving gameplay extras into `src/Enhanced`):

1. **touchUi core (`touchUi.c`, `touchUi.h`)**, pure C, no GL, no SDL video:
   - input: `touchUi_Finger(id, phase, x_pt, y_pt, t_sec)`; `touchUi_Gyro(dx_rad,
     dy_rad, t)`; `touchUi_SetScreen(w_pt, h_pt, safe insets)`.
   - state machines for every control (below), all times from the event's own
     timestamp, all distances in points (dp on Android).
   - output: held scancodes, look delta, actions (`select power`, `quick save`,
     `open menu`...) sent through `iosGame.c`-style helpers, haptic requests,
     and a **draw list** (circles, rings, arcs, rounded rects, text, icons,
     with alpha) for the frame.
   - settings load/save through a tiny key=value file in `SDL_GetPrefPath()`.
2. **touchUi GL renderer (`touchUiRender_gl.c`)**: draws the draw list after the
   game frame and HUD, before `SDL_GL_SwapWindow` (Window.c). One small GLES 3
   shader with signed-distance circles, rings and arcs (crisp at any size, no
   textures), one font atlas for labels (a bundled open-licence font baked at
   build time; never a game font, since the overlay must work before any game
   data loads), batched into one or two draw calls. It saves and restores the
   GL state it touches.
3. **Platform adapters (`touchUi_android.c`, later `touchUi_ios.m`)**: haptics,
   photo saving, gyro orientation, safe area, text input. A handful of
   functions each, behind `touchUiPlat_*`.

Glue in the engine stays where iOS already has it: the same three calls in
Window.c and stdControl.c, under `TOUCHUI_ENABLED` instead of `TARGET_IOS`.

### Input from SDL3

- `SDL_EVENT_FINGER_DOWN / MOTION / UP / CANCELED`, coordinates normalised 0..1
  over the window, turned into points with the window size and
  `SDL_GetWindowDisplayScale()`. `SDL_EVENT_FINGER_CANCELED` (a system gesture
  took the finger) must end a touch the same way as sliding off: nothing fires.
- While the overlay is shown, set `SDL_HINT_TOUCH_MOUSE_EVENTS` to 0 so fingers
  don't also click; set it back to 1 when it hides (menus are clicked by touch).
  Real mouse events (`which != SDL_TOUCH_MOUSEID`) still go to the game.
- A real mouse or controller in use can hide the overlay, as iOS does for a pad
  (`iosTouch_SetPadHud`); a touch brings it back.

### Rendering notes

- The game's HUD is drawn in the engine's virtual resolution; the overlay is
  drawn in real pixels so it stays sharp.
- Photo modes "CLEAN / HUD / HUD+BTNS" become easy: capture the framebuffer
  before or after the overlay pass.
- Fade: idle buttons at 0.65 alpha (`IOSTOUCH_IDLE_ALPHA`), pressed at 1.0.

## Components to rebuild

Each item: what it does, and the numbers to keep (from iosTouch.m; points).

1. **Move stick.** Appears where the left thumb lands in the left 42% of the
   screen, only while it is down. Radius 60, knob 24, dead zone 30%. Walks
   (W/A/S/D) however far it is pushed. Run marker (while Always Run is off): a
   15-point circle 100 above the stick (down to 76 where the top row is in the
   way), running past 70 within 30 degrees of it, walking again under 60 or
   past 40 degrees. A haptic tick when running starts.
2. **Look drag.** A drag anywhere else turns the view: 1.6 game mouse units per
   point across, 1.3 up and down. FIRE, ALT, DUCK, ACT, JUMP and FORCE pass a
   drag on through to looking. 6-point touch slop before a tap counts as a drag.
3. **Action cluster** (bottom right, 30 apart): FIRE (r 42, LCTRL), ALT (28, Z),
   DUCK (29, C), ACT (29, SPACE), JUMP (31, X), FORCE (30, F, with the force
   meter as a ring, glowing when full).
4. **Top-left row:** NEXT WPN (G), FORCE WHEEL (E), then LIGHT, IR, BACTA while
   the player has them (BACTA a 0.3 s hold).
5. **FORCE WHEEL and NEXT WPN gestures (standard):**
   - tap (lifted within 0.3 s, never slid 10 points) = next power / next weapon;
   - a 10-point slide opens the wheel at once, to slide to a slice and lift;
   - a 0.3 s hold (a ring fills) opens it to tap a slice; sliding from there
     picks by sliding again;
   - picking: dead zone 14, leash 60, hysteresis 4 degrees;
   - the gap at the bottom cancels; lifting in the middle or back at the start
     changes nothing;
   - the game holds still while a wheel is open (single player,
     `iosGame_SetHoldReason`);
   - a haptic tick per slice crossed;
   - every power and weapon in a fixed slice, not-yet-had ones dimmed; four
     gold stars per power level (0 to 4 filled); ammo under each weapon,
     greyed with no ammo to fire.
   - Wheel size: outer radius up to 168, inner 0.37 of it, selected slice pops
     out 10.
6. **Saber wheel** (SABER on the top row, once the saber is had): tap or hold
   opens it to tap a colour, a slide to slide to one (18-degree slices).
7. **QUICK SAVE / QUICK LOAD (standard):** a 0.3 s hold with a ring filling;
   fires once when full, finger still down. Sliding off cancels (even off and
   back on between frames). Two fingers fire once. Held together, whichever
   fills first; QUICK SAVE wins a tie. Nothing while the typing line is open.
8. **MENU (standard):** tap = the game menu (or closes the typing line). Hold
   0.45 s = the tray under it: SENS, GYRO, FPS, camera switch, cheat switch,
   VIEW, JUMP style, CONTROLS, keyboard. A touch anywhere on the tray's backing
   is on the nearest button. Closes after a choice (SENS and GYRO stay open) or
   a touch off it. Holding GYRO or the camera switch opens their option rows.
9. **Gyro aiming (standard):** OFF (default) / TOUCH / ALWAYS. TOUCH aims while
   either thumb is down on the game (stick, look area, or the six cluster
   buttons; not the top row, a wheel or the tray); lifting both freezes the view
   with no spring back. SENS 1.0 / 1.5 / 2.0 / 3.0, default 1.5; at 1.0 the view
   turns as far as the device does. Never aims while a wheel, the typing line or
   the tray is open, nor briefly after the screen rotates. Turn is measured
   about "up", so it works from upright to lying flat. Smoothing as on iOS
   (about 0.125 s), yaw relax 1.41. Mode, last mode and SENS are remembered.
   Shows NO GYRO if the device has none.
10. **FPS readout** left of QUICK SAVE (0.5 s period), remembered.
11. **Camera** (CAMERA button, photo kinds CLEAN / HUD / HUD+BTNS, selfies,
    burst up to 10 at 0.2 s, Action Freeze with its orbit and swipe gestures).
    This is the largest part of iosTouch.m after the wheels; it depends on the
    engine-side camera code (iosSelfie.c, iosFreeze.c, iosCapture.c) which the
    Enhanced extraction is moving. Port last.
12. **Cheat and VIEW buttons** (switched on in the tray), the flight button's
    tap / double tap / hold (0.25 / 0.35 / 0.5 s), first or third person.
13. **Typing line** for cheats: `SDL_StartTextInput()` with the window's
    keyboard; MENU closes it.
14. **Safe area:** nothing in the corners, the cutout or the gesture bar.
    `SDL_GetWindowSafeArea()` gives the insets on Android (display cutout,
    navigation bar) and iOS alike; it replaces the UIKit half of
    iosSafeArea.c, and the HUD margins (jkHud.c, jkHudInv.c) use it too.
15. **Swapped controls** (left-handed), remembered.

Things that are iPhone-sized on iOS and need a look on a tablet: the button
sizes are points, so they are physically the same size on a tablet; the stick
zone is a fraction of the width (42%), which on a 10-inch tablet is a wide area.
Keep the standard; check on the device whether the top row should sit nearer
the thumbs on large screens.

## Platform adapters

| Need | Android | iOS (later) |
|---|---|---|
| Haptic tick (wheel slice, run, camera) | JNI: `View.performHapticFeedback(HapticFeedbackConstants.CLOCK_TICK)` on the SDL surface (no permission; follows the user's touch-feedback setting); fall back to `Vibrator.vibrate(VibrationEffect.createPredefined(EFFECT_TICK))` (VIBRATE is already in the manifest) | UISelectionFeedbackGenerator |
| Gyro | `SDL_OpenSensor` on the `SDL_SENSOR_GYRO` sensor (rad/s, device axes); rotate by `SDL_GetCurrentDisplayOrientation()`; gravity from `SDL_SENSOR_ACCEL` for the "about up" maths | CoreMotion as now, or SDL sensors too |
| Settings | file in `SDL_GetPrefPath()` | same (moves off NSUserDefaults with a one-time copy) |
| Save photo | JNI: `MediaStore.Images` insert into `Pictures/OpenJKDF2` (no storage permission on Android 10+) | PhotoKit as now (iosPhotos.m) |
| Safe area | `SDL_GetWindowSafeArea()` | same |
| Typing | `SDL_StartTextInput()` | same |
| App in background | `SDL_EVENT_WILL_ENTER_BACKGROUND`: release all touches, stop the gyro | same |

## Test approach

- **Core logic on the Linux rig, no GPU needed.** The core takes finger events
  with timestamps and returns scancodes, look deltas, actions and haptic
  requests, so a test can script touches exactly ("down at (900,500) t=0, move
  to (912,500) t=0.05, up t=0.1") and check the result: a tap gives E, a 10 pt
  slide opens the wheel, 0.29 s still is a tap, 0.3 s opens it, a lift in the
  bottom gap cancels, QUICK SAVE fires at 0.3 s once, sliding off and back does
  not fire, MENU 0.45 s opens the tray, SENS cycles 1.0 to 3.0 and back,
  GYRO TOUCH stops when both thumbs lift. One table-driven test per standard
  rule.
- **Real engine on the rig.** The rig's scripted runs inject `SDL_EVENT_FINGER_*`
  with `SDL_PushEvent` (the same path Android's events take) into the real game
  with real (local-only) data, and screenshot the frame with the overlay drawn,
  at phone and tablet sizes (e.g. 2400x1080 and 2560x1600) and with fake safe
  insets.
- **Compile checks** for Android (CI) and iOS (CI) on every change.
- **Device:** the owner tests on the tablet; the iOS app keeps its UIKit overlay
  until the shared one matches it, judged side by side.

## Phased plan

1. **Core skeleton + stick + look + cluster.** Finger routing, hint switching,
   held scancodes, look delta. GL renderer for circles and labels. Rig tests.
   First playable Android build with touch.
2. **Top row, QUICK SAVE / LOAD rings, MENU tap and hold, the tray with SENS,
   GYRO, FPS, keyboard.** Settings file. Safe area. Haptics adapter.
3. **Force and weapon wheels** with all three gestures, stars, ammo, hold the
   game, haptic ticks. Biggest rig test set.
4. **Gyro** (SDL sensors, orientation, TOUCH / ALWAYS, smoothing).
5. **Saber wheel, items, VIEW, cheat button, flight, swapped controls.**
6. **Camera, selfies, burst, Action Freeze, photo saving** (after the Enhanced
   extraction settles where that engine code lives).
7. **Optional: iOS switches to the shared overlay**, behind a setting first,
   once it matches the UIKit one on the device.

Each phase: rig tests, independent review, an exp/... CI build, the owner's
test on the tablet.

## Open questions

- Tablet layout: keep the iPhone layout in points (standard), or scale the top
  row inward on screens over about 8 inches? Decide after the first device test.
- Where the shared code lives relative to `src/Enhanced` (coordinate with that
  work).
- Whether Android needs its own icon art for buttons that use SF Symbols on iOS
  (the camera and keyboard glyphs): draw them as simple vector shapes.
