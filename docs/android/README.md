# Android test build: install and test on a tablet

An early test build of OpenJKDF2 for Android tablets and phones (64-bit ARM,
which is nearly every device from the last several years). It has multiplayer
over ENet (the same as the iOS app), and plays with on-screen touch controls
(the same layout and gestures as the iPhone app), a keyboard, a mouse or a
game controller. In the menus a touch acts as a mouse click. How the touch
controls are built is in `TOUCH_OVERLAY_PLAN.md`.

It is one app with two icons: **OpenJKDF2 Enhanced** (Jedi Knight) and
**OpenMoTS Enhanced** (Mysteries of the Sith). It installs next to upstream's
OpenJKDF2 app, not over it. No game files come with it: you use your own copy
of the game.

Needs Android 12 or newer to be safe (the game code is built for Android 12;
older versions may refuse to start it).

## 1. Get the APK

1. On the tablet, sign in to GitHub in the browser and open the repository's
   Actions page, workflow **Android APK experiment**, then the newest run with a
   green tick.
2. Under **Artifacts**, tap **OpenJKDF2-Enhanced-android-arm64**. It downloads
   as a .zip.
3. Open the .zip in the Files app and extract it. Inside is
   `OpenJKDF2-Enhanced-android-arm64.apk`.

(Artifacts are kept for 30 days.)

## 2. Install it

1. Tap the .apk. Android asks to allow installs from this app (the Files app or
   the browser): allow it, go back, tap **Install**.
2. If Play Protect warns about an unknown app, choose to install anyway (it is
   a test build, not from the Play Store).
3. A newer build installs over the old one and keeps your files, as long as
   both were signed with the same key. If Android says the app "conflicts with
   an existing package" or "isn't compatible", the key changed. Then:
   1. With the Files app (or a computer), copy the `jk1` and `mots` folders
      (they hold your saves too) out of
      `Android/data/org.openjkdf2.enhanced.jk/files/` to somewhere safe, such
      as `Download/`.
   2. Uninstall the app fully (if asked whether to keep its data, say **no**:
      kept data still blocks the new key).
   3. Install the new .apk, then copy `jk1` and `mots` back into that folder.

### Keeping the key the same (once, for the owner)

The builds are signed with a key kept in GitHub's build cache. GitHub forgets
it after 7 days with no build, and the next build then has a new key (step 3
above). To make the key permanent, store one as a repository secret:

1. On a computer with Java, run:
   `keytool -genkeypair -keystore debug.keystore -storepass android -alias androiddebugkey -keypass android -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US"`
   then `base64 -w0 debug.keystore > key.txt` (on a Mac: `base64 -i debug.keystore -o key.txt`).
2. On GitHub: the repository > **Settings** > **Secrets and variables** >
   **Actions** > **New repository secret**. Name: `ANDROID_DEBUG_KEYSTORE_B64`.
   Value: the contents of `key.txt`. Save.
3. Keep `debug.keystore` somewhere private and delete `key.txt`.

(Or ask a Claude session to do this for you with the `gh` tool.) The first
build after that has the new key, so do the copy-out / reinstall steps once.
Every build after it installs over the last one.

## 3. Copy in your game files

The game looks for its files in the app's own folder:

- Jedi Knight: `Android/data/org.openjkdf2.enhanced.jk/files/jk1/`
- Mysteries of the Sith: `Android/data/org.openjkdf2.enhanced.jk/files/mots/`

(both apps share the one app id; each game has its own folder). Inside, the
same layout as the game's own install: `episode/`, `resource/`, and `MUSIC/`
if you have the music.

Three ways to get them there, easiest first:

- **Let the app copy them.** Put your game folder (an installed copy of the
  game, or the CD's contents) anywhere on the tablet, for example in
  `Download/`. Start the app. With no game files it says so and opens a folder
  picker: pick the game folder and it copies what it needs. Do the same once in
  OpenMoTS for Mysteries of the Sith.
- **From a computer with a USB cable.** Connect the tablet, choose "File
  transfer" on it, and on the computer copy the files into the folders above.
- **With the Files app.** The app shows up as **OpenJKDF2 Enhanced** in the
  side menu of Android's own Files app (and in any "open from" picker), so you
  can copy into it there. Some makers' file managers don't show it; use the
  Files app that came with Android.

Required for Jedi Knight: `episode/JK1.gob`, `episode/JK1CTF.gob`,
`episode/JK1MP.gob`, `resource/Res1hi.gob`, `resource/Res2.gob`,
`resource/jk_.cd`. For Mysteries of the Sith: `episode/JKM.goo`,
`episode/JKM_KFY.goo`, `episode/JKM_MP.goo`, `episode/JKM_SABER.goo`,
`resource/Jkmres.goo`, `resource/JKMsndLO.goo`, `resource/jk_.cd`.

Please keep your game files to yourself: don't upload them anywhere.

## 4. What to test

Tick what works, note what doesn't (a photo of the screen helps).

**Touch controls** (in a level; they work like the iPhone app's)
- [ ] The buttons show once a level starts, none in the menus or cutscenes,
      and nothing sits under the camera hole, the screen's rounded corners or
      the navigation bar. Say how big they feel on the tablet.
- [ ] Left thumb: the stick appears where it lands and walks. Push up past
      the ring onto the small "^^" circle: it runs (a light tick).
- [ ] Right thumb: dragging anywhere else turns the view; dragging on FIRE
      fires and turns.
- [ ] FIRE, ALT, DUCK, ACT, JUMP, FORCE (FORCE shows the power and its meter
      ring, glowing when full).
- [ ] NEXT WPN / FORCE WHEEL: a quick tap is the next weapon / power. Touch
      and slide: the wheel opens at once; slide to a slice and lift. Touch and
      hold still: a ring fills, the wheel opens to tap a slice. The gap at the
      bottom cancels. The game stands still while a wheel is open. Each slice
      you cross gives a small tick (if the tablet has haptics). Learned powers
      show their level as gold stars.
- [ ] QUICK SAVE / QUICK LOAD: only a hold saves or loads (a ring fills, about
      a third of a second). Slide off before it fills: nothing.
- [ ] MENU: a tap opens the game menu. Hold it: the tray opens under it:
      GYRO (tap: on/off; hold: MODE and SENS), FPS, CHEAT, LEVEL (only while
      the cheat is on), VIEW, JUMP style, CONTROLS (swap sides), the camera
      switch and the keyboard (for cheats).
- [ ] GYRO: off to start. Switch it on (TOUCH): tilting the tablet aims while
      a thumb is on the screen; lift both thumbs and the view stays put. Try
      ALWAYS too, and the SENS steps (1.0x, 1.5x, 2.0x, 3.0x). Both ways round
      in landscape. If it says NO GYRO, the tablet has no gyroscope.
- [ ] CHEAT (switch it on in the tray): hold it half a second to turn the
      cheat on/off; with it on, holding JUMP flies and ACT is noclip. DUCK
      only ever crouches.
- [ ] LEVEL (in the tray while the cheat is on): hold it; after half a second
      it counts 1, 2, 3 (a buzz each, "Level 3 of 21" at the top). Lift to go
      there. Slide off, or touch elsewhere to close the tray: no level. A quick
      tap only says "Hold LEVEL to count levels".
- [ ] CAMERA (the camera at the top, on unless switched off in the tray):
      - a tap takes a photo (a white flash and a tap felt); "1 photo saved to
        Pictures/OpenJKDF2" shows at the top. Check the photo is in the
        tablet's Gallery / Photos app, in an OpenJKDF2 album;
      - touch and slide sideways: a burst, a photo every 0.2 s while your
        finger stays down (up to 10), the count beside the button;
      - touch and hold still half a second: the selfie (3 photos, the camera
        at arm's length; everyone near poses);
      - slide down: Action Freeze, the game stops; one finger turns the camera
        round, two fingers move it and pinch to zoom, CAMERA takes a photo,
        the X (top right) goes back to the game exactly where it was;
      - slide up: the cinematic burst, 5 photos round the frozen action, then
        the game goes on.
      None of the photo modes work in multiplayer (a message says so).
- [ ] The camera options: hold the tray's camera switch: PHOTO (which photos a
      tap takes: CLEAN, HUD, HUD+BTNS with the touch buttons on), SELFIES
      (1, 3 or 5), FREEZE (whether sliding down/up stops the action).
- [ ] SABER (once you have the lightsaber, or with the cheat on): a tap opens
      the saber wheel: tap a colour (ORIGINAL is the game's own).
      On the left: SINGLE (one saber), DUAL (two sabers; then LEFT / BOTH /
      RIGHT on the right choose which blade a colour goes to) and CROSS (one
      saber with a crossguard: a jagged, crackling blade that comes to a
      point, two short blades out of small metal vents either side of the
      hilt, a strong glow; only the main blade hits). EQUIP puts the saber in
      your hand.
      The gap at the bottom (or a tap off the wheel) closes it. A slide from
      SABER opens it to slide to a colour.
- [ ] CLASSIC / REFORGED (at the top of the saber wheel's middle): a tap
      switches the colours' look, and the wheel stays open. REFORGED (to
      start): the film-style glow, and in Jedi Knight 16 colours (no green).
      CLASSIC: the earlier look, 17 colours in both games. It changes how
      you see every blade, yours and other players', and the blade in hand
      changes at once. In Jedi Knight a green you kept shows as ORIGINAL
      while REFORGED is on, and other players see it as ORIGINAL too.
- [ ] FILM (it was called BEAM; an on/off switch under SINGLE, DUAL and
      CROSS): a tap turns the film look on or off, and the wheel stays open.
      Off (to start): the game's blade, narrowing to a point. On: a blade as
      in the films, as long as the game's own and slim to the films'
      proportions, even with a rounded end, a white-hot core, its colour at
      the edges and in a soft glow that only adds light, and a faint
      shimmer, for every colour and both looks; the blade in hand changes at
      once. Hits stay the game's either way, and already reach past its own
      blade (a normal swing a quarter further than the blade it draws, the
      strongest attacks up to two and a half times as far). It's for your own screen:
      your blade, and players whose game sends their saber choice (not
      enemy Jedi). It doesn't change CROSS, which keeps its own blade: the
      switch is dimmed while CROSS is picked. A pad's D-pad up and the 5
      key do the same while the saber wheel is open; a pad's X and the 1 key
      step through SINGLE, DUAL and CROSS.
- [ ] Use a keyboard, mouse or controller: the buttons hide; touch the screen
      and they come back.
- [ ] Quit and start again: GYRO, SENS, FPS, CHEAT, VIEW, CONTROLS, the
      camera options and the saber's colours, style, look and FILM are kept.

**Start up**
- [ ] Both icons appear; each starts its own game (Jedi Knight / MotS).
- [ ] The intro and menus show, full screen, the right way round.
- [ ] Sound and music play.
- [ ] Tapping menu items works (touch acts as a mouse click), and a tap
      skips a cutscene.

**Keyboard and mouse** (Bluetooth or USB)
- [ ] Walk with W/A/S/D, fire, jump, use; Esc opens the menu.
- [ ] The mouse turns the view freely in game (no pointer stuck at the screen
      edge); back in the menu the pointer shows again.
- [ ] Mouse buttons fire.

**Game controller** (Bluetooth or USB: Xbox, PlayStation, Switch Pro...)
- [ ] Sticks move and look; triggers and buttons work.
- [ ] Controls > Joystick in the game's settings lets you change the buttons.
- [ ] Unplug or switch off the controller and back on during play.

**Saving**
- [ ] Save a game, quit the app fully, start again, load it.

**Multiplayer** (needs a second device: the iOS app, a desktop build, or
another Android)
- [ ] Host a game on the tablet; join it from the other device by typing the
      tablet's address (Settings > About tablet > Status, or the Wi-Fi details).
- [ ] Join a game hosted elsewhere by its address.
- [ ] Play a few minutes: movement, firing and chat look right on both.
- Games nearby are **not** listed automatically on Android yet: type the
  address.

**Other**
- [ ] Switch to another app and back during play and during a cutscene.
- [ ] Turn the tablet round (landscape both ways).

## Known gaps

- Photos: on Android 10 and newer they go to the gallery (Pictures/
  OpenJKDF2). On Android 9 and older, or if that fails, they go to the app's
  own folder instead (Android/data/<app>/files/Pictures/OpenJKDF2, seen with a
  file manager or a computer), and the message says "the app's
  Pictures/OpenJKDF2". The camera has no shutter sound on Android.
- The touch controls were tested on a Linux test setup with the real game and
  simulated touches, not on an Android device: sizes, the gyro's direction
  and the haptics need a check on the tablet.
- No automatic "games nearby" list on Android (needs Android's own network
  discovery wired in); join by address works.
- Not tested on a device by us at all: this build was only compiled and checked
  in CI.
- The GameNetworkingSockets option (joining upstream OpenJKDF2 hosts) is not in
  the Android build.
