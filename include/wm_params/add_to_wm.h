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

// Add parameters to WifiManager

#define WM_STR_PARAM(param, desc, default, len)      \
  custom_##param.setValue(param, sizeof(param) - 1); \
  wm.addParameter(&custom_##param);

#define WM_NUM_PARAM(param, desc, default, len)        \
  custom_##param.setValue(String(param).c_str(), len); \
  wm.addParameter(&custom_##param);

#define CONCAT(x, y) x##y
#define WM_HTML_(html, idx)                                      \
  static String CONCAT(html_header_str_, idx)(html);             \
  static WiFiManagerParameter CONCAT(                            \
      html_header_, idx)(CONCAT(html_header_str_, idx).c_str()); \
  wm.addParameter(&CONCAT(html_header_, idx));
#define WM_HTML(html) WM_HTML_(html, __COUNTER__)

WM_USER_PARAMS

#undef WM_STR_PARAM
#undef WM_NUM_PARAM
#undef CONCAT
#undef WM_HTML_
#undef WM_HTML
