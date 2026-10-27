EQUILIBRIUM OUTPOST - all files of the escape room (team 3)
=========================================================

OUR OWN
  1_laptop/main_game_screen.html
  1_laptop/walkie_laptop_sender.html
  2_phones/walkie_phone_receiver.html
  3_esp32_boards/controller_valves_gas_servo_oled/   (valves, servo box, OLED, LED strip)

FROM ANOTHER GROUP
  2_phones/morse_touchee_phone_sender.html           (Touch-ee page)
  3_esp32_boards/encoder_dials_morse_leds/           (rotary encoder module)
  3_esp32_boards/fire_module/                        (fire module)
  gas part (slider + potentiometer) inside the controller sketch

"mr-kip" (the valve sensor) is the standard OOCSI thing, used unchanged. No code of it in this folder.

WHAT GOES WHERE
  1_laptop/        open on the main laptop: the game screen + the microphone page
  2_phones/        the two phone pages: Touch-ee (morse sender) + walkie talkie receiver (in the box)
  3_esp32_boards/  one folder per ESP32 board. Open the .ino in the Arduino IDE (folder name = sketch name).

OOCSI CHANNELS (oocsi.id.tue.nl)
  OOCSI-things/team-3          valves, gas bars, encoders, Touch-ee
  OOCSI-things/team-3-game     game flow (stage, phone_open, fire, fire_out ...)
  OOCSI-things/team-3-walkie   walkie talkie connection

NOTES
  - The code (4152) is set in main_game_screen.html (FIRE_CODE) and morse_touchee_phone_sender.html (SECRET_CODE).
  - The two walkie pages must be served by the Express server (public/ folder). Their file names are now
    walkie_laptop_sender.html and walkie_phone_receiver.html, so the links changed too.
  - WiFi name / password are at the top of each .ino file.
