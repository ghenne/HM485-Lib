//*******************************************************************
//
// HMWHomebrew.cpp
//
// Homematic Wired Hombrew Hardware
// Arduino Uno als Homematic-Device
// HMW-1WIRE-TMP10
// Thorsten Pferdekaemper (thorsten@pferdekaemper.com)
// nach einer Vorlage von
// Dirk Hoffmann (hoffmann@vmd-jena.de)
//
//-------------------------------------------------------------------
//Hardwarebeschreibung:
// =====================
//
// Pinsettings for Arduino Uno
//
// D0: RXD, normaler Serieller Port fuer Debug-Zwecke
// D1: TXD, normaler Serieller Port fuer Debug-Zwecke
// D2: RXD, RO des RS485-Treiber
// D3: TXD, DI des RS485-Treiber
// D4: Direction (DE/-RE) Driver/Receiver Enable vom RS485-Treiber
//
// D5: Taster 1
// D10: 1Wire-Data
// D13: Status-LED

// Die Firmware funktioniert mit dem Standard-Uno Bootloader, im
// Gegensatz zur Homematic-Firmware

//*******************************************************************

// Die "DEBUG_UNO"-Version ist fuer einen normalen Arduino Uno (oder so)
// gedacht. Dadurch wird RS485 ueber SoftwareSerial angesteuert
// und der Hardware-Serial-Port wird fuer Debug-Ausgaben benutzt
// Dadurch kann man die normale USB-Verbindung zum Programmieren und
// debuggen benutzen

#define DEBUG_NONE 0   // Kein Debug-Ausgang, RS485 auf Hardware-Serial
#define DEBUG_UNO 1    // Hardware-Serial ist Debug-Ausgang, RS485 per Soft auf pins 5/6
#define DEBUG_UNIV 2   // Hardware-Serial ist RS485, Debug per Soft auf pins 5/6

#define DEBUG_VERSION DEBUG_NONE

// Do not remove the include below
#include "HBW-1W-T10.h"

#if DEBUG_VERSION == DEBUG_UNO || DEBUG_VERSION == DEBUG_UNIV
#include <SoftwareSerial.h>
#endif

// debug-Funktion
#include "HMWDebug.h"

// OneWire
#include <OneWire.h>

// EEPROM
#include <EEPROM.h>

// HM Wired Protokoll
#include "HMWRS485.h"
// default module methods
#include "HMWModule.h"

// Ein paar defines...
#if DEBUG_VERSION == DEBUG_UNO
  #define RS485_RXD 5
  #define RS485_TXD 6
  #define LED 13        // Signal-LED
#elif DEBUG_VERSION == DEBUG_UNIV
  #define DEBUG_RXD 5
  #define DEBUG_TXD 6
  #define LED 4
#else
  #define LED 4         // Signal-LED
#endif

#define BUTTON 8            // Das Bedienelement
#define RS485_TXEN 2        // Transmit-Enable

#define ONEWIRE_PIN A3

#define MAX_SENSORS 10   // maximum number of 1-Wire Sensors
#define DEFAULT_TEMP -273.15   // for unused channels
#define ERROR_TEMP -273.14     // CRC error

// config of one sensor
struct sensor_config {
  byte :8;                          // dummy
  byte send_delta_temp;             // Temperaturdifferenz, ab der gesendet wird
  byte :8;
  uint16_t send_min_interval;   // Minimum-Sendeintervall
  uint16_t send_max_interval;   // Maximum-Sendeintervall
  byte address[8];                  // 1-Wire-Adresse
  byte :8;
};

// TODO: Check whether we have enough memory to do the following
sensor_config sensors[MAX_SENSORS];

// currently measured temperature
int16_t currentTemp[MAX_SENSORS];         // Temperatures in �C * 100
// TODO: If we want to work with peering, then the config etc. probably needs to be set by peer
// temperature measured on last send
int16_t lastSentTemp[MAX_SENSORS];
// time of last send
int32_t lastSentTime[MAX_SENSORS];


// OneWire
OneWire myWire(ONEWIRE_PIN);


#if DEBUG_VERSION == DEBUG_UNO
SoftwareSerial rs485(RS485_RXD, RS485_TXD); // RX, TX
HMWRS485 hmwrs485(&rs485, RS485_TXEN);
#elif DEBUG_VERSION == DEBUG_UNIV
HMWRS485 hmwrs485(&Serial, RS485_TXEN);
#else
HMWRS485 hmwrs485(&Serial, RS485_TXEN);  // keine Debug-Ausgaben
#endif
HMWModule* hmwmodule;   // wird in setup initialisiert


// write config to EEPROM in a hopefully smart way
void writeConfig(){
    byte* ptr;
	// EEPROM lesen und schreiben
	ptr = (byte*)(sensors);
	for(int16_t address = 0; address < sizeof(sensors[0]) * MAX_SENSORS; address++){
	  hmwmodule->writeEEPROM(address + 0x10, *ptr);
	  ptr++;
    };
};


// Sensor Adressen lesen
// Before calling this function, config must have been read
// It only searches for new sensors if there is a free channel
void sensorAddressesGet() {

  byte addr[8];
  byte channel;

  // search for addresses on the bus
  myWire.reset_search();

  while(1) {
    // search free slot
	for(channel = 0; channel < MAX_SENSORS && sensors[channel].address[0] != 0xFF; channel++);
    if(channel == MAX_SENSORS) break;   // no free slot found
    // now channel points to a free slot
	if(!myWire.search(addr)) break;     // no further sensor found
	hmwdebug(F("\r\n 1-Wire Device found:"));
	for( byte i = 0; i < 8; i++) {
		hmwdebug(" ");
		hmwdebug(addr[i], HEX);
	};
	if(OneWire::crc8(addr, 7) != addr[7]) {
		hmwdebug(F("CRC is not valid - ignoring device!"));
	  continue;
    };
	// now we found a valid device, check if this is already known
	byte oldChan;
    for(oldChan = 0; oldChan < MAX_SENSORS; oldChan++) {
       if(memcmp(addr, sensors[oldChan].address, 8) == 0) break;   // found
    };
    if(oldChan < MAX_SENSORS){
    	hmwdebug(F("Device already known"));
  	  continue;
    };
    // we have a new device!
    memcpy(sensors[channel].address, addr, 8);
  };
  // now the devices are in sensors[], write them to the EEPROM
  writeConfig();
};


void setDefaults(){
  // defaults setzen
  for(byte channel = 0; channel < MAX_SENSORS; channel++){
    if(sensors[channel].send_delta_temp == 0xFF) sensors[channel].send_delta_temp = 5;
    if(sensors[channel].send_min_interval == 0xFFFF) sensors[channel].send_min_interval = 10;
    if(sensors[channel].send_max_interval == 0xFFFF) sensors[channel].send_max_interval = 150;
  };
};


// After writing the central Address to the EEPROM, the CCU does
// not trigger a re-read config
uint32_t centralAddressGet() {
  uint32_t result = 0;
  for(int16_t address = 2; address < 6; address++){
	result <<= 8;
	result |= EEPROM.read(address);
  };
  return result;
};


// Klasse fuer Callbacks vom Protokoll
class HMWDevice : public HMWDeviceBase {
  public:
	void setLevel(byte channel,uint16_t level) {
      return;  // there is nothing to set
	}

	uint16_t getLevel(byte channel, byte command) {
      // there is only one channel for now
	  int16_t defTemp = (int16_t)(DEFAULT_TEMP * 100);
	  if(channel > MAX_SENSORS) return (uint16_t)(defTemp);
	  return currentTemp[channel];
	};

	void readConfig(){
      byte* ptr;
	  // EEPROM lesen
      // sensor config
	  ptr = (byte*)(sensors);
	  for(int16_t address = 0; address < sizeof(sensors[0]) * MAX_SENSORS; address++){
	    *ptr = EEPROM.read(address + 0x10);
	    ptr++;
	  };
	  // defaults setzen
	  setDefaults();
	};
};

// The device will be created in setup()
HMWDevice hmwdevice;


// send "start conversion" to device
void oneWireStartConversion(byte channel) {
  // ignore channels without sensor
  if(sensors[channel].address[0] == 0xFF)
	return;
  myWire.reset();
  myWire.select(sensors[channel].address);
  myWire.write(0x44, 1);        // start conversion, with parasite power on at the end
};


float oneWireReadTemp(byte channel) {
   // ignore channels without sensor
   if(sensors[channel].address[0] == 0xFF)
	   return (float)DEFAULT_TEMP;

	byte data[12];

	  // present = ds.reset();   TODO: what exactly does the "present" do we need it?
	  myWire.reset();
	  myWire.select(sensors[channel].address);
	  myWire.write(0xBE);         // Read Scratchpad

	  for ( byte i = 0; i < 9; i++) {           // we need 9 bytes
	    data[i] = myWire.read();
	  }
	  // CRC check  TODO: if this happens only once, maybe don't do it
	  if(OneWire::crc8(data, 8) != data[8])
		  return (float)ERROR_TEMP;

	  // Convert the data to actual temperature
	  // because the result is a 16 bit signed integer, it should
	  // be stored to an "int16_t" type, which is always 16 bits
	  // even when compiled on a 32 bit processor.
	  int16_t raw = (data[1] << 8) | data[0];
	  if (sensors[channel].address[0] == 0x10) {
	    raw = raw << 3; // 9 bit resolution default
	    if (data[7] == 0x10) {   // DS18S20 or old DS1820
	      // "count remain" gives full 12 bit resolution
	      raw = (raw & 0xFFF0) + 12 - data[6];
	    }
	  } else {
	    byte cfg = (data[4] & 0x60);
	    // at lower res, the low bits are undefined, so let's zero them
	    if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
	    else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
	    else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
	    // default is 12 bit resolution, 750 ms conversion time
	  }
	  return (float)(raw / 16.0);
};


// handle 1-Wire temperature measurement
// measurement every 10 seconds, one measurement needs about 1 second
// we have up to 10 sensors, so give every one 1 second
void handleOneWire() {

  static byte currentChannel = 255;  // we should never have 255 channels
  static int32_t lastTime = 0;
  int32_t now = millis();

  // once every 1 seconds
  if(now -lastTime < 1000) return;
  lastTime = now;

  if(currentChannel == 255){  // special for the first call
	currentChannel = 0;
  }else{
	// read temperature
 	currentTemp[currentChannel] = (int16_t)(oneWireReadTemp(currentChannel) * 100);
 	currentChannel = (currentChannel + 1) % MAX_SENSORS;
  };
  // start next measurement
  oneWireStartConversion(currentChannel);
  // myWire.depower() is not needed here as we poll the bus all the time
};


void factoryReset() {
  // writes FF into config
  memset(sensors, 0xFF, sizeof(sensors[0]) * MAX_SENSORS);
  // central address
  for(int16_t address = 2; address < 6; address++)
    EEPROM.write(address, 0xFF);
  // set defaults
  setDefaults();
  // nach neuen Sensoren suchen
  // this will at the end write to EEPROM
  sensorAddressesGet();
}


void handleButton() {
  // langer Tastendruck (5s) -> LED blinkt hektisch
  // dann innerhalb 10s langer Tastendruck (3s) -> LED geht aus, EEPROM-Reset

  static int32_t lastTime = 0;
  static byte status = 0;  // 0: normal, 1: Taste erstes mal gedr�ckt, 2: erster langer Druck erkannt
                           // 3: Warte auf zweiten Tastendruck, 4: Taste zweites Mal gedr�ckt
                           // 5: zweiter langer Druck erkannt

  int32_t now = millis();
  boolean buttonState = !digitalRead(BUTTON);

  switch(status) {
    case 0:
      if(buttonState) status = 1;
      lastTime = now;
      break;
    case 1:
      if(buttonState) {   // immer noch gedrueckt
        if(now - lastTime > 5000) status = 2;
      }else{              // nicht mehr gedr�ckt
    	if(now - lastTime > 100)   // determine sensors and send announce on short press
    		sensorAddressesGet();
    		hmwmodule->broadcastAnnounce(0);
        status = 0;
      };
      break;
    case 2:
      if(!buttonState) {  // losgelassen
    	status = 3;
    	lastTime = now;
      };
      break;
    case 3:
      // wait at least 100ms
      if(now - lastTime < 100)
    	break;
      if(buttonState) {   // zweiter Tastendruck
    	status = 4;
    	lastTime = now;
      }else{              // noch nicht gedrueckt
    	if(now - lastTime > 10000) status = 0;    // give up
      };
      break;
    case 4:
      if(now - lastTime < 100) // entprellen
          	break;
      if(buttonState) {   // immer noch gedrueckt
        if(now - lastTime > 3000) status = 5;
      }else{              // nicht mehr gedr�ckt
        status = 0;
      };
      break;
    case 5:   // zweiter Druck erkannt
      if(!buttonState) {    //erst wenn losgelassen
    	// Factory-Reset          !!!!!!  TODO: Gehoert das ins Modul?
    	factoryReset();
    	status = 0;
      }
      break;
  }

  // control LED
  static int32_t lastLEDtime = 0;
  switch(status) {
    case 0:
      digitalWrite(LED, LOW);
      break;
    case 1:
      digitalWrite(LED, HIGH);
      break;
    case 2:
    case 3:
    case 4:
      if(now - lastLEDtime > 100) {  // schnelles Blinken
    	digitalWrite(LED,!digitalRead(LED));
    	lastLEDtime = now;
      };
      break;
    case 5:
      if(now - lastLEDtime > 750) {  // langsames Blinken
       	digitalWrite(LED,!digitalRead(LED));
       	lastLEDtime = now;
      };
  }
};

#if DEBUG_VERSION != DEBUG_NONE
void printChannelConf(){
  for(byte channel = 0; channel < MAX_SENSORS; channel++) {
	  hmwdebug("Channel     : "); hmwdebug(channel); hmwdebug("\r\n");
	  hmwdebug("Min Interval: "); hmwdebug(sensors[channel].send_min_interval); hmwdebug("\r\n");
	  hmwdebug("Max Interval: "); hmwdebug(sensors[channel].send_max_interval); hmwdebug("\r\n");
	  hmwdebug("Delta Temp  : "); hmwdebug(sensors[channel].send_delta_temp); hmwdebug("\r\n");
	  hmwdebug("Current Temp: "); hmwdebug(currentTemp[channel]); hmwdebug("\r\n");
	  hmwdebug("Last Temp   : "); hmwdebug(lastSentTemp[channel]); hmwdebug("\r\n");
	  hmwdebug("Size        : "); hmwdebug(sizeof(sensors[channel])); hmwdebug("\r\n");
	  hmwdebug("\r\n");
  }
}
#endif


void setup()
{
#if DEBUG_VERSION == DEBUG_UNO
	pinMode(RS485_RXD, INPUT);
	pinMode(RS485_TXD, OUTPUT);
#elif DEBUG_VERSION == DEBUG_UNIV
	pinMode(DEBUG_RXD, INPUT);
	pinMode(DEBUG_TXD, OUTPUT);
#endif
	pinMode(RS485_TXEN, OUTPUT);
	digitalWrite(RS485_TXEN, LOW);

	pinMode(BUTTON, INPUT_PULLUP);
	pinMode(LED, OUTPUT);

#if DEBUG_VERSION == DEBUG_UNO
   hmwdebugstream = &Serial;
   Serial.begin(19200);
   rs485.begin(19200);    // RS485 via SoftwareSerial
#elif DEBUG_VERSION == DEBUG_UNIV
   SoftwareSerial* debugSerial = new SoftwareSerial(DEBUG_RXD, DEBUG_TXD);
   debugSerial->begin(19200);
   hmwdebugstream = debugSerial;
   Serial.begin(19200, SERIAL_8E1);
#else
   Serial.begin(19200, SERIAL_8E1);
#endif

   // Default temperature is -273.15 (currently...)
   for(byte i = 0; i < MAX_SENSORS; i++) {
	  currentTemp[i] = (int16_t)(DEFAULT_TEMP * 100);
	  lastSentTemp[i] = (int16_t)(DEFAULT_TEMP * 100);
      lastSentTime[i] = 0;
   };

   // config aus EEPROM lesen
   hmwdevice.readConfig();
   // nach neuen Sensoren suchen
   sensorAddressesGet();

	// device type: 0x81
   // TODO: Modultyp irgendwo als define
 	hmwmodule = new HMWModule(&hmwdevice, &hmwrs485, 0x81);

    hmwdebug("Huhu\n");

#if DEBUG_VERSION != DEBUG_NONE
	printChannelConf();
#endif
}


// broadcast announce message once at the beginning
// this might need to "wait" until the bus is free
void handleBroadcastAnnounce() {
  static boolean announced = false;
  // avoid sending broadcast in the first second
  // we don't care for the overflow as the announce
  // is sent after 40 days anyway (hopefully)
  if(millis() < 1000) return;
  if(announced) return;
  // send methods return 0 if everything is ok
  announced = (hmwmodule->broadcastAnnounce(0) == 0);
}


// The loop function is called in an endless loop
void loop()
{
// Daten empfangen und alles, was zur Kommunikationsschicht geh�rt
// processEvent vom Modul wird als Callback aufgerufen
   hmwrs485.loop();

// send announce message, if not done yet
   handleBroadcastAnnounce();

 // Temperatur lesen
   handleOneWire();

 // Bedienung ueber Button
   handleButton();

 // Pruefen, ob wir irgendwas senden muessen
   int32_t now = millis();
   for(byte channel = 0; channel < MAX_SENSORS; channel++) {
	 // do not send before min interval
	 if(sensors[channel].send_min_interval && now - lastSentTime[channel] < (int32_t)(sensors[channel].send_min_interval) * 1000)
		  continue;
     if(    (sensors[channel].send_max_interval && now - lastSentTime[channel] >= (int32_t)(sensors[channel].send_max_interval) * 1000)
    	 || (sensors[channel].send_delta_temp
    	         && abs( currentTemp[channel] - lastSentTemp[channel] ) >= (uint16_t)(sensors[channel].send_delta_temp) * 10)) {
         // if bus is busy, we might retry
    	 if(hmwmodule->sendInfoMessage(channel,currentTemp[channel], centralAddressGet()) != 1) {
             lastSentTemp[channel] = currentTemp[channel];
             lastSentTime[channel] = now;
    	 };
     };
   };
};






