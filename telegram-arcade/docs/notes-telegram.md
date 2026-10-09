# Telegram integration notes

How the bot (`server/telegram/`) talks to Telegram, how to set it up, which
permissions it needs, and what is verified versus still to be tested on real
clients. Sources are in `docs/research/telegram-miniapp-launch.md`
and `telegram-bot-files-groups.md` (Bot API 10.3, August 2026). Those reports
include an adversarial verification pass.

## 1. How it fits together

| Piece | File | Job |
|---|---|---|
| Bot API client | `api.ts` | JSON calls over `fetch` with timeouts, `TelegramError` (error_code, description, retry_after, migrate_to_chat_id), bounded 429 retries, file download with a hard size limit |
| Membership | `membership.ts` | `getChatMember` checks, a TTL cache in `memberships`, fail-closed behaviour, revocation listeners; `DevMembership` for tests and dev scripts |
| Bot | `bot.ts` | Updates (polling or webhook), group lifecycle, uploads to the shelf, commands, lobby card, announcements |
| Outbox | `outbox.ts` | Per-group send queue: pacing, 429 pauses, coalescing, migration-aware, blocks `web_app` buttons |
| Migration | `migration.ts` | Group to supergroup move. The internal group id and room token are kept. |
| Texts | `texts.ts` | Every message the bot posts, HTML-escaped |

**Launching the arcade from a group.** Inline `web_app` buttons only work in
private chats. In a group the bot posts an inline **url** button with the
direct Mini App link:

```
https://t.me/<BOT_USERNAME>/<MINIAPP_SHORT_NAME>?startapp=<room token>
```

The room token has 16 base64url characters (the limit is 64). Clients open
the link over the group. The launch data carries `chat_type`,
`chat_instance` and `start_param`, but not the chat id, so a launch never
proves membership. The server maps `start_param` to a group and then calls
`getChatMember(group chat id, user id)`. A forwarded link only works for
members of that group. The outbox throws if any code tries to send a
`web_app` button to a group.

## 2. BotFather setup

1. `/newbot`: choose a name and username. Put the token in `BOT_TOKEN` and the
   username (without `@`) in `BOT_USERNAME`. At start the bot calls `getMe` and
   refuses to run if the two do not match.
2. `/setjoingroups`: **Enable**, so the bot can be added to groups.
3. `/setprivacy`: leave it **Enabled**, which is the default. The bot is made a
   group admin, and admin bots receive every group message whatever this
   setting says. Leaving privacy on means a bot that is not an admin only gets
   commands and replies. `/add`, sent as a reply to a file, still works in
   that case.
4. `/newapp`: pick the bot, give a title, description and 640×360 photo.
   - **Web App URL** = `PUBLIC_URL`. Its domain is the Mini App's domain.
     Since Bot API 10.2, Mini App methods only work from that origin, so serve
     the client from exactly this origin (no other domain, no iframes).
   - **Short name** = `MINIAPP_SHORT_NAME` (default `arcade`). The direct link
     becomes `t.me/<bot>/<short name>`.
   - To change the URL later, use `/myapps`, then *Edit Web App URL*.
5. `/setdomain` is **not** needed. It is only for the Login Widget and
   `login_url` buttons. The external-browser fallback uses our own one-time
   handoff links.
6. Commands need no BotFather step. At start the bot sets its group command
   list (`/arcade`, `/shelf`, `/add`, `/hostme`, `/help`) and its private
   `/start` with `setMyCommands`. It also suggests minimal admin rights with
   `setMyDefaultAdministratorRights`.

## 3. Adding the bot to a group: minimal permissions

The `/start` reply in a private chat has an **Add to a group** button:
`https://t.me/<bot>?startgroup=arcade&admin=manage_chat`. The `admin`
parameter asks for admin status and nothing else.

**Why admin at all.** Only admin bots are documented to:
- receive every group message, which is how the bot sees uploaded game files;
- receive `chat_member` updates, so a member who leaves loses access at once;
- get a guaranteed `getChatMember` answer for other users.

**Rights the bot needs: none beyond admin status.** In a **supergroup**,
switch every right off in the admin editor. `can_manage_chat` is implied and
cannot be removed. The bot never deletes, pins, bans, restricts, invites,
promotes, changes group info, posts stories, manages topics or video chats,
or stays anonymous. On start it logs any extra rights it holds, so an
operator can remove them.

In a **basic group** rights are not granular: admin is on or off, and only
the owner can promote. Some clients turn a basic group into a supergroup when
a bot is given non-default rights, and that gives the chat a new id. The bot
handles this (see §5).

**Without admin.** With privacy mode on, the bot only sees commands and
replies. Uploads are then invisible, but `/add` sent as a reply to the file
works. `getChatMember` for ordinary members may fail, for example in groups
with hidden members, and checks then fail closed. The welcome card says this.

**Restricting the bot to known groups.** Set `ALLOWED_CHAT_IDS` to a
comma-separated list of chat ids. In any other group the bot posts a short
note and leaves. A supergroup gets 10 s of grace first, because an allowed
basic group that upgrades shows up under a new id and its migration notice
may arrive a moment later. Every chat id a registered group ever had counts
as allowed.

## 4. Receiving updates

`UPDATES_MODE`:
- `polling` (default): `deleteWebhook`, then a `getUpdates` long poll
  (timeout 50 s). The offset is stored in `kv` under `tg_offset` after each
  update, so a restart resumes where it stopped.
- `webhook`: `setWebhook` to `PUBLIC_URL/telegram/webhook` with
  `secret_token = WEBHOOK_SECRET`, which must be 16-256 characters from
  `A-Z a-z 0-9 _ -`. The HTTP server passes the
  `X-Telegram-Bot-Api-Secret-Token` header to `handleWebhook`, which compares
  it in constant time:
  - wrong or missing secret: 401;
  - not ready: 503 (Telegram retries);
  - malformed body: 400.

  The cloud API only delivers to **https** on ports 443, 80, 88 or 8443.
- `off`: no updates. Announcements still work.

Both modes ask for `allowed_updates = message, my_chat_member, chat_member,
callback_query`. `chat_member` is only delivered when asked for, and only to
admin bots.

**Idempotency.** Each `update_id` is inserted into `telegram_updates` before
anything else happens, and a repeat is skipped. Each update is handled
inside its own `try/catch`. Slow work (sends, downloads, validation) runs in
the background, so one bad or slow update never stops the loop. Rows are
pruned after 3 days. Telegram keeps updates for 24 h, and update ids restart
at a random value after a week with no updates.

## 5. Group lifecycle

| Event | What the bot does |
|---|---|
| Bot added (`my_chat_member` member or administrator) | `groups.ensure` and admin status plus rights stored. Posts a welcome card with the launch button, which also becomes the lobby card. A group the operator set to `disabled` stays idle. |
| Promoted or demoted | Rights stored. Short confirmation, or a warning that uploads are no longer visible. |
| Bot removed or banned | Status set to `bot_removed`. Every session for the group revoked. `hooks.onGroupDisabled(groupId)` called. Queued messages and announcements dropped. Membership checks deny access. |
| Bot re-added | Status back to `active`, same room token, new welcome card. |
| `migrate_to_chat_id` / `migrate_from_chat_id`, or a send error carrying `migrate_to_chat_id` | `groups.migrate(old, new)`. Same internal id, room token, shelf and records. Idempotent. If the new supergroup registered itself first, that empty duplicate is merged into the original group and its card is edited to carry the right token. |
| `chat_member`: user left, kicked, or restricted with `is_member=false` | The loss is recorded at once and `onRevoked` fires. The bot then revokes the user's sessions and calls `hooks.onMembershipRevoked(groupId, userId)`, once. |
| `chat_member`: joined or role changed | Cache invalidated. The next access is re-verified with `getChatMember`. |
| Command from an unknown group | The bot asks Telegram for its own membership and registers the group. This covers being added while the server was offline for more than 24 h. |

## 6. Membership checks (`MembershipService`)

- Mapping: `creator` gives role creator, `administrator` gives administrator,
  and `member` gives member. `restricted` counts as a member only while
  `is_member` is true. `left` and `kicked` are not members.
- Cached in `memberships` for `MEMBERSHIP_TTL_SEC` (default 300 s). `force`
  always asks Telegram. Concurrent checks for one user share a single request.
  An answer that was invalidated while in flight is not trusted, and the bot
  asks again.
- **Fail closed.** If Telegram cannot answer (network error, 5xx, 429, or the
  bot is not in the chat), the user is not a member, unless a positive answer
  younger than the TTL is on record. "user not found" is a definite no.
- Groups whose status is not `active` give no access, and no API call is made.
- `DevMembership` takes a fixed allowlist written in code. `createMembership`
  only picks it when `DEV_MODE=1` and there is no `BOT_TOKEN`.

## 7. Uploads from the group

- **Accepted files:** extensions from `shared/systems.ts` (`.zip`, `.nes`).
  - `.7z` and `.rar` get an explanation (re-pack as `.zip`).
  - Other documents are ignored without a reply.
  - Uploads sent anonymously as the group cannot be attributed, and the bot says so.
- **Larger than `MAX_UPLOAD_BYTES`:** the reply states the limit. Nothing is
  downloaded.
- **Larger than 20 MB on the cloud Bot API:** the reply explains Telegram's
  limit. The fallback is **Upload from device** on the arcade's shelf, or
  running a local Bot API server. Nothing is downloaded. A `file is too big`
  error from `getFile` gets the same explanation.
- **Otherwise:**
  1. Downloaded to `DATA_DIR/tmp`, created exclusively with mode 0600 and
     capped while streaming.
  2. Passed to `shelf.ingest({ source: 'telegram', tg: { fileId, fileUniqueId, messageId }, … })`.
  3. At most 2 downloads or ingests run at once, with at most 10 queued per
     group and 40 in total.
- **Replies:** results come back as a compact game card (name, system,
  players, mode, status or missing BIOS and parent sets) with the launch
  button. Results that finish together are combined into one message.
- **Repeats:** a file whose `file_unique_id` already produced a live shelf
  entry in this group is not downloaded again.
- **`/add`**, sent as a reply to an earlier file, ingests that file. Telegram
  attaches the replied-to message and its document to the command. This
  recovers uploads posted while the bot could not see them. The file is
  credited to whoever posted it.

**Local Bot API server** (no 20 MB limit):
1. Run `telegram-bot-api --local` with your own `api_id` and `api_hash` from
   my.telegram.org.
2. Call `logOut` on the cloud API first. The bot cannot return to the cloud
   for 10 minutes after that.
3. Set `BOT_API_BASE=http://<server>:8081`, `BOT_API_LOCAL=1` and
   `BOT_FILE_DIR`.

How local mode behaves:
- `getFile` returns an **absolute** path on that server's disk and only
  answers once the whole file is there, so the bot gives it the long download
  timeout (10 min).
- There is no `/file/` HTTP route. The file is copied from the shared volume.
- `BOT_FILE_DIR` is the server's `--dir`, mounted at the same path. If it is
  mounted elsewhere, use `<path on the Bot API server>=<path here>`.
- Paths outside it, including through symlinks, are refused.
- After a successful copy the server's file is deleted (best effort), so ROMs
  are not stored twice. A read-only mount simply keeps the file.
- These paths contain the bot token, so they are never logged.

## 8. Group messages and rate limits

- Gameplay chat stays in the Mini App. The group only gets the welcome or
  lobby card, upload cards, command replies and announcements.
- **Outbox:**
  - at most **10 messages per group per minute** (Telegram's limit is about
    20) and one every 1.1 s per group;
  - a 429 pauses that group's queue for `retry_after`;
  - at most 30 queued per group;
  - nothing is sent to a group that is not active.
- **`announce(groupId, text, { key, delayMs })`:**
  - at most one announcement message per `ANNOUNCE_MIN_INTERVAL_SEC`
    (default 60 s) per group. This is measured from `last_announce_at`, which
    is stored in the database, so a restart does not cause a burst;
  - a pending announcement with the same key is replaced (latest wins);
  - `delayMs` (up to 10 min) holds an announcement back, so results are not
    spoiled before spectators see them;
  - everything due at the same moment goes out as one message, at most 4 lines;
  - stale entries (15 min) are dropped, with at most 5 pending per group;
  - announcements are plain text, because they contain user-provided names;
  - pending announcements live in memory and are lost on restart.
- **Commands:**
  - each has a 10 s cooldown per group (`/hostme` and `/add` per user);
  - `/arcade` edits the lobby card in place when it is among the last ~15
    messages, and otherwise posts a new one;
  - `refreshLobby(groupId)` edits the card in place (for example after a game
    change);
  - `/hostme` verifies admin or creator status with a forced `getChatMember`,
    then calls `hooks.claimHost`.

## 9. Wiring (main)

`server/main.ts` wires the bot:

| Hook | What it calls |
|---|---|
| `onGroupDisabled` | `rooms.disableGroup` |
| `onMembershipRevoked` | `rooms.revokeUser` (idempotent, so a second eviction through the gateway's own `onRevoked` listener is harmless) |
| `onMemberChanged` | a forced `membership.check`, then `rooms.updateMemberRole` (a demoted admin loses moderator powers at once) or `rooms.revokeUser` |
| `claimHost` | `rooms.claimHost(g, u, true)`; the bot has verified admin or creator status |
| `roomSummary` | the lobby card's status line |

On shutdown, `app.close()` awaits `bot.shutdown()` before the database
closes, which also aborts in-flight downloads.

Other review fixes:
- The webhook uses `max_connections: 1`, so updates arrive in order.
- Download temp files left by a crash are swept at start.

Without `BOT_TOKEN`, with `DEV_MODE=1`, `start()` logs a warning and does
nothing. `launchUrl` then returns a `PUBLIC_URL/?startapp=` link.

## 10. Verified versus to be tested

**Verified from primary sources.** Docs, spec JSON and Bot API server source,
per the research reports:
- `web_app` buttons are private-only. A url button holding a direct link
  works in groups, and clients pass the group as context.
- Direct-link initData has `chat_type`, `chat_instance` and `start_param`,
  and no chat id.
- `getChatMember` is guaranteed only for admin bots. Statuses and
  `is_member` work as described.
- `chat_member` needs admin and explicit `allowed_updates`.
  `allowed_updates` is sticky.
- Cloud `getFile` is limited to 20 MB. Local mode gives absolute paths,
  blocks until the file has downloaded, and has no `/file/` route.
- Updates are kept 24 h. Webhook delivery is at least once. The secret header
  rule and the 409 conflict between polling and a webhook are as described.
- The `migrate_to_chat_id` / `migrate_from_chat_id` fields and the error
  parameter exist.
- The 20 messages per minute group limit and the 429 `retry_after` format
  are as described.
- The `startgroup` + `admin=` link format is as described.

**Verified by our unit tests** (fake Bot API, 28 tests in
`test/unit/telegram-*.test.ts`): everything in §4 to §8 as described,
including webhook authentication, duplicate updates, allowlist, removal and
re-add, migration (both orders), revocation, cache, TTL and fail-closed
behaviour, upload limits, download limits and path confinement, announcement
pacing, the outbox's per-minute cap and 429 handling, and polling offset
persistence.

**Needs testing on real clients and accounts** (not done):
- Tapping the url button in a group and in a supergroup, including forum
  topics, on iOS, Android, Desktop and Web. The Mini App should open over the
  group with `start_param`.
- Whether desktop asks for confirmation on every tap for an unverified bot.
- Whether an admin bot with every right switched off really receives
  documents posted without a mention, in basic groups and in supergroups.
- How each client handles `?startgroup=arcade&admin=manage_chat`. Does it
  pre-fill only that right? Does it upgrade a basic group?
- Whether the bot gets `my_chat_member` in the new supergroup after a
  migration, and in which order relative to the migration notices.
- `getChatMember` from a non-admin bot in groups with hidden members.
- Whether message edits count toward the group limit, and the real 429
  `retry_after` values.
- The real 20 MB refusal on the cloud, and multi-GB files through
  `--local`. Also whether `getFile` works in groups with protected content.
- Whether `/add`'s `reply_to_message` carries the document for files posted
  before the bot joined.
- `chat_instance` stability. It is not used for access, but it may be useful
  for diagnostics.
