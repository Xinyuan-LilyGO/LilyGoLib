#pragma once

// Override for the entire build, or change this default when using Arduino IDE.
#ifndef LILYGO_DEBUG_ENABLED
#define LILYGO_DEBUG_ENABLED 0
#endif

#if LILYGO_DEBUG_ENABLED != 0 && LILYGO_DEBUG_ENABLED != 1
#error "LILYGO_DEBUG_ENABLED must be 0 or 1"
#endif
