/*
   ==========================================================
   PROJECT EUPHORIA
   FireNexa - Smart Office Security System
   ==========================================================

   CONTROLLER:
   Arduino UNO R3

   FUNCTIONS:
   - Receives sensor data from NodeMCU ESP8266
   - Controls LCD
   - Controls Red/Green LEDs
   - Controls Buzzer
   - Controls Servo Gate
   - Room Light ALWAYS ON

   NODEMCU -> UNO:
   NodeMCU TX -> UNO D7
   NodeMCU GND -> UNO GND

   LCD:
   RS -> D12
   E  -> D11
   D4 -> D5
   D5 -> D4
   D6 -> D3
   D7 -> D2

   SERVO:
   Signal -> D6

   BUZZER:
   + -> D8
   - -> GND

   GREEN LED:
   D9 -> 220 ohm -> LED -> GND

   RED LED:
   D10 -> 220 ohm -> LED -> GND

   ROOM LIGHT:
   A0 -> 220 ohm -> LED -> GND
   ==========================================================
*/

#include <LiquidCrystal.h>
#include <Servo.h>
#include <SoftwareSerial.h>

// ==========================================================
// LCD
// ==========================================================

LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

// ==========================================================
// NODEMCU COMMUNICATION
// D7 = RX
// D13 = TX (not connected)
// ==========================================================

SoftwareSerial nodeMCU(7, 13);

// ==========================================================
// OUTPUT PINS
// ==========================================================

#define SERVO_PIN       6
#define BUZZER_PIN      8
#define GREEN_LED_PIN   9
#define RED_LED_PIN     10
#define ROOM_LIGHT_PIN  A0

// ==========================================================
// SERVO
// ==========================================================

Servo gateServo;

// ==========================================================
// VARIABLES
// ==========================================================

float temperature = 0.0;
float humidity = 0.0;

int smokeValue = 0;
int flameValue = 0;

char systemStatus[20] = "WARMUP";

unsigned long lastDataReceived = 0;

// Communication timeout
const unsigned long COMM_TIMEOUT = 10000;

// ==========================================================
// BUZZER CONTROL
// ==========================================================

// Alarm timing
unsigned long lastBuzzerChange = 0;

bool buzzerState = false;

// Change these values if you want different sound
const unsigned long BUZZER_ON_TIME  = 250;
const unsigned long BUZZER_OFF_TIME = 250;

// ==========================================================
// LCD
// ==========================================================

void showLCD() {

  lcd.setCursor(0, 0);
  lcd.print("Firenexa        ");

  lcd.setCursor(0, 1);
  lcd.print("Smart Security  ");
}

// ==========================================================
// NORMAL / SAFE SYSTEM
// ==========================================================

void normalMode() {

  // Green ON
  digitalWrite(GREEN_LED_PIN, HIGH);

  // Red OFF
  digitalWrite(RED_LED_PIN, LOW);

  // Buzzer OFF
  digitalWrite(BUZZER_PIN, LOW);

  buzzerState = false;

  // Gate closed
  gateServo.write(0);
}

// ==========================================================
// EMERGENCY SYSTEM
// ==========================================================

void emergencyMode() {

  // Red ON
  digitalWrite(RED_LED_PIN, HIGH);

  // Green OFF
  digitalWrite(GREEN_LED_PIN, LOW);

  // Gate open
  gateServo.write(90);

  // Buzzer handled separately
}

// ==========================================================
// BUZZER ALARM
// ==========================================================

void updateBuzzer() {

  unsigned long currentMillis = millis();

  if (buzzerState == false) {

    // Currently OFF
    if (currentMillis - lastBuzzerChange >= BUZZER_OFF_TIME) {

      buzzerState = true;

      lastBuzzerChange = currentMillis;

      digitalWrite(BUZZER_PIN, HIGH);
    }
  }

  else {

    // Currently ON
    if (currentMillis - lastBuzzerChange >= BUZZER_ON_TIME) {

      buzzerState = false;

      lastBuzzerChange = currentMillis;

      digitalWrite(BUZZER_PIN, LOW);
    }
  }
}

// ==========================================================
// STOP BUZZER
// ==========================================================

void stopBuzzer() {

  buzzerState = false;

  digitalWrite(BUZZER_PIN, LOW);

  lastBuzzerChange = millis();
}

// ==========================================================
// CHECK WHETHER STATUS IS AN ACTUAL ALERT
// ==========================================================

bool isAlertStatus() {

  if (strcmp(systemStatus, "FIRE") == 0) {
    return true;
  }

  if (strcmp(systemStatus, "SMOKE") == 0) {
    return true;
  }

  if (strcmp(systemStatus, "HIGH_TEMP") == 0) {
    return true;
  }

  if (strcmp(systemStatus, "MULTIPLE") == 0) {
    return true;
  }

  return false;
}

// ==========================================================
// CONTROL SYSTEM
// ==========================================================

void controlSystem() {

  // --------------------------------------------------------
  // ROOM LIGHT ALWAYS ON
  // --------------------------------------------------------

  digitalWrite(ROOM_LIGHT_PIN, HIGH);

  // --------------------------------------------------------
  // CHECK ACTUAL ALERT
  // --------------------------------------------------------

  if (isAlertStatus()) {

    // Emergency
    emergencyMode();

  }

  else {

    // SAFE / WARMUP / SENSOR_ERROR / NO_DATA
    normalMode();

    stopBuzzer();
  }
}

// ==========================================================
// READ DATA FROM NODEMCU
// ==========================================================

void readNodeMCU() {

  static char buffer[100];

  static byte index = 0;

  while (nodeMCU.available()) {

    char c = nodeMCU.read();

    // ------------------------------------------------------
    // End of packet
    // ------------------------------------------------------

    if (c == '\n') {

      buffer[index] = '\0';

      index = 0;

      // ----------------------------------------------------
      // Expected:
      //
      // DATA,25.0,60.0,100,0,SAFE
      //
      // ----------------------------------------------------

      if (strncmp(buffer, "DATA,", 5) == 0) {

        float t;
        float h;

        int smoke;
        int flame;

        char status[20];

        int result = sscanf(
          buffer,
          "DATA,%f,%f,%d,%d,%19s",
          &t,
          &h,
          &smoke,
          &flame,
          status
        );

        // --------------------------------------------------
        // Valid packet
        // --------------------------------------------------

        if (result == 5) {

          temperature = t;

          humidity = h;

          smokeValue = smoke;

          flameValue = flame;

          strncpy(
            systemStatus,
            status,
            sizeof(systemStatus) - 1
          );

          systemStatus[sizeof(systemStatus) - 1] = '\0';

          // -----------------------------------------------
          // Communication is working
          // -----------------------------------------------

          lastDataReceived = millis();

          // -----------------------------------------------
          // Debug output to Arduino Serial Monitor
          // -----------------------------------------------

          Serial.print("Temperature: ");
          Serial.print(temperature);

          Serial.print("  Humidity: ");
          Serial.print(humidity);

          Serial.print("  Smoke: ");
          Serial.print(smokeValue);

          Serial.print("  Flame: ");
          Serial.print(flameValue);

          Serial.print("  Status: ");
          Serial.println(systemStatus);

          // -----------------------------------------------
          // Control system immediately
          // -----------------------------------------------

          controlSystem();
        }
      }
    }

    // ------------------------------------------------------
    // Prevent buffer overflow
    // ------------------------------------------------------

    else {

      if (index < sizeof(buffer) - 1) {

        buffer[index++] = c;
      }

      else {

        index = 0;
      }
    }
  }
}

// ==========================================================
// COMMUNICATION CHECK
// ==========================================================

void checkCommunication() {

  unsigned long currentMillis = millis();

  /*
     IMPORTANT:

     NO_DATA is NOT treated as an alert.

     Therefore:
     - Red LED remains OFF
     - Green LED remains ON
     - Buzzer OFF
     - Gate closed
  */

  if (currentMillis - lastDataReceived > COMM_TIMEOUT) {

    strcpy(systemStatus, "NO_DATA");

    normalMode();

    stopBuzzer();
  }
}

// ==========================================================
// SETUP
// ==========================================================

void setup() {

  // --------------------------------------------------------
  // Arduino USB Serial
  // --------------------------------------------------------

  Serial.begin(9600);

  // --------------------------------------------------------
  // NodeMCU communication
  // --------------------------------------------------------

  nodeMCU.begin(9600);

  // --------------------------------------------------------
  // Pin modes
  // --------------------------------------------------------

  pinMode(BUZZER_PIN, OUTPUT);

  pinMode(GREEN_LED_PIN, OUTPUT);

  pinMode(RED_LED_PIN, OUTPUT);

  pinMode(ROOM_LIGHT_PIN, OUTPUT);

  // --------------------------------------------------------
  // Initial states
  // --------------------------------------------------------

  digitalWrite(BUZZER_PIN, LOW);

  digitalWrite(RED_LED_PIN, LOW);

  digitalWrite(GREEN_LED_PIN, HIGH);

  // Room light ALWAYS ON
  digitalWrite(ROOM_LIGHT_PIN, HIGH);

  // --------------------------------------------------------
  // Servo
  // --------------------------------------------------------

  gateServo.attach(SERVO_PIN);

  gateServo.write(0);

  // --------------------------------------------------------
  // LCD
  // --------------------------------------------------------

  lcd.begin(16, 2);

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Firenexa");

  lcd.setCursor(0, 1);
  lcd.print("Smart Security");

  delay(2000);

  lcd.clear();

  showLCD();

  // --------------------------------------------------------
  // Start communication timer
  // --------------------------------------------------------

  lastDataReceived = millis();

  Serial.println("================================");
  Serial.println("FireNexa Arduino UNO Started");
  Serial.println("================================");
}

// ==========================================================
// LOOP
// ==========================================================

void loop() {

  // --------------------------------------------------------
  // Keep LCD fixed
  // --------------------------------------------------------

  showLCD();

  // --------------------------------------------------------
  // Read NodeMCU data
  // --------------------------------------------------------

  readNodeMCU();

  // --------------------------------------------------------
  // Check communication
  // --------------------------------------------------------

  checkCommunication();

  // --------------------------------------------------------
  // Check whether current status is actual alert
  // --------------------------------------------------------

  if (isAlertStatus()) {

    // Keep emergency outputs active
    digitalWrite(RED_LED_PIN, HIGH);

    digitalWrite(GREEN_LED_PIN, LOW);

    gateServo.write(90);

    // Proper repeating buzzer
    updateBuzzer();

  }

  else {

    // Normal state
    digitalWrite(RED_LED_PIN, LOW);

    digitalWrite(GREEN_LED_PIN, HIGH);

    stopBuzzer();

    gateServo.write(0);
  }

  // --------------------------------------------------------
  // Room light ALWAYS ON
  // --------------------------------------------------------

  digitalWrite(ROOM_LIGHT_PIN, HIGH);
}