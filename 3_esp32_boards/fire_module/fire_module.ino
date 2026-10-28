/******************************************************************************
   Equilibrium Outpost - FIRE MODULE (ESP32-C6 DevKitC-1, LED strip + button)

   WHO MADE IT : ANOTHER GROUP'S MODULE (fire module), not ours. It is connected to our
                 OOCSI channels so it works with our laptop page.
   LIBRARIES   : OOCSI, Adafruit NeoPixel
   WIFI        : change ssid / password below to your hotspot

   phone_open from the laptop -> after 2-5 s the strip pulses red/orange (fire)
   press the button           -> strip off, laptop gets fire_out and moves on

   OOCSI channel: OOCSI-things/team-3-game
     in : phone_open (fire starts), stage (resets this module)
     out: fire (true = burning), fire_out (true = put out)

   WIRING (use the numbers printed next to the pins on the board)
     LED strip  +5V  -> 5V
                GND  -> G (GND)
                DIN  -> 5        (330 ohm resistor in between if you have one)
     Button     leg 1 -> 2
                leg 2 -> G (GND)  (a different G pin than the strip is fine)
     The BOOT button on the board also puts the fire out (handy for testing).
 ******************************************************************************/

#include "OOCSI.h"
#include <Adafruit_NeoPixel.h>

// ---------- settings you may need to change ----------
const char* ssid     = "iPhone";
const char* password = "finn1234";
const char* OOCSIName = "Equilibrium_Fire_08";      // unique name on the server

#define NUM_LEDS   30          // number of LEDs on the strip
#define BRIGHTNESS 80          // 0-255

const int STRIP_PIN  = 5;      // strip data
const int BUTTON_PIN = 2;      // big button (other leg to GND)
// BOOT_PIN (= 9, the BOOT button on the board) is already defined by the ESP32 core
// -----------------------------------------------------

const char* hostserver   = "oocsi.id.tue.nl";
const char* CHANNEL_GAME = "OOCSI-things/team-3-game";
OOCSI oocsi = OOCSI();

// RGBW strip (4 colours per LED: red, green, blue, white)
Adafruit_NeoPixel strip(NUM_LEDS, STRIP_PIN, NEO_GRBW + NEO_KHZ800);

// the fire module is always in one of these states:
//   IDLE = nothing yet | WAITING = box opened, fire starts in 2-5 s | BURNING = strip pulses | OUT = put out
enum FireState { IDLE, WAITING, BURNING, OUT };
FireState state = IDLE;
unsigned long fireStartAt = 0;      // time when the fire should start
unsigned long lastHeartbeat = 0;     // last time we told the laptop that it burns
unsigned long lastFrame = 0;
bool wasPressed = false;
unsigned long lastChange = 0;


// ---------- strip ----------
// whole strip in one colour
void stripAll(int r, int g, int b) {
  for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, strip.Color(r, g, b));
  strip.show();
}

// all LEDs off
void stripOff() {
  strip.clear();
  strip.show();
}

// whole strip pulses slowly: dim = deep red, bright = orange, with a small flicker
void animateFire() {
  if (millis() - lastFrame < 40) return;
  lastFrame = millis();
  float phase = (millis() % 1400) / 1400.0f;
  float l = 0.2f + 0.8f * (sinf(phase * 6.2831853f) + 1.0f) / 2.0f;
  l += random(-10, 11) / 100.0f;
  if (l < 0) l = 0;
  if (l > 1) l = 1;
  stripAll((int)(255 * l), (int)(70 * l * l), 0);
}


// ---------- OOCSI ----------
// tell the laptop: fire=true (burning) or fire_out=true (put out)
void sendFire(bool burning, bool out) {
  oocsi.newMessage(CHANNEL_GAME);
  oocsi.addBool("fire", burning);
  if (out) oocsi.addBool("fire_out", true);
  oocsi.sendMessage();
}

// called for every incoming OOCSI message
void processOOCSI() {
  // a new game stage = reset this module
  if (oocsi.has("stage")) {
    state = IDLE;
    stripOff();
    Serial.println("stage message -> reset");
  }
  // the walkie talkie box opened -> the fire starts after a random 2-5 seconds
  if (oocsi.has("phone_open") && state == IDLE) {
    unsigned long wait = random(2000, 5001);
    fireStartAt = millis() + wait;
    state = WAITING;
    Serial.print("phone box open -> fire in "); Serial.print(wait); Serial.println(" ms");
  }
}


// ---------- button ----------
// the big button (or the BOOT button) puts out the fire
void handleButton() {
  bool pressed = (digitalRead(BUTTON_PIN) == LOW) || (digitalRead(BOOT_PIN) == LOW);
  if (pressed == wasPressed || millis() - lastChange < 50) return;   // no change / debounce
  wasPressed = pressed;
  lastChange = millis();
  if (!pressed) return;

  Serial.println("BUTTON PRESSED");
  if (state == BURNING) {
    state = OUT;
    stripOff();
    sendFire(false, true);
    Serial.println("FIRE OUT -> sent to the laptop");
  }
}


// ---------- setup + loop ----------
// runs once at power on
void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BOOT_PIN, INPUT_PULLUP);

  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  stripAll(255, 60, 0);          // short orange flash = strip works
  delay(600);
  stripOff();

  randomSeed(micros());
  Serial.println("connecting to WiFi + OOCSI...");
  oocsi.connect(OOCSIName, hostserver, ssid, password, processOOCSI);
  oocsi.subscribe(CHANNEL_GAME);
  Serial.println("ready. Press the button: you should see BUTTON PRESSED");
}

// runs all the time
void loop() {
  oocsi.check();      // incoming messages
  handleButton();     // is the button pressed?

  // waiting time over -> the fire starts
  if (state == WAITING && (long)(millis() - fireStartAt) >= 0) {
    state = BURNING;
    sendFire(true, false);
    lastHeartbeat = millis();
    Serial.println("FIRE! press the button");
  }

  // burning: pulse the strip and keep telling the laptop
  if (state == BURNING) {
    animateFire();
    if (millis() - lastHeartbeat >= 2000) {      // keep telling the laptop it burns
      lastHeartbeat = millis();
      sendFire(true, false);
    }
  }

  delay(2);
}