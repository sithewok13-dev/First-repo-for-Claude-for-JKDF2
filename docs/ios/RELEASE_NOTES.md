<!--
  The GitHub release text for a v* release, made by a manual run of
  .github/workflows/ios.yml with release=<tag> (a pushed v* tag also makes
  one). The workflow publishes this file as the release body (body_path), as
  it is in the commit it builds, with both .ipa files attached (Jedi Knight
  and OpenMoTS). Update it before each release run: the run fails before
  building unless this file has a "### What's in <tag>" heading line for that
  tag (for example "### What's in v0.4"), written exactly like that and on a
  line of its own. A tag without a '-' (v0.2) is marked as the latest
  release; one with a '-' (v0.3-beta1) becomes a prerelease. Links must be
  absolute: relative links don't resolve on a release page. Keep each
  paragraph and list item on one line: a release page turns every line break
  into a visible one.
-->

**OpenJKDF2 for iOS v0.6**: an unofficial iPhone and iPad build of [OpenJKDF2](https://github.com/shinyquagsire23/OpenJKDF2), the open-source Jedi Knight: Dark Forces II engine, with on-screen touch controls. **New in this release: lightsabers your way, cheat mode, and a photo camera that stops time.** Pick your blade's colour from 17, or fight with **two sabers** at once, each in its own colour. Switch on **cheat mode** and a red Sith emblem button turns `sithewok` on and off, with new powers on the buttons you already use: **fly**, walk **through walls**, and **jump to any level**. And the camera now does **Action Freeze**: stop the fight mid-blast, swing the camera round it and take your shot, or let it sweep round the action by itself for five cinematic photos. It's all in both apps, Jedi Knight and Mysteries of the Sith. Coming from v0.4? You also get the **selfie camera** from v0.5. From v0.3 or older? Also **Mysteries of the Sith** in its own app, **OpenMoTS**. It's still **experimental**, so expect bugs.

**You need your own copy of the game; no game files are included.** OpenJKDF2 needs Star Wars Jedi Knight: Dark Forces II, and OpenMoTS needs Star Wars Jedi Knight: Mysteries of the Sith (GOG and Steam sell both).

**Two apps, one for each game:** **OpenJKDF2** (`OpenJKDF2-iOS-unsigned.ipa`) plays Jedi Knight, and **OpenMoTS** (`OpenJKDF2-MotS-iOS-unsigned.ipa`) plays Mysteries of the Sith. Install the one for the game you have, or both: they sit side by side, each with its own icon and its own folder for its files and saves. **OpenMoTS** (new in v0.4) has had much less testing than OpenJKDF2 (see *Known limitations*). If something goes wrong in it, please report it (see *Found a bug?*).

**Setup guide:** https://github.com/sithewok13-dev/OpenJKDF2-iOS/blob/HEAD/docs/ios/README.md

**New here?** *Known limitations*, *Requirements* and *Install* are near the bottom of this page, just above the download.

### What's in v0.6

New since v0.5, in both apps:

- **Lightsaber colours:** a **SABER** button on the top row (once you have the lightsaber) opens a saber wheel with **17 colours**: the game's own (ORIGINAL), red, crimson, orange, amber, yellow, lime, green, teal, cyan, light blue, blue, purple, magenta, pink, white and black. It changes only how your blade looks, in first and third person and in photos. See *The lightsaber: SABER* below.
- **Two sabers:** choose **DUAL** on the saber wheel and Kyle (or Mara) fights with a saber in each hand, each in its own colour. The second blade strikes too. In first person both arms move, and between fights the two sabers settle into a few natural resting stances, with a faint shimmer on each blade.
- **Cheat mode:** a **CHEAT** switch in MENU's hidden tray. Switched on, a red **Sith emblem** button appears on the top row and `sithewok` goes on, with a laugh from the Emperor. Hold the emblem for half a second to switch the cheat off or on again. See *Cheats* below.
- **New cheat powers while `sithewok` is on:** hold **JUMP** to **fly**, hold **ACT** for **noclip** (through walls), and hold **DUCK** to **pick any level**. Falls never hurt, the Force catches you if you drop into a pit, and the cheat now stays on through new levels and loaded games, keeping your weapon and Force power.
- **Action Freeze:** slide down off **CAMERA** and the game stops exactly where it is, blaster bolts and explosions in mid-air. Turn the camera round with one finger, move and zoom it with two, then take your photo. See *Action Freeze* below.
- **The cinematic burst:** slide up off **CAMERA** for five photos as the camera sweeps round the action (a duel, a boss, a blast), then the game carries on.
- **Selfies of 1, 3 or 5 photos** (3 by default), each one posed afresh. Kyle sometimes shows off his gun or saber, and now and then something unexpected happens: someone points at something, someone didn't notice the photo, or a passer-by photobombs.
- **MENU's tray is rearranged:** GYRO, FPS, CAMERA, CHEAT and the keyboard. **Tap GYRO** to switch tilt aiming on or off; **hold GYRO** for its MODE and SENS. **Hold CAMERA** for PHOTO, SELFIES and FREEZE. See *MENU's hidden tray* below.
- **CAMERA shows from the start** on a new install (switch it off in the tray if you don't want it).
- **BACTA needs a short hold** (a third of a second, with a ring) so a stray touch can't waste a bacta tank.
- **Fixes:** a saber put away quickly could stay lit on the next weapon (a gun with a giant blade). The blue blade looked white: it's properly blue now. A level-count message could switch the cheat off by mistake.

Added in v0.5 (if you're coming from v0.4):

- **The selfie camera:** hold the camera button and the whole game freezes. Everyone nearby turns to the camera and strikes a pose, you hold the phone out, the flash goes off, and then everything carries on exactly as it was. Tap the same button for ordinary photos of your game, or slide off it for a burst, all straight into your Photos app.
- **Easier-to-read mission text:** the game's messages appear just below the top row of buttons instead of behind them, a third bigger, and long lines wrap between words.

Added in v0.4 (if you're coming from v0.3):

- **Mysteries of the Sith, in its own app: OpenMoTS.** The same engine and the same touch controls as Jedi Knight, in a second app with its own icon and its own folder in the Files app, so it installs next to OpenJKDF2 and keeps its files and saves apart. Its Force wheel and weapon wheel show Mysteries of the Sith's own Force powers and weapons, and the `sithewok` cheat works there too. See *Mysteries of the Sith: the OpenMoTS app* below.
- **Both apps find your game files in more places.** The recommended place is still the game folder the app makes (`jk1` in OpenJKDF2, `mots` in OpenMoTS). But the apps now also find the files straight in the app's own folder in the Files app (*OpenJKDF2* or *OpenMoTS*), or in a folder inside it, such as one made by unpacking a `.zip`. If your files are already in `jk1`, nothing changes.

Added in v0.3 (if you're coming from v0.2):

- **A cheat of this build's own, `sithewok`:** everything at once (all weapons and ammo, every item, every Force power at 4 stars, full health and shields, and invincibility) and a Force meter that never drains. Careful: in Jedi Knight **it can decide your story path**. See *Cheats* below.

Added in v0.2 (if you're coming from v0.1):

- **A weapon wheel** on **NEXT WPN**: every weapon at once, with its ammo count, so you can pick the one you want. A quick tap still switches to the next weapon.
- **Tap, swipe or long-press** on **FORCE WHEEL** and **NEXT WPN** (see *Tap, swipe or long-press* below). **Changed:** a quick tap on FORCE WHEEL now selects your next learned Force power (in v0.1 it opened the wheel: long-press it instead), and NEXT WPN switches weapon when you **lift** your finger.
- **Force power levels on the Force wheel:** four stars under each power you've learned, one gold star per level.
- **Tilt aiming (gyro):** aim by turning the phone, on top of dragging. It's **off** until you switch it on in MENU's hidden tray (hold **MENU**), which now holds **SENS**, **GYRO**, **FPS** and the **keyboard**.
- **QUICK LOAD needs a much shorter hold:** about a third of a second, the same as QUICK SAVE, with the same safety rules. Neither works while the cheat typing line is open.
- **FORCE sits a little further from the buttons along the top**, so a thumb reaching for FORCE won't land on QUICK SAVE, QUICK LOAD or MENU.

Still here from v0.1: the floating move stick with its run marker, drag to look, FIRE / ALT / DUCK / ACT / JUMP / FORCE, the Force meter ring, LIGHT / IR / BACTA item buttons, a HUD that fits rounded and notched screens, tap to skip cutscenes, and the sound fixes. Still based on OpenJKDF2 0.9.9 (upstream as of September 2026).

---

### How to play: every control explained

The touch controls appear once you're playing a level, and hide in menus and cutscenes (there you just tap, like clicking with a mouse). The game runs in landscape, either way round. **Both apps have the same controls.** What's different in Mysteries of the Sith (its wheels and cheats) is in *Mysteries of the Sith: the OpenMoTS app* below.

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

Exact positions shift a little to fit your screen, the camera cutout and the HUD. Buttons are slightly see-through until you touch them. SABER shows once you have the lightsaber, and CHEAT only in cheat mode (see *Cheats*). Where the top row gets crowded, the FPS counter moves to the bottom middle of the screen.

#### The camera: selfies, photos, bursts and Action Freeze

**CAMERA** (the camera symbol) sits in the middle of the top row. It's there from the start; to hide it (or show it again), hold **MENU** and tap the **camera symbol** in the tray. The first time you use it, iOS asks whether the app may **add photos** to your library: say yes, and the photos go straight into your Photos app. The game holds still while iOS asks. The app can only add photos; it can't see the ones already in your library.

What CAMERA does depends on how you touch it:

| Gesture | What it does |
| --- | --- |
| **Hold still** (half a second, a ring fills) | A selfie (1, 3 or 5 photos). |
| **Tap** | A photo of the game. |
| **Slide off sideways** | A burst: a photo every fifth of a second while your finger stays down. |
| **Slide down** (a long slide) | Action Freeze: stop the game and move the camera round it. |
| **Slide up** (towards the top edge) | The cinematic burst: five photos as the camera sweeps round the action. |

With **FREEZE** switched off (in CAMERA's options, below), a slide in any direction is a burst.

**Selfies:** hold CAMERA still for half a second.

- The whole game freezes, enemies and bosses included, and the HUD and touch buttons hide.
- The view switches to the phone, held out at arm's length and facing you. Up to **8 people near you**, in sight, turn to it and strike a **random pose**: wave, fist up, hand on hip, point, flex, salute, hands behind the head, victory sign, shrug, hand on the chin or the heart, arms crossed, an arm round a neighbour, and more. Some of the front row kneel on one knee or crouch, some turn three-quarters to the camera or lean, and now and then a whole group strikes the same pose like a team photo. Droids and creatures just turn to face the camera.
- **You** (Kyle, or Mara Jade in her levels of Mysteries of the Sith) hold the phone out in your right or left hand and strike a pose with the other, looking toward the lens. Sometimes you show off your gun or lightsaber instead. The camera angle changes from shot to shot: sometimes a little higher or lower, sometimes slightly tilted.
- **Now and then something unexpected happens:** someone points at something, someone hasn't noticed the photo, someone stares, or a passer-by photobombs.
- **1, 3 or 5 photos:** a selfie takes **3** unless you change it with **SELFIES** in CAMERA's options. Everyone poses afresh for each one, and the game stays frozen the whole time.
- **The flash** lights everyone up, and shots or blasts flying in front of someone's face are left out of the photo.
- After the flash and the shutter, the photo stays on screen for a moment. Then everyone goes back to exactly what they were doing, and the game carries on. A selfie changes nothing in your game.
- **Nobody nearby?** It's a selfie of you alone. If people are spread out too far to fit in one photo, the ones furthest away are left out.
- Selfies are always clean photos (*... selfie.png*), whatever PHOTO is set to.
- **When it can't:** if there's no room to hold the camera out (right up against a wall), or the game is showing a cutscene, a scope or another camera, or you're dead, it says *Can't take a selfie here*. If the last photos are still being saved, it says *Still saving the last photo*: try again in a moment. While the typing line is open, holding CAMERA does nothing (a tap still takes a photo).

**Photos and bursts:**

- **Tap CAMERA:** a white flash, the camera's shutter sound, and the photo. A line at the top of the screen says when it's saved (*1 photo saved*).
- **Burst:** touch CAMERA and slide your finger off it sideways (a short slide is enough). The app takes a photo every fifth of a second for as long as your finger stays down, up to 10, with a quick tick for each and a counter beside the button. Lift your finger to stop. Burst photos are always clean ones, without the HUD.

**Action Freeze** (new in v0.6): touch CAMERA and slide your finger **down**, further than for a burst.

- The game stops exactly where it is: blaster bolts, explosions, flying debris and everyone mid-move. The buttons hide, apart from CAMERA, and an **X** appears at the top right.
- **One finger** turns the camera round what it's looking at. **Two fingers** move it, and **pinch** to zoom in or out.
- The camera stays out of walls, closed doors and lifts. If it can't go somewhere, it stops. Close to a wall it moves in a little, and glides back out when there's room.
- **Tap CAMERA** to take a clean photo, as many as you like. **Tap the X** to go back to the game, which carries on from exactly the same moment.
- If it can't freeze (a cutscene, a scope, while dead), it says *Can't freeze here*.

**The cinematic burst** (new in v0.6): touch CAMERA and slide your finger **up**. Reaching near the top edge of the screen is enough.

- The game freezes for a moment and the camera takes **five photos** as it sweeps round the action, then the game carries on by itself.
- The first photo is exactly what you saw. The other four go round the most interesting thing in front of you: someone you're fighting, a shot in flight or an explosion. In a duel or a boss fight, you and your opponent are both in the shots, seen from the side. With nobody around, it goes round you.
- The sweep picks the side with the most room, so the camera doesn't end up inside a wall or behind someone. In a cramped spot it sweeps less, or just zooms in.

**CAMERA's options:** open the tray (hold MENU), then touch and hold the **camera symbol in the tray** until its ring fills. A row opens under the tray with three buttons:

- **PHOTO:** what a tap takes. Tap PHOTO for its three tick boxes: **CLEAN** (just the game, no gauges or buttons: ticked unless you change it), **HUD** (the game with its health and ammo gauges) and **HUD+BTNS** (with the touch buttons too, as you see the screen). At least one stays ticked. If you tick more than one, each tap of CAMERA takes one photo of each, all of the same moment.
- **SELFIES:** how many photos a selfie takes: **1**, **3** (the default) or **5**. Each tap goes on to the next.
- **FREEZE:** **ON** (the default) or **OFF**. Off, sliding CAMERA down or up is just a burst, as before v0.6.
- Touch anywhere else to close the row and the tray.

**Where the photos go:**

- Photos are full-screen PNGs, named after the app and the time, for example *OpenJKDF2 2026-10-12 14.05.09 clean.png* (a burst's are numbered: *... burst 01.png*). In Mysteries of the Sith they start with *OpenMoTS*.
- **If Photos access is off** (you said no, or turned it off in *Settings*), the photos go to the Files app instead, in *OpenJKDF2 > Screenshots* (or *OpenMoTS > Screenshots*), and the message says so. To use the Photos app again: *Settings > Privacy & Security > Photos*, then the app, then *Add Photos Only*.

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
| **LIGHT**, **IR**, **BACTA** | top left, after FORCE WHEEL | Tap LIGHT or IR to switch the field light or IR goggles on or off (yellow while on). **Hold BACTA** for a third of a second (a ring fills) to use a bacta tank: one per press, and a tap or a brush past uses none. Each button only appears once you have that item, always in the same spot. BACTA shows how many you have when it's more than one. |
| **QUICK SAVE** | top right | Hold for about a third of a second. See *Quick save and quick load* below. |
| **QUICK LOAD** | top right | Hold for about a third of a second. |
| **MENU** | top right corner | Tap: the game's menu (objectives, map, Jedi powers, save, load, setup). Hold: the hidden tray. |
| **CAMERA** (the camera symbol) | top, in the middle | Hold still: a selfie. Tap: a photo. Slide sideways: a burst. Slide down: Action Freeze. Slide up: the cinematic burst. See *The camera* above. |
| **SABER** (a lightsaber) | top, right of CAMERA (once you have the lightsaber) | The saber wheel: blade colours, one saber or two. See *The lightsaber: SABER* below. |
| **CHEAT** (the Sith emblem) | top, right of SABER (cheat mode only) | Hold for half a second: `sithewok` off or on. Red while it's on. See *Cheats* below. |

FIRE, ALT, DUCK, ACT, JUMP and FORCE act as soon as you touch them. LIGHT, IR, MENU, CAMERA and the tray buttons act when you **lift** your finger, so if you touch one by mistake, slide off before you lift (on CAMERA, a slide starts a burst or Action Freeze instead: see *The camera* above). BACTA and CHEAT need a hold, so a brush past does nothing. While `sithewok` is on, JUMP, ACT and DUCK show a small red second line (FLY, NOCLIP, LVL SEL) for their cheat holds: see *Cheats* below.

#### MENU's hidden tray

**Tap MENU** for the game's menu, as before. **Hold MENU** until its ring fills (about half a second) and a small tray opens just under it with six buttons, from left to right:

| Tray button | What it does |
| --- | --- |
| **GYRO** | **Tap:** tilt aiming on or off (it comes back on in the mode you last used: TOUCH the first time). Lit yellow while it's on. **Touch and hold** until its ring fills: a row under the tray with **MODE** (**TOUCH** or **ALWAYS**) and **SENS** (**1.0x**, **1.5x**, the default, **2.0x** or **3.0x**). See *Tilt aiming* below. |
| **FPS** | Shows or hides a frame-rate counter, to the left of QUICK SAVE. Yellow while the counter is on. |
| **CAMERA** (the camera symbol) | **Tap:** shows or hides the CAMERA button (yellow while it's shown). **Touch and hold** until its ring fills: CAMERA's options, **PHOTO**, **SELFIES** and **FREEZE**. See *The camera* above. |
| **CHEAT** | Cheat mode on or off. On: the CHEAT button appears and `sithewok` goes on. Off: `sithewok` goes off and the button goes away. Yellow while cheat mode is on. See *Cheats* below. |
| **JUMP** | How Force Jump works: **SMOOTH** (the default), **STEPS** or **CLASSIC**. Each tap goes on to the next, and the tray stays open. See *Force Jump* below. |
| **Keyboard** (the keyboard symbol) | Opens the game's typing line with the iPhone keyboard, for cheats. Tap it again to close the line. |

- GYRO and JUMP show their setting on the button (for example *GYRO OFF*, *JUMP SMOOTH*), and MODE and SENS show theirs.
- Either keep your thumb down, slide it onto a tray button and lift, or lift first and then tap one. Anywhere on the tray's dark backing counts as the nearest button.
- Each tap on **JUMP**, **MODE** or **SENS** goes on to the next setting, and the tray (or row) stays open so you can tap again. The tray closes after **FPS**, **CAMERA**, **CHEAT** or the keyboard, or when you touch anywhere else. That touch still counts: touching FIRE closes the tray and fires. Touching MENU again only closes it.
- If you slide off MENU while holding it, nothing happens.
- The app **remembers GYRO, SENS, FPS, CAMERA and its options, CHEAT and JUMP** next time you open it. The `sithewok` cheat itself always starts off when you open the app: hold the CHEAT button to switch it on.
- On smaller screens (camera cutout on the right, Display Zoom, a large HUD Scale) the tray sits further left, so it stays clear of FORCE and the buttons around FIRE.

#### Tilt aiming (gyro)

Turn and tilt the phone to aim, on top of dragging. **It starts switched off.** Hold **MENU** and tap **GYRO** to switch it on or off. To choose between TOUCH and ALWAYS, or change the sensitivity, hold GYRO in the tray until its ring fills, then tap **MODE** or **SENS**:

- **GYRO OFF** (the default): no tilt aiming. The app doesn't read the motion sensor at all.
- **GYRO TOUCH:** turning the phone aims while **either thumb** is touching the game: your left thumb on the move stick (even resting without moving), or your right thumb on the look area or on FIRE, ALT, DUCK, ACT, JUMP or FORCE. So with your left thumb on the stick, hopping your right thumb onto FIRE doesn't interrupt aiming. **To move the phone back to a comfortable position, lift both thumbs**: the view stays exactly where it is, like lifting a mouse off the desk, and carries on from there when you touch again. The buttons along the top don't count.
- **GYRO ALWAYS:** turning the phone always aims, with or without a thumb down.

Good to know:

- **The view never springs back.** There's no "straight ahead" position: however you hold the phone when you start is where you start from.
- Turning left and right works however far back you tip the phone, even lying flat on a table. Tilting the top edge of the screen toward you looks up.
- Tilt aiming pauses while a wheel, the cheat typing line or MENU's tray is open, and for a moment after you turn the phone round to the other landscape side.
- The game's mouse sensitivity doesn't change it: use **SENS** (hold GYRO in the tray).
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

In Jedi Knight, every Force power always has the same slot, whether you've learned it yet or not (powers you haven't learned are grey):

- **Left side, blue: light side.** From the top: Healing, Persuasion, Blinding, Absorb, Protection.
- **Across the top, gold: neutral.** From the left: Jump, Speed, Seeing, Pull.
- **Right side, red: dark side.** From the top: Throw, Grip, Lightning, Destruction, Deadly Sight.
- On the wheel and the FORCE button, a few names are shortened: PERSUADE, PROTECT and DESTRUCT.
- **The gap at the bottom** means *cancel*.
- **Stars (added in v0.2):** under each power you've learned, a row of four stars shows its level, one gold star per level (a level 2 power shows two gold stars and two empty ones).

Once you've picked a power, use it with **FORCE**.

#### The weapon wheel

Added in v0.2. In Jedi Knight, every weapon always has the same slot, whether you've found it yet or not, in the order of the PC number keys, clockwise from the bottom left: Fists, Bryar Pistol, Stormtrooper Rifle, Thermal Detonator, Bowcaster, Repeater, Rail Detonator, Sequencer Charge, Concussion Rifle, Lightsaber. The gap at the bottom means *cancel*.

- **Colours show the kind of ammo:** gold for the fists and the lightsaber (no ammo), blue for energy cells, green for power cells, red for explosives.
- **Ammo counts:** under each weapon you have, its ammo shows, the same number the HUD shows with that weapon in hand. Weapons that share ammo show the same number. The fists and the lightsaber show no count.
- **Grey, with no count** = you haven't found it yet.
- **Dark in its own colour, with its count (usually 0)** = you have it but can't fire it, so it can't be selected. The bowcaster needs at least 2 power cells and the concussion rifle at least 8, as in the game. Thermal detonators and sequencer charges are their own ammo, so with none left they show as not found.
- The weapon in your hand has a **white edge**.
- If you pick a weapon while the game is still in the middle of switching, it switches as soon as the game is ready. That includes the weapon you're putting away: pick it to switch straight back.

#### The lightsaber: SABER

New in v0.6. Once you have the lightsaber (or while `sithewok` is on), a **SABER** button appears on the top row, showing your blade (or two crossed blades) in your colours. It opens the **saber wheel**, the same way as the other wheels: tap or hold it to open the wheel and tap a colour, or slide off it towards a colour and lift. The gap at the bottom cancels, and the game holds still while the wheel is open.

- **17 colours:** ORIGINAL (the game's own, unchanged), red, crimson, orange, amber, yellow, lime, green, teal, cyan, light blue, blue, purple, magenta, pink, white, and black with a bright edge.
- **The wheel stays open** after you pick a colour, so you can change both blades in one go. Close it with the gap at the bottom, or by tapping outside the wheel.
- **EQUIP**, in the middle of the wheel, closes it and puts the lightsaber in your hand. It's dim while you don't have the saber.
- **SINGLE or DUAL**, beside the wheel: one saber, or one in each hand. With DUAL, **LEFT**, **BOTH** and **RIGHT** choose which blade a colour goes to. Each style keeps its own colours.
- **Two sabers fight for real:** the second blade strikes, deals damage and throws sparks like the first. In first person both arms move, and between swings the two sabers settle into a few natural resting stances, with a faint shimmer on each blade. Kyle has no new animations, so in his two-handed swings the blades come from nearly the same place.
- **Only the look changes,** and only in your own single-player game: the saber's damage, your saves and the levels are untouched. The colours show in first and third person, in selfies and in Action Freeze. The app remembers your colours and style.
- A lightsaber that only `sithewok` gave you goes away again when the cheat goes off (see *Cheats*). One the game gave you stays.

#### The FORCE button and the Force meter

- **FORCE** uses your selected Force power for as long as you hold it: tap for a single use, hold for powers that charge up (Force Jump) or keep going (Lightning).
- **The button shows the name of the power** it will use, under the word FORCE (for example SPEED). It's dimmed until you have a power.
- **The blue ring around FORCE is your Force meter.** It follows the game's meter live: it shrinks as you use the Force, disappears when the meter is empty, and grows back as the meter refills. When the meter is full, the ring **glows and gently pulses**.
- **The ring and the HUD's Force bar** (in the gauge at the bottom right, next to the ammo count) show the same meter, but on different scales. The ring is full at your current maximum, which rises with your Jedi rank. The HUD bar is only full at the top rank. So early on, the ring can be full and glowing while the HUD bar isn't. Before your first Jedi rank there's no ring.
- Pick a power with FORCE WHEEL (tap for the next one, or open the wheel).

#### Force Jump

Force Jump jumps when you **lift** your finger off FORCE, and how long you held it decides how high. It has 4 levels (the stars on the Force wheel): each star raises the highest jump, and gives you a little longer to reach it (0.35 s a star, so 1.4 s at 4 stars). Choose how holding works with **JUMP** in MENU's tray:

- **SMOOTH** (the default): a **tap** is the lowest Force Jump (a bit under half the full height). The longer you hold, the higher it goes, smoothly, up to the highest for your stars. A **gold ring** fills inside FORCE as you hold, with a light tick when it's full: lift whenever the ring shows the height you want.
- **STEPS:** the same heights in clear steps, one a star. A tap is the lowest step, and every 0.35 s you hold goes up a step, with a tick and a quarter more of the gold ring (at 4 stars), to the highest.
- **CLASSIC:** exactly as the original game: a quick tap is the **highest** jump, and so is a hold past the highest; only a hold in between is lower. No ring.

In every style the jump costs the same Force and needs you on the ground (or on water), and the other Force powers work as before.

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

While you're typing, most touch buttons don't work. A swipe or long press on FORCE WHEEL or NEXT WPN closes the line and opens its wheel (a quick tap does nothing while you type). Cheats only work in single player. A few favourites in Jedi Knight: `sithewok` (everything at once, see below), `red5` (all weapons and ammo), `bactame` (full health and shields), `yodajammies` (full Force meter), `jediwannabe on` (invincibility). The full list is in the guide: https://github.com/sithewok13-dev/OpenJKDF2-iOS/blob/HEAD/docs/ios/README.md#cheats

**`sithewok`** is this iOS build's own cheat (the original game doesn't have it). It gives you everything at once: all weapons and ammo, all items, every Force power at 4 stars with the top Jedi rank, full health, shields and Force meter, and invincibility. While it's on:

- **The Force meter never drains**, however much you use your powers.
- **Ammo never runs out:** every kind is filled to its most when it goes on, and refills as you fire, the battery for the field light and IR goggles too.
- **Falls never hurt you**, however far you drop.
- **The Force catches you** if you fall into a bottomless pit: a soft blue flash, and you're back on the ledge you fell from.
- **It stays on** through new levels and loaded games, and puts everything back at the start of each level. Your weapon in hand and your selected Force power come with you to the next level.
- **JUMP, ACT and DUCK get new holds** (each shows its name in small red letters under the button while the cheat is on):
  - **Hold JUMP** still for half a second (a ring fills): **fly**. You float, forward goes where you look, hold JUMP to rise and DUCK to sink. Hold JUMP again for half a second, or **double-tap JUMP**, to land. Walls still stop you.
  - **Hold ACT** still for half a second: **noclip**, flying straight through walls. Hold it again to switch it off.
  - **Hold DUCK** still: **pick a level**. After half a second the button shows **1**, then counts up one every 0.4 seconds (with a tick each, and *Level 7 of 21* on the message line), going round to 1 again after the last level. Lift your finger to go to that level. Slide off to cancel.
  - A quick press of JUMP, ACT or DUCK does what it always does.

**Typing `sithewok` again switches it off** (so does the CHEAT button, below): no more invincibility, endless Force, safe falls, flying or noclip. You keep the weapons, items and Force powers it gave you, except a lightsaber you only had from the cheat. Type `sithewok` once more to switch it back on and fill everything back up. It also goes off when you close the app (swipe it away).

**Cheat mode: the CHEAT button.** Hold **MENU** and tap **CHEAT** in the tray. A **CHEAT** button with the **red Sith emblem** appears on the top row, `sithewok` goes on, and the Emperor laughs. Then:

- **Hold CHEAT** for half a second (a ring fills) to switch `sithewok` off (the emblem goes grey) or on again (red). A tap does nothing, so you can't switch it by accident.
- **Typing works too:** typing `sithewok` while it's off switches it on and brings the CHEAT button back (with the laugh). Typing `sithewok` while it's on switches it off and the button goes away. Holding the button instead leaves it on screen, grey.
- **To leave cheat mode,** tap CHEAT in the tray again: `sithewok` goes off and the button goes away.
- The app remembers cheat mode, but `sithewok` itself starts off each time you open the app: hold CHEAT to switch it on.
- The laugh plays at the game's sound effects volume, so turn that down if you'd rather not hear it.

Catches, in Jedi Knight (Mysteries of the Sith's are in its section below):

- **It can change the story.** With every light and dark power at 4 stars, the two sides cancel out, so when the game picks your path (after level 14) you get the light side unless you've killed more than about one in five of the civilians. Switching it off doesn't undo this, because you keep the stars. If you care which side you end up on, keep a save from before you first use `sithewok`, or wait to use it until your path has been set after level 14.
- On the Force screen after level 14, where your path is set, the game takes the other side's stars away one at a time, which takes about 16 seconds if you've used `sithewok`. Wait until it stops before you tap OK, or your path isn't fully set until a later Force screen.
- **Picking a level** with DUCK jumps straight there, skipping the story in between, and goes to the level itself rather than the cutscene before it. Where a level number has a light and a dark side version, you get the first one. Keep a normal save of your real progress.
- Pits that a level's own script uses to end you (rather than an ordinary fall) can still kill you.

#### Other things worth knowing

- **Messages and mission text** appear just below the top row of buttons, so the buttons never cover them, and long lines wrap.
- **The HUD fits rounded, notched screens:** the health and ammo gauges sit above the home bar, the rounded corners never cut into the dials, and the health and shield numbers are drawn bigger so they're easy to read. The HUD Scale is 2.5 by default (*Setup > Display*).
- **Cutscenes:** tap the screen to skip. **Menus:** tap them as you'd click.
- **If the game feels slow:** in *Setup > Display*, try an *SSAA Multiplier* below 1 (for example `0.75` or `0.5`), and turn on the FPS counter in MENU's tray to see the difference.
- **Leave the key bindings at their defaults** (*Setup > Controls*). The touch buttons press the game's default keys, so if you rebind an action, its touch button stops working.

### Mysteries of the Sith: the OpenMoTS app

**Star Wars Jedi Knight: Mysteries of the Sith** (1998), the expansion, plays in its own app, **OpenMoTS**: the same engine and the same touch controls as OpenJKDF2, in a second app with its own icon and its own folder in the Files app. It installs next to OpenJKDF2 and keeps its files, saves and settings apart from Jedi Knight's (SENS, GYRO, FPS and CAMERA too). It only needs Mysteries of the Sith's own files.

- **Installing:** download `OpenJKDF2-MotS-iOS-unsigned.ipa` and install it the same way as OpenJKDF2, with the same tool (see *Install* below). With a free Apple ID, it counts as one more of your 3 sideloaded apps. If your tool says you've reached the limit, don't delete OpenJKDF2 to make room: that deletes its game files and saves too. Deactivate or remove a different app instead.
- **Its files:** open OpenMoTS once, so it makes its folder, then copy the `Episode`, `Resource` and `MUSIC` folders from your Mysteries of the Sith folder into *On My iPhone (or On My iPad) > OpenMoTS > mots*. Like OpenJKDF2, it also finds them straight in the *OpenMoTS* folder, or in a folder inside it, but `mots` is the place the guide uses.
- **Keep the two games apart:** Mysteries of the Sith's files go in OpenMoTS only, and Jedi Knight's in OpenJKDF2 only. Each app only starts with its own game's files.
- **Expansions & Mods** in OpenMoTS has no entry for Jedi Knight: open the OpenJKDF2 app instead.
- The setup guide has the steps, with a picture of the folders: https://github.com/sithewok13-dev/OpenJKDF2-iOS/blob/HEAD/docs/ios/README.md#mysteries-of-the-sith

**The same as in Jedi Knight:** the move stick and its run marker, drag to look, every button, MENU's hidden tray, the camera (selfies, photos, bursts, Action Freeze and the cinematic burst), SABER and its saber wheel (for Mara's saber), cheat mode and the CHEAT button, tilt aiming, QUICK SAVE and QUICK LOAD, the LIGHT, IR and BACTA item buttons, the FORCE button with its Force meter ring, tap, swipe or long-press on the wheel buttons, the stars on the Force wheel, and the HUD that fits your screen.

**Different in Mysteries of the Sith:**

- **The Force wheel** has the 17 Force powers you can use in Mysteries of the Sith, sorted into its four tiers (the four columns of its Force screen) instead of light and dark side. Defense is for multiplayer only, so it isn't on the wheel. The gap at the bottom means *cancel*, as in Jedi Knight.
  - **Left side, gold: tier I.** From the top: Jump, Projection, Seeing, Speed, Push.
  - **Top left, green: tier II.** From the left: Pull, Grip, Far Sight, Saber Throw.
  - **Top right, blue: tier III.** From the top: Chain Lightning, Healing, Blinding, Persuasion.
  - **Right side, purple: tier IV.** From the top: Absorb, Destruction, Protection, Deadly Sight.
- **The weapon wheel** has its 17 weapons, in the order NEXT WPN goes through them, clockwise from the bottom left: Fists, Lightsaber, Bryar, Blastech, Rifle, Scope Rifle, Thermal, Flash Bomb, Bowcaster, Repeater, Rail Det, Rail Seeker, Sequencer, Manual Seq, Conc Rifle, E-Web, Carbo Gun. The E-Web and the Carbo Gun are purple; the others are coloured by their ammo, as in Jedi Knight.
- **Cheats** go in the typing line as in Jedi Knight (see *Cheats* above). Mysteries of the Sith's own cheats work: `diediedie` (weapons), `gimmestuff` (items), `morelife` (health and shields), `trixie` (Force meter), `cartograph` (map), `iamagod` (every Force power), `trainme` (Force level-up), `boinga on` / `boinga off` (invincibility), `freebird` (fly) and `gameover` (finishes the level). The Jedi Knight names work here too, except `imayoda` and `sithlord`, which do nothing in Mysteries of the Sith.

**`sithewok`** works in Mysteries of the Sith too, with everything it does in Jedi Knight: all weapons and ammo, all items, every Force power at 4 stars with the top Jedi rank, full health and shields, invincibility, the endless Force, safe falls, the Force catching you in pits, flying, noclip, and picking a level with DUCK (13 levels). Cheat mode and the CHEAT button work the same way. The catches in Mysteries of the Sith:

- **Your Force powers stay through the Force screen at the end of each level.** Normally Mysteries of the Sith takes back powers beyond what your rank allows. This lasts until the app is closed, in any game you play (also after you switch `sithewok` off). If you load a game saved with these powers after reopening the app, switch `sithewok` on again before the level ends, or the Force screen turns them back into stars.
- While you have them, the Force screen's *Choose* numbers and left-over stars don't matter: just tap *Ok*.
- When the story switches to Mara Jade (level 5), the game gives her her own starting powers, rank and weapons, and `sithewok` puts everything back once her level has started. If anything's missing, hold CHEAT to switch it off and on again.
- After the opening video of a level, your weapon in hand comes back a moment later than in Jedi Knight.

### Known limitations

- **What's new in v0.6 has been tested mostly on a computer**, with the real game engine and simulated touches: the saber colours and two sabers, cheat mode and its holds (fly, noclip, picking a level), Action Freeze and the cinematic burst, and the new selfies. Some of it has been tried on an iPhone, but not all of it, and not on an iPad. **If something looks or works wrong, please report it** (see *Found a bug?*).
- **Two sabers use Kyle's one-saber moves:** there are no new animations, so in his two-handed swings the blades come from nearly the same place, and the resting stances are in first person only.
- **Action Freeze close to walls:** the camera keeps out of walls and closed doors, but very close to one the view behind it can come out black.
- **The selfie camera** has been tried on an iPhone (lots of selfies and photos in Jedi Knight), and tested on a computer with the real game engine, with each selfie checked to put the whole level back exactly as it was. Selfies in Mysteries of the Sith have only been tested on a computer. With low-detail 1990s models, some poses can look a little odd up close. **If a selfie looks strange, or the game doesn't carry on as before after one, please report it** (see *Found a bug?*).
- **OpenMoTS (Mysteries of the Sith) is new in v0.4, and has had much less testing than OpenJKDF2.** On an iPhone, it has only been checked to start and run with the files in its folder so far. It hasn't been played through on one, and it hasn't been tried on an iPad.
- **OpenMoTS on a computer:** it has been tested with the real game engine, the Mysteries of the Sith files and simulated touches: the menus, the first level, the HUD, the main buttons, both wheels, the cheats and `sithewok`, quick save and quick load, the item buttons, finishing a level and starting the next, and Mara Jade's first level. Tilt aiming and the sound haven't been checked in OpenMoTS yet.
- **Finding the files outside `jk1` and `mots` is new in v0.4**, and has mostly been tested on a computer. If an app doesn't find your files, put them in `jk1` (or `mots`) as the guide shows.
- **In OpenJKDF2, please ignore *Install Mysteries of the Sith*** under *Expansions & Mods*: Mysteries of the Sith plays in OpenMoTS. If you tapped it, close the app and open it again.
- **No multiplayer.** The iOS build has no networking.
- **The touch buttons only know the default key bindings** (see above).
- **No previous-weapon or previous-power button:** use the wheels. There's no button for the in-game map overlay (MENU > Map shows the map instead), and no button for items other than the field light, IR goggles and bacta.
- **The run circle only runs straight ahead.** Turn on *Always Run* to run in every direction.
- **No separate touch look-sensitivity setting** for dragging. The game's mouse *Sensitivity* (*Setup > Controls > Mouse*) should change it, but that hasn't been tested yet. Tilt aiming has SENS.
- **Game controllers and hardware keyboards** haven't been tested with either app.
- **Free Apple ID signing expires every 7 days**: refresh the apps in AltStore or SideStore, or install them again with the same tool and Apple ID. Your files and saves are kept.
- OpenJKDF2 has been played on an iPhone in earlier versions, but both apps have had only limited testing so far.

### Requirements

- iPhone or iPad on **iOS / iPadOS 18 or newer** (iPhone XS / XR or newer, including the iPhone SE 2nd generation and later), for both apps. OpenMoTS has only been tried on an iPhone so far.
- Your own game files, at least `episode` and `resource`, ideally with `resource/video` (cutscenes) and `MUSIC` (soundtrack): Jedi Knight's for OpenJKDF2, and Mysteries of the Sith's for OpenMoTS. Mysteries of the Sith's files take about 450 MB in the copy it was tested with (under 200 MB without the cutscenes and music).
- A sideloading tool that can install your own `.ipa` (for example AltStore, SideStore or Sideloadly). The same tool works for both apps. With a free Apple ID, apps have to be re-signed every 7 days, and you can have only 3 sideloaded apps at a time: AltStore or SideStore itself counts as one, and OpenJKDF2 and OpenMoTS count as one each.

### Install

1. Download the `.ipa` for your game, below: **`OpenJKDF2-iOS-unsigned.ipa`** for Jedi Knight, or **`OpenJKDF2-MotS-iOS-unsigned.ipa`** for Mysteries of the Sith (*MotS* in its name). To play both, download both and do each step for each app.
2. Sign and install it with your sideloading tool. The first time, iOS may ask you to turn on **Developer Mode** and to **trust** your Apple ID. On your home screen the app is called **OpenJKDF2** (Jedi Knight) or **OpenMoTS** (Mysteries of the Sith), each with its own icon.
3. **Open the app once.** It says the game files are missing (*OpenJKDF2 is missing the following required assets*, or *OpenMoTS is missing...*) and asks you to copy your installation into the app's Documents folder. That's expected: it has just made its folder. The app can't go any further without the files, so close it (swipe it away).
4. **Get your `Episode`, `Resource` and `MUSIC` folders onto your phone** from the game's folder on your computer, for example with iCloud Drive or as a `.zip`. Then, in the Files app, copy them into *On My iPhone (or On My iPad) > OpenJKDF2 > jk1* for Jedi Knight, or *On My iPhone (or On My iPad) > OpenMoTS > mots* for Mysteries of the Sith. (The apps also find them straight in *OpenJKDF2* or *OpenMoTS*, or in a folder inside it, but `jk1` and `mots` are the places the guide uses.) Where the game folder is on your computer: https://github.com/sithewok13-dev/OpenJKDF2-iOS/blob/HEAD/docs/ios/README.md#1-get-your-game-files-ready
5. **Open the app again.** The intro plays, then you make a player, then the main menu.

The setup guide covers each step, including Developer Mode and "Untrusted Developer": https://github.com/sithewok13-dev/OpenJKDF2-iOS/blob/HEAD/docs/ios/README.md#2-install-the-app

**Updating from an older version:** sideload the new `.ipa` with the same tool and Apple ID. It replaces the app and keeps your game files and saves, and files in `jk1` (or `mots`) stay where they are. (With a different tool or Apple ID, iOS may install it as a separate app with an empty folder: the guide explains how to move your files over.) **Tried a test build** from the *iOS latest build* prerelease? Install this release over it the same way: it keeps its files and saves too.

**Your saves** are in the Files app: *OpenJKDF2 > jk1 > player*, or *OpenMoTS > mots > player*. (If you put the game files somewhere else in the app's folder, the `player` folder is next to them.) Copy it somewhere safe now and then, and always before you delete an app: deleting an app deletes its folder, game files and saves included.

**Download:** under *Assets* below (not the *Source code* files): `OpenJKDF2-iOS-unsigned.ipa` is Jedi Knight (OpenJKDF2), and `OpenJKDF2-MotS-iOS-unsigned.ipa` is Mysteries of the Sith (OpenMoTS). The *iOS latest build* prerelease, also on the Releases page, is rebuilt automatically after every code change and may be broken: use this release.

**Found a bug?** Please report it here, not to the upstream OpenJKDF2 project, and say you're on **v0.6** and which app, OpenJKDF2 or OpenMoTS (AltStore and Settings show 0.9.9 for both: that's the engine's version): https://github.com/sithewok13-dev/OpenJKDF2-iOS/issues

---

Unofficial builds. OpenJKDF2 is by shinyquagsire23 (Max Thomas) and the OpenJKDF2 contributors. This is not an official OpenJKDF2 release, and it is not affiliated with or endorsed by Lucasfilm, Disney or LucasArts. Star Wars and Jedi Knight are trademarks of Lucasfilm Ltd. No game assets are included.

The apps include open-source libraries under their own licenses, among them OpenAL Soft and libsmacker (GNU LGPL; OpenAL Soft is linked statically, and the full source and build scripts are in this repository), SDL3 and SDL_mixer (zlib) and ANGLE (BSD-style). The list with links to each license is in the guide: https://github.com/sithewok13-dev/OpenJKDF2-iOS/blob/HEAD/docs/ios/README.md#credits-and-legal
