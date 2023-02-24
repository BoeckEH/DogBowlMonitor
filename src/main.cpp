#define ESP32_RTOS // Uncomment this line if you want to use the code with freertos only on the ESP32
                   // Has to be done before including "OTA.h"

#include <OTA.h>
#include <credentials.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiMulti.h>
#include "time.h"
#include <HTTPClient.h>

#define uS_TO_S_FACTOR 1000000ULL /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP 600         /* Time ESP32 will sleep (in seconds) */
#define HOW_MANY_INA_ROW 2        // this is sleep time times how many in a row have to be empty (TIME_TO_SLEEP * HOW_MANY_INA_ROW)
#define REPEAT_NUMBER 2
#define WATER_PIN 4
#define PRE_SLEEP_TIME 60 // we will loop sleeping for a second this many time before implementing the deep sleep cycles
#define OKWATER true
#define LOWWATER false

#ifndef BUILT_IN_LED
#define BUILT_IN_LED 5
#endif

RTC_DATA_ATTR bool haveAlerted = false;
RTC_DATA_ATTR long bootCount;        // how many times have we woken up to check
RTC_DATA_ATTR int noWaterCount = 0;  // has to be empty for x number of checks
RTC_DATA_ATTR int reminderCount = 0; // wait this many wake cycles from last broadcast of no water (REPEAT_NUMBER * TIME_TO_SLEEP)

const char theHost[] = "www.littlenodes.com";
const char theRest[] = "https://www.littlenodes.com/api/alexa/triggeralexa.php?email=[YOUR EMAIL]&apikey=[YOUR API KEY]";

WiFiMulti wifiMulti;

// Number of milliseconds to wait without receiving any data before we give up
const int kNetworkTimeout = 30 * 1000;
// Number of milliseconds to wait if no data is available before trying again
const int kNetworkDelay = 1000;

void wifiSetup()
{

  WiFi.disconnect();
  WiFi.hostname("DogBowl"); // DHCP Hostname (useful for finding device for static lease)
                            //    WiFi.config(staticIP, subnet, gateway, dns);

  // Set WIFI module to STA mode
  WiFi.mode(WIFI_STA);
  
  // These are from credentials.h.  Ypu need to creat the file and put these #defines in there.
  wifiMulti.addAP(MySSID1, MyPassword);
  wifiMulti.addAP(MySSID2, MyPassword);
  wifiMulti.addAP(MySSID3, MyPassword);

  Serial.println("Connecting Wifi...");
  if (wifiMulti.run() != WL_CONNECTED)
  {
    // Connected!
    delay(100);
  }
  Serial.printf("[WIFI] STATION Mode, SSID: %s, IP address: %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;

boolean currentWaterLevel = OKWATER;

void printLocalTime()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}

float checkWaterLevel()
{

  float sensorReading = 0; // Read the analog value for the touch sensor
  byte sampleSize = 8;     // Number of samples we want to take

  for (byte i = 0; i < sampleSize; i++) // Average samples together to minimize false readings
  {
    sensorReading += touchRead(WATER_PIN); // We sample the touch pin here
    delay(10);
  }
  sensorReading /= sampleSize;

  return sensorReading;
}

void weNeedToSaySomething()
{
  {
    // so the water is low, it is multiple times low, and we need to send another reminder.
    // turn on wifi
    wifiSetup();
    // get the time
    // printLocalTime();
    // connect to the little nodes
    WiFiClientSecure c;
    c.setInsecure();
    HttpClient http(c, theHost, 443);
    Serial.println("Doing the get");
    http.get(theRest);
    Serial.print("After the get, response code:");
    Serial.println(http.responseBody());
  }
}

int loopcounter = 0;

void setup()
{
  Serial.begin(115200);
  pinMode(BUILT_IN_LED, OUTPUT);
  digitalWrite(BUILT_IN_LED, HIGH);
  Serial.println("Booting");
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  ++bootCount;
  Serial.printf("Booted %lu times\n", bootCount);
  // check the water level
  float level = checkWaterLevel();
  // if the water level is low (greater than 10) we have no water

  if (level > 10)
  {
    // so increment the no water counter
    ++noWaterCount;
    Serial.printf("Start: There is no water. Water count: %i, reminder count: %i, have alerted: %d \n", noWaterCount, reminderCount, haveAlerted);

    // if the water is low,, and the water count is > how many in row, and we haven't alerted, we should say something
    // and clear the reminder counter
    if (noWaterCount > HOW_MANY_INA_ROW && !haveAlerted)
    {
      weNeedToSaySomething();
      haveAlerted = true;
      reminderCount = 0;
      Serial.printf("Alert: There is no water. Water count: %i, reminder count: %i, have alerted: %d \n", noWaterCount, reminderCount, haveAlerted);
    }

    // if the water is low, and the water count is > how many in row and we HAVE alerted, increment the reminder count
    if (noWaterCount > HOW_MANY_INA_ROW && haveAlerted)
    {
      ++reminderCount;
      Serial.printf("Remnd: There is no water. Water count: %i, reminder count: %i, have alerted: %d \n", noWaterCount, reminderCount, haveAlerted);
    }

    if (noWaterCount > HOW_MANY_INA_ROW && reminderCount > REPEAT_NUMBER)
    {
      weNeedToSaySomething();
      reminderCount = 0;
      Serial.printf("Again: There is no water. Water count: %i, reminder count: %i, have alerted: %d \n", noWaterCount, reminderCount, haveAlerted);
    }
  }
  else
  {
    reminderCount = 0;
    haveAlerted = false;
    noWaterCount = 0;
    Serial.printf("Alert: There is    water. Water count: %i, reminder count: %i, have alerted: %d \n", noWaterCount, reminderCount, haveAlerted);

  } // if we booted from the button, let's hang around for a couple minutes to see if we can get OTA'd
  if (bootCount > 1)
  {
    Serial.println("Going to sleep");
    esp_deep_sleep_start();
  }

  Serial.println("Looping to wait for OTA update, wifi enabled");
  setupOTA("DogBowl", MySSID1, MyPassword);
  digitalWrite(BUILT_IN_LED, LOW);
}

void loop()
{
  // Your code here
  ++loopcounter;
  float checked = checkWaterLevel();
  Serial.printf("%3i:  Water level: %9.1f\n", loopcounter, checked);
  delay(1000);
  if (loopcounter > PRE_SLEEP_TIME)
  {
    Serial.println("Time's up m'man.  Sleepy time is upon us.");
    noWaterCount = 0;
    reminderCount = 0;
    haveAlerted = false;
    digitalWrite(BUILT_IN_LED, HIGH);
    esp_deep_sleep_start();
  }
}
