# Telegram Mini App launch mechanisms, identity and webview APIs

Area: Telegram Mini App launch mechanisms, identity, and webview APIs. The project is a group-shared retro arcade, one game per group.
Date of research: 2026-10-08.

## 0. Sources and how much to trust each

| Source | Version / date | How it was obtained |
|---|---|---|
| Bot API spec (scraped JSON) | **Bot API 10.3, August 24, 2026** | `https://raw.githubusercontent.com/PaulSonOfLars/telegram-bot-api-spec/main/api.json`, saved at the research workspace (not committed; sources are cited inline)api.json` |
| Verbatim HTML snapshots of core.telegram.org pages: `/bots/webapps`, `/bots/api`, `/api/links`, `/bots/features` | Snapshot commit `1d68f81`, 2026-10-06 | `github.com/Bibo-Joshi/telegram-bot-api-changes`. This tracker commits the raw page HTML. Converted to text in the research workspace (not committed; sources are cited inline)txt/*.txt`. The line numbers below refer to these text files. |
| Older doc history (2023) | `spontanurlaub/telegram-bot-api-log` | Used only to date when fields were added |
| `telegram-web-app.js` (production) | Last changed 2026-10-07 (docs reference `?64`) | Tracked copies in `abhijeetpatil2122/telegram-changes-data/production/js/` and `glebxdlolreal/prodserverchangesall`. The two copies are identical (md5 `b2c8da20…`). Saved at the research workspace (not committed; sources are cited inline)twajs/telegram-web-app.prod.js` |
| tdesktop | `22b352e`, 2026-10-08, app version 7.2.10 | sparse clone at the research workspace (not committed; sources are cited inline)tdesktop` |
| Telegram Android | `f2908b1`, 2026-09-30, 12.10.6 (7112) | sparse clone at `.../android` |
| Telegram iOS | `f1dd7a2`, 2026-10-07, app 13.0 | sparse clone at `.../ios` |
| tweb (Web K) | `4016d6f`, 2026-10-08 | sparse clone at `.../tweb` |
| TDLib (master) | `c15d3f5` | raw files at `.../tdlib/` (`td_api.tl`, `telegram_api.tl`, `LinkManager.cpp`, `WebAppManager.cpp`, `KeyboardButton.cpp`, `InlineKeyboardButton.cpp`) |
| `@tma.js/init-data-node` 2.0.8 (2026-06-20) | community reference validator | npm tarball, `.../npm/idn` |
| `@types/telegram-web-app` 10.1.0 | community typings | npm tarball |
| tma.js docs (`apps/docs/platform/*.md`) | community docs | raw GitHub, `.../community/` |

Status labels: **VERIFIED** means I read the primary text or code myself. **REPORTED** means a secondary source or a search summary. **ASSUMED** means my own inference.

---

## 1. Which launch mechanisms work in groups and supergroups

| Mechanism | Works in a group? | Chat context in initData | Evidence |
|---|---|---|---|
| Inline keyboard `web_app` button | **No** | n/a | **VERIFIED** spec: *"Available only in private chats between a user and the bot. Not supported for messages sent on behalf of a business account."* (`InlineKeyboardButton.web_app`; txt/core.bots.api.txt:4112). **REPORTED**: the server returns `400 Bad Request: BUTTON_TYPE_INVALID` when such a button is sent to a group (qna.habr.com/q/1330672, telegraf issue 1936). |
| Reply keyboard `web_app` button | **No** | Docs say initData is empty | **VERIFIED** spec: *"Available in private chats only."* (`KeyboardButton.web_app`; api.txt:3926). TDLib also refuses it client-side with `"Web App buttons can be used in private chats only"` (tdlib/KeyboardButton.cpp:130-133). |
| Menu button (`MenuButtonWebApp`) | **No** (private chats with the bot only) | n/a | **VERIFIED** spec: `setChatMenuButton` is *"Use this method to change the bot's menu button in a private chat, or the default menu button."* `chat_id`: *"Unique identifier for the target private chat."* (api.txt:11010-11020). New: `MenuButtonWebApp.web_app` may hold *"a t.me link to a Web App of the bot … in which case the Web App will be opened as if the user pressed the link."* |
| Attachment menu | Technically yes: groups were added in Bot API 6.1. **Not available to normal bots in production.** | `chat` (id, type, title, username, photo_url) plus `query_id` | **VERIFIED** webapps doc: *"Attachment menu integration is currently only available for major advertisers on the Telegram Ad Platform. However, all bots can use it in the test server environment."* (webapps.txt:433, 2088). Bot API 6.1 changelog: *"Added the ability to use bots added to the attachment menu in group, supergroup and channel chats."* (webapps.txt:255) |
| Inline mode (`InlineQueryResultsButton.web_app`, reached via `switch_inline_query*`) | Yes, in any chat where inline mode works | **None**. The doc says: *"Inline Mini Apps have no access to the chat – they can't read messages or send new ones on behalf of the user."* (webapps.txt:385) | **VERIFIED**. Clients use `messages.requestSimpleWebView`, which has **no peer parameter** (tdlib/telegram_api.tl:2755; tdesktop bot_attach_web_view.cpp:1253-1272). |
| **Direct link Mini App** `https://t.me/<bot>/<appname>?startapp=<p>&mode=<m>` | **Yes** | `chat_type`, `chat_instance`, `start_param`. **No** `chat`, **no** `query_id`. | **VERIFIED** doc: *"Mini App Bots can be launched from a direct link in any chat. They support a startapp parameter and are aware of the current chat context."* *"In this mode, Mini Apps can use the chat_type and chat_instance parameters to keep track of the current chat context. This introduces support for concurrent and shared usage by multiple chat members – to create live whiteboards, group orders, multiplayer games and similar apps."* *"Mini Apps opened from a direct link have no access to the chat – they can't read messages or send new ones on behalf of the user."* (webapps.txt:393-399) |
| **Main Mini App link** `https://t.me/<bot>?startapp[=<p>]&mode=<m>` | **Yes** | Same as direct link | **VERIFIED** doc: *"A bot's main Mini App can also be opened in the current chat by direct link in the format https://t.me/botusername?startapp … In this mode, Mini Apps can use the chat_type and chat_instance parameters…"* (webapps.txt:357-365). Requires a Main Mini App to be set in @BotFather. |
| Inline keyboard **`url`** button whose URL is one of the t.me links above | **Yes.** The spec puts no chat-type restriction on `url`. | The same as a direct-link launch. The clients pass the message's chat as the context peer. | **VERIFIED** spec: `url`: *"HTTP or tg:// URL to be opened when the button is pressed."* (api.txt:4104). Client code: see §1.1. |
| `login_url` button | Allowed in groups, but it is **not a Mini App**. It opens an HTTPS page with Login-Widget auth data. | n/a (Login Widget hash scheme) | **VERIFIED** spec: *"An HTTPS URL used to automatically authorize the user. Can be used as a replacement for the Telegram Login Widget. Not supported for ephemeral messages."* (api.txt:4116) |
| `callback_game` (HTML5 Games) | Yes. Games can be sent to groups. | No initData. The bot learns `message.chat.id`, `from` and `chat_instance` from the callback query, and returns the game URL via `answerCallbackQuery.url`. | **VERIFIED** spec: `sendGame.chat_id`: *"Games can't be sent to channel direct messages chats and channel chats"* (api.txt:18331). **VERIFIED**: tdesktop opens games in the same `Ui::BotWebView` panel (bot_attach_web_view.cpp:1501-1517). **ASSUMED**: without `tgWebAppVersion`, `telegram-web-app.js` defaults to version `6.0` (line 289), so most WebApp methods are disabled for a game page. |
| Chat-join-request Mini App (Bot API 10.1, `sendChatJoinRequestWebApp`) | Only for users who are *requesting to join*. The bot must be the chat's guard bot. | `chat` plus `chat_join_request_query_id` | **VERIFIED** webapps.txt:417-425. Useful for gatekeeping only. |

Also relevant (VERIFIED, Bot API 10.2, July 14, 2026): **ephemeral messages**. These are *"group messages … visible only to a specific user and the bot"*, sent through `sendMessage.ephemeral_message_parameters{receiver_user_id, callback_query_id, replace_callback_query_message}`. They let the bot hand an individual user a personal launch button (a url button to a direct link) inside the group. Delivery is not guaranteed: *"It is not guaranteed that the user will receive the message, especially if they are offline."* (`EphemeralMessageParameters`).

### 1.1 Client source: a direct link clicked in a group sends the group as context

The server receives the group peer in `messages.requestAppWebView` / `messages.requestMainWebView` (TL: `peer:InputPeer`, tdlib/telegram_api.tl:2777, 2805). All four clients I checked pass the chat where the link was clicked:

- **tdesktop** (VERIFIED):
  - `local_url_handlers.cpp:723-769` parses `appname`, `startapp`, `mode=fullscreen|compact` and records `.clickFromMessageId = myContext.itemId`.
  - `window_session_controller.cpp:711-735`: for `ResolveType::BotApp`, `contextPeer = item ? item->history()->peer : bot`.
  - `bot_attach_web_view.cpp:1312-1321` sends `MTPmessages_RequestAppWebView(..., _context.action->history->peer->input(), ...)`.
  - For the Main Mini App link, `ResolveContext` (`bot_attach_web_view.cpp:410-427`) uses the currently open chat (`dialogsEntryStateCurrent`). `requestMain` passes it at line 1293.
- **Android** (VERIFIED):
  - `BotWebViewSheet.java:1577-1594`: `req.peer = fragment instanceof ChatActivity ? (user ?: getCurrentChat()) : botUser` for `TL_messages_requestAppWebView`.
  - The same logic is used for `TL_messages_requestMainWebView` (lines 1612-1618).
  - `MessagesController.java:24525-24531` uses `((ChatActivity) fragment).getDialogId()` for the main app.
- **iOS** (VERIFIED): `ChatControllerOpenWebApp.swift:739-746` sets `peerId = chatController?.chatLocation.peerId ?? botPeer.id`, then calls `requestAppWebView(peerId: peerId, …)` at line 811.
- **tweb / Web K** (VERIFIED): `chat.ts:1500` has `options.peerId ??= this.peerId`. `appAttachMenuBotsManager.ts:186-222` sends `peer` for app and main web views.
- **TDLib** (VERIFIED): `getWebAppLinkUrl chat_id:int53 …` is documented as *"Identifier of the chat in which the link was clicked; pass 0 if none"* (td_api.tl:13464-13471). `WebAppManager.cpp:529-560` falls back to the bot only if the chat is inaccessible.

**tdesktop UX detail (VERIFIED):** a url button from a **non-verified** bot is opened through `HiddenUrlClickHandler::Open` (`api_bot.cpp:363-377`). That sets `mayShowConfirmation = true` (`click_handler_types.cpp:425`), which becomes `botAppForceConfirmation` and then `ConfirmType::Always` (`bot_attach_web_view.cpp:1003-1010`). So on desktop the user sees an "Open app" confirmation box **every time** they tap the url button. Behaviour on mobile was not verified (must test).

---

## 2. Direct link parameters

- **Formats** (VERIFIED, `/api/links`, links.txt:2249-2327):
  - `t.me/<bot_username>?startapp&mode=<mode>`
  - `t.me/<bot_username>?startapp=<start_parameter>&mode=<mode>` (Main Mini App)
  - `t.me/<bot_username>/<short_name>?startapp=<start_parameter>&mode=<mode>` (direct link)
  - The `tg://resolve?domain=<bot>&appname=<short_name>&startapp=…&mode=…` equivalents
- **`startapp` charset and length:**
  - The official Mini Apps page and the `/api/links` page give **no explicit charset or length limit for `startapp`** (VERIFIED by absence, snapshot 2026-10-06).
  - The general deep-link rule on `/bots/features` (for `start`/`startgroup`) is *"A-Z, a-z, 0-9, _ and - are allowed. We recommend using base64url to encode parameters with binary and other types of content. The parameter can be up to 64 characters long."* (features.txt:249). VERIFIED.
  - `/api/links` says `start`/`startgroup` are *"up to 64 base64url characters"*. VERIFIED.
  - TDLib checks `startapp` only with `is_valid_start_parameter() = is_base64url_characters()`, without a length check (LinkManager.cpp:57-59, 2408, 3116). For `/<bot>/<app>` links the value is passed through as-is (line 3069). VERIFIED.
  - tma.js community docs: *"Only … A-Z, a-z, 0-9, _ and - … The parameter can be up to 512 characters long"*, with regex `/^[\w-]{0,512}$/`. REPORTED.
  - **Recommendation:** keep `startapp` within 64 base64url characters. That is safe under either limit (ASSUMED). The value is delivered as `start_param` (inside signed initData) and as the GET parameter `tgWebAppStartParam` (VERIFIED webapps.txt:395).
- **`mode`:**
  - `mode=compact`: *"Starting from Bot API 7.6, by default, Mini Apps of this type open to full-screen height … you can change this behavior by including the parameter mode=compact"* (webapps.txt:401). The text first appears in the snapshot of 2024-07-01, the Bot API 7.6 release date. VERIFIED.
  - `mode=fullscreen`: documented on `/api/links` as *"If equal to fullscreen, the messages.requestAppWebView.fullscreen flag must be set"* (links.txt:2285-2327). It first appears in the snapshot of 2025-01-19 (VERIFIED from mirror history). The fullscreen flags and Mini App fullscreen mode arrived with **Bot API 8.0 (Nov 17, 2024)**.
  - The Bot API Mini Apps page itself still only documents `mode=compact` (VERIFIED).
  - Client parsing: tdesktop parses `mode=fullscreen` for both link types and `compact` only for the Main app link (`local_url_handlers.cpp:758, 767-769`). Android parses both (`LaunchActivity.java:2245-2246`). TDLib maps `compact`/`fullscreen`/default to full-size (`LinkManager.cpp:609-617`). VERIFIED.
- Only `startapp` reaches the Mini App. Other query parameters on the t.me link are not forwarded. ASSUMED from the client parsers: tdesktop and Android read only `startapp`/`mode`.

---

## 3. initData fields and which launches populate them

Raw transport (VERIFIED, `telegram-web-app.js` lines 1-19 and 287-347):
- Launch parameters arrive in the **URL fragment**: `tgWebAppData`, `tgWebAppVersion`, `tgWebAppPlatform`, `tgWebAppThemeParams`, `tgWebAppFullscreen`, and so on.
- They are copied into `sessionStorage` (`initParams`), so a page reload keeps the **old** initData.
- `tgWebAppStartParam` arrives as a **GET** query parameter (docs).
- `Telegram.WebApp.initData` is the raw `tgWebAppData` string. `initDataUnsafe` is the parsed version, with JSON-decoded values.

`WebAppInitData` (VERIFIED, webapps.txt:1677-1733). The doc says the object *"is empty if the Mini App was launched from a keyboard button or from inline mode"*.

| Field | Meaning (doc) | Populated by |
|---|---|---|
| `query_id` | *"A unique identifier for the Mini App session, required for sending messages via the answerWebAppQuery method."* | `messages.requestWebView` launches (inline `web_app` button, menu button, attachment menu) — private chats, or attachment menu in groups. **Not** direct links (they "can't … send new ones on behalf of the user"). |
| `chat_join_request_query_id` | Bot API 10.1 | chat-join-request launches |
| `user` | current user (`WebAppUser`: id, first/last name, username, language_code, is_premium, added_to_attachment_menu, allows_write_to_pm, photo_url) | all launches with initData |
| `receiver` | *"Returned only for private chats and only for Mini Apps launched via the attachment menu."* | attachment menu, private chat |
| `chat` | *"…Returned for supergroups, channels and group chats – only for Mini Apps launched via the attachment menu and chat join requests."* | attachment menu or join request **only** |
| `chat_type` | *"Can be either "sender" for a private chat with the user opening the link, "private", "group", "supergroup", or "channel". Returned only for Mini Apps launched from direct links."* | direct link and Main app link |
| `chat_instance` | *"Global identifier, uniquely corresponding to the chat from which the Mini App was opened. Returned only for Mini Apps launched from a direct link."* | direct link and Main app link |
| `start_param` | Table text: *"…Only returned for Mini Apps when launched from the attachment menu via link."* This is outdated. The direct-link and main-app sections say a non-empty `startapp` *"will be passed to the Mini App in the start_param field and in the GET parameter tgWebAppStartParam"* (webapps.txt:357, 395). | attachment links, direct links, Main app links |
| `can_send_after` | seconds until `answerWebAppQuery` may be used | with `query_id` |
| `auth_date` | *"Unix time when the form was opened."* | always |
| `hash` | HMAC for the bot server | always |
| `signature` | Ed25519, *"A signature of all passed parameters (except hash), which the third party can use to check their validity."* | always (Bot API 8.0+) |

The `chat_type`, `chat_instance` and `startapp` text first appears in the doc snapshot of **2023-04-21 (Bot API 6.7)** (VERIFIED from mirror history).

**Is the group chat id available for a direct-link launch from a group?**
- **No.** VERIFIED from the docs: `chat` is limited to attachment menu and join-request launches. Direct links get only `chat_type` and `chat_instance`.
- The clients do send the group peer to Telegram's server (§1.1), but per the docs the server does not echo the chat id in initData. Confirm on devices that no `chat` key appears.

**Is `start_param` delivered for direct-link launches?** **Yes** (VERIFIED, doc sections for direct links and Main app links). It is covered by `hash`/`signature` because it is an initData field. Anyone can still craft a link with any `startapp`, so the signature proves only that Telegram delivered the value, not that the bot authored it.

**What is `chat_instance`? Is it stable and unique per chat?**
- Doc: a *"global identifier, uniquely corresponding to the chat"* (VERIFIED). The same wording is used for `CallbackQuery.chat_instance`: *"Global identifier, uniquely corresponding to the chat to which the message with the callback button was sent. Useful for high scores in games."* (api.txt:4250).
- The doc's stated purpose, *"concurrent and shared usage by multiple chat members"*, implies all members of a group see the **same** value. This is ASSUMED, but strongly implied by the official text.
- A Telegram bug report (bugs.telegram.org/c/42529, Android 10.15.1) said that in channels `chat_type` came back as `sender` with a *different* `chat_instance* "even though it is the same miniapp in the same channel"*, later marked fixed. REPORTED. This supports the per-chat-shared reading and shows that clients have regressed before.
- **Not established anywhere:** whether `chat_instance` is per-bot, whether it equals `callback_query.chat_instance` for the same chat, and whether it survives a group-to-supergroup migration. These must be tested.

---

## 4. initData validation

**Bot-token HMAC** (VERIFIED, webapps.txt:1811-1832):
- *"…comparing the received hash parameter with the hexadecimal representation of the HMAC-SHA-256 signature of the data-check-string with the secret key, which is the HMAC-SHA-256 signature of the bot's token with the constant string WebAppData used as a key."*
- *"Data-check-string is a chain of all received fields, sorted alphabetically, in the format key=<value> with a line feed character ('\n', 0x0A) used as separator."*
- In pseudo-code: `secret_key = HMAC_SHA256(key="WebAppData", msg=bot_token)`, then `hex(HMAC_SHA256(key=secret_key, msg=data_check_string)) == hash`.
- The reference implementation `@tma.js/init-data-node` 2.0.8 (VERIFIED, `dist/entries/parsing-Cn-1lfce.js`) does the following:
  - URL-decodes the pairs with `URLSearchParams`.
  - Excludes **only `hash`**, so `signature` *is* included in the HMAC data-check-string.
  - Sorts the pairs and joins them with `\n`.
  - Computes the secret with `createHmac(token, "WebAppData")`, where the HMAC key is `"WebAppData"`.

**Third-party Ed25519 `signature`** (VERIFIED, webapps.txt:1834-1867; added with **Bot API 8.0, 2024-11-17**, per changelog line 153 and mirror history):
- *"…the base64url-encoded representation of the Ed25519 signature of the data-check-string."*
- The data-check-string is `"<bot_id>:WebAppData\n"` followed by all fields **except `hash` and `signature`**, sorted and joined by `\n`.
- Public keys (hex):
  - Production `e7bf03a2fa4602af4580703d88dda5bb59f32ed8b02a56c187fe7d34caed242d`
  - Test `40055058a4ee38156a06562e52eece92a771bcd8346a8c4615cb7376eddf72ec`
- The tma.js implementation uses the same keys and the same string (VERIFIED).

**auth_date freshness:**
- The official docs give no number. They say only *"To prevent the use of outdated data, you can additionally check the auth_date field"* (VERIFIED).
- `@tma.js/init-data-node` defaults to `expiresIn = 86400` s (24 h) (VERIFIED code; this is a community default).
- Recommendation (ASSUMED/design):
  - Accept initData only to *establish* a session, with a short window such as ≤ 1 h. Then issue our own short-lived, refreshable session token for the WebSocket.
  - initData does not change while the Mini App stays open or is reloaded (sessionStorage), and Android re-opens an existing tab (`tryReopenTab`, `MessagesController.java:24536`). A reconnect can therefore present old initData.
  - Treat initData as a bearer secret: never log it.

---

## 5. WebApp JS API: features and the Bot API version that introduced each

All VERIFIED from the docs (webapps.txt, method table and "Recent changes"). A "JS gate" means `telegram-web-app.js` itself refuses or warns below that version (VERIFIED, line numbers from the prod file).

| Feature | Bot API | Notes |
|---|---|---|
| `requestFullscreen()`, `exitFullscreen()`, `isFullscreen`, events `fullscreenChanged` and `fullscreenFailed` (`UNSUPPORTED`, `ALREADY_FULLSCREEN`) | **8.0** (2024-11-17) | JS gate 8.0, which throws `WebAppMethodUnsupported` (lines 2929-2941). Client support: tdesktop handles it (attach_bot_webview.cpp:2371-2388); also Android, iOS and tweb. |
| `lockOrientation()`, `unlockOrientation()`, `isOrientationLocked` | **8.0** | JS gate 8.0 (line 629). Handled on Android (`web_app_toggle_orientation_lock`, BotWebViewContainer.java:2902) and iOS. **Not handled by tdesktop** (no-op). |
| `disableVerticalSwipes()` / `enableVerticalSwipes()`, `isVerticalSwipesEnabled` | **7.7** | JS gate 7.7 (line 607). Android and iOS handle `web_app_setup_swipe_behavior`. Desktop ignores it. Doc: the user can still swipe the header. |
| `enableClosingConfirmation()` / `disable…`, `isClosingConfirmationEnabled` | **6.2** | JS gate 6.2 (line 597) |
| `isActive`, events `activated` and `deactivated` | **8.0** | Driven by the native `visibility_changed` event (JS lines 397-405). *"False, if the Mini App is minimized."* |
| `safeAreaInset`, `contentSafeAreaInset`, events `safeAreaChanged` and `contentSafeAreaChanged`, CSS vars `--tg-safe-area-inset-*` and `--tg-content-safe-area-inset-*` | **8.0** | |
| `viewportHeight`, `viewportStableHeight`, `isExpanded`, `expand()`, event `viewportChanged{isStateStable}` | base (6.0) | No version label |
| `HapticFeedback` (`impactOccurred`, `notificationOccurred`, `selectionChanged`) | **6.1** | JS gate 6.1 (line 1477). Android, iOS and tweb handle it. **tdesktop does not** (no-op). |
| `CloudStorage` | **6.9** | 1024 keys per user. Key is 1-128 chars `[A-Za-z0-9_-]`. Value is 0-4096 chars. JS gate 6.9. |
| `DeviceStorage` | **9.0** (2025-04-11) | Up to 5 MB per user, local only. tdesktop implements it. |
| `SecureStorage` | **9.0** | Up to 10 items. Uses iOS Keychain or Android Keystore. **tdesktop always fails it** (`secureStorageFailed`, attach_bot_webview.cpp:2459-2466). |
| `isVersionAtLeast(v)` | **6.1** | Compares against `tgWebAppVersion`, which defaults to `'6.0'` (JS line 289). Note: tweb rewrites `tgWebAppVersion=8.0` to `9.0` in the server URL (webApp.tsx:247-249). That suggests the version is set server-side; ASSUMED. |
| `platform` | **6.4** | Value is the platform string the client sends. VERIFIED: `"tdesktop"` (tdesktop), `"android"` (Android), `"ios"` / `"macos"` (Telegram-iOS `BotWebView.swift:7-11`), `"web"` (tweb). REPORTED (tma.js): `weba` (Web A), plus `android_x`, `unigram`, and `unknown` as the JS default. |
| `openLink(url[, {try_instant_view}])` | **6.1** (try_instant_view 6.4) | *"opens a link in an external browser. The Mini App will not be closed."* It must be called from a user gesture. Undocumented option `try_browser` (JS gate 7.6, line 3026). |
| `openTelegramLink(url)` | **6.1** | Only t.me / telegram.me hosts are accepted (JS `isTmeHostname`). Since **7.0** the Mini App is **not closed**. tdesktop opens it with `keepOpen=true` (attach_bot_webview.cpp:2863-2876). Undocumented option `force_request`. |
| `close()` | base | Undocumented `{return_back:true}` (7.6). On Android this returns to the external app or bot tab that opened the Mini App (BotWebViewContainer.java:1565-1597). It does **not** return to a group. |
| `hideKeyboard()` | 9.1 | |
| `shareMessage(prepared_id)` with `savePreparedInlineMessage(allow_group_chats)` | 8.0 | |
| `switchInlineQuery(query, ['groups',…])` | 6.7 | |

**Security change (VERIFIED, Bot API 10.2, 2026-07-14):** *"Hardened the security of Mini Apps by disallowing the usage of Mini App methods from origins different from the original Mini App domain. The protection will be automatically enabled for all Mini Apps on July 20, 2026."* Keep the Mini App page that calls `Telegram.WebApp.*` on the bot's configured origin, and do not rely on calls from cross-origin iframes or navigations.

---

## 6. Returning to the group, and posting launch buttons into groups

**Bots can post launch buttons into groups** (VERIFIED spec). Use an inline keyboard `url` button whose URL is `https://t.me/<bot>/<app>?startapp=<token>` or `https://t.me/<bot>?startapp=<token>`. `url` has no private-chat restriction, unlike `web_app`. §1.1 shows that clients open it in the context of that group.

Other ways to get the link into a group:
- **Ephemeral per-user messages** (10.2).
- **Inline-mode results** that carry such a url button.
- `WebApp.shareMessage` of a `savePreparedInlineMessage` result with `allow_group_chats` (8.0).
- `switchInlineQuery(..., ['groups'])`.

Pinning the lobby message requires admin rights (ASSUMED/standard).

**Returning the user to the group:**
1. A direct-link or Main-app launch opens **over the current chat**, which is the group. `Telegram.WebApp.close()` therefore leaves the user in the group. The context is VERIFIED in client code; the visible UX is ASSUMED.
2. `openTelegramLink('https://t.me/<public_group_username>')`, or for a private **supergroup** `https://t.me/c/<channel_id>/<message_id>`. The links doc defines `channel` as *"Channel or supergroup ID"*, and the Bot API id is `-100<channel_id>` (ASSUMED/standard). Since 7.0 the Mini App stays open. Basic (non-super) private groups have no such link.
3. On mobile, users can minimize the Mini App, which fires `deactivated`, and go back to the chat. Pause or handle input loss on `deactivated`.

---

## 7. Implications for the arcade (design guidance; ASSUMED unless noted)

- **Use a url-button direct link as the only launch path in groups.** Inline and reply `web_app` buttons and the menu button do not work in groups, and the attachment menu is advertiser-only.
- **Group binding.** The bot (ideally a group **admin**) posts a lobby message whose url button carries `startapp = base64url(groupRef ‖ HMAC)`, kept within 64 characters. On connect the server should:
  1. Validate `hash` and check freshness.
  2. Check `chat_type ∈ {group, supergroup}`.
  3. Map `start_param` to the group `chat_id`.
  4. Bind or compare `chat_instance` with the value stored for that group, using TOFU on first launch.
  5. Call `getChatMember(chat_id, user.id)`. VERIFIED: *"only guaranteed to work for other users if the bot is an administrator in the chat"*.

  This stops forwarded links from being used outside the group.
- **Do not depend on `query_id` / `answerWebAppQuery`.** They are unavailable for direct links. The bot posts results itself through the Bot API.
- **Desktop:** expect a confirmation dialog on every url-button launch from an unverified bot. Fullscreen is supported there; haptics, orientation lock and swipe control are no-ops.
- **Mobile gameplay:** call `disableVerticalSwipes()` (7.7+) and `enableClosingConfirmation()`. Optionally call `requestFullscreen()` and `lockOrientation()` (8.0+) after a `isVersionAtLeast('8.0')` check. Use the safe-area CSS variables.

---

## 8. Must test on real clients (iOS, Android, Telegram Desktop for Windows/macOS/Linux, Telegram macOS, Web A, Web K)

1. Tap a url button `t.me/<bot>/<app>?startapp=X` in a group and in a supergroup, including a forum topic. Confirm initData has `chat_type`, `chat_instance` and `start_param`, and has **no** `chat` and **no** `query_id`. Repeat with the Main-app link `t.me/<bot>?startapp=X`.
2. Check `chat_instance` behaviour:
   - It should be identical for two different users in the same group.
   - It should differ between groups.
   - It should be the same for the direct-link app and the Main-app link.
   - It should be stable across days and after a group-to-supergroup migration.
   - Compare it with `callback_query.chat_instance` for the same group.
3. `startapp` length and charset: 64, 65, 512 and 513 characters, and characters outside base64url. Check each client.
4. `mode=compact` / `mode=fullscreen` on each client. Also `requestFullscreen()` / `lockOrientation()` / `disableVerticalSwipes()` on iOS and Android in landscape play.
5. Confirmation dialogs on launch: how often they appear per client for an unverified bot (desktop forces one every time per the code), and the first-launch ToS / "allow messages" dialog.
6. Minimizing and tabs: whether `activated`/`deactivated` fire, and whether re-opening reuses an old webview and old initData (Android `tryReopenTab`).
7. `close()` and `openTelegramLink('https://t.me/c/<id>/<msg>')` return the user to the group on each client.
8. Bot API 10.2 same-origin enforcement: WebApp methods should work from our origin, and nothing should be needed from iframes.
9. Ephemeral url-button messages (10.2) render and are tappable on all clients.
10. `tgWebAppVersion` reported per client. Haptics on iOS and Android. Gamepad and keyboard input inside each client's webview (outside this area's scope but critical).

## 9. Open questions

- Is `chat_instance` per-bot or global? Does it equal `CallbackQuery.chat_instance` for the same chat? Telegram does not document either.
- What is the official maximum length of `startapp`? Undocumented (community says 512).
- Can `answerCallbackQuery.url` point at a `t.me/<bot>/<app>` direct link from a non-game callback button? The spec mentions only `t.me/your_bot?start=XXXX`.
- Do Games (`callback_game`) opened on mobile get a webview where `Telegram.WebApp` works, and could they serve as a fallback launch path in groups?

---

## Verification

Adversarial fact-check run on 2026-10-09. I re-checked each high-relevance claim against primary sources that I downloaded again myself. I did not reuse the first researcher's text conversions. My working files are in the research workspace (not committed; sources are cited inline)verify/`:

- `api.json`: PaulSonOfLars spec, re-downloaded. It still reports **Bot API 10.3, August 24, 2026**.
- `core.*.html`, converted with my own HTML-to-text script into `core.*.v.txt`: the Bibo-Joshi `master` snapshots. These are **byte-identical** (`cmp`) to commit `1d68f81` (2026-10-06), so no newer doc change exists in the tracker.
- `twa.js`: `telegram-web-app.js` from `abhijeetpatil2122/telegram-changes-data`. Its md5 `b2c8da20…` is identical to the earlier copy. This is a third-party tracking mirror, not telegram.org itself. The docs reference `?64`.
- `clients/`: fresh raw copies fetched on 2026-10-09 from the default branches of tdesktop (`dev`), DrKLO/Telegram (`master`), Telegram-iOS (`master`), tweb (`master`), Ajaxy/telegram-tt (Web A, `master`) and tdlib/td (`master`).
- `macos/`: a sparse clone of overtake/TelegramSwift. **Its last public commit is from 2025-07-29, so it is stale.**
- `pypi/`: aiogram 3.31.0 (2026-08-26) and kurigram 2.2.26.
- `mp_*.md`: MadelineProtoDocs copies of the MTProto method pages.

Line numbers below refer to these files.

| # | Claim (short) | Verdict | Evidence / correction |
|---|---|---|---|
| 1 | `InlineKeyboardButton.web_app` is private-chat only, not for business | **CONFIRMED** | `api.json` (10.3), `InlineKeyboardButton.web_app`: "*Available only in private chats between a user and the bot. Not supported for messages sent on behalf of a business account.*" The same text is in the Bibo `master` snapshot. |
| 2 | `KeyboardButton.web_app` is private-only; TDLib rejects it | **CONFIRMED** | `api.json`: "*Available in private chats only.*" TDLib `KeyboardButton.cpp:131-132` gives `"Web App buttons can be used in private chats only"`. The gate is `request_buttons_allowed = dialog_type == DialogType::User` (`ReplyMarkup.cpp:329`). (The cited 130-133 is off by one line on today's master.) |
| 3 | Attachment menu works in groups since 6.1 but is for major advertisers only (all bots on the test server) | **CONFIRMED** | `core.bots.webapps.v.txt:129` (6.1 changelog), `:219-220` and `:1427`, verbatim. |
| 4 | Direct-link apps launch "in any chat", support startapp, are chat-aware via `chat_type`/`chat_instance`, and have "no access to the chat" | **CONFIRMED** | `webapps.v.txt:199-202`. Also `core.bots.features.v.txt:228`: "*When opened from a direct link in a group, mini apps can also use the chat_instance parameter to track the current context, supporting shared usage by multiple chat members*". |
| 5 | A Main Mini App link `t.me/<bot>?startapp` opens "in the current chat" with the same `chat_type`/`chat_instance` context, and needs a BotFather Main Mini App | **DOWNGRADE** | The docs say this (`webapps.v.txt:175-185`). `/api/links` (`core.api.links.v.txt:1304-1306`) says the link falls back to a username link if the bot has no main app. **However, Telegram-iOS master does not pass the group for this link type** (see #7). Corrected: *documented* behaviour only. On iOS, a Main-app link tapped in a group is requested with the **bot** as peer, so the group context is very likely lost. Use the named direct link `t.me/<bot>/<short_name>?startapp=` for group binding. |
| 6 | `InlineKeyboardButton.url` has no private-chat restriction, so a bot can post a t.me direct link button into a group | **CONFIRMED** (spec level) | `api.json` `url`: "*HTTP or tg:// URL to be opened when the button is pressed…*". No chat-type restriction; only `login_url` notes "*Not supported for ephemeral messages*". I did not observe the server accepting such a message myself. |
| 7 | **All** major clients send the clicked-in chat (group) as peer of `requestAppWebView` / `requestMainWebView`, falling back to the bot | **DOWNGRADE (partly wrong)** | **`requestAppWebView` (named direct link `t.me/<bot>/<app>`)** — true for every client checked:<br>• tdesktop: `window_session_controller.cpp:711-735`, `contextPeer = item ? item->history()->peer : bot`, i.e. the chat **of the clicked message**. `bot_attach_web_view.cpp:1319-1321`.<br>• Android: `BotWebViewSheet.java:1586`, `BotWebViewAttachedSheet.java:1218`.<br>• iOS: `ChatControllerOpenWebApp.swift:739-746, 811`, `chatLocation.peerId`.<br>• tweb: `chat.ts:1500` plus `appAttachMenuBotsManager.ts:188-191`.<br>• Web A: `bots.ts:1054-1058`, `peer: selectCurrentChat() \|\| bot`.<br>• macOS (stale source): `InAppLinks.swift:852-856`.<br>• TDLib: `WebAppManager.cpp:529-536`. It falls back to the bot when the chat is inaccessible **or is a monoforum**.<br>**`requestMainWebView` (Main-app link `t.me/<bot>?startapp`)** — **not** true on all clients:<br>• tdesktop: current chat (`ResolveContext`, `bot_attach_web_view.cpp:410-427`; request at 1281-1293).<br>• Android: current chat (`MessagesController.openApp` uses `ChatActivity.getDialogId()`; `BotWebViewSheet.java:1617`).<br>• tweb: current chat (`internalLinkProcessor.ts:1377-1392` with `main: true`, then `chat.ts:1500`).<br>• Web A: current chat (`chats.ts:1935, 1968-1981`).<br>• **Telegram-iOS master: the bot.** `UrlHandling.swift:1066-1069` resolves an empty app name to `.withBotApp(botApp: nil)`. `ChatController.swift:9954-9980` (`openResolved`) then calls `openWebApp(… chatPeer: nil …)`, and `ChatControllerOpenWebApp.swift:274-275` sends `requestMainWebView(peerId: chatPeer?.id ?? botId)`.<br>• **TelegramSwift (macOS, public source 2025-07-29): the user's own peer.** `WebappBrowser.swift:40-41`, `requestMainWebView(peerId: context.peerId)`.<br>The MTProto method docs (MadelineProtoDocs mirror of core.telegram.org; REPORTED) define the contracts. `requestAppWebView.peer`: "*If the client has clicked on the link in a Telegram chat, pass the chat's peer information; otherwise pass the bot's peer information*". `requestMainWebView.peer`: "*Currently open chat, may be inputPeerEmpty*". So iOS deviates from the documented contract for Main-app links. |
| 8 | `initData.chat` (with id) is only for attachment menu and join requests, so a direct link from a group gives only `chat_type` + `chat_instance` | **CONFIRMED** (documentation level) | `webapps.v.txt:1139`, verbatim. This is the documented server behaviour; I have not observed it on the wire. A community project report (GitHub `Wladefant/super-board#425`, REPORTED via search) describes the same: direct links give `chat_type`, `chat_instance` and `start_param`, with no `chat.id`. |
| 9 | `chat_type` / `chat_instance` definitions; both first appeared 2023-04-21 (6.7) | **CONFIRMED** | `webapps.v.txt:1140-1145`, verbatim. `mirror-log` commit `797b17a` (2023-04-21 11:45 UTC) adds both rows marked `NEW`, together with the "Direct link" section. |
| 10 | `chat_instance` is the same for all members of a chat (ASSUMED) | **UNCHECKABLE** | The ASSUMED label is correct. The server computes the value, so no client code can show it. TDLib only stores the server value (`MessagesManager.cpp:11510, 11786`). `features.v.txt:228` strengthens the inference but never states equality. One indirect hint (REPORTED, bugs.telegram.org/c/42529): when Android passed the wrong peer, `chat_type` became `sender` with a *different* `chat_instance`. That suggests the value follows the peer the client sends. This makes #7 matter: on iOS, Main-app links will probably yield a per-user value, not a per-group one. |
| 11 | Undocumented: whether `initData.chat_instance` == `CallbackQuery.chat_instance`, and whether it is per-bot | **CONFIRMED** (undocumented) | I grepped all four doc pages. `chat_instance` appears only in the `CallbackQuery` and `WebAppInitData` definitions and the two "shared usage" sentences. Searches found no authoritative statement. New detail: TDLib's `td_api.tl:3474` now also exposes `chat_instance` on `Message` "*for bots only*". |
| 12 | `start_param` **is** delivered for direct-link and Main-app launches; the `WebAppInitData` table text is outdated | **CONFIRMED** | `webapps.v.txt:180, 200` (delivered) vs `:1148` (table says "*Only returned … from the attachment menu via link*"). `/api/links:1348` maps `startapp` to `requestAppWebView.start_param`. Calling the table "outdated" is a reasonable reading of an internal inconsistency in the docs. |
| 13 | HMAC: secret = HMAC-SHA256(key="WebAppData", msg=token); hash = hex HMAC(secret, dcs); dcs = all fields except `hash` (`signature` included), URL-decoded, sorted, joined by `\n` | **CONFIRMED** | The doc prose matches. Note that the doc's pseudo-code `HMAC_SHA256(<bot_token>, "WebAppData")` uses (data, key) argument order. The doc does not say explicitly that `signature` is included; two independent implementations confirm it:<br>• aiogram 3.31.0 `utils/web_app.py:122-140`: `parse_qsl` (decodes), `pop("hash")` only, sorted, `hmac.new(key=b"WebAppData", msg=token)`.<br>• `@tma.js/init-data-node` 2.0.8: `parsing-Cn-1lfce.js:49-50, 184-215`; `node.js:9-11` shows `createHmac(data, key)`. |
| 14 | Version table (8.0 fullscreen/orientation/isActive/safe-area; 7.7 swipes; 6.2 closing confirmation; 6.1 haptics/isVersionAtLeast/openLink/openTelegramLink; 6.4 platform/try_instant_view; 6.9 CloudStorage limits; 9.0 DeviceStorage 5 MB / SecureStorage 10; 9.1 hideKeyboard; base viewport; 7.0 openTelegramLink stays open; openLink needs a gesture) | **CONFIRMED** | `webapps.v.txt` changelog `:17-135`; events `:1262-1332`; `:399-407`, `:427`, `:801-819`, `:1050`, `:1069`. JS gates in `twa.js`: 597 (6.2), 607 (7.7), 629/2929-2955 (8.0), 1477 (6.1), 1523 (6.9), 1582/1646 (9.0), 3021-3026 (openLink 6.1/6.4/7.6). Minor: `hideKeyboard` has **no** JS version gate (`twa.js:3426`). The table omits `requestChat` (9.6) and the new `Serverless` object (see below). |
| 15 | Bot API 10.2 (2026-07-14) same-origin hardening, auto-enabled 2026-07-20, opt-out in the @BotFather Mini App | **CONFIRMED** | `core.bots.api.v.txt:70`, verbatim. It is in the Bot API changelog, not the Mini Apps changelog. Corroborated by MTProto `webViewResultUrl … same_origin:flags.3?true` (`telegram_api.tl:1582`) and by iOS reading `result.flags.contains(.sameOrigin)`. |
| 16 | `getChatMember` is "*only guaranteed to work for other users if the bot is an administrator in the chat*" | **CONFIRMED** | `api.json` `getChatMember.description`, verbatim. |

### Additional findings missed by the original report

1. **Use named direct links, not Main-app links, for group binding.** See #5 and #7. On Telegram-iOS master, `t.me/<bot>?startapp` tapped inside a group calls `requestMainWebView` with the **bot** as peer. The public macOS source passes the user's own peer. Group context (`chat_type=group/supergroup` and a group-level `chat_instance`) is therefore expected only with `t.me/<bot>/<short_name>?startapp=…` (VERIFIED code reading; still to be confirmed on devices).
2. **tdesktop takes the direct-link context from the clicked message.** If the link is opened outside a message, for example from an external browser or a typed `tg://` link, the context falls back to the bot (`window_session_controller.cpp:712-719`). TDLib also falls back to the bot for monoforum chats.
3. **Bot API 9.6 `WebApp.requestChat(req_id)`** (`webapps.v.txt:455`) together with `savePreparedKeyboardButton(user_id, button)` (button type `request_chat`; `api.json`) lets the Mini App ask the user to pick a group. The bot then receives a `ChatShared` with the real `chat_id`. This is an alternative or complement to `startapp`-based group binding when `chat_instance` is unreliable. The delivery path of the `chat_shared` service message is ASSUMED to be the user–bot chat.
4. **New `WebApp.Serverless` object** ("*An object for calling the endpoints of the bot's Serverless project*", marked NEW, `webapps.v.txt:336-338`). It is not in any changelog entry yet and is probably irrelevant to us, but it is noted.
5. **Bot API 10.3 replaced the 10.2 ephemeral parameters.** 10.2 had `receiver_user_id`/`callback_query_id` on `sendMessage`; 10.3 has `ephemeral_message_parameters` (`core.bots.api.v.txt`, 10.3 changelog). The report's §1 already uses the 10.3 form, which is correct.
6. **`chat_instance` and forum topics**: one value per chat, not per topic (REPORTED, `Wladefant/super-board#425`). The topic must therefore be carried in `startapp` if needed.
7. **Source freshness:**
   - The PaulSonOfLars spec and the Bibo-Joshi snapshots are current as of 2026-10-06/09.
   - `spontanurlaub/telegram-bot-api-log` stopped at 2023-10-23. It is fine for dating the 2023 change, but not for anything newer.
   - The public TelegramSwift (macOS) repo has not been updated since 2025-07-29, so the macOS findings may be out of date.
   - `telegram-web-app.js` was read from a third-party tracker, so treat its line numbers as REPORTED-grade provenance, even though two independent trackers agree.

Sources used for REPORTED items:
- [super-board issue #425](https://github.com/Wladefant/super-board/issues/425)
- [bugs.telegram.org/c/42529](https://bugs.telegram.org/c/42529)
- [MadelineProtoDocs messages.requestAppWebView](https://raw.githubusercontent.com/danog/MadelineProtoDocs/master/docs/API_docs/methods/messages.requestAppWebView.md)
- [MadelineProtoDocs messages.requestMainWebView](https://raw.githubusercontent.com/danog/MadelineProtoDocs/master/docs/API_docs/methods/messages.requestMainWebView.md)
