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

# Display random messages from fortune file

import argparse
import colorsys
import io
import json
import random
import sys
from os import path

import paho.mqtt.client as mqtt


def pick_color():
    hue = random.random()
    rgb = colorsys.hsv_to_rgb(hue, 1.0, 1.0)
    return {"r": int(rgb[0] * 255), "g": int(rgb[1] * 255), "b": int(rgb[2] * 255)}


def getline(f: io.TextIOBase, n: int):
    f.seek(0)
    for i in range(1, n):
        f.readline()
    return f.readline()


def on_ready(client, userdata, message):
    n = random.randrange(0, userdata["line_count"])
    print(f"chose line {n}:")

    raw_line = getline(userdata["file"], n).strip()

    chopped_line = raw_line[2:]
    print(chopped_line)

    base_topic = userdata["base_topic"]
    client.publish(f"{base_topic}/set", json.dumps({"color": pick_color()}))
    client.publish(f"{base_topic}/text", json.dumps({"text": chopped_line}))


def count_lines(f: io.TextIOBase):
    lines = 0
    f.seek(0)
    while f.readline():
        lines += 1

    return lines


def on_connect(client, userdata, flags, rc):
    if rc != 0:
        sys.exit(f"MQTT failed to connect: {mqtt.connack_string(rc)}")


def on_disconnect(client, userdata, rc):
    if rc != 0:
        sys.exit(f"MQTT disconnected: {mqtt.connack_string(rc)}")


def get_args():
    parser = argparse.ArgumentParser(
        description="Shows random lines from a fortune file."
    )
    parser.add_argument(
        "--fortune_file", default="fortunes.txt", help="Fortune file name"
    )
    parser.add_argument("--marquee_node", required=True, help="MQTT marquee node name")
    parser.add_argument("--mqtt_server", default="mqtt", help="MQTT server name")
    parser.add_argument("--mqtt_user", help="MQTT auth user name")
    parser.add_argument("--mqtt_pass", help="MQTT auth password")

    args = parser.parse_args()

    return args


def main():
    args = get_args()

    f = open(args.fortune_file, "r")

    line_count = count_lines(f)
    print(f"{line_count} lines in fortune file")

    base_topic = f"marquee/{args.marquee_node}"

    client = mqtt.Client(
        userdata={"line_count": line_count, "file": f, "base_topic": base_topic}
    )
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
