#pragma once

#include <WiFiClient.h>

class MQTT {
 private:
  bool debug;
  WiFiClient wifiClient;
  SemaphoreHandle_t brokerMutex;
  bool sendPacket(byte *, int);
  void log(const char *pformat, ...);
  void dumpPacket(byte *ppacket, int length);

  static void receiveTask(void *);
  TaskHandle_t receiveHandle;
  QueueHandle_t receiveQueue;

  bool sendCONNECT(char *pclient, int keepAlive);
  bool awaitCONNACK();

  bool sendPUBLISH(char *ptopic, void *pdata, int length, bool retain, int *pid);
  bool awaitPUBACK(int id);
  bool sendPUBACK(int id);

  bool sendDISCONNECT();

  bool sendPING();
  bool awaitPINGRESP();

  bool sendSUBSCRIBE(char *ptopic, int *pid);
  bool awaitSUBACK(int id);

  static void keepAliveTask(void *pdata);
  TaskHandle_t keepAliveHandle;
  int keepAlive;

  static void subscriptionTask(void *);
  TaskHandle_t subscriptionHandle;

 public:
  MQTT(WiFiClient &wifiClient);
  void enableDebug(bool);
  bool connect(char *pserver, int port, char *pclient, int keepalive = 60);
  bool publish(char *ptopic, char *pmessage, bool retain = false);
  bool subscribe(char *ptopic);
  bool disconnect();
};