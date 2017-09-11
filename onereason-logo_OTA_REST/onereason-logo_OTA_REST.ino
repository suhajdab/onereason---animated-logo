#include "FastLED.h"
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "secrets.h"
#include "palettes.h"


const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSW;
const char* host = "onereason-logo";

ESP8266WebServer server(80);

FASTLED_USING_NAMESPACE
#if defined(FASTLED_VERSION) && (FASTLED_VERSION < 3001000)
#warning "Requires FastLED 3.1 or later; check github for latest code."
#endif

#define FRAMES_PER_SECOND  120
#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

#define NUM_LEDS 64
#define DATA_PIN 2
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
/**
   2 NeoPixel strips back to back
   32 inner circle
   32 outer circle
*/

CRGB leds[NUM_LEDS];

// ten seconds per color palette makes a good demo
// 20-120 is better for deployment
#define SECONDS_PER_PALETTE 30

bool beOn = true;
bool isOn = true;


// SETUP
void setup() {
  delay(3000); // 3 second delay for recovery

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setMaxPowerInVoltsAndMilliamps(3, 300);

  setup_wifi();
  setup_OTA();

  if (MDNS.begin(host)) {
    Serial.println("MDNS responder started");
  }

  server.on("/light", HTTP_GET, handleGET);
  server.on("/light", HTTP_POST, handlePOST);
  server.onNotFound(handleNotFound);
  server.begin();
}

void setup_wifi() {
  WiFi.mode(WIFI_STA);

  WiFi.begin(ssid, password);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    WiFi.begin(ssid, password);
  }
}

void setup_OTA() {
  ArduinoOTA.setHostname(host);
  ArduinoOTA.onStart([]() { // switch off all the PWMs during upgrade
    analogWrite(LED_BUILTIN, 0);
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
  });

  ArduinoOTA.onEnd([]() { // do a fancy thing with our board led at end
    fill_solid(leds, NUM_LEDS, CRGB::Green);
    FastLED.show();
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    // use pixels to indicate upload progress
    uint8_t progressLed = 32 - 32 * progress / total;
    fadeToBlackBy( leds, NUM_LEDS, 2);
    leds[progressLed] = CRGB::White;
    leds[NUM_LEDS - progressLed] = CRGB::White;
    FastLED.show();
  });

  ArduinoOTA.onError([](ota_error_t error) {
    fill_solid(leds, NUM_LEDS, CRGB::Red);
    FastLED.show();
    ESP.restart();
  });

  /* setup the OTA server */
  ArduinoOTA.begin();
}

// return state as JSON
void handleGET() {
  String message = "";
  message += "{ \"on\": ";
  message += isOn ? "true" : "false";
  message += " }";
  server.send(200, "application/json", message);
}

// change state
void handlePOST() {
  String message = "";
  if (server.arg("plain") == "ON") {
    beOn = true;
  } else {
    beOn = false;
  }
  server.send(200, "text/plain", "OK");
}

void handleNotFound() {
  //  digitalWrite(led, 1);
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  //  digitalWrite(led, 0);
}


// Forward declarations of an array of cpt-city gradient palettes, and
// a count of how many there are.  The actual color palette definitions
// are at the bottom of this file.
extern const TProgmemRGBGradientPalettePtr gGradientPalettes[];
extern const uint8_t gGradientPaletteCount;

// Current palette number from the 'playlist' of color palettes
uint8_t gCurrentPaletteNumber = 0;

CRGBPalette16 gCurrentPalette( CRGB::Black);
CRGBPalette16 gTargetPalette( gGradientPalettes[0] );


// LOOP
void loop() {
  if (beOn != isOn) {
    if (beOn) {
      // restore last palette
      gTargetPalette = gGradientPalettes[ gCurrentPaletteNumber ];
    } else {
      // activate black palette
      gTargetPalette = gGradientPalettes[ gGradientPaletteCount - 1 ];
    }

    isOn = beOn;
  }

  if (isOn) {
    EVERY_N_SECONDS( SECONDS_PER_PALETTE ) {
      gCurrentPaletteNumber = addmod8( gCurrentPaletteNumber, 1, gGradientPaletteCount - 1);
      gTargetPalette = gGradientPalettes[ gCurrentPaletteNumber ];
    }
  }

  EVERY_N_MILLISECONDS(40) {
    nblendPaletteTowardPalette( gCurrentPalette, gTargetPalette, 16);
  }

  colorwaves( leds, NUM_LEDS, gCurrentPalette);

  FastLED.show();
  FastLED.delay(20);

  if (WiFi.status() != WL_CONNECTED) {
    delay(1);
    Serial.print("WIFI Disconnected. Attempting reconnection.");
    setup_wifi();
    return;
  }
  
  ArduinoOTA.handle();
  server.handleClient();
}


// This function draws color waves with an ever-changing,
// widely-varying set of parameters, using a color palette.
void colorwaves( CRGB * ledarray, uint16_t numleds, CRGBPalette16 & palette) {
  static uint16_t sPseudotime = 0;
  static uint16_t sLastMillis = 0;
  static uint16_t sHue16 = 0;

  uint8_t sat8 = beatsin88( 87, 220, 250);
  uint8_t brightdepth = beatsin88( 341, 96, 224);
  uint16_t brightnessthetainc16 = beatsin88( 203, (25 * 256), (40 * 256));
  uint8_t msmultiplier = beatsin88(147, 23, 60);

  uint16_t hue16 = sHue16;//gHue * 256;
  uint16_t hueinc16 = beatsin88(113, 300, 1500);

  uint16_t ms = millis();
  uint16_t deltams = ms - sLastMillis ;
  sLastMillis  = ms;
  sPseudotime += deltams * msmultiplier;
  sHue16 += deltams * beatsin88( 400, 5, 9);
  uint16_t brightnesstheta16 = sPseudotime;

  for ( uint16_t i = 0 ; i < numleds; i++) {
    hue16 += hueinc16;
    uint8_t hue8 = hue16 / 256;
    uint16_t h16_128 = hue16 >> 7;
    if ( h16_128 & 0x100) {
      hue8 = 255 - (h16_128 >> 1);
    } else {
      hue8 = h16_128 >> 1;
    }

    brightnesstheta16  += brightnessthetainc16;
    uint16_t b16 = sin16( brightnesstheta16  ) + 32768;

    uint16_t bri16 = (uint32_t)((uint32_t)b16 * (uint32_t)b16) / 65536;
    uint8_t bri8 = (uint32_t)(((uint32_t)bri16) * brightdepth) / 65536;
    bri8 += (255 - brightdepth);

    uint8_t index = hue8;
    //index = triwave8( index);
    index = scale8( index, 240);

    CRGB newcolor = ColorFromPalette( palette, index, bri8);

    uint16_t pixelnumber = i;
    pixelnumber = (numleds - 1) - pixelnumber;

    nblend( ledarray[pixelnumber], newcolor, 128);
  }
}
