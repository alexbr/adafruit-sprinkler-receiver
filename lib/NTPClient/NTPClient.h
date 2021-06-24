#ifndef NTPClient_h
#define NTPClient_h

#include <Arduino.h>
#include <WiFiUdp.h>

// NTP time stamp is in the first 48 bytes of the message
#define NTP_PACKET_SIZE 48
#define DEFAULT_UDP_PORT 8888
#define SEVENTY_YEARS 2208988800UL

struct Date {
   unsigned long time = 0;

   void setTime(unsigned long time) {
      this->time = time;
   }

   unsigned long getTime() {
      return time;
   }

   uint8_t getHours() {
      return ((time % 86400L) / 3600);
   }

   uint8_t getMinutes() { return (time % 3600) / 60; }

   int8_t getSeconds() { return time % 60; }
};

class NTPClient {
 private:
   UDP *udp;
   IPAddress *timeServer;
   int port;
   void sendNTPpacket(IPAddress &address);

 public:
   NTPClient(UDP &udp, IPAddress &timeServer);
   void begin();
   Date getDate();
   time_t getTime();
};

#endif