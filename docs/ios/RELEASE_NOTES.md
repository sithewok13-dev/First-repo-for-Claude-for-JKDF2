<!--
  The GitHub release text for a pushed v* tag: .github/workflows/ios.yml
  publishes this file as the release body (body_path), as it is in the tagged
  commit. Update it before tagging each release: the run fails before building
  unless this file has a "### What's in <tag>" heading line for the pushed tag
  (for example "### What's in v0.2"). Links must be absolute: relative links
  don't resolve on a release page.
-->

The first release of **OpenJKDF2 for iOS**: an unofficial iPhone and iPad build of
[OpenJKDF2](https://github.com/shinyquagsire23/OpenJKDF2), the open-source
Jedi Knight: Dark Forces II engine, with on-screen touch controls.
It's **experimental**, so expect bugs.

**You need your own copy of Star Wars Jedi Knight: Dark Forces II** (GOG or
Steam). No game files are included.

**Setup guide:** https://github.com/sithewok13-dev/First-repo-for-Claude-for-JKDF2/blob/HEAD/docs/ios/README.md

### What's in v0.1

- Jedi Knight single player on iPhone and iPad, in landscape
- Touch controls: a move stick that appears under your left thumb (push up onto
  its **^^** marker to run), drag to look, FIRE, ALT, DUCK, ACT and JUMP, and a
  FORCE button with a Force-meter ring around it
- A Force wheel: slide from FORCE WHEEL towards a power and lift to pick it,
  or tap to open it and tap a power. The game pauses while it's open.
- LIGHT, IR and BACTA buttons that appear once you have those items, plus NEXT WPN
- QUICK SAVE (hold for about a third of a second) and QUICK LOAD (hold for a second), so a stray
  touch can't save over or throw away your progress
- MENU: tap it for the game menu, or hold it for a tray with a keyboard (for typing
  cheats) and an FPS counter
- A HUD that fits rounded, notched screens: the gauges sit above the home bar, the health and
  shield numbers are bigger, and the HUD scale is 2.5 by default
- Tap to skip cutscenes, and tap your way through menus
- Fixes so that sound effects and music both play
- Based on OpenJKDF2 0.9.9 (upstream as of September 2026)

### Requirements

- iPhone or iPad on **iOS / iPadOS 18 or newer** (iPhone XS / XR or newer)
- Your own Jedi Knight game files, at least `episode` and `resource`, ideally
  with `resource/video` (cutscenes) and `MUSIC` (soundtrack)
- A sideloading tool that can install your own `.ipa` (for example AltStore,
  SideStore or Sideloadly). With a free Apple ID, the app has to be re-signed
  every 7 days.

### Install

1. Download **`OpenJKDF2-iOS-unsigned.ipa`** below.
2. Sign and install it with your sideloading tool.
3. Open the app once, then use the Files app to copy your `Episode`, `Resource` and
   `MUSIC` folders into *On My iPhone > OpenJKDF2 > jk1*.

The guide covers each step, all the controls, cheats and troubleshooting.

### Known limitations

- Mysteries of the Sith hasn't been tested and isn't supported (please ignore
  *Install Mysteries of the Sith* under *Expansions & Mods* for now), and
  there's no multiplayer.
- The touch buttons press the game's default keys: leave the key bindings in
  *Setup > Controls* at their defaults.
- It has only had limited testing so far.

Found a bug? Please report it here, not to the upstream OpenJKDF2 project:
https://github.com/sithewok13-dev/First-repo-for-Claude-for-JKDF2/issues

---

Unofficial build. OpenJKDF2 is by shinyquagsire23 (Max Thomas) and the
OpenJKDF2 contributors. This is not an official OpenJKDF2 release, and it is not
affiliated with or endorsed by Lucasfilm, Disney or LucasArts. Star Wars and
Jedi Knight are trademarks of Lucasfilm Ltd. No game assets are included.

The app includes open-source libraries under their own licenses, among them
OpenAL Soft and libsmacker (GNU LGPL; OpenAL Soft is linked statically, and the
full source and build scripts are in this repository), SDL3 and SDL_mixer
(zlib) and ANGLE (BSD-style). The list with links to each license is in the
guide: https://github.com/sithewok13-dev/First-repo-for-Claude-for-JKDF2/blob/HEAD/docs/ios/README.md#credits-and-legal
