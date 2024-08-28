#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <string>
#include <ESP32Servo.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Stepper.h>

// Declare
const char* ssid = "Wokwi-GUEST";
const char* password = "";

const char* mqttServer = "broker.hivemq.com";
const int  portMQTT = 1883;


WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);


// Khai báo cho các cảm biến siêu âm
const int trigPin1 = 10;
const int echoPin1 = 35;

const int trigPin2 = 32;
const int echoPin2 = 33;

const int trigPin3 = 25;
const int echoPin3 = 26;


const int trigPin4 = 12;
const int echoPin4 = 13;

const int trigPin5 = 27;
const int echoPin5 = 14;

// Khai báo cho động cơ servo
Servo myServo;

// Khai báo cho Stepper Motor
const int stepsPerRevolution = 200;  // số bước mỗi vòng quay
Stepper myStepper(stepsPerRevolution, 16, 17);

// Khai báo cho buzzer và LED
const int buzzerPin = 19;
const int ledPin1 = 5;  // Red LED
const int ledPin2 = 18; // Limegreen LED
const int ledPin3 = 23; // Orange LED
const int pirPin = 9;
const int batteryPin = 34;

unsigned long pastTime_lcd = 0;
unsigned long intervalTime = 2500;

int mode_lcd = 2;

bool status_bin = 0;
bool enable_button = 1;
int batteryPercent = 0;

LiquidCrystal_I2C lcd = LiquidCrystal_I2C(0x27, 16, 2);

enum WasteType {
  PLASTIC_WASTE,   // Rác thải nhựa
  METAL_WASTE,     // Rác thải kim loại
  ORGANIC_WASTE,   // Rác thải hữu cơ
  NON_RECYCLABLE   // Rác thải không tái chế
};

int wastePercent[4] = {0};

WasteType currentWasteType;

bool detectFinish = 0;

void wifiConnect() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" Connected!");
}

void mqttConnect() {
  while (!mqttClient.connected()) {
    Serial.println("Attemping MQTT connection...");
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("connected");

      //***Subscribe all topic you need***
      mqttClient.subscribe("intelbin/enable");
      mqttClient.subscribe("intelbin/disable");
      mqttClient.subscribe("waste/recognition");
    }
    else {
      Serial.println("try again in 5 seconds");
      delay(5000);
    }
  }
}

void datapush()
{
  StaticJsonDocument<200> jsonDoc;
  jsonDoc["RTN"] = wastePercent[0]; // lượng rác thải nhựa trong thùng
  jsonDoc["RKL"] = wastePercent[1]; // lượng rác thải kim loại
  jsonDoc["RHC"] = wastePercent[2]; // lượng rác thải hữu cơ
  jsonDoc["RKTC"] = wastePercent[3]; // lượng rác thải không tái chế
  jsonDoc["battery"] = batteryPercent; //Số % pin còn lại
  if (status_bin)
    jsonDoc["message"] = "Processing";
  else
    jsonDoc["message"] = "Available"; 
  
  // Chuyển đổi JSON thành chuỗi
  String jsonString;
  serializeJson(jsonDoc, jsonString);

  // Gửi JSON thông qua MQTT
  mqttClient.publish("intelbin/data", jsonString.c_str());
}

//MQTT Receiver
void callback(char* topic, byte* message, unsigned int length) {
  Serial.println(topic);
  String strMsg;
  for (int i = 0; i < length; i++) {
    strMsg += (char)message[i];
  }
  Serial.println(strMsg);

  if (length > 5) // Trường hợp data từ Model AI
  {
    detectFinish = 1;
    
    if (strMsg == "Rac thai nhua")
      currentWasteType = PLASTIC_WASTE;
    else if (strMsg == "Rac kim loai")
      currentWasteType = METAL_WASTE;
    else if (strMsg == "Rac huu co")
      currentWasteType = ORGANIC_WASTE;
    else if (strMsg == "Rac khong tai che")
      currentWasteType = NON_RECYCLABLE;
  }
  else
  {
    if (strMsg == "0")
    {
      enable_button = 0;
    }
    else if (strMsg == "1")
    {
      enable_button = 1;
    }
  }
}


// Hàm quay step motor với góc độ được chỉ định (degree)
void rotateStepperMotor(int degree, int direction) {
  int steps = map(degree, 0, 360, 0, stepsPerRevolution);
  myStepper.step(steps*direction);

  //myStepper.step(steps*-direction);
}

// Hàm quay servo 90 độ trong 1 giây và quay về 0 độ
void rotateServo(int degree) {
  for (int pos = 0 ; pos <= degree;pos++){
    myServo.write(pos);
    delay(5);
  }
  delay(1000);
  for (int pos = degree ; pos >= 0;pos--){
    myServo.write(pos);
    delay(5);
  }
  delay(500);
}

// Hàm chuông báo với tần số 1000 Hz
void activateBuzzer() {
  tone(buzzerPin, 1000); // Buzzer kêu với tần số 1000 Hz
  delay(235);            // Buzzer kêu trong 500 ms
  noTone(buzzerPin);     // Tắt buzzer
}

long measureDistance(int trigPin, int echoPin) {
  long duration, distance;

  // Gửi tín hiệu từ chân trigPin
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  // Đo thời gian phản hồi tại chân echoPin
  duration = pulseIn(echoPin, HIGH);

  // Tính khoảng cách (tốc độ âm thanh trong không khí là 343 m/s)
  distance = duration * 0.034 / 2;

  return distance;
}

void readAmountWaste(){
  // 100cm là 0 % rác, 5cm là 100% lượng rác
  wastePercent[0] = constrain(map(measureDistance(trigPin1, echoPin1), 100, 5, 0,100), 0,100); 
  wastePercent[1] = constrain(map(measureDistance(trigPin2, echoPin2), 100, 5, 0,100), 0, 100);
  wastePercent[2] = constrain(map(measureDistance(trigPin4, echoPin4), 100, 5, 0,100), 0 ,100);
  wastePercent[3] = constrain(map(measureDistance(trigPin5, echoPin5), 100, 5, 0,100), 0, 100);
}

void lcdShow(int mode){
  switch (mode)
  {
    case 1:
      lcd.clear();
        // Hiển thị phần trăm lượng rác lên dòng đầu tiên và thứ hai của màn hình
        lcd.setCursor(0, 0); // Đặt con trỏ ở dòng đầu tiên
        lcd.print("RTN:");
        lcd.print(wastePercent[0]);
        lcd.print("%");
        lcd.print("RKL:");
        lcd.print(wastePercent[1]);
        lcd.print("%");

        lcd.setCursor(0, 1); // Đặt con trỏ ở dòng thứ hai
        lcd.print("RHC:");
        lcd.print(wastePercent[2]);
        lcd.print("% ");
        lcd.print("RKTC:");
        lcd.print(wastePercent[3]);
        lcd.print("%");
    break;

    case 2: 
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("IntelBin is");
      lcd.setCursor(0,1);
      lcd.print("now Available!!!");
    break;
    case 3:
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("IntelBin");
      lcd.setCursor(0,1);
      lcd.print("processing");
    break;
     case 4:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Rac thai nhua");
      break;

    case 5:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Rac kim loai");
      break;

    case 6:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Rac huu co");
      break;

    case 7:
      lcd.clear();
      lcd.setCursor(4, 0);
      lcd.print("Rac khong");
      lcd.setCursor(5,1);
      lcd.print("tai che");
      break;
    case 8:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Full trash!!!");
      break;
    case 9: 
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("IntelBin is");
      lcd.setCursor(0,1);
      lcd.print("Unavailable!!!");
      break;
    default:
      break;
  }
}

void ledOn(int mode){
  switch (mode)
  {
  case 1:
    digitalWrite(ledPin1, 0);
    digitalWrite(ledPin2, 1);
    digitalWrite(ledPin3, 0);
    break;
   case 2:
    digitalWrite(ledPin1, 0);
    digitalWrite(ledPin2, 0);
    digitalWrite(ledPin3, 1);
    break;
  case 3:
    digitalWrite(ledPin1, 1);
    digitalWrite(ledPin2, 0);
    digitalWrite(ledPin3, 0);
    break;
  default:
    break;
  }
}


void activeSortingProcess(){
    
    lcdShow(3);
    ledOn(2);

    while(!detectFinish){
        mqttClient.loop();
    } 

    WasteType result = currentWasteType;

    ledOn(3);

    // Sau khi detect thành công thì sẽ trả ra loại rác
    switch (result)
    {
      case PLASTIC_WASTE:
        lcdShow(4);
        rotateStepperMotor(45, 1);
      break;

      case METAL_WASTE:
        lcdShow(5);
        rotateStepperMotor(135, 1);
      break;

      case ORGANIC_WASTE:
        lcdShow(6);
        rotateStepperMotor(45, -1);
      break;

      case NON_RECYCLABLE:
        lcdShow(7);
        rotateStepperMotor(135, -1);
      break;
      default:
        break;
    }

    rotateServo(180);



    detectFinish = 0;
}

bool isFullTrash(){
  for (int i = 0; i< 4;++i){
    if (wastePercent[i] >  95){
      return 1;
    } 
  }
  return 0;
}



void setup() {
  Serial.begin(115200);

  wifiConnect();
  mqttClient.setServer(mqttServer, portMQTT);
  mqttClient.setCallback(callback);
  mqttClient.setKeepAlive( 90 );



  // Thiết lập LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Setup Complete");



  // Thiết lập các chân của ESP32
  pinMode(trigPin1, OUTPUT);
  pinMode(echoPin1, INPUT);

  pinMode(trigPin2, OUTPUT);
  pinMode(echoPin2, INPUT);

  pinMode(trigPin3, OUTPUT);
  pinMode(echoPin3, INPUT);

  pinMode(trigPin4, OUTPUT);
  pinMode(echoPin4, INPUT);

  pinMode(trigPin5, OUTPUT);
  pinMode(echoPin5, INPUT);

  pinMode(buzzerPin, OUTPUT);
  pinMode(ledPin1, OUTPUT);
  pinMode(ledPin2, OUTPUT);
  pinMode(ledPin3, OUTPUT);

  pinMode(pirPin, INPUT);
  pinMode(batteryPin, INPUT);

  // Thiết lập servo
  myServo.attach(4,500,2400); // Chân PWM của servo kết nối với chân 4 của ESP32
  
  // Thiết lập stepper motor
  myStepper.setSpeed(500); // Tốc độ stepper motor (có thể điều chỉnh)
  

  myServo.write(0);

}



void loop() {



  // Main flow
  if (!mqttClient.connected()) {
    mqttConnect();
  }
  mqttClient.loop();

  batteryPercent = constrain(map(analogRead(batteryPin), 0,4096, 0,100),0,100);
  Serial.println(batteryPercent);
  bool isInUse = digitalRead(pirPin);
  bool trashPutIn = measureDistance(trigPin3, echoPin3) < 5; // Nếu có rác được đưa vào máng phân loại
  bool fullTrash = isFullTrash();


  status_bin = trashPutIn;
  readAmountWaste();
  


  if (fullTrash){
    ledOn(3);
    tone(buzzerPin, 1000);
  }
  else{
    noTone(buzzerPin);
    ledOn(1);
  }

  // Màn hình hiển thị available và 
  if (!enable_button){
    lcdShow(8);
  }
  else{
    if (millis() - pastTime_lcd > intervalTime){
      if (mode_lcd == 2){
        lcdShow(2);
        mode_lcd = 1;
      }
      else{
        lcdShow(1);
        mode_lcd = 2;
      }

      pastTime_lcd = millis();
    }
  }

  if (!isInUse){   
    lcd.noBacklight();
  }
  else{
    lcd.backlight();
  }

  datapush();

  if (trashPutIn && enable_button && !fullTrash){
    lcdShow(3); // Processing
    ledOn(2); // Đèn cam
    activeSortingProcess();
  } 

}
