
/************************************************************************
  This is an access control project using a NodeMCU and CR522 RFID reader.
  It uses "Blynk" as an IOT for exchanging information with NodeMCU
  and controlling access through smart phone or PC
  Creator: JOHN BACAS  (john.bacas@gmail.com)
 ***********************************************************************/

/* wiring the MFRC522 to ESP8266 (ESP-12)
RST     = GPIO0
SDA(SS) = GPIO2 
MOSI    = GPIO13
MISO    = GPIO12
SCK     = GPIO14
GND     = GND
3.3V    = 3.3V
*/
 // BLYNK.console Home - FirmWare Configuration
#define BLYNK_TEMPLATE_ID ""
#define BLYNK_TEMPLATE_NAME ""
#define BLYNK_AUTH_TOKEN ""

/* Comment this out to disable prints and save space */
#define BLYNK_PRINT Serial

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <TimeLib.h>
#include <WidgetRTC.h>
#include <EEPROM.h> 
#include <SPI.h>
#include "MFRC522.h"

#define LED_ON LOW
#define LED_OFF HIGH
//#define redLed 16
#define greenLed 5
#define blueLed 4
#define relay 16
#define buzzer 0

boolean match = false; 
boolean programMode = false; 
int successRead; 

byte storedCard[4];  
byte readCard[4];
byte masterCard[4];

String tag = "";

#define RST_PIN	2  // RST-PIN for RC522 - RFID - SPI - GPIO2 - D4
#define SS_PIN	15  // SDA-PIN for RC522 - RFID - SPI - GPIO15 - D8

String currentTime;
String currentDate;

const char *ssid =	"YOUR NETWORK SSID";	    // change according to your Network
const char *pass =	"YOUR NETWORK PASSWORD";	// change according to your Network


MFRC522 mfrc522(SS_PIN, RST_PIN);	// Create MFRC522 instance
BlynkTimer timer;
WidgetRTC rtc;
WidgetTerminal terminal(V0);

int ledState = LOW;
long slowFlashing = 1200;
long fastFlashing = 150;
unsigned long previousMillis = 0;
//const long interval = 1200; // blue led flashing interval

BLYNK_CONNECTED() {
  // Synchronize time on connection
  rtc.begin();
}


void setup() {       //*********************** SETUP ***************************
  pinMode(greenLed, OUTPUT);
  pinMode(blueLed, OUTPUT);
  pinMode(relay, OUTPUT);
  pinMode(buzzer, OUTPUT);
  
  digitalWrite(relay, HIGH);
  digitalWrite(greenLed, LED_OFF);
  digitalWrite(blueLed, LED_OFF);
  
  Serial.begin(9600);    // Initialize serial communications
  SPI.begin();	         // Init SPI bus
  mfrc522.PCD_Init();    // Init MFRC522
  mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max);
  
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  setSyncInterval(10 * 60); // Sync interval in seconds (10 minutes)
  delay(200);
  
  EEPROM.begin(512);
  byte check;
  EEPROM.get(1,check);
  if (check != 143) { 
    Serial.println("No Master Card Defined");
    Serial.println("Scan A PICC to Define as Master Card");
    do {
      successRead = getID();
      ledFlashing(fastFlashing);  // program mode: blue led fast flashing
    } while (!successRead);
    
    for ( int j = 0; j < 4; j++ ) { 
      EEPROM.put( 2 + j, readCard[j] ); 
    }
    EEPROM.put(1, byte(143));
    EEPROM.commit();
    Serial.println("Master Card Defined");
  }
  
  Serial.print("Master Card's UID: ");
  for ( int i = 0; i < 4; i++ ) {     
    EEPROM.get(2 + i, masterCard[i]); 
    Serial.print(masterCard[i], HEX);
  }
  
  Serial.println("");
  readEEPROM();
  Serial.println("");
  //Blynk.run();
  //timer.run();
  Serial.println("Waiting PICCs to be scanned :)");
  ledFlashing(slowFlashing);  // normal mode: blue led slow flashing
}


void loop() {        //************************ LOOP *********************************
  
  do {
    Blynk.run();
    timer.run();
    successRead = getID(); 
    if (programMode) {
      ledFlashing(fastFlashing);  // program mode: blue led fast flashing
    }
    else {
      ledFlashing(slowFlashing); // normal mode: blue led slow flashing
    }
  }
  while (!successRead);
 
  if (programMode) {
    if ( isMaster(readCard) ) { 
      Serial.println("This is Master Card");
      Serial.println("Exiting Program Mode");
      Serial.println("-----------------------------");
      Blynk.virtualWrite(V0, " Master Card: Exit Program Mode ");
      programMode = false;
      return;
    }
    else {
      if ( findID(readCard) ) { 
        Serial.println("I know this PICC, so removing");
        deleteID(readCard);
        Serial.println("-----------------------------");
        Blynk.virtualWrite(V0, " Tag removed ");
      }
      else { 
        Serial.println("I do not know this PICC, adding...");
        writeID(readCard);
        Serial.println("-----------------------------");
        Blynk.virtualWrite(V0, " Tag added ");
      }
    }
  }
  else {
    if ( isMaster(readCard) ) {  
      programMode = true;
      Serial.println("Hello Master - Entered Program Mode");
      Blynk.virtualWrite(V0, " Master Card: Program Mode Entered ");
      byte count;
      EEPROM.get(0, count); 
      Serial.print("I have ");  
      Serial.print(count);
      Serial.print(" record(s) on EEPROM");
      Serial.println("");
      readEEPROM();
      Serial.println("Scan a PICC to ADD or REMOVE");
      Serial.println("-----------------------------");
    }
    else {
      if ( findID(readCard) ) { 
        Serial.println("Welcome, You shall pass");
        Blynk.virtualWrite(V0, " Access Granted ");
        Serial.println("----------------------------");
        Blynk.logEvent("entrancelog", String("Tag ID: ") + tag + String("  Access Granted"));
        //digitalWrite(greenLed, LED_ON);
        digitalWrite(relay, LOW);
        delay(1200);
        //digitalWrite(greenLed, LED_OFF);
        digitalWrite(relay, HIGH);
      }
      else {     
        Serial.println("You shall not pass");
        Blynk.virtualWrite(V0, " Access Denied ");
        Serial.println("----------------------------");
        Blynk.logEvent("entrancelog", String("Tag ID: ") + tag + String("  Access Denied"));
        //digitalWrite(greenLed, LED_ON);
        digitalWrite(buzzer, HIGH);
        delay(1200);
        //digitalWrite(greenLed, LED_OFF);
        digitalWrite(buzzer, LOW);
      }
    }
  }
}

// ********************************** FUNCTIONS ************************************
int getID() {
  if ( ! mfrc522.PICC_IsNewCardPresent()) {
    delay(70);
    return 0;
  }
  if ( ! mfrc522.PICC_ReadCardSerial()) {
    delay(70);
    return 0;
  }
  int minutes = minute();
  int seconds = second();
  currentTime = String(hour()) + ":";
  currentTime += (minutes<10)? "0" + String(minutes) + ":" : String(minutes) + ":";
  currentTime += (seconds<10)? "0" + String(seconds) + " , " : String(seconds)+ " , ";
  currentDate = String(day()) + "-" + month() + "-" + year() + " , ";  
  tag="";
  Serial.print("Scanned PICC's UID: ");
  for (int i = 0; i < 4; i++) { 
    readCard[i] = mfrc522.uid.uidByte[i];
    Serial.print(readCard[i], HEX);
    tag += String(readCard[i], HEX);    
  }
  Blynk.virtualWrite(V0, "\n" + currentDate + currentTime + " TagID: ");
  Blynk.virtualWrite(V0, tag);
  //Blynk.logEvent("entrancelog", String("Tag ID: ") + tag);
  Serial.println("");
  mfrc522.PICC_HaltA();
  digitalWrite(blueLed, LED_OFF);
  digitalWrite(greenLed, LED_ON);
  delay(500);
  digitalWrite(greenLed, LED_OFF);
  return 1;
}


void ledFlashing (long interval) {
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
  previousMillis = currentMillis;
    if (ledState == LOW) {
      ledState = HIGH;
    } else {
      ledState = LOW;
    }
  digitalWrite(blueLed, ledState);
  } 
}


void readID( int number ) {
  int start = (number * 4 ) + 2; 
  for ( int i = 0; i < 4; i++ ) { 
    EEPROM.get(start + i, storedCard[i]); 
  }
}


void writeID( byte a[] ) {
  if ( !findID( a ) ) {
    byte num;
    EEPROM.get(0, num); 
    int start = ( num * 4 ) + 6;
    num++;
    EEPROM.put( 0, num );
    for ( int j = 0; j < 4; j++ ) {
      EEPROM.put( start + j, a[j] );
    }
    EEPROM.commit();
    successWrite();
  }
  else {
    failedWrite();
  }
}


void deleteID( byte a[] ) {
  if ( !findID( a ) ) { 
    failedWrite();
  }
  else {
    byte num;
    EEPROM.get(0, num);
    //int slot;
    //int start;
    //int looping;
    int j=0;
    int slot = findIDSLOT( a );
    int starting = (slot * 4) + 2;
    int looping = (num - slot) * 4;
     
    //char buf[40];
    //sprintf(buf, "Slot: %d, starting: %d, looping: %d", slot, starting, looping);
    //Serial.println(buf);
  
    num--;
    EEPROM.put( 0, num );
    for ( j = 0; j < looping; j++ ) {
      byte record;
      EEPROM.get(starting + 4 + j, record);
      EEPROM.put(starting + j, record);
    }
    for ( int k = 0; k < 4; k++ ) {
      EEPROM.put( starting + j + k, byte(0));
    }
    EEPROM.commit();
    successDelete();
  }
}


boolean checkTwo ( byte a[], byte b[] ) {
  if ( a[0] != NULL )
    match = true;
  for ( int k = 0; k < 4; k++ ) { 
    if ( a[k] != b[k] )
      match = false;
  }
  if ( match ) {
    return true; 
  }
  else  {
    return false; 
  }
}


int findIDSLOT( byte find[] ) {
  byte count;
  EEPROM.get(0, count); 
  for ( int i = 1; i <= count; i++ ) {
    readID(i); 
    if ( checkTwo( find, storedCard ) ) {    
      return i;
      break;
    }
  }
  return int(count+1);
}


boolean findID( byte find[] ) {
  byte count;
  EEPROM.get(0,count);
  for ( int i = 1; i <= count; i++ ) {
    readID(i);
    if ( checkTwo( find, storedCard ) ) {
      return true;
      break;
    }
//    else {
//    }
  }
  return false;
}

void successWrite() {  // green led flases 3 times
  digitalWrite(blueLed, LED_OFF);
  digitalWrite(greenLed, LED_OFF);
  delay(100);
  digitalWrite(greenLed, LED_ON);
  delay(100);
  digitalWrite(greenLed, LED_OFF);
  delay(100);
  digitalWrite(greenLed, LED_ON);
  delay(100);
  digitalWrite(greenLed, LED_OFF);
  delay(100);
  digitalWrite(greenLed, LED_ON);
  delay(100);
  digitalWrite(greenLed, LED_OFF);
  Serial.println("Succesfully added ID record to EEPROM");
}

void failedWrite() {  // blue led flashes 3 times
  digitalWrite(blueLed, LED_OFF);
  digitalWrite(greenLed, LED_OFF);
  delay(100);
  digitalWrite(blueLed, LED_ON);
  digitalWrite(buzzer, HIGH);
  delay(100);
  digitalWrite(blueLed, LED_OFF);
  digitalWrite(buzzer, LOW);
  delay(100);
  digitalWrite(blueLed, LED_ON);
  digitalWrite(buzzer, HIGH);
  delay(100);
  digitalWrite(blueLed, LED_OFF);
  digitalWrite(buzzer, LOW);
  delay(100);
  digitalWrite(blueLed, LED_ON);
  digitalWrite(buzzer, HIGH);
  delay(100);
  digitalWrite(blueLed, LED_OFF);
  digitalWrite(buzzer, LOW);
  Serial.println("Failed! There is something wrong with ID or bad EEPROM");
}

void successDelete() {  // blue and green leds flash 3 times
  digitalWrite(blueLed, LED_OFF);
  digitalWrite(greenLed, LED_OFF);
  delay(100);
  digitalWrite(blueLed, LED_ON);
  digitalWrite(greenLed, LED_ON);
  delay(100);
  digitalWrite(blueLed, LED_OFF);
  digitalWrite(greenLed, LED_OFF);
  delay(100);
  digitalWrite(blueLed, LED_ON);
  digitalWrite(greenLed, LED_ON);
  delay(100);
  digitalWrite(blueLed, LED_OFF);
  digitalWrite(greenLed, LED_OFF);
  delay(100);
  digitalWrite(blueLed, LED_ON);
  digitalWrite(greenLed, LED_ON);
  delay(100);
  digitalWrite(blueLed, LED_OFF);
  digitalWrite(greenLed, LED_OFF);
  Serial.println("Succesfully removed ID record from EEPROM");
}

boolean isMaster( byte test[] ) {
  if ( checkTwo( test, masterCard ) )
    return true;
  else
    return false;
}


void readEEPROM(){
  Serial.print("Records:");
  byte num = EEPROM.read(0);
  for (int i = 6; i < num * 4 + 6; i++){
    if (i % 4 == 2){      
      Serial.println("");
      Serial.print(String((i-2) / 4)+ ") ");
    }
    byte data = EEPROM.read(i);
    Serial.print(data, HEX);
    Serial.print(" ");
  }
  Serial.println("");
}
