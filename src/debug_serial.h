/*
 * Copyright 2024 Christopher Masto
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef LED_MARQUEE_DEBUG_SERIAL_H_
#define LED_MARQUEE_DEBUG_SERIAL_H_

// Set to true to enable serial, which may cause scrolling glitches
#define LM_SERIAL_DEBUG 0

#if LM_SERIAL_DEBUG
#define debug_begin(...) Serial.begin(__VA_ARGS__);
#define debug_print(...) Serial.print(__VA_ARGS__);
#define debug_println(...) Serial.println(__VA_ARGS__);
#define debug_printf(...) Serial.printf(__VA_ARGS__);
#define debug_setDebugOutput(...) Serial.setDebugOutput(__VA_ARGS__);
#else
#define debug_begin(...)
#define debug_print(...)
#define debug_println(...)
#define debug_printf(...)
#define debug_setDebugOutput(...)
#endif

#endif  // LED_MARQUEE_DEBUG_SERIAL_H_
