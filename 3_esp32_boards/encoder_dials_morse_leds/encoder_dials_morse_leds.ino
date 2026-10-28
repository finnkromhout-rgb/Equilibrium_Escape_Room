/******************************************************************************
   Equilibrium Outpost - Code dials (4 rotary encoders) + Morse LEDs

   WHO MADE IT : BASED ON ANOTHER GROUP'S MODULE - we rewired / changed it for our escape room.
                 Original: the Touch-ee / rotary encoder module of the other group.
                 Changed by us: our own OOCSI channels, interrupt reading, equal clicks
                 per number, blue / yellow morse LEDs.
   BOARD       : ESP32-C6 DevKitC-1 (Arduino IDE, ESP32 core 3.x)
   LIBRARIES   : OOCSI only
   WIFI        : change ssid / password below to your hotspot

   It talks to the laptop page (main_game_screen.html) over our own OOCSI channels.

   Morse side : the Touch-ee page ("Control board") sends touchee_x on our channel:
                blue button = 0.6, yellow button = -0.6, released = 0.
                The BLUE LED is on only while the blue circle is pressed, the
                YELLOW LED only while the yellow circle is pressed. When the
                circle is released the LED goes dark again.
   Dial side  : 4 rotary encoders. Every number is EXACTLY the same amount of clicks
                (CLICKS_PER_NUMBER, default 4 = 72 degrees on a 20-click encoder).
                Pointing up = 1, then 2, 3, 4, 5, and after 5 back to 1.
                The encoders are read with interrupts, so no click is missed while
                the WiFi is busy. Click = send that number.
                Master button = reset all encoders to 1.

   OOCSI channels (must match the html on the laptop)
     CHANNEL       OOCSI-things/team-3       in : touchee_x (from the Touch-ee page)
                                             out: enc_event  "turn" | "submit" | "reset"
                                                  enc_index  0..3   (which encoder)
                                                  enc_pos    1..5   (its number)
     CHANNEL_GAME  OOCSI-things/team-3-game  in : stage  (when it becomes "fire" all dials reset to 1)

   Pins for the ESP32-C6 (DevKitC-1), as in the wiring schematic
     LEDs: blue 22 (68 ohm), yellow 23 (220 ohm), other leg of each LED to GND
     Encoders (S1, S2, KEY): 1 = 0,1,2   2 = 3,6,7   3 = 18,19,21   4 = 10,11,20
     Encoder modules: GND to GND, 5V pin to 3.3V
     Master reset button: GPIO 9 (BOOT pin, other leg to GND)
     Note: do not hold the master button while pressing reset / plugging in USB,
           otherwise the board starts in upload mode.
 ******************************************************************************/

#include "OOCSI.h"

// --- WiFi credentials ---
const char* ssid = "iPhone";
const char* password = "finn1234";

// --- OOCSI connection settings ---
const char* OOCSIName = "Equilibrium_Valves_04";   // must be unique on the server
const char* hostserver = "oocsi.id.tue.nl";
OOCSI oocsi = OOCSI();

const char* CHANNEL      = "OOCSI-things/team-3";        // html: CH_HARDWARE (+ Touch-ee page)
const char* CHANNEL_GAME = "OOCSI-things/team-3-game";   // html: CH_GAME

// ---------------------------------------------------------------- LEDs
// two LEDs that copy the morse buttons on the Touch-ee phone page
#define LED1_PIN 22          // blue   (Touch-ee left button)
#define LED2_PIN 23          // yellow (Touch-ee right button)

// Your LEDs light up when the pin is LOW (the LED is fed from 3.3V and the pin "pulls it down").
// So: LOW = LED on, HIGH = LED off.
// If the LEDs ever behave the wrong way round again (lit when nothing is pressed), swap these two.
#define LED_ON  LOW
#define LED_OFF HIGH
const unsigned long NO_SIGNAL_OFF_MS = 600;  // while pressed the Touch-ee keeps sending; if nothing arrives for this long the LED goes dark
int ledZone = 0;                            // 0 = both dark, 1 = blue on, 2 = yellow on
unsigned long lastZoneMsgTime = 0;          // when the last blue/yellow message arrived

// switch the two LEDs
void setLeds(bool blue, bool yellow) {
  digitalWrite(LED1_PIN, blue ? LED_ON : LED_OFF);
  digitalWrite(LED2_PIN, yellow ? LED_ON : LED_OFF);
}

// zone: 0 = both off, 1 = blue on, 2 = yellow on
void showZone(int zone) {
  ledZone = zone;
  setLeds(zone == 1, zone == 2);
}

// safety: if the "released" message gets lost, the LED still goes dark
void updateLeds() {
  if (ledZone != 0 && millis() - lastZoneMsgTime > NO_SIGNAL_OFF_MS) showZone(0);
}

// ---------------------------------------------------------------- encoders
const uint8_t pinMasterReset = 9;   // master button (BOOT button of the board): all dials back to 1
bool masterResetPressed = false;

// everything we need to remember for ONE encoder
struct RotaryEncoder {
  const char* name;
  const uint8_t pinA;     // S1
  const uint8_t pinB;     // S2
  const uint8_t pinBtn;   // Key (push switch)
  volatile long clicks;   // counted by the interrupt (+1 / -1 per click)
  volatile uint8_t ab;    // last state of S1/S2
  volatile int8_t sub;    // steps inside one click
  bool btnPressed;
  unsigned long lastBtnTime;
  int lastValidPos;       // last confirmed number 1..5
};

// how many clicks of the knob for ONE number (the same for every number)
// 4 clicks = 72 degrees on a 20-click encoder, so 1..5 is exactly one full turn.
// Turns too far / not far enough per number? Only change this value.
const int CLICKS_PER_NUMBER = 4;
const int NUM_POSITIONS = 5;                // numbers 1..5
const unsigned long BTN_DEBOUNCE_MS = 40;

// the 4 encoders and their pins
RotaryEncoder encoders[] = {
  // name,   S1, S2, Key, clicks, ab, sub, btn,  tBtn, pos
  { "Enc 1", 0,  1,  2,   0,      3,  0,   false, 0,    1 },
  { "Enc 2", 3,  6,  7,   0,      3,  0,   false, 0,    1 },
  { "Enc 3", 18, 19, 21,  0,      3,  0,   false, 0,    1 },
  { "Enc 4", 10, 11, 20,  0,      3,  0,   false, 0,    1 }
};
// number of encoders (counted automatically)
const int NUM_ENCODERS = sizeof(encoders) / sizeof(encoders[0]);

// interrupt: runs on every change of S1 or S2. A full click = 4 valid steps,
// counted only when the knob is back in its rest position (filters out contact bounce)
const int8_t QUAD[16] = { 0, -1, 1, 0,  1, 0, 0, -1,  -1, 0, 0, 1,  0, 1, -1, 0 };

void IRAM_ATTR encoderISR(void* arg) {
  RotaryEncoder* e = (RotaryEncoder*)arg;
  uint8_t now = (digitalRead(e->pinA) << 1) | digitalRead(e->pinB);
  e->sub += QUAD[(e->ab << 2) | now];
  e->ab = now;
  if (now == 3) {                         // rest position (both HIGH)
    if (e->sub >= 2)       e->clicks++;
    else if (e->sub <= -2) e->clicks--;
    e->sub = 0;
  }
}

// clicks -> number 1..5. Every number is exactly CLICKS_PER_NUMBER clicks wide,
// centred on its own mark, and after 5 it goes round to 1 again.
int clicksToNumber(long c) {
  long cycle = (long)CLICKS_PER_NUMBER * NUM_POSITIONS;
  long m = ((c + CLICKS_PER_NUMBER / 2) % cycle + cycle) % cycle;
  return m / CLICKS_PER_NUMBER + 1;
}

// ---------------------------------------------------------------- sending
// send one encoder event to the laptop: "turn", "submit" or "reset"
void sendEncoder(const char* event, int index, int pos) {
  oocsi.newMessage(CHANNEL);
  oocsi.addString("enc_event", event);
  if (index >= 0) {
    oocsi.addInt("enc_index", index);
    oocsi.addInt("enc_pos", pos);
  }
  oocsi.sendMessage();
  Serial.print("sent ");
  Serial.print(event);
  if (index >= 0) { Serial.print(" enc="); Serial.print(index); Serial.print(" pos="); Serial.print(pos); }
  Serial.println();
}

// all dials back to number 1
void resetAllEncoders(bool announce) {
  for (int i = 0; i < NUM_ENCODERS; i++) {
    encoders[i].clicks = 0;
    encoders[i].sub = 0;
    encoders[i].lastValidPos = 1;
  }
  Serial.println("all encoders reset to 1");
  if (announce) sendEncoder("reset", -1, 0);
}

// ---------------------------------------------------------------- OOCSI in
// called for every incoming OOCSI message
void processOOCSI() {
  // new game stage: start every fire stage with clean dials
  String stage = String(oocsi.getString("stage", ""));
  if (stage == "fire") resetAllEncoders(false);

  // Touch-ee message (-200 = key not in this message)
  float x = oocsi.getFloat("touchee_x", -200);
  if (x == -200) return;

  // which button does the Touch-ee say is pressed? (blue = 0.6, yellow = -0.6, nothing = 0)
  int zone = 0;
  if (x > 0.4 && x < 0.85)        zone = 1;    // blue
  else if (x < -0.4 && x > -0.85) zone = 2;    // yellow

  // LED follows the button: blue circle pressed = blue LED, yellow circle pressed = yellow LED,
  // released (or anywhere else) = dark
  if (zone != 0) lastZoneMsgTime = millis();
  if (zone != ledZone) {
    showZone(zone);
    Serial.println(zone == 1 ? "BLUE on" : (zone == 2 ? "YELLOW on" : "LEDs off"));
  }

  Serial.print("touchee x: ");
  Serial.println(x);
}

// ---------------------------------------------------------------- setup / loop
// runs once at power on
void setup() {
  Serial.begin(115200);
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  showZone(0);

  pinMode(pinMasterReset, INPUT_PULLUP);
  for (int i = 0; i < NUM_ENCODERS; i++) {
    pinMode(encoders[i].pinA, INPUT_PULLUP);
    pinMode(encoders[i].pinB, INPUT_PULLUP);
    pinMode(encoders[i].pinBtn, INPUT_PULLUP);
    encoders[i].ab = (digitalRead(encoders[i].pinA) << 1) | digitalRead(encoders[i].pinB);
    attachInterruptArg(digitalPinToInterrupt(encoders[i].pinA), encoderISR, &encoders[i], CHANGE);
    attachInterruptArg(digitalPinToInterrupt(encoders[i].pinB), encoderISR, &encoders[i], CHANGE);
  }

  oocsi.connect(OOCSIName, hostserver, ssid, password, processOOCSI);
  oocsi.subscribe(CHANNEL);
  oocsi.subscribe(CHANNEL_GAME);
  Serial.println("Valve module ready");
}

// runs all the time
void loop() {
  oocsi.check();   // incoming messages

  updateLeds();   // safety timeout for the LEDs

  unsigned long now = millis();

  // master reset
  bool resetLow = (digitalRead(pinMasterReset) == LOW);
  if (resetLow && !masterResetPressed) {
    masterResetPressed = true;
    resetAllEncoders(true);
  } else if (!resetLow && masterResetPressed) {
    masterResetPressed = false;
  }

  for (int i = 0; i < NUM_ENCODERS; i++) {
    RotaryEncoder &e = encoders[i];
    // turning: the interrupt counts the clicks, here we turn them into a number 1..5
    int pos = clicksToNumber(e.clicks);
    if (pos != e.lastValidPos) {
      e.lastValidPos = pos;
      sendEncoder("turn", i, pos);              // live number on the laptop
    }

    // click = submit the last confirmed number
    bool btnLow = (digitalRead(e.pinBtn) == LOW);
    if (btnLow && !e.btnPressed && (now - e.lastBtnTime) > BTN_DEBOUNCE_MS) {
      e.btnPressed = true;
      e.lastBtnTime = now;
      sendEncoder("submit", i, e.lastValidPos);
    } else if (!btnLow && e.btnPressed && (now - e.lastBtnTime) > BTN_DEBOUNCE_MS) {
      e.btnPressed = false;
      e.lastBtnTime = now;
    }
  }
}