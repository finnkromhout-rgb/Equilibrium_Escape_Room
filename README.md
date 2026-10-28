# Equilibrium Outpost - escape room (team 3)

All code of our escape room: the laptop pages, the phone pages and the ESP32 boards. Everything talks over [OOCSI](https://oocsi.id.tue.nl).

## Folders

| Folder | What is in it |
|---|---|
| `1_laptop/` | `main_game_screen.html` (the whole game) and `walkie_laptop_sender.html` (microphone) |
| `2_phones/` | `morse_touchee_phone_sender.html` (morse code) and `walkie_phone_receiver.html` (speaker in the box) |
| `3_esp32_boards/` | One Arduino sketch per ESP32 board. Open the `.ino` in the Arduino IDE |

## Who made what

**Our own**
- `1_laptop/main_game_screen.html`
- `1_laptop/walkie_laptop_sender.html`
- `2_phones/walkie_phone_receiver.html`
- `3_esp32_boards/controller_valves_gas_servo_oled/` (valves, servo box, OLED, LED strip)

**From another group**
- `2_phones/morse_touchee_phone_sender.html` (Touch-ee page)
- `3_esp32_boards/encoder_dials_morse_leds/` (rotary encoder module)
- `3_esp32_boards/fire_module/` (fire module)
- the gas part (slider + potentiometer) inside the controller sketch

`mr-kip` (the valve sensor) is the standard OOCSI thing, used unchanged. Its code is not in this repository.

## OOCSI channels

| Channel | Used for |
|---|---|
| `OOCSI-things/team-3` | valves, gas bars, encoders, Touch-ee |
| `OOCSI-things/team-3-game` | game flow (`stage`, `phone_open`, `fire`, `fire_out`, ...) |
| `OOCSI-things/team-3-walkie` | walkie talkie connection |

## Set-up notes

- The morse code (`4152`) is set in `main_game_screen.html` (`FIRE_CODE`) and in `morse_touchee_phone_sender.html` (`SECRET_CODE`). Keep both the same.
- The two walkie pages are served by an Express server (`public/` folder, port 3000), for example `http://localhost:3000/walkie_laptop_sender.html`. The phone opens `walkie_phone_receiver.html` through the cloudflared link.
- Each `.ino` has the WiFi name and password at the top. Change them to your own network.
- Board: ESP32-C6 DevKitC-1, Arduino IDE with ESP32 core 3.x.
