/**
 * Nanode + ENC28J60 Ethernet shield/EtherCard TFTP server demo
 * only supports write request. Tested with Tftpd64 tftp client only
 *
 * As this uses an external sram chip alon with a custom bootloader, its functionality is pretty limited.
 * It is shown here as an example of a simple UDP based server.
 *
 * Written By and (c) Andrew D Lindsay, May 2011
 * http://blog.thiseldo.co.uk
 * Feel free to use this code, modify and redistribute.
 * 
 */

#undef DEBUG
//#define DEBUG

#include <EtherCard.h>
#include <NanodeMAC.h>
#ifdef DEBUG
#include <EthPacketDump.h>
#endif
#include <SRAM9.h>
#include <avr/wdt.h>

// 
// Please modify the following lines. mac and ip have to be unique
// in your local area network. You can not have the same numbers in
// two devices:
static uint8_t mymac[6] = { 0,0,0,0,0,0 };
static uint8_t destmac[6] = { 0,0,0,0,0,0 };

static uint8_t myip[4] = { 192,168,1,101 };
static uint8_t gwip[4] = { 192,168,1,1 };

static uint8_t hisip[4] = { 0,0,0,0 };
//static uint8_t destip[4] = { 192,168,1,70 };

#define MYWWWPORT 80

// TFTP port
#define LISTEN_TFTP 69

// Bunch of pointers to tftp header locations
#define UDP_TFTP_TYPE_H_P 0x2a
#define UDP_TFTP_TYPE_L_P 0x2b
#define UDP_TFTP_BLOCK_H_P 0x2c
#define UDP_TFTP_BLOCK_L_P 0x2d
#define UDP_TFTP_START_P 0x2c
#define UDP_TFTP_DATA_START_P 0x2e

// TFTP message types
#define TFTP_RRQ 1
#define TFTP_WRQ 2
#define TFTP_DATA 3
#define TFTP_ACK 4
#define TFTP_ERROR 5

// This is approximately the maximum hex size for an image that
// will fit into Nanode
#define MAX_HEX_SIZE 77100
// Block size of 256, 512, 1024 are available, set max to 512
#define MAX_NANODE_BLOCK 512

const char iphdr[] PROGMEM ={
  0x45,0,0,0x82,0,0,0x40,0,0x20};

// TIDs or src/dest ports
uint8_t srcport_h = 0xA0;
uint8_t srcport_l = 0xD0;
uint8_t destport_h = 0x00;
uint8_t destport_l = 0x00;

// TFTP parameters
uint16_t blockSize = 512;
uint16_t transferSize = 0;
long expectedSize = 0L;
uint16_t sramAddress = 0;
uint16_t lastBlockNum = 0;
long cksum = 0L;

// Packet buffer, must be big enough for packet and 512b data plus IP/UDP headers
#define BUFFER_SIZE 650
//static uint8_t buf[BUFFER_SIZE+1];

byte Ethernet::buffer[BUFFER_SIZE];
BufferFiller bfill;

//EtherShield es=EtherShield();
NanodeMAC mac( mymac );

struct SRAMHeader {
  uint32_t magic;           // Magic cookie
  uint16_t datalen;         // length of uploaded data
};

SRAMHeader sramHeader;
#ifdef DEBUG
// Instantiate packet dump
EthPacketDump dump=EthPacketDump();
#endif

uint32_t lastUpdate = 0;

uint8_t intelHex[46];
int hexBufIndex = 0;
int rowNum = 0;
int recordLen = 0;

void setup() {
#ifdef DEBUG
  Serial.begin(57600);
  Serial.println("TFTP Demo");
  dump.begin( &Serial,true, true, true, true, BUFFER_SIZE );
#endif

  if (ether.begin(sizeof Ethernet::buffer, mymac) == 0) 
    Serial.println( "Failed to access Ethernet controller");

  ether.staticSetup(myip, gwip);

  ether.copyIp(ether.hisip, hisip);
  ether.printIp("Server: ", ether.hisip);

  while (ether.clientWaitingGw())
    ether.packetLoop(ether.packetReceive());
  Serial.println("Gateway found");

#ifdef DEBUG
  Serial.println("Waiting for request");
#endif
}

// A dirty hack to jump to start of boot loader
void reboot() {
asm volatile ("  jmp 0x7C00");
}
// Output a ip address from buffer from startByte
#ifdef DEBUG
void printIP( uint8_t *buf ) {
  for( int i = 0; i < 4; i++ ) {
    Serial.print( buf[i], DEC );
    if( i<3 )
      Serial.print( "." );
  }
}
// Output a ip address from buffer from startByte
void printMac( uint8_t *buf ) {
  for( int i = 0; i < 6; i++ ) {
    Serial.print( buf[i], HEX );
    if( i<5 )
      Serial.print( ":" );
  }
}
#endif

// Main loop, waits for tftp request and allows file to be uploaded to SRAM
void loop(){
  uint16_t dat_p;

  // handle ping and wait for a tcp packet
  int plen = ether.packetReceive();

  if( plen > 0 ) {
    //      dump.packetDump( buf, plen );

    // Check if Initial UDP
    if( isTftpStart( Ethernet::buffer ) ) {
      handleTftpInit( plen );
    }
    if( isTftpData( Ethernet::buffer ) ) {
      handleTftpData( plen );
    }
  }
  dat_p=ether.packetLoop(plen);
}

// Test if this is a UDP TFTP start, its on port 69
boolean isTftpStart( uint8_t *buf ) {
  return ( buf[IP_PROTO_P] == IP_PROTO_UDP_V 
  && buf[UDP_DST_PORT_L_P] == LISTEN_TFTP);
}

// test if this is a data packet, port already set
boolean isTftpData( uint8_t *buf ) {
  return ( buf[UDP_DST_PORT_L_P] == srcport_l &&
    buf[UDP_DST_PORT_H_P] == srcport_h && 
    buf[UDP_TFTP_TYPE_L_P] == 3 );
}


// Process the init packet, set filename, block size and transfer size
void handleTftpInit( int plen ) {
#ifdef DEBUG
  Serial.println("TFTP Packet");
  dump.packetDump( Ethernet::buffer, plen );
#endif

  // Reset everything
  blockSize = 512;    // default
  transferSize = 0;
  expectedSize = 0;
  lastBlockNum = 0;
  sramAddress = sizeof( sramHeader );
  cksum = 0L;

  hexBufIndex = 0;
  rowNum = 0;    
  recordLen = 0;

  // Get destination port
  destport_h = Ethernet::buffer[UDP_SRC_PORT_H_P];
  destport_l = Ethernet::buffer[UDP_SRC_PORT_L_P];
#ifdef DEBUG
  Serial.print("Dest: " );
  Serial.print( destport_h, HEX );
  Serial.print( destport_l, HEX );

  Serial.print( " Request type: " );
  Serial.println( Ethernet::buffer[UDP_TFTP_TYPE_L_P], DEC );
#endif
  // Dont support request
  if(  Ethernet::buffer[UDP_TFTP_TYPE_L_P] != TFTP_WRQ ) {
    sendTftpError(4, "Request not supported" );
    return;
  }
  // Keep dest mac and IP
  uint8_t i=0;
  while(i<6){
    destmac[i]= Ethernet::buffer[ETH_SRC_MAC+i];
    i++;
  }
  i = 0;
  while(i<4){
    hisip[i]=Ethernet::buffer[IP_SRC_P+i];
    i++;
  }
 #ifdef DEBUG
  printIP( hisip);
  Serial.println();
  printMac(destmac);
  Serial.println();
#endif
  // Scan the option, save filename
  // Get filename
  char filename[40];
  char optBuffer[20];
  int fnameLen = getNextOption( &Ethernet::buffer[UDP_TFTP_START_P], filename );

#ifdef DEBUG
  Serial.print( "Filename Len: ");
  Serial.print( fnameLen, DEC );
  Serial.print( " " );
  Serial.println( filename );
#endif

  // See what other options are present
  int optLen = 0;
  int optionOffset = fnameLen + 1;
  while( (optLen = getNextOption( &Ethernet::buffer[UDP_TFTP_START_P+optionOffset], optBuffer )) > 0 ) {
#ifdef DEBUG
    Serial.print( "Option Offset: ");
    Serial.print( optionOffset, DEC );
    Serial.print( " Option Len: ");
    Serial.print( optLen, DEC );
    Serial.print( " " );
    Serial.println( optBuffer );
#endif
    // Decode options, look for tsize then value, must be less than xxx
    if( strcmp( optBuffer, "tsize" ) == 0 ) {
      // Found tsize
      optionOffset += optLen + 1;   // New offset and skip 0 byte
      optLen = getNextOption( &Ethernet::buffer[UDP_TFTP_START_P+optionOffset], optBuffer );
      expectedSize = atol( optBuffer );
#ifdef DEBUG
      Serial.print( "Transfer size is " );
      Serial.println( expectedSize );
#endif
      if( expectedSize > MAX_HEX_SIZE ) {
        sendTftpError(3, "Transfer size too large" );
        return;
      }
    }

    // Decode options, look for blksize then value, must be 128 or 512
    if( strcmp( optBuffer, "blksize" ) == 0 ) {
      // Found tsize
      optionOffset += optLen + 1;   // New offset and skip 0 byte
      optLen = getNextOption( &Ethernet::buffer[UDP_TFTP_START_P+optionOffset], optBuffer );
      blockSize = atoi( optBuffer );
#ifdef DEBUG
      Serial.print( "Block size is " );
      Serial.println( blockSize, DEC );
#endif
      if( blockSize > MAX_NANODE_BLOCK ) {
#ifdef DEBUG
        Serial.println( "Block size too big");
#endif
        sendTftpError(0, "Block too large" );
        return;
      }
    }
    optionOffset += optLen + 1;   // New offset and skip 0 byte
  }
  // All setup correctly, send back Ack
  sendTftpAck( lastBlockNum );
  initSram();
}

void handleTftpData(int plen ) {
  uint16_t blockNum = (Ethernet::buffer[UDP_TFTP_BLOCK_H_P] << 8) + Ethernet::buffer[UDP_TFTP_BLOCK_L_P];

  // next block received
  if( blockNum == lastBlockNum + 1 ) {
    uint16_t receivedSize = plen - 46;  // remove header length

    uint16_t lastSramAddress = sramAddress;
    lastBlockNum = blockNum;

    uint16_t newSramAddress = storeData( &Ethernet::buffer[UDP_TFTP_DATA_START_P], sramAddress, receivedSize );

    // Need to send Ack or error depending on value of sramAddress
    if( newSramAddress != lastSramAddress ) {
      sendTftpAck( blockNum );
      transferSize += receivedSize;
      sramAddress = newSramAddress;
    } 
    else {
//      dump.packetDump( buf, plen );
//      Serial.println("Sending error");
      sendTftpError(3, "Checksum error on record" );
    }

    //    Serial.print( "Rx " );
    //    Serial.print( receivedSize, DEC );
    //    Serial.print( " block size " );
    //    Serial.println( blockSize );
    if( receivedSize < blockSize ) {
      //      Serial.println( "Last block received");
      if( transferSize != expectedSize ) {
#ifdef DEBUG
        Serial.println( "Failed to receive correctly");
#endif
      } 
      else {
        if( checkReceivedData( sramAddress ) ) {
#ifdef DEBUG
          Serial.println("Checksum OK");
#endif
          setSramHeader(sramAddress);
          // Dump sram
#ifdef DEBUG
          Serial.println("Dumping sram");
          dumpSram(sizeof( sramHeader ), sramAddress+sizeof( sramHeader ));

  uint32_t magic;
  uint16_t imageSize;
  uint8_t *srptr;

  SRAM9.readstream(0);
  srptr = (uint8_t*)&magic;
  for( int i=0; i<4; i++ ) {
    *srptr++ = SRAM9.RWdata(0xFF);
  }
  srptr = (uint8_t*)&imageSize;
  *srptr++ = SRAM9.RWdata(0xFF);
  *srptr++ = SRAM9.RWdata(0xFF);
  
  Serial.print( "Image Size: " );
  Serial.println( imageSize, HEX );

  if( magic == 0x79A5F0C1 ) {
    Serial.println("Magic Found");
  }

#endif
//          Serial.println("Rebooting....");
          reboot();
        }

      }
    }
  } 
  else {
    // Next block not received 
  }
}

// Scan for next option in packet
int getNextOption( uint8_t *bufStart, char *dest ) {
  int index = 0;
  //  while( index < 40 && bufStart[index] != 0) {
  //    dest[index] = bufStart[index];
  while( index < 40 && *bufStart != 0) {
    *dest++ = *bufStart++;
    index++;
  }

  //  dest[index] = '\0';
  *dest = '\0';
  return index;
}


// Send Ack
void sendTftpAck( uint16_t blockNum ) {

//  Serial.print( "ACK Block: " );
//  Serial.println( blockNum, DEC );

  // Set data
  Ethernet::buffer[UDP_TFTP_TYPE_H_P] = 0;
  Ethernet::buffer[UDP_TFTP_TYPE_L_P] = TFTP_ACK;
  Ethernet::buffer[UDP_TFTP_BLOCK_H_P] = (blockNum & 0xff00) >> 8 ;
  Ethernet::buffer[UDP_TFTP_BLOCK_L_P] = blockNum & 0xff;

  ether.sendUdp( (char*)&Ethernet::buffer[UDP_TFTP_TYPE_H_P], 4, (uint16_t)(srcport_h<<8) + srcport_l, hisip, (uint16_t)(destport_h<<8) + destport_l);
 
 // check dest mac address
 /*        uint8_t i=0;
        while(i<6){
                Serial.print(buf[ETH_DST_MAC +i], HEX);
                Serial.print(":");
                i++;
        }
  Serial.println();
  i = 0;
        while(i<4){
                Serial.print(buf[IP_DST_P +i], DEC);
                Serial.print(":");
                i++;
        }
  Serial.println();
*/
}

// Send Error 
void sendTftpError( uint8_t errorCode, char *errorMessage ) {
//  Serial.print( "Error ACK: " );
//  Serial.print( errorCode, DEC );
//  Serial.print( " " );
//  Serial.println( errorMessage );

  int payloadLen = strlen( errorMessage) + 5;

  // Set data
  Ethernet::buffer[UDP_TFTP_TYPE_L_P] = TFTP_ERROR;
  Ethernet::buffer[UDP_TFTP_BLOCK_H_P] = 0 ;
  Ethernet::buffer[UDP_TFTP_BLOCK_L_P] = 3;
  strcpy( (char*)&Ethernet::buffer[UDP_TFTP_TYPE_L_P+3], errorMessage );
  Ethernet::buffer[UDP_TFTP_TYPE_L_P+payloadLen+1] = 0;

  ether.sendUdp( (char*)&Ethernet::buffer[UDP_TFTP_TYPE_H_P], payloadLen, (uint16_t)(srcport_h<<8) + srcport_l, hisip, (uint16_t)(destport_h<<8) + destport_l);

}

int hexToInt( char char1, char char2 ) {
  int b = 0;

  if( char1 >= 'A' )
    b = (char1 - 'A' + 10) << 4;
  else
    b = (char1 - '0') << 4;

  if( char2 >= 'A' )
    b += (char2 - 'A' + 10);
  else
    b += (char2 - '0');

  return b;
}

// Write data records to SRAM
uint16_t storeData( uint8_t *bufStart, uint16_t startAddress, int dlen ) {
  int bufIndex = 0;
  uint16_t sramAddress = startAddress;

  // Need to split buffer into individual Intel Hex records

  // Remove line endings
  while( bufStart[bufIndex] == '\n' || bufStart[bufIndex] == '\r' ) {
//    Serial.print(bufIndex, DEC);
//    Serial.print( " " );
    bufIndex++;
  }

  if( bufStart[bufIndex] == ':' ) {
    // start of record
//    Serial.println("Found record start");
    recordLen = hexToInt(bufStart[bufIndex+1],bufStart[bufIndex+2]) * 2 + 11;
    hexBufIndex = 0;
  }    

/*  Serial.print("StoreData recordLen: " );
  Serial.print( recordLen, DEC );
  Serial.print(" dlen: " );
  Serial.print( dlen, DEC );
  Serial.print(" Address: " );
  Serial.print( sramAddress, HEX );
  Serial.print(" hexBufIndex: " );
  Serial.print( hexBufIndex, DEC );
  Serial.print(" bufIndex: " );
  Serial.println( bufIndex, DEC );
*/

  if( recordLen <= 0 || recordLen > 43 )
    return sramAddress;

  if( hexBufIndex >= recordLen ) {
    hexBufIndex = 0;
  }
  
  while( bufIndex < dlen ) {
//    Serial.print("While bufIndex: " );
//    Serial.println( bufIndex, DEC );

    // If start of a record, work out length of record
    if( bufStart[bufIndex] == ':' ) {
      // start of record
      // If bufIndex+1 and +2 are likely to be outside of data then finish
      if( bufIndex < (dlen - 2) ) {
        recordLen = hexToInt(bufStart[bufIndex+1],bufStart[bufIndex+2]) * 2 + 11;
      } else {
        // default to 43
        recordLen = 43;
      }
      hexBufIndex = 0;
//      Serial.println("Start new row");
      // if bufIndex + 
    }

    int i = (hexBufIndex < 43 ? hexBufIndex : 0); 
//    Serial.print( "COPY LOOP HexBufIndex: " );
//    Serial.print( hexBufIndex,DEC);
//    Serial.print(">");
    while((i < recordLen) && (bufIndex < dlen)) {
//      Serial.print(i,DEC);
//      Serial.print( " " );
//      Serial.print((char)bufStart[bufIndex]);
      intelHex[i++] = bufStart[bufIndex++];
    }
    hexBufIndex = i;
//    Serial.print( "< After HexBufIndex: " );
//    Serial.println( hexBufIndex,DEC);

    // Remove line endings
    while( bufStart[bufIndex] == '\n' || bufStart[bufIndex] == '\r' ) {
//      Serial.print(bufIndex, DEC);
//      Serial.print( " " );
      bufIndex++;
    }

    if( hexBufIndex >= recordLen ) {
      // Full record, if data write away
      intelHex[recordLen] = '\0';
      rowNum++;
#ifdef DEBUG
      Serial.print(">");
      Serial.print( (char*)intelHex );
      Serial.println("<");
#endif      
      if( intelHex[8] == '0' ) {
        uint8_t rowcksum = hexToInt(intelHex[recordLen-2],intelHex[recordLen-1]);
        uint8_t calcCksum = 0;
        for( int b=1; b<(recordLen-2); b+=2 ) {
          uint8_t bt = hexToInt(intelHex[b],intelHex[b+1]);
          calcCksum += bt;

        }
        calcCksum ^= 0xff;
        calcCksum++;
        if( rowcksum != calcCksum ) {
#ifdef DEBUG
          Serial.println("Checksum error!");
          Serial.print("CKSum >>" );
          Serial.print( calcCksum, HEX );
          Serial.print( "<<");
          Serial.print("Record CKSum >>" );
          Serial.print( rowcksum, HEX );
          Serial.print( "<<");

//          Serial.print(" Address: " );
//          Serial.print( sramAddress, HEX );
//          Serial.print(" hexBufIndex: " );
//          Serial.print( hexBufIndex, DEC );
//          Serial.print(" recordLen: " );
//          Serial.print( recordLen, DEC );
//          Serial.print(" bufIndex: " );
//          Serial.println( bufIndex, DEC );
#endif
          return sramAddress;
        } 
        else {
          // Write data to SRAM
//          Serial.println("Write SRAM");
          SRAM9.writestream(sramAddress);  // start address from 0

          for( int b=9; b<(recordLen-2); b+=2 ) {
            uint8_t bt = hexToInt(intelHex[b],intelHex[b+1]);
            SRAM9.RWdata( bt ); //write to every SRAM address 
            cksum += bt;
            sramAddress++;
          }
          SRAM9.closeRWstream();
        }
      }
    } 
    else {
      break;
    }

    // Display value of everything

//    Serial.print("END Address: " );
//    Serial.print( sramAddress, HEX );
//    Serial.print(" hexBufIndex: " );
//    Serial.print( hexBufIndex, DEC );
//    Serial.print(" bufIndex: " );
//    Serial.println( bufIndex, DEC );
  }
  return sramAddress;
}

boolean checkReceivedData( uint16_t dlen ) {
  // Test if all bytes in sram
  SRAM9.readstream(0);
  long tmpcksum = 0;
  for( int i=sizeof( sramHeader ); i< (sramAddress+sizeof( sramHeader )); i++ ) {
    tmpcksum += SRAM9.RWdata(0xFF);
  }

  SRAM9.closeRWstream();

#ifdef DEBUG
  Serial.print("Original: " );
  Serial.print( cksum );
  Serial.print( " SRAM: " );
  Serial.println( tmpcksum ); 
#endif
  return ( cksum == tmpcksum );
}


void initSram() {
  SRAM9.writestream(0);  // start address from 0

    for(unsigned int i = 0; i < 32768; i++)
    SRAM9.RWdata(0); //write to every SRAM address 

  SRAM9.closeRWstream();
}

void setSramHeader( uint16_t dataSize ) {
#ifdef DEBUG
  Serial.println("Setting Header");
#endif
  sramHeader.magic = 0x79A5F0C1;
  sramHeader.datalen = dataSize;
  SRAM9.writestream(0);  // start address from 0
  uint8_t *bt = (uint8_t*)&sramHeader;
  for( int b=0; b<sizeof( sramHeader ); b++ ) {
    SRAM9.RWdata( *bt++ ); //write to every SRAM address 
  }
  SRAM9.closeRWstream();
}

#ifdef DEBUG
void printHex( uint8_t hexval ) {
  if( hexval < 16 ) {
    Serial.print("0");
  }
  Serial.print( hexval, HEX );
  Serial.print( " " );
}

void dumpSram( uint16_t startAddress, uint16_t length ) {
  SRAM9.readstream(0);
  for( int i=startAddress; i<length; i += 16 ) {
    for( int x=0; x<16; x++ ) {
      printHex( (uint8_t) SRAM9.RWdata(0xFF) );
    }
    Serial.println(""); 
  }
  SRAM9.closeRWstream();
}
#endif
// End






