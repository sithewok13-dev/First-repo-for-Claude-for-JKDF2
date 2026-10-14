# OpenJKDF2 for iOS: setup guide

Play **Star Wars Jedi Knight: Dark Forces II** (1997) on an iPhone or iPad,
with on-screen touch controls, using your own copy of the game. Its expansion,
**Mysteries of the Sith**, has an app of its own.

- **Download:** `OpenJKDF2-iOS-unsigned.ipa` from the
  [latest release](https://github.com/sithewok13-dev/OpenJKDF2-iOS/releases/latest)
- **Mysteries of the Sith:** a separate app, OpenMoTS
  (`OpenJKDF2-MotS-iOS-unsigned.ipa`, new in v0.4). See
  [Mysteries of the Sith](#mysteries-of-the-sith).
- **New in v0.6: lightsabers your way, cheat mode, and Action Freeze.** Pick
  your blade's colour from 17, or fight with two sabers at once (see
  [The lightsaber: SABER](#the-lightsaber-saber)). Switch on cheat mode for a
  `sithewok` button, flying, walking through walls and jumping to any level
  (see [Cheats](#cheats)). Stop the fight mid-blast and swing the camera round
  it for your photo (see [The camera](#the-camera)).
- **Problems?** [Open an issue](https://github.com/sithewok13-dev/OpenJKDF2-iOS/issues)
  (see [Reporting bugs](#reporting-bugs))

Contents: [What this is](#what-this-is) ·
[What you need](#what-you-need) ·
[1. Get your game files ready](#1-get-your-game-files-ready) ·
[2. Install the app](#2-install-the-app) ·
[3. Copy the game files to your iPhone](#3-copy-the-game-files-to-your-iphone) ·
[4. Play](#4-play) ·
[Touch controls](#touch-controls) ·
[The camera](#the-camera) ·
[Tilt aiming](#tilt-aiming) ·
[The Force wheel](#the-force-wheel) ·
[Force Jump](#force-jump) ·
[The weapon wheel](#the-weapon-wheel) ·
[The lightsaber: SABER](#the-lightsaber-saber) ·
[Saving and loading](#saving-and-loading) ·
[Cheats](#cheats) ·
[Settings worth knowing](#settings-worth-knowing) ·
[Troubleshooting](#troubleshooting) ·
[Mysteries of the Sith](#mysteries-of-the-sith) ·
[Known issues and limitations](#known-issues-and-limitations) ·
[Reporting bugs](#reporting-bugs) ·
[Credits and legal](#credits-and-legal)

---

## What this is

This is an **unofficial iOS build of [OpenJKDF2](https://github.com/shinyquagsire23/OpenJKDF2)**,
the open-source re-implementation of the Jedi Knight: Dark Forces II engine by
shinyquagsire23 (Max Thomas) and the OpenJKDF2 contributors. Upstream OpenJKDF2
can already be built for iOS. This build adds the parts you need to actually
play it on a phone:

- on-screen touch controls (move stick, look, buttons, a Force power wheel,
  quick save and quick load), and optional tilt aiming
- a camera for photos and selfies, lightsaber colours and two sabers, and a
  cheat mode
- a HUD that stays clear of rounded screen corners and the home bar
- sound fixes
- a ready-made (unsigned) `.ipa` that GitHub builds automatically
- a second app, OpenMoTS, for the expansion, Mysteries of the Sith

Please keep in mind:

- **It's experimental.** Expect bugs, and keep backups of your saves.
- **No game files are included.** You need your own copy of Jedi Knight from
  GOG or Steam (and of Mysteries of the Sith, for OpenMoTS). Nobody here can
  give you the game files, so please don't ask.
- **It is not affiliated with or endorsed by Lucasfilm, Disney or LucasArts.**
  It's also not an official OpenJKDF2 release. Please report problems with
  this build here, not to the OpenJKDF2 developers (see
  [Reporting bugs](#reporting-bugs)).

## What you need

- **An iPhone or iPad running iOS / iPadOS 18 or newer.** On iPhone that means an
  iPhone XS / XR or newer, including the iPhone SE (2nd generation) and later.
- **Your own copy of Star Wars Jedi Knight: Dark Forces II**, installed on a
  computer, from
  [GOG](https://www.gog.com/game/star_wars_jedi_knight_dark_forces_ii) or
  [Steam](https://store.steampowered.com/app/32380/STAR_WARS_Jedi_Knight_Dark_Forces_II/).
  The GOG version is a good choice: it's DRM-free and comes with the
  soundtrack as music files.
  **No Windows PC?** Both stores sell the game for Windows. On a Mac, GOG's
  offline installer (the `.exe` from your GOG library) can usually be unpacked
  with the free tool *innoextract*, but that hasn't been tested with this game.
- **A way to sideload an app** (install an app that isn't from the App Store).
  The free options need an Apple ID and, at least for setup, a Mac or Windows
  PC. See [2. Install the app](#2-install-the-app).
- **Free space on your phone** for the game files: about as much as the
  `Episode`, `Resource` and `MUSIC` folders take on your computer (check their
  size there).

**Mysteries of the Sith** (the expansion) has an app of its own, OpenMoTS, new
in v0.4: see [Mysteries of the Sith](#mysteries-of-the-sith). In this app
(OpenJKDF2), please ignore *Install Mysteries of the Sith* on the main menu's
*Expansions & Mods* screen, and never copy expansion files into OpenJKDF2.

## 1. Get your game files ready

Find the folder Jedi Knight is installed in on your computer:

- **GOG:** the folder you installed the game to. In GOG Galaxy: select the
  game, open the settings menu next to Play, then *Manage installation* > *Show folder*.
- **Steam:** right-click the game in your Library, then *Manage* > *Browse local files*.

Inside it you need these folders and files. Capital letters don't matter
(`Episode` and `episode` both work), but the folder structure does:

| Folder | What's in it | Needed? |
| --- | --- | --- |
| `episode` | `JK1.gob`, `JK1CTF.gob`, `JK1MP.gob` | **Required** |
| `resource` | `Res1hi.gob`, `Res2.gob`, `jk_.cd` | **Required** |
| `resource/video` | the cutscenes (`.SMK` files) | Recommended: without it you get no cutscenes |
| `MUSIC` | the soundtrack (`Track12.ogg` ... `Track32.ogg` in the GOG version) | Recommended: without it there's no music. Copy it if your copy has it. |

The easiest thing is to copy the whole `Episode`, `Resource` and `MUSIC`
folders. You don't need `JK.EXE` or anything else from the game folder.

## 2. Install the app

### Download the .ipa

Go to the [latest release](https://github.com/sithewok13-dev/OpenJKDF2-iOS/releases/latest)
and download `OpenJKDF2-iOS-unsigned.ipa` from its *Assets*. You can download it
on the phone in Safari (it goes to Files > Downloads) or on your computer,
depending on the tool you use below. (The other file there,
`OpenJKDF2-MotS-iOS-unsigned.ipa`, is the Mysteries of the Sith app: see
[Mysteries of the Sith](#mysteries-of-the-sith).)

There is also an
["iOS latest build"](https://github.com/sithewok13-dev/OpenJKDF2-iOS/releases/tag/ios-latest)
pre-release. It's rebuilt automatically every time the code changes, so it
has the newest fixes, but it may also be broken. Use the latest release unless
you're testing a fix.

### Sign and install it

The `.ipa` is **unsigned**, and an iPhone only runs apps signed with an Apple
certificate, so you sign it yourself with a sideloading tool. Any tool that can
install your own `.ipa` file works. Some common free ones:

- **[AltStore](https://altstore.io)** (AltStore Classic). You install AltServer
  on a Mac or Windows PC and use it to put AltStore on your phone. Then in
  AltStore, go to *My Apps*, tap **+** and pick the `.ipa`. AltStore refreshes
  your apps over Wi-Fi when AltServer is running on your computer.
  (*AltStore PAL*, the EU app marketplace, is a different thing and can't
  install this file.)
- **[SideStore](https://sidestore.io)**. A version of AltStore that, after a
  one-time setup with a computer, signs and refreshes apps on the phone itself.
- **[Sideloadly](https://sideloadly.io)**. A Mac or Windows program: connect
  your phone, drop the `.ipa` in, sign in with your Apple ID, and it installs
  the app.

These tools ask for your **Apple ID and password**, because they sign the app
with it. They're widely used, but they're made by third parties, not Apple;
some people use a separate Apple ID just for sideloading. On Windows, AltServer
and Sideloadly also need Apple's iTunes and iCloud for Windows installed:
follow each tool's own setup instructions.

Other ways: an Apple Developer Program membership (Apple's paid account) lets you sign
apps for a year at a time, and there are third-party signing services. This
guide doesn't recommend any paid service. If you use one, it's your call
whether you trust it.

**With a free Apple ID, apps only stay signed for 7 days.** After that the app
won't open until you refresh it (AltStore and SideStore can do this for you)
or install it again with the same tool. Your game files and saves are kept
when you refresh. A free Apple ID can also only have 3 sideloaded apps
at a time (AltStore or SideStore itself counts as one, and OpenJKDF2 and
OpenMoTS count as one each).

The first time, iOS may ask for two more things:

- **Developer Mode:** go to *Settings > Privacy & Security > Developer Mode*,
  turn it on, restart the phone, and confirm.
- **"Untrusted Developer":** go to *Settings > General > VPN & Device Management*,
  tap your Apple ID under *Developer App*, and tap *Trust*.

**Updating:** to install a newer version, sideload the new `.ipa` with the same tool and
Apple ID. It replaces the app and keeps your game files and saves. If you
switch to a different tool or Apple ID, iOS may install it as a separate
app with an empty folder. If that happens, open the new app once, copy
everything from the old app's `jk1` folder into the new app's `jk1` folder
in the Files app (see below), and only then delete the old app. (If your game
files are somewhere else in the old app's folder, copy them and the `player`
folder next to them instead.)

## 3. Copy the game files to your iPhone

1. **Open OpenJKDF2 once.** It will tell you the game files are missing and
   ask you to copy your installation into the app's "Documents folder", with a
   long path that ends in `Documents/jk1`. That's normal: it has just created
   its folder. In the Files app, the app's Documents folder is
   *On My iPhone > OpenJKDF2*, and your files go **one level deeper, inside
   `jk1`** (step 4). The app can't go any further without the files, so close
   it (swipe up from the bottom of the screen and swipe OpenJKDF2 away).
2. **Get the folders from step 1 onto your phone**, any way you like:
   iCloud Drive (or another cloud drive that shows up in the Files app), a USB
   stick, or a `.zip` file (for example, zip the folders on a Mac and AirDrop
   the `.zip` to your phone).
3. **Open the Files app** and go to *Browse > On My iPhone* (*On My iPad*)
   *> OpenJKDF2 > jk1*.
4. **Copy (or move) `Episode`, `Resource` and `MUSIC` into `jk1`.** For example,
   long-press a folder > *Copy*, then long-press an empty spot in `jk1` > *Paste*.
5. **Check that it looks like this**, with `episode` and `resource` directly
   inside `jk1` and not in another folder:

   ```
   On My iPhone
   └── OpenJKDF2
       └── jk1
           ├── episode
           │   ├── JK1.gob
           │   ├── JK1CTF.gob
           │   └── JK1MP.gob
           ├── resource
           │   ├── Res1hi.gob
           │   ├── Res2.gob
           │   ├── jk_.cd
           │   └── video        (cutscenes)
           └── MUSIC            (soundtrack)
   ```

   The game adds its own `player` folder (your player profile and saves),
   plus empty `mods` and `expansions` folders. Leave those alone.
6. **Open OpenJKDF2 again.**

Things to know:

- **Capital letters don't matter** for anything inside `jk1`. The game finds
  `Episode` or `episode`, and `RES2.GOB` or `Res2.gob`. But use the `jk1` folder
  the app made (all lower case). Don't make your own.
- **Not in `jk1`?** Since v0.4 the app also finds the files when `Episode` and
  `Resource` are straight in the *OpenJKDF2* folder itself, or in a folder of
  their own inside it (for example one made by unpacking a `.zip`). `jk1` is
  still the best place, and the app looks there first. Wherever the files
  are, the game keeps your saves (the `player` folder) and its settings next
  to them, so keep just one copy of the game files, and if you move them
  later, move `player` with them. A folder inside `jk1`, or two folders deep,
  is still too deep.
- **Using a .zip:** tapping a `.zip` in the Files app unpacks it. Unpacking can
  create an extra folder around your files. If that happens inside `jk1`, move
  `Episode`, `Resource` and `MUSIC` out of it so they sit directly inside
  `jk1`.
- **With a cable instead:** on a Mac, open Finder, select your iPhone and go to
  the *Files* tab. On Windows, use the Apple Devices app (or iTunes) and its file sharing.
  Drag a folder named `jk1`, containing `episode`, `resource` and `MUSIC`, onto
  OpenJKDF2. If OpenJKDF2 already has a `jk1` folder, it may ask to replace
  it, and replacing it deletes your saves (`jk1/player`). Back those up first.
  If your computer won't copy folders this way, use the Files app steps above
  instead.
- **Deleting the app deletes its folder too**, `jk1` and your saves included.
  Copy `jk1/player` somewhere safe first if you want to keep them.

## 4. Play

When the game files are in place, the game starts like the PC version:
the intro plays, then the main menu.

- **Menus:** tap buttons and lists the way you'd click them with a mouse.
- **Text boxes** (for example your player name): tap the box to bring up the
  keyboard.
- **Cutscenes:** tap the screen to skip.
- **The objectives screen** at the start of a level: tap *Ok*.
- The game runs in **landscape**, either way round.

The touch controls appear once you're actually playing a level, and they hide again in menus
and cutscenes.

## Touch controls

The layout looks roughly like this. Exact positions shift a little to fit your
screen, the camera cutout and the HUD.

```
 NEXT   FORCE   LIGHT  IR  BACTA  CAMERA  SABER  CHEAT  (FPS)  QUICK  QUICK  MENU
 WPN    WHEEL   (only items you have)                         SAVE   LOAD
                          [GYRO] [FPS] [CAMERA] [CHEAT] [JUMP] [keyboard]
                          (MENU's tray, when held)
                             [MODE] [SENS]             (GYRO held in the tray)
                             [PHOTO] [SELFIES] [FREEZE] (CAMERA held in the tray)

      (^^)  run marker                              JUMP   FORCE
       |                                     ACT
     ( o )  move stick: appears                            ALT
            under your left thumb         DUCK     FIRE
                                                         [ammo gauge]
```

Buttons are slightly see-through until you touch them. SABER shows once you
have the lightsaber, and CHEAT only in cheat mode (see [Cheats](#cheats)).
Where the top row gets crowded, the FPS counter moves to the bottom middle of
the screen. The game's messages
and mission text appear just below the top row of buttons, so the buttons
never cover them, and long lines wrap between words.

### Moving and looking

- **Move:** put your left thumb down anywhere on the left side of the screen
  (a bit less than half of it). The stick appears under your thumb. Push it to walk
  forwards, backwards and sideways. It walks at the same speed however far you
  push it, and it disappears when you lift your thumb.
- **Run:** while the game's *Always Run* option is off (the default), a small
  circle with **^^** in it sits above the stick, joined to it by a line. Push
  your thumb up past the stick's ring onto that circle to **run straight
  ahead**. It lights up blue and you feel a light tap. Slide back down to walk
  again. If your thumb lands near the top of the screen, the circle sits closer
  to the stick, and if there's no room at all, there's no circle that time.
  With *Always Run* turned on (*Setup > Controls > Options*), there's no circle
  and you always run, in every direction.
- **Look / aim:** drag anywhere else on the screen, which mostly means the
  right side. Dragging on FIRE, ALT, DUCK, ACT, JUMP or FORCE also turns
  the view, so if your thumb lands on one while you're aiming, keep dragging.
  You can also aim by turning the phone: see [Tilt aiming](#tilt-aiming)
  (it starts switched off).

### Buttons

| Button | Where | What it does |
| --- | --- | --- |
| **FIRE** | bottom right, the big one | Fires your weapon, for as long as you hold it. |
| **DUCK** | left of FIRE | Crouches while held. |
| **ACT** | up and left of FIRE | Activate: doors, switches, elevators, consoles. |
| **JUMP** | above FIRE | Jumps. |
| **ALT** | up and right of FIRE, just above the ammo gauge | Secondary fire (each weapon's second mode). |
| **FORCE** | right of JUMP, or above it (a little to its left) on smaller screens with the camera cutout on the right. With Display Zoom, where there's no room there, it can sit further left, between DUCK and ACT. It always keeps a little space from the top row, so holding it can't set off QUICK SAVE or QUICK LOAD. | Uses your selected Force power, for as long as you hold it. Tap for a single use, hold for powers that charge up (Force Jump: see [Force Jump](#force-jump)) or keep going (Lightning). The button shows the power's name, and it's dimmed until you have a power. The **blue ring** around it is your Force meter: it shrinks as you use the Force, and glows and pulses when the meter is full. |
| **NEXT WPN** | top left | **Tap:** switches to your next weapon. **Slide** or **hold:** opens the weapon wheel to choose one: see [The weapon wheel](#the-weapon-wheel). |
| **FORCE WHEEL** | top left | **Tap:** selects your next learned Force power (FORCE shows which). **Slide** or **hold:** opens the Force wheel to choose one: see [The Force wheel](#the-force-wheel). |
| **LIGHT**, **IR**, **BACTA** | top left, after FORCE WHEEL | Field light, IR goggles and bacta tank. **Tap** LIGHT or IR to switch it on or off (yellow while on). **Hold BACTA** for about a third of a second (a ring fills) to use a bacta tank: one per press, and a tap or a brush past uses none. Each button only appears once you have that item, and always in the same spot. BACTA shows how many you have when it's more than one. |
| **QUICK SAVE** | top right | **Hold** for about a third of a second: a ring fills, then the game quick saves. See [Saving and loading](#saving-and-loading). |
| **QUICK LOAD** | top right | **Hold** for about a third of a second: a ring fills, then your quick save loads. To cancel, lift or slide off before the ring fills. |
| **MENU** | top right corner | **Tap** opens the game's menu (objectives, map, Jedi powers, save, load, setup and more). **Hold** opens a small tray just under it, see below. |
| **CAMERA** (the camera symbol) | top, in the middle | **Hold still:** a selfie. **Tap:** a photo. **Slide sideways:** a burst. **Slide down:** Action Freeze. **Slide up:** the cinematic burst. See [The camera](#the-camera). |
| **SABER** (a lightsaber) | top, right of CAMERA (once you have the lightsaber) | Opens the saber wheel: blade colours, one saber or two. See [The lightsaber: SABER](#the-lightsaber-saber). |
| **CHEAT** (the red Sith emblem) | top, right of SABER (cheat mode only) | **Hold** for half a second: `sithewok` off or on. Red while it's on. See [Cheats](#cheats). |

FIRE, ALT, DUCK, ACT, JUMP and FORCE act as soon as you touch them.
LIGHT, IR, MENU, CAMERA and the tray buttons act when you **lift** your finger
on them, so if you touch one by mistake, slide off before you lift (on CAMERA,
a slide starts a burst or Action Freeze instead: see [The camera](#the-camera)).
BACTA and CHEAT need a hold, so a brush past does nothing. NEXT WPN and FORCE
WHEEL act on a quick tap when you lift, or open their wheel when you slide or
hold (see [Tap, slide or hold](#tap-slide-or-hold)). SABER opens its wheel
however you touch it. While
`sithewok` is on, JUMP, ACT and DUCK show a small red second line (FLY,
NOCLIP, LVL SEL) for their cheat holds: see [Cheats](#cheats).

### MENU's tray: tilt aiming, FPS, camera, cheat mode and keyboard

**Tap MENU** for the game's menu. **Hold MENU** until its ring fills (about
half a second), and a small tray opens just under it with six buttons, from
left to right **GYRO**, **FPS**, **CAMERA**, **CHEAT**, **JUMP** and the
**keyboard**.
Either keep your thumb down, slide it onto a tray button and lift, or lift
first and then tap one. Anywhere on the tray's dark backing counts as the
nearest button.

- **GYRO:** a **tap** switches tilt aiming on or off. It comes back on in the
  mode you last used (TOUCH the first time), and it's lit yellow while it's
  on. **Touch and hold** GYRO until its ring fills for a row under the tray
  with **MODE** (**TOUCH** or **ALWAYS**) and **SENS** (**1.0x**, **1.5x**,
  the default, **2.0x** or **3.0x**). See [Tilt aiming](#tilt-aiming).
- **FPS:** shows or hides a frame-rate counter, to the left of QUICK SAVE. The FPS
  button is yellow while the counter is on.
- **CAMERA** (the camera symbol): a tap shows or hides the CAMERA button
  (yellow while it's shown). Touch and hold it until its ring fills for
  CAMERA's options, **PHOTO**, **SELFIES** and **FREEZE**: see
  [The camera](#the-camera).
- **CHEAT:** cheat mode on or off. On, the CHEAT button appears and
  `sithewok` goes on. Off, `sithewok` goes off and the button goes away.
  Yellow while cheat mode is on. See [Cheats](#cheats).
- **JUMP:** how Force Jump works, **SMOOTH** (the default), **STEPS** or
  **CLASSIC**. Each tap goes on to the next, and the tray stays open. See
  [Force Jump](#force-jump).
- **Keyboard** (the keyboard symbol, at the right end of the tray): opens the
  game's typing line with the iPhone keyboard, for [cheats](#cheats). Tap it
  again to close the line. While it's open, MENU and the keyboard button are
  lit yellow.

GYRO and JUMP show their setting on the button (for example *GYRO OFF*,
*JUMP SMOOTH*), and MODE and SENS show theirs. Each tap on JUMP, MODE or SENS
goes on to the next setting, and the tray (or row) stays open so you can tap
again.

The app remembers GYRO, SENS, FPS, CAMERA and its options, CHEAT and JUMP
next time you open it. The `sithewok` cheat itself always starts off when you open
the app: hold the CHEAT button to switch it on.

The tray usually sits right under MENU. On smaller screens (with the camera
cutout on the right, with Display Zoom, or at a large HUD Scale) it sits
further left, so that it never covers FORCE or the buttons around FIRE. The
tray closes after you pick FPS, CAMERA, CHEAT or the keyboard, or when you
touch anywhere else. That touch still counts: touching FIRE closes the tray
and fires. Touching MENU again just closes it. If you slide off MENU while
holding it, nothing happens.

## The camera

The camera button (**CAMERA** in this guide, the camera symbol in the middle
of the top row) takes selfies with everyone nearby, frozen mid-fight and
posing for you, ordinary photos and bursts of your game, and since v0.6 it
can stop time with **Action Freeze**. Everything goes straight into your
Photos app.

**It's there from the start.** To hide it (or show it again), hold **MENU**
and tap the **camera symbol** in the tray; the app remembers. The first time
you use it, iOS asks whether the app may **add photos** to your library: say
yes. The game holds still while iOS asks. The app can only add photos; it
can't see the ones already in your library.

What CAMERA does depends on how you touch it:

| Gesture | What it does |
| --- | --- |
| **Hold still** (half a second, a ring fills) | A selfie (1, 3 or 5 photos). |
| **Tap** | A photo of the game. |
| **Slide off sideways** | A burst: a photo every fifth of a second while your finger stays down. |
| **Slide down** (a long slide) | [Action Freeze](#action-freeze): stop the game and move the camera round it. |
| **Slide up** (towards the top edge) | [The cinematic burst](#the-cinematic-burst): five photos as the camera sweeps round the action. |

With **FREEZE** switched off (in [CAMERA's options](#cameras-options)), a
slide in any direction is a burst.

### Selfies

**Hold CAMERA still** for half a second (a ring fills around it).

- The whole game freezes, enemies and bosses included, and the HUD and touch
  buttons hide.
- The view switches to the phone, held out at arm's length and facing you.
  Up to **8 people near you**, in sight, turn to it and strike a **random
  pose**: wave, fist up, hand on hip, point, flex, salute, hands behind the
  head, victory sign, shrug, hand on the chin or the heart, arms crossed, an
  arm round a neighbour, and more. Some of the front row kneel on one knee or
  crouch, some turn three-quarters to the camera or lean, and now and then a
  whole group strikes the same pose. Droids and creatures just turn to the
  camera.
- **You** (Kyle, or Mara Jade in her levels of Mysteries of the Sith) hold
  the phone out in your right or left hand, strike a pose with the other, and
  look toward the lens. Sometimes you show off your gun or lightsaber
  instead. The camera angle changes from shot to shot: a little higher or
  lower, sometimes slightly tilted.
- **Now and then something unexpected happens:** someone points at
  something, someone hasn't noticed the photo, someone stares, or a
  passer-by photobombs.
- **1, 3 or 5 photos:** a selfie takes **3** unless you change it with
  **SELFIES** in [CAMERA's options](#cameras-options). Everyone poses afresh
  for each one, and the game stays frozen the whole time.
- **The flash** lights everyone up, and shots or blasts flying in front of
  someone's face are left out of the photo.
- After the flash and the shutter, the photo stays on screen for a moment.
  Then everyone goes back to exactly what they were doing and the game carries
  on. A selfie changes nothing in your game.
- **Nobody nearby?** It's a selfie of you alone. If people are spread out too
  far to fit in one photo, the ones furthest away are left out.
- Selfies are always clean photos (*... selfie.png*), whatever PHOTO is set
  to.
- **When it can't:** with no room to hold the camera out (right up against a
  wall), during a cutscene, through a scope or another camera, or when you're
  dead, it says *Can't take a selfie here*. If the last photos are still
  being saved, it says *Still saving the last photo*: try again in a moment.
  While the typing line is open, holding CAMERA does nothing (a tap still
  takes a photo).

### Photos and bursts

- **Tap CAMERA:** a white flash, the shutter sound, and the photo. A line at
  the top of the screen says when it's saved (*1 photo saved*).
- **Burst:** touch CAMERA and slide your finger off it sideways (a short
  slide is enough). The app takes a photo every fifth of a second while your
  finger stays down, up to 10, with a quick tick for each and a counter
  beside the button. Lift to stop. Burst photos are always clean ones.

### Action Freeze

New in v0.6. Touch CAMERA and slide your finger **down**, further than for a
burst.

- The game stops exactly where it is: blaster bolts, explosions, flying
  debris and everyone mid-move. The buttons hide, apart from CAMERA, and an
  **X** appears at the top right.
- **One finger** turns the camera round what it's looking at. **Two
  fingers** move it, and **pinch** to zoom in or out.
- The camera stays out of walls, closed doors and lifts. If it can't go
  somewhere, it stops. Close to a wall it moves in a little, and glides back
  out when there's room.
- **Tap CAMERA** to take a clean photo, as many as you like. **Tap the X** to
  go back to the game, which carries on from exactly the same moment.
- If it can't freeze (a cutscene, a scope, while dead), it says *Can't freeze
  here*.

### The cinematic burst

New in v0.6. Touch CAMERA and slide your finger **up**. Reaching near the top
edge of the screen is enough.

- The game freezes for a moment and the camera takes **five photos** as it
  sweeps round the action, then the game carries on by itself.
- The first photo is exactly what you saw. The other four go round the most
  interesting thing in front of you: someone you're fighting, a shot in
  flight or an explosion. In a duel or a boss fight, you and your opponent
  are both in the shots, seen from the side. With nobody around, it goes
  round you.
- The sweep picks the side with the most room, so the camera doesn't end up
  inside a wall or behind someone. In a cramped spot it sweeps less, or just
  zooms in.

### CAMERA's options

Open the tray (hold MENU, then lift), then touch and hold the **camera symbol
in the tray** until its ring fills. A row opens under the tray with three
buttons:

- **PHOTO:** what a tap takes. Tap PHOTO for its three tick boxes: **CLEAN**
  (just the game: ticked unless you change it), **HUD** (with the health and
  ammo gauges) and **HUD+BTNS** (with the touch buttons too, as you see the
  screen). Tap each to tick or untick it; at least one stays ticked. With
  more than one ticked, each tap of CAMERA takes one photo of each, of the
  same moment.
- **SELFIES:** how many photos a selfie takes: **1**, **3** (the default) or
  **5**. Each tap goes on to the next.
- **FREEZE:** **ON** (the default) or **OFF**. Off, sliding CAMERA down or up
  is just a burst, as before v0.6.
- Touch anywhere else to close the row and the tray.

### Where the photos go

- Photos are full-screen PNGs named after the app and the time, for example
  *OpenJKDF2 2026-10-12 14.05.09 clean.png* (a burst's are numbered:
  *... burst 01.png*; *OpenMoTS ...* in Mysteries of the Sith).
- **If Photos access is off,** they go to the Files app instead, in
  *OpenJKDF2 > Screenshots* (or *OpenMoTS > Screenshots*), and the message
  says so. To use the Photos app again: *Settings > Privacy & Security >
  Photos*, then the app, then *Add Photos Only*.

## Tilt aiming

You can also aim by turning and tilting the iPhone, on top of dragging. **Tilt
aiming starts switched off.** Hold **MENU** and tap **GYRO** in its tray to
switch it on or off. It comes back on in the mode you last used (TOUCH the
first time). To choose between TOUCH and ALWAYS, or change the sensitivity,
hold GYRO in the tray until its ring fills, then tap **MODE** or **SENS**:

- **GYRO OFF** (the default): no tilt aiming. The app doesn't read the
  motion sensor at all.
- **GYRO TOUCH:** turning the phone aims while either thumb is touching the
  game: your left thumb on the move stick (even resting there without moving),
  or your right thumb on the look area (resting or dragging) or on FIRE, ALT,
  DUCK, ACT, JUMP or FORCE. So while your left thumb is on the stick, moving
  your right thumb from the look area to FIRE doesn't interrupt aiming, and
  while you walk, turning the phone always aims. To move the phone back to a
  comfortable position, lift both thumbs: the view stays exactly where it is,
  and when you touch again it carries on from there, like lifting a mouse off
  the desk. The buttons along the top (NEXT WPN, FORCE WHEEL, QUICK SAVE, MENU
  and the rest) don't count.
- **GYRO ALWAYS:** turning the phone always aims, with or without a thumb down.

**SENS** sets how far the view turns: 1.0x, 1.5x (the default), 2.0x or 3.0x.
At 1.0x the view turns exactly as far as the phone does. GYRO is lit yellow
while tilt aiming is on.

Good to know:

- **The view never resets or springs back.** Tilt aiming only adds the way
  the phone turns. There's no "straight ahead" position it returns to, so
  however you hold the phone when you start, that's where you start from.
- Turning left and right works however far back you tip the phone, even lying
  flat on a table. It also works lying down with the screen facing you from
  above, or on your side. Tilting the top of the phone toward you looks up.
- Tilt aiming pauses while a wheel (Force, weapon or saber), the cheat typing
  line or MENU's tray is open, and for a moment after you turn the phone round
  to the other landscape side.
- The game's mouse sensitivity (*Setup > Controls > Mouse*) doesn't change it.
  Use SENS instead (hold GYRO in the tray).
- If the device has no motion sensor, GYRO says **NO GYRO** and does nothing.

## Force Jump

Force Jump jumps when you **lift** your finger off FORCE, and how long you
held it decides how high. It has 4 levels (the stars on the Force wheel):
each star raises the highest jump, and gives you a little longer to reach it
(0.35 s a star, so 1.4 s at 4 stars). Choose how holding works with **JUMP**
in MENU's tray:

- **SMOOTH** (the default): a **tap** is the lowest Force Jump (a bit under
  half the full height). The longer you hold, the higher it goes, smoothly,
  up to the highest for your stars. A **gold ring** fills inside FORCE as you
  hold, with a light tick when it's full: lift whenever the ring shows the
  height you want.
- **STEPS:** the same heights in clear steps, one a star. A tap is the lowest
  step, and every 0.35 s you hold goes up a step, with a tick and a quarter
  more of the gold ring (at 4 stars), to the highest.
- **CLASSIC:** exactly as the original game: a quick tap is the **highest**
  jump, and so is a hold past the highest; only a hold in between is lower.
  No ring.

In every style the jump costs the same Force and needs you on the ground (or
on water), and the other Force powers work as before.

## The Force wheel

**FORCE WHEEL** opens the Force wheel (see [Tap, slide or hold](#tap-slide-or-hold)
for the three ways to use the button). **The game pauses while the wheel is
open.** Every Force power always has the same slot, whether you've learned it
yet or not (powers you haven't learned are dark):

- **Left side, blue: light side.** From the top: Healing, Persuasion,
  Blinding, Absorb, Protection.
- **Across the top, gold: neutral.** From the left: Jump, Speed, Seeing, Pull.
- **Right side, red: dark side.** From the top: Throw, Grip, Lightning,
  Destruction, Deadly Sight.
- **The gap at the bottom** means *cancel*.

**Stars:** under the name of each power you've learned, a row of four stars
shows its level, one gold star per level (a power at level 2 shows two gold
stars and two empty ones).

Once you've picked a power, use it with **FORCE**.

### Tap, slide or hold

FORCE WHEEL and NEXT WPN work the same way. Each has three gestures:

- **Quick tap** (lift within about a third of a second, without sliding):
  FORCE WHEEL selects your **next learned Force power**, skipping the ones you
  haven't learned (the FORCE button shows which one you have now). NEXT WPN
  switches to your **next weapon**. No wheel opens.
- **Touch and slide (quickest):** as soon as your thumb slides away from the
  button, the wheel opens. Keep sliding towards a slot. A short slide in the
  right direction is enough, but you can also slide all the way onto it. The
  slot pops out, its name shows in the middle, and you feel a light tick.
  **Lift to select it.** To change nothing, lift in the gap at the bottom, in
  the middle of the wheel, or back where you started. A slide too short to
  point at anything leaves the wheel open for tapping.
- **Touch and hold still** for about a third of a second: a ring fills round
  the button, and the wheel opens and **stays open**. Lift, then tap a slot to
  select it. (Or, without lifting, slide your thumb out from the button to
  pick by sliding after all.)

With the wheel open for tapping: tap a slot to select it. Tapping a power you
haven't learned, or a weapon you can't switch to, does nothing. Tapping
anywhere else (the middle, the gap, outside the wheel, or either button at the
top left) closes the wheel without changing anything.

## The weapon wheel

**NEXT WPN** opens the weapon wheel when you slide or hold it (a quick tap
switches to the next weapon, as before). It works like the Force wheel (see
[Tap, slide or hold](#tap-slide-or-hold)), and **the game pauses while it is
open** too.

Every weapon always has the same slot, whether you've found it yet or not, in
the order of the number keys on PC, clockwise from the bottom left: Fists,
Bryar Pistol, Stormtrooper Rifle, Thermal Detonator, Bowcaster, Repeater,
Rail Detonator, Sequencer Charge, Concussion Rifle, Lightsaber. The gap at the
bottom means *cancel*. The colours show the kind of ammo: gold for the fists
and the lightsaber (no ammo), blue for energy cells, green for power cells and
red for explosives.

- **Under each weapon you have, its ammo count** shows: the same number the
  HUD shows with that weapon in hand (energy cells for the Bryar and the
  rifle; power cells for the bowcaster, repeater and concussion rifle; rail
  charges for the rail detonator; how many thermal detonators and sequencer
  charges you're carrying). Weapons that share ammo show the same number. The
  fists and the lightsaber show no count.
- **Weapons you haven't found yet are grey**, with no count.
- **A weapon you have but can't fire is dark in its own colour**, with its
  count (usually **0**), and can't be selected. The bowcaster needs more than
  1 power cell and the concussion rifle more than 7, as in the game, so they
  can be dark at 1 or 7. Thermal detonators and sequencer charges are their own ammo: when
  you have none left they show as not found.
- The weapon in hand has a white edge.

If you pick a weapon while the game is still switching weapons (just after a
switch), it switches as soon as the game is ready, within a second or so. That
includes the weapon you're putting away: pick it to switch straight back. A
quick load drops a pick that is still waiting.

## The lightsaber: SABER

New in v0.6. Once you have the lightsaber (or while `sithewok` is on), a
**SABER** button appears on the top row, right of CAMERA. It shows your blade
(or two crossed blades) in your colours, and opens the **saber wheel**, the
same way as the other wheels: tap or hold it to open the wheel and then tap a
colour, or slide off it towards a colour and lift. The gap at the bottom
cancels, and the game holds still while the wheel is open.

- **17 colours:** ORIGINAL (the game's own, unchanged), red, crimson, orange,
  amber, yellow, lime, green, teal, cyan, light blue, blue, purple, magenta,
  pink, white, and black with a bright edge.
- **The wheel stays open** after you pick a colour, so you can change both
  blades in one go. Close it with the gap at the bottom, or by tapping outside
  the wheel.
- **EQUIP**, in the middle of the wheel, closes it and puts the lightsaber in
  your hand. It's dim while you don't have the saber.
- **SINGLE or DUAL**, beside the wheel: one saber, or one in each hand. With
  DUAL, **LEFT**, **BOTH** and **RIGHT** choose which blade a colour goes to.
  Each style keeps its own colours.
- **Two sabers fight for real:** the second blade strikes, deals damage and
  throws sparks like the first. In first person both arms move, and between
  swings the two sabers settle into a few natural resting stances, with a
  faint shimmer on each blade. Kyle has no new animations, so in his
  two-handed swings the blades come from nearly the same place.
- **Only the look changes,** and only in your own single-player game: the
  saber's damage, your saves and the levels are untouched. The colours show
  in first and third person, in selfies and in Action Freeze. The app
  remembers your colours and style.
- A lightsaber that only `sithewok` gave you goes away again when the cheat
  goes off (see [Cheats](#cheats)). One the game gave you stays.

## Saving and loading

- **QUICK SAVE** saves to the game's quick-save slot, the same one F9 uses on PC.
  Hold it until the ring fills (about a third of a second). It saves once, while your
  finger is still down. If you lift sooner, or slide off the button, it doesn't
  save, even if you slide back on. It doesn't work while the cheat typing line
  is open.
- **QUICK LOAD** loads that quick save. Hold it until the ring fills (about a
  third of a second), so a stray tap can't throw away your progress. It loads
  once, while your finger is still down. If you lift sooner, or slide off the
  button, it doesn't load, even if you slide back on. It doesn't work while the
  cheat typing line is open. If the quick save is from another level, that
  level is loaded. If you haven't quick saved yet, it says *No quicksave yet*.
- **Holding with more than one finger** still saves or loads only once. To do
  it again, touch and hold the button again. If you press QUICK SAVE and QUICK
  LOAD together, only the one whose ring fills first goes off (if both fill in
  the same frame, it saves).
- **Normal saves:** tap MENU and use the game's own save and load screens, as
  on PC.
- Your player profile and all saves are stored in *Files > On My iPhone >
  OpenJKDF2 > jk1 > player* (or in the `player` folder next to your game
  files, if they're somewhere else in the OpenJKDF2 folder). Copy that folder
  somewhere safe now and then.

## Cheats

Cheats go into the game's typing line (on PC, that's where chat goes):

1. During a level, **hold MENU** until its ring fills, then tap the
   **keyboard** button at the right end of the tray that appears under it.
2. A typing line opens at the top of the screen, with the iPhone keyboard.
   Type the cheat (autocorrect is off here, so it goes in exactly as typed),
   then tap **return**.
3. To close the line without sending anything, tap **MENU** (or the keyboard
   button in the tray again).

While the typing line is open, most other touch buttons don't work. The
exceptions: **MENU** (and the tray's keyboard button) close the line, and
**FORCE WHEEL** and **NEXT WPN** close it and open their wheel when you
slide or hold them (a quick tap does nothing while you type). Cheats only work in single
player. Some cheats need `on` or `off` after them.

| Type this | What it does |
| --- | --- |
| `sithewok` | Everything at once, plus flying, noclip and picking a level; type it again to switch it off: see [sithewok](#sithewok) below. This one was added by this iOS build. |
| `jediwannabe on` / `jediwannabe off` | Invincibility on / off |
| `eriamjh` | Fly (type it again to stop) |
| `noclip` | Fly through walls (type it again to stop). This one was added by OpenJKDF2. |
| `whiteflag on` / `whiteflag off` | Turns enemy AI off / on |
| `slowmo on` / `slowmo off` | Slow motion on / off |
| `red5` | All weapons and ammo |
| `wamprat` | All items: bacta tanks, IR goggles, field light, keys and so on |
| `bactame` | Full health and shields |
| `yodajammies` | Full Force meter |
| `5858lvr` | Shows the whole level on the map (MENU > Map). Type it again to undo. |
| `imayoda` | All light side and neutral Force powers |
| `sithlord` | All dark side and neutral Force powers |
| `raccoonking` | All Force powers |
| `deeznuts` | Force level-up: sets all your Force powers (and your Jedi rank to match) to 1 star, then 2, 3 and 4 stars on each later use, then back to 1. It starts at 1 star even if you already had more, so late in the game it can make you weaker. |
| `thereisnotry` | Finishes the current level |

`imayoda`, `sithlord` and `raccoonking` also set your Jedi rank to 2 and the
powers they give to 1 star, and `imayoda` takes away any dark side powers you
have (`sithlord`: any light side powers). Late in the game they can make you
weaker, so save first.

### sithewok

`sithewok` is this iOS build's own cheat (the original game doesn't have it).
It gives you everything at once: all weapons and ammo, all items, every Force
power at 4 stars with the top Jedi rank, full health, shields and Force meter,
and invincibility. While it's on:

- **The Force meter never drains**, however much you use your powers.
- **Ammo never runs out:** every kind is filled to its most when it goes on,
  and refills as you fire, the battery for the field light and IR goggles
  too.
- **Falls never hurt you**, however far you drop.
- **The Force catches you** if you fall into a bottomless pit: a soft blue
  flash, and you're back on the ledge you fell from.
- **It stays on** through new levels and loaded games, and puts everything
  back at the start of each level. Your weapon in hand and your selected
  Force power come with you to the next level.
- **JUMP, ACT and DUCK get new holds.** While the cheat is on, each shows its
  hold in small red letters under the button (FLY, NOCLIP, LVL SEL):
  - **Hold JUMP** still for half a second (a ring fills): **fly**. You float,
    forward goes where you look, hold JUMP to rise and DUCK to sink. Hold JUMP
    again for half a second, or **double-tap JUMP**, to land. Walls still stop
    you.
  - **Hold ACT** still for half a second: **noclip**, flying straight through
    walls. Hold it again to switch it off.
  - **Hold DUCK** still: **pick a level**. After half a second the button
    shows **1**, then counts up one every 0.4 seconds (with a tick each, and
    *Level 7 of 21* on the message line), going round to 1 again after the
    last level. Lift your finger to go to that level. Slide off to cancel.
  - A quick press of JUMP, ACT or DUCK does what it always does.

**Typing `sithewok` again switches it off** (so does the CHEAT button,
below): no more invincibility, endless Force, safe falls, flying or noclip.
You keep the weapons, items and Force powers it gave you, except a lightsaber
you only had from the cheat. Type `sithewok` once more to switch it back on
and fill everything back up. It also goes off when you close the app (swipe
it away).

### Cheat mode: the CHEAT button

Hold **MENU** and tap **CHEAT** in the tray. A **CHEAT** button with the **red
Sith emblem** appears on the top row, `sithewok` goes on, and the Emperor
laughs. Then:

- **Hold CHEAT** for half a second (a ring fills) to switch `sithewok` off
  (the emblem goes grey) or on again (red). A tap does nothing, so you can't
  switch it by accident.
- **Typing works too:** typing `sithewok` while it's off switches it on and
  brings the CHEAT button back (with the laugh). Typing `sithewok` while it's
  on switches it off and the button goes away. Holding the button instead
  leaves it on screen, grey.
- **To leave cheat mode,** tap CHEAT in the tray again: `sithewok` goes off
  and the button goes away.
- The app remembers cheat mode, but `sithewok` itself starts off each time
  you open the app: hold CHEAT to switch it on.
- The laugh plays at the game's sound effects volume, so turn that down if
  you'd rather not hear it.

### sithewok's catches in Jedi Knight

(Mysteries of the Sith's are in [its section](#mysteries-of-the-sith).)

- **It can change the story.** With every light and dark power at 4 stars,
  the two sides cancel out, so when the game picks your path (after level 14)
  you get the light side unless you've killed more than about one in five of
  the civilians. Switching it off doesn't undo this, because you keep the
  stars. If you care which side you end up on, keep a save from before you
  first use `sithewok`, or wait to use it until your path has been set after
  level 14.
- On the Force screen after level 14, where your path is set, the game takes
  the other side's stars away one at a time, which takes about 16 seconds if
  you've used `sithewok`. Wait until it stops before you tap OK, or your path
  isn't fully set until a later Force screen.
- **Picking a level** with DUCK jumps straight there, skipping the story in
  between, and goes to the level itself rather than the cutscene before it.
  Where a level number has a light and a dark side version, you get the first
  one. Keep a normal save of your real progress.
- Pits that a level's own script uses to end you (rather than an ordinary
  fall) can still kill you.

## Settings worth knowing

All of these are in the game's own *Setup* screens (tap MENU, then Setup, or
use Setup from the main menu):

- **Always Run** (*Setup > Controls > Options*): off by default, which is why
  the stick has its run circle. Turn it on to run all the time, in every
  direction.
- **HUD Scale** (*Setup > Display*): how big the health and ammo gauges are.
  The iOS default is 2.5. Tap the box to type a new value.
- **Performance** (*Setup > Display*): if the game feels slow, try setting
  *SSAA Multiplier* below 1 (for example `0.75` or `0.5`), which draws the game
  at a lower resolution. *Enable SSAO* and *Enable Bloom* are off by default;
  if you turned them on, turn them off again. Turn on the FPS counter (MENU
  tray) to see the effect. (*Enable HiDPI* makes no difference on iOS.)
- **Look speed** (*Setup > Controls > Mouse*): there's no separate touch
  setting, but dragging to look goes into the game as mouse movement, so the
  *Sensitivity* slider for the mouse's turn and look entries should change how
  fast the view turns. This hasn't been tested on a device yet. Changing the
  sensitivity doesn't change any bindings, so it's safe (see the next point).
  Tilt aiming has its own setting, SENS (hold GYRO in MENU's tray, see
  [Tilt aiming](#tilt-aiming)).
- **Keyboard bindings** (*Setup > Controls*): **leave these at their defaults.**
  The touch buttons press the game's default keys, so if you rebind an action,
  its touch button stops working.

## Troubleshooting

- **The app closes as soon as it opens:** its signature has probably expired
  (7 days with a free Apple ID). Refresh it in AltStore or SideStore, or install
  it again. Also check Developer Mode and "Trust" (see
  [Sign and install it](#sign-and-install-it)).
- **There's no OpenJKDF2 folder in Files:** open the app once first, so it
  creates the folder.
- **"OpenJKDF2 is missing the following required assets":** the message lists
  which files it couldn't find and, after *Root dir*, the folder it looked in.
  Check the files are in `jk1/episode` and `jk1/resource`, and not one folder
  deeper (inside `jk1`). The app also looks straight in the OpenJKDF2 folder,
  and in the folders directly inside it, for a `resource` folder with
  `Res2.gob` in it. Make sure the files aren't still zipped.
- **I copied the folders into OpenJKDF2, not into jk1:** since v0.4 that works
  too, as long as `Episode` and `Resource` are straight in the OpenJKDF2
  folder. Your saves then go in a `player` folder next to them. `jk1` is still
  the recommended place: if you'd rather have them there, move `Episode`,
  `Resource` and `MUSIC` (and `player`, if you've played already) into `jk1`.
- **I tapped *Install Mysteries of the Sith*:** the app now asks for the
  expansion's files. Close the app and open it again; it should start Jedi
  Knight again. You may see an empty `mots` folder next to `jk1`, which you can
  leave or delete.
- **No music:** copy the `MUSIC` folder next to `Episode` and `Resource` (into
  `jk1`, see step 1). Not every copy of the game includes it.
- **No cutscenes:** copy `Resource/video` too.
- **I can't see the touch buttons:** they only show while you're playing a level.
  In menus, tap the menu items directly.
- **The view turns when I move the phone:** tilt aiming is on. Hold MENU and
  tap GYRO to switch it off (it says GYRO OFF). Or hold GYRO in the tray and
  set MODE to TOUCH, so it only aims while a thumb is on the move stick, on
  the look area or on the buttons around FIRE (see [Tilt aiming](#tilt-aiming)).
- **Tilt aiming stops for a moment when I move my right thumb to FIRE (GYRO
  TOUCH):** keep your left thumb on the move stick. Tilt aiming then carries on
  while your right thumb is in the air. It only stops when both thumbs are up.
- **A touch button does nothing:** if you changed the key bindings in
  *Setup > Controls*, change them back to the defaults. Also, the typing line
  might still be open: tap MENU to close it.

## Mysteries of the Sith

**Star Wars Jedi Knight: Mysteries of the Sith** (1998), the expansion, has its
own app: **OpenMoTS**, new in v0.4. It's the same game engine and the same
touch controls, in a second app with its own icon and its own folder in the
Files app, so it installs next to OpenJKDF2 and keeps its files, saves and
settings (SENS, GYRO, FPS and CAMERA too) apart from Jedi Knight's. It only
needs Mysteries of the Sith's own files. The camera, SABER and its saber
wheel (for Mara's saber), and cheat mode with the CHEAT button all work
there too.

**How well it's tested:** OpenMoTS has had much less testing than OpenJKDF2.
It has been tested on a computer, with the real game engine, the Mysteries of
the Sith files and simulated touches: the menus, the first level, the HUD,
the main buttons, both wheels, the cheats and `sithewok`, quick save and
quick load, the item buttons, finishing a level and starting the next, and
Mara Jade's first level. Tilt aiming and the sound haven't been checked in
OpenMoTS yet. On an iPhone, it has only been checked to start and run with
the files in its folder so far. It hasn't been played through on one, and it
hasn't been tried on an iPad.

**What you need:** your own copy of Mysteries of the Sith (GOG and Steam sell
it), installed on a computer (find its folder the same way as for Jedi Knight,
in [1. Get your game files ready](#1-get-your-game-files-ready)), and room on
your phone for about 450 MB (under 200 MB without the cutscenes and music).

1. **Download `OpenJKDF2-MotS-iOS-unsigned.ipa`** from the
   [latest release](https://github.com/sithewok13-dev/OpenJKDF2-iOS/releases/latest).
   Sign and install it the same way, with the same tool, as OpenJKDF2
   ([2. Install the app](#2-install-the-app)). With a free Apple ID, it counts
   as one more of your 3 sideloaded apps. If your tool says you've reached the
   limit, **don't delete OpenJKDF2** to make room: that deletes its game files
   and saves too. Deactivate or remove a different app instead.
2. **Open OpenMoTS once.** It tells you the game files are missing, with a
   long path that ends in `Documents/mots`. That's normal: it has just created
   its folder. Close it.
3. **In the Files app, go to *On My iPhone (or On My iPad) > OpenMoTS > mots***
   and copy the `Episode`, `Resource` and `MUSIC` folders from your Mysteries
   of the Sith folder into it, the same way as for Jedi Knight
   ([3. Copy the game files to your iPhone](#3-copy-the-game-files-to-your-iphone)).
   It should look like this:

   ```
   On My iPhone
   └── OpenMoTS
       └── mots
           ├── Episode
           │   ├── JKM.GOO
           │   ├── JKM_KFY.GOO
           │   ├── JKM_MP.GOO
           │   └── JKM_SABER.GOO
           ├── Resource
           │   ├── JKMRES.GOO
           │   ├── JKMsndLO.goo
           │   ├── JK_.CD
           │   └── VIDEO        (cutscenes)
           └── MUSIC            (soundtrack)
   ```

4. **Open OpenMoTS again.** The intro plays, then you make a player and start
   *Single Player > New Game*.

Things to know:

- **Not in `mots`?** Like OpenJKDF2, OpenMoTS also finds the files when
  `Episode` and `Resource` are straight in the *OpenMoTS* folder, or in a
  folder of their own inside it (see
  [3. Copy the game files to your iPhone](#3-copy-the-game-files-to-your-iphone)).
  `mots` is still the recommended place. Your saves go in a `player` folder
  next to the files, wherever they are.
- **Keep the two games apart.** Mysteries of the Sith files go in OpenMoTS's
  `mots` folder only, never in OpenJKDF2's `jk1`, and Jedi Knight files don't
  go in `mots`.
- **Deleting OpenMoTS deletes its folder too**, `mots` and your saves
  (`mots/player`) included.
- **Updating:** install a newer `OpenJKDF2-MotS-iOS-unsigned.ipa` with the same
  tool and Apple ID, as for OpenJKDF2. It replaces the app and keeps your
  files and saves. That includes going from the OpenMoTS test build on the
  "iOS latest build" pre-release to a release.
- *Expansions & Mods* in OpenMoTS has no entry for Jedi Knight: open the
  OpenJKDF2 app instead.
- The **Force wheel** shows the 17 Force powers you can use in Mysteries of
  the Sith, in its four tiers (the four columns of its Force screen),
  clockwise from the bottom left: tier I (gold) up the left side, tier II
  (green) at the top left, tier III (blue) at the top right, and tier IV
  (purple) down the right. Defense is for multiplayer only, so it isn't on
  the wheel.
- The **weapon wheel** shows its 17 weapons, in the order NEXT WPN goes
  through them. The E-Web and the Carbo Gun are purple.
- **Cheats** go in the typing line as in Jedi Knight ([Cheats](#cheats)).
  Mysteries of the Sith's own cheats work: `diediedie` (weapons),
  `gimmestuff` (items), `morelife` (health and shields), `trixie` (Force
  meter), `cartograph` (map), `iamagod` (every Force power), `trainme` (Force
  level-up), `boinga on` / `boinga off` (invincibility), `freebird` (fly) and
  `gameover` (finishes the level). The Jedi Knight names in the table work
  here too, except `imayoda` and `sithlord`, which do nothing in Mysteries of
  the Sith.

`sithewok` works in Mysteries of the Sith too, with everything it does in
Jedi Knight (see [Cheats](#cheats)): all weapons and ammo, all items, every
Force power at 4 stars with the top Jedi rank, full health and shields,
invincibility, the endless Force, safe falls, the Force catching you in pits,
flying, noclip, and picking a level with DUCK (13 levels). Cheat mode and the
CHEAT button work the same way. The catches in Mysteries of the Sith:

- Your Force powers stay through the Force screen at the end of each level.
  Normally Mysteries of the Sith takes back powers beyond what your rank
  allows. This lasts until the app is closed, in any game you play (also after
  you switch `sithewok` off). If you load a game saved with these powers after reopening
  the app, switch `sithewok` on again before the level ends, or the Force
  screen turns them back into stars.
- While you have them, the Force screen's *Choose* numbers and left-over stars
  don't matter: just tap *Ok*.
- When the story switches to Mara Jade (level 5), the game gives her her own
  starting powers, rank and weapons, and `sithewok` puts everything back once
  her level has started. If anything's missing, hold CHEAT to switch it off
  and on again.
- After the opening video of a level, your weapon in hand comes back a moment
  later than in Jedi Knight.

If something doesn't work in OpenMoTS, please say so in your bug report
([Reporting bugs](#reporting-bugs)).

## Known issues and limitations

- **Experimental build.** It has only had limited testing so far. It may
  crash, and the touch layout may not suit every iPhone or iPad screen.
- **Mysteries of the Sith (OpenMoTS) is new, and has had much less testing.**
  On an iPhone, it has only been checked to start and run so far (see
  [Mysteries of the Sith](#mysteries-of-the-sith)). In OpenJKDF2, please
  ignore *Install Mysteries of the Sith* under *Expansions & Mods*.
- **What's new in v0.6 has been tested mostly on a computer**, with the real
  game engine and simulated touches: the saber colours and two sabers, cheat
  mode and its holds (fly, noclip, picking a level), Action Freeze and the
  cinematic burst, and the new selfies. Some of it has been tried on an
  iPhone, but not all of it, and not on an iPad. If something looks or works
  wrong, please report it (see [Reporting bugs](#reporting-bugs)).
- **Two sabers use Kyle's one-saber moves:** there are no new animations, so
  in his two-handed swings the blades come from nearly the same place, and
  the resting stances are in first person only.
- **Action Freeze close to walls:** the camera keeps out of walls and closed
  doors, but very close to one the view behind it can come out black.
- **The selfie camera** (new in v0.5) has been tried on an iPhone (photos and
  lots of selfies in Jedi Knight) and tested on a computer with the real game
  engine, each selfie checked to put the level back
  exactly as it was. Selfies in Mysteries of the Sith have only been tested on
  a computer. With low-detail 1990s models, some poses can look a little odd
  up close. If a selfie looks strange, or the game doesn't carry on as before
  after one, please report it.
- **Finding the files outside `jk1` and `mots`** is new in v0.4, and has
  mostly been tested on a computer. If an app doesn't find your files, put
  them in `jk1` (or `mots`) as this guide shows.
- **No multiplayer.** The iOS build has no networking.
- **No in-app file picker.** Game files have to be copied into the app's
  folder (`jk1`, or `mots` for OpenMoTS) with the Files app (or Finder /
  Apple Devices).
- **Not every PC key has a touch button.** There is no previous-weapon or
  previous-power button: use the weapon and Force wheels (NEXT WPN and FORCE
  WHEEL). There is no button for the
  in-game map overlay (MENU > Map shows the map instead), and no button for
  inventory items other than field light, IR goggles and bacta.
- **The run circle only runs straight ahead.** Turn on *Always Run* if you want
  to run in every direction.
- **The touch buttons only know the default key bindings** (see
  [Settings worth knowing](#settings-worth-knowing)).
- **No separate touch look-sensitivity setting** (the mouse sensitivity may
  work instead: see *Look speed* in [Settings worth knowing](#settings-worth-knowing)).
- **Game controllers and hardware keyboards** haven't been tested with either
  app.
- **Free Apple ID signing expires every 7 days**, and deleting the app deletes
  your files and saves.

## Reporting bugs

Please report problems with this iOS build on this repository's
[issue tracker](https://github.com/sithewok13-dev/OpenJKDF2-iOS/issues)
(you need a free GitHub account). Please don't report them to the upstream
OpenJKDF2 project, unless the same thing also happens in the desktop version.

To help track a bug down, please include:

- your device model and iOS version, for example *iPhone 15, iOS 18.5*
- which app: OpenJKDF2 (Jedi Knight) or OpenMoTS (Mysteries of the Sith)
- which build you're using: the release name (for example *v0.6*), or, for an
  "iOS latest build", the run number and commit from its description. (AltStore
  and Settings show the app's version as 0.9.9: that's the engine's version,
  not the release name.)
- what you did, what you expected, and what happened instead, plus the
  level and roughly where in it, if that matters
- a screenshot or a screen recording, if you can
- whether you copied the cutscenes and music, and whether you changed any
  settings

Please don't attach any of the game's own files.

## Credits and legal

- **OpenJKDF2** is by shinyquagsire23 (Max Thomas) and the OpenJKDF2
  contributors: <https://github.com/shinyquagsire23/OpenJKDF2>. That
  includes the upstream iOS support this build is based on. OpenJKDF2 is
  released under a permissive license: see [LICENSE.md](../../LICENSE.md).
- This build also includes other open-source libraries, each under its own
  license:
  - [OpenAL Soft](https://github.com/kcat/openal-soft/blob/d3875f333fb6abe2f39d82caca329414871ae53b/COPYING)
    (GNU LGPL, version 2 or later), linked statically into the app, and
    [libsmacker](../../src/external/libsmacker/COPYING) (GNU LGPL 2.1). Their
    full source, and the build scripts to rebuild the app with a changed
    version of them, are in this repository (OpenAL Soft as the `lib/openal`
    submodule).
  - [SDL3](https://github.com/libsdl-org/SDL/blob/f87239e71e42da91ca317a12eefb82cfbf3393eb/LICENSE.txt)
    and [SDL_mixer](https://github.com/libsdl-org/SDL_mixer/blob/72a81869b45e249e8e67102db4e98dd2441f05a1/LICENSE.txt)
    (zlib license), with libogg, libvorbis and Opus (BSD-style licenses)
  - [ANGLE](https://github.com/google/angle/blob/4b7308a36376199c095ccb0ab69b773daa8b9fbe/LICENSE)
    (BSD-style license), which runs the game's OpenGL ES graphics on Metal
  - [fcaseopen](../../src/external/fcaseopen/LICENSE.txt) and
    [libsmusher](../../src/external/libsmusher/LICENSE) (MIT license)
- OpenJKDF2 contains **no original game assets**. You need a legitimately
  purchased copy of Jedi Knight: Dark Forces II to play (and of Mysteries of
  the Sith, for OpenMoTS).
- Star Wars, Jedi Knight, LucasArts and related names are trademarks of
  Lucasfilm Ltd. and/or its affiliates. This project is not affiliated with,
  endorsed by or supported by Lucasfilm, Disney or LucasArts.
