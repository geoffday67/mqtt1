#pragma once

#include <Arduino.h>

enum PacketType {
  RESERVED = 0,
  CONNECT = 1,
  CONNACK = 2,
  PUBLISH = 3,
  PUBACK = 4,
  PUBREC = 5,
  PUBREL = 6,
  PUBCOMP = 7,
  SUBSCRIBE = 8,
  SUBACK = 9,
  UNSUBSCRIBE = 10,
  UNSUBACK = 11,
  PINGREQ = 12,
  PINGRESP = 13,
  DISCONNECT = 14
};

class Response {
 protected:
  byte *pdata;

 public:
  Response();
  Response(byte *);
  ~Response();
  int length;
  PacketType getType();
};

class Publish : public Response {
 public:
  int getId();
  int getQoS();
  int getMessage(byte **);
};

class PubAck : public Response {
 public:
  int getId();
};

class SubAck : public Response {
 public:
  int getId();
};