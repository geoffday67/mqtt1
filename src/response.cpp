#include "response.h"

Response::Response() {
  length = 0;
  pdata = 0;
}

Response::Response(byte *pbuffer) {
  int remaining = pbuffer[1];  // TODO Use logic per spec to calculate remaining length.
  length = remaining + 2;
  pdata = new byte[length];
  memcpy(pdata, pbuffer, length);
}

Response::~Response() {
  delete[] pdata;
}

PacketType Response::getType() {
  if (length > 0) {
    return static_cast<PacketType>((pdata[0] & 0xF0) >> 4);
  } else {
    return PacketType::RESERVED;
  }
}

int Publish::getQoS() {
  if (length < 1) {
    return 0;
  }

  return (pdata[0] & 0x06) >> 1;
}

int Publish::getMessage(byte **ppmessage) {
  if (length < 4) {
    *ppmessage = 0;
    return 0;
  }

  int topic_length = (pdata[2] * 256) + pdata[3];
  *ppmessage = pdata + topic_length + 6;
  int remaining_length = pdata[1];
  return remaining_length - (topic_length + 4);
}

int Publish::getId() {
  if (length < 4) {
    return 0;
  }

  int topic_length = (pdata[2] * 256) + pdata[3];
  return (pdata[topic_length + 4] * 256) + pdata[topic_length + 5];
}

int PubAck::getId() {
  if (length < 4) {
    return 0;
  }

  return (pdata[2] * 256) + pdata[3];
}

int SubAck::getId() {
  if (length < 4) {
    return 0;
  }

  return (pdata[2] * 256) + pdata[3];
}