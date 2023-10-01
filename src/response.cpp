#include "response.h"

Response::Response() {
  pRaw = 0;
}

Response::Response(byte *pbuffer) {
  int index, multiplier;
  byte b;

  // Calculate remaining length per the algorithm in the spec (assumes the raw data is at least 2 bytes long).
  index = 1;
  remainingLength = 0;
  multiplier = 1;
  while (1) {
    b = pbuffer[index];
    remainingLength += (b & 0x7F) * multiplier;
    if (!(b & 0x80)) {
      break;
    }
    multiplier *= 128;
    index++;
  }

  // Allocate space for the raw byte data and copy it in.
  totalLength = remainingLength + index + 1;
  pRaw = new byte[totalLength];
  memcpy(pRaw, pbuffer, totalLength);

  // The fixed header is always at the start of the data.
  pFixedHeader = pRaw;

  // The variable header follows the fixed header.
  pVariableHeader = pRaw + index + 1;
}

Response::~Response() {
  delete[] pRaw;
}

PacketType Response::getType() {
    return static_cast<PacketType>((pFixedHeader[0] & 0xF0) >> 4);
}

int Publish::getQoS() {
  return (pFixedHeader[0] & 0x06) >> 1;
}

int Publish::getTopic(byte **pptopic) {
  int topic_length = (pVariableHeader[0] * 256) + pVariableHeader[1];
  *pptopic = pVariableHeader + 2;
  return topic_length;
}

int Publish::getId() {
  int topic_length = (pVariableHeader[0] * 256) + pVariableHeader[1];
  return (pVariableHeader[topic_length + 2] * 256) + pVariableHeader[topic_length + 3];
}

int Publish::getPayload(byte **pppayload) {
  int topic_length = (pVariableHeader[0] * 256) + pVariableHeader[1];
  *pppayload = pVariableHeader + topic_length + 4;
  return remainingLength - (topic_length + 4);
}

int PubAck::getId() {
  return (pVariableHeader[0] * 256) + pVariableHeader[1];
}

int SubAck::getId() {
  return (pVariableHeader[0] * 256) + pVariableHeader[1];
}