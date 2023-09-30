#include "mqtt.h"

#include <Arduino.h>

#include "response.h"

MQTT::MQTT(WiFiClient &wifiClient) {
  this->wifiClient = wifiClient;
  debug = false;
  keepAliveHandle = 0;
  keepAlive = 0;
  subscriptionHandle = 0;
  brokerMutex = xSemaphoreCreateMutex();
  receiveHandle = 0;
  receiveQueue = 0;
}

void MQTT::enableDebug(bool enable) {
  debug = enable;
}

void MQTT::log(const char *pformat, ...) {
  if (!debug) {
    return;
  }

  char buffer[256];

  va_list args;
  va_start(args, pformat);

  vsnprintf(buffer, 255, pformat, args);
  Serial.print(buffer);

  va_end(args);
}

void MQTT::dumpPacket(byte *ppacket, int length) {
  int n;

  log("Packet length %d: ", length);
  for (n = 0; n < length; n++) {
    log(" 0x%02X", ppacket[n]);
  }
  log("\n");
}

void MQTT::receiveTask(void *pdata) {
  MQTT *pmqtt = (MQTT *)pdata;
  int count, processed;
  byte *pbuffer;
  Response *presponse;

  while (1) {
    count = pmqtt->wifiClient.available();
    if (count > 1) {
      // pmqtt->log("%d bytes available\n", count);
      pbuffer = new byte[count];
      pmqtt->wifiClient.read(pbuffer, count);
      processed = 0;
      while (processed < count) {
        presponse = new Response(pbuffer + processed);
        xQueueSend(pmqtt->receiveQueue, &presponse, 0);
        processed += presponse->length;
      }
      delete[] pbuffer;
    }
  }
}

void MQTT::subscriptionTask(void *pdata) {
  MQTT *pmqtt = (MQTT *)pdata;
  Response *presponse;
  Publish *ppublish;
  char message[64];
  byte *psource;
  int length, id;

  while (1) {
    // Wait forever for a packet to appear.
    while (1) {
      if (xQueueReceive(pmqtt->receiveQueue, &presponse, portMAX_DELAY) == pdTRUE) {
        break;
      }
    }

    // Is it for us?
    if (presponse->getType() == PacketType::PUBLISH) {
      ppublish = static_cast<Publish *>(presponse);
      length = ppublish->getMessage(&psource);
      // TODO Check message length
      memcpy(message, psource, length);
      message[length] = 0;
      id = ppublish->getId();
      pmqtt->log("Received message [%s] with ID 0x%04X\n", message, id);
      delete presponse;
      pmqtt->sendPUBACK(id);
    } else {
      xQueueSend(pmqtt->receiveQueue, &presponse, 0);
    }
  }
}

bool MQTT::sendPacket(byte *ppacket, int length) {
  bool result = false;
  int n;

  n = wifiClient.write(ppacket, length);
  if (n != length) {
    log("Wrong number of bytes sent, %d sent, expecting %d\n", n, length);
    goto exit;
  }

  result = true;

exit:
  return result;
}

bool MQTT::sendCONNECT(char *pclient, int keepAlive) {
  byte *ppacket;
  int index, n;
  bool result = false;

  ppacket = new byte[strlen(pclient) + 32];

  ppacket[0] = 0x10;
  index = 2;
  ppacket[index++] = 0;  // Protocol name
  ppacket[index++] = 4;
  ppacket[index++] = 'M';
  ppacket[index++] = 'Q';
  ppacket[index++] = 'T';
  ppacket[index++] = 'T';
  ppacket[index++] = 4;     // Protocol level = 4 for version 3.1.1
  ppacket[index++] = 0x00;  // Connect flags => don't clean session
  // ppacket[index++] = 0x02;  // Connect flags => clean session
  ppacket[index++] = keepAlive / 256;
  ppacket[index++] = keepAlive % 256;
  n = strlen(pclient);
  ppacket[index++] = n / 256;
  ppacket[index++] = n % 256;
  memcpy(ppacket + index, pclient, n);
  index += n;
  ppacket[1] = index - 2;
  if (!sendPacket(ppacket, index)) {
    log("Error sending connection request\n");
    goto exit;
  }
  log("Connection requested\n");

  result = true;

exit:
  delete[] ppacket;
  return result;
}

bool MQTT::awaitCONNACK() {
  Response *presponse;
  bool result = false;

  while (1) {
    // Wait for there to be a response packet.
    if (xQueueReceive(receiveQueue, &presponse, pdMS_TO_TICKS(5000)) == pdFALSE) {
      log("Timed out waiting for connection response\n");
      goto exit;
    }

    // Is it a CONNACK for us?
    if (presponse->getType() == PacketType::CONNACK) {
      break;
    }

    // No, put the response back on the queue.
    xQueueSend(receiveQueue, &presponse, 0);
  }

  // Yes, get the response.
  delete presponse;

  log("Connection acknowledged\n");

  result = true;

exit:
  return result;
}

bool MQTT::sendPUBLISH(char *ptopic, void *pdata, int length, bool retain, int *pid) {
  byte *ppacket;
  int index, n;
  bool result = false;

  ppacket = new byte[strlen(ptopic) + length + 32];

  // Always QoS = 1, variable "retain"
  if (retain) {
    ppacket[0] = 0x33;
  } else {
    ppacket[0] = 0x32;
  }
  index = 2;
  n = strlen(ptopic);
  ppacket[index++] = n / 256;
  ppacket[index++] = n % 256;
  memcpy(ppacket + index, ptopic, n);
  index += n;

  // Generate (probably) unique ID
  *pid = millis() & 0xFFFF;
  ppacket[index++] = *pid / 256;
  ppacket[index++] = *pid % 256;

  memcpy(ppacket + index, pdata, length);
  index += length;
  ppacket[1] = index - 2;
  if (!sendPacket(ppacket, index)) {
    log("Error publishing message\n");
    goto exit;
  }
  log("Message published to %s with ID 0x%04X\n", ptopic, *pid);

  result = true;

exit:
  delete[] ppacket;
  return result;
}

bool MQTT::awaitPUBACK(int id) {
  Response *presponse;
  PubAck *ppuback;
  bool result = false;

  while (1) {
    // Wait for there to be a response packet.
    if (xQueueReceive(receiveQueue, &presponse, pdMS_TO_TICKS(5000)) == pdFALSE) {
      log("Timed out waiting for publish response\n");
      goto exit;
    }

    // Is it for us?
    if (presponse->getType() == PacketType::PUBACK) {
      ppuback = static_cast<PubAck *>(presponse);
      if (ppuback->getId() == id) {
        break;
      }
    }

    xQueueSend(receiveQueue, &presponse, 0);
  }

  delete presponse;

  log("Publish acknowledged for ID 0x%04X\n", id);

  result = true;

exit:
  return result;
}

bool MQTT::sendPUBACK(int id) {
  byte *ppacket;
  int index, n;
  bool result = false;

  ppacket = new byte[4];

  ppacket[0] = 0x40;
  index = 2;
  ppacket[index++] = id / 256;
  ppacket[index++] = id % 256;
  ppacket[1] = index - 2;
  if (!sendPacket(ppacket, index)) {
    log("Error acknowledging publish\n");
    goto exit;
  }
  log("Publish acknowledgement sent for ID 0x%04X\n", id);

  result = true;

exit:
  delete[] ppacket;
  return result;
}

bool MQTT::sendPING() {
  byte *ppacket;
  int index, n;
  bool result = false;

  ppacket = new byte[2];

  ppacket[0] = 0xC0;
  index = 2;
  ppacket[1] = index - 2;
  if (!sendPacket(ppacket, index)) {
    log("Error sending ping\n");
    goto exit;
  }
  log("Ping sent\n");

  result = true;

exit:
  delete[] ppacket;
  return result;
}

bool MQTT::awaitPINGRESP() {
  Response *presponse;
  bool result = false;

  while (1) {
    // Wait for there to be a response packet.
    if (xQueueReceive(receiveQueue, &presponse, pdMS_TO_TICKS(5000)) == pdFALSE) {
      log("Timed out waiting for ping response\n");
      goto exit;
    }

    // Is it a PING for us?
    if (presponse->getType() == PacketType::PINGRESP) {
      break;
    }

    xQueueSend(receiveQueue, &presponse, 0);
  }

  // Yes, get the response and remove it from the queue.
  delete presponse;

  log("Ping acknowledged\n");

  result = true;

exit:
  return result;
}

bool MQTT::sendDISCONNECT() {
  byte packet[2];
  int index, n;
  bool result = false;

  packet[0] = 0xE0;
  index = 2;
  packet[1] = index - 2;
  if (!sendPacket(packet, index)) {
    log("Error sending disconnection request\n");
    goto exit;
  }
  log("Disconnection requested\n");

  result = true;

exit:
  return result;
}

void MQTT::keepAliveTask(void *pdata) {
  MQTT *pmqtt = (MQTT *)pdata;

  while (1) {
    if (!pmqtt->sendPING() || !pmqtt->awaitPINGRESP()) {
      ESP.restart();
    }

    vTaskDelay(pmqtt->keepAlive * 1000);
  }
}

bool MQTT::sendSUBSCRIBE(char *ptopic, int *pid) {
  byte *ppacket;
  int index, n;
  bool result = false;

  ppacket = new byte[strlen(ptopic) + 64];

  ppacket[0] = 0x82;
  index = 2;

  // Generate (probably) unique ID
  *pid = millis() & 0xFFFF;
  ppacket[index++] = *pid / 256;
  ppacket[index++] = *pid % 256;

  // Length of subscribed topic
  n = strlen(ptopic);
  ppacket[index++] = n / 256;
  ppacket[index++] = n % 256;
  memcpy(ppacket + index, ptopic, n);
  index += n;

  // QoS (1) of subscription
  ppacket[index++] = 0x01;

  ppacket[1] = index - 2;
  if (!sendPacket(ppacket, index)) {
    log("Error sending subscription request\n");
    goto exit;
  }
  log("Subscribed to %s with ID 0x%04X\n", ptopic, *pid);

  result = true;

exit:
  delete[] ppacket;
  return result;
}

bool MQTT::awaitSUBACK(int id) {
  Response *presponse;
  SubAck *psuback;
  bool result = false;

  while (1) {
    // Wait for there to be a response packet.
    if (xQueueReceive(receiveQueue, &presponse, pdMS_TO_TICKS(5000)) == pdFALSE) {
      log("Timed out waiting for subscribe response\n");
      goto exit;
    }

    // Is it for us?
    if (presponse->getType() == PacketType::SUBACK) {
      psuback = static_cast<SubAck *>(presponse);
      if (psuback->getId() == id) {
        break;
      }
    }

    xQueueSend(receiveQueue, &presponse, 0);
  }

  delete presponse;

  log("Subscribe acknowledged for ID 0x%04X\n", id);

  result = true;

exit:
  return result;
}

bool MQTT::connect(char *pserver, int port, char *pclient, int keepalive) {
  int tries;
  bool result = false;

  if (receiveQueue) {
    vQueueDelete(receiveQueue);
  }
  receiveQueue = xQueueCreate(100, sizeof(Response *));
  if (receiveHandle) {
    vTaskDelete(receiveHandle);
  }
  xTaskCreatePinnedToCore(receiveTask, "MQTT receive", 8192, this, 1, &receiveHandle, 1);

  if (!wifiClient.connect(pserver, port)) {
    log("Error connecting to broker\n");
    goto exit;
  }
  log("Connected to broker at %s\n", wifiClient.remoteIP().toString().c_str());

  tries = 0;
  while (true) {
    if (sendCONNECT(pclient, keepalive * 2) && awaitCONNACK()) {
      // if (atomicCONNECT(pclient, keepalive * 2)) {
      break;
    }
    if (++tries == 3) {
      goto exit;
    }
  }

  if (keepAliveHandle) {
    vTaskDelete(keepAliveHandle);
  }
  this->keepAlive = keepalive;
  xTaskCreatePinnedToCore(keepAliveTask, "MQTT keep alive", 8192, this, 1, &keepAliveHandle, 1);

  if (subscriptionHandle) {
    vTaskDelete(subscriptionHandle);
  }
  xTaskCreatePinnedToCore(subscriptionTask, "MQTT subscrption", 8192, this, 1, &subscriptionHandle, 1);

  result = true;

exit:
  return result;
}

bool MQTT::publish(char *ptopic, char *pmessage, bool retain) {
  int tries, id;
  bool result = false;

  tries = 0;
  while (true) {
    if (sendPUBLISH(ptopic, pmessage, strlen(pmessage), retain, &id) && awaitPUBACK(id)) {
      break;
    }
    if (++tries == 3) {
      goto exit;
    }
  }

  result = true;

exit:
  return result;
}

bool MQTT::subscribe(char *ptopic) {
  int tries, id;
  bool result = false;

  tries = 0;
  while (true) {
    if (sendSUBSCRIBE(ptopic, &id) && awaitSUBACK(id)) {
      break;
    }
    if (++tries == 3) {
      goto exit;
    }
  }

  result = true;

exit:
  return result;
}

bool MQTT::disconnect() {
  int tries;
  bool result = false;

  tries = 0;
  while (true) {
    if (sendDISCONNECT()) {
      break;
    }
    if (++tries == 3) {
      goto exit;
    }
  }

  if (receiveHandle) {
    vTaskDelete(receiveHandle);
    receiveHandle = 0;
  }

  if (receiveQueue) {
    vQueueDelete(receiveQueue);
    receiveQueue = 0;
  }

  if (keepAliveHandle) {
    vTaskDelete(keepAliveHandle);
    keepAliveHandle = 0;
  }

  if (subscriptionHandle) {
    vTaskDelete(subscriptionHandle);
    subscriptionHandle = 0;
  }

  wifiClient.stop();

  result = true;

exit:
  return result;
}

/*
Because keep alive pings are sent from a separate task it's possible to get the responses out of order.
Option 1, more flexible receiver:
  Receive packet using dynamic decoding of received data type and length.
  Communicate ack to sender (by task notification?).
Option 2, synchronise communication with broker, e.g. by mutex.

Option 1 is better as we have no control over data arriving via subscriptions, i.e. a packet could
arrive with published subscription data in between sending a ping request and getting its response, thus
confusing the ping response checker.

Single point of receipt of network data in a task. Pass the response to waiting tasks in turn (those that have sent a packet).
The first to consume the response stops the round-robin process. "Response" includes PUBLISH packets as a result of subscriptions;
the subscription task processes those and sends the acknowledgement. Need a list of tasks that might be interested in a response.
*/
