// ======================================================
// PROJECT EUPHORIA
// FireNexa - Smart Office Security System
//
// CONTROLLER:
// NodeMCU ESP8266
//
// FUNCTIONS:
// DHT11 + MQ-2 + Flame Sensor
// WiFi + Blynk + ThingSpeak
// Sends sensor data to Arduino UNO
//
// NODEMCU TX -> ARDUINO UNO D7
// NODEMCU GND -> ARDUINO GND
// ======================================================

#define BLYNK_TEMPLATE_ID "YOUR_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "Smart Office Security"
#define BLYNK_AUTH_TOKEN "YOUR_BLYNK_AUTH_TOKEN"

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <ThingSpeak.h>
#include <DHT.h>


// ======================================================
// WIFI
// ======================================================

char ssid[] = "Wi-Fi_Name";
char pass[] = "Wi-Fi_Password";


// ======================================================
// THINGSPEAK
// ======================================================

unsigned long myChannelNumber = YOUR_CHANNEL_NUMBER;

const char *myWriteAPIKey =  "YOUR_THINGSPEAK_WRITE_API_KEY";

WiFiClient client;


// ======================================================
// SENSOR PINS
// ======================================================

#define DHTPIN D4
#define DHTTYPE DHT11

#define FLAME_PIN D5
#define MQ2_PIN A0


DHT dht(DHTPIN, DHTTYPE);

BlynkTimer timer;


// ======================================================
// SETTINGS
// ======================================================

const unsigned long WARMUP_TIME = 60000;


// ======================================================
// FLAME
// ======================================================

#define FLAME_DETECTED LOW


// ======================================================
// SMOKE
// ======================================================

const int SMOKE_TRIGGER = 550;
const int SMOKE_CLEAR = 450;

bool smokeAlarm = false;


// ======================================================
// TEMPERATURE
// ======================================================

const float TEMP_TRIGGER = 40.0;
const float TEMP_CLEAR = 37.0;

bool tempAlarm = false;


// ======================================================
// TIMING
// ======================================================

unsigned long startTime = 0;

unsigned long previousThingSpeakTime = 0;

const unsigned long THINGSPEAK_INTERVAL = 20000;


// ======================================================
// SENSOR VARIABLES
// ======================================================

float temperature = 0.0;
float humidity = 0.0;

int smokeValue = 0;
int flameValue = HIGH;


// ======================================================
// SYSTEM VARIABLES
// ======================================================

const char *systemStatus = "WARMUP";

bool emergency = false;

int emergencyType = 0;


// Emergency types:
//
// 0 = SAFE / WARMUP
// 1 = SMOKE
// 2 = HIGH_TEMP
// 3 = FIRE
// 4 = MULTIPLE


// ======================================================
// NOTIFICATION FLAGS
// ======================================================

bool fireNotificationSent = false;
bool smokeNotificationSent = false;
bool temperatureNotificationSent = false;
bool multipleNotificationSent = false;


// ======================================================
// SETUP
// ======================================================

void setup()
{
  // ----------------------------------------------------
  // SERIAL
  // ----------------------------------------------------
  //
  // IMPORTANT:
  // This Serial is used for:
  //
  // NodeMCU TX -> Arduino UNO D7
  //
  // DO NOT use Serial.println() for debugging.
  // Only DATA messages should be transmitted.
  //

  Serial.begin(9600);

  delay(100);


  // ----------------------------------------------------
  // FLAME SENSOR
  // ----------------------------------------------------

  pinMode(FLAME_PIN, INPUT);


  // ----------------------------------------------------
  // DHT11
  // ----------------------------------------------------

  dht.begin();


  // ----------------------------------------------------
  // WIFI
  // ----------------------------------------------------

  WiFi.mode(WIFI_STA);

  WiFi.begin(ssid, pass);


  // ----------------------------------------------------
  // WAIT FOR WIFI
  // ----------------------------------------------------

  unsigned long wifiStartTime = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - wifiStartTime < 20000
  )
  {
    delay(500);
  }


  // ----------------------------------------------------
  // BLYNK
  // ----------------------------------------------------

  Blynk.config(BLYNK_AUTH_TOKEN);

  if (WiFi.status() == WL_CONNECTED)
  {
    Blynk.connect(10000);
  }


  // ----------------------------------------------------
  // THINGSPEAK
  // ----------------------------------------------------

  ThingSpeak.begin(client);


  // ----------------------------------------------------
  // START WARMUP
  // ----------------------------------------------------

  startTime = millis();


  // ----------------------------------------------------
  // SENSOR TIMER
  // ----------------------------------------------------

  timer.setInterval(
    2000L,
    readSensorsAndSend
  );
}


// ======================================================
// MAIN LOOP
// ======================================================

void loop()
{
  // ----------------------------------------------------
  // WIFI RECONNECT
  // ----------------------------------------------------

  if (WiFi.status() != WL_CONNECTED)
  {
    WiFi.disconnect();
    WiFi.begin(ssid, pass);
  }


  // ----------------------------------------------------
  // BLYNK RECONNECT
  // ----------------------------------------------------

  if (
    WiFi.status() == WL_CONNECTED &&
    !Blynk.connected()
  )
  {
    Blynk.connect(1000);
  }


  // ----------------------------------------------------
  // BLYNK
  // ----------------------------------------------------

  Blynk.run();


  // ----------------------------------------------------
  // TIMER
  // ----------------------------------------------------

  timer.run();
}


// ======================================================
// SENSOR FUNCTION
// ======================================================

void readSensorsAndSend()
{
  unsigned long currentTime = millis();


  // ====================================================
  // READ DHT11
  // ====================================================

  float newTemperature = dht.readTemperature();
  float newHumidity = dht.readHumidity();


  // ====================================================
  // WARMUP
  // ====================================================

  if (
    currentTime - startTime <
    WARMUP_TIME
  )
  {
    temperature = newTemperature;
    humidity = newHumidity;

    if (isnan(temperature))
      temperature = 0;

    if (isnan(humidity))
      humidity = 0;


    systemStatus = "WARMUP";

    emergency = false;

    emergencyType = 0;


    // --------------------------------------------------
    // SEND TO ARDUINO
    // --------------------------------------------------

    Serial.print("DATA,");
    Serial.print(temperature, 1);
    Serial.print(",");
    Serial.print(humidity, 1);
    Serial.println(",0,0,WARMUP");


    // --------------------------------------------------
    // BLYNK
    // --------------------------------------------------

    sendBlynkData();


    return;
  }


  // ====================================================
  // STORE DHT DATA
  // ====================================================

  bool sensorError = false;


  if (
    isnan(newTemperature) ||
    isnan(newHumidity)
  )
  {
    sensorError = true;

    temperature = 0;
    humidity = 0;
  }
  else
  {
    temperature = newTemperature;
    humidity = newHumidity;
  }


  // ====================================================
  // MQ-2
  // ====================================================

  smokeValue = analogRead(MQ2_PIN);


  // ====================================================
  // FLAME
  // ====================================================

  flameValue = digitalRead(FLAME_PIN);


  // ====================================================
  // SMOKE HYSTERESIS
  // ====================================================

  if (
    !smokeAlarm &&
    smokeValue >= SMOKE_TRIGGER
  )
  {
    smokeAlarm = true;
  }


  if (
    smokeAlarm &&
    smokeValue <= SMOKE_CLEAR
  )
  {
    smokeAlarm = false;
  }


  // ====================================================
  // TEMPERATURE HYSTERESIS
  // ====================================================

  if (
    !tempAlarm &&
    temperature >= TEMP_TRIGGER
  )
  {
    tempAlarm = true;
  }


  if (
    tempAlarm &&
    temperature <= TEMP_CLEAR
  )
  {
    tempAlarm = false;
  }


  // ====================================================
  // DETERMINE SYSTEM STATUS
  // ====================================================

  emergency = false;
  emergencyType = 0;


  // ----------------------------------------------------
  // SENSOR ERROR
  // ----------------------------------------------------

  if (sensorError)
  {
    systemStatus = "SENSOR_ERROR";

    emergency = true;

    emergencyType = 5;
  }


  else
  {
    int emergencyCount = 0;


    // Flame
    if (
      flameValue ==
      FLAME_DETECTED
    )
    {
      emergencyCount++;
    }


    // Smoke
    if (smokeAlarm)
    {
      emergencyCount++;
    }


    // Temperature
    if (tempAlarm)
    {
      emergencyCount++;
    }


    // --------------------------------------------------
    // MULTIPLE
    // --------------------------------------------------

    if (emergencyCount >= 2)
    {
      systemStatus = "MULTIPLE";

      emergency = true;

      emergencyType = 4;
    }


    // --------------------------------------------------
    // FIRE
    // --------------------------------------------------

    else if (
      flameValue ==
      FLAME_DETECTED
    )
    {
      systemStatus = "FIRE";

      emergency = true;

      emergencyType = 3;
    }


    // --------------------------------------------------
    // SMOKE
    // --------------------------------------------------

    else if (smokeAlarm)
    {
      systemStatus = "SMOKE";

      emergency = true;

      emergencyType = 1;
    }


    // --------------------------------------------------
    // HIGH TEMPERATURE
    // --------------------------------------------------

    else if (tempAlarm)
    {
      systemStatus = "HIGH_TEMP";

      emergency = true;

      emergencyType = 2;
    }


    // --------------------------------------------------
    // SAFE
    // --------------------------------------------------

    else
    {
      systemStatus = "SAFE";

      emergency = false;

      emergencyType = 0;
    }
  }


  // ====================================================
  // SEND DATA TO ARDUINO
  // ====================================================

  Serial.print("DATA,");

  Serial.print(temperature, 1);

  Serial.print(",");

  Serial.print(humidity, 1);

  Serial.print(",");

  Serial.print(smokeValue);

  Serial.print(",");

  if (
    flameValue ==
    FLAME_DETECTED
  )
  {
    Serial.print(1);
  }
  else
  {
    Serial.print(0);
  }

  Serial.print(",");

  Serial.println(systemStatus);


  // ====================================================
  // BLYNK
  // ====================================================

  sendBlynkData();


  // ====================================================
  // NOTIFICATIONS
  // ====================================================

  if (Blynk.connected())
  {
    sendEmergencyNotifications();
  }


  // ====================================================
  // THINGSPEAK
  // ====================================================

  if (
    currentTime -
    previousThingSpeakTime >=
    THINGSPEAK_INTERVAL
  )
  {
    previousThingSpeakTime =
      currentTime;


    ThingSpeak.setField(
      1,
      temperature
    );

    ThingSpeak.setField(
      2,
      humidity
    );

    ThingSpeak.setField(
      3,
      smokeValue
    );

    ThingSpeak.setField(
      4,
      flameValue == FLAME_DETECTED ? 1 : 0
    );

    ThingSpeak.setField(
      5,
      emergency ? 1 : 0
    );

    ThingSpeak.setField(
      6,
      emergencyType
    );


    ThingSpeak.writeFields(
      myChannelNumber,
      myWriteAPIKey
    );
  }
}


// ======================================================
// BLYNK DATA
// ======================================================

void sendBlynkData()
{
  if (!Blynk.connected())
  {
    return;
  }


  Blynk.virtualWrite(
    V0,
    temperature
  );


  Blynk.virtualWrite(
    V1,
    humidity
  );


  Blynk.virtualWrite(
    V2,
    smokeValue
  );


  Blynk.virtualWrite(
    V3,
    flameValue == FLAME_DETECTED ? 1 : 0
  );


  Blynk.virtualWrite(
    V4,
    systemStatus
  );


  Blynk.virtualWrite(
    V5,
    emergency ? 1 : 0
  );


  Blynk.virtualWrite(
    V6,
    emergencyType
  );
}


// ======================================================
// BLYNK NOTIFICATIONS
// ======================================================

void sendEmergencyNotifications()
{
  // ----------------------------------------------------
  // MULTIPLE
  // ----------------------------------------------------

  if (
    emergencyType == 4 &&
    !multipleNotificationSent
  )
  {
    Blynk.logEvent(
      "multiple_alert",
      "MULTIPLE EMERGENCY! Check the office."
    );

    multipleNotificationSent = true;
  }

  if (emergencyType != 4)
  {
    multipleNotificationSent = false;
  }


  // ----------------------------------------------------
  // FIRE
  // ----------------------------------------------------

  if (
    emergencyType == 3 &&
    !fireNotificationSent
  )
  {
    Blynk.logEvent(
      "fire_alert",
      "FIRE ALERT! Flame detected."
    );

    fireNotificationSent = true;
  }

  if (emergencyType != 3)
  {
    fireNotificationSent = false;
  }


  // ----------------------------------------------------
  // SMOKE
  // ----------------------------------------------------

  if (
    emergencyType == 1 &&
    !smokeNotificationSent
  )
  {
    Blynk.logEvent(
      "smoke_alert",
      "SMOKE ALERT! High smoke level detected."
    );

    smokeNotificationSent = true;
  }

  if (emergencyType != 1)
  {
    smokeNotificationSent = false;
  }


  // ----------------------------------------------------
  // TEMPERATURE
  // ----------------------------------------------------

  if (
    emergencyType == 2 &&
    !temperatureNotificationSent
  )
  {
    Blynk.logEvent(
      "temperature_alert",
      "HIGH TEMPERATURE ALERT!"
    );

    temperatureNotificationSent = true;
  }

  if (emergencyType != 2)
  {
    temperatureNotificationSent = false;
  }
}