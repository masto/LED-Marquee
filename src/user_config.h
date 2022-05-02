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

#ifndef LED_MARQUEE_USER_CONFIG_H_
#define LED_MARQUEE_USER_CONFIG_H_

#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <string.h>

#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace led_marquee {

// WiFiManager parameters are kind of clunky to work with, so this class wraps
// them and takes care of allocating storage and copying to/from JSON.
class UserConfig {
 public:
  UserConfig(const UserConfig &) = default;
  UserConfig &operator=(const UserConfig &) = default;

  explicit UserConfig(std::shared_ptr<WiFiManager> wm) : wm_(wm){};

  const char *StringValue(std::string param) const;
  const int IntValue(std::string param) const;

  void AddParam(const std::string name, const std::string desc,
                const String default_value, const int len);
  void AddHtml(const String html);

  void ReadFromWifiManager();
  void ReadFromJson(const DynamicJsonDocument &json);
  void ToJson(DynamicJsonDocument &json);

 private:
  struct UserParameter {
    const std::string name;
    const std::string desc;
    std::unique_ptr<char[]> value;
    int len;
    std::unique_ptr<WiFiManagerParameter> wm_param;
  };

  std::unordered_map<std::string, UserParameter> params_;
  std::vector<UserParameter> wm_html_;

  std::shared_ptr<WiFiManager> wm_;
};

}  // namespace led_marquee

#endif  // LED_MARQUEE_USER_CONFIG_H_