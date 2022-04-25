/**
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

// Marquee controls

// Instantiate color selector
var hueb = new Huebee('#color-input', {
    // options
    notation: 'hex',
    saturations: 1,
    staticOpen: true
});

hueb.on('change', function (color, hue, sat, lum) {
    fetch("/color", {
        method: "POST",
        body: new FormData(document.getElementById("color-form"))
    });
});

document.getElementById("brightness-input").addEventListener("change", (e) => {
    fetch("/brightness", {
        method: "POST",
        body: new FormData(document.getElementById("brightness-form"))
    });
});

document.getElementById("speed-input").addEventListener("change", (e) => {
    fetch("/speed", {
        method: "POST",
        body: new FormData(document.getElementById("speed-form"))
    });
});
