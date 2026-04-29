#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHTesp.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESP32Servo.h> 

#define BUZZER 12
#define BUZZER_CHANNEL 0
#define BUZZER_FREQ 2000
#define BUZZER_RESOLUTION 8
#define LDR_PIN 34
#define MAX_SAMPLES 500


float light_inten;
float temp;
float theta;
int angle;

Servo myservo;
int servoPin = 26;


unsigned long lastSampleTime = 0;
unsigned long lastSendTime = 0;

int samplingInterval = 5000; // 5 sec default
int sendingInterval = 60000; // 2 min default

int theta_offset = 30;
float gamma_val = 0.75;
int T_med = 30;

float samples[MAX_SAMPLES];  // Holds normalized samples
int sampleCount = 0;

const int DHT_PIN = 15;
WiFiClient espClient;
PubSubClient mqttClient(espClient);
DHTesp dhtSensor;
char tempAr[6];
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);


bool isScheduledON = false;
unsigned long scheduledOnTime;

void connect_to_wifi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin("Wokwi-GUEST", "");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("✅ WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}
float readNormalizedLDR() {
  int raw = analogRead(LDR_PIN);
  light_inten = raw / 4095.0;
  return raw / 4095.0;
}

void sampleLDR() {
  if (sampleCount < MAX_SAMPLES) {
    samples[sampleCount++] = readNormalizedLDR();
  }
}

void sendAverage() {
  if (sampleCount == 0) return;

  float sum = 0;
  for (int i = 0; i < sampleCount; i++) {
    sum += samples[i];
  }
  float avg = sum / sampleCount;
  char msg[10];
  dtostrf(avg, 4, 3, msg);  // Format: 0.123
  mqttClient.publish("lightAvg", msg);
  Serial.print("📤 Sent average: "); Serial.println(msg);

  sampleCount = 0; // reset buffer
}

void buzzerOn(bool on){
  if(on) {
    ledcWrite(BUZZER_CHANNEL, 128);  // 50% duty cycle for buzzer tone
  }else{
    ledcWrite(BUZZER_CHANNEL, 0);    // Turn off
  }
}
unsigned long getTime() {
  timeClient.update();
  return timeClient.getEpochTime();
}
void checkSchedule(){
  if(isScheduledON){
    unsigned long currentTime = getTime();
    Serial.println(currentTime);
    if(currentTime > scheduledOnTime){
      buzzerOn(true);
      isScheduledON = false;
      mqttClient.publish("EE-MainPub", "1");
      mqttClient.publish("EE-SchPub", "0");
      Serial.println("Scheduled ON");
    }
  }
}
void receiveCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  char payloadCharAr[length];
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
    payloadCharAr[i] = (char)payload[i];
  }
  Serial.println();
  if (strcmp(topic, "ON-OFF") == 0) {
    buzzerOn(payloadCharAr[0] == '1');
  }else if(strcmp(topic, "EE-Test") == 0){
      if(payloadCharAr[0] == 'N'){
        isScheduledON = false;
      }else{
        isScheduledON = true;
        Serial.println("Scheduled ON");
        scheduledOnTime = atol(payloadCharAr);
      }
  }
  if (strcmp(topic, "samplingInterval") == 0) {
    String value = String((char*)payload);
    samplingInterval = value.toInt() * 1000;
    Serial.println("Sampling interval updated: " + String(samplingInterval));
  }else if (strcmp(topic, "sendingInterval") == 0) {
    String value = String((char*)payload);
    sendingInterval = value.toInt() * 1000;
    Serial.println("Sending interval updated: " + String(sendingInterval));
  }else if (strcmp(topic, "minimumAngle") == 0){
    String value = String((char*)payload);
    theta_offset = value.toInt();
    Serial.println("offset angle updated: " + String(theta_offset));
  }else if (strcmp(topic, "controllingFactor") == 0){
    String value = String((char*)payload);
    gamma_val = value.toFloat();
    Serial.print("controlling factor updated: " );
    Serial.println(gamma_val);
  }else if (strcmp(topic, "idealStorageTemperature") == 0){
    String value = String((char*)payload);
    T_med = value.toInt();
    Serial.println("ideal storage temp updated: " + String(T_med));
  }//idealStorageTemperature
}

void setupMqtt(){
  mqttClient.setServer("broker.hivemq.com",1883);
  mqttClient.setCallback(receiveCallback);
}


void updateTemperature() {
  TempAndHumidity data = dhtSensor.getTempAndHumidity();
  temp = data.temperature;
  String(data.temperature, 2).toCharArray(tempAr, 6);
}
void motorDrive(){
  
  readNormalizedLDR();  // Always update LDR before use

  float ln_ratio = 0;
  if(samplingInterval > 0 && sendingInterval > 0) {
    ln_ratio = log((float)samplingInterval / (float)sendingInterval);
  }

  theta = theta_offset + ((180 - theta_offset) * light_inten * gamma_val * ln_ratio * (temp / T_med));
  theta = constrain(theta, 0, 180);
  myservo.write((int)theta);
  char msg[10];
  dtostrf(theta, 4, 2, msg);  // Format: e.g., "90.00"
  mqttClient.publish("Servo", msg);

  //Serial.print("🔁 Motor Angle Set To: ");
  //Serial.println((int)theta);
}



void setup() {
  Serial.begin(115200);
  Serial.println("Hello from Wokwi!");
  connect_to_wifi();
  setupMqtt();
  dhtSensor.setup(DHT_PIN, DHTesp::DHT22);
  
  ledcSetup(BUZZER_CHANNEL, BUZZER_FREQ, BUZZER_RESOLUTION);
  ledcAttachPin(BUZZER, BUZZER_CHANNEL);
  ledcWrite(BUZZER_CHANNEL, 0); // Ensure buzzer is off

  timeClient.begin();
  timeClient.setTimeOffset(5.5*3600);
  Serial.print("⏳ Waiting for NTP time...");
  while(!timeClient.update()) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("✅ Current Epoch Time: ");
  Serial.println(timeClient.getEpochTime());
  myservo.setPeriodHertz(50);  // Standard 50 Hz servo
  myservo.attach(26, 1000, 2000);    


}
void connectToBroker(){
  while(!mqttClient.connected()){
    Serial.print("Attempting MQTT Connection");
  
    if(mqttClient.connect("ESP-32")){
      Serial.print("Connected");
      mqttClient.subscribe("ON-OFF");
      mqttClient.subscribe("EE-Test");
      mqttClient.subscribe("samplingInterval");
      mqttClient.subscribe("sendingInterval");
      mqttClient.subscribe("minimumAngle");
      mqttClient.subscribe("controllingFactor");
      mqttClient.subscribe("idealStorageTemperature");
    }else{
      Serial.print("failed");
      Serial.print(mqttClient.state());
      delay(5000);
    }
  }
  
}

void loop() {
  // MQTT logic can go here later
  if(!mqttClient.connected()){
    connectToBroker();
  }
  mqttClient.loop();
  updateTemperature();
  //Serial.println(tempAr);
  mqttClient.publish("EE",tempAr);
  checkSchedule();
  unsigned long now = millis();

  if (now - lastSampleTime >= samplingInterval) {
    lastSampleTime = now;
    sampleLDR();
    Serial.print("Sampled LDR: ");
    Serial.println(sampleCount);
  }

  if (now - lastSendTime >= sendingInterval) {
    lastSendTime = now;
    sendAverage();
  }
 
  motorDrive();
  delay(1000);
}

