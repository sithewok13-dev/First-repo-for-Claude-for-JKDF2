// Minimal deterministic libretro frontend for WebAssembly.
//
// The same compiled module runs in two places:
//   * the server's emulation worker (Node.js), which owns the authoritative
//     game state, produces snapshots/state hashes and feeds game adapters, and
//   * every player's / spectator's browser, which runs a lockstep replica fed
//     with the server's authoritative per-frame input records.
//
// Everything that could make two instances diverge is pinned here: wall-clock
// time, libc randomness and time zones are replaced with functions of the
// session frame counter, input is a plain per-port bitmask written by the
// host, and the core is told it runs in a rollback-netplay context so it uses
// its netplay-safe savestate path.
//
// The host (JS) drives everything through the fe_* exports below.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include <emscripten/emscripten.h>

#include "libretro.h"

#define FE_MAX_PORTS 8
#define FE_MAX_OPTIONS 256
#define FE_AUDIO_MAX_FRAMES 8192
#define FE_MAX_DESCRIPTORS 512
#define FE_MAX_MEMDESC 64

// ---------------------------------------------------------------------------
// Deterministic clock and randomness
// ---------------------------------------------------------------------------

static int64_t g_epoch = 1514764800;  // 2018-01-01T00:00:00Z unless the host sets one
static uint64_t g_frame = 0;          // frames executed in this session
static double g_fps = 60.0;
static uint32_t g_rand_calls = 0;     // rand() calls within the current frame

static int64_t fe_now_usec(void) {
  double secs = (double)g_frame / (g_fps > 0 ? g_fps : 60.0);
  return g_epoch * 1000000LL + (int64_t)(secs * 1000000.0);
}

extern "C" {

time_t time(time_t *t) {
  time_t v = (time_t)(fe_now_usec() / 1000000LL);
  if (t) *t = v;
  return v;
}

int gettimeofday(struct timeval *tv, void *tz) {
  (void)tz;
  if (tv) {
    int64_t us = fe_now_usec();
    tv->tv_sec = (time_t)(us / 1000000LL);
    tv->tv_usec = (suseconds_t)(us % 1000000LL);
  }
  return 0;
}

int clock_gettime(clockid_t clk, struct timespec *ts) {
  (void)clk;
  if (ts) {
    int64_t us = fe_now_usec();
    ts->tv_sec = (time_t)(us / 1000000LL);
    ts->tv_nsec = (long)((us % 1000000LL) * 1000);
  }
  return 0;
}

clock_t clock(void) {
  return (clock_t)((double)g_frame / (g_fps > 0 ? g_fps : 60.0) * CLOCKS_PER_SEC);
}

// Time zones differ between viewers; everyone sees UTC.
struct tm *localtime_r(const time_t *t, struct tm *out) { return gmtime_r(t, out); }
struct tm *localtime(const time_t *t) {
  static struct tm tmp;
  return gmtime_r(t, &tmp);
}

// mktime() would consult the viewer's time zone through tzset(); use UTC.
time_t mktime(struct tm *tm) { return timegm(tm); }
void tzset(void) {}

// rand() is a pure function of (frame, call index within the frame), so it does
// not depend on how long this instance has been running or which snapshot it
// was restored from.
static uint32_t fe_mix32(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU;
  x ^= x >> 15; x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}
int rand(void) {
  uint32_t v = fe_mix32((uint32_t)g_frame * 0x9E3779B9U ^ fe_mix32(++g_rand_calls));
  return (int)(v & 0x7fffffff);
}
void srand(unsigned seed) { (void)seed; }
long random(void) { return (long)rand(); }
void srandom(unsigned seed) { (void)seed; }

}  // extern "C"

// ---------------------------------------------------------------------------
// Frontend state
// ---------------------------------------------------------------------------

static uint16_t g_pad[FE_MAX_PORTS];
static unsigned g_port_device[FE_MAX_PORTS];

static enum retro_pixel_format g_pixfmt = RETRO_PIXEL_FORMAT_0RGB1555;
static uint32_t *g_rgba = NULL;
static size_t g_rgba_cap = 0;
static unsigned g_fb_w = 0, g_fb_h = 0;
static int g_render = 1;        // convert the frame to RGBA for display
static int g_frame_valid = 0;   // a frame was produced during the last run

static int16_t g_audio[FE_AUDIO_MAX_FRAMES * 2];
static size_t g_audio_frames = 0;
static int g_audio_on = 1;

static int g_savestate_ctx = RETRO_SAVESTATE_CONTEXT_ROLLBACK_NETPLAY;
static uint64_t g_serialization_quirks = 0;

static struct retro_system_info g_sysinfo;
static struct retro_system_av_info g_av;
static int g_loaded = 0;
static void *g_game_data = NULL;
static unsigned g_rotation = 0;

static char g_log_level = 2;  // 0 debug .. 3 error; default warn

struct fe_option { char key[96]; char value[128]; char def[128]; int set_by_host; };
static fe_option g_opts[FE_MAX_OPTIONS];
static int g_nopts = 0;
static int g_opts_dirty = 0;

struct fe_desc { unsigned port, device, index, id; char text[96]; };
static fe_desc g_desc[FE_MAX_DESCRIPTORS];
static int g_ndesc = 0;

struct fe_memdesc { uint64_t flags; uint8_t *ptr; uint32_t offset; uint32_t start; uint32_t select; uint32_t disconnect; uint32_t len; };
static fe_memdesc g_mem[FE_MAX_MEMDESC];
static int g_nmem = 0;

static uint8_t *g_state_buf = NULL;
static size_t g_state_cap = 0;
static uint8_t g_hash_out[8];

static fe_option *fe_find_option(const char *key, int create) {
  for (int i = 0; i < g_nopts; i++)
    if (!strcmp(g_opts[i].key, key)) return &g_opts[i];
  if (!create || g_nopts >= FE_MAX_OPTIONS) return NULL;
  fe_option *o = &g_opts[g_nopts++];
  memset(o, 0, sizeof(*o));
  snprintf(o->key, sizeof(o->key), "%s", key);
  return o;
}

static void fe_register_default(const char *key, const char *def) {
  fe_option *o = fe_find_option(key, 1);
  if (!o) return;
  snprintf(o->def, sizeof(o->def), "%s", def ? def : "");
  if (!o->set_by_host) snprintf(o->value, sizeof(o->value), "%s", o->def);
}

static void fe_log(enum retro_log_level level, const char *fmt, ...) {
  if ((int)level < g_log_level) return;
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  fprintf(stderr, "[core:%d] %s", (int)level, buf);
  size_t n = strlen(buf);
  if (n == 0 || buf[n - 1] != '\n') fputc('\n', stderr);
}

// ---------------------------------------------------------------------------
// libretro callbacks
// ---------------------------------------------------------------------------

static bool fe_environment(unsigned cmd, void *data) {
  switch (cmd) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
      struct retro_log_callback *cb = (struct retro_log_callback *)data;
      cb->log = fe_log;
      return true;
    }
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
      *(bool *)data = true;
      return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
      enum retro_pixel_format f = *(enum retro_pixel_format *)data;
      if (f == RETRO_PIXEL_FORMAT_0RGB1555 || f == RETRO_PIXEL_FORMAT_RGB565 || f == RETRO_PIXEL_FORMAT_XRGB8888) {
        g_pixfmt = f;
        return true;
      }
      return false;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
      *(const char **)data = "/system";
      return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      *(const char **)data = "/save";
      return true;
    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
      *(const char **)data = "/system";
      return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
      *(unsigned *)data = 2;
      return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES: {
      const struct retro_variable *v = (const struct retro_variable *)data;
      for (; v && v->key; v++) {
        // "Description; default|second|third"
        const char *semi = strchr(v->value, ';');
        char def[128] = {0};
        if (semi) {
          semi++;
          while (*semi == ' ') semi++;
          size_t i = 0;
          while (semi[i] && semi[i] != '|' && i < sizeof(def) - 1) { def[i] = semi[i]; i++; }
        }
        fe_register_default(v->key, def);
      }
      return true;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS: {
      const struct retro_core_option_definition *d = (const struct retro_core_option_definition *)data;
      for (; d && d->key; d++)
        fe_register_default(d->key, d->default_value ? d->default_value : (d->values[0].value ? d->values[0].value : ""));
      return true;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL: {
      const struct retro_core_options_intl *intl = (const struct retro_core_options_intl *)data;
      const struct retro_core_option_definition *d = intl ? intl->us : NULL;
      for (; d && d->key; d++)
        fe_register_default(d->key, d->default_value ? d->default_value : (d->values[0].value ? d->values[0].value : ""));
      return true;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2: {
      const struct retro_core_options_v2 *o = (const struct retro_core_options_v2 *)data;
      const struct retro_core_option_v2_definition *d = o ? o->definitions : NULL;
      for (; d && d->key; d++)
        fe_register_default(d->key, d->default_value ? d->default_value : (d->values[0].value ? d->values[0].value : ""));
      return true;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL: {
      const struct retro_core_options_v2_intl *intl = (const struct retro_core_options_v2_intl *)data;
      const struct retro_core_option_v2_definition *d = (intl && intl->us) ? intl->us->definitions : NULL;
      for (; d && d->key; d++)
        fe_register_default(d->key, d->default_value ? d->default_value : (d->values[0].value ? d->values[0].value : ""));
      return true;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
      return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
      struct retro_variable *v = (struct retro_variable *)data;
      fe_option *o = fe_find_option(v->key, 0);
      v->value = o ? o->value : NULL;
      return o != NULL;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
      *(bool *)data = g_opts_dirty != 0;
      g_opts_dirty = 0;
      return true;
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS: {
      const struct retro_input_descriptor *d = (const struct retro_input_descriptor *)data;
      g_ndesc = 0;
      for (; d && d->description && g_ndesc < FE_MAX_DESCRIPTORS; d++) {
        fe_desc *o = &g_desc[g_ndesc++];
        o->port = d->port; o->device = d->device; o->index = d->index; o->id = d->id;
        snprintf(o->text, sizeof(o->text), "%s", d->description);
      }
      return true;
    }
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
      return true;
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
    case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
    case RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:
    case RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE:
    case RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK:
      return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE: {
      const struct retro_message *m = (const struct retro_message *)data;
      if (m && m->msg) fe_log(RETRO_LOG_INFO, "message: %s", m->msg);
      return true;
    }
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT: {
      const struct retro_message_ext *m = (const struct retro_message_ext *)data;
      if (m && m->msg) fe_log(RETRO_LOG_INFO, "message: %s", m->msg);
      return true;
    }
    case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION:
      *(unsigned *)data = 1;
      return true;
    case RETRO_ENVIRONMENT_SET_ROTATION:
      g_rotation = *(const unsigned *)data;
      return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY: {
      const struct retro_game_geometry *g = (const struct retro_game_geometry *)data;
      g_av.geometry.base_width = g->base_width;
      g_av.geometry.base_height = g->base_height;
      g_av.geometry.aspect_ratio = g->aspect_ratio;
      return true;
    }
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
      g_av = *(const struct retro_system_av_info *)data;
      g_fps = g_av.timing.fps;
      return true;
    }
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS: {
      const struct retro_memory_map *m = (const struct retro_memory_map *)data;
      g_nmem = 0;
      for (unsigned i = 0; m && i < m->num_descriptors && g_nmem < FE_MAX_MEMDESC; i++) {
        const struct retro_memory_descriptor *d = &m->descriptors[i];
        fe_memdesc *o = &g_mem[g_nmem++];
        o->flags = d->flags; o->ptr = (uint8_t *)d->ptr; o->offset = (uint32_t)d->offset;
        o->start = (uint32_t)d->start; o->select = (uint32_t)d->select;
        o->disconnect = (uint32_t)d->disconnect; o->len = (uint32_t)d->len;
      }
      return true;
    }
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
      g_serialization_quirks = *(uint64_t *)data;
      return true;
    case RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT:
      if (data) *(int *)data = g_savestate_ctx;
      return true;
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
      // Bit 0: video, bit 1: audio, bit 2: "use fast savestates" (netplay-safe).
      // Audio is never hard-disabled (bit 3), because skipping sound-chip
      // emulation could change emulated state on some cores.
      if (data) *(int *)data = 1 | 2 | (g_savestate_ctx == RETRO_SAVESTATE_CONTEXT_ROLLBACK_NETPLAY ? 4 : 0);
      return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE:
      *(unsigned *)data = RETRO_LANGUAGE_ENGLISH;
      return true;
    case RETRO_ENVIRONMENT_GET_USERNAME:
      *(const char **)data = NULL;
      return false;
    case RETRO_ENVIRONMENT_GET_INPUT_MAX_USERS:
      *(unsigned *)data = FE_MAX_PORTS;
      return true;
    case RETRO_ENVIRONMENT_GET_FASTFORWARDING:
      *(bool *)data = false;
      return true;
    case RETRO_ENVIRONMENT_GET_THROTTLE_STATE: {
      struct retro_throttle_state *t = (struct retro_throttle_state *)data;
      t->mode = RETRO_THROTTLE_NONE;
      t->rate = (float)g_fps;
      return true;
    }
    default:
      return false;
  }
}

static void fe_video_refresh(const void *data, unsigned width, unsigned height, size_t pitch) {
  if (!data) return;  // duplicated frame: keep the previous RGBA image
  g_frame_valid = 1;
  if (!g_render) return;
  size_t need = (size_t)width * height;
  if (need > g_rgba_cap) {
    free(g_rgba);
    g_rgba = (uint32_t *)malloc(need * 4);
    g_rgba_cap = g_rgba ? need : 0;
    if (!g_rgba) return;
  }
  g_fb_w = width;
  g_fb_h = height;
  const uint8_t *src = (const uint8_t *)data;
  uint32_t *dst = g_rgba;
  if (g_pixfmt == RETRO_PIXEL_FORMAT_XRGB8888) {
    for (unsigned y = 0; y < height; y++) {
      const uint32_t *s = (const uint32_t *)(src + y * pitch);
      for (unsigned x = 0; x < width; x++) {
        uint32_t p = s[x];
        // ABGR in memory order for ImageData (little endian): R G B A
        *dst++ = 0xff000000u | ((p & 0xff) << 16) | (p & 0xff00) | ((p >> 16) & 0xff);
      }
    }
  } else if (g_pixfmt == RETRO_PIXEL_FORMAT_RGB565) {
    for (unsigned y = 0; y < height; y++) {
      const uint16_t *s = (const uint16_t *)(src + y * pitch);
      for (unsigned x = 0; x < width; x++) {
        uint16_t p = s[x];
        uint32_t r = (p >> 11) & 0x1f, g = (p >> 5) & 0x3f, b = p & 0x1f;
        r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2);
        *dst++ = 0xff000000u | (b << 16) | (g << 8) | r;
      }
    }
  } else {
    for (unsigned y = 0; y < height; y++) {
      const uint16_t *s = (const uint16_t *)(src + y * pitch);
      for (unsigned x = 0; x < width; x++) {
        uint16_t p = s[x];
        uint32_t r = (p >> 10) & 0x1f, g = (p >> 5) & 0x1f, b = p & 0x1f;
        r = (r << 3) | (r >> 2); g = (g << 3) | (g >> 2); b = (b << 3) | (b >> 2);
        *dst++ = 0xff000000u | (b << 16) | (g << 8) | r;
      }
    }
  }
}

static void fe_audio_sample(int16_t left, int16_t right) {
  if (!g_audio_on || g_audio_frames >= FE_AUDIO_MAX_FRAMES) return;
  g_audio[g_audio_frames * 2] = left;
  g_audio[g_audio_frames * 2 + 1] = right;
  g_audio_frames++;
}

static size_t fe_audio_batch(const int16_t *data, size_t frames) {
  if (!g_audio_on) return frames;
  size_t room = FE_AUDIO_MAX_FRAMES - g_audio_frames;
  size_t n = frames < room ? frames : room;
  memcpy(&g_audio[g_audio_frames * 2], data, n * 4);
  g_audio_frames += n;
  return frames;
}

static void fe_input_poll(void) {}

static int16_t fe_input_state(unsigned port, unsigned device, unsigned index, unsigned id) {
  (void)index;
  if (port >= FE_MAX_PORTS) return 0;
  if ((device & RETRO_DEVICE_MASK) != RETRO_DEVICE_JOYPAD) return 0;
  if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t)g_pad[port];
  if (id > 15) return 0;
  return (g_pad[port] >> id) & 1;
}

// ---------------------------------------------------------------------------
// Core-specific hooks (implemented per core in shim files; weak defaults)
// ---------------------------------------------------------------------------

extern "C" void fe_core_pre_load(void) __attribute__((weak));
extern "C" void fe_core_pre_load(void) {}
extern "C" const char *fe_core_driver_info(void) __attribute__((weak));
extern "C" const char *fe_core_driver_info(void) { return "{}"; }
extern "C" const char *fe_core_catalog(void) __attribute__((weak));
extern "C" const char *fe_core_catalog(void) { return "[]"; }
extern "C" const char *fe_core_state_map(int action) __attribute__((weak));
extern "C" const char *fe_core_state_map(int action) { (void)action; return "[]"; }

// ---------------------------------------------------------------------------
// Exports
// ---------------------------------------------------------------------------

extern "C" {

EMSCRIPTEN_KEEPALIVE int fe_init(void) {
  memset(g_pad, 0, sizeof(g_pad));
  retro_set_environment(fe_environment);
  retro_set_video_refresh(fe_video_refresh);
  retro_set_audio_sample(fe_audio_sample);
  retro_set_audio_sample_batch(fe_audio_batch);
  retro_set_input_poll(fe_input_poll);
  retro_set_input_state(fe_input_state);
  retro_init();
  memset(&g_sysinfo, 0, sizeof(g_sysinfo));
  retro_get_system_info(&g_sysinfo);
  return (int)retro_api_version();
}

EMSCRIPTEN_KEEPALIVE const char *fe_core_name(void) { return g_sysinfo.library_name ? g_sysinfo.library_name : ""; }
EMSCRIPTEN_KEEPALIVE const char *fe_core_version(void) { return g_sysinfo.library_version ? g_sysinfo.library_version : ""; }
EMSCRIPTEN_KEEPALIVE const char *fe_core_extensions(void) { return g_sysinfo.valid_extensions ? g_sysinfo.valid_extensions : ""; }

EMSCRIPTEN_KEEPALIVE void fe_set_epoch(double epoch_seconds) { g_epoch = (int64_t)epoch_seconds; }
EMSCRIPTEN_KEEPALIVE void fe_set_log_level(int level) { g_log_level = (char)level; }

EMSCRIPTEN_KEEPALIVE int fe_set_option(const char *key, const char *value) {
  fe_option *o = fe_find_option(key, 1);
  if (!o) return 0;
  snprintf(o->value, sizeof(o->value), "%s", value);
  o->set_by_host = 1;
  g_opts_dirty = 1;
  return 1;
}

EMSCRIPTEN_KEEPALIVE const char *fe_get_option(const char *key) {
  fe_option *o = fe_find_option(key, 0);
  return o ? o->value : NULL;
}

EMSCRIPTEN_KEEPALIVE int fe_option_count(void) { return g_nopts; }
EMSCRIPTEN_KEEPALIVE const char *fe_option_key(int i) { return (i >= 0 && i < g_nopts) ? g_opts[i].key : ""; }

// Loads content from the in-memory filesystem. Returns 1 on success.
EMSCRIPTEN_KEEPALIVE int fe_load_game(const char *path) {
  if (g_loaded) return 0;
  fe_core_pre_load();
  struct retro_game_info info;
  memset(&info, 0, sizeof(info));
  info.path = path;
  if (!g_sysinfo.need_fullpath) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return 0; }
    g_game_data = malloc((size_t)n);
    if (!g_game_data) { fclose(f); return 0; }
    if (fread(g_game_data, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(g_game_data); g_game_data = NULL; return 0; }
    fclose(f);
    info.data = g_game_data;
    info.size = (size_t)n;
  }
  g_frame = 0;
  g_rand_calls = 0;
  if (!retro_load_game(&info)) {
    free(g_game_data);
    g_game_data = NULL;
    return 0;
  }
  for (unsigned p = 0; p < FE_MAX_PORTS; p++) {
    if (g_port_device[p]) retro_set_controller_port_device(p, g_port_device[p]);
  }
  memset(&g_av, 0, sizeof(g_av));
  retro_get_system_av_info(&g_av);
  g_fps = g_av.timing.fps > 0 ? g_av.timing.fps : 60.0;
  g_loaded = 1;
  return 1;
}

EMSCRIPTEN_KEEPALIVE void fe_set_port_device(unsigned port, unsigned device) {
  if (port >= FE_MAX_PORTS) return;
  g_port_device[port] = device;
  if (g_loaded) retro_set_controller_port_device(port, device);
}

EMSCRIPTEN_KEEPALIVE double fe_fps(void) { return g_fps; }
EMSCRIPTEN_KEEPALIVE double fe_sample_rate(void) { return g_av.timing.sample_rate; }
EMSCRIPTEN_KEEPALIVE unsigned fe_base_width(void) { return g_av.geometry.base_width; }
EMSCRIPTEN_KEEPALIVE unsigned fe_base_height(void) { return g_av.geometry.base_height; }
EMSCRIPTEN_KEEPALIVE double fe_aspect(void) { return g_av.geometry.aspect_ratio; }
EMSCRIPTEN_KEEPALIVE unsigned fe_rotation(void) { return g_rotation; }

EMSCRIPTEN_KEEPALIVE void fe_set_input(unsigned port, unsigned mask) {
  if (port < FE_MAX_PORTS) g_pad[port] = (uint16_t)mask;
}

// Runs exactly one emulated frame. render: convert video to RGBA; audio: keep samples.
EMSCRIPTEN_KEEPALIVE int fe_run_frame(int render, int audio) {
  if (!g_loaded) return 0;
  g_render = render;
  g_audio_on = audio;
  g_audio_frames = 0;
  g_frame_valid = 0;
  g_rand_calls = 0;
  retro_run();
  g_frame++;
  return g_frame_valid;
}

EMSCRIPTEN_KEEPALIVE double fe_frame(void) { return (double)g_frame; }
EMSCRIPTEN_KEEPALIVE void fe_set_frame(double frame) { g_frame = (uint64_t)frame; }

EMSCRIPTEN_KEEPALIVE uint32_t *fe_video_ptr(void) { return g_rgba; }
EMSCRIPTEN_KEEPALIVE unsigned fe_video_width(void) { return g_fb_w; }
EMSCRIPTEN_KEEPALIVE unsigned fe_video_height(void) { return g_fb_h; }
EMSCRIPTEN_KEEPALIVE int16_t *fe_audio_ptr(void) { return g_audio; }
EMSCRIPTEN_KEEPALIVE unsigned fe_audio_frames(void) { return (unsigned)g_audio_frames; }

EMSCRIPTEN_KEEPALIVE void fe_set_savestate_context(int ctx) { g_savestate_ctx = ctx; }
EMSCRIPTEN_KEEPALIVE double fe_serialization_quirks(void) { return (double)g_serialization_quirks; }

EMSCRIPTEN_KEEPALIVE unsigned fe_serialize_size(void) { return g_loaded ? (unsigned)retro_serialize_size() : 0; }

// Serializes into an internal buffer; returns its size (0 on failure). Read it with fe_state_ptr().
EMSCRIPTEN_KEEPALIVE unsigned fe_serialize(void) {
  if (!g_loaded) return 0;
  size_t n = retro_serialize_size();
  if (n == 0) return 0;
  if (n > g_state_cap) {
    free(g_state_buf);
    g_state_buf = (uint8_t *)malloc(n);
    g_state_cap = g_state_buf ? n : 0;
    if (!g_state_buf) return 0;
  }
  memset(g_state_buf, 0, n);
  if (!retro_serialize(g_state_buf, n)) return 0;
  return (unsigned)n;
}

EMSCRIPTEN_KEEPALIVE uint8_t *fe_state_ptr(void) { return g_state_buf; }

// Allocates a buffer the host can fill before fe_unserialize_buffer().
EMSCRIPTEN_KEEPALIVE uint8_t *fe_state_reserve(unsigned n) {
  if (n > g_state_cap) {
    free(g_state_buf);
    g_state_buf = (uint8_t *)malloc(n);
    g_state_cap = g_state_buf ? n : 0;
  }
  return g_state_buf;
}

EMSCRIPTEN_KEEPALIVE int fe_unserialize_buffer(unsigned n) {
  if (!g_loaded || !g_state_buf || n > g_state_cap) return 0;
  return retro_unserialize(g_state_buf, n) ? 1 : 0;
}

// 64-bit FNV-1a over the serialized state. Written to an 8-byte buffer (little endian).
EMSCRIPTEN_KEEPALIVE uint8_t *fe_state_hash(void) {
  unsigned n = fe_serialize();
  uint64_t h = 1469598103934665603ULL;
  for (unsigned i = 0; i < n; i++) {
    h ^= g_state_buf[i];
    h *= 1099511628211ULL;
  }
  for (int i = 0; i < 8; i++) g_hash_out[i] = (uint8_t)(h >> (8 * i));
  return n ? g_hash_out : NULL;
}

EMSCRIPTEN_KEEPALIVE uint8_t *fe_memory_ptr(unsigned id) { return g_loaded ? (uint8_t *)retro_get_memory_data(id) : NULL; }
EMSCRIPTEN_KEEPALIVE unsigned fe_memory_size(unsigned id) { return g_loaded ? (unsigned)retro_get_memory_size(id) : 0; }

EMSCRIPTEN_KEEPALIVE int fe_memmap_count(void) { return g_nmem; }
// Fills 7 uint32 values: flags(lo), ptr, offset, start, select, disconnect, len.
EMSCRIPTEN_KEEPALIVE uint32_t *fe_memmap_get(int i) {
  static uint32_t out[7];
  if (i < 0 || i >= g_nmem) return NULL;
  out[0] = (uint32_t)g_mem[i].flags;
  out[1] = (uint32_t)(uintptr_t)g_mem[i].ptr;
  out[2] = g_mem[i].offset;
  out[3] = g_mem[i].start;
  out[4] = g_mem[i].select;
  out[5] = g_mem[i].disconnect;
  out[6] = g_mem[i].len;
  return out;
}

EMSCRIPTEN_KEEPALIVE int fe_descriptor_count(void) { return g_ndesc; }
// Fills 4 uint32 values: port, device, index, id. Text via fe_descriptor_text.
EMSCRIPTEN_KEEPALIVE uint32_t *fe_descriptor_get(int i) {
  static uint32_t out[4];
  if (i < 0 || i >= g_ndesc) return NULL;
  out[0] = g_desc[i].port; out[1] = g_desc[i].device; out[2] = g_desc[i].index; out[3] = g_desc[i].id;
  return out;
}
EMSCRIPTEN_KEEPALIVE const char *fe_descriptor_text(int i) { return (i >= 0 && i < g_ndesc) ? g_desc[i].text : ""; }

EMSCRIPTEN_KEEPALIVE const char *fe_driver_info(void) { return fe_core_driver_info(); }
EMSCRIPTEN_KEEPALIVE const char *fe_catalog(void) { return fe_core_catalog(); }
EMSCRIPTEN_KEEPALIVE const char *fe_state_map(int action) { return fe_core_state_map(action); }

EMSCRIPTEN_KEEPALIVE void fe_reset(void) {
  if (g_loaded) retro_reset();
}

EMSCRIPTEN_KEEPALIVE void fe_unload(void) {
  if (!g_loaded) return;
  retro_unload_game();
  g_loaded = 0;
  free(g_game_data);
  g_game_data = NULL;
}

}  // extern "C"
