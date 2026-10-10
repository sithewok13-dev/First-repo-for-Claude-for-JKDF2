# OpenJKDF2 for iOS: notes for Claude Code sessions

An unofficial iPhone/iPad build of OpenJKDF2 (Jedi Knight: Dark Forces II), with
touch controls. The owner plays it on an iPhone, isn't a programmer, and reads
everything on a phone.

- **Build branch:** `claude/openjkdf2-ios-build-2gcr9s` (the default branch).
- **iOS code:** `src/Platform/iOS/` (`iosTouch.m` overlay, `iosGame.c` engine
  helpers, `iosSafeArea.c` HUD margins). Cheats are in `src/Main/jkDev.c`.
- **Docs:** `docs/ios/README.md` (setup guide), `docs/ios/RELEASE_NOTES.md` (the
  release text). **CI:** `.github/workflows/ios.yml`.
- **Mysteries of the Sith** is a separate app, **OpenMoTS**
  (`packaging/ios/make_mots_app.sh`), with its own bundle id and Files folder.

**First, read the house style guide** in the owner's private files repo
(`docs/HOUSE_STYLE.md`), if you have access. The rules below apply even without it.

## Rules

- **Pushing publishes.** Any branch push (except docs-only changes) updates the
  public `ios-latest` prerelease. Push only reviewed work, when the owner wants a
  test build. Back up unreviewed work as a patch in the owner's private files repo
  (that also answers the uncommitted-changes stop hook).
- **Releases:**
  - `vX.Y` tags, made by a manual run of `ios.yml` on the build branch with input
    `release=<tag>` (`gh workflow run ios.yml --ref <build branch> -f release=<tag>`).
    A pushed `v*` tag also makes a release, so never push tags by hand. The notes
    need the exact line `### What's in <tag>`, one line per paragraph or list item,
    and absolute links. Commit and push them only after the owner's go.
  - **Before any public release or public text,** build a preview (phone and desktop
    widths) and wait for the owner's explicit go, such as "Publish v0.4".
  - After publishing, download the IPA and verify: bundle id, no game data, the new
    code present, Latest marked, the tag on the built commit.
- **Game data:** never commit, upload, bundle or publish game files (installers,
  `.goo`/`.gob` files, extracted folders, zips), here or anywhere public. Work on
  local copies only.
- **Commits:**
  - Identity `Claude <noreply@anthropic.com>` (already configured); never the
    owner's email or name. Subject `iOS: ...`, then a plain wrapped body.
  - End with the attribution trailer lines your session provides. Apart from those,
    no AI model names in commits, code or docs.
  - No PRs unless asked. Never stash, reset or check out over a working tree that
    another job may be editing.
- **Touch controls standard** (shared with the owner's other ports; keep it the same):
  - Move stick where the left thumb lands; drag elsewhere to look.
  - QUICK SAVE / QUICK LOAD: a 0.3 s hold with a ring. Sliding off cancels; QUICK
    SAVE wins a tie.
  - MENU: tap = game menu; hold 0.45 s = tray with SENS, GYRO, FPS, keyboard.
  - GYRO: OFF (default) / TOUCH (aims while either thumb is down; lifting both
    freezes the view) / ALWAYS. SENS 1.0/1.5/2.0/3.0, default 1.5. Remembered.
  - FORCE WHEEL and NEXT WPN: tap = next; a 10 pt slide opens the wheel at once; a
    0.3 s hold opens it to tap. The gap at the bottom cancels. The game holds still
    while a wheel is open. A haptic tick per slice. 4 gold stars per power level.
  - HUD and buttons stay clear of the corners, the notch or Dynamic Island, and the
    home bar.
- **Testing:** a real-engine Linux rig with the real data (local only) and the real
  overlay (scripted touches, mock UIKit), plus compile checks. Then independent
  reviews and a fix pass. The owner tests on the device. Say plainly what was and
  wasn't tested.
- **Lean mode:** small contexts, no full log or file dumps (use grep/sed/head/tail),
  reuse builds, one full final run.
- **Talking to the owner:** plain words, short messages, direct links, and honesty
  about mistakes and untested things. If they skip a question, choose a sensible
  default and say what you chose.
