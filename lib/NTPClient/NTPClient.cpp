/**
 * UDP NTP Client
 */

#include "NTPClient.h"
#include <Arduino.h>

// buffer to hold incoming and outgoing packets
byte packetBuffer[NTP_PACKET_SIZE];

NTPClient::NTPClient(UDP &udp, IPAddress &timeServer) {
   this->udp = &udp;
   this->timeServer = &timeServer;
   this->port = DEFAULT_UDP_PORT;
}

void NTPClient::begin() { udp->begin(port); }

Date NTPClient::getDate() {
   Date d;
   d.setTime(getTime());
   return d;
}

time_t NTPClient::getTime() {
   // Discard previous replies
   while (udp->parsePacket());

   sendNTPpacket(*(this->timeServer));

   unsigned long epoch = 0;
   unsigned long beginWait = millis();

   // Loop for a little to see if a reply is available
   while (millis() - beginWait < 1000) {
      if (udp->parsePacket() >= NTP_PACKET_SIZE) {
         udp->read(packetBuffer, NTP_PACKET_SIZE);

         unsigned long secsSince1900;
         // convert four bytes starting at location 40 to a long integer
         secsSince1900 = (unsigned long)packetBuffer[40] << 24;
         secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
         secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
         secsSince1900 |= (unsigned long)packetBuffer[43];

         const unsigned long seventyYears = 2208988800UL;
         epoch = secsSince1900 - seventyYears;
      }
   }

   return epoch;
}

// send an NTP request to the time server at the given address
void NTPClient::sendNTPpacket(IPAddress &address) {
   // set all bytes in the buffer to 0
   memset(packetBuffer, 0, NTP_PACKET_SIZE);
   // Initialize values needed to form NTP request
   // (see URL above for details on the packets)
   packetBuffer[0] = 0b11100011; // LI, Version, Mode
   packetBuffer[1] = 0;          // Stratum, or type of clock
   packetBuffer[2] = 6;          // Polling Interval
   packetBuffer[3] = 0xEC;       // Peer Clock Precision
   // 8 bytes of zero for Root Delay & Root Dispersion
   packetBuffer[12] = 49;
   packetBuffer[13] = 0x4E;
   packetBuffer[14] = 49;
   packetBuffer[15] = 52;

   // all NTP fields have been given values, now
   // you can send a packet requesting a timestamp:
   udp->beginPacket(address, 123); // NTP requests are to port 123
   udp->write(packetBuffer, NTP_PACKET_SIZE);
   udp->endPacket();
}