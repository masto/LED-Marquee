# Copyright 2022 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Word clock

import argparse
import json
import time

import paho.mqtt.client as mqtt

digits = [
    "zero",
    "one",
    "two",
    "three",
    "four",
    "five",
    "six",
    "seven",
    "eight",
    "nine",
    "ten",
    "eleven",
    "twelve",
    "thirteen",
    "fourteen",
    "fifteen",
    "sixteen",
    "seventeen",
    "eighteen",
    "nineteen",
]

tens = ["", "ten", "twenty", "thirty", "fourty", "fifty"]


def word(n):
    if n < 20:
        return digits[n]

    t = int(n / 10)
    d = n % 10
    if d == 0:
        return tens[t]
    else:
        return tens[t] + "-" + digits[d]


def time_words():
    local_time = time.localtime()

    pm = False
    h = local_time.tm_hour
    if h > 12:
        h -= 12
        pm = True

    timestr = word(h)

    m = local_time.tm_min
    if m == 0:
        timestr += " o'clock"
    elif m < 10:
        timestr += " oh " + word(m)
    else:
        timestr += " " + word(m)

    s = local_time.tm_sec
    if s == 1:
        timestr += " and one second"
    elif s > 0:
        timestr += " and " + word(s) + " seconds"

    if local_time.tm_hour > 17:
        timestr += " in the evening"
    elif local_time.tm_hour > 11:
        timestr += " in the afternoon"
    else:
        timestr += " in the morning"

    return timestr


def on_ready(client, userdata, message):
    # Skip "pre-queuing". This is real-time(-ish).
    payload = json.loads(message.payload)
    if not payload["ready"]:
        return

    message = f"It's {time_words()}."
    print(message)

    base_topic = userdata["base_topic"]
    client.publish(f"{base_topic}/text", json.dumps({"text": message}))


def on_connect(client, userdata, flags, rc):
    if rc != 0:
        sys.exit(f"MQTT failed to connect: {mqtt.connack_string(rc)}")


def on_disconnect(client, userdata, rc):
    if rc != 0:
        sys.exit(f"MQTT disconnected: {mqtt.connack_string(rc)}")


def get_args():
    parser = argparse.ArgumentParser(
        description="Shows the time written out in English."
    )
    parser.add_argument("--marquee_node", required=True, help="MQTT marquee node name")
    parser.add_argument("--mqtt_server", default="mqtt", help="MQTT server name")
    parser.add_argument("--mqtt_user", help="MQTT auth user name")
    parser.add_argument("--mqtt_pass", help="MQTT auth password")

    args = parser.parse_args()

    return args


def main():
    args = get_args()

    base_topic = f"marquee/{args.marquee_node}"

    client = mqtt.Client(userdata={"base_topic": base_topic})
    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    if args.mqtt_user:
        client.username_pw_set(args.mqtt_user, args.mqtt_pass)
    client.connect(args.mqtt_server)

    client.subscribe(f"{base_topic}/ready")
    client.message_callback_add(f"{base_topic}/ready", on_ready)

    client.loop_forever()


if __name__ == "__main__":
    main()
