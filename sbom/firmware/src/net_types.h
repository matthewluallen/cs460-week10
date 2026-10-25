// net_types.h - select the TCP server/client implementation by transport.
// Ethernet build uses the W5500's own stack (Arduino Ethernet library);
// otherwise the WiFi/lwIP stack. The Modbus/web code is written against these.
#pragma once
#ifdef ENABLE_ETHERNET
  #include <Ethernet_Generic.hpp>
  typedef EthernetServer NetServer;
  typedef EthernetClient NetClient;
#else
  #include <WiFi.h>
  typedef WiFiServer NetServer;
  typedef WiFiClient NetClient;
#endif
