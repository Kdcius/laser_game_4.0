//This piece of code is embedded in an ESP32, it will act as a laser tag receiver shield attached to the chest of the player.
//First the pistol (ESP32) will send its playerID and mac address to the shield via IR signal. The shield will then connect to the pistol using MQTT
//When linked, the receiver will send every IR signal received to the pistol via MQTT

#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <ESP8266WiFi.h>
// #include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <FastLED.h>

#define NUM_LEDS 8

#define VIBRATOR_PIN 12

IRrecv irrecv(13);  // Create an IR receiver object

CRGB leds[NUM_LEDS];

int shieldID = -1;  // The ID of the shield, it will be set by the pistol
int playerID = -1;
bool linked = false;
bool selfShoted = false;
bool shoted = false;
bool reloading = false;
bool playerHit = false;
String topic = "";
uint32_t receivedData = 0;

const unsigned long animDelay = 50; // Delay between animations
unsigned long animStartTime = 0; // Last time an animation was played
unsigned long animLastTime = 500; // Duration of the animation
bool animPlaying = false; // Is an animation currently playing

decode_results results;

WiFiClient espClient;
PubSubClient client(espClient);
const char* mqtt_server = "192.168.1.175";
const char* mqtt_server_hotspot = "192.168.170.193";

void vibration(int duration = 100){
  digitalWrite(VIBRATOR_PIN, HIGH);
  delay(duration);
  digitalWrite(VIBRATOR_PIN, LOW);
}

//Vibrations using analogWrite
void pwmVibration(int duration = 100){
    for(int dutyCycle = 0; dutyCycle < 255; dutyCycle++){   
    // changing the LED brightness with PWM
    analogWrite(VIBRATOR_PIN, dutyCycle);
    delay(int((duration/255)/2));
  }
  digitalWrite(VIBRATOR_PIN, LOW);
  delay(duration/4);
  digitalWrite(VIBRATOR_PIN, HIGH);
  delay(duration/4);
  digitalWrite(VIBRATOR_PIN, LOW);
}

uint8_t position = 0;  // Global variable to keep track of the dot's position
uint8_t frameCounter = 0;  // Counter for frames
void sinelon(CRGB color, int speed = 5, int tailSize = 50) //Smaller speed = faster animation, smaller tailSize = longer tail
{
  fadeToBlackBy( leds, NUM_LEDS, tailSize);
  leds[position] += color;
  // Only increment position every N frames
  const uint8_t framesPerMove = speed;  // Adjust this value to your needs
  if (++frameCounter >= framesPerMove) {
    position = (position + 1) % NUM_LEDS;
    frameCounter = 0;
  }
  FastLED.show();
}


// Global variables to keep track of the light's state and position
uint8_t reloadPosition = 0;
uint8_t reloadBrightness = 0;
bool increasingBrightness = true;
bool animEnded = false;
void reloadLights(int speed = 2) {
  // Adjust speed if necessary
  const uint8_t framesPerChange = speed;

  // Only change brightness every N frames
  static uint8_t frameCounter = 0;
  if (++frameCounter >= framesPerChange && !animEnded) {
    frameCounter = 0;

    // Update brightness
    if (increasingBrightness) {
      if (++reloadBrightness >= 255) {
        increasingBrightness = false;
      }
    } 
    else {
        // Move to the next LED
        increasingBrightness = true;
        reloadPosition = (reloadPosition + 1) % (NUM_LEDS / 2);
        if (reloadPosition == (NUM_LEDS / 2)) {
          reloadPosition = 0;
          reloadBrightness = 0;
          frameCounter = 0;
          animEnded = true;
        }
    }
  }

  if (!animEnded) {
    // Set LEDs color based on the current brightness and position
    leds[reloadPosition] = CRGB(255 - reloadBrightness, (int)(reloadBrightness * 0.6), 0);
    leds[NUM_LEDS - reloadPosition - 1] = CRGB(255 - reloadBrightness, reloadBrightness, 0);

    FastLED.show();
  }
}


void lightAll(CRGB color, int delayTime = 0){
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = color;
    FastLED.show();
  }
  delay(delayTime);
}

void turnOffAll(int delayTime = 0){
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB::Black;
    FastLED.show();
  }
  delay(delayTime);
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println("Received MQTT message: " + message + " on topic: " + String(topic));
  if (message=="rel_start"){
    reloading = true;
    Serial.println("Reloading STARTED");
  }
  if (message=="rel_end"){
    reloading = false;
    turnOffAll();
    Serial.println("Reloading ENDED");
  }
  if (message=="hit"){
    Serial.println("You got hit");
    playerHit=true;
  }
  if (message=="revive"){
    Serial.println("You got revived");
    playerHit=false;
  }
}

void linking(){
  playerID = receivedData;
  shieldID = playerID;
  // Serial.println("Received PlayerID: " + String(playerID));
  //Check if the playerID is valid
  if (playerID >= 0 && playerID < 100) {

    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);
    while (!client.connected()) {
      lightAll(CRGB::OrangeRed);
      Serial.println("Connecting to MQTT with client name: Shield/"+String(shieldID)+"...");
      String clientName="Shield/"+String(shieldID);
      if (client.connect(clientName.c_str())) {
        Serial.println("Connected to MQTT");
      } else {
        Serial.print("failed with state ");
        Serial.println(client.state());
        lightAll(CRGB::Red);
        delay(300);
      }
    }
    lightAll(CRGB::Green, 500);
    turnOffAll();


    // Subscribe to the topic of pistol <-> shield communication
    topic = "pistolShield/" + String(playerID);
    char charTopic[topic.length() + 1];
    topic.toCharArray(charTopic, topic.length() + 1);
    
    if (client.subscribe(charTopic)) { // check for successful subscription
      Serial.println("Subscribed to topic: " + topic);
      
      // Send the shieldID to the pistol to confirm the connection
      if (client.publish(charTopic, "linked")) { // check for successful publish
        // Serial.println("Published linked message to topic: " + topic);
        linked = true;
      }
      else {
        Serial.println("Failed to publish shieldID");
        // Wait for a second to let the pistol receive the shieldID and send it again
        delay(1000);
        // Send the shieldID to the pistol to confirm the connection
        if (client.publish(charTopic, "linked")) { // check for successful publish
          // Serial.println("Published linked message to topic: " + topic);
          linked = true;
        }
        else {
          Serial.println("Failed to publish shieldID, 2nd try");
        }
      }
      // Send "linked" again to confirm the connection
      if (client.publish(charTopic, "linked")) { // check for successful publish
        // Serial.println("Published linked message to topic: " + topic);
        linked = true;
      }
      else {
        Serial.println("Failed to publish shieldID, 3rd try");
      }
      
    } else {
      
      Serial.println("Failed to subscribe to topic: " + topic);
    }
  }
  else {
    Serial.println("Invalid playerID");
  }
}


const char* ssid1 = "Freebox-Romain";
const char* password1 = "xxxxxxxxxx";
const char* ssid2 = "Romain's Galaxy Z Fold2 5G";
const char* password2 = "motdepasse";

void setup() {
  Serial.begin(115200);

  pinMode(VIBRATOR_PIN, OUTPUT);
  vibration(100);
  delay(100);
  vibration(150);

  irrecv.enableIRIn();  // Start the receiver

  FastLED.addLeds<WS2811, 14, GRB>(leds, 8);
  FastLED.setBrightness(64);
  // delay(500);

  Serial.println("Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid1, password1);
  static unsigned long startConnectTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis()-startConnectTime < 15000) {
    sinelon(CRGB(255, 0, 255));
    delay(30);
  }
  if (WiFi.status() != WL_CONNECTED){
    WiFi.disconnect();
    WiFi.begin(ssid2, password2);         // Connect to the WiFi network
    Serial.println("Connecting to backup network...");

    mqtt_server=mqtt_server_hotspot;
    
    delay(1000);
    while (WiFi.status() != WL_CONNECTED) {
      sinelon(CRGB(20, 0, 255));
      delay(30);
    }
    Serial.println("Connected to backup network");
    lightAll(CRGB(20, 0, 255), 500);
    lightAll(CRGB::Green, 500);
    turnOffAll();
  }
  else{
    Serial.println("Connected to primary network");
    lightAll(CRGB(255, 0, 255), 500);
    lightAll(CRGB::Green, 500);
    turnOffAll();
  }

  // PWM vibration
  pwmVibration(200);
}

int lastIntensity = 0;
bool increasing = true;
bool reloadingChanged = false;
bool reloadingLast = false;
bool hitChanged = false;
bool hitLast = false;

void loop() {
  // ArduinoOTA.handle();
  client.loop();  // Keep MQTT connection alive

  reloadingChanged = (reloading != reloadingLast);
  hitChanged = (playerHit != hitLast);

  reloadingLast = reloading;
  hitLast = playerHit;

  //While the shield is not linked we want to softly pulse the LEDs in blue (last intensity should go from 0 to 255 then from 255 to 0)
  if (!linked){
    animPlaying = true;
    lightAll((CRGB(0, 0, lastIntensity)));
    // Serial.println(lastIntensity);
    if (increasing){
      lastIntensity += 9;
      if (lastIntensity >= 250) increasing = false;
    }
    else{
      lastIntensity -= 9;
      if (lastIntensity <= 0) increasing = true;
    }
  }

  // Listen for IR signals
  if (irrecv.decode(&results)) {
    // If a signal is received, process the data
    // Serial.println("BRUT IR signal: " + String(results.value, HEX));
    receivedData = results.value;
    Serial.println("Received IR signal: " + String(receivedData, HEX));
    
    // If the shield is not linked to a pistol, check if the received IR signal is a playerID, and link to the pistol
    if (playerID == -1 || !linked){
      linking();
      //Skip a loop to avoid sending the received IR signal to the pistol
      irrecv.resume();
      if (linked){
        lightAll(CRGB::Green, 300);
        turnOffAll();
        Serial.println("Shield linked to playerID: " + String(playerID));
      }
      return;
    }

    // If the shield is linked to a pistol, send the received IR signal to the pistol
    if (linked) {
      if (String(receivedData, HEX).length() == 7) {
        char charTopic[topic.length() + 1];
        topic.toCharArray(charTopic, topic.length() + 1);
        // Send the received IR signal to the pistol (in HEX format)
        if (client.publish(charTopic, String(receivedData, HEX).c_str())) { // check for successful publish
          // Serial.println("Published IR signal to topic: " + topic);
        } else {
          Serial.println("Failed to publish IR signal");
        }
        //The received IR message is 1200000 where 1 is the playerID, 2 is the shot type and the rest is currently unused
        //Extract the shot type and playerID from the received IR message
        String message = String(receivedData, HEX);
        uint8_t receivedPlayerID = (message.substring(0, 1)).toInt();
        uint8_t receivedShotType = (message.substring(1, 2)).toInt();
        Serial.println("Received playerID: " + String(receivedPlayerID) + " Shot type: " + String(receivedShotType) + " ShieldID: " + String(shieldID));
        //If the received playerID is the same as the shieldID, it means we shot ourself, so we set the selfShot flag to true
        selfShoted = (receivedPlayerID == shieldID);
        //If the received playerID is different from the shieldID, it means we got shot, so we set the shoted flag to true
        shoted = (receivedPlayerID != shieldID);
      }
    }
    irrecv.resume();  // Resume receiving
  }
  //Here we will control the LEDs according to the flags
  //If we shot ourself, we turn the LEDs blue for half a second, we need to do this without blocking the loop
  if (selfShoted){
    if (!animPlaying){
      Serial.println("Self shoted animation STARTED");
      animPlaying = true;
      animStartTime = millis();
    }
    // Serial.println(millis()-animStartTime);
    if (millis()-animStartTime < 500){
      // Serial.println("Self shoted animation UPDATED");
      animLastTime = millis();
      sinelon(CHSV( 200, 255, 192), 20, 10);
    }
    else{
      Serial.println("Self shoted animation ENDED");
      animPlaying = false;
      selfShoted = false;
      for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CRGB::Black;
      }
      FastLED.show();
    }
  }
  // if (shoted){                                             //STILL NEED TO BE TESTED WHEN WE HAVE 2 GUNS
  //   if (!animPlaying){                                     //This could potentially be a problem if we get shot while the self shoted animation is playing
  //     Serial.println("Shoted animation STARTED");
  //     animPlaying = true;
  //     animStartTime = millis();
  //   }
  //   Serial.println(millis()-animStartTime);
  //   if (millis()-animStartTime < 500){
  //     Serial.println("Shoted animation UPDATED");
  //     animLastTime = millis();
  //     sinelon(CRGB::Red, 20, 10);
  //   }
  //   else{
  //     Serial.println("Shoted animation ENDED");
  //     animPlaying = false;
  //     shoted = false;
  //     for (int i = 0; i < NUM_LEDS; i++) {
  //       leds[i] = CRGB::Black;
  //     }
  //     FastLED.show();
  //   }
  // }
  if (reloading){  //If we are reloading, we turn the LEDs violet
    reloadLights();
  }
  // We want to turn off the leds when reloading is finished
  if (reloadingChanged && !reloading){
    turnOffAll();
    animEnded = false;
  }

  if (playerHit){
    lightAll(CRGB::Red);
  }
  else if (hitChanged){
    turnOffAll();
  }
}
