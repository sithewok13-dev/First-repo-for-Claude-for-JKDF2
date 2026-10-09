# Telegram Bot API: files, group permissions, membership checks, updates

Area: Telegram Bot API file handling, group privacy and permissions, membership checks, update delivery.
Researched: 2026-10-08. Current Bot API version: **10.3 (August 24, 2026)**.

## Sources and how they were read

| Source | Version / date | Local copy |
|---|---|---|
| Bot API spec JSON (scraped from core.telegram.org/bots/api), `PaulSonOfLars/telegram-bot-api-spec` `api.json` | "Bot API 10.3", release_date "August 24, 2026"; repo commit `d59462bb` (2026-08-25) | the research workspace (not committed; sources are cited inline)api.json` |
| Verbatim HTML snapshots of core.telegram.org pages (`bots/api`, `bots/faq`, `bots/features`, `bots/webapps`, `api/links`, `api/files`, `api/config`, `api/bots/bot-to-bot`), taken from the `MarshalX/telegram-crawler` `data` branch | data-branch HEAD `9f08dcca`, committed 2026-10-08 22:43 UTC; the bots/api snapshot's newest changelog entry is August 24, 2026 (10.3) | the research workspace (not committed; sources are cited inline)crawler/*.html` (+ `.txt` text conversions; the line numbers below refer to these) |
| `tdlib/telegram-bot-api` (the Bot API server source) | commit `e3e9dd8e` (2026-08-25), `project(TelegramBotApi VERSION 10.3)`, `parameters->version_ = "10.3"` | the research workspace (not committed; sources are cited inline)tdlib-telegram-bot-api/` |
| `tdlib/td` at the submodule commit pinned by the server (`bc9c263e`): FileLoaderUtils.cpp, PartsManager.h, FileManager.cpp, FileLocation.h/.hpp, LinkManager.cpp | as pinned | the research workspace (not committed; sources are cited inline)td-files/` |

core.telegram.org itself is unreachable from this container. "VERIFIED" below means I read the primary text in the spec JSON, in a verbatim page snapshot, or in the source code myself. "REPORTED" means a secondary source or search summary. "ASSUMED" means my own inference.

---

## 1. File size limits

### Cloud Bot API (api.telegram.org)

- **Download (`getFile`): 20 MB.** VERIFIED. The spec says: "For the moment, bots can download files of up to 20MB in size... The file can then be downloaded via the link `https://api.telegram.org/file/bot<token>/<file_path>`... It is guaranteed that the link will be valid for at least 1 hour. When the link expires, a new one can be requested by calling getFile again." It also notes: "This function may not preserve the original file name and MIME type. You should save the file's MIME type and name (if available) when the File object is received." (api.json `methods.getFile`). The FAQ repeats the limit: "this will only work with files of up to 20 MB in size" (bots/faq, line 90).
  - Code: `MAX_DOWNLOAD_FILE_SIZE = 20 << 20` (20 MiB = 20,971,520 bytes) at `telegram-bot-api/Client.h:72`. When not in local mode, `do_get_file` fails early with `400 "Bad Request: file is too big"` if `max(expected_size, downloaded_size) > MAX_DOWNLOAD_FILE_SIZE` (`Client.cpp:17040-17044`). It also cancels an in-progress download that grows past the limit (`Client.cpp:9378-9383`). `file_path` is only emitted when `downloaded_size <= MAX_DOWNLOAD_FILE_SIZE` (`Client.cpp:17965-17969`).
- **Bot uploads: 50 MB via multipart, 10 MB for photos; 20 MB by URL (5 MB for photos); no limit when resending by file_id.** VERIFIED in the bots/api "Sending files" section (lines 5005-5025) and in `sendDocument`: "Bots can currently send files of any type of up to 50 MB in size". Sending by URL in `sendDocument` "will currently only work for .PDF and .ZIP files". The 50 MB cap is not in the open-source server's code (I found no such constant). It is enforced by the cloud deployment. ASSUMED: a front-end or proxy enforces it.
- **Cloud-only server-side throttles in the open-source server.** VERIFIED. These apply only when not in `--local` mode. A query is rejected with 429 (`retry_after: 60`, sent after a 3 s delay) in three cases: active requests exceed `1000 + updates_per_minute`; active upload bytes exceed 4 GiB; or active uploads exceed `100 + updates_per_minute/5` (`Client.cpp:8637-8652`, `17529-17536`). Uploads larger than 100 KB that have the same total size are paced at 0.2-0.9 s intervals (`Client.cpp:13764-13800`).

### What users can upload into a group

- **Non-Premium: 4000 parts × 512 KB = 2,097,152,000 bytes (2000 MiB, about 1.95 GiB). Premium: 8000 parts × 512 KB = 4,194,304,000 bytes (4000 MiB).** VERIFIED in two places:
  - core.telegram.org/api/files (lines 37-40): "`upload_max_fileparts_default` - Maximum number of file parts uploadable by non-Premium users. `upload_max_fileparts_premium` - ... Premium users... the total file size limit can only be reached with the biggest possible part_size of 512KB". core.telegram.org/api/config lists the mandatory defaults `"upload_max_fileparts_default": 4000` and `"upload_max_fileparts_premium": 8000` (lines 496-497, under "Here's the full list of defaults that must be used").
  - TDLib: `PartsManager.h:56-59` `MAX_PART_COUNT = 4000; MAX_PART_COUNT_PREMIUM = 8000; MAX_PART_SIZE = 512 << 10`. `FileLoaderUtils.cpp:274` has the absolute cap `MAX_FILE_SIZE = 4000 << 20 /* 4000 MB */`.
  - Caveat: these are client-config defaults. The live server value can differ. "2 GB / 4 GB" is the usual rounding.
- Group admins can stop members from sending documents: `ChatPermissions.can_send_documents` and `ChatMemberRestricted.can_send_documents` (api.json). VERIFIED.

### Local Bot API server (tdlib/telegram-bot-api)

- **`--api-id` and `--api-hash` are mandatory in every mode, not only `--local`.** VERIFIED. README line 50: "The only mandatory options are `--api-id` and `--api-hash`". They come from https://my.telegram.org, or from the env vars `TELEGRAM_API_ID`/`TELEGRAM_API_HASH`. The code check is at `telegram-bot-api.cpp:304-309`.
- **`--local` mode enables the following.** VERIFIED. The list appears identically in README lines 54-62 and in the bots/api section "Using a Local Bot API Server" (lines 159-168):
  - "Download files without a size limit."
  - "Upload files up to 2000 MB."
  - "Upload files using their local path and the file URI scheme." In code, a `file:/...` value becomes `inputFileLocal` only in local mode (`Client.cpp:10781-10786`).
  - "Use an HTTP URL for the webhook", "any local IP address", "any port".
  - "Set max_webhook_connections up to 100000." Code: `max_value = local_mode_ ? 100000 : 100` (`Client.cpp:17216`). The default is 100 in local mode and 40 otherwise (`telegram-bot-api.cpp:483`).
  - "Receive the absolute local path as a value of the file_path field without the need to download the file after a getFile request."
- **How `file_path` works.** VERIFIED from code, `Client.cpp:17957-17970`. In local mode `file_path` is `file->local_->path_`, an **absolute path on the Bot API server's filesystem**. In non-local mode it is the path relative to `files_dir_`, and only for files of 20 MB or less. The server's HTTP handler routes only `/bot<token>/...` and returns 404 for every other path, including `/file/...` (`HttpConnection.cpp:33-35`). **So the self-hosted server does not serve file downloads over HTTP.** Your bot process has to share a filesystem or volume with telegram-bot-api, or you have to put your own static file server in front of its working directory. ASSUMED: the server keeps the files on disk under its `--dir` working directory, so plan for cleanup. README line 83: "make sure that the bot can correctly handle absolute file paths in response to getFile requests".
- **Effective download ceiling in local mode.** ASSUMED: the ceiling is TDLib's 4000 MiB `MAX_FILE_SIZE` (FileManager.cpp:52 clamps `download_limit` to it), so in practice "no limit" means any file a user could upload (at most 4000 MiB).
- **Switching to the local server.** VERIFIED (spec `logOut`, README lines 78-94). Call `logOut` on the cloud first. After that the bot "will not be able to log in back to the cloud Bot API server for 10 minutes". To move between local servers, call `deleteWebhook`, then `close`, then move the `<bot_user_id>` subdirectory. `close` "will return error 429 in the first 10 minutes after the bot is launched".
- The server accepts only plain HTTP, so you need a TLS-terminating proxy for remote HTTPS access. The default port is 8081 (README lines 64-66). VERIFIED.

---

## 2. Privacy mode: what a bot sees in a group

VERIFIED from bots/features "Privacy Mode" (lines 433-444) and bots/faq "What messages will my bot get?" (lines 48-59):

- **Every bot, regardless of settings, receives** all service messages, all private-chat messages, and all messages from channels where it is a member.
- **Bots that are admins, and bots with privacy mode disabled, receive all messages.** The FAQ adds "except messages sent by other bots". Bot API 10.0 (May 8, 2026) added "the ability to see certain messages sent by other bots in groups" through Bot-to-Bot Communication Mode (bots/features lines 290-305; api/bots/bot-to-bot).
- **With privacy mode on, a non-admin bot receives only:** commands explicitly meant for it (`/command@this_bot`); general commands such as `/start` if it was the last bot to send a message to the group; messages sent via the bot (inline); and replies to messages implicitly or explicitly meant for the bot. "each particular message can only be available to one privacy-enabled bot at a time... Replies have the highest priority."
- Quote: "Privacy mode is enabled by default for all bots, except bots that were added to a group as admins (bot admins always receive all messages). It can be disabled so that the bot receives all messages like an ordinary user (**the bot will need to be re-added to the group for this change to take effect**)." Group members can see the bot's privacy setting in the member list.
- **BotFather:** `/setprivacy` sets "which messages your bot will receive when added to a group". `/setjoingroups` toggles "whether your bot can be added to groups or not" (bots/features lines 553-555). A bot can check its own settings at runtime: `getMe` returns `can_join_groups` and `can_read_all_group_messages` ("True, if privacy mode is disabled for the bot. Returned only in getMe."). VERIFIED (api.json `User`).
- **Seeing documents that members upload.** ASSUMED: this follows from the rules above. A plain document post that is not a reply to the bot and has no `/cmd@bot` caption reaches the bot only if (a) the bot is an admin or (b) privacy mode is disabled and the bot was re-added after the change. Otherwise, ask members to *reply* to a bot message with the file; `ForceReply` makes this easy. Note that **the Bot API has no method for reading chat history** (api.json has no history method; `getUserPersonalChatMessages` covers only a user's profile-linked personal chat). Updates are kept for at most 24 hours (bots/api "Getting updates", line 172). So uploads made before the bot joined, or while it was offline for more than 24 h, can never be collected. The bot has to index files as updates arrive and persist `file_id`.

---

## 3. getChatMember, ChatMember statuses, getChatAdministrators

- **Guarantee.** VERIFIED from the spec: "The method is only guaranteed to work for other users if the bot is an administrator in the chat. Returns a ChatMember object on success." Code: for any user other than the bot itself, the server requires `AccessRights::ReadMembers` (`Client.cpp:16323-16336`). For a private supergroup, a bot that is not a member gets `403 "Forbidden: bot is not a member of the supergroup chat"`. If the bot was banned it gets `"Forbidden: bot was kicked from the supergroup chat"` (`Client.cpp:8838-8857`).
- Related, VERIFIED from the spec: `ChatFullInfo.has_hidden_members` means "non-administrators can only get the list of bots and administrators in the chat". `can_manage_chat` includes "see hidden supergroup and channel members". ASSUMED: if hidden members is on, a non-admin bot's `getChatMember` on ordinary members may fail. This is one reason to make the bot an admin.
- **Six ChatMember variants and their `status` strings.** VERIFIED in api.json and in code (`get_chat_member_status`, `Client.cpp:19736-19755`):
  - `ChatMemberOwner` → `"creator"`. Fields: `is_anonymous`, `custom_title`.
  - `ChatMemberAdministrator` → `"administrator"`. Fields: `can_be_edited`, all rights flags, `custom_title`.
  - `ChatMemberMember` → `"member"`. Optional fields: `tag`, `until_date` (the date a subscription expires).
  - `ChatMemberRestricted` → `"restricted"`, "Supergroups only". Fields: **`is_member`** ("True, if the user is a member of the chat at the moment of the request"), the permission flags (`can_send_documents` among them), and `until_date` (0 means forever).
  - `ChatMemberLeft` → `"left"`.
  - `ChatMemberBanned` → `"kicked"`. Field: `until_date` (0 means forever).
- **Membership predicate** (use the same logic the server uses internally). VERIFIED in code, `Client::is_chat_member`, `Client.cpp:9044-9055`: `kicked` and `left` are non-members; `restricted` counts as a member only when `is_member` is true; everything else, **including `creator`**, counts as a member. The code comments say "ignore Creator.is_member_" and "only creator itself knows that he is a left creator" (`Client.cpp:5830`, `9052`). ASSUMED consequence: an owner who has left the group still shows as `"creator"`.
- **getChatAdministrators.** VERIFIED. It returns an array of ChatMember. A new `return_bots` parameter was added in 10.0: "By default, bots other than the current bot are omitted". For supergroups the server fetches at most **100** administrators (`getSupergroupMembers(..., Administrators, 0, 100)`, `Client.cpp:16338-16360`).
- **Rate limits for getChatMember and other read methods.** **Not documented.** I found nothing in the spec or FAQ. ASSUMED: cache membership per (chat, user) with a short TTL and refresh it from `chat_member` updates. Handle 429 generically (section 6).

---

## 4. Updates

- **`chat_member`** VERIFIED from the spec: "A chat member's status was updated in a chat. **The bot must be an administrator in the chat and must explicitly specify "chat_member" in the list of allowed_updates** to receive these updates." Default `allowed_updates` is "all update types except chat_member, message_reaction, and message_reaction_count". The code confirms this as `DEFAULT_ALLOWED_UPDATE_TYPES` at `Client.h:1576-1580`. `allowed_updates` is sticky: "If not specified, the previous setting will be used". Changes do not affect updates created before the call.
- **`my_chat_member`** VERIFIED: "The bot's chat member status was updated in a chat. For private chats, this update is received only when the bot is blocked or unblocked by the user." The server emits `my_chat_member` when the user in the change is the bot itself, and `chat_member` otherwise (`Client.cpp:18702-18720`). It is enabled by default. `ChatMemberUpdated` carries `chat`, `from` (who made the change), `date`, `old_chat_member`, `new_chat_member`, `invite_link`, `via_join_request`, and `via_chat_folder_invite_link`. ASSUMED usage: adding, removing, promoting or demoting the bot shows up as a status or rights change in `new_chat_member`; check `ChatMemberAdministrator` flags there to confirm the bot got the rights it needs.
- Service messages also announce joins and leaves (`new_chat_members` "the bot itself may be one of these members", `left_chat_member`). These are delivered regardless of privacy mode. REPORTED: large groups may hide join/leave service messages, so prefer `chat_member` as the authoritative source.
- **Group → supergroup migration.** VERIFIED.
  - Service message fields: `Message.migrate_to_chat_id` ("The group has been migrated to a supergroup with the specified identifier") in the old group and `Message.migrate_from_chat_id` in the new supergroup. Code: `messageChatUpgradeTo`/`messageChatUpgradeFrom` at `Client.cpp:5035-5046`.
  - A call that needs write access to the old group id returns `400 "Bad Request: group chat was upgraded to a supergroup chat"` with `parameters.migrate_to_chat_id` (`ResponseParameters`; `Client.cpp:8821-8827`).
  - IDs "may have more than 32 significant bits... at most 52 significant bits", so store them as int64 or double. Re-key every per-group record (shelf, seats, queue) when migration happens.
  - `supergroup_chat_created` never arrives through updates.
- **`update_id`.** VERIFIED from the spec: "Update identifiers start from a certain positive number and increase sequentially... handy if you're using webhooks, since it allows you to ignore repeated updates or to restore the correct update sequence, should they get out of order. **If there are no new updates for at least a week, then identifier of the next update will be chosen randomly instead of sequentially.**" Updates are kept on the server "not... longer than 24 hours". Code: message updates expire `date + 86400` (`Client.cpp:18474`).
- **Duplicates and ordering under webhooks.** VERIFIED behavior plus an ASSUMED consequence:
  - A non-2xx response or a dropped connection triggers a retry with exponential backoff. The first retry comes after 1 s and the delay doubles up to a random 60-120 s cap. The webhook's own `Retry-After` header is honored, clamped to 3600 s. The update is dropped once the next attempt would pass its 24 h expiry (`WebhookActor.cpp:493-523`, `WebhookActor.h:75`).
  - HTTP 410 responses sustained for 23 h close the webhook (`WebhookActor.cpp:636-648`, `WebhookActor.h:76`).
  - ASSUMED consequence: delivery is at-least-once, so **dedupe by `update_id`**.
  - Updates are serialized per "queue". Messages are queued per chat_id (`Client.cpp:19411`). `chat_member` updates are queued per **user_id** and `my_chat_member` per chat_id (`Client.cpp:18713`). With `max_connections` > 1 (default 40), different queues are delivered concurrently. ASSUMED consequence: a member's join event and their first message can arrive in either order.
- **Webhook secret.** VERIFIED. `setWebhook.secret_token` is 1-256 characters from `A-Z a-z 0-9 _ -`. "If specified, the request will contain a header **X-Telegram-Bot-Api-Secret-Token** with the secret token as content." Code: `WebhookActor.cpp:560-561`. Compare it in constant time. The 2xx response body of a webhook may carry an API call in a `method` field. Its result can't be known, and `get*`, `setWebhook`, `deleteWebhook`, `close` and `logOut` are ignored there (`WebhookActor.cpp:618-634`).
- **`getUpdates` offset semantics.** VERIFIED from the spec plus code `Client.cpp:16926-16950`, `17588-17625`:
  - `offset` = highest `update_id` + 1. An update is confirmed once getUpdates is called with a higher offset.
  - A negative offset returns the last `-offset` updates and forgets the earlier ones.
  - `limit` is 1-100 (default 100). `timeout` is clamped to **50 s** (`LONG_POLL_MAX_TIMEOUT`, `Client.h:1732`).
  - getUpdates fails with **409 Conflict** while a webhook is set. A second concurrent poller gets "Conflict: terminated by other getUpdates request; make sure that only one bot instance is running" (`Client.cpp:17499`).

---

## 5. Admin rights for reading uploads and checking membership; requesting rights at add time

- **ChatAdministratorRights fields (10.3).** VERIFIED:
  - Required: `is_anonymous`, `can_manage_chat`, `can_delete_messages`, `can_manage_video_chats`, `can_restrict_members`, `can_promote_members`, `can_change_info`, `can_invite_users`, `can_post_stories`, `can_edit_stories`, `can_delete_stories`, `can_send_welcome_messages` (new in 10.3).
  - Optional: `can_post_messages`, `can_edit_messages` (channels only), `can_pin_messages`, `can_manage_topics`, `can_manage_direct_messages`, `can_manage_tags`.
  - `can_manage_chat` = "access the chat event log, get boost list, see hidden supergroup and channel members, report spam messages, ignore slow mode, and send messages to the chat without paying Telegram Stars. **Implied by any other administrator privilege.**"
- **Minimal set.** ASSUMED, but derived from VERIFIED text:
  - Admin status alone is what unlocks "bot admins always receive all messages" (seeing uploads), `chat_member` updates, and the getChatMember guarantee. None of these docs names a specific right.
  - `promoteChatMember`: "Pass False for all boolean parameters to demote a user", so an admin needs at least one right. The minimal admin is `can_manage_chat` only, which also covers hidden members.
  - Add `can_delete_messages` only if the bot must clean up chat. Add `can_pin_messages` only to pin a "now playing" message. Add `can_restrict_members` only for moderation. Avoid `anonymous`.
  - **Alternative without admin:** disable privacy mode (`/setprivacy`) and re-add the bot. That gives full message visibility but **not** `chat_member` updates or the getChatMember guarantee.
- **Deep link with rights.** VERIFIED from core.telegram.org/api/links, "Group/channel bot links" (lines 707-763): `t.me/<bot_username>?startgroup=<parameter>&admin=<permissions>` (or `?startgroup&admin=...`; tg:// forms too).
  - `admin` is "A combination of the following identifiers separated by `+`": `change_info`, `post_messages`, `edit_messages`, `delete_messages`, `restrict_members`, `invite_users`, `pin_messages`, `manage_topics`, `promote_members`, `manage_video_chats`, `anonymous`, `manage_chat`, `post_stories`, `edit_stories`, `delete_stories`, `manage_direct_messages`, `manage_tags`.
  - With `admin`, the client should "Bring up a dialog selection of groups where the user can add/edit admins". If the bot is already an admin, the client should "combine existing admin rights with the admin rights in admin" and then call `messages.startBot` with the parameter.
  - TDLib implements this (`LinkManager.cpp:398-460`, `2873-2880`) and also accepts `send_welcome_messages`.
  - The start parameter allows "up to 64 base64url characters" (`A-Z a-z 0-9 _ -`). After the bot is added the group receives `/start@your_bot <param>` (bots/features lines 124-136).
  - Example: `https://t.me/<bot>?startgroup=<groupToken>&admin=manage_chat` (ASSUMED, composed from the doc).
  - Caveat: the user can still edit the rights before confirming. Always verify through `my_chat_member` or `getChatMember(chat, bot_id)`.
- **Default suggested rights.** VERIFIED: `setMyDefaultAdministratorRights(rights, for_channels)` "change[s] the default administrator rights requested by the bot when it's added as an administrator... These rights will be suggested to users, but they are free to modify the list before adding the bot." `KeyboardButtonRequestChat` (private chat only) can request a group where the bot gets `bot_administrator_rights` and must be a subset of `user_administrator_rights`. It returns `ChatShared.chat_id`.
- **Mini App linkage (adjacent but relevant).** VERIFIED from the spec and bots/webapps:
  - `InlineKeyboardButton.web_app` is "Available only in private chats between a user and the bot". In a group, launch the Mini App through a URL button that holds a direct link `https://t.me/<bot>/<app>?startapp=<param>` or `https://t.me/<bot>?startapp=<param>`.
  - For direct links, `initData` carries `chat_type` and `chat_instance` ("Global identifier, uniquely corresponding to the chat from which the Mini App was opened") but **not** the chat id. `WebAppInitData.chat` is only filled for attachment-menu launches and join-request flows.
  - ASSUMED design: put a server-signed group token in `startapp` (64 chars max), then call `getChatMember(group_id, initData.user.id)`. `CallbackQuery.chat_instance` uses the same description, so it could map chat_instance to chat_id, but it is unverified that the two namespaces are equal.

---

## 6. Sending rate limits and 429 handling

- VERIFIED from bots/faq "My bot is hitting limits" (lines 96-104):
  - "In a single chat, avoid sending more than one message per second. We may allow short bursts that go over this limit, but eventually you'll begin receiving 429 errors."
  - "**In a group, bots are not be able to send more than 20 messages per minute.**"
  - "For bulk notifications, bots are not able to broadcast more than about 30 messages per second, unless they enable paid broadcasts". Paid broadcasts allow up to 1000 msg/s at 0.1 Stars per message above 30/s and need at least 100,000 Stars and at least 100,000 MAU. `allow_paid_broadcast` is a per-send parameter.
- **What counts toward the limit.** Not documented: whether edits (`editMessageText`) count toward the 20/min group limit, and the limits for getChatMember or getFile. ASSUMED: the arcade must not spam the group. Use one pinned or edited status message, keep ordinary updates inside the Mini App, and keep text chat inside the Mini App (or treat group messages as user-authored).
- **429 format.** VERIFIED in code, `Query.cpp:120-127` and `Query.cpp:27-39`: `{"ok":false,"error_code":429,"description":"Too Many Requests: retry after N","parameters":{"retry_after":N}}`. MTProto flood waits are mapped to this format (`Client.cpp:62-80`). `ResponseParameters.retry_after` = "the number of seconds left to wait before the request can be repeated". The spec warns that `error_code` contents "are subject to change". Handling: sleep at least `retry_after` seconds, then retry. Keep a per-chat token bucket (≤1/s, ≤20/min per group) and serialize sends per chat.
- Other code-level limits (VERIFIED, cloud and non-local only):
  - `setWebhook` with a URL is limited to once per second (`retry_after: 1`).
  - Creating new bot sessions on a server is flood-limited per IP to 20/min and 600/h, and globally to 1000/min and 10000/h (`ClientManager.cpp:110-131`, `565-569`). This matters only for multi-bot hosting.

---

## 7. Document fields; file_id vs file_unique_id; deduplication

- **`Document` fields.** VERIFIED: `file_id`, `file_unique_id`, `thumbnail?`, `file_name?` ("Original filename as defined by the sender"), `mime_type?` ("as defined by the sender"), and `file_size?`. `file_size` "can be bigger than 2^31... has at most 52 significant bits, so a signed 64-bit integer or double... [is] safe". `file_name` and `mime_type` are controlled by the sender. Never trust them for type detection; sniff the content after downloading.
- **`file_id`.** VERIFIED from the spec and "Sending files":
  - "Identifier for this file, which can be used to download or reuse the file".
  - "**file_id is unique for each individual bot and can't be transferred from one bot to another.**"
  - "file_id uniquely identifies a file, but **a file can have different valid file_ids even for the same bot**."
  - The type can't change when resending.
  - The FAQ says "file_ids can be treated as persistent".
  - Resending by file_id has "no limits" on size, so a bot can re-post a 2 GB upload by file_id even though it cannot download it from the cloud.
- **`file_unique_id`.** VERIFIED from the spec: "Unique identifier for this file, which is **supposed to be the same over time and for different bots. Can't be used to download or reuse the file.**" Use it as the dedup and primary key, never `file_id`.
- **Is it content-based?** No, according to the code. VERIFIED: TDLib computes it as `base64url(zero_encode(serialize(as_unique())))` (`FileManager.cpp:1217-1218`). For a non-web file, `as_unique` serializes the file-type class plus the location key (`FileLocation.hpp:286-300`). For documents that key is just the server-side document `id_` (`FileLocation.hpp:173-175`). So `file_unique_id` identifies the **server object (document id)**, not a content hash.
  - ASSUMED: forwards and resends of the same document share one `file_unique_id`. Two independent uploads of identical bytes will probably produce **different** documents, and so different `file_unique_id`s, unless Telegram's servers deduplicate. That is undocumented, and a search found no primary statement either way.
  - **Recommendation:** dedupe ROMs by your own content hash (SHA-1/CRC32 per zip member, matching the FBNeo/MAME dat files), with `file_unique_id` as a secondary key.

---

## Design implications for the arcade bot (ASSUMED, derived from the facts above)

1. With the cloud Bot API, ROM sets larger than 20 MB uploaded to the group **cannot be downloaded by the bot**. Many fighters and beat 'em ups are under 20 MB, but later Neo Geo, CPS-3 and similar sets are not. Three options:
   - (a) Run a local `telegram-bot-api --local` next to the bot. It needs your own api_id/api_hash and shared storage, and the bot must handle absolute `file_path`.
   - (b) Upload ROMs straight from the Mini App to our server over HTTPS. This bypasses Telegram file limits, but the Telegram group no longer holds the shelf.
   - (c) Accept the 20 MB cap.

   Option (a) is the closest match to "uploads in the group".
2. Make the bot a group admin with `manage_chat` only. Request it with `?startgroup=<token>&admin=manage_chat` and `setMyDefaultAdministratorRights`. This gives all messages (uploads), `chat_member` updates, and reliable getChatMember. Subscribe with `allowed_updates` including `message`, `my_chat_member`, `chat_member`, `callback_query`.
3. Membership check: `status in {creator, administrator, member}` or (`restricted` and `is_member`). Re-validate on `chat_member` events and evict seats and queue entries on `left` or `kicked`.
4. Webhook: verify `X-Telegram-Bot-Api-Secret-Token`, dedupe on `update_id` (do not assume continuity after a week of idle time), return 2xx quickly and process asynchronously, and handle `migrate_to_chat_id`.
5. Keep group messages to 20/min or fewer. Put the live game state in the Mini App, not in group messages.

## Must test on real devices / real accounts

- Whether a 2 GB or 4 GB file uploaded from iPhone, Android and Desktop (non-Premium and Premium) shows the expected `file_size`, and whether `getFile` fails with "file is too big" on the cloud and succeeds on `--local`.
- Whether an admin bot with only `manage_chat` receives documents posted without a mention, in both basic groups and supergroups.
- Whether privacy-off plus re-add without admin gives document visibility. Whether a privacy-on non-admin bot gets uploads that are replies to its own message.
- How each client (iOS, Android, Desktop, Web K/A) handles `?startgroup=x&admin=manage_chat`: does it pre-fill only manage_chat, and can the user strip it?
- `getChatMember` from a non-admin bot in a supergroup with hidden members enabled.
- Whether re-uploading identical bytes gives the same or a different `file_unique_id` (cloud).
- Whether edits count toward the 20 msg/min group limit, with observed 429 `retry_after` values.
- Whether Mini App `chat_instance` equals `CallbackQuery.chat_instance` for the same group.
- Ordering of `chat_member` versus `message` updates under webhook concurrency.
- Whether `getFile` works for documents in groups with protected content enabled.

## Open questions

- How the cloud enforces the 50 MB bot upload cap; it is not in the open-source code.
- Undocumented per-method rate limits: getChatMember, getFile, editMessageText.
- Whether Telegram servers deduplicate identical uploads.
- Disk retention and cleanup policy for files downloaded by the local server.
- Live `upload_max_fileparts_*` values, which can differ from the documented defaults.

---

## Verification

Adversarial fact-check, 2026-10-09. I re-opened every primary source independently rather than reusing the original researcher's local copies:

- Fresh `api.json` from `PaulSonOfLars/telegram-bot-api-spec` (still "Bot API 10.3", "August 24, 2026"; byte-identical to the earlier copy).
- Fresh page snapshots of `core.telegram.org/{bots/api,bots/faq,bots/features,bots/webapps,api/files,api/config,api/links}` from the `MarshalX/telegram-crawler` `data` branch, converted to text by my own script. They are byte-identical to the earlier copies, so the line numbers below match the `.txt` files.
- A fresh `--depth 1` clone of `tdlib/telegram-bot-api` (commit `e3e9dd8e`, 2026-08-25, `project(TelegramBotApi VERSION 10.3)`).
- `tdlib/td` files at the pinned submodule commit `bc9c263e`: PartsManager.h/.cpp, FileLoaderUtils.cpp, FileManager.cpp, LinkManager.cpp, DialogParticipant.cpp, DialogParticipantManager.cpp.
- Telegram Android's `ChatRightsEditActivity.java` (DrKLO/Telegram `master`, fetched 2026-10-09).

Everything is stored under the research workspace (not committed; sources are cited inline)verify/`.

### Verdicts

| # | Claim (short) | Verdict | Evidence / correction |
|---|---|---|---|
| 1 | getFile ≤20 MB, link valid ≥1 h, name/MIME may be lost | **CONFIRMED** | api.json `methods.getFile` quotes all three statements. bots/faq line 90. `Client.h:72` has `MAX_DOWNLOAD_FILE_SIZE = 20 << 20` (20 MiB = 20,971,520 B). `Client.cpp:17041-17044` rejects with `400 file is too big`. `Client.cpp:9378-9383` cancels downloads that grow past the limit. The server also sets the TDLib option `ignore_file_names` (`Client.cpp:9405-9407`), which explains the lost file names. |
| 2 | Upload limit = parts × 512 KB; 4000 / 8000 parts | **CONFIRMED** (wording nuance) | api/files lines 33-40. api/config line 87 ("Here's the full list of defaults that must be used") and lines 496-497. Lines 666-669 say "multiply by 524288". 4000×524288 = 2,097,152,000 B = exactly 2000 MiB; 8000 parts = exactly 4000 MiB. Nuance: these are *fallback* defaults, used only when `help.getAppConfig` lacks the key (api/config lines 85-86). They are not "mandatory" limits. TDLib hard-codes them (`PartsManager.h:56-59`, used in `PartsManager.cpp:50,91,127,283`) and caps at 4000 MiB (`FileLoaderUtils.cpp:274`, `FileManager.cpp:52`). |
| 3 | Local server needs api-id/hash always; `--local` feature list | **CONFIRMED** | README lines 50-62 are verbatim. `telegram-bot-api.cpp:212-222` defines the options and env defaults. `:304-309` is the unconditional check ("You must provide valid api-id and api-hash"). Every feature has a `--local` code gate: `file:/` at `Client.cpp:10781-10786`; HTTP and port at `WebhookActor.cpp:704-715`; IP at `:776-787`; `max_webhook_connections` at `Client.cpp:17216`; download size at `:9378` and `:17041`; absolute path at `:17960-17965`. The "2000 MB upload" figure has no constant in the server code. It is Telegram's non-Premium MTProto limit (4000 parts), so it is a property of the Telegram cloud, not of the binary. bots/api lines 159-168 phrase it as "if you switch to a local Bot API server", but the code requires `--local`. |
| 4 | Local `file_path` is absolute; the server does not serve `/file/` | **CONFIRMED** | `Client.cpp:17959-17971` emits `file->local_->path_` in local mode. `HttpConnection.cpp:29-35` returns `404 Not Found` for any path not starting with `/bot`. Extra: this also applies in non-local self-hosted mode (relative path, but still no `/file/` route). |
| 5 | Privacy mode rules; admins see all; re-add needed | **CONFIRMED** | bots/features lines 433-444 and bots/faq lines 48-58 are verbatim. The FAQ adds "except messages sent by other bots". The 10.0 Bot-to-Bot mode exception is at bots/features lines 290-301. |
| 6 | No history method; 24 h retention; old uploads "cannot be collected" | **DOWNGRADE** | The facts hold: none of the 185 methods in api.json reads history, bots/api line 172 states the 24 h retention, and `Client.cpp:18474` has `left_time = message_date + 86400 - now`. The conclusion is overstated, see the corrected claim below. |
| 7 | getChatMember guarantee; ReadMembers; 403 for non-member | **CONFIRMED** (clarify) | The spec text is verbatim. `Client.cpp:16323-16336` requires `ReadMembers` for other users. `Client.cpp:8839-8857` returns 403 "bot is not a member of the supergroup chat" for a private supergroup, and 403 "bot was kicked…" when banned. Clarification: `ReadMembers` is an internal access level, **not an admin check**. For supergroups it behaves like `Read`. For basic groups it only requires the group to be active and the bot not to have left or been kicked (`Client.cpp:8814-8837`). Non-admin failures therefore come from Telegram's servers (for example hidden members), not from the Bot API server. |
| 8 | Six status strings; `restricted` supergroup-only with `is_member`, permissions, `until_date` | **CONFIRMED** | api.json `ChatMember*` and `get_chat_member_status`. `JsonChatMember` emits `is_member`, permissions and `until_date` only when `chat_type_ == Supergroup`. TDLib refuses to restrict members in basic groups ("Can't restrict users in basic group chats", `DialogParticipantManager.cpp:2508-2510`). `is_chat_member` (`Client.cpp:9044-9055`) ignores `Creator.is_member_`. |
| 9 | `chat_member` needs admin and opt-in; default excludes 3 types; sticky | **CONFIRMED** | api.json `Update.chat_member`, plus `getUpdates`/`setWebhook` `allowed_updates` ("If not specified, the previous setting will be used"). `Client.h:1574-1579` `DEFAULT_ALLOWED_UPDATE_TYPES` removes exactly ChatMember, MessageReaction and MessageReactionCount. These are the only three Update fields that say "must explicitly specify". |
| 10 | `secret_token` is 1-256 `[A-Za-z0-9_-]`, sent in the header | **CONFIRMED** | api.json `setWebhook.secret_token`. `Client.cpp:17244-17250` validates length ≤256 and base64url characters. `WebhookActor.cpp:560-561` adds the header only when the token is non-empty. |
| 11 | `startgroup` + `admin` link semantics and identifiers | **CONFIRMED** | api/links lines 707-763 are verbatim. Mapping notes: `restrict_members` maps to `ban_users`, `manage_chat` to `other`, `manage_tags` to `manage_ranks`. `messages.startBot` is called only "if a parameter is provided". `LinkManager.cpp:398-460` accepts the documented set plus `send_welcome_messages`, and `:2873-2876` parses both `startgroup` forms. These are client-side rules: each client decides how strictly it follows them (see #12). |
| 12 | Minimal admin = `can_manage_chat` alone, which gives all messages, `chat_member` and getChatMember (ASSUMED) | **DOWNGRADE** | Building blocks confirmed: "Implied by any other administrator privilege" (api.json `ChatAdministratorRights.can_manage_chat`), and TDLib forces `CAN_MANAGE_DIALOG` whenever any right is set (`DialogParticipant.cpp` `AdministratorRights` constructor, `if (flags_ != 0) flags_ \|= CAN_MANAGE_DIALOG`). `promoteChatMember` says "Pass False for all boolean parameters to demote". Not true for **basic groups**: there, admin status is a boolean at the MTProto level (`messages.editChatAdmin(chat_id, user, is_admin)`, TDLib `DialogParticipantManager.cpp:572`), and only the owner can promote ("Need owner rights in the group chat", `:2540-2543`). Telegram Android converts a basic group to a supergroup when the chosen bot rights are "non-default" (`ChatRightsEditActivity.java:1105-1108`, `1536-1544`), and the chat id changes. No primary doc says that a `manage_chat`-only admin gets the same message visibility as other admins. The docs only say "bot admins". |
| 13 | Rate limits 1/s per chat, 20/min per group, ~30/s broadcast; paid broadcast 1000/s, 0.1 Star, ≥100k Stars, ≥100k MAU | **CONFIRMED** | bots/faq lines 96-101 (verbatim, including the typo "are not be able") and bots/api line 5269. These are documented policy statements. Enforcement is in the Telegram cloud and cannot be checked in open-source code. Whether edits count is still undocumented. |
| 14 | `file_id` per-bot, non-transferable, multiple valid; persistent; `file_unique_id` stable and not usable for download | **CONFIRMED** | bots/api "Sending files" lines 5005-5018 and bots/faq lines 93-94 are verbatim. api.json `File.file_unique_id`. |
| 15 | `InlineKeyboardButton.web_app` private-only; groups use a t.me direct link; initData has `chat_type`/`chat_instance` but no chat id | **CONFIRMED** (precision note) | The api.json `web_app` description ("Available only in private chats between a user and the bot"); `KeyboardButton.web_app` is private-only too. bots/webapps lines 180-207 cover direct links. Lines 1120-1145: `chat` is filled "only for Mini Apps launched via the attachment menu and chat join requests", and `chat_type`/`chat_instance` are "Returned only for Mini Apps launched from direct links". Precision note: the bare `?startapp` form requires a Main Mini App set in BotFather. The `/<app>` form requires an app created with BotFather. The inline-mode `InlineQueryResultsButton.web_app` also opens a Mini App from a group, but it carries no chat context. |

### Corrected claims

- **#6.** The Bot API has no history-listing method, and pending updates expire 24 h after the message date. Old uploads are *not* necessarily lost:
  - **Reply route (code-verified).** When a member replies to an old upload (for example with `/add@bot`), the server fetches the replied-to message (`getRepliedMessage`, `Client.cpp:19371-19376`). It is delivered as `reply_to_message` with the full `document`, including its `file_id`. The server logs note that the fetch may fail "because of the chosen privacy mode" (`Client.cpp:8673-8691`).
  - **Forward/copy route (untested).** `forwardMessage`, `copyMessage` and `forwardMessages` (up to 100 ids) accept arbitrary message ids from any basic group, or any supergroup the bot belongs to (`Client.cpp:9057-9081`, `9084-9103`, `14440-14454`). Whether Telegram returns messages sent before the bot joined is undocumented (a REPORTED secondary source says it does not), so test it.
- **#12.** Bot admin status, as documented, gives all messages, `chat_member` updates and the getChatMember guarantee. In **supergroups** the smallest grantable set is `can_manage_chat` alone. In **basic groups** rights cannot be granular: admin is on/off with the full basic-group admin set, only the owner can grant it, and a client may migrate the group to a supergroup when non-default rights are requested (new chat_id). Whether a `manage_chat`-only admin bot sees all messages is inferred from "bot admins always receive all messages", not stated.

### Additional findings (missed or under-stated in the report above)

1. **Cloud webhook constraints** (VERIFIED): HTTPS only, ports 443/80/88/8443, IPv4 only, and no reserved IPs (`WebhookActor.cpp:704-715`, `776-787`; bots/api line 329). `--local` lifts all of these.
2. **In `--local` mode, `file_path` contains the bot token** (VERIFIED). The per-bot directory is `<dir or files-dir>/<bot_token>[/test]/` with `:` replaced by `~` unless `--allow-colon-in-filenames` is set (`Client.cpp:8603-8612`). Never log or expose `file_path` to clients.
3. **In `--local` mode, `getFile` returns only after the whole file has downloaded to the server's disk** (VERIFIED, `Client.cpp:17045-17048` and `9373-9386`, which answers on `is_downloading_completed_`). A 1-2 GB ROM blocks the call for the full transfer, so use long client timeouts and run it asynchronously. Files accumulate under the working directory. The server enables TDLib `use_storage_optimizer` (`Client.cpp:9405-9407`), but whether that evicts downloaded files is undocumented (ASSUMED).
4. **Basic-group admin is boolean and owner-only** (VERIFIED, `DialogParticipantManager.cpp:572`, `2498-2567`). Android's `isDefaultAdminRights()` treats "all listed rights false" (that is, `manage_chat` only) as *default*. A `?startgroup&admin=manage_chat` link on a basic group may therefore make the bot a plain full basic-group admin without migrating the group (ASSUMED from code reading; test on each client).
5. **Telegram Games as a group alternative.** `sendGame` works in groups ("Games can't be sent to channel direct messages chats and channel chats" only). A `callback_game` press gives the bot a CallbackQuery with `from.id` and `message.chat.id`, and `answerCallbackQuery.url` then opens the game URL (VERIFIED, api.json). The bot can embed a server-signed (chat_id, user_id) token in that URL, which binds the chat id that direct-link Mini Apps lack. Games do not get Mini App `initData` (ASSUMED). Non-game callbacks may also answer with `t.me` links (api.json `answerCallbackQuery.url`), so a per-user `?startapp=<signed token>` link is a possible chat-binding route (ASSUMED; test it).
6. **Doc inconsistency:** the WebAppInitData table describes `start_param` as the `startattach` value for attachment-menu launches only, but the direct-link sections (bots/webapps lines 180, 200) say `startapp` arrives in `start_param` and `tgWebAppStartParam`. Read both.
7. **License:** `tdlib/telegram-bot-api` is Boost Software License 1.0 (README, `LICENSE_1_0.txt`). It is permissive, so self-hosting is fine.
8. TDLib uses its hard-coded 4000/8000 part limits and does not read the live `upload_max_fileparts_*` config, so a server-side change would surface as upload errors, not as a client limit.
