// ============================================================================
// ArtNet Sender - Art-Net 4 ArtDmx packet transmitter over WiFi UDP
// ============================================================================
// ArtDmx packet layout (Art-Net 4 spec, section 7):
//   Offset  Size  Content
//   0       8     ID = "Art-Net\0"
//   8       2     OpCode = 0x5000, little-endian → [0x00, 0x50]
//   10      1     ProtVerHi = 0x00
//   11      1     ProtVerLo = 0x0E  (protocol version 14)
//   12      1     Sequence  = 1..255 cycling (0 = disabled)
//   13      1     Physical  = 0
//   14      1     SubUni    = lower 8 bits of Port-Address
//   15      1     Net       = upper 7 bits of Port-Address (bits 14:8)
//   16      1     LengthHi  = high byte of data length (big-endian)
//   17      1     LengthLo  = low byte of data length
//   18      N     DMX data  (N = length, must be even, max 512)
//   Total: 18 + N bytes (530 bytes for 512 channels)
// ============================================================================

#include "artnet_sender.h"
#include <WiFi.h>

// Global instance
ArtNetSender artnetSender;

void ArtNetSender::begin() {
    if (_begun) return;
    _udp.begin(ARTNET_PORT);
    _begun = true;
    Serial.printf("[ARTNET] Sender initialized on port %d\n", ARTNET_PORT);
}

void ArtNetSender::send(const uint8_t* dmxData, uint16_t channels) {
    if (!_enabled || !_begun || !WiFi.isConnected()) return;

    // Length must be even per spec
    if (channels > DMX_CHANNELS) channels = DMX_CHANNELS;
    if (channels & 1) channels++;  // Round up to even

    // Build packet (stack-allocated; 530 bytes is fine)
    uint8_t packet[18 + DMX_CHANNELS];

    // --- Header ---
    memcpy(packet, "Art-Net\0", 8);           // ID
    packet[8]  = 0x00;                         // OpCode lo (ArtDmx = 0x5000)
    packet[9]  = 0x50;                         // OpCode hi
    packet[10] = 0x00;                         // ProtVerHi
    packet[11] = 0x0E;                         // ProtVerLo (14)

    // Sequence: cycles 1..255 (0 means disabled per spec)
    if (++_sequence == 0) _sequence = 1;
    packet[12] = _sequence;

    packet[13] = 0x00;                         // Physical (source port, informational)
    packet[14] = (uint8_t)(_universe & 0xFF);  // SubUni (lower 8 bits of Port-Address)
    packet[15] = (uint8_t)((_universe >> 8) & 0x7F); // Net (bits 14:8)
    packet[16] = (uint8_t)(channels >> 8);     // LengthHi (big-endian)
    packet[17] = (uint8_t)(channels & 0xFF);   // LengthLo

    // --- Data ---
    memcpy(packet + 18, dmxData, channels);

    // --- Transmit ---
    IPAddress dest;
    if (!dest.fromString(_targetIP)) {
        dest = IPAddress(255, 255, 255, 255);  // Fallback to broadcast
    }

    _udp.beginPacket(dest, ARTNET_PORT);
    _udp.write(packet, 18 + channels);
    _udp.endPacket();

    _packetCount++;
}
