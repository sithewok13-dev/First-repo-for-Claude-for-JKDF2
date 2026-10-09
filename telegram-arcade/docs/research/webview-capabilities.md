# Embedded webview capabilities in Telegram clients, for an emulator-based Mini App

Area: what the webviews inside each Telegram client support for a WASM emulator with gamepad, touch and keyboard input, audio, background behaviour, WebRTC/WebCodecs, fullscreen and orientation.
Date: 2026-10-08. Author: research subagent (webview-capabilities).

Status labels:
- **VERIFIED**: I read the primary source text or code myself. The citation gives a repo path and line.
- **REPORTED**: from a secondary source, such as MDN browser-compat-data (BCD), a search summary, a bug-tracker summary or a forum post.
- **ASSUMED**: my own inference. It needs a device test.

---

## 0. Sources read, with versions

| Source | Version / date | Local copy |
|---|---|---|
| tdesktop `lib_webview` (`desktop-app/lib_webview`) | `d6e2e0b`, 2026-10-08 | the research workspace (not committed; sources are cited inline)lib_webview` |
| tdesktop app code (Mini App panel `attach_bot_webview.cpp`) | `22b352e`, 2026-10-08 (sibling clone) | the research workspace (not committed; sources are cited inline)tdesktop` |
| Telegram-iOS `submodules/WebUI` (+ Info plist) | `f1dd7a2`, 2026-10-07 | the research workspace (not committed; sources are cited inline)telegram-ios` |
| Telegram Android (DrKLO) `ui/web`, `ui/bots` | `f2908b1`, 2026-09-30, v12.10.6 (7112) | the research workspace (not committed; sources are cited inline)telegram-android` |
| Telegram for macOS (overtake/TelegramSwift) | `579cebb`, **2025-07-29**. The public repo may lag the shipping app. | the research workspace (not committed; sources are cited inline)telegram-macos` |
| Telegram Web K (`morethanwords/tweb`) | `4016d6f`, 2026-10-08 | the research workspace (not committed; sources are cited inline)tweb` |
| Telegram Web A (`Ajaxy/telegram-tt`) | `28ffcf7`, 2026-09-29 | the research workspace (not committed; sources are cited inline)telegram-tt` |
| WebKit `main` (raw files: UnifiedWebPreferences.yaml, PermissionsPolicy.cpp, NavigatorGamepad.cpp, UIGamepadProvider*.{mm,cpp}, WKContentViewInteraction.mm, WKWebView.mm, WKWebViewConfiguration.h, AudioContext.cpp, AnimationFrameRate.cpp, Document.cpp, Page.cpp, WebKitSettings.cpp, OptionsGTK.cmake, PlatformEnableCocoa.h) | fetched 2026-10-08 | the research workspace (not committed; sources are cited inline)webkit/` |
| Chromium `main` (raw files: permissions_policy_features.json5, aw_main_delegate.cc, AwContents.java, AwSettings.java, aw_settings.cc, WebViewChromium.java, ContentUiEventHandler.java, GamepadList.java, autoplay_policy.cc, audio_context.cc) | fetched 2026-10-08 | the research workspace (not committed; sources are cited inline)chromium/` |
| Gecko (`mozilla-firefox/firefox` main): PermissionsPolicyUtils.cpp, Navigator.cpp | fetched 2026-10-08 | the research workspace (not committed; sources are cited inline)gecko/` |
| MDN browser-compat-data (has `webview_android` and `webview_ios` columns) | 8.1.5, 2026-10-08 | the research workspace (not committed; sources are cited inline)bcd/package/data.json` |
| Mini App docs (`core.telegram.org/bots/webapps` snapshot, 2026-10-06) and `telegram-web-app.js` (prod, changed 2026-10-07) | sibling copies | the research workspace (not committed; sources are cited inline)txt/core.bots.webapps.txt`, `.../twajs/telegram-web-app.prod.js` |

Current engine versions, from BCD (REPORTED): Safari iOS / WKWebView 27 (2026-09-14); Android WebView 155 (2026-10-06).

---

## 1. Which engine each client uses

| Client | Engine | Evidence |
|---|---|---|
| Telegram iOS | `WKWebView` subclass `WebAppWebView` | VERIFIED `submodules/WebUI/Sources/WebAppWebView.swift:136-201` |
| Telegram Android | Android System WebView (`MyWebView extends WebView`) | VERIFIED `ui/web/BotWebViewContainer.java:431-510` |
| Telegram Desktop, Windows | **WebView2** (Edge Chromium) when the runtime is present. Falls back to legacy **EdgeHTML** only if WebView2 is missing. No webview at all before Windows 8.1. | VERIFIED `lib_webview/webview/platform/win/webview_win.cpp:31-50,66-74` |
| Telegram Desktop, macOS | `WKWebView` | VERIFIED `lib_webview/webview/platform/mac/webview_mac.mm:782-835` |
| Telegram Desktop, Linux | **WebKitGTK**: `libwebkitgtk-6.0.so.4`, else `libwebkit2gtk-4.1.so.0`, else `-4.0.so.37`, loaded at runtime from the distro. Runs in a separate helper process. Mini Apps can open as an "external" window. | VERIFIED `webview_linux_webkitgtk_library.cpp:14-16`; tdesktop `attach_bot_webview.cpp:2170-2190` (`WindowMode::External` when `_externalShell`) |
| Telegram for macOS (native Swift app) | `WKWebView` with default configuration | VERIFIED `Telegram-Mac/WebpageModalController.swift:1165-1199` |
| Telegram Web K / Web A | Cross-origin `<iframe>` inside whatever browser the user runs | VERIFIED tweb `src/components/telegramWebView.ts:29-33`, telegram-tt `WebAppTab.tsx:1236-1248` |

Mini Apps in tdesktop are **not** created in lib_webview's "restricted" mode. That mode mutes audio, turns off WebRTC and turns on Lockdown Mode on macOS. tdesktop's Mini App `WindowConfig` sets no `restrictedOrigin` (VERIFIED `attach_bot_webview.cpp:2170-2190`). The restricted settings therefore do not apply to us.

---

## 2. Summary matrix

Legend: Y = works, N = does not work, ? = needs a device test. Each cell gives the status of the claim behind it.

| Capability | iOS (WKWebView) | Android (WebView) | Desktop Win (WebView2) | Desktop macOS (WKWebView) | Desktop Linux (WebKitGTK) | Web K / Web A (iframe) |
|---|---|---|---|---|---|---|
| Gamepad API | Y, but only for the first-responder WKWebView (VERIFIED code). Off in Lockdown Mode (VERIFIED). iOS 17 bug (REPORTED) | Y. Key events reach `GamepadList` (VERIFIED). BCD lists 37+ (REPORTED) | Y (REPORTED) | Y, only if the WKWebView is first responder of the key window (VERIFIED code) | Y, if built with libmanette and the GTK toplevel is active (VERIFIED code). Distro builds may differ (ASSUMED) | Y. Default allowlist is `*` in Blink, WebKit and Gecko, so no `allow="gamepad"` is needed (VERIFIED) |
| WASM + JIT | Y. Lockdown Mode disables JIT/Wasm (REPORTED) | Y | Y | Y | Y | Y |
| WASM SIMD | iOS 16.4+ (REPORTED) | 91+ (REPORTED) | Y (Chromium) | Safari 16.4+ engine (REPORTED) | ? version-dependent | browser-dependent |
| SharedArrayBuffer / wasm threads | Possible with COOP+COEP on iOS 15.2+ (REPORTED). `credentialless` is not supported, so use `require-corp` (REPORTED) | **N**: WebView never reaches `crossOriginIsolated` (REPORTED: BCD + crbug 40914606) | ? top-level COOP/COEP should work (ASSUMED) | ? (ASSUMED yes) | ? | **N**: the iframe lacks `allow="cross-origin-isolated"` (VERIFIED) and that feature defaults to `self` (VERIFIED) |
| AudioWorklet | 14.5+ (REPORTED) | 66+ (REPORTED) | Y | Y | ? | Y |
| Web Audio autoplay without a tap | Probably Y: Telegram sets `mediaTypesRequiringUserActionForPlayback = []` (VERIFIED), and WebKit then drops the AudioContext gesture rule (VERIFIED code) | Y in the main frame: the Chromium `kUserGestureRequired` path only gates cross-origin frames (VERIFIED code) | ? (ASSUMED a gesture is needed) | Y. The WebKit non-iOS default needs no gesture (VERIFIED pref default) | Y (WebKitGTK default false, VERIFIED) | **N** until the user taps inside the iframe. No `allow="autoplay"` (VERIFIED) |
| Silent switch mutes Web Audio | Yes by default (REPORTED). Mitigate with `navigator.audioSession.type='playback'` (16.4+, REPORTED) plus a looping `<audio>` fallback (REPORTED) | n/a | n/a | n/a | n/a | same as Safari when on iOS |
| HTML Fullscreen API (`element.requestFullscreen`) | **N**: Telegram does not enable element fullscreen, and the WebKit default is off (VERIFIED) | **N**: Telegram's `WebChromeClient` lacks `onShowCustomView`, so WebView reports fullscreen as unsupported (VERIFIED code path) | Y, fills the webview area (ASSUMED) | **N** (VERIFIED default off) | Y (GTK default on; lib_webview handles it, VERIFIED) | Y on desktop (`allowfullscreen`, VERIFIED). N on iPhone Safari (REPORTED) |
| `Telegram.WebApp.requestFullscreen()` | Y (VERIFIED handler) | Y (VERIFIED handler) | Y (VERIFIED handler) | Y | Y | Y if the browser allows it (VERIFIED handler) |
| `screen.orientation.lock()` | N (WebKit locking API default false, VERIFIED) | **N**: WebView passes `--disable-screen-orientation-lock` (VERIFIED) | N (desktop Chrome always throws, REPORTED) | N | N | N on desktop. Web K's sandbox also lacks `allow-orientation-lock` (VERIFIED) |
| `Telegram.WebApp.lockOrientation()` (8.0+) | Y, locks the current orientation (VERIFIED) | Y, locks the current orientation (VERIFIED) | n/a | n/a | n/a | n/a |
| `disableVerticalSwipes()` (7.7+) | Y (VERIFIED) | Y (VERIFIED) | n/a | n/a | n/a | n/a |
| Pinch zoom blocked | Y with viewport `user-scalable=no`: `ignoresViewportScaleLimits` defaults to NO (VERIFIED) | Y: Telegram never enables `builtInZoomControls` for bots (VERIFIED) | page zoom only | page zoom only | page zoom only | use `touch-action:none` |
| Long-press callout / selection | Already suppressed by Telegram's injected CSS. Drag interaction removed (VERIFIED) | Must suppress yourself (ASSUMED) | n/a | n/a | n/a | Must suppress yourself |
| WebRTC `RTCPeerConnection` | Y. Since iOS 11; `getUserMedia` since 14.3 (REPORTED) | Y (REPORTED BCD) | Y (non-restricted mode keeps it, VERIFIED) | Y (non-restricted mode keeps it, VERIFIED) | **N**: WebKitGTK default off, often not compiled in, and lib_webview does not enable it (VERIFIED) | Y |
| WebCodecs `VideoDecoder` | 16.4+ (REPORTED) | 94+ (REPORTED) | Y | Y | GStreamer builds Y (VERIFIED pref); WebKitGTK ≥2.44 (REPORTED) | browser-dependent |
| `activated` / `deactivated` (Telegram minimize) | Y (VERIFIED) | Y (VERIFIED) | **N**: tdesktop never sends `visibility_changed` (VERIFIED by grep) | **N** | **N** | Y (VERIFIED) |
| Web inspector for debugging | Safari Web Inspector (`isInspectable = true`, iOS 16.4+, VERIFIED) | chrome://inspect when Telegram's debug-webview setting is on (VERIFIED) | `webview-debug-enabled` option (VERIFIED) | same option | same option | browser devtools |

---

## 3. Details by topic

### 3.1 Gamepad API

**Telegram Web (iframes).**
- VERIFIED: Web K's Mini App iframe uses `allow: 'camera; microphone; geolocation; accelerometer; gyroscope; magnetometer; device-orientation; clipboard-write;'` and `allowFullscreen = true` (tweb `src/components/webApp.tsx:1063,1068`). The sandbox is `allow-scripts allow-same-origin allow-popups allow-forms allow-modals allow-storage-access-by-user-activation` (`webApp.tsx:57-64`). The attribute does **not** include `gamepad`, `autoplay` or `cross-origin-isolated`.
- VERIFIED: Web K's **HTML5 Games** iframe (`openGameInAppBrowser`) is more permissive. It uses `allow='accelerometer; gyroscope; magnetometer; gamepad; fullscreen; autoplay; clipboard-write;'` and adds `allow-orientation-lock allow-pointer-lock` to the sandbox (tweb `src/components/browser.tsx:737-748`).
- VERIFIED: Web A's Mini App iframe uses `allow='camera; microphone; geolocation; clipboard-write; web-share; screen-wake-lock;'`. Its sandbox adds `allow-pointer-lock allow-orientation-lock`, and it sets `allowFullScreen` (telegram-tt `src/util/browser/iframe.ts:1-13`, `WebAppTab.tsx:1245-1247` (lines 1244-1247 hold sandbox/allow/allowFullScreen)). Web A's game iframe has only `allow="fullscreen"` (`src/components/main/GameModal.tsx:83-91`).
- VERIFIED: the **default allowlist for `gamepad` is `*`** in all three engines:
  - Chromium: `permissions_policy_features.json5:361-364`, `feature_default: "EnableForAll"`
  - WebKit: `PermissionsPolicy.cpp:227-233`, Gamepad maps to `"*"`
  - Gecko: `PermissionsPolicyUtils.cpp:43`, `{"gamepad", eAll}`

  So **a cross-origin Mini App iframe does not need `allow="gamepad"`**. WebKit does enforce the policy: `getGamepads()` throws a SecurityError when it is disabled (`NavigatorGamepad.cpp:90-91`). That could only bite if web.telegram.org sent a `Permissions-Policy: gamepad=()` header. I could not check the production headers because web.telegram.org is blocked from this container, so this needs a test.

**iOS (WKWebView).**
- VERIFIED: the Gamepad API is compiled in on iOS (`PlatformEnableCocoa.h:388-389`) and `GamepadsEnabled` defaults to true for WebKit (`UnifiedWebPreferences.yaml:2563-2574`). No entitlement or WKWebViewConfiguration flag is involved.
  - REPORTED: in WebKit bug 269292, adding the "Game controllers" capability made no difference.
  - REPORTED (BCD): `getGamepads` has shipped since iOS 10.3 in both Safari iOS and `webview_ios`. "iOS 14.5" is when the GameController framework gained newer controllers such as the PS5 and Xbox Series pads. That is general knowledge (REPORTED), not a WKWebView gate.
- VERIFIED, and the key constraint: WebKit sends gamepad input only to the page whose `WKContentView` is the **first responder of the key window** (`UIGamepadProviderIOS.mm:38-50`). A **single tap** on the web content makes WKContentView first responder (`WKContentViewInteraction.mm:4134`, in `_singleTapRecognized`). Current WebKit calls `viewBecameActive` / `viewBecameInactive` on first-responder changes (`WKContentViewInteraction.mm:2074,2194`).
- REPORTED (WebKit bug 269292 and a May 2024 webkit-changes commit): on iOS 17.x, WKWebView apps (Chrome for iOS, Cordova and others) got an empty `getGamepads()` unless the view was already first responder when the page loaded. The fix landed in May 2024, so ASSUMED it shipped in iOS 18 or later.
- VERIFIED: Telegram iOS never calls `becomeFirstResponder` on the web view (no match in `submodules/WebUI`). We depend on the user tapping the page.
- ASSUMED: if the game calls `preventDefault()` on every touch, WebKit's tap recognizer may never fire, so the view never becomes first responder and the pad stays silent. Mitigation: a normal "Tap to start / connect controller" button with no `preventDefault`.
- VERIFIED: `GamepadsEnabled` is `disableInLockdownMode: true` (`UnifiedWebPreferences.yaml:2572`). So are `WebAudioEnabled`, `WebGLEnabled` and `PeerConnectionEnabled`. **Users in Lockdown Mode cannot play** unless they exclude Telegram from it.

**Android (System WebView).**
- VERIFIED: `AwContents` sends every key event through `GamepadList.dispatchKeyEvent(event)` before normal handling (`AwContents.java:4910-4925`). Generic motion (stick) events go through `ContentUiEventHandler.onGenericMotionEvent`, which calls `Gamepad.from(webContents).onGenericMotionEvent` (`ContentUiEventHandler.java:84-86,149-150`).
- REPORTED (BCD): `navigator.getGamepads` in `webview_android` since 37.
- ASSUMED: Android only delivers key events to the focused view, so the WebView needs focus. A tap normally gives it focus. Telegram's `updateKeyboardFocusable()` is hard-wired to `focusable=false` (`BotWebViewContainer.java:570-592`, `keyboardFocusable && isPageLoaded && false`), but it never ran with `true`, so it should not block descendants. This needs a device test.

**Windows (WebView2).** REPORTED: Chromium's gamepad backends (XInput, WGI, RawInput) work in WebView2. There are reports that the Steam Overlay and `allow-host-input-processing` interfere. Focus or visibility requirements: ASSUMED; needs a test.

**macOS (tdesktop and Telegram for macOS).** VERIFIED: gamepad input goes only to the WKWebView that is first responder of `[NSApp keyWindow]` (`UIGamepadProviderMac.mm:39-52`). lib_webview's mac `Instance::focus()` is empty (`webview_mac.mm:1298-1299`), so the user must click into the page.

**Linux (WebKitGTK).** VERIFIED:
- `ENABLE_GAMEPAD` defaults ON for the GTK port and requires libmanette (`OptionsGTK.cmake:99,266-271`).
- Input goes only to a visible WebKitWebView inside a GTK toplevel that is active (`UIGamepadProviderGtk.cpp:89-101`).

ASSUMED: this works best in tdesktop's "external" Mini App window mode, where the WebKitGTK window is a real toplevel. Embedded mode goes through a nested compositor, and the GTK window may never be "active".

### 3.2 WebAssembly, JIT, SIMD, threads, cross-origin isolation

- **JIT.** WKWebView runs JavaScriptCore with JIT in the WebContent process (REPORTED, long-standing). Android WebView and WebView2 use V8 with JIT. Lockdown Mode disables JIT, WebAssembly and WebGL. That is REPORTED in lib_webview's own source comment (`webview_mac.mm:132-140`); WebGL is VERIFIED in the WebKit prefs.
- **SIMD** (REPORTED, BCD `webassembly.fixed-width-SIMD`): Safari iOS / WKWebView 16.4+, Android WebView 91+, Chrome 91+. Relaxed SIMD is not in Safari. Ship a non-SIMD fallback, or use SIMD only on platforms that support it, and keep both builds bit-exact (see §4).
- **Threads / SharedArrayBuffer.**
  - **Android WebView: no.** BCD lists `SharedArrayBuffer`, `webassembly.threads-and-atomics` and the `Cross-Origin-Opener-Policy` header as `false` for `webview_android` (REPORTED). Chromium issue 40914606 is described as "SharedArrayBuffer is unavailable in Android WebView because crossOriginIsolated is false", with WebView's single renderer process per app as the reason (REPORTED). **Telegram Android is the largest mobile platform, so the core must be single-threaded.**
  - **Telegram Web: no.** Chromium's `cross-origin-isolated` feature has no `feature_default`, which means `EnableForSelf` (`permissions_policy_features.json5:32-33,289-291`, VERIFIED). WebKit also sets it to `'self'` (`PermissionsPolicy.cpp:258`, VERIFIED). Neither Web K nor Web A puts it in `allow` (VERIFIED). The parent would also have to be cross-origin isolated, which I could not check (ASSUMED not).
  - **iOS WKWebView**: COOP/COEP and SAB since 15.2 (REPORTED, BCD). `COEP: credentialless` is not supported in Safari or WKWebView (REPORTED, BCD), so `require-corp` would mean self-hosting `telegram-web-app.js` and every asset with CORP/CORS. Whether WKWebView in Telegram actually reaches `crossOriginIsolated === true`: ASSUMED yes; needs a test.
  - WebView2 / macOS WKWebView / WebKitGTK as top-level documents: ASSUMED possible; needs a test.
  - Conclusion: build the emulator core **without pthreads**, and treat threads as an optional speed-up at most.
- **Frame pacing.** VERIFIED (WebKit `AnimationFrameRate.cpp:33`, `Page.cpp:556-561,3075-3085`, `Document.cpp:9454-9455`): WebKit halves `requestAnimationFrame` to 30 fps in any of these cases:
  - iOS Low Power Mode
  - aggressive thermal mitigation
  - a cross-origin iframe the user has not interacted with yet (Telegram Web on Safari)

  WebKit also prefers ~60 fps on 120 Hz screens (`PreferPageRenderingUpdatesNear60FPSEnabled`, VERIFIED pref). **The emulator tick must not be driven by rAF.** Drive it from a time accumulator or the audio clock, and use rAF only to present.
- Android user agent (VERIFIED `BotWebViewContainer.java:489-499`): Telegram appends `Telegram-Android/<ver> (<Manufacturer Model>; Android <rel>; SDK <n>; LOW|AVERAGE|HIGH)`. That is a free device-performance hint.
- Android rendering (VERIFIED `BotWebViewContainer.java:455-457`): Telegram sets `LAYER_TYPE_HARDWARE` on the WebView unless a server flag disables it. ASSUMED: this may add a composition pass; profile it.

### 3.3 Web Audio: unlock rules and the iOS silent switch

- **iOS Telegram.** VERIFIED:
  - `configuration.allowsInlineMediaPlayback = true` and `mediaTypesRequiringUserActionForPlayback = []` (`WebAppWebView.swift:178-184`).
  - WebKit turns that into `setRequiresUserGestureForAudioPlayback(false)` (`WKWebView.mm:829-831`).
  - `AudioContext` adds `RequireUserGestureForAudioStartRestriction` only when `page->requiresUserGestureForAudioPlayback()` (`AudioContext.cpp:156-163`).

  ASSUMED: an AudioContext can start without a tap in Telegram iOS. The page-consent restriction still applies on Cocoa (`AudioContext.cpp:161-162`).
- **Android.** VERIFIED:
  - WebView maps `mediaPlaybackRequiresUserGesture` to `kUserGestureRequired` (`aw_settings.cc:695-698`).
  - Under that policy, `AudioContext` requires a gesture **only if the frame is cross-origin to the main frame** (`audio_context.cc:1483-1498`). A Mini App is the main frame, so Web Audio is not gated.
  - Telegram sets `setMediaPlaybackRequiresUserGesture(true)` on every page start and `false` on the first `ACTION_DOWN` (`BotWebViewContainer.java:4347,5192`). For `<video>`/`<audio>` autoplay, which matters for the streaming design, the first touch therefore unlocks.
- **Telegram Web.** VERIFIED: Chromium's `IsDocumentAllowedToPlay` (`autoplay_policy.cc:81-117`) walks from the iframe upward. It accepts sticky activation on the iframe itself. It looks at ancestors only while the `autoplay` policy is enabled, and that policy defaults to `self` (`permissions_policy_features.json5:134-135`). The Web K/A iframes do not delegate it. Result: **the AudioContext stays suspended until the user taps inside the Mini App.** The click on the bot button in the parent page does not count.
- **Recommendation.** Always show a "tap to join/spectate" overlay that creates or `resume()`s the AudioContext inside the handler. It is harmless where unlock isn't needed and required on Web, and it also wakes iOS gamepads (§3.1).
- **iOS silent switch.** REPORTED:
  - Web Audio uses the ambient audio-session category, which the ring/silent switch mutes. `<audio>`/`<video>` use playback, which it doesn't.
  - `navigator.audioSession.type = 'playback'` (Safari / WKWebView 16.4+; BCD `api.Navigator.audioSession`; WebKit `DOMAudioSessionEnabled` defaults to true, VERIFIED `UnifiedWebPreferences.yaml:1738-1747`) makes Web Audio ignore the switch in Safari.
  - One developer reports it was not enough on its own in WKWebView and used a hidden looping near-silent `<audio>` started on the same tap.

  Because Telegram sets `mediaTypesRequiringUserActionForPlayback = []`, that fallback element can play. Do both, then test on a device with the switch on.
- **Background audio on iOS.** VERIFIED: Telegram iOS declares `UIBackgroundModes` `audio, fetch, location, remote-notification, voip` (`Telegram/Telegram-iOS/InfoBazel.plist:139-146`). REPORTED: WKWebView Web Audio usually stops or freezes in the background regardless (Apple dev forum 121822; WebKit bugs 237878 and 240646, where an AudioContext reports "running" but is silent after resume). Plan to rebuild or resume the AudioContext on `visibilitychange` → visible.
- **Desktop.** VERIFIED: WebKit's non-iOS default is `RequiresUserGestureForAudioPlayback: false` (`UnifiedWebPreferences.yaml:5145-5152`), so macOS WKWebView and WebKitGTK allow it. WebView2's autoplay policy is not configured by tdesktop (VERIFIED: no autoplay args, `webview_windows_edge_chromium.cpp:1008-1013`); its effective default is ASSUMED.

### 3.4 Touch, pointer, gestures, keyboard, viewport

- Pointer Events, `setPointerCapture`, `touch-action`, `visualViewport` (REPORTED, BCD): PointerEvent iOS 13+, WebView 55+; `touch-action` iOS 9.3+ (manipulation), full support iOS 13+; VisualViewport iOS 13+, WebView 61+. Use `touch-action: none` on the controls overlay and canvas, and pointer events with `setPointerCapture` per finger.
- **Pinch / double-tap zoom.**
  - iOS: `ignoresViewportScaleLimits` defaults to NO (`WKWebViewConfiguration.h:199-203`, VERIFIED) and Telegram does not change it (VERIFIED), so `<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">` is honoured.
  - Android: pinch requires `supportZoom && builtInZoomControls`, and double-tap also requires `useWideViewport` (`AwSettings.java:2124-2132`, VERIFIED). Telegram only sets those for non-bot webviews (`BotWebViewContainer.java:470-482`, VERIFIED), so zoom is off for Mini Apps.
- **Long-press.**
  - iOS: Telegram injects `*{-webkit-touch-callout:none} :not(input):not(textarea):not([contenteditable]){-webkit-user-select:none}` into the main frame (`WebAppWebView.swift:73-76,170-171`, VERIFIED). It also removes WKContentView's `UIDragInteraction` (`WebAppWebView.swift:258-272`, VERIFIED).
  - Android, Web and desktop (ASSUMED): add the same CSS yourself, plus `contextmenu` `preventDefault`.
- **Swipe-to-close / minimize.**
  - `Telegram.WebApp.disableVerticalSwipes()` (Bot API 7.7+) sends `web_app_setup_swipe_behavior`. iOS sets `_isPanGestureEnabled` (`WebAppController.swift:1635-1638`, VERIFIED); Android calls `delegate.onWebAppSwipingBehavior` (`BotWebViewContainer.java:1809-1816`, VERIFIED).
  - The docs say the user can still close or minimize by swiping the header (VERIFIED `core.bots.webapps.txt:524-526`).
  - Also call `enableClosingConfirmation()`.
  - iOS (VERIFIED): `interactiveTransitionGestureRecognizerTest = { point.x > 30 }` (`WebAppWebView.swift:194-196`; semantics in `Display/Source/InteractiveTransitionGestureRecognizer.swift:10-21`). Horizontal content gestures win everywhere **except the leftmost 30 pt**, where Telegram's interactive back/dismiss gesture can still start. Keep controls out of that strip, or test it.
  - Android (ASSUMED): system gesture navigation (back swipe from the screen edges) cannot be excluded from web content.
- **Keyboard.**
  - VERIFIED: WebKit iOS key events go to WKContentView as first responder (same mechanism as gamepads). Telegram iOS returns `nil` for `inputAccessoryView` and removes keyboard-frame observers (`WebAppWebView.swift:258-280,350-352`).
  - ASSUMED: a physical keyboard on iPad and Android works after a tap into the page. Telegram's own `UIKeyCommand`s or the Android back key may capture some keys (Esc, Cmd-combinations, Back). Test them.
  - Desktop webviews get normal keyboard focus once the user clicks.
  - Telegram Web: key events go to the iframe only while it has focus.
- **Text chat in the Mini App.** VERIFIED: iOS removes WKWebView's own keyboard observers and has Telegram scroll to the active element (`WebAppWebView.swift:258-280,316-340`). Use `viewportChanged` / `visualViewport` for layout while the on-screen keyboard is up, and test the chat input during play.

### 3.5 Background lifecycle

- **Telegram minimize** fires `activated` / `deactivated` (Bot API 8.0). `telegram-web-app.js` maps them from the client event `visibility_changed {is_visible}` (`telegram-web-app.prod.js:397-405`, VERIFIED). Who sends it (VERIFIED):
  - iOS on `isMinimized` change (`WebAppController.swift:4192-4206`)
  - Android in `preserveWebView()` / `replaceWebView()` (`BotWebViewContainer.java:1113-1118,373`)
  - Web K on collapse (`browser.tsx:732`; `webApp.tsx:1387-1388`)
  - Web A (`WebAppTab.tsx:412-419`)
  - **tdesktop: never.** No `visibility_changed` anywhere in `inline_bots`, `ui/chat/attach`, `window`, `core` or `api`.
- **Android minimize.** VERIFIED: Telegram calls `webView.onPause()` but keeps `pauseTimers()` commented out (`BotWebViewSheet.java:338-339`, also `BotWebViewAttachedSheet.java:263-264`). `AwContents.onPause()` updates WebContents visibility (`AwContents.java:3110-3118`), so the page becomes `hidden`: rAF stops and timers are throttled, but JS keeps running. Window-visibility changes (app to background) also hide the page (`AwContents.java:5127-5131,3849-3858`, VERIFIED).
- **WebKit (iOS / macOS / GTK).** VERIFIED: hidden-page DOM timer throttling and CSS-animation suspension are on by default for Cocoa and GTK (`UnifiedWebPreferences.yaml:2762-2789`).
  - REPORTED/ASSUMED: when Telegram iOS goes to the background or the phone locks, the WebContent process is suspended after a short grace period. Timers stop, the WebSocket goes stale or drops, and audio stops.
  - ASSUMED: while minimized inside Telegram iOS the WKWebView may still count as "visible". Measure rAF and timer rates in that state.
- **Chromium (Web, WebView2).** REPORTED general behaviour: hidden pages get 1 Hz timer alignment and, after about 5 minutes, intensive throttling (about 1 wake-up per minute for chained timers). Pages playing audio are exempt in Chrome.
- **Design consequence.** Treat `visibilitychange: hidden`, `deactivated`, `pagehide` or WebSocket close as "client paused":
  - Server-side seat timeouts and hand-off.
  - On resume: reconnect, fetch the latest savestate or authoritative input log, fast-forward, then rebuild the AudioContext.
  - Do not count on any client keeping time in the background.
  - `freeze` / `resume` events exist only in Chromium (REPORTED, BCD).

### 3.6 WebRTC and WebCodecs

- **iOS WKWebView.** REPORTED: `RTCPeerConnection` and data channels have worked in WKWebView since iOS 11; `getUserMedia` since 14.3. VERIFIED: the WebKit `PeerConnectionEnabled` default on Cocoa is `webRTCAvailable()` (`UnifiedWebPreferences.yaml:4716-4730`, `WebPreferencesDefaultValues.cpp:298-302`). Telegram iOS does not disable it. Off in Lockdown Mode (VERIFIED).
- **Android WebView.** REPORTED (BCD): `RTCPeerConnection` 56+, `RTCDataChannel`. Telegram's `onPermissionRequest` only covers camera and mic (`BotWebViewContainer.java:4811-4936`, VERIFIED); data-only or receive-only WebRTC needs no permission (ASSUMED).
- **WebView2 / macOS tdesktop.** VERIFIED: WebRTC is restricted (`--force-webrtc-ip-handling-policy=disable_non_proxied_udp`, `peerConnectionEnabled=NO`) **only in restricted mode** (`webview_windows_edge_chromium.cpp:1008-1013`, `webview_mac.mm:141-153`). Mini Apps don't use that mode, so WebRTC is available.
- **Linux tdesktop (WebKitGTK).** VERIFIED: WebRTC is unavailable.
  - WebKitGTK's `enable-webrtc` default comes from `PeerConnectionEnabled`, which is `false` on non-LIBWEBRTC WebKit (`WebKitSettings.cpp:1680-1700`; `UnifiedWebPreferences.yaml:4722-4726`).
  - The GTK port only compiles WebRTC when `ENABLE_EXPERIMENTAL_FEATURES` is set (`OptionsGTK.cmake:140`).
  - lib_webview never calls `webkit_settings_set_enable_webrtc(..., true)`. It only sets it to false in restricted mode (`webview_linux_webkitgtk.cpp:156-181,1304-1313`).
  - So **the streaming design (a) needs a non-WebRTC fallback for Linux desktop**, for example WebSocket + WebCodecs or MSE.
- **WebCodecs `VideoDecoder`.** REPORTED (BCD): Safari / WKWebView 16.4+, WebView 94+, Chrome 94+. `AudioDecoder` only arrived in Safari 26. WebKitGTK: VERIFIED `WebCodecsVideoEnabled` is true for `USE(GSTREAMER)` (`UnifiedWebPreferences.yaml:7123-7142`, `OptionsGTK.cmake:139`). The 2.44 release added it (REPORTED). Hardware decoder availability on Android and Linux varies (ASSUMED).
- Other (REPORTED, BCD): WebTransport reached Safari / WKWebView 26.4 (Chrome 97). `requestVideoFrameCallback` iOS 15.4+. WebGL2 iOS 15+. OffscreenCanvas iOS 16.4+.

### 3.7 Fullscreen and orientation

- **HTML Fullscreen API.**
  - iOS: VERIFIED that the WebKit default `FullScreenEnabled` is false except GTK/WPE (`UnifiedWebPreferences.yaml:2488-2500`), and Telegram iOS doesn't set `isElementFullscreenEnabled`. It also injects a script forcing `playsinline` on videos (`WebAppWebView.swift:78-118`). BCD: `webview_ios: false`.
  - Android: VERIFIED. `WebViewChromium.setWebChromeClient` calls `setFullscreenSupported(doesSupportFullscreen(client))`, which checks for `onShowCustomView`/`onHideCustomView` overrides (`WebViewChromium.java:2090,2119-2137`). The value reaches `web_prefs->fullscreen_supported` (`aw_settings.cc:745-746`). Telegram's anonymous `WebChromeClient` (`BotWebViewContainer.java:4474`) has no such override, so element fullscreen is unsupported.
  - tdesktop macOS: VERIFIED off (lib_webview comment `webview_mac.mm:137-138`; no `elementFullscreenEnabled`).
  - tdesktop Linux: VERIFIED that WebKitGTK `enable-fullscreen` defaults to true and lib_webview tracks fullscreen changes.
  - WebView2: ASSUMED it fills the webview area. lib_webview has no `ContainsFullScreenElementChanged` handler (VERIFIED by grep).
  - Telegram Web: `allowfullscreen` is set (VERIFIED), which WebKit maps to Fullscreen for all origins (`PermissionsPolicy.cpp:397-398`). Not on iPhone Safari (REPORTED, BCD: iPad only).
- **Telegram fullscreen.** Use `Telegram.WebApp.requestFullscreen()` / `exitFullscreen()` (Bot API 8.0). VERIFIED handlers in each client:
  - iOS `WebAppController.swift:1713-1716`
  - Android `BotWebViewContainer.java:2595-2617`, replies `fullscreen_changed` / `fullscreen_failed`
  - tdesktop `attach_bot_webview.cpp:2371-2388`
  - Web K `webApp.tsx:1293-1310`, built on the browser Fullscreen API of the parent container
  - Web A `WebAppTab.tsx:661-677`, can reply `fullscreen_failed UNSUPPORTED`

  Listen for `fullscreenChanged` / `fullscreenFailed` and use `safeAreaInset` / `contentSafeAreaInset`. ASSUMED: Web K's parent-side `requestFullscreen()` relies on user activation propagating from the iframe click, so call it from a tap handler.
- **Orientation.**
  - `screen.orientation.lock()` is unusable. Android WebView appends `kDisableScreenOrientationLock` (`aw_main_delegate.cc:139-140`, VERIFIED; BCD's "38" for webview_android is contradicted by this source). WebKit's `ScreenOrientationLockingAPIEnabled` defaults to false (`UnifiedWebPreferences.yaml:5316-5322`, VERIFIED). Desktop Chrome always throws (REPORTED).
  - Use `Telegram.WebApp.lockOrientation()` / `unlockOrientation()` (Bot API 8.0). This **locks the current orientation** (docs VERIFIED `core.bots.webapps.txt:642-648`; iOS `WebAppController.swift:1769-1772`; Android `BotWebViewContainer.java:2902-2911` → `BotWebViewSheet.lockOrientation`, which calls `AndroidUtilities.lockOrientation(activity)`).
  - To play in landscape: ask the user to rotate, then lock.

---

## 4. Implications for the architecture decision

- **(b) Lockstep WASM replicas.** Everything it needs exists on every client: WASM with JIT, Web Audio/AudioWorklet, WebSocket and IndexedDB. The binding constraints:
  1. **Single-threaded core.** There is no SAB/threads on Telegram Android or Telegram Web.
  2. **SIMD only behind feature detection.** Determinism across SIMD and scalar builds must be verified by the state hashes the design already plans, or else ship one build everywhere.
  3. **Emulation clock separate from rAF.** WebKit can drop rAF to 30 fps.
  4. **Every client may be paused or suspended at any time** (backgrounding, minimize, lock). Savestate resync on resume is mandatory, not an edge case.
  5. Lockdown Mode iOS users cannot run it.
- **(a) Server-side emulation + streaming** needs WebRTC (missing on tdesktop Linux) or WebCodecs (iOS 16.4+, and the WebKitGTK version varies) and video autoplay (needs the first touch on Android and Web). Telegram iOS's `mediaTypesRequiringUserActionForPlayback=[]` and `playsinline` injection help. It avoids the single-thread CPU limit on weak Android phones, but costs server CPU/GPU and adds latency.
- Both designs: input handling (gamepad focus, touch, keyboard) and lifecycle handling are identical, and they are the larger cross-client risk.

---

## 5. Concrete implementation guidance

1. Start screen: one big plain `<button>` ("Tap to play / spectate"). In its click handler, do the following, in this order:
   - Create or resume the `AudioContext`.
   - Set `navigator.audioSession.type='playback'` if present.
   - Start a looping silent `<audio>` on iOS.
   - Call `Telegram.WebApp.disableVerticalSwipes()`, `enableClosingConfirmation()` and `expand()`. Call `requestFullscreen()` if the user wants it.

   This gives the iOS WKContentView first responder (gamepad + keyboard), satisfies the Web autoplay rule, and lifts Safari's cross-origin rAF throttle.
2. CSS:
   - `touch-action:none; user-select:none; -webkit-user-select:none; -webkit-touch-callout:none; overscroll-behavior:none` on the game root.
   - Viewport meta with `user-scalable=no, viewport-fit=cover`.
   - Honour `--tg-safe-area-inset-*` / `--tg-content-safe-area-inset-*`.
   - Keep on-screen controls out of the leftmost 30 pt on iOS, and away from the screen edges on Android.
3. Input: Pointer Events with per-pointer capture for the virtual pad. Poll `navigator.getGamepads()` every tick (snapshots, not cached objects). Show a "press any button" hint, because pads are hidden until a button press. Use `KeyboardEvent.code` mapping.
4. Timing: a fixed 60 Hz (or the core's native rate) accumulator driven by `performance.now()` and/or AudioWorklet consumption. Render on rAF. Never use a `requestAnimationFrame`-count clock.
5. Lifecycle:
   - Listen to `visibilitychange`, `pagehide`, `Telegram.WebApp.onEvent('activated'|'deactivated')` and WebSocket `close`.
   - On hide: tell the server (best effort) and stop audio.
   - On show: reconnect, resync from savestate, rebuild audio.
6. COOP/COEP: optional. Only iOS and desktop could benefit. If used, self-host `telegram-web-app.js` (COEP `require-corp`; Safari lacks `credentialless`).
7. Detect capabilities at runtime and report them to the server, so the room knows who can be a player:
   - `crossOriginIsolated`
   - `WebAssembly.validate(simdModule)`
   - `'audioSession' in navigator`
   - `typeof RTCPeerConnection`
   - `typeof VideoDecoder`
   - `document.fullscreenEnabled`
   - `Telegram.WebApp.platform` / `version`

---

## 6. Must test on real devices

1. iOS 18+ and iOS 17.x (Telegram iOS):
   - Do Xbox, PS and MFi pads appear in `getGamepads()` after a tap?
   - Do they survive Telegram minimize/restore, opening Telegram's own chat input, and app background/foreground?
   - Does a page that `preventDefault`s all touches still get first responder?
2. iOS with the ring/silent switch on: Web Audio with and without `navigator.audioSession.type='playback'`, and with and without the looping `<audio>` element.
3. iOS: does an AudioContext start without a tap (expected yes, given Telegram's config)? Does audio stop on lock or background, and does it come back after resume (WebKit bug 240646 pattern)?
4. iOS: rAF rate in Low Power Mode (expected 30 fps), while minimized inside Telegram, and on 120 Hz ProMotion.
5. iOS: does the left 30 pt edge swipe dismiss or go back during play? Does `disableVerticalSwipes` stop accidental closes? Do landscape fullscreen and `lockOrientation()` work?
6. iOS: do a Lockdown Mode user, and the same user with Telegram excluded from Lockdown Mode, get WASM, Web Audio, WebGL and gamepads?
7. iOS: does `crossOriginIsolated` become true with COOP/COEP inside Telegram? What are the WebSocket survival time and reconnect behaviour after lock?
8. Android (several OEMs, WebView 120+ and the current one):
   - Gamepads over Bluetooth and USB: buttons, sticks, D-pad.
   - Does the B button trigger Telegram "back"?
   - Physical keyboard events.
   - Does the WebView keep focus after Telegram UI interactions?
9. Android: what happens to timers, WebSocket and audio after minimize (`onPause` → hidden) and after app background (screen off, about 5 minutes)? Is the process frozen or killed?
10. Android: does an AudioContext start before the first touch (expected yes, main frame)? Does `<video>` autoplay need the first touch? Measure the performance cost of `LAYER_TYPE_HARDWARE`.
11. Android: do edge back gestures in landscape fullscreen close the Mini App? Does `lockOrientation()` behave?
12. Windows tdesktop (WebView2): gamepad (XInput and DualSense) after clicking into the panel; autoplay policy; `crossOriginIsolated` with COOP/COEP; fullscreen via `requestFullscreen()`; timer and rAF behaviour when the panel is minimized.
13. macOS tdesktop and Telegram for macOS: gamepad needs a click (first responder). Keyboard. Fullscreen via the Telegram API.
14. Linux tdesktop (Ubuntu/Fedora/Debian, X11 and Wayland, embedded and external window modes):
    - Gamepad (libmanette present?)
    - WebAssembly SIMD
    - WebCodecs
    - Confirm `RTCPeerConnection` is undefined
    - WebKitGTK version in the field
15. Web K and Web A in Chrome, Firefox and Safari (desktop) and Safari iOS:
    - `getGamepads()` inside the Mini App iframe, which needs an unrestrictive top-level `Permissions-Policy`.
    - AudioContext unlock needs a tap inside the iframe.
    - Safari 30 fps rAF before interaction.
    - `requestFullscreen()` path.
    - Keyboard focus.
    - Storage partitioning of IndexedDB (ROM cache) in Safari.
16. All clients: does `Telegram.WebApp.isActive`, `activated` or `deactivated` fire as expected? It is expected never to fire on tdesktop.

---

## 7. Open questions

- Production HTTP headers of web.telegram.org/k and /a (any `Permissions-Policy`, COOP/COEP). This container cannot reach them.
- Whether the WebKit gamepad first-responder fix (May 2024) shipped in iOS 18.0, and what share of users are still on iOS 17.
- WebView2's effective autoplay policy inside tdesktop.
- Real WebKitGTK versions and build flags (libmanette, WebCodecs, SIMD) on common distros, and Flatpak/Snap tdesktop builds that bundle their own WebKitGTK.
- Whether Telegram for macOS (TelegramSwift) has changed since the public repo's last commit (2025-07-29).
- Whether Telegram iOS keeps the WKWebView "visible" (unthrottled) while minimized, and how long the WebContent process survives in the background, given that Telegram has the `audio` background mode.

---

## Verification

Adversarial fact-check of the high-relevance claims above, done 2026-10-09. I re-fetched every primary source myself rather than reusing the original researcher's local copies. My copies are in the research workspace (not committed; sources are cited inline)verify/`.

Sources and versions:
- Chromium `main` raw files (chrome/VERSION 157.0.8094.0) plus release tags 137.0.7151.138, 140.0.7339.264 and 150–156
- WebKit `main` plus the release tags `WebKit-7614…7619.*` and the branches `webkitglib/2.48…2.54`
- Gecko `mozilla-firefox/firefox` `main`
- tweb and telegram-tt `master` (fetched fresh)
- My own full `Telegram/SourceFiles` checkout of tdesktop at `22b352e` (2026-10-08)
- The existing Telegram-iOS (`f1dd7a2`) and Telegram-Android (`f2908b1`) clones, plus GitHub code search over the full Telegram-iOS and DrKLO/Telegram repos
- AndroidX `androidx-main` (webkit), AOSP `Generic.kcm`, GNOME `gnome-build-meta` (gnome-50, master), and the Flathub `org.telegram.desktop` manifest

| # | Claim (short) | Verdict |
|---|---|---|
| 1 | Gamepad policy default `*` in all 3 engines; Web K/A iframes don't list it | **CONFIRMED** |
| 2 | iOS: gamepad input only to first-responder WKContentView; a single tap makes it first responder | **CONFIRMED** |
| 3 | iOS 17 gamepad bug fixed May 2024, likely iOS 18; no entitlement | **CONFIRMED**, and upgraded to VERIFIED for the fix commit and the release branch |
| 4 | Telegram iOS never calls `becomeFirstResponder` on the Mini App web view | **CONFIRMED**, with a new caveat (`web_app_hide_keyboard`) |
| 5 | Android WebView plumbs gamepad key and motion events; BCD 37 | **CONFIRMED**, with a caveat about the B-button "Back" fallback |
| 6 | Android WebView *never* becomes crossOriginIsolated, so no SAB in Telegram Android | **DOWNGRADE**: the "never" is outdated. The Telegram Android conclusion still holds today |
| 7 | `cross-origin-isolated` defaults to `self` and is not delegated, so Telegram Web Mini Apps cannot use SAB | **REFUTED** for Chromium-based browsers (Document-Isolation-Policy). The premises are true; the conclusion holds only for Safari and Firefox |
| 8 | WebKit halves rAF for Low Power Mode, thermal, visually idle, and non-interacted cross-origin iframes | **CONFIRMED**. Thermal applies only when a non-default pref is on |
| 9 | Chromium: a cross-origin iframe without `allow=autoplay` needs a tap inside the iframe to unlock Web Audio | **CONFIRMED** (Chromium only), with exemptions noted |
| 10 | `activated`/`deactivated` senders per client; tdesktop never sends it | **CONFIRMED** |
| 11 | iOS background or lock suspends WebContent, stopping timers, WebSocket and audio | **CONFIRMED in part**: upgraded from ASSUMED to VERIFIED for the mechanism and for audio. Timing and WebSocket behaviour remain ASSUMED |
| 12 | No WebRTC in tdesktop Linux, because `enable-webrtc` defaults to false and lib_webview never enables it | **DOWNGRADE**: the mechanism is wrong for shipping WebKitGTK. The practical conclusion holds for upstream-default and Flatpak builds |

### Details

**1. Gamepad permissions policy: CONFIRMED.**
- Chromium `permissions_policy_features.json5:361-364` sets `Gamepad` to `feature_default: "EnableForAll"`. The file's global default is `EnableForSelf` (`:32-33`).
- WebKit `PermissionsPolicy.cpp:227-233` maps Gamepad to `"*"`.
- Gecko `PermissionsPolicyUtils.cpp:43` has `{"gamepad", eAll}`.
- Fresh tweb `webApp.tsx:57-64,1063` and telegram-tt `iframe.ts:10,13` (used at `WebAppTab.tsx:1245-1247`) do not contain `gamepad`.
- Chromium's `getGamepads()` only enforces the policy (`navigator_gamepad.cc:108-112`). A cross-origin subframe is only use-counted (`:138-141`); it has no focus requirement. Visibility still matters.
- Remaining risk, UNCHECKABLE here: a `Permissions-Policy` response header on web.telegram.org.

**2. iOS first responder: CONFIRMED.**
- `UIGamepadProviderIOS.mm:38-53` routes input to the key window's first responder if it is a `WKContentView`. The only other target is the page of an active WebXR session.
- `WKContentViewInteraction.mm:4125-4135`: `_singleTapRecognized` calls `becomeFirstResponder` only if `_potentialTapInProgress`. A synthetic click (`:4199-4203`) also does it.
- `:2048-2074` and `:2194`: `becomeFirstResponderForWebView` and its resign counterpart call `viewBecameActive` and `viewBecameInactive`.

**3. iOS 17 bug and fix: CONFIRMED, with stronger evidence than "REPORTED".**
- The fix is WebKit commit `24bc94ef`, "iOS: Fix gamepad detection when becoming firstResponder", bug 269292, Canonical 279124@main, dated 2024-05-22. Its message says gamepads "were only detected at initial load of the page if the WKWebView were marked as the firstResponder".
- I checked `WKContentViewInteraction.mm` at release tags for the `viewBecameActive` call. It is **present** at `WebKit-7619.1.24` and `WebKit-7619.1.26.11.4`, the Safari 18 / iOS 18 branch. It is **absent** at `WebKit-7618.3.11.13.3` (Safari 17.6) and at `7619.1.10` through `7619.1.13`.
- The mapping of tag 7619.1.26 to iOS 18.0 is REPORTED (common knowledge).
- The first-responder routing itself is older. It already exists at `WebKit-7614.4.6.11.7`, so the "detected only at load" bug probably affects iOS 16.x as well as 17.x (ASSUMED).
- "No entitlement" is VERIFIED only on the WebKit side: there is no entitlement check, and `GamepadsEnabled` defaults to true for WebKit (`UnifiedWebPreferences.yaml:2563-2572`). Whether Apple's GameController framework itself needs anything is REPORTED, from the bug comment.

**4. Telegram iOS never calls becomeFirstResponder: CONFIRMED.**
- There is no `FirstResponder` string anywhere in `submodules/WebUI` at `f1dd7a2`, or in master's `WebAppWebView.swift`.
- GitHub code search finds `WebAppWebView` only in `submodules/WebUI`. It finds no `becomeFirstResponder webView` anywhere in the repo.
- **New caveat (VERIFIED code):** `WebAppController.swift:1961-1962` handles `web_app_hide_keyboard` (`Telegram.WebApp.hideKeyboard()`) with `self.view.window?.endEditing(true)`. That resigns whatever is first responder in the window, WKContentView included. ASSUMED effect: calling `hideKeyboard()` (for example after the chat input) stops iOS gamepad delivery until the next tap. Do not call it while a pad is in use, or ask for a re-tap afterwards.

**5. Android gamepad plumbing: CONFIRMED.**
- `AwContents.java:4910-4925`: `dispatchKeyEvent` passes events to `GamepadList.dispatchKeyEvent` first.
- Motion events take this path: `onGenericMotionEvent` (`:4966-4970`) → `EventForwarder` (JNI) → `ContentUiEventHandler.onGenericMotionEvent` (`@CalledByNative`, `:83-86`) → `Gamepad.onGenericMotionEvent`.
- BCD 8.1.5 lists `webview_android` `getGamepads` at `37`.
- **New caveat (VERIFIED code; effect ASSUMED):** `GamepadList.handleKeyEvent` and `handleMotionEvent` consume events **only while `mIsGamepadAPIActive`** (`GamepadList.java:178-205,349-371`). AOSP `Generic.kcm:513-571` defines fallbacks: `BUTTON_B` → `BACK`, `BUTTON_A`/`START` → `DPAD_CENTER`, `SELECT` → `MENU`, `MODE` → `HOME`. A pad's B button therefore probably acts as Android Back (closing or minimizing the Mini App) whenever the page is not actively using the Gamepad API, for example before the first `getGamepads()` call or while hidden. Start polling immediately, and enable closing confirmation.

**6. Android WebView COI: DOWNGRADE.**
- The BCD data is as reported: SAB, threads-and-atomics and COOP are all `false` for `webview_android`.
- Current Chromium adds an **embedder opt-in**:
  - `AwContentBrowserClient::OriginSupportsConcreteCrossOriginIsolation` returns `AllowCrossOriginIsolatedApis(origin)` from a per-profile allowlist (`aw_content_browser_client.cc:1455-1461`, `aw_browser_context.cc:988-1019`).
  - The page must also send `Document-Isolation-Policy` (`AwCrossOriginIsolatedAllowlistTest.java:101-164`: header + allowlist = isolated; either alone = not; `AwContents.java:3699-3706`).
  - The hook is present at tags 150–156. The AndroidX boundary feature `CROSS_ORIGIN_ISOLATED_ALLOW_LIST` first appears at tag 152.0.7977.162 (commit `913875fa`, 2026-07-21).
  - AndroidX `Profile.setCrossOriginIsolatedAllowlist()` (`androidx-main` `Profile.java:788-821`, `WebViewFeature.java:1018-1025`) exposes it to apps.
- Telegram Android uses `androidx.webkit:webkit:1.14.0` (`TMessagesProj/build.gradle:48`). Code search finds no `CrossOriginIsolated` in DrKLO/Telegram.
- Corrected: *Android WebView is not cross-origin isolated by default. Since WebView 152 the embedding app can opt specific origins in, and the page must then send `Document-Isolation-Policy`. Telegram Android does not opt in, so there is no SAB or wasm threads in Telegram Android today. Recheck if Telegram adds it.*

**7. Telegram Web COI: REFUTED (Chromium).**
- The premises hold:
  - `cross-origin-isolated` has no `feature_default` in Chromium (`json5:289-291`), which means `self`.
  - WebKit returns `'self'` (`PermissionsPolicy.cpp:256-259`).
  - Neither Web K nor Web A delegates it.
- But Chromium's `LocalDOMWindow::CrossOriginIsolatedCapability()` **ignores the `cross-origin-isolated` permission policy when isolation comes from Document-Isolation-Policy** (`local_dom_window.cc:2695-2711`).
  - `kDocumentIsolationPolicy` is `FEATURE_ENABLED_BY_DEFAULT` (`services/network/public/cpp/features.cc:336`).
  - At the 137.0.7151.138 tag it was default-on only for Mac, Win, ChromeOS and Linux.
  - `kDocumentIsolationPolicyWithoutSiteIsolation` is enabled by default (`content/common/features.cc:214-215`).
  - The WICG explainer says the same: "a document with COEP and Document-Isolation-Policy should have access to COI-gated APIs, whether the top-level delegated the permission or not".
  - It shipped in Chrome 137 (REPORTED, Chrome blog, 2025-05-01).
- WebKit has `DocumentIsolationPolicyEnabled` with `status: testable`, `default false` (`UnifiedWebPreferences.yaml:2004-2010`). I found no DIP in Gecko (code search), so ASSUMED unsupported there.
- Corrected: *A Mini App in Telegram Web K/A on desktop Chrome or Edge 137+ can become `crossOriginIsolated` (SAB, wasm threads) by sending `Document-Isolation-Policy: isolate-and-credentialless` (or `isolate-and-require-corp`) on its own document. This works regardless of web.telegram.org's headers or the iframe `allow` list. It does not work in Safari or Firefox. Current main also enables DIP on Android Chrome; the version where that changed is not pinned. Treat threads as an optional speed-up there and keep the single-threaded build as the baseline.*

**8. rAF halving: CONFIRMED, with one nuance.**
- `AnimationFrameRate.cpp:33` lists the four half-speed reasons. `:47-86` halves the nominal rate: 60 → 30, and on displays above 60 Hz it divides by `IntervalThrottlingFactor = 2`.
- `ScriptedAnimationController.cpp:80-95` merges the page reasons into rAF.
- `Page.cpp:556-561,3075-3100` sets the page reasons.
- `Document.cpp:9454-9455,9762-9765` adds `NonInteractedCrossOriginFrame` for a cross-origin document without user interaction and removes it on the first handled gesture.
- On iOS, `VisuallyIdle` equals `!isActiveViewVisible()` (`PageClientImplIOS.mm:238-241`).
- Nuance: `AggressiveThermalMitigation` is set only when `RespondToThermalPressureAggressively` is true. That pref is `status: internal`, `defaultValue: false` (`UnifiedWebPreferences.yaml:5211-5217`), so ordinary thermal pressure does not halve rAF by default.

**9. Chromium iframe autoplay: CONFIRMED (Chromium).**
- `autoplay_policy.cc:81-118`:
  - Sticky activation of the iframe itself counts.
  - The ancestor walk continues only if the `autoplay` policy is enabled, and that policy defaults to `self` (`json5:134-136`).
  - The MEI bypass applies only to the outermost main frame.
- `audio_context.cc:1458-1462,1499-1501` uses this check under `kDocumentUserActivationRequired`.
- Exemptions:
  - A user site exception (`DocumentHasUserExceptionFlag`).
  - Active `getUserMedia` capture.
  - **New:** in Chromium, a gamepad button press seen through `getGamepads()` while the page is visible calls `LocalFrame::NotifyUserActivation` (`navigator_gamepad.cc:126-133`). A pad press inside the Mini App therefore also unlocks audio, in Chromium only.
- Safari and Firefox were not checked.

**10. `visibility_changed` senders: CONFIRMED.**
- iOS `WebAppController.swift:4192-4206`: only on `isMinimized` changes, not on app background.
- Android `BotWebViewContainer.java:373,1113-1118`, called from `BotWebViewSheet`, `BotWebViewAttachedSheet` and `BotWebViewMenuContainer`.
- Web K `browser.tsx:732` → `webApp.tsx:1387-1388`.
- Web A `WebAppTab.tsx:412-425`.
- tdesktop: I grepped **all** of `Telegram/SourceFiles` at `22b352e`, not only the five directories. There is no `visibility_changed`. The complete list of `postEvent` names in `attach_bot_webview.cpp` has none.
- **New:** Telegram for macOS (TelegramSwift master `WebappBrowser.swift`, `WebpageModalController.swift`) does not send it either.
- `telegram-web-app.js` lines 397-405 match. That file comes from two identical third-party GitHub change trackers, not from telegram.org.

**11. iOS background: CONFIRMED in part, upgraded from ASSUMED.** The following is VERIFIED in WebKit `main`:
- When the WKWebView is `_isBackground`, `PageClientImpl::isActiveViewVisible()` returns false, so the page is hidden. The exceptions are PiP and WebXR (`PageClientImplIOS.mm:166-196`).
- iOS adds `BackgroundProcessPlaybackRestricted` for **WebAudio** (`MediaSessionManagerIOS.mm:89-91`). `applicationDidEnterBackground` then interrupts those sessions (`MediaSessionManagerInterface.cpp:315-331`).
  - So Web Audio stops on background no matter what `UIBackgroundModes` Telegram declares.
  - Video+audio is additionally restricted when suspended under lock.
- With no activity left, `ProcessThrottler` moves the WebContent process toward `Suspended` and sends PrepareToSuspend with a `processSuspensionTimeout` of 20 s (`ProcessThrottler.cpp:49,391-396`).
- Audible playback would hold a "WebKit Media Playback" assertion (`WebProcessProxy.cpp:2543-2571`), but the interruption above removes it.

Still ASSUMED: the exact grace period in Telegram, and what happens to WebSockets. WebSockets live in the Network process, so they may survive briefly or go stale. A device test is still needed.

**12. Linux WebRTC: DOWNGRADE.**
- True:
  - The GTK port compiles WebRTC only if the builder opts in. `ENABLE_WEB_RTC` is `${ENABLE_EXPERIMENTAL_FEATURES}` on main and 2.48–2.52, and a hard `OFF` on `webkitglib/2.54`.
  - lib_webview only ever sets `enable-webrtc` to false, inside `ApplyRestrictedSettings` (`webview_linux_webkitgtk.cpp:156-181`).
- Wrong for shipping releases: `enable-webrtc` defaults to `FEATURE_DEFAULT(PeerConnectionEnabled)` (`WebKitSettings.cpp:1691-1700`). On every release branch `webkitglib/2.48`, `2.50`, `2.52` and `2.54`, `PeerConnectionEnabled` has `"USE(GSTREAMER_WEBRTC)": true`. Only `main` lacks that line. So a distro WebKitGTK built with WebRTC exposes `RTCPeerConnection` to Mini Apps by default, and lib_webview does not turn it off for non-restricted views.
- VERIFIED: the Flathub `org.telegram.desktop` uses `org.gnome.Platform` 50. GNOME 50's `webkitgtk.inc` builds WebKitGTK **2.54.1** with no WebRTC flag (so off) and with libmanette (so gamepad on).
- UNCHECKABLE: Fedora, Arch and Debian specs. Their hosts are blocked by the proxy.
- Corrected: *Expect no WebRTC in Telegram Desktop on Linux (upstream default and Flatpak). A distro WebKitGTK built with GStreamer WebRTC would have it enabled by default. Feature-detect `RTCPeerConnection` and keep a WebSocket fallback.*

### Effect on the report's conclusions

- §3.2 and §4 "no SAB/threads on Telegram Web" should read: "none on Safari or Firefox; available on Chromium 137+ desktop via Document-Isolation-Policy". The single-threaded core stays the baseline, because Telegram Android has no SAB.
- §2 matrix, Linux WebRTC: change to "N in Flatpak and upstream-default builds; Y only if the distro compiled GStreamer WebRTC (2.48+ then defaults on)".
- §3.1 Android: add the B → Back fallback risk. iOS: add the `hideKeyboard()` → lost-first-responder risk.
- §3.5 iOS: Web Audio interruption on background is now VERIFIED in WebKit source.
