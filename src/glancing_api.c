#include <pebble.h>
#include "glancing_api.h"

// Enable debugging of glancing, currently just vibrate on glancing
#define DEBUG

#ifndef WITHIN
#define WITHIN(n, min, max) ((n) >= (min) && (n) <= (max))
#endif

#define WITHIN_ZONE(zone, x, y, z) ( \
  WITHIN(x, zone.x_segment.start, zone.x_segment.end) && \
  WITHIN(y, zone.y_segment.start, zone.y_segment.end) && \
  WITHIN(z, zone.z_segment.start, zone.z_segment.end) \
  )

typedef struct segment {
  int start;
  int end;
} segment;

typedef struct glancing_zone {
  segment x_segment;
  segment y_segment;
  segment z_segment;
} glancing_zone;

// watch tilted towards user, screen pointed toward user
glancing_zone active_zone = {
  .x_segment = { -500, 500},
  .y_segment = { -900, 200},
  .z_segment = { -1100, 0}
};

// arm hanging downward, select button pointing toward ground
glancing_zone inactive_zone_downward = {
  .x_segment = { 800, 1000},
  .y_segment = { -500, 500},
  .z_segment = { -800, 800}
};

// arm horizontal, screen facing away from user, essentially wrist was rotated away from user
glancing_zone inactive_zone_away = {
  .x_segment = { -600, 600},
  .y_segment = { 850, 1200},
  .z_segment = { -500, 500}
};


static void prv_glancing_callback(GlancingData *data) {}
static GlancingDataHandler prv_handler = prv_glancing_callback;
static GlancingData prv_data = {.state = GLANCING_INACTIVE};
static int prv_timeout_ms = 0;
static AppTimer *glancing_timeout_handle = NULL;
static bool prv_control_backlight = false;
static bool prv_legacy_flick_backlight = false;
// the time duration of the fade out
static const int32_t LIGHT_FADE_TIME_MS = 500;

// window of time from inactive zone to active to trigger glance
// stored in seconds for the time being
static const uint32_t DOWNWARD_WINDOW = 1;
static const uint32_t AWAY_WINDOW = 1;
static const uint32_t ROLL_WINDOW_MS = 500;

typedef struct time_ms_t {
  time_t sec;
  uint16_t ms;
} time_ms_t;
static time_ms_t s_glanced_window = {0};
static time_ms_t s_last_active = {0};

static inline void prv_update_state(GlanceState state) {
  // Only call subscribed callback when state changes
  if (prv_data.state != state) {
    prv_data.state = state;
    prv_handler(&prv_data);
  }
}

static inline bool prv_is_glancing(void) {
  return prv_data.state == GLANCING_ACTIVE;
}

static void glance_timeout(void* data) {
  prv_update_state(GLANCING_TIMEDOUT);
}

// Light interactive timer to save power by not turning on light in ambient sunlight
static void prv_light_timer(void *data) {
  if (prv_is_glancing()) { 
    app_timer_register(LIGHT_FADE_TIME_MS, prv_light_timer, data);
    light_enable_interaction();
  } else {
    // no control over triggering fade-out from API
    // so just turn light off for now
    light_enable(false);
  }
}

static void prv_accel_handler(AccelData *data, uint32_t num_samples) {
  static bool unglanced = true;
  uint32_t active_count = 0;
  time_ms_t current_time;
  time_ms(&current_time.sec, &current_time.ms);

  for (uint32_t i = 0; i < num_samples; i++) {
    if(WITHIN_ZONE(active_zone, data[i].x, data[i].y, data[i].z)) {
      active_count++;
      time_ms(&s_last_active.sec, &s_last_active.ms);
      // state must be unglanced before active can be triggered again
      // and all samples must be in the active zone to trigger active
      if (unglanced && active_count == num_samples && 
          ((int64_t)current_time.sec * 1000 + current_time.ms < 
           (int64_t)s_glanced_window.sec * 1000 + s_glanced_window.ms)) {
#ifdef DEBUG
        vibes_double_pulse();
#endif
        unglanced = false;
        prv_update_state(GLANCING_ACTIVE);
        // timeout for glancing
        if (prv_timeout_ms) {
          glancing_timeout_handle = app_timer_register(prv_timeout_ms, glance_timeout, data);
        }
        if (prv_control_backlight) {
          prv_light_timer(NULL);
        }
        return;
      }
    } else if (WITHIN_ZONE(inactive_zone_downward, data[i].x, data[i].y, data[i].z)) {
      unglanced = true;
      time_ms(&s_glanced_window.sec, &s_glanced_window.ms);
      s_glanced_window.sec += DOWNWARD_WINDOW;
      prv_update_state(GLANCING_INACTIVE);
      // Disable timeout if unnecessary
      if (glancing_timeout_handle) {
        app_timer_cancel(glancing_timeout_handle);
      }
      // If even 1 sample was in inactive zone, we trigger unglanced
      // and inactive and return
      return;
    } else if (WITHIN_ZONE(inactive_zone_away, data[i].x, data[i].y, data[i].z)) {
      unglanced = true;
      prv_update_state(GLANCING_INACTIVE);
      // Disable timeout if unnecessary
      if (glancing_timeout_handle) {
        app_timer_cancel(glancing_timeout_handle);
      }
      // only restart unglanced timer if they were in the active range just before this
      if (((int64_t)current_time.sec * 1000 + current_time.ms < 
          (int64_t)s_last_active.sec * 1000 + s_last_active.ms + ROLL_WINDOW_MS)) {
        time_ms(&s_glanced_window.sec, &s_glanced_window.ms);
        s_glanced_window.sec += AWAY_WINDOW;
      }
      // If even 1 sample was in inactive zone, we trigger unglanced
      // and inactive and return
      return;
    }
  }

  if (!active_count) {
    // never hit active or inactive zones (ie. Dead zone): just kill it, but not unglanced
    prv_update_state(GLANCING_INACTIVE);
    // Disable timeout if unnecessary
    if (glancing_timeout_handle) {
      app_timer_cancel(glancing_timeout_handle);
    }
  }
}

static void prv_tap_handler(AccelAxisType axis, int32_t direction) {
  if(!prv_is_glancing()) {
    // Enable the old flick behaviour for backlight
    if (prv_legacy_flick_backlight) {
      light_enable_interaction();
    } else {
      // force light to be off when we are not looking
      // to override previous flick light behavior
      light_enable(false);
    }
  }
}

void glancing_service_subscribe(int timeout_ms, bool control_backlight, 
                                bool legacy_flick_backlight, GlancingDataHandler handler) {
  prv_handler = handler;
  prv_timeout_ms = (timeout_ms > 0) ? timeout_ms : 0;

  // Setup motion accel handler with low sample rate
  // 10 hz with buffer for 5 samples for 0.5 second update rate
  accel_data_service_subscribe(5, prv_accel_handler);
  accel_service_set_sampling_rate(ACCEL_SAMPLING_25HZ);
  
  prv_legacy_flick_backlight = legacy_flick_backlight;
  prv_control_backlight = control_backlight;

  if (prv_control_backlight) {
    // Setup tap service to support or disable flick to light behavior
    accel_tap_service_subscribe(prv_tap_handler);
  }
}

void glancing_service_unsubscribe() {
  accel_data_service_unsubscribe();
  if (prv_control_backlight) {
    accel_tap_service_unsubscribe();
  }
}
