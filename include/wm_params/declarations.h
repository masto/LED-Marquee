/*
 * Copyright 2022 Google LLC
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

// Declare user parameters

#define WM_STR_PARAM(param, desc, default, len) \
  char param[len] = default;                    \
  WiFiManagerParameter custom_##param(#param, desc, default, len);

#define WM_NUM_PARAM(param, desc, default, len) \
  uint16_t param = default;                     \
  WiFiManagerParameter custom_##param(#param, desc, #default, len);

#define WM_HTML(html)

WM_USER_PARAMS

#undef WM_STR_PARAM
#undef WM_NUM_PARAM
#undef WM_HTML
