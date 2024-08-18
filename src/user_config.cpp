// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "user_config.h"

#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <string.h>

#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "debug_serial.h"

namespace led_marquee {

const char *UserConfig::StringValue(std::string param) const {
  return params_.at(param).value.get();
}

const int UserConfig::IntValue(std::string param) const {
  return String(StringValue(param)).toInt();
}

void UserConfig::AddParam(const std::string name, const std::string desc,
                          const String default_value, const int len) {
  // Allocate string buffer for the value and copy default to it.
  auto value = std::unique_ptr<char[]>(new char[len + 1]);
  strlcpy(value.get(), default_value.c_str(), len + 1);
  // Hang on to the storage for all these strings, since WiFiManager doesn't
  // take ownership of anything.
  const auto &[it, insertion] = params_.try_emplace(
      name, UserParameter{name, desc, std::move(value), len});
  auto &param = it->second;

  // Add the new parameter to WiFiManager.
  auto wm_param = std::make_unique<WiFiManagerParameter>(
      param.name.c_str(), param.desc.c_str(), param.value.get(), len);
  wm_->addParameter(wm_param.get());
  param.wm_param = std::move(wm_param);
}

void UserConfig::AddHtml(const String html) {
  // Copy the string to owned storage.
  auto value = std::unique_ptr<char[]>(new char[html.length() + 1]);
  strlcpy(value.get(), html.c_str(), html.length() + 1);
  auto &param = wm_html_.emplace_back(UserParameter{.value = std::move(value)});

  // Add the HTML to WiFiManager.
  auto wm_param = std::make_unique<WiFiManagerParameter>(param.value.get());
  wm_->addParameter(wm_param.get());
  param.wm_param = std::move(wm_param);
}

void UserConfig::ReadFromWifiManager() {
  for (auto &[name, param] : params_) {
    strlcpy(param.value.get(), param.wm_param->getValue(), param.len + 1);
  }
}

void UserConfig::ReadFromJson(const DynamicJsonDocument &json) {
  for (auto &[name, param] : params_) {
    if (json.containsKey(name)) {
      String tmp;
      const char *source;
      if (json[name].is<const char *>()) {
        source = json[name];
      } else if (json[name].is<int>()) {
        tmp = String(json[name].as<int>());
        source = tmp.c_str();
      }
      if (source) {
        strlcpy(param.value.get(), source, param.len + 1);
        param.wm_param->setValue(param.value.get(), param.len);
      } else {
        debug_printf("Unknown JSON type for '%s'\n", name.c_str());
      }
    }
  }
}

void UserConfig::ToJson(DynamicJsonDocument &json) {
  for (auto &[name, param] : params_) {
    json[name] = param.value.get();
  }
}

}  // namespace led_marquee
