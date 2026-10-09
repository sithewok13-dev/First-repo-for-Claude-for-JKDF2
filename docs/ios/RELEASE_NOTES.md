<!--
  The GitHub release text for a pushed v* tag: .github/workflows/ios.yml
  publishes this file as the release body (body_path), as it is in the tagged
  commit. Update it before tagging each release: the run fails before building
  unless this file has a "### What's in <tag>" heading line for the pushed tag
  (for example "### What's in v0.3"), written exactly like that and on a line
  of its own. A tag without a '-' (v0.2) is marked as the latest release; one
  with a '-' (v0.3-beta1) becomes a prerelease. Links must be absolute:
  relative links don't resolve on a release page. Keep each paragraph and
  list item on one line: a release page turns every line break into a
  visible one.
-->

**OpenJKDF2 for iOS v0.2**: an unofficial iPhone and iPad build of [OpenJKDF2](https://github.com/shinyquagsire23/OpenJKDF2), the open-source Jedi Knight: Dark Forces II engine, with on-screen touch controls. This release adds a **weapon wheel**, **tilt aiming**, Force power levels on the Force wheel, three ways to use the wheel buttons, and a shorter, safer QUICK LOAD. It's still **experimental**, so expect bugs.

**You need your own copy of Star Wars Jedi Knight: Dark Forces II** (GOG or Steam). No game files are included.

**Setup guide:** https://github.com/sithewok13-dev/First-repo-for-Claude-for-JKDF2/blob/HEAD/docs/ios/README.md

**New here?** *Requirements* and *Install* are at the bottom of this page, just above the download.

### What's in v0.2

New since v0.1:

- **A weapon wheel** on **NEXT WPN**: every weapon at once, with its ammo count, so you can pick the one you want. A quick tap still switches to the next weapon.
- **Tap, swipe or long-press** on **FORCE WHEEL** and **NEXT WPN**. Both buttons now work the same way: a quick tap steps to the next power or weapon, a swipe opens the wheel to pick by sliding, and a long press opens it to pick by tapping. See *Tap, swipe or long-press* below.
  - **Changed:** a quick tap on FORCE WHEEL now selects your **next learned Force power** straight away. In v0.1 it opened the wheel. To open the wheel for tapping, long-press it instead.
  - **Changed:** NEXT WPN now switches weapon when you **lift** your finger, not as soon as you touch it.
- **Force power levels on the Force wheel:** under each power you've learned, four stars show its level (one gold star per level).
- **Tilt aiming (gyro).** Aim by turning the phone, on top of dragging. It's **off** until you switch it on in MENU's hidden tray (hold **MENU**). There are three settings (OFF, TOUCH, ALWAYS) and four sensitivities.
- **MENU's hidden tray (hold MENU) has two new buttons:** it now holds **SENS**, **GYRO**, **FPS** and the **keyboard** (v0.1 had just FPS and the keyboard).
- **QUICK LOAD needs a much shorter hold:** about a third of a second, the same as QUICK SAVE (it was a full second). Both now follow the same safety rules: slide off to cancel, two fingers on one button save or load only once, and pressing both together only does the one whose ring fills first.
- **QUICK LOAD no longer works while the cheat typing line is open** (like QUICK SAVE).
- **FORCE sits a little further from the buttons along the top**, so a thumb reaching for FORCE won't land on QUICK SAVE, QUICK LOAD or MENU, and it stays clear of MENU's bigger tray.

Still here from v0.1: the floating move stick with its run marker, drag to look, FIRE / ALT / DUCK / ACT / JUMP / FORCE, the Force meter ring, LIGHT / IR / BACTA item buttons, a HUD that fits rounded and notched screens, tap to skip cutscenes, and the sound fixes. Still based on OpenJKDF2 0.9.9 (upstream as of September 2026).

---

### How to play: every control explained

The touch controls appear once you're playing a level, and hide in menus and cutscenes (there you just tap, like clicking with a mouse). The game runs in landscape, either way round.

```
 NEXT   FORCE   LIGHT  IR  BACTA         (FPS)  QUICK  QUICK  MENU
 WPN    WHEEL   (only items you have)            SAVE   LOAD
                                     [SENS] [GYRO] [FPS] [keyboard]
                                     (MENU's tray, when held)

      (^^)  run marker                              JUMP   FORCE
       |                                     ACT
     ( o )  move stick: appears                            ALT
            under your left thumb         DUCK     FIRE
                                                         [ammo gauge]
```

Exact positions shift a little to fit your screen, the camera cutout and the HUD. Buttons are slightly see-through until you touch them.

#### Moving, running and Always Run

- **Move:** put your left thumb down anywhere on the left side of the screen (a bit less than half of it). The stick appears right under your thumb and disappears when you lift it. It walks at the same speed however far you push it.
- **Run:** a small circle with **^^** in it sits above the stick, joined to it by a line. Push your thumb up past the stick's ring onto that circle to **run straight ahead**. It lights up blue and you feel a light tap. It doesn't lock: slide back down and you walk again, lift and you stop.
  - If your thumb lands near the top of the screen, the circle sits closer to the stick, and if there's no room at all, there's no circle that time.
- **Always Run:** to run all the time, in every direction, turn on the game's own **Always Run** option: tap **MENU**, then *Setup > Controls > Options*. While it's on, the **^^** circle goes away (you're always running anyway), and there's no way to walk slowly until you turn it off again. Turn it off and the circle comes back.

#### Looking and aiming

- **Drag to look:** drag anywhere else on the screen, which mostly means the right side.
- **Dragging on FIRE, ALT, DUCK, ACT, JUMP or FORCE also turns the view**, so if your thumb lands on one while you're aiming, just keep dragging. You can hold FIRE and aim with the same thumb.
- You can also aim by turning the phone: see *Tilt aiming* below.

#### Buttons

| Button | Where | What it does |
| --- | --- | --- |
| **FIRE** | bottom right, the big one | Fires your weapon, for as long as you hold it. |
| **DUCK** | left of FIRE | Crouches while held. |
| **ACT** | up and left of FIRE | Activate: doors, switches, elevators, consoles. |
| **JUMP** | above FIRE | Jumps. |
| **ALT** | up and right of FIRE, above the ammo gauge | Secondary fire (each weapon's second mode). |
| **FORCE** | right of JUMP (or above it on smaller screens) | Uses your selected Force power. See *The FORCE button and the Force meter* below. |
| **NEXT WPN** | top left | Tap: next weapon. Swipe or long-press: the weapon wheel. |
| **FORCE WHEEL** | top left | Tap: next learned Force power. Swipe or long-press: the Force wheel. |
| **LIGHT**, **IR**, **BACTA** | top left, after FORCE WHEEL | Tap to use the field light, IR goggles or a bacta tank. Each button only appears once you have that item, always in the same spot. LIGHT and IR turn yellow while switched on. BACTA shows how many you have when it's more than one. |
| **QUICK SAVE** | top right | Hold for about a third of a second. See *Quick save and quick load* below. |
| **QUICK LOAD** | top right | Hold for about a third of a second. |
| **MENU** | top right corner | Tap: the game's menu (objectives, map, Jedi powers, save, load, setup). Hold: the hidden tray. |

FIRE, ALT, DUCK, ACT, JUMP and FORCE act as soon as you touch them. LIGHT, IR, BACTA, MENU and the tray buttons act when you **lift** your finger, so if you touch one by mistake, slide off before you lift.

#### MENU's hidden tray

**Tap MENU** for the game's menu, as before. **Hold MENU** until its ring fills (about half a second) and a small tray opens just under it with four buttons, from left to right:

| Tray button | What it does |
| --- | --- |
| **SENS** | Tilt aiming's sensitivity: **1.0x**, **1.5x** (the default), **2.0x** or **3.0x**. At 1.0x the view turns exactly as far as the phone does. Dimmed while GYRO is off. |
| **GYRO** | Tilt aiming: **OFF** (the default), **TOUCH** or **ALWAYS**. Lit yellow while tilt aiming is on. See *Tilt aiming* below. |
| **FPS** | Shows or hides a frame-rate counter, to the left of QUICK SAVE. Yellow while the counter is on. |
| **Keyboard** (the keyboard symbol) | Opens the game's typing line with the iPhone keyboard, for cheats. Tap it again to close the line. |

- SENS and GYRO show their current setting on the button (for example *SENS 1.5x*, *GYRO OFF*).
- Either keep your thumb down, slide it onto a tray button and lift, or lift first and then tap one. Anywhere on the tray's dark backing counts as the nearest button.
- Each tap on **SENS** or **GYRO** goes on to the next setting, and the tray stays open so you can tap again. The tray closes after **FPS** or the keyboard, or when you touch anywhere else. That touch still counts: touching FIRE closes the tray and fires. Touching MENU again only closes it.
- If you slide off MENU while holding it, nothing happens.
- The app **remembers SENS, GYRO and FPS** next time you open it.
- On smaller screens (camera cutout on the right, Display Zoom, a large HUD Scale) the tray sits further left, so it stays clear of FORCE and the buttons around FIRE.

#### Tilt aiming (gyro)

Turn and tilt the phone to aim, on top of dragging. **It starts switched off.** Hold **MENU** and tap **GYRO** to step through the settings:

- **GYRO OFF** (the default): no tilt aiming. The app doesn't read the motion sensor at all.
- **GYRO TOUCH:** turning the phone aims while **either thumb** is touching the game: your left thumb on the move stick (even resting without moving), or your right thumb on the look area or on FIRE, ALT, DUCK, ACT, JUMP or FORCE. So with your left thumb on the stick, hopping your right thumb onto FIRE doesn't interrupt aiming. **To move the phone back to a comfortable position, lift both thumbs**: the view stays exactly where it is, like lifting a mouse off the desk, and carries on from there when you touch again. The buttons along the top don't count.
- **GYRO ALWAYS:** turning the phone always aims, with or without a thumb down.

Good to know:

- **The view never springs back.** There's no "straight ahead" position: however you hold the phone when you start is where you start from.
- Turning left and right works however far back you tip the phone, even lying flat on a table. Tilting the top edge of the screen toward you looks up.
- Tilt aiming pauses while a wheel, the cheat typing line or MENU's tray is open, and for a moment after you turn the phone round to the other landscape side.
- The game's mouse sensitivity doesn't change it: use **SENS**.
- On a device without a motion sensor, GYRO says **NO GYRO** and does nothing.

#### Tap, swipe or long-press

**FORCE WHEEL** and **NEXT WPN** each have three gestures (the setup guide calls them tap, slide and hold):

- **Quick tap:** lift within about a third of a second, without sliding. FORCE WHEEL selects your **next learned Force power**, skipping ones you haven't learned (the FORCE button shows which you have now). NEXT WPN switches to your **next weapon**. No wheel opens.
- **Swipe** (the quickest way to pick): touch the button and slide your thumb away. The wheel opens **at once**. Keep sliding towards a slot: a short slide in the right direction is enough. The slot pops out, its name shows in the middle, and you feel a light tap. **Lift to select it.**
- **Long press:** touch and hold still for about a third of a second. A ring fills round the button, then the wheel opens and **stays open**. Lift, then **tap** a slot to select it. (Or, without lifting, slide out from the button to pick by sliding after all.)

Then:

- **The middle of the wheel tells you what lifting will do**, for example *lift to select*, *not learned yet*, *no ammo* or *lift to cancel*.
- **To cancel** while sliding: lift in the **gap at the bottom** of the wheel (marked *cancel*), in the middle of the wheel, or back where you started. Lifting on a power you haven't learned or a weapon you can't pick also cancels. A slide too short to point at anything leaves the wheel open for tapping.
- **To cancel** a wheel that's open for tapping: tap anywhere that isn't a slot (the middle, the gap, outside the wheel, or either button at the top left). Tapping a power you haven't learned, or a weapon you can't switch to, does nothing and leaves the wheel open.
- **The game pauses while a wheel is open**, so take your time.

#### The Force wheel

Every Force power always has the same slot, whether you've learned it yet or not (powers you haven't learned are grey):

- **Left side, blue: light side.** From the top: Healing, Persuasion, Blinding, Absorb, Protection.
- **Across the top, gold: neutral.** From the left: Jump, Speed, Seeing, Pull.
- **Right side, red: dark side.** From the top: Throw, Grip, Lightning, Destruction, Deadly Sight.
- On the wheel and the FORCE button, a few names are shortened: PERSUADE, PROTECT and DESTRUCT.
- **The gap at the bottom** means *cancel*.
- **Stars (new):** under each power you've learned, a row of four stars shows its level, one gold star per level (a level 2 power shows two gold stars and two empty ones).

Once you've picked a power, use it with **FORCE**.

#### The weapon wheel

New in v0.2. Every weapon always has the same slot, whether you've found it yet or not, in the order of the PC number keys, clockwise from the bottom left: Fists, Bryar Pistol, Stormtrooper Rifle, Thermal Detonator, Bowcaster, Repeater, Rail Detonator, Sequencer Charge, Concussion Rifle, Lightsaber. The gap at the bottom means *cancel*.

- **Colours show the kind of ammo:** gold for the fists and the lightsaber (no ammo), blue for energy cells, green for power cells, red for explosives.
- **Ammo counts:** under each weapon you have, its ammo shows, the same number the HUD shows with that weapon in hand. Weapons that share ammo show the same number. The fists and the lightsaber show no count.
- **Grey, with no count** = you haven't found it yet.
- **Dark in its own colour, with its count (usually 0)** = you have it but can't fire it, so it can't be selected. The bowcaster needs at least 2 power cells and the concussion rifle at least 8, as in the game. Thermal detonators and sequencer charges are their own ammo, so with none left they show as not found.
- The weapon in your hand has a **white edge**.
- If you pick a weapon while the game is still in the middle of switching, it switches as soon as the game is ready. That includes the weapon you're putting away: pick it to switch straight back.

#### The FORCE button and the Force meter

- **FORCE** uses your selected Force power for as long as you hold it: tap for a single use, hold for powers that charge up (Force Jump) or keep going (Lightning).
- **The button shows the name of the power** it will use, under the word FORCE (for example SPEED). It's dimmed until you have a power.
- **The blue ring around FORCE is your Force meter.** It follows the game's meter live: it shrinks as you use the Force, disappears when the meter is empty, and grows back as the meter refills. When the meter is full, the ring **glows and gently pulses**.
- **The ring and the HUD's Force bar** (in the gauge at the bottom right, next to the ammo count) show the same meter, but on different scales. The ring is full at your current maximum, which rises with your Jedi rank. The HUD bar is only full at the top rank. So early on, the ring can be full and glowing while the HUD bar isn't. Before your first Jedi rank there's no ring.
- Pick a power with FORCE WHEEL (tap for the next one, or open the wheel).

#### Quick save and quick load: why the short hold

**QUICK SAVE** saves to the game's quick-save slot (the same one F9 uses on PC), and **QUICK LOAD** loads it. Both need you to **hold** the button for about a **third of a second**: a ring fills around it, and when it's full the game saves or loads, once, with your finger still down.

**Why the delay?** So a stray touch can't save over your quick save or throw away your progress by loading an old one. These buttons sit at the top of the screen next to MENU, and a quick brush past them does nothing.

- **To cancel**, lift or slide off before the ring fills. It won't go off even if you slide back on.
- **Even with two fingers on one button**, it saves or loads only once. To do it again, touch and hold again.
- **QUICK SAVE and QUICK LOAD pressed together:** only the one whose ring fills first goes off (if both fill at the same moment, it saves).
- Neither works while the cheat typing line is open.
- After a quick load the game shows *Quick-loaded*. If the quick save is from another level, that level is loaded. If you haven't quick saved yet, it says *No quicksave yet*.
- **Normal saves:** tap MENU and use the game's own save and load screens, as on PC.

#### Cheats

1. During a level, **hold MENU** and tap the **keyboard** button in the tray.
2. A typing line opens at the top of the screen with the iPhone keyboard. Type the cheat (autocorrect is off here) and tap **return**.
3. To close the line without sending anything, tap **MENU** (or the keyboard button in the tray again). While the line is open, MENU and the keyboard button are lit yellow.

While you're typing, most touch buttons don't work. A swipe or long press on FORCE WHEEL or NEXT WPN closes the line and opens its wheel (a quick tap does nothing while you type). Cheats only work in single player. A few favourites: `red5` (all weapons and ammo), `bactame` (full health and shields), `yodajammies` (full Force meter), `jediwannabe on` (invincibility). The full list is in the guide: https://github.com/sithewok13-dev/First-repo-for-Claude-for-JKDF2/blob/HEAD/docs/ios/README.md#cheats

#### Other things worth knowing

- **The HUD fits rounded, notched screens:** the health and ammo gauges sit above the home bar, the rounded corners never cut into the dials, and the health and shield numbers are drawn bigger so they're easy to read. The HUD Scale is 2.5 by default (*Setup > Display*).
- **Cutscenes:** tap the screen to skip. **Menus:** tap them as you'd click.
- **If the game feels slow:** in *Setup > Display*, try an *SSAA Multiplier* below 1 (for example `0.75` or `0.5`), and turn on the FPS counter in MENU's tray to see the difference.
- **Leave the key bindings at their defaults** (*Setup > Controls*). The touch buttons press the game's default keys, so if you rebind an action, its touch button stops working.
- **Your saves** are in the Files app, in *On My iPhone (or On My iPad) > OpenJKDF2 > jk1 > player*. Copy that folder somewhere safe now and then. Deleting the app deletes it.

### Requirements

- iPhone or iPad on **iOS / iPadOS 18 or newer** (iPhone XS / XR or newer, including the iPhone SE 2nd generation and later)
- Your own Jedi Knight game files, at least `episode` and `resource`, ideally with `resource/video` (cutscenes) and `MUSIC` (soundtrack)
- A sideloading tool that can install your own `.ipa` (for example AltStore, SideStore or Sideloadly). With a free Apple ID, the app has to be re-signed every 7 days, and you can have only 3 sideloaded apps at a time (AltStore or SideStore itself counts as one).

### Install

1. Download **`OpenJKDF2-iOS-unsigned.ipa`** below.
2. Sign and install it with your sideloading tool. The first time, iOS may ask you to turn on **Developer Mode** and to **trust** your Apple ID.
3. Open the app once, then use the Files app to copy your `Episode`, `Resource` and `MUSIC` folders into *On My iPhone (or On My iPad) > OpenJKDF2 > jk1*.

The setup guide covers each step, including Developer Mode and "Untrusted Developer": https://github.com/sithewok13-dev/First-repo-for-Claude-for-JKDF2/blob/HEAD/docs/ios/README.md#2-install-the-app

**Updating from v0.1:** sideload the new `.ipa` with the same tool and Apple ID. It replaces the app and keeps your game files and saves. (With a different tool or Apple ID, iOS may install it as a separate app with an empty folder: the guide explains how to move your files over.)

### Known limitations

- **Mysteries of the Sith isn't supported** in this app (it hasn't been tested). Please ignore *Install Mysteries of the Sith* under *Expansions & Mods*.
- **No multiplayer.** The iOS build has no networking.
- **The touch buttons only know the default key bindings** (see above).
- **No previous-weapon or previous-power button:** use the wheels. There's no button for the in-game map overlay (MENU > Map shows the map instead), and no button for items other than the field light, IR goggles and bacta.
- **The run circle only runs straight ahead.** Turn on *Always Run* to run in every direction.
- **No separate touch look-sensitivity setting** for dragging. The game's mouse *Sensitivity* (*Setup > Controls > Mouse*) should change it, but that hasn't been tested yet. Tilt aiming has SENS.
- **Game controllers and hardware keyboards** haven't been tested with this build.
- **Free Apple ID signing expires every 7 days**: refresh the app in AltStore or SideStore, or install it again with the same tool and Apple ID. Your files and saves are kept.
- It has only had limited testing so far.

Found a bug? Please report it here, not to the upstream OpenJKDF2 project, and say you're on **v0.2** (AltStore and Settings show 0.9.9: that's the engine's version): https://github.com/sithewok13-dev/First-repo-for-Claude-for-JKDF2/issues

---

Unofficial build. OpenJKDF2 is by shinyquagsire23 (Max Thomas) and the OpenJKDF2 contributors. This is not an official OpenJKDF2 release, and it is not affiliated with or endorsed by Lucasfilm, Disney or LucasArts. Star Wars and Jedi Knight are trademarks of Lucasfilm Ltd. No game assets are included.

The app includes open-source libraries under their own licenses, among them OpenAL Soft and libsmacker (GNU LGPL; OpenAL Soft is linked statically, and the full source and build scripts are in this repository), SDL3 and SDL_mixer (zlib) and ANGLE (BSD-style). The list with links to each license is in the guide: https://github.com/sithewok13-dev/First-repo-for-Claude-for-JKDF2/blob/HEAD/docs/ios/README.md#credits-and-legal
