#include <pebble.h>
#include "glancing_api.h"

static char active_str[] = "ACTIVE";
static char inactive_str[] = "INACTIVE";
static char timedout_str[] = "TIMED_OUT";

static Window *window;

static TextLayer *time_text_layer;
char time_string[] = "00:00:00";

static bool seconds_mode = false;
static TextLayer *glance_text_layer;
char glance_string[16] = "INACTIVE";

static GlanceState state = GLANCING_INACTIVE;

void tick_handler(struct tm *tick_time, TimeUnits units_changed){
  if (seconds_mode) {
    strftime(time_string, sizeof(time_string), 
        clock_is_24h_style() ? "%H:%M:%S" : "%I:%M:%S", tick_time);
  } else {
    // Format only with hour:minute
    strftime(time_string, sizeof(time_string), 
        clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);
  }
  layer_mark_dirty(text_layer_get_layer(time_text_layer));
}

void glancing_callback(GlancingData *data) {
  time_t current_time;
 
  switch (data->state) {
    case GLANCING_ACTIVE:
      seconds_mode = true;
      tick_timer_service_subscribe((SECOND_UNIT), tick_handler);
      //Kick the tick_handler for instant update
      current_time = time(NULL);
      tick_handler(localtime(&current_time),SECOND_UNIT);
      strncpy(glance_string, active_str, sizeof(glance_string) - 1);
      window_set_background_color(window, GColorGreen); // Green for active
      break;
    case GLANCING_TIMEDOUT:
      seconds_mode = false;
      strncpy(glance_string, timedout_str, sizeof(glance_string) - 1);
      window_set_background_color(window, GColorBlue);  // Blue for timedout
      break;
    case GLANCING_INACTIVE:
    default:
      seconds_mode = false;
      //Kick the tick_handler for instant update
      current_time = time(NULL);
      tick_handler(localtime(&current_time),MINUTE_UNIT);
      tick_timer_service_subscribe((MINUTE_UNIT), tick_handler);
      // Leave the TIMEDOUT message
      if (state != GLANCING_TIMEDOUT) {
        strncpy(glance_string, inactive_str, sizeof(glance_string) - 1);
      }
      window_set_background_color(window, GColorRed);  // Red for inactive
      break;
  }
  layer_mark_dirty(text_layer_get_layer(glance_text_layer));
  state = data->state;
}

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  const GPoint center = grect_center_point(&bounds);

  time_text_layer = text_layer_create(GRect (0, center.y, bounds.size.w, 32)); 
  text_layer_set_text(time_text_layer, time_string);
  text_layer_set_font(time_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_color(time_text_layer, GColorWhite);
  text_layer_set_background_color(time_text_layer, GColorClear);
  text_layer_set_text_alignment(time_text_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(time_text_layer));


  glance_text_layer = text_layer_create(GRect (0, center.y + 30, bounds.size.w, 32)); 
  text_layer_set_text(glance_text_layer, glance_string);
  text_layer_set_font(glance_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_color(glance_text_layer, GColorWhite);
  text_layer_set_background_color(glance_text_layer, GColorClear);
  text_layer_set_text_alignment(glance_text_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(glance_text_layer));

  // Force time update
  time_t current_time = time(NULL);
  struct tm *current_tm = localtime(&current_time);
  tick_handler(current_tm, MINUTE_UNIT);
  
  // Setup tick time handler
  tick_timer_service_subscribe((MINUTE_UNIT), tick_handler);

  // Enable Glancing with normal 5 second timeout, takeover backlight
  glancing_service_subscribe(5 * 1000, true, false, glancing_callback);
}

static void window_unload(Window *window) {
  text_layer_destroy(time_text_layer);
}

static void init(void) {
  window = window_create();
  window_set_background_color(window, GColorRed);
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(window, true);
}

static void deinit(void) {
  window_destroy(window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
