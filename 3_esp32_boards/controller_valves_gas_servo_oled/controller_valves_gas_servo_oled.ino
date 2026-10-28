/******************************************************************************
   Equilibrium Outpost - Controller (boiler room + gas bars + servo box on ONE ESP32)

   WHO MADE IT : MIXED
                 OUR OWN        : valve puzzle (Job 1), servo box (Job 3), OLED screens, LED strip
                 ANOTHER GROUP'S: gas bar controller, slider + potentiometer (Job 2)
                 "mr-kip" (sends mr-kip_d1) is the standard OOCSI thing, used unchanged.
   BOARD       : ESP32-C6 DevKitC-1 (Arduino IDE, ESP32 core 3.x)
   LIBRARIES   : OOCSI, Adafruit NeoPixel, Adafruit GFX, Adafruit SH110X
   WIFI        : change ssid / password below to your hotspot

   Job 1  Boiler valve puzzle : OLED hints + LED progress strip. The valve
                                position comes from the "mr-kip" thing (mr-kip_d1).
   Job 2  Gas bar controller  : slider = which bar, potentiometer = bar height.
                                No button: the laptop locks a bar by itself once it
                                has been held in the right position.
                                Shown on the laptop (stage 3 of the html).
   Job 3  Servo (phone box)   : 90 degrees (vertical, closed) at the start. When the
                                morse code is entered on the laptop (phone_open or
                                box_open) it turns to 45 degrees = the box with the
                                walkie talkie opens and the communication starts.
                                A new run (stage "boiler") closes it again.

   OLED       : shows the valve hints, and after every finished puzzle it says
                which puzzle to go to next (names can be changed under "OLED: NEXT PUZZLE"):
                valves done      -> GO TO: MORSE CODE TRANSMITTER
                code entered     -> GO TO: WALKIE TALKIE BOX
                fire starts      -> GO TO: FIRE (put it out with the button)
                fire out         -> GO TO: GAS DISTRIBUTION
                gas done         -> ESCAPE COMPLETE

   OOCSI channels (these must match the html on the laptop)
     CHANNEL       OOCSI-things/team-3        in : unlock_next, mr-kip_d1
                                              out: revealed_step, revealed_pressure,
                                                   step_done
     CHANNEL_BARS  OOCSI-things/team-3        out: bar_index, bar_height
     CHANNEL_GAME  OOCSI-things/team-3-game   in : stage  ('boiler','fire','gas','done')
                                                   heat   (0 very hot ... 4 cold, sets the LED colour)
                                                   phone_open / box_open (opens the servo box)
                                                   fire (true = burning, from the fire module) - only for the OLED

   Pins (all different, nothing clashes)
     LED strip 5 | OLED SDA 6, SCL 7 | slider 0 | potentiometer 2 | servo 4
 ******************************************************************************/

#include "OOCSI.h"
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// the three valve positions of the boiler puzzle
enum Direction { DIR_EAST, DIR_SOUTH, DIR_WEST };

// --- WiFi credentials ---
const char* ssid = "iPhone";
const char* password = "finn1234";

// --- OOCSI connection settings ---
const char* OOCSIName = "Equilibrium_Controller_03";  // must be unique on the server
const char* hostserver = "oocsi.id.tue.nl";
OOCSI oocsi = OOCSI();

const char* CHANNEL      = "OOCSI-things/team-3";       // boiler puzzle   (html: CH_BOILER)
const char* CHANNEL_BARS = "OOCSI-things/team-3";       // gas bars, same channel as the boiler (html: CH_HARDWARE)
const char* CHANNEL_GAME = "OOCSI-things/team-3-game";  // game flow       (html: CH_GAME)

// --- Pins ---
#define STRIP_PIN 5
#define OLED_SDA  6
#define OLED_SCL  7
#define SERVO_PIN 4   // servo data pin on GPIO 4 (orange/yellow). Red -> 5V, brown/black -> GND
const int SLIDER_PIN = 0;   // slider middle pin  -> 0
const int POT_PIN    = 2;   // rotary middle pin  -> 2

// --- LED strip (progress of the valve puzzle + heat colour of the room) ---
#define NUM_LEDS 30
Adafruit_NeoPixel strip(NUM_LEDS, STRIP_PIN, NEO_GRBW + NEO_KHZ800);

// --- OLED display (I2C, SH1106 driver): hints and "go to next puzzle" screens ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);


/* ============================================================================
   JOB 3: SERVO (walkie talkie box) - 90 degrees = closed, 45 degrees = open
   ============================================================================ */

#define SERVO_CLOSED  90     // start position (vertical)
#define SERVO_OPEN    45     // position when the box opens (45 degrees back)
#define SERVO_STEP_MS 20     // time per degree: higher = slower and stronger (45 degrees = about 0.9 s)
// (SERVO_OPEN = 135 if the box opens the wrong way)
int servoNow = SERVO_CLOSED;       // where the servo is now
int servoTarget = SERVO_CLOSED;    // where it should go
unsigned long lastServoStep = 0;

void servoWrite(int angle) {
  int pulse = 500 + angle * 2000 / 180;          // 500..2500 microseconds
  ledcWrite(SERVO_PIN, pulse * 65535L / 20000);  // 50 Hz, 16 bit
}

void servoAngle(int angle) { servoTarget = angle; }   // only sets the goal

void servoUpdate() {                                   // called from loop(): moves 1 degree at a time
  if (servoNow == servoTarget || millis() - lastServoStep < SERVO_STEP_MS) return;
  lastServoStep = millis();
  servoNow += (servoTarget > servoNow) ? 1 : -1;
  servoWrite(servoNow);
}


/* ============================================================================
   JOB 1: BOILER VALVE PUZZLE
   ============================================================================ */

// --- Direction thresholds (calibrated to the mounted valve's real readings) ---
// Axis range is roughly -2.0 to 2.0, wrapping at the ends.
const float D1_EAST = 1.0;
const float D1_WEST = -1.0;
const float D1_MAX_ABS = 2.0;  // South sits at the wrap point, near +2.0 or -2.0
const float D1_TOL = 0.25;     // adjust if steps feel too strict or too loose


// true when the valve value d1v is at the wanted direction
bool isAtDirection(Direction dir, float d1v) {
  switch (dir) {
    case DIR_EAST:  return fabs(d1v - D1_EAST) <= D1_TOL;
    case DIR_WEST:  return fabs(d1v - D1_WEST) <= D1_TOL;
    case DIR_SOUTH: return fabs(d1v) >= (D1_MAX_ABS - D1_TOL);
  }
  return false;
}

// --- Sequence (edit freely) ---
// the 3 steps of the valve puzzle: pressure shown on the OLED + the direction to turn the valve to
const int SEQUENCE_LEN = 3;
const float SEQUENCE_PRESSURE[SEQUENCE_LEN] = {2.0, 1.0, 3.0};   // South, East, West pressures
const Direction SEQUENCE_DIR[SEQUENCE_LEN]  = {DIR_SOUTH, DIR_EAST, DIR_WEST};

const unsigned long HOLD_DURATION = 3000;       // hold the valve this long (ms) in the right position

const unsigned long LED_START_DELAY = 1000;

// --- state of the valve puzzle ---
int currentStep = 0;
bool revealed = false;
bool awaitingUnlock = true;
bool inZone = false;
unsigned long zoneStartTime = 0;
bool isSolved = false;
float d1 = 0;

// whole strip in one colour
void setStripColor(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, strip.Color(r, g, b, 0));
  strip.show();
}

// --- Heat level of the boiler room: 0 = very hot ... 4 = cold again ---
//   0 start (very hot)      1 valves done       2 morse code entered
//   3 fire put out          4 gas puzzle solved (cold, all clear)
// The laptop sends the level ("heat"), the colour is chosen here.
int heatLevel = 0;
void heatColor(int level, uint8_t &r, uint8_t &g, uint8_t &b) {
  switch (level) {
    case 0:  r = 255; g = 10;  b = 0;   break;   // red hot
    case 1:  r = 255; g = 30;  b = 0;   break;   // red-orange
    case 2:  r = 255; g = 55;  b = 0;   break;   // orange
    case 3:  r = 255; g = 85;  b = 0;   break;   // light orange (still warm, never white)
    default: r = 0;   g = 110; b = 255; break;   // cold blue
  }
}

// green LEDs = steps done, the rest shows the heat colour; fills up while you hold the valve
void updateProgressBar() {
  int segPerStep = NUM_LEDS / SEQUENCE_LEN;
  int litFromSteps = currentStep * segPerStep;

  float holdProgress = 0;
  if (revealed && inZone) {
    unsigned long elapsed = millis() - zoneStartTime;
    if (elapsed > LED_START_DELAY) {
      holdProgress = constrain((float)(elapsed - LED_START_DELAY) / (HOLD_DURATION - LED_START_DELAY), 0.0, 1.0);
    }
  }
  int litFromHold = holdProgress * segPerStep;
  int totalLit = litFromSteps + litFromHold;

  uint8_t hr, hg, hb;
  heatColor(heatLevel, hr, hg, hb);          // not-yet-done LEDs show how hot it still is
  for (int i = 0; i < NUM_LEDS; i++) {
    if (i < totalLit) strip.setPixelColor(i, strip.Color(0, 255, 0, 0));   // clear green = step done
    else strip.setPixelColor(i, strip.Color(hr, hg, hb, 0));
  }
  strip.show();
}

// ---- OLED screens of the valve puzzle ----
void showWaiting() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 10);
  display.println("AWAITING SIGNAL");
  display.println("FROM CONTROL ROOM...");
  display.display();
}

void showPressure(float p, int stepNum) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.print("STEP "); display.print(stepNum); display.print("/"); display.println(SEQUENCE_LEN);
  display.setTextSize(2);
  display.setCursor(0, 24);
  display.print(p, 1);
  display.println(" BAR");
  display.setTextSize(1);
  display.setCursor(0, 50);
  display.println("HOLD 3s AT POSITION");
  display.display();
}

void showStepComplete() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 20);
  display.println("STEP COMPLETE");
  display.println("AWAITING NEXT SIGNAL...");
  display.display();
}

/* ============================================================================
   OLED: NEXT PUZZLE - after every finished puzzle, say where to go next
   (max 21 characters per line on the OLED, change the names freely)
   ============================================================================ */

// screen order: 0 valves | 1 -> morse | 2 -> walkie box | 3 -> fire | 4 -> gas | 5 escape complete
int oledStage = 0;

// done = what was just finished, next1 / next2 = name of the next puzzle (2 lines)
void showGoTo(const char* done, const char* next1, const char* next2) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.println(done);
  display.drawLine(0, 11, 127, 11, SH110X_WHITE);
  display.setCursor(0, 18);
  display.println("GO TO:");
  display.setCursor(0, 32);
  display.print("> "); display.println(next1);
  display.setCursor(12, 44);
  display.println(next2);
  display.display();
}

// only ever moves forward, so a repeated or late message never shows an old screen
void oledNext(int stageNr) {
  if (stageNr <= oledStage) return;
  oledStage = stageNr;
  switch (stageNr) {
    case 1: showGoTo("VALVES STABILIZED",   "MORSE CODE",       "TRANSMITTER");        break;
    case 2: showGoTo("MORSE CODE ACCEPTED", "WALKIE TALKIE BOX", "(IT IS OPENING)");   break;
    case 3: showGoTo("!! FIRE !!",          "THE FIRE",         "PRESS THE BUTTON");   break;
    case 4: showGoTo("FIRE IS OUT",         "GAS DISTRIBUTION", "(SLIDER + KNOB)");    break;
    case 5:
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SH110X_WHITE);
      display.setCursor(0, 0);
      display.println("GAS STABILIZED");
      display.drawLine(0, 11, 127, 11, SH110X_WHITE);
      display.setTextSize(2);
      display.setCursor(0, 22);
      display.println("ESCAPE");
      display.println("COMPLETE");
      display.display();
      break;
  }
}

void showStabilized() { oledNext(1); }   // valves done -> go to the morse code transmitter

// back to a clean start, so the room can be played again without rebooting
void resetBoiler() {
  heatLevel = 0;
  currentStep = 0;
  revealed = false;
  awaitingUnlock = true;
  inZone = false;
  zoneStartTime = 0;
  isSolved = false;
  oledStage = 0;
  showWaiting();
}

// change the boiler-room heat level (the LED strip follows)
void setHeat(int level) {
  level = constrain(level, 0, 4);
  if (level == 0 && (isSolved || currentStep != 0 || revealed)) { resetBoiler(); return; }
  heatLevel = level;
  if (isSolved) {
    uint8_t r, g, b;
    heatColor(heatLevel, r, g, b);
    setStripColor(r, g, b);
  }
}


/* ============================================================================
   JOB 2: GAS BAR CONTROLLER (slider + potentiometer, the page locks the bars)
   >>> THIS PART IS FROM ANOTHER GROUP, NOT OURS <<<
   ============================================================================ */

const int NUM_BARS = 8;          // number of bars in the gas puzzle (slider has 8 zones)

int lastBarIndex = -1;       // which bar
int lastBarHeight = -1;      // height of bar (-1 = none sent yet)

// rate limit timer to keep Wi-Fi smooth (10Hz / 100ms)
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 100;

int potAnchor = -1;       // pot position at the moment the current bar was selected
bool potPickedUp = false; // has the pot been moved since selecting this bar?

// while the gas screen is showing, repeat the current state now and then,
// so the laptop catches up even if its page was still loading when we sent it
bool gasActive = false;
unsigned long lastHeartbeat = 0;
const unsigned long heartbeatInterval = 1500;

void sendBarState(int idx, int hgt) {
  oocsi.newMessage(CHANNEL_BARS);
  oocsi.addInt("bar_index", idx);
  if (hgt >= 0) oocsi.addInt("bar_height", hgt);   // no height = the laptop keeps the bar's own height
  oocsi.sendMessage();                             // (locking is done by the laptop page)
}

// the laptop switched to the gas screen -> start fresh and send the controller's position again
void startGasStage() {
  lastBarIndex = -1;
  lastBarHeight = -1;
  potAnchor = -1;
  potPickedUp = false;
  lastHeartbeat = millis();
}

// averaged analog reading: the pot / slider jitter a little, this smooths it out
int readSmooth(int pin) {
  long sum = 0;
  for (int i = 0; i < 8; i++) sum += analogRead(pin);
  return sum / 8;
}

// pot tolerance (bar-height percent)
const int POT_PICKUP   = 6;   // after choosing a bar, the pot must move this far before the bar follows it
const int POT_DEADBAND = 3;   // afterwards, ignore changes smaller than this (higher = less sensitive)

void handleSliderAndPot() {
  if (millis() - lastSendTime < sendInterval) return;
  lastSendTime = millis();

  // slider -> which bar (index 0 to 7)
  int rawSlider = readSmooth(SLIDER_PIN);
  int currentBarIndex = constrain(map(rawSlider, 0, 3000, 0, NUM_BARS - 1), 0, NUM_BARS - 1);

  // pot -> bar height (0 to 100)
  int rawPot = readSmooth(POT_PIN);
  int potHeight = constrain(map(rawPot, 0, 3000, 0, 100), 0, 100);

  // selected a different bar: it KEEPS its own height. The pot only takes over
  // once it is actually turned (so the bar does not jump to the old pot position).
  if (currentBarIndex != lastBarIndex) {
    lastBarIndex = currentBarIndex;
    potAnchor = potHeight;
    potPickedUp = false;
    lastBarHeight = -1;          // -1 = "no height sent", the laptop keeps the bar where it is
    sendBarState(currentBarIndex, -1);
    lastHeartbeat = millis();
    Serial.print("Idx: "); Serial.print(currentBarIndex);
    Serial.println(" | bar selected (height unchanged)");
    return;
  }

  if (!potPickedUp) {
    if (abs(potHeight - potAnchor) < POT_PICKUP) return;
    potPickedUp = true;
  } else if (lastBarHeight >= 0 && abs(potHeight - lastBarHeight) < POT_DEADBAND) {
    return;
  }

  lastBarHeight = potHeight;
  sendBarState(currentBarIndex, lastBarHeight);
  lastHeartbeat = millis();
  Serial.print("Idx: "); Serial.print(currentBarIndex);
  Serial.print(" | Height: "); Serial.println(lastBarHeight);
}

// repeat the current bar state now and then (laptop may have missed one)
void gasHeartbeat() {
  if (!gasActive || lastBarIndex < 0) return;
  if (millis() - lastHeartbeat < heartbeatInterval) return;
  lastHeartbeat = millis();
  sendBarState(lastBarIndex, lastBarHeight);
}


/* ============================================================================
   OOCSI: incoming messages (called by oocsi.check())
   ============================================================================ */

void processOOCSI() {
  // ---- game flow from the laptop (game channel) ----
  if (oocsi.has("stage")) {
    String stage = String(oocsi.getString("stage", ""));
    gasActive = (stage == "gas");
    if (gasActive) startGasStage();
    // safety net in case a heat message is missed
    if (stage == "fire" && heatLevel < 1) setHeat(1);
    if (stage == "gas"  && heatLevel < 3) setHeat(3);
    if (stage == "done") setHeat(4);
    if (stage == "boiler") servoAngle(SERVO_CLOSED);       // new run -> box closed again (90)
    // OLED: where to go next
    if (stage == "boiler" && oledStage != 0) { oledStage = 0; showWaiting(); }
    if (stage == "fire") oledNext(1);   // valves done   -> morse code transmitter
    if (stage == "gas")  oledNext(4);   // fire put out  -> gas distribution
    if (stage == "done") oledNext(5);   // gas done      -> escape complete
  }

  // the fire module reports the fire -> go put it out
  if (oocsi.has("fire") && oocsi.getBool("fire", false)) oledNext(3);
  if (oocsi.has("fire_out")) oledNext(4);

  // morse code entered -> box opens (servo to 45)
  if (oocsi.has("phone_open") || oocsi.has("box_open")) {
    Serial.println("box open -> servo to 45 degrees");
    servoAngle(SERVO_OPEN);
    oledNext(2);                       // morse code done -> walkie talkie box
  }

  if (oocsi.has("heat")) setHeat(oocsi.getInt("heat", 0));

  // ---- boiler puzzle ----
  d1 = oocsi.getFloat("mr-kip_d1", d1);
  if (oocsi.has("mr-kip_d1")) {
    Serial.print("mr-kip_d1 = ");
    Serial.println(d1);
  }

  if (oocsi.has("unlock_next")) {
    int u = oocsi.getInt("unlock_next", 0);

    // the laptop asking for step 1 means a new run is starting: begin clean
    if (u == 1 && (currentStep != 0 || isSolved || revealed)) resetBoiler();
    if (u == 1) heatLevel = 0;

    if (u == currentStep + 1 && awaitingUnlock && !revealed) {
      revealed = true;
      awaitingUnlock = false;
      showPressure(SEQUENCE_PRESSURE[currentStep], currentStep + 1);

      oocsi.newMessage(CHANNEL);
      oocsi.addInt("revealed_step", currentStep + 1);
      oocsi.addFloat("revealed_pressure", SEQUENCE_PRESSURE[currentStep]);
      oocsi.sendMessage();
    }
  }

  if (revealed && !awaitingUnlock && !isSolved) {
    bool nowInZone = isAtDirection(SEQUENCE_DIR[currentStep], d1);

    if (nowInZone && !inZone) zoneStartTime = millis();
    if (!nowInZone) zoneStartTime = 0;
    inZone = nowInZone;

    if (inZone && millis() - zoneStartTime >= HOLD_DURATION) {
      oocsi.newMessage(CHANNEL);
      oocsi.addInt("step_done", currentStep + 1);
      oocsi.sendMessage();

      currentStep++;
      revealed = false;
      awaitingUnlock = true;
      inZone = false;
      zoneStartTime = 0;

      if (currentStep >= SEQUENCE_LEN) {
        isSolved = true;
        if (heatLevel < 1) heatLevel = 1;     // valves done: a bit less hot (the laptop confirms this)
        setHeat(heatLevel);
        showStabilized();
      } else {
        showStepComplete();
      }
    }
  }
}


/* ============================================================================
   SETUP + LOOP
   ============================================================================ */

// runs once at power on
void setup() {
  Serial.begin(115200);
  delay(1000);

  // servo on pin 4: starts vertical (90 degrees, box closed)
  ledcAttach(SERVO_PIN, 50, 16);   // ESP32 core 3.x
  servoWrite(SERVO_CLOSED);

  // slider / pot
  pinMode(SLIDER_PIN, INPUT);
  pinMode(POT_PIN, INPUT);
  analogSetAttenuation(ADC_11db);  // full 3.3V range

  // LED strip + OLED
  strip.begin();
  strip.setBrightness(100);

  Wire.begin(OLED_SDA, OLED_SCL);
  // I2C scan: tells you in the Serial Monitor whether the OLED is seen at all
  uint8_t oledAddr = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.print("I2C device found at 0x"); Serial.println(a, HEX);
      if (a == 0x3C || a == 0x3D) oledAddr = a;
    }
  }
  if (oledAddr == 0) {
    Serial.println("!! NO OLED FOUND on I2C. Check 3V3/GND to the OLED and SDA=6, SCL=7 (try swapping them).");
    oledAddr = 0x3C;
  }
  display.begin(oledAddr, true);
  showWaiting();

  // OOCSI: one connection, two channels to listen to
  Serial.println("connecting to OOCSI...");
  oocsi.connect(OOCSIName, hostserver, ssid, password, processOOCSI);
  oocsi.subscribe(CHANNEL);
  oocsi.subscribe(CHANNEL_GAME);
}

unsigned long lastLedUpdate = 0;

// runs all the time
void loop() {
  oocsi.check();   // incoming messages (also keeps the connection alive)

  servoUpdate();           // move the servo step by step
  handleSliderAndPot();    // read slider + pot and send them
  gasHeartbeat();

  // LED strip: refresh ~25x per second (not every loop, so WiFi stays smooth)
  if (!isSolved && millis() - lastLedUpdate >= 40) {
    lastLedUpdate = millis();
    updateProgressBar();
  }

  delay(2);
}