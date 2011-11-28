/**
 * ENC28J60 Ethershield DHCP and Pachube demo - Pachube RGB
 * This reads a number of Pachube datastreams and
 * converts the output to colours on a RGB LED.
 * The original demo uses a Pachube app with 3 rotary controls
 * that select the value for each of the Red, Green and Blue colours.
 * The example uses DHCP to determin the local IP address, gateway,
 * and DNS server.
 * 
 * As a debugging aid, the RGB LED initialy glows red.
 * Once the ENC28J60 has been initialised it changes to Orange
 * When the IP address has been allocated it changes to Green.
 * It then changes to the set colour once the values are being
 * read from the Pachube application.
 *
 * See http://blog.thiseldo.co.uk/?p=574 for details on how to set this up with Pachube dashboard
 *
 * Written By and (c) Andrew D Lindsay, May 2011
 * http://blog.thiseldo.co.uk
 * Feel free to use this code, modify and redistribute.
 * 
 */

// If using a Nanode (www.nanode.eu) instead of Arduino and ENC28J60 EtherShield or JeeNode and EtherCard then
// use this define:
#define NANODE

#include <EtherCard.h>
#ifdef NANODE
#include <NanodeMAC.h>
#endif

#define DEBUG

// If using a Common Anode RGB LED (i.e. has connection to +5V
// Then leave this uncommented this
//#define COMMON_ANODE
// If using Common Cathode RGB LED (i.e. has common connection to GND)
// then comment out the above line or change to:
#undef COMMON_ANODE

#ifdef COMMON_ANODE
#define LOW_LIMIT 255
#define HIGH_LIMIT 0
#else
#define LOW_LIMIT 0
#define HIGH_LIMIT 255
#endif

// Define where the RGB LED is connected - this is a common cathode LED.
#define BLUEPIN  3  // Blue LED,  connected to digital pin 3
#define REDPIN   5  // Red LED,   connected to digital pin 5
#define GREENPIN 6  // Green LED, connected to digital pin 6

// Please modify the following lines. mac and ip have to be unique
// in your local area network. You can not have the same numbers in
// two devices:
// how did I get the mac addr? Translate the first 3 numbers into ascii is: TUX
#ifdef NANODE
static uint8_t mymac[6] = { 0,0,0,0,0,0 };
#else
static uint8_t mymac[6] = { 0x54,0x55,0x58,0x12,0x34,0x56 };
#endif

// IP and netmask allocated by DHCP
static uint8_t myip[4] = { 0,0,0,0 };
static uint8_t mynetmask[4] = { 0,0,0,0 };
static uint8_t gwip[4] = { 0,0,0,0 };
static uint8_t dnsip[4] = { 0,0,0,0 };
static uint8_t dhcpsvrip[4] = { 0,0,0,0 };

// IP address of the host being queried to contact (IP of the first portion of the URL):
static uint8_t websrvip[4] = { 0, 0, 0, 0};

int currentRed = 0;
int currentGreen = 0;
int currentBlue = 0;

//============================================================================================================
// Pachube declarations
//============================================================================================================
#define PORT 80                   // HTTP

// the EtherCard library does not really support sending additional info in a get request
// here we fudge it in the host field to add the API key
// Http header is
// Host: <HOSTNAME>
// X-PachubeApiKey: xxxxxxxx
// User-Agent: Arduino/1.0
// Accept: text/html
// Add your own API key
#define PACHUBEAPIKEY "pachube.com\r\nX-PachubeApiKey: xxxxxxxxxxxxxxxxxx"          
#define PACHUBE_VHOST "pachube.com"
// This demo only works with v1 API URLs, A v2 url has 1 result per line 
// Add your own FEED ID
#define PACHUBEAPIURL "/api/xxxxx.csv"

static uint8_t resend=0;

byte Ethernet::buffer[700];
#ifdef NANODE
NanodeMAC mac( mymac );
#endif

void browserresult_callback(uint8_t statuscode,uint16_t datapos, uint16_t dlen){
char headerEnd[2] = {'\r','\n' };
int contentLen = 0;

#ifdef DEBUG
  Serial.print("Received data, status:"); 
  Serial.println(statuscode,DEC);
//  Serial.println((char*)&Ethernet::buffer[datapos]);
#endif

  if (datapos != 0)
  {
    // Scan headers looking for Content-Length: 5
    // Start of a line, look for "Content-Length: "
    // now search for the csv data - it follows the first blank line
    uint16_t pos = datapos;
    while (Ethernet::buffer[pos])    // loop until end of buffer (or we break out having found what we wanted)
    {
      // Look for line with \r\n on its own
      if( strncmp ((char*)&Ethernet::buffer[pos],headerEnd, 2) == 0 ) {
        Serial.println("End of headers");
        pos += 2;
        break;
      }
      
      if( strncmp ((char*)&Ethernet::buffer[pos], "Content-Length:", 15) == 0 ) {
        // Found Content-Length 
        pos += 16;          // Skip to value
        char ch = Ethernet::buffer[pos++];
        contentLen = 0;
        while(ch >= '0' && ch <= '9' ) {  // Only digits
          contentLen *= 10;
          contentLen += (ch - '0');
          ch = Ethernet::buffer[pos++];
        }
#ifdef DEBUG
        Serial.print("Content Length: " );
        Serial.println( contentLen, DEC );
#endif
      }
      // Scan to end of line
      while( Ethernet::buffer[pos++] != '\r' ) { }
      while( Ethernet::buffer[pos++] != '\n' ) { }
      
      if (Ethernet::buffer[pos] == 0) break; // run out of buffer??
    }
    if (Ethernet::buffer[pos])  // we didn't run out of buffer
    {

      int red = 0;
      int green = 0;
      int blue = 0;
      int index = pos;
      char ch = Ethernet::buffer[index++];
      while(ch >= '0' && ch <= '9' ) {
        red *= 10;
        red += (ch - '0');
        ch = Ethernet::buffer[index++];
      }
      ch = Ethernet::buffer[index++];
      while(ch >= '0' && ch <= '9') {
        green *= 10;
        green += (ch - '0');
        ch = Ethernet::buffer[index++];
      }
      ch = Ethernet::buffer[index++];
      while(ch >= '0' && ch <= '9' && index < (pos+contentLen+1)) {
        blue *= 10;
        blue += (ch - '0');
        ch = Ethernet::buffer[index++];
      }

#ifdef DEBUG
      Serial.print( "Red: " );
      Serial.println( red,DEC );
      Serial.print( "Green: " );
      Serial.println( green,DEC );
      Serial.print( "Blue: " );
      Serial.println( blue,DEC );
#endif

      // Set the RGB LEDS
//      solid( red, green, blue, 0 );
      fadeTo( red, green, blue );
    }
  }
}

//function fades existing values to new RGB values
void fadeTo(int r, int g, int b)
{
  //map values
  r = map(r, 0, 255, LOW_LIMIT, HIGH_LIMIT);
  g = map(g, 0, 255, LOW_LIMIT, HIGH_LIMIT);
  b = map(b, 0, 255, LOW_LIMIT, HIGH_LIMIT);

  //output
  fadeToColour( REDPIN, currentRed, r );
  fadeToColour( GREENPIN, currentGreen, g );
  fadeToColour( BLUEPIN, currentBlue, b );
  
  currentRed = r;
  currentGreen = g;
  currentBlue = b;
}

// Fade a single colour
void fadeToColour( int pin, int fromValue, int toValue ) {
  int increment = (fromValue > toValue ? -1 : 1 );
  int startValue = (fromValue > toValue ?  : 1 );
  
  if( fromValue == toValue ) 
    return;  // Nothing to do!

  if( fromValue > toValue ) {
    // Fade down
    for( int i = fromValue; i >= toValue; i += increment ) {
      analogWrite( pin, i );
      delay(10);
    }
  } else {
    // Fade up
    for( int i = fromValue; i <= toValue; i += increment ) {
      analogWrite( pin, i );
      delay(10);
    }
  }
}


//function holds RGB values for time t milliseconds, mainly for demo
void solid(int r, int g, int b, int t)
{
  //map values
  r = map(r, 0, 255, LOW_LIMIT, HIGH_LIMIT);
  g = map(g, 0, 255, LOW_LIMIT, HIGH_LIMIT);
  b = map(b, 0, 255, LOW_LIMIT, HIGH_LIMIT);

  //output
  analogWrite(REDPIN,r);
  analogWrite(GREENPIN,g);
  analogWrite(BLUEPIN,b);

  currentRed = r;
  currentGreen = g;
  currentBlue = b;

  //hold at this colour set for t ms
  if( delay > 0 )
    delay(t);
}

void setup(){
#ifdef DEBUG
  Serial.begin(19200);
  Serial.println("EtherCard Pachube RGB");
#endif

  pinMode(REDPIN,   OUTPUT);   // sets the pins as output
  pinMode(GREENPIN, OUTPUT);   
  pinMode(BLUEPIN,  OUTPUT);
  // Set the RGB LEDs off
  solid(255, 0, 0, 0 );

  uint8_t rev = ether.begin(sizeof Ethernet::buffer, mymac);
  Serial.print("ENC28J60 Revision " );
  Serial.println( rev, DEC );
  if ( rev == 0) 
    Serial.println( "Failed to access Ethernet controller");

  Serial.println("Setting up DHCP");
  if (!ether.dhcpSetup())
    Serial.println( "DHCP failed");
  
  ether.printIp("My IP: ", ether.myip);
  ether.printIp("Netmask: ", ether.mymask);
  ether.printIp("GW IP: ", ether.gwip);
  ether.printIp("DNS IP: ", ether.dnsip);

  if (!ether.dnsLookup(PSTR( PACHUBE_VHOST )))
    Serial.println("DNS failed");

  ether.printIp("SRV: ", ether.hisip);
  
  solid(255, 153, 51, 0 );

#ifdef DEBUG
//  Serial.print( "ENC28J60 version " );
//  Serial.println( es.ES_enc28j60Revision(), HEX);
//  if( es.ES_enc28j60Revision() <= 0 ) {
//    Serial.println( "Failed to access ENC28J60");

//    while(1);    // Just loop here
//  }
#endif

/*  
  for( int i=0; i<4; i++ ) {
   solid(255,0,0, 200 );
   solid(0,255,0, 200 );
   solid(0,0,255, 200 );
   }
  
  // All off
  solid( 0, 0, 0, 0 );
*/
#ifdef DEBUG
  Serial.println("Ready");
#endif
}

#ifdef DEBUG2
// Output a ip address from buffer from startByte
void printIP( uint8_t *buf ) {
  for( int i = 0; i < 4; i++ ) {
    Serial.print( buf[i], DEC );
    if( i<3 )
      Serial.print( "." );
  }
}
#endif


void loop()
{
  static uint32_t timetosend;
  uint16_t dat_p;
  int plen = 0;

  // Main processing loop now we have our addresses
//  while( es.ES_dhcp_state() == DHCP_STATE_OK ) {
    // Stays within this loop as long as DHCP state is ok
    // If it changes then it drops out and forces a renewal of details
    // handle ping and wait for a tcp packet - calling this routine powers the sending and receiving of data
    plen = ether.packetReceive();
    dat_p=ether.packetLoop( plen );
    if( plen > 0 ) {
      // We have a packet
      // Check if IP data
      if (dat_p == 0) {
//        if (es.ES_client_waiting_gw() ){
          // No ARP received for gateway
//          continue;
//        }
      } 
    }
    // If we have IP address for server and its time then request data

    if( millis() - timetosend > 3000)  // every 3 seconds
    {
      timetosend = millis();
#ifdef DEBUG
      Serial.println("Sending request");
#endif
      // note the use of PSTR - this puts the string into code space and is compulsory in this call
      // second parameter is a variable string to append to HTTPPATH, this string is NOT a PSTR
      ether.browseUrl(PSTR(PACHUBEAPIURL), "", PSTR(PACHUBEAPIKEY), &browserresult_callback);
     }
//  }
}

