# Pilot guide

Step by step, from no bot at all to friends playing in a Telegram group,
followed by the device tests that close the remaining unverified items in
`COMPATIBILITY.md`. Budget about an hour for setup and an evening for the
device session.

## 0. Before you start

You need:
- A Telegram account. You will own the bot.
- A running server with a public **https** address. Follow `OPERATIONS.md`
  §2. Either option works:
  - a VPS with a domain;
  - a home machine with Cloudflare Tunnel.
- A private Telegram group with the friends who will test, or a new test
  group.
- Lawful game files. Start with the project's own test cabinet:
  - `native/testroms/build/atc-versus.nes`: 2-player fighter-style test
  - `atc-coop.nes`: up to 4 players, co-op
  - `atc-turns.nes`: hot-seat turns
  - `atc-solo.nes`: single player

  These files have verified adapters, so results and rotation work
  automatically. Add your own arcade sets (`.zip`) and NES games afterwards.
  Neo Geo sets also need your `neogeo.zip`.

## 1. Create the bot (BotFather)

In Telegram, open **@BotFather**:

1. `/newbot`. Choose a display name, for example "Friends Arcade", and a
   username ending in `bot`.
   - BotFather replies with the **token**. Treat it like a password.
   - Put it in `deploy/.env` as `BOT_TOKEN=...`.
   - Put the username, without `@`, as `BOT_USERNAME=...`.
2. `/setjoingroups` → choose the bot → **Enable**.
3. Leave `/setprivacy` as it is (enabled). The bot will be a group admin,
   and admins receive all group messages anyway.
4. `/newapp` → choose the bot. BotFather asks for:
   - a title, for example "Arcade";
   - a short description;
   - a 640×360 photo;
   - an optional GIF (send `/empty` to skip);
   - the **Web App URL**: exactly your `PUBLIC_URL`, for example
     `https://arcade.example.com`;
   - a **short name**: `arcade`. It must match `MINIAPP_SHORT_NAME`.
5. Restart the server so it picks up the settings:
   `docker compose -f deploy/docker-compose.yml up -d`. The log should show
   the bot starting with no errors. It checks that `BOT_USERNAME` matches the
   token.

You do **not** need `/setdomain`, and you do not need a menu button.

## 2. Add the bot to the group

1. Open a private chat with your bot and send `/start`. Tap **➕ Add to a
   group** and pick the group. Telegram asks to make the bot an admin with
   *Manage chat* only.
2. **Supergroups:** in the admin rights editor, switch every other right
   **off**. The bot never deletes, bans, pins or invites. It only needs admin
   status to see uploaded files and to know when someone leaves.
3. **Basic groups:** admin is all or nothing there. Making a bot an admin
   may upgrade the group to a supergroup, which gives the group a new chat
   id. The arcade keeps the same room, shelf and records across that change.
4. The bot posts a welcome card with an **🎮 Open arcade** button. You can
   bring that button back at any time with `/arcade`.
5. If you are a group admin, send `/hostme` to become the arcade host. The
   host appoints deputies, changes room rules and has the final say. Group
   admins act as deputies, and the group owner always has host powers.

Optional: set `ALLOWED_CHAT_IDS` so the bot refuses to serve any other group.
To list the groups the bot has joined, with their chat ids:

```sh
docker compose -f deploy/docker-compose.yml exec arcade node --no-warnings -e \
  "const {DatabaseSync}=require('node:sqlite');console.table(new DatabaseSync('/data/arcade.sqlite',{readOnly:true}).prepare('SELECT chat_id, title, status FROM groups').all())"
```

## 3. First game

1. Put a game on the shelf in one of two ways:
   - post `atc-versus.nes` in the group as a file;
   - tap **Open arcade**, go to **Shelf**, then **Upload from device**.

   The bot answers with a card once the file is checked.
2. Tap **🎮 Open arcade**.
   - On Telegram Desktop, confirm the "Open app?" prompt. It appears every
     time for bots without Telegram verification.
   - In the arcade: **Shelf** → **Start** on the game.
3. On the **Arcade** tab, tap the screen once ("Tap to play" / "Tap to
   watch"). This enables sound and lets iPhones see controllers.
4. **Take a seat** or **Join queue**, then **Insert coin** (key 5) and press
   Start. Everyone else in the group can open the same button to watch,
   chat and queue.

What to expect:
- **Versus games:** the winner stays and the next person in the queue
  challenges.
- **Co-op games:** a player who reaches game over gives their seat to the
  next person, but only if someone is waiting.
- **Changing games:** use the **Vote** tab. The host or a deputy can override
  a vote, giving a reason.

## 4. Device test session (closes the ⏳ items)

Do this once with 3–4 people on different devices and networks. For each
row, note pass/fail plus the numbers shown on the status line under the game
(fps, input→screen, net, emulation ms/frame), and screenshot anything odd.
Then update `COMPATIBILITY.md` with what you saw.

| # | Test | Devices |
|---|---|---|
| 1 | Tap **Open arcade** in the group. The app opens over the group and shows the group's name. | iPhone, Android, Desktop (Win/mac/Linux), Telegram Web |
| 2 | Forward the arcade button to someone **not** in the group. They see "Only its members can open it." | any |
| 3 | Two players on two phones, on different networks (one on mobile data), play `atc-versus` for 3 matches. The winner stays and the loser goes to the back of the queue. Results appear under **Records**. | iPhone + Android |
| 4 | A third person watches. Their view matches the players' (a slight delay is normal). They chat. | any |
| 5 | Touch controls: move, jump and press two buttons at once. Use ✥ to move and resize controls. Check that the left edge is still usable for Telegram's back swipe on iPhone. | iPhone, Android |
| 6 | Game controller (Bluetooth Xbox / PlayStation / 8BitDo). Pair it, open the arcade, tap the screen once, then press buttons. Check **Controls → Controller test**. | each platform |
| 7 | Sound: on iPhone with the **silent switch on**, sound still plays after the first tap. Volume and mute work. | iPhone, Android, Desktop |
| 8 | Fullscreen and rotation: fullscreen during play, rotate to landscape, return. Swipe-down does not close the app while playing. | iPhone, Android |
| 9 | Background the app or lock the phone for 20 s during play, then return. Held buttons are released. You are still seated if back within the grace period (30 s by default). Otherwise the seat passes on. | iPhone, Android |
| 10 | Switch Wi-Fi to mobile data during play. The app reconnects and stays in sync (the resync count on the status line may go up by one). | phone |
| 11 | The host closes Telegram entirely. The game keeps running for everyone. After 60 s a deputy, group admin or volunteer is offered the acting-host role. | any |
| 12 | Vote to switch to `atc-coop`. The switch waits for the current match to end. Then 3–4 players play co-op. When one player loses all lives, a waiting person gets the seat. | 4 devices |
| 13 | `atc-turns`: one controller is passed around in turn order. | 2–3 devices |
| 14 | Upload a game over 20 MB through the Mini App's **Upload from device**. | any |
| 15 | Your own arcade romset (for example a CPS-2 zip): upload, the shelf shows "ready", play 2 players. Note the "emulation ms/frame" on the slowest phone. | any |
| 16 | **Controls → Game files on this device → Remove**. The next game start downloads the files again. | any |
| 17 | **Controls → Open in browser**. The link opens the arcade in Safari/Chrome as you, once. | any |
| 18 | Restart the server (`docker compose restart arcade`) during a game. Everyone reconnects, then **Shelf → Resume** continues from the save point. | server + any |

Things to report back, which feed the next round of work:
- Any device where the status line shows fps under 58, or "emulation" above
  about 12 ms/frame.
- Any desync or resync count above 0 that keeps rising.
- Controller models that were not detected, and whether "Open in browser"
  fixed it.
- The input→screen numbers players found acceptable or not, for fighters
  especially.

## 5. Ending the pilot or pausing it

- To pause: `docker compose -f deploy/docker-compose.yml stop`. Rooms are
  checkpointed.
- To leave a group: remove the bot from the group. Sessions are revoked at
  once, and the shelf and records are kept in case you re-add the bot.
- Backups: see `OPERATIONS.md` §4. Do a restore drill once before relying on
  them.
