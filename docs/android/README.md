# Android test build: install and test on a tablet

An early test build of OpenJKDF2 for Android tablets and phones (64-bit ARM,
which is nearly every device from the last several years). It has multiplayer
over ENet (the same as the iOS app), and plays with a keyboard, a mouse or a
game controller. **On-screen touch controls are not in it yet**: a touch only
acts as a mouse click, which is enough for the menus. The plan for them is in
`TOUCH_OVERLAY_PLAN.md`.

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
3. A newer build installs over the old one and keeps your files, as long as it
   was signed with the same key. If Android says the app "conflicts with an
   existing package", the key changed: uninstall first (when asked, choose to
   **keep the app's data**), then install.

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

**Start up**
- [ ] Both icons appear; each starts its own game (Jedi Knight / MotS).
- [ ] The intro and menus show, full screen, the right way round.
- [ ] Sound and music play.
- [ ] Tapping menu items works (touch acts as a mouse click).

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

- No on-screen touch controls yet (planned).
- No automatic "games nearby" list on Android (needs Android's own network
  discovery wired in); join by address works.
- Not tested on a device by us at all: this build was only compiled and checked
  in CI.
- The GameNetworkingSockets option (joining upstream OpenJKDF2 hosts) is not in
  the Android build.
