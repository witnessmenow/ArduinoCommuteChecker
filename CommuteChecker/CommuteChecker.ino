/*******************************************************************
 *  A project to light up leds fro the quickest route using
 *  live traffic times from Google maps.
 *  Also has a 7 segment display to show travel time
 *
 *  Main Hardware:
 *  - ESP8266 (I used a Wemos D1 Mini Clone)
 *  - PL9823 Addressable LEDS (Similar to Neopixels)
 *  - 4 digit 7 Segment display with a TM1637 driver
 *
 *  Written by Brian Lough
 *******************************************************************/

// ----------------------------
// Standard Libraries
// ----------------------------

#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include "FS.h"

// ----------------------------
// Additional libraries - each one of these will need to be installed.
// ----------------------------

#include <WiFiManager.h>
// For configuring the Wifi credentials without re-programing
// Availalbe on library manager (WiFiManager)
// https://github.com/tzapu/WiFiManager

#include <GoogleMapsDirectionsApi.h>
// For accessing Google Maps Api
// Availalbe on library manager (GoogleMapsApi)
// https://github.com/witnessmenow/arduino-google-maps-api

#include <JsonStreamingParser.h>
// is a required dependancy of the GoogleMapsDirectionsApi
// Available on the library manager (Json Streaming Parser)
// https://github.com/squix78/json-streaming-parser

#include <ArduinoJson.h>
// For the config file
// Available on the library manager (ArduinoJson)
// https://github.com/bblanchon/ArduinoJson

#include <DoubleResetDetector.h>
// For entering Config mode by pressing reset twice
// Available on the library manager (Double Reset Detector)
// https://github.com/datacute/DoubleResetDetector

#include <Adafruit_NeoPixel.h>
// For controlling the Addressable LEDs
// Available on the library manager (Adafruit Neopixel)
// https://github.com/adafruit/Adafruit_NeoPixel

#include <TM1637Display.h>
// For controlling the 7-segment display
// Not yet available on the library manager
// Go to the github page and there is a download button
// Click Download as zip, and add to Arduino IDE(Sketch->Include Library-> Add .zip library)
// https://github.com/avishorp/TM1637

#include <NTPClient.h>
// For keeping the clock time for the 7-segment display
// Available on the library manager (NTPClient)
// https://github.com/arduino-libraries/NTPClient


// The name of the config file stored on SPIFFS, it will create this if it doesn't exist
#define ROUTE_CONFIG_FILE "route.config"

struct Routes{
  String description;
  String waypoint;
  DirectionsResponse response;
  int * leds;
  int numLeds;
  uint8_t label;
};

const uint8_t LETTER_A = SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G;
const uint8_t LETTER_B = SEG_C | SEG_D | SEG_E | SEG_F | SEG_G;
const uint8_t LETTER_C = SEG_A | SEG_D | SEG_E | SEG_F;
const uint8_t LETTER_D = SEG_B | SEG_C | SEG_D | SEG_E | SEG_G;
const uint8_t LETTER_E = SEG_A | SEG_D | SEG_E | SEG_F | SEG_G;
const uint8_t LETTER_F = SEG_A | SEG_E | SEG_F | SEG_G;

const uint8_t LETTER_O = SEG_C | SEG_D | SEG_E | SEG_G;

const uint8_t SEG_BOOT[] = {
  LETTER_B,                                        // b
  LETTER_O,                                        // o
  LETTER_O,                                        // o
  SEG_D | SEG_E | SEG_F | SEG_G                    // t - kinda
  };

  const uint8_t SEG_CONF[] = {
  LETTER_C,                                        // C
  LETTER_O,                                        // o
  SEG_C | SEG_E | SEG_G,                           // n
  LETTER_F                                         // F
  };

  const uint8_t SEG_DONE[] = {
  LETTER_D,                                        // d
  LETTER_O,                                        // o
  SEG_C | SEG_E | SEG_G,                           // n
  LETTER_E                                         // E
  };

  const uint8_t SEG_ERR[] = {
  LETTER_E,                                       // E
  SEG_E | SEG_G,                                  // r
  SEG_E | SEG_G,                                  // r
  0
  };

// ----------------------------
// Change the following to adapt for you
// ----------------------------

// Pin that your addressable LEDS are connected to
#define LED_PIN D3

// Pins for 7 Segment Display module
#define CLK D6
#define DIO D5

#define NUMBER_OF_ROUTES 3

// Total number of addressable LEDs connected
#define NUMBER_OF_LEDS 18

// Set between 0 and 255, 255 being the brigthest
#define BRIGTHNESS 16

// Server to get the time off (shouldn't need to change)
// See here for a list: http://www.pool.ntp.org/en/
const char timeServer[] = "pool.ntp.org";

// Offset for setting the time
// Get the value from https://epochconverter.com/timezones
#define NTP_OFFSET 3600

// If the travel time is longer than normal + MEDIUM_TRAFFIC_THRESHOLD, light the route ORANGE
// Value is in seconds
#define MEDIUM_TRAFFIC_THRESHOLD 60

// If the travel time is longer than normal + BAD_TRAFFIC_THRESHOLD, light the route RED
// Value is in seconds (5 * 60 = 300)
#define BAD_TRAFFIC_THRESHOLD 300

// Default Directions API key, you can set this if you want or put it in using the WiFiManager
char apiKey[45] = "";

//Free Google Maps Api only allows for 2500 "elements" a day
// 3 routes can be requested every two minutes and be under the limit
unsigned long delayBetweenApiCalls = 1000 * 60 * 2; // 2 mins

unsigned long delayBetweenDisplayChange = 1000 * 15;


//Where journey should start and end
String origin = "53.3002785,-8.9872549"; // Parkmore East
String destination = "Dun+Na+Coiribe,+Co.+Galway";

// Led Array is the indexs of the Addressable Leds that are on each route
int routeALeds[] = {0,2,5,8,10,12,14,15};
int routeBLeds[] = {0,1,3,4,6,7,10,12,14,15};
int routeCLeds[] = {0,2,5,9,11,13,16,17};

// Add or remove Routes here, make sure to change the NUMBER_OF_ROUTES define to match
void populateRoutes() {
  // Waypoiny needs "via:" before the co-ord or address for traffic to work
  // (Id, Description, waypoint for journey between origin and destination, Led Array, Number of Leds)

  Serial.println("Populating Routes:");

  setRoute(0, "Tuam Road", "via:53.3003501,-9.0073709", routeALeds, 8, LETTER_A);

  setRoute(1, "Castlegar sideroad", "via:53.2981156,-9.0287593", routeBLeds, 10, LETTER_B);

  setRoute(2, "Bypass", "via:53.2912109,-8.9988612", routeCLeds, 8, LETTER_C);
}

// You also may need to change the colours in the getRouteColour method.
// The leds I used seemed to have the Red colour and Green colour swapped in comparison
// to the library's documentation.

// ----------------------------
// End of area you need to change
// ----------------------------

Adafruit_NeoPixel leds = Adafruit_NeoPixel(NUMBER_OF_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
Routes routes[NUMBER_OF_ROUTES];
TM1637Display display(CLK, DIO);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, timeServer, NTP_OFFSET, 60000);

// Number of seconds after reset during which a
// subseqent reset will be considered a double reset.
#define DRD_TIMEOUT 10

// RTC Memory Address for the DoubleResetDetector to use
#define DRD_ADDRESS 0

DoubleResetDetector drd(DRD_TIMEOUT, DRD_ADDRESS);

void setRoute(int id, String description, String waypoint, int leds[], int numLeds, uint8_t label) {
  routes[id].description = description;
  routes[id].waypoint = waypoint;
  routes[id].leds = leds;
  routes[id].numLeds = numLeds;
  routes[id].label = label;
}

WiFiClientSecure client;
GoogleMapsDirectionsApi *directionsApi;
DirectionsInputOptions inputOptions;

unsigned long api_due_time = 0;
unsigned long displayChangeDueTime = 0;
int displayState = 0;

int fastestRouteId = 0;

// flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());

  display.setSegments(SEG_CONF);
  drd.stop();
}

void setup() {

  Serial.begin(115200);

  display.setBrightness(0x0f);
  display.setSegments(SEG_BOOT);

  leds.begin(); // This initializes the NeoPixel library.
  leds.setBrightness(BRIGTHNESS);
  unLightAllLeds();

  populateRoutes();

  if (!SPIFFS.begin()) {
    Serial.println("Failed to mount file system");
    return;
  }

  loadConfig();

  WiFiManager wifiManager;
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  // Adding an additional config on the WIFI manager webpage for the API Key
  WiFiManagerParameter customApiKey("apiKey", "API Key", apiKey, 50);
  wifiManager.addParameter(&customApiKey);

  if (drd.detectDoubleReset()) {
    Serial.println("Double Reset Detected");
    wifiManager.startConfigPortal("RouteCheck", "thepassword");
  } else {
    Serial.println("No Double Reset Detected");
    wifiManager.autoConnect("RouteCheck", "thepassword");
  }

  strcpy(apiKey, customApiKey.getValue());

  if (shouldSaveConfig) {
    saveConfig();
  }
  display.setSegments(SEG_DONE);
  directionsApi = new GoogleMapsDirectionsApi(apiKey, client);
  inputOptions.departureTime = "now";

  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  IPAddress ip = WiFi.localIP();
  Serial.println(ip);
  timeClient.begin();
  drd.stop();

}

bool loadConfig() {
  File configFile = SPIFFS.open(ROUTE_CONFIG_FILE, "r");
  if (!configFile) {
    Serial.println("Failed to open config file");
    return false;
  }

  size_t size = configFile.size();
  if (size > 1024) {
    Serial.println("Config file size is too large");
    return false;
  }

  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);

  configFile.readBytes(buf.get(), size);

  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(buf.get());

  if (!json.success()) {
    Serial.println("Failed to parse config file");
    return false;
  }

  strcpy(apiKey, json["mapsApiKey"]);
  return true;
}

bool saveConfig() {
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
  json["mapsApiKey"] = apiKey;

  File configFile = SPIFFS.open(ROUTE_CONFIG_FILE, "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing");
    return false;
  }

  json.printTo(configFile);
  return true;
}

void displayResponse(DirectionsResponse response) {
  Serial.println("Response:");
  Serial.print("Trafic from ");
  Serial.print(response.start_address);
  Serial.print(" to ");
  Serial.println(response.end_address);

  Serial.print("Duration in Traffic text: ");
  Serial.println(response.durationTraffic_text);
  Serial.print("Duration in Traffic in seconds: ");
  Serial.println(response.durationTraffic_value);

  Serial.print("Normal duration text: ");
  Serial.println(response.duration_text);
  Serial.print("Normal duration in seconds: ");
  Serial.println(response.duration_value);

  Serial.print("Distance text: ");
  Serial.println(response.distance_text);
  Serial.print("Distance in meters: ");
  Serial.println(response.distance_value);
}

void getTravelTimes(){
  for(int i=0; i < NUMBER_OF_ROUTES; i++) {

    Serial.print("Getting data for route: ");
    Serial.println(routes[i].description);

    inputOptions.waypoints = routes[i].waypoint;
    routes[i].response = directionsApi->directionsApi(origin, destination, inputOptions);
  }
}

int findFastestRoute() {
  int fastestId = 0;
  int fastestTravelTime = routes[0].response.durationTraffic_value;
  for(int i=1; i < NUMBER_OF_ROUTES; i++) {
    if((fastestTravelTime > routes[i].response.durationTraffic_value && routes[i].response.durationTraffic_value > 0) || fastestTravelTime == 0) {
      fastestTravelTime = routes[i].response.durationTraffic_value;
      fastestId = i;
    }
  }

  Serial.print("Fastest way is: ");
  Serial.println(routes[fastestId].description);
  if (fastestTravelTime == 0) {
    return -1;
  }
  return fastestId;
}

uint32_t getRouteColour(int routeId) {

  int difference = routes[routeId].response.durationTraffic_value - routes[routeId].response.duration_value;

  if(difference > BAD_TRAFFIC_THRESHOLD) {
    return leds.Color(0, 255, 0); // Red
  } else if ( difference > MEDIUM_TRAFFIC_THRESHOLD ) {
    return leds.Color(60, 180, 0); // Yellow
  }

  return leds.Color(255, 0, 0); //Green
}

void unLightAllLeds() {
  for(int i=0; i< NUMBER_OF_LEDS; i++) {
    leds.setPixelColor(i, leds.Color(0, 0, 0));
  }
  leds.show();
}

void displayTravelTime(int routeId) {
  uint8_t data[4];
  int travelInMinutes = routes[routeId].response.durationTraffic_value / 60;

  data[0] = routes[routeId].label;
  data[1] = SEG_G;
  data[2] = display.encodeDigit(travelInMinutes / 10);
  data[3] = display.encodeDigit(travelInMinutes % 10);

  display.setSegments(data);
}

void displayError() {
  display.setSegments(SEG_ERR);
}

void lightRoute(int routeId) {
  unLightAllLeds();
  uint32_t colour = getRouteColour(routeId);
  for (int j = 1; j <= routes[routeId].numLeds; j++) {
    for (int i = 0; i < j; i++) {
      int offset = routes[routeId].numLeds - 1 - i;
      int ledIndex = *(routes[routeId].leds +offset);
      leds.setPixelColor(ledIndex, colour);
    }
    leds.show();
    delay(100);
  }
}

void serialPrintTravelTimes() {
  for(int i=0; i < NUMBER_OF_ROUTES; i++) {
    displayResponse(routes[i].response);
  }
}

void displayTime() {
  unsigned long epoch = timeClient.getEpochTime();
  int hour = (epoch  % 86400L) / 3600;
  int minutes = (epoch % 3600) / 60;

  uint8_t data[4];

  if(hour < 10) {
    data[0] = display.encodeDigit(0);
    data[1] = display.encodeDigit(hour);
  } else {
    data[0] = display.encodeDigit(hour /10);
    data[1] = display.encodeDigit(hour % 10);
  }

  // Turn on double dots
  data[1] = data[1] | B10000000;

  if(minutes < 10) {
    data[2] = display.encodeDigit(0);
    data[3] = display.encodeDigit(minutes);
  } else {
    data[2] = display.encodeDigit(minutes /10);
    data[3] = display.encodeDigit(minutes % 10);
  }

  display.setSegments(data);
}

void loop() {
  unsigned long timeNow = millis();
  if (timeNow > api_due_time)  {
    Serial.println("Checking maps");
    getTravelTimes();
    // serialPrintTravelTimes();
    fastestRouteId = findFastestRoute();
    displayState = 0;

    if(fastestRouteId >= 0) {
      displayTravelTime(fastestRouteId);
      lightRoute(fastestRouteId);
    } else {
      //There was no valid response
      displayError();
    }

    api_due_time = timeNow + delayBetweenApiCalls;
    displayChangeDueTime = millis() + delayBetweenDisplayChange;
  }
  timeNow = millis();
  if (timeNow > displayChangeDueTime) {
    if(displayState == 0) {
      displayState = 1;
      timeClient.update();
    } else {
      displayState = 0;
      displayTravelTime(fastestRouteId);
    }
    displayChangeDueTime = millis() + delayBetweenDisplayChange;
  }

  if(displayState == 1) {
    displayTime();
  }
}
