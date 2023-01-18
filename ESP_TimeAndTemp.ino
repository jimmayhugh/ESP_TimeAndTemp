/*
 * Simple NTP client
 * https://www.mischianti.org/
 *
 * The MIT License (MIT)
 * written by Renzo Mischianti <www.mischianti.org>
 */
 
#include "/home/jimm/Arduino/arduino_secrets.h" 

///////please enter your sensitive data in the Secret tab/arduino_secrets.h
char ssid[]     = SECRET_SSID;        // your network SSID (name)
char password[] = SECRET_PASS;    // your network password (use for WPA, or use as key for WEP)
int keyIndex = 0;            // your network key Index number (needed only for WEP)
 
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266mDNS.h>
#include <NTPClient.h>
#include <TimeLib.h>
#include <time.h>
#include <Timezone.h>    // https://github.com/JChristensen/Timezone
#include <Wire.h> // Enable this line if using Arduino Uno, Mega, etc.
#include <Adafruit_GFX.h>
#include "Adafruit_LEDBackpack.h"
#include <OneWire.h>
#include <stdio.h>
#include <stdlib.h>

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

// UDP temp info

#define TEMP_PORT 5226
WiFiUDP tempUDP;
char packet[255];
char reply[10];
int packetSize;

// OneWire Stuff
  byte i;
  byte present = 0;
  byte type_s;
  byte data[9];
  byte addr[8];
  float celsius, fahrenheit;

char *ftoa(char *a, double f, int precision)
{
  long p[] = {0,10,100,1000,10000,100000,1000000,10000000,100000000};
  
  char *ret = a;
  long heiltal = (long)f;
  itoa(heiltal, a, 10);
  while (*a != '\0') a++;
  *a++ = '.';
  long desimal = abs((long)((f - heiltal) * p[precision]));
  itoa(desimal, a, 10);
  return ret;
}

void sendTemp(void)
{
  // If packet received...
  packetSize = tempUDP.parsePacket();
  if (packetSize) 
  {
    Serial.print("Received packet! Size: ");
    Serial.println(packetSize); 
    int len = tempUDP.read(packet, 255);
    if (len > 0)
    {
      packet[len] = '\0';
    }
    Serial.print("Packet received: ");
    Serial.println(packet);

    // Send return packet
    ftoa(reply,fahrenheit,1);
    tempUDP.beginPacket(tempUDP.remoteIP(), tempUDP.remotePort());
    tempUDP.write(reply);
    tempUDP.endPacket();
  }
}

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
  
  delay(1000);
  
  Serial.println();
  Serial.println();
  
  while ( WiFi.status() != WL_CONNECTED )
  {
    delay ( 500 );
    Serial.print ( "." );
  }
  // Connected to WiFi
  Serial.println();
  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  // Set up mDNS responder:
  // - first argument is the domain name, in this example
  //   the fully-qualified domain name is "esp8266.local"
  // - second argument is the IP address to advertise
  //   we send our IP address on the WiFi network
  if (!MDNS.begin("outsidetemp")) {
    Serial.println("Error setting up MDNS responder!");
    while (1) { delay(1000); }
  }
  Serial.println("mDNS responder started");

  timeClient.begin();
  delay ( 1000 );
  ntpUpdate();

  tempUDP.begin(TEMP_PORT);
  Serial.print("Listening on UDP port: ");
  Serial.println(TEMP_PORT);
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

  sendTemp();
  delay(1000);

  MDNS.update();
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
