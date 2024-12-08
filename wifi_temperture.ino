#include <Arduino.h>
#include <Wire.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <ArduinoHttpClient.h>
#include <U8g2lib.h>
#include <Temperature_LM75_Derived.h>

// -----------------------------------
// include configuration
// -----------------------------------
#include "config.h"

// -----------------------------------
// Display Setup
// -----------------------------------
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(
  U8G2_R0, /* clock=*/14, /* data=*/12, /* reset=*/U8X8_PIN_NONE);

// -----------------------------------
// Sensor Setup
// -----------------------------------
Generic_LM75 temperatureSensor;



// -----------------------------------
// API Endpoints & Connection
// -----------------------------------

// Time API (HTTPS)
const char* timeHost = "timeapi.io";
const int timePort = 443;
String timeUrl = "/api/time/current/zone?timeZone=" + String(TIMEZONE);

// Open-Meteo API (HTTP)
const char* weatherHost = "api.open-meteo.com";
const int weatherPort = 80;
String weatherUrl = "/v1/forecast?latitude=" + String(LATITUDE, 6) + "&longitude=" + String(LONGITUDE, 6) + "&hourly=temperature_2m&timezone=" + String(TIMEZONE) + "&forecast_days=1";
WiFiClientSecure timeClientSecure;
HttpClient* timeClient = nullptr;

WiFiClient weatherClientRaw;
HttpClient* weatherClient = nullptr;

// -----------------------------------
// Constants & Delays
// -----------------------------------
const int TIME_DELAY_MS = 10000;
const int CONNECT_DELAY = 1000;  // Shortened to update display more frequently
const int MINUTE_IN_MS = 60000;

// -----------------------------------
// Function Declarations
// -----------------------------------
String getCurrentDateTimeFromAPI();
float getCurrentTemperatureFromWeatherAPI(const String& targetHour);
void updateDisplayMessage(const char* message);

void setup() {
  Serial.begin(9600);
  u8g2.begin();
  Wire.begin();

  // Initial display message
  updateDisplayMessage("Connecting to WiFi...");

  Serial.println("Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // Show a connecting message until WiFi is connected
  while (WiFi.status() != WL_CONNECTED) {
    delay(CONNECT_DELAY);
    updateDisplayMessage("Connecting to WiFi...");
    Serial.print(".");
  }

  Serial.println("\nWiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // For HTTPS: Insecure connection for ESP8266
  timeClientSecure.setInsecure();

  // Initialize HttpClient objects
  timeClient = new HttpClient(timeClientSecure, timeHost, timePort);
  weatherClient = new HttpClient(weatherClientRaw, weatherHost, weatherPort);

  // Before proceeding with normal loop operation, ensure we can get at least one successful time request
  // to confirm that the connection and APIs are working.
  while (true) {
    updateDisplayMessage("Requesting current time...");
    String currentDateTime = getCurrentDateTimeFromAPI();
    if (currentDateTime == "") {
      Serial.println("Failed to get current date/time. Retrying...");
      updateDisplayMessage("Time request failed.");
      delay(TIME_DELAY_MS);
    } else {
      // Break out of the loop once we have a successful response
      Serial.println("Time API responded successfully.");
      break;
    }
  }
}

void loop() {
  // 1. Get current date/time from API
  String currentDateTime = getCurrentDateTimeFromAPI();
  if (currentDateTime == "") {
    Serial.println("Failed to get current date/time.");
    delay(TIME_DELAY_MS);
    return;
  }

  // Extract date and hour from the datetime string
  // Example datetime: "2024-12-08T20:31:30.5139483"
  String currentDate = currentDateTime.substring(0, 10);   // "YYYY-MM-DD"
  String currentHour = currentDateTime.substring(11, 13);  // "HH"
  String targetHourStr = currentDate + "T" + currentHour + ":00";

  // 2. Get weather data for the current hour from API
  float currentApiTemperature = getCurrentTemperatureFromWeatherAPI(targetHourStr);

  // 3. Read temperature from the local sensor
  float localSensorTemp = temperatureSensor.readTemperatureC();

  // -----------------------------------
  // Display on OLED
  // -----------------------------------
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14B_tr);

  // Draw date/time
  String dateStr = currentDate;
  String timeStr = currentDateTime.substring(11, 19);  // "HH:MM:SS"
  u8g2.setDrawColor(1);
  u8g2.drawStr(0, 10, dateStr.c_str());
  u8g2.drawStr(0, 25, timeStr.c_str());

  // Draw local sensor temperature
  String localTempStr = "Sensor Temp: " + String(localSensorTemp) + "C";
  u8g2.drawStr(0, 40, localTempStr.c_str());
  Serial.print("Sensor: ");
  Serial.println(localSensorTemp);
  // Draw API temperature if available
  if (!isnan(currentApiTemperature)) {
    String apiTempStr = "API Temp: " + String(currentApiTemperature) + "C";
    u8g2.drawStr(0, 55, apiTempStr.c_str());
  }

  u8g2.sendBuffer();

  delay(MINUTE_IN_MS);
}

// Update OLED display with a single status message (clears the screen first)
void updateDisplayMessage(const char* message) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14B_tr);
  u8g2.setDrawColor(1);
  u8g2.drawStr(0, 25, message);
  u8g2.sendBuffer();
}

String getCurrentDateTimeFromAPI() {
  Serial.println("Requesting current time...");
  timeClient->get(timeUrl);

  int statusCode = timeClient->responseStatusCode();
  String response = timeClient->responseBody();

  if (statusCode != 200) {
    Serial.print("Time API request failed. Status code: ");
    Serial.println(statusCode);
    Serial.println("Response:");
    Serial.println(response);
    return "";
  }

  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, response);
  if (error) {
    Serial.print("Time JSON parse error: ");
    Serial.println(error.c_str());
    Serial.println("RAW:");
    Serial.println(response);
    return "";
  }

  const char* dateTime = doc["dateTime"];
  if (!dateTime) {
    Serial.println("No dateTime field found in time API response.");
    Serial.println("RAW:");
    Serial.println(response);
    return "";
  }

  return String(dateTime);
}

float getCurrentTemperatureFromWeatherAPI(const String& targetHour) {
  Serial.println("Requesting current weather...");
  weatherClient->get(weatherUrl);

  int statusCode = weatherClient->responseStatusCode();
  String response = weatherClient->responseBody();

  if (statusCode != 200) {
    Serial.print("Weather API request failed. Status code: ");
    Serial.println(statusCode);
    Serial.println("Response:");
    Serial.println(response);
    return NAN;
  }

  StaticJsonDocument<2048> doc;
  DeserializationError error = deserializeJson(doc, response);
  if (error) {
    Serial.print("Weather JSON parse error: ");
    Serial.println(error.c_str());
    Serial.println("RAW:");
    Serial.println(response);
    return NAN;
  }

  JsonArray timeArray = doc["hourly"]["time"];
  JsonArray temperatureArray = doc["hourly"]["temperature_2m"];

  if (timeArray.isNull() || temperatureArray.isNull()) {
    Serial.println("Temperature or time data not found in the response.");
    Serial.println("RAW:");
    Serial.println(response);
    return NAN;
  }

  int dataCount = min(timeArray.size(), temperatureArray.size());
  for (int i = 0; i < dataCount; i++) {
    const char* timeVal = timeArray[i];
    float tempVal = temperatureArray[i];

    if (String(timeVal) == targetHour) {
      return tempVal;
    }
  }

  return NAN;
}