# OpenJKDF2 for computers: test builds

Test builds of this fork for **Windows**, **Mac** (Apple silicon) and
**Linux**. They play Jedi Knight: Dark Forces II and Mysteries of the Sith,
with the extras from the iPhone app (two sabers, saber colours, Force Jump
styles) and the same multiplayer, so a computer and an iPhone can play
together.

**These are experiments.** They are built automatically and have not been
tried on a real Windows PC or Mac yet. Only the Linux build has been started
here (it loads a level). Please report what happens.

No game files are included. You need your own copy of the game (GOG or
Steam).

## Download

1. Open the repository on GitHub, then **Actions**, then **Desktop builds
   (experiment)**.
2. Open the newest run with a green tick.
3. Under **Artifacts**, download the one for your computer (you need to be
   signed in to GitHub):
   - `OpenJKDF2-windows-x86_64` (Windows 10 or 11, 64-bit)
   - `OpenJKDF2-macos-arm64` (a Mac with Apple silicon: M1 or newer)
   - `OpenJKDF2-linux-x86_64` (64-bit Linux, for example Ubuntu 24.04)
4. GitHub wraps each one in a zip. Unzip it. Inside is the game's own zip
   (or `.tar.gz` on Linux); unzip that too.

There is no build for Intel Macs yet.

## Put the game files in

The game looks for its files **in the same folder as the program**. Copy
these folders from your game into the OpenJKDF2 folder you unzipped:

- Jedi Knight: `Episode`, `Resource` and `MUSIC` (from the GOG or Steam
  install folder, for example `C:\GOG Games\Star Wars Jedi Knight` or
  Steam's `steamapps/common/Star Wars Jedi Knight`).
- Mysteries of the Sith: its own `Episode`, `Resource` and `MUSIC`.

The two games can't share one folder. For both, make two copies of the
OpenJKDF2 folder, one per game, and start Mysteries of the Sith with its
own launcher (below).

**Jedi Knight only:** if the game doesn't find its files, it offers to copy
them: say **Yes** and pick your Jedi Knight install folder. It then copies
them to its own folder (Windows: `%APPDATA%\OpenJKDF2\openjkdf2`, Mac:
`~/Library/Application Support/OpenJKDF2/openjkdf2`, Linux:
`~/.local/share/OpenJKDF2/openjkdf2`). **Don't use that offer for Mysteries
of the Sith:** it would copy the wrong files and leave the folder unusable.
Copy Mysteries of the Sith's folders next to the program by hand instead.

## Start it

### Windows

Double-click `openjkdf2-64.exe`. Windows may say **"Windows protected your
PC"** (SmartScreen), because the program isn't signed. Click **More info**,
then **Run anyway**. Keep `OpenAL32.dll` next to the program (it's the
sound).

For Mysteries of the Sith, double-click `Mysteries of the Sith.bat` instead
(in the folder with the Mysteries of the Sith files).

### Mac

Put the game folders next to `OpenJKDF2.app` (in the same folder). The app
isn't signed by Apple, so the first time:

1. Right-click (or Control-click) `OpenJKDF2.app` and choose **Open**, then
   **Open** again. (On macOS 15 and newer, if there's no Open button: try to
   open it once, then go to **System Settings > Privacy & Security** and
   click **Open Anyway**.)
2. If it still won't start, or doesn't find the game files next to it, open
   **Terminal** and type this (with the path to your folder), then press
   Return:

   ```
   xattr -dr com.apple.quarantine ~/Downloads/OpenJKDF2-macos-arm64
   ```

   (A downloaded app is run from a hidden copy until that mark is removed,
   and then it can't see the files next to it.)

For Mysteries of the Sith there's no separate app yet. Quit the game if it's
running, open **Terminal**, go to the folder with the Mysteries of the Sith
files (with its own copy of `OpenJKDF2.app`) and start it like this:

```
cd ~/Downloads/OpenMoTS
open -n OpenJKDF2.app --args -motsCompat
```

(`-n` starts a fresh copy, so the Mysteries of the Sith setting isn't lost if
another copy of the game is already open.)

### Linux

Open a terminal in the folder and type `./openjkdf2.sh` (or `./openmots.sh`
for Mysteries of the Sith). Both start the game from their own folder, which
is where it looks for the game files. Double-clicking them only works if your
file manager runs scripts; in Ubuntu's Files, right-click the script and
choose **Run as a Program** instead (a plain double-click opens it as text).
It needs these libraries, which most desktops already have (on Ubuntu or
Debian): `sudo apt install libopenal1 libgtk-3-0 libcurl4 libssl3 libavahi-compat-libdnssd1`.
`libraries.txt` in the folder lists everything it was built against.

Linux file names are case-sensitive. The folders as GOG and Steam name them
work.

## The extras and their keys

The keys are the game's own, as in the original (change them in **Setup >
Controls**). The extras from the iPhone app are there too, but most have no
key on a computer yet; they are set by typing in the game's console.

- **First or third person:** **F1** switches the view. It is remembered.
- **The console:** press the key under Esc (**`** or **~**) to open it, type
  a line and press Enter. Press it again to close. **T** (the typing line)
  also takes these lines, and cheats.
- **Two sabers:** type `enh_saberStyle DUAL` (or `SINGLE` for one).
- **Saber colours:** `enh_saberColourSingle red`, or for two sabers
  `enh_saberColourDualRight blue` and `enh_saberColourDualLeft green`.
  Colours: `game` (the game's own), `red`, `crimson`, `orange`, `amber`,
  `yellow`, `lime`, `green`, `teal`, `cyan`, `lightblue`, `blue`, `purple`,
  `magenta`, `pink`, `white`, `black`.
- **Force Jump:** `enh_forceJumpStyle 2` is the classic jump (the default on
  computers), `0` the smooth one and `1` the stepped one from the iPhone app.
- **Third person:** `enh_thirdPerson 1` (or `0`), the same as F1.
- Typing just the name (for example `enh_saberStyle`) shows what it's set
  to. The choices are saved in `openjkdf2_cvars.json` in the game folder.
- With two sabers you get the resting stances, and in Mysteries of the Sith
  Saber Throw throws both, one after the other.
- **Cheats:** type them on the typing line (**T**), as in the original.
- **In a multiplayer game** the console and **T** only send chat, so
  these settings and cheats can't be typed there. Set them before you host
  or join.

### Photos

Photos are saved as PNG files in your **Pictures** folder, in
**OpenJKDF2** (Windows: `Pictures\OpenJKDF2`; Mac and Linux:
`~/Pictures/OpenJKDF2`). If the system has no Pictures folder set up, they
go in a `photos` folder next to the game instead.

But there is **no key for the camera on a computer yet**: the photo,
selfie, Action Freeze and cinematic burst are started from the iPhone's
touch buttons only. The part that saves the pictures is in, so they'll go
there once a key is added.

## Multiplayer

The same multiplayer as the iPhone app (up to four players, deathmatch or
co-op), so computers and iPhones can play together. Everyone needs the same
game files, and the same game (Jedi Knight or Mysteries of the Sith).

- **Host:** **Multiplayer > Host Game**, pick the type and level, **OK**.
  The top line shows your address (for example `192.168.1.20`).
- **Join:** **Multiplayer > Join Game**, type the host's address in
  **Address** and click **Join**.
- **Games nearby** (same Wi-Fi or network) are listed by themselves:
  - **Mac:** yes.
  - **Linux:** yes, if Avahi is running (it is on most desktops:
    `avahi-daemon`).
  - **Windows:** no. Type the host's address.
- The game uses UDP port **27020**. If a firewall asks, allow it (Windows
  asks the first time you host).

### Games hosted by the official OpenJKDF2

These builds can also **join** games hosted by the official OpenJKDF2
(the original project's own releases). Use **Join Game** and type the
host's address, as above. It works for deathmatch-style games only, not
co-op: the game says so if you try to join an official co-op game.

It doesn't work the other way: a game **hosted** by these builds (or the
iPhone app) can't be joined by official OpenJKDF2 players. Only these
builds and the iPhone app can join it.

## What hasn't been tested

- **Windows and Mac:** not run at all yet. They are built and packaged
  automatically, but nobody has started them.
- **Linux:** started here on a test machine (no real screen or sound): it
  loads a level. Not tried on a real desktop, with sound, or with a
  controller.
- **Joining official OpenJKDF2 games:** tested only on Linux, on one
  test machine. Not on Windows or Mac.
- **Multiplayer** between a computer and an iPhone, and between two
  computers on a real network.
- **Games nearby** on Mac and Linux.
- **Mysteries of the Sith** on a computer.

Report problems on this repository's issue tracker, and say which computer
and system you used.
