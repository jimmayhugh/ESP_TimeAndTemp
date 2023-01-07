/*
 * Simple NTP client
 * https://www.mischianti.org/
 *
 * The MIT License (MIT)
 * written by Renzo Mischianti <www.mischianti.org>
 */
 
const char *ssid     = "SSID";
const char *password = "PASSWORD";
 
#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <time.h>
#include <Timezone.h>    // https://github.com/JChristensen/Timezone
#include <Wire.h> // Enable this line if using Arduino Uno, Mega, etc.
#include <Adafruit_GFX.h>
#include "Adafruit_LEDBackpack.h"
#include <OneWire.h>

Adafruit_7segment ledClock = Adafruit_7segment();
Adafruit_7segment ledTemp = Adafruit_7segment();
OneWire  ds(D5);  // on pin 10 (a 4.7K resistor is necessary)


/**
 * Input time in epoch format and return tm time format
 * by Renzo Mischianti <www.mischianti.org> 
 */
static tm getDateTimeByParams(long time){
    struct tm *newtime;
    const time_t tim = time;
    newtime = localtime(&tim);
    return *newtime;
}
/**
 * Input tm time format and return String with format pattern
 * by Renzo Mischianti <www.mischianti.org>
 */
static String getDateTimeStringByParams(tm *newtime, char* pattern = (char *)"%d/%m/%Y %H:%M:%S"){
    char buffer[30];
    strftime(buffer, 30, pattern, newtime);
    return buffer;
}
 
static String getEpochStringByParams(long time, char* pattern = (char *)"%I%M"){
//    struct tm *newtime;
    tm newtime;
    newtime = getDateTimeByParams(time);
    return getDateTimeStringByParams(&newtime, pattern);
}


WiFiUDP ntpUDP;
 
// By default 'pool.ntp.org' is used with 60 seconds update interval and
// no offset
//NTPClient timeClient(ntpUDP);
 
// You can specify the time server pool and the offset, (in seconds)
// additionaly you can specify the update interval (in milliseconds).
int GTMOffset = 0; // SET TO UTC TIME
NTPClient timeClient(ntpUDP, "time.nist.gov", GTMOffset*60*60, 15*60*1000);
 
// US Pacific Time Zone (Las Vegas, Los Angeles)
TimeChangeRule usPDT = {"PDT", Second, Sun, Mar, 2, -420};
TimeChangeRule usPST = {"PST", First, Sun, Nov, 2, -480};
Timezone usPT(usPDT, usPST);

// OneWire Stuff
  byte i;
  byte present = 0;
  byte type_s;
  byte data[9];
  byte addr[8];
  float celsius, fahrenheit;

void findDS(void)
{
  if ( !ds.search(addr)) {
    Serial.println("No more addresses.");
    Serial.println();
    ds.reset_search();
    delay(250);
    return;
  }
  
  Serial.print("ROM =");
  for( i = 0; i < 8; i++) {
    Serial.write(' ');
    Serial.print(addr[i], HEX);
  }

  if (OneWire::crc8(addr, 7) != addr[7]) {
      Serial.println("CRC is not valid!");
      return;
  }
  Serial.println();

}

void readDS (void)
{

  ds.reset();
  ds.select(addr);
  ds.write(0x44, 1);        // start conversion, with parasite power on at the end
  
  delay(1000);     // maybe 750ms is enough, maybe not
  // we might do a ds.depower() here, but the reset will take care of it.
  
  present = ds.reset();
  ds.select(addr);    
  ds.write(0xBE);         // Read Scratchpad

  /*
  Serial.print("  Data = ");
  Serial.print(present, HEX);
  Serial.print(" ");
  */ // uncomment for testing
  for ( i = 0; i < 9; i++) {           // we need 9 bytes
    data[i] = ds.read();
    //Serial.print(data[i], HEX); // uncomment for testing
    //Serial.print(" ");          // uncomment for testing
  }
  /*
  Serial.print(" CRC=");
  Serial.print(OneWire::crc8(data, 8), HEX);
  Serial.println();
  */ // uncomment for testing
  
  // Convert the data to actual temperature
  // because the result is a 16 bit signed integer, it should
  // be stored to an "int16_t" type, which is always 16 bits
  // even when compiled on a 32 bit processor.
  int16_t raw = (data[1] << 8) | data[0];
  if (type_s) {
    raw = raw << 3; // 9 bit resolution default
    if (data[7] == 0x10) {
      // "count remain" gives full 12 bit resolution
      raw = (raw & 0xFFF0) + 12 - data[6];
    }
  } else {
    byte cfg = (data[4] & 0x60);
    // at lower res, the low bits are undefined, so let's zero them
    if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
    else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
    else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
    //// default is 12 bit resolution, 750 ms conversion time
  }
  celsius = (float)raw / 16.0;
  fahrenheit = celsius * 1.8 + 32.0;
  /*
  Serial.print("  Temperature = ");
  Serial.print(celsius);
  Serial.print(" Celsius, ");
  Serial.print(fahrenheit);
  Serial.println(" Fahrenheit");
  */ // uncomment for testing
}

void ntpUpdate(void)
{
  if (timeClient.update()){
     Serial.println ( "Adjust local clock" );
     unsigned long epoch = timeClient.getEpochTime();
     setTime(epoch);
  }else{
     Serial.println ( "NTP not updated" );
  }
}

void setup()
{
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  ledClock.begin(0x70);
  ledTemp.begin(0x71);
  
  findDS();
 
  while ( WiFi.status() != WL_CONNECTED )
  {
    delay ( 500 );
    Serial.print ( "." );
  }
  Serial.println();
  timeClient.begin();
  delay ( 1000 );
  ntpUpdate();
}

int loopCnt = 0;

void loop()
{
  // I print the time from local clock but first I check DST 
  // to add hours if needed
  char* ltime_array = new char[5];
  String ltime=getEpochStringByParams(usPT.toLocal(now()));
  // Serial.print("String ltime = "); // uncomment for testing
  // Serial.println(ltime);           // uncomment for testing
  strcpy(ltime_array, ltime.c_str());
  // Serial.print("ltime_array = ");  // uncomment for testing
  // Serial.println(ltime_array);     // uncomment for testing
  ltime_array[4] = 0;
  int timeint = atoi(ltime_array);
  // Serial.print("timeint = ");      // uncomment for testing
  // Serial.println(timeint);         // uncomment for testing
    // print a string message
  ledClock.print(timeint);
  ledClock.drawColon(true);
  ledClock.writeDisplay();

  delay(1000);
  
  readDS();
  ledTemp.print(fahrenheit,1);
  ledTemp.writeDisplay();

  delay(1000);
  
  loopCnt++;
  // Serial.print("loopCnt = "); // uncomment for testing
  // Serial.println(loopCnt);    // uncomment for testing
  if(loopCnt >= 600)
  {
    loopCnt=0;
    ntpUpdate();
  }
}
