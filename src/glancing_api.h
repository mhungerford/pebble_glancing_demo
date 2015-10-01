#pragma once

#include <pebble.h>

typedef enum {
  GLANCING_INACTIVE = 0,
  GLANCING_ACTIVE = 1,
  GLANCING_TIMEDOUT = 2,
} GlanceState;

typedef struct GlancingData {
  GlanceState state;
} GlancingData;

typedef void (*GlancingDataHandler)(GlancingData *data);

void glancing_service_subscribe(
    int timeout_ms, bool control_backlight, GlancingDataHandler handler);

void glancing_service_unsubscribe();
