#pragma once

// ============================================================================
// DMX Interceptor - Hardware Configuration
// Target: ESP-WROOM-32 (original ESP32, 38-pin DevKit)
// ============================================================================

// --- DMX UART Port Assignments ---
// ESP32 has 3 UARTs: UART0 (USB serial debug), UART1, UART2
// We use UART1 for DMX input and UART2 for DMX output.
// #ifdef ESP32_S3_ZERO
#define DMX_INPUT_PORT DMX_NUM_1  // UART1 - DMX Receive
#define DMX_OUTPUT_PORT DMX_NUM_2 // UART2 - DMX Transmit

// --- DMX Input Pins (MAX485 Module #1 - RX only) ---
#define DMX_INPUT_RX_PIN 1 // GPIO1 <- MAX485 RO (Receiver Output)
#define DMX_INPUT_TX_PIN 2 // GPIO2 -> (unused, but UART needs a TX pin)
#define DMX_INPUT_EN_PIN 4 // GPIO4 -> MAX485 DE+RE tied together (LOW=receive)

// --- DMX Output Pins (MAX485 Module #2 - TX only) ---
#define DMX_OUTPUT_TX_PIN 5 // GPIO5 -> MAX485 DI (Driver Input)
#define DMX_OUTPUT_RX_PIN 6 // GPIO6 <- (unused, but UART needs an RX pin)
#define DMX_OUTPUT_EN_PIN                                                      \
  7 // GPIO7 -> MAX485 DE+RE tied together (HIGH=transmit)

// --- Status LED ---
#define STATUS_LED_PIN                                                         \
  8 // GPIO8 -> External Status LED (ESP32-S3-Zero lacks onboard monochromatic
    // LED)
// #else
//   #define DMX_INPUT_PORT   DMX_NUM_1   // UART1 - DMX Receive
//   #define DMX_OUTPUT_PORT  DMX_NUM_2   // UART2 - DMX Transmit

//   // --- DMX Input Pins (MAX485 Module #1 - RX only) ---
//   // UART1 default pins (GPIO 9/10) are connected to flash — must remap!
//   #define DMX_INPUT_RX_PIN   16   // GPIO16 <- MAX485 RO (Receiver Output)
//   #define DMX_INPUT_TX_PIN   17   // GPIO17 -> (unused, but UART needs a TX
//   pin) #define DMX_INPUT_EN_PIN    4   // GPIO4  -> MAX485 DE+RE tied
//   together (LOW=receive)

//   // --- DMX Output Pins (MAX485 Module #2 - TX only) ---
//   #define DMX_OUTPUT_TX_PIN  33   // GPIO33 -> MAX485 DI (Driver Input)
//   #define DMX_OUTPUT_RX_PIN  36   // GPIO36 <- (unused, input-only pin is
//   fine here) #define DMX_OUTPUT_EN_PIN  32   // GPIO32 -> MAX485 DE+RE tied
//   together (HIGH=transmit)

//   // --- Status LED ---
//   #define STATUS_LED_PIN      2   // GPIO2 -> Onboard blue LED on most ESP32
//   DevKits
// #endif

// --- Smoke Machine ---
// Smoke machine is a DMX fixture controlled via a DMX output channel.
// No relay needed — we just set the smoke channel value in the output buffer.
#define SMOKE_DMX_CHANNEL_DEFAULT 512 // Default DMX channel for smoke machine
#define SMOKE_BURST_MS 3000           // Default burst duration
#define SMOKE_COOLDOWN_MS 60000       // Minimum time between bursts
#define SMOKE_MAX_MS 10000            // Safety max continuous time

// --- DMX Constants ---
#define DMX_CHANNELS 512 // Full universe
#define DMX_PACKET_SIZE_FULL                                                   \
  (DMX_CHANNELS + 1) // 513 bytes (start code + 512 data)

// --- WiFi Configuration ---
#define WIFI_AP_SSID "DMXInterceptor"
#define WIFI_AP_PASSWORD "dmx12345"
#define WIFI_HOSTNAME "dmxinterceptor"
#define WIFI_AP_CHANNEL 6
#define WIFI_MAX_STA_RETRY 10

// --- sACN / E1.31 Configuration ---
#define SACN_DEFAULT_UNIVERSE 1         // Default sACN data universe
#define SACN_DEFAULT_TRIGGER_UNIVERSE 2 // Default macro trigger universe
#define SACN_TIMEOUT_MS 3000 // Fade sACN to zero after no packets for 3s
#define SACN_PORT 5568       // Standard sACN port

// --- Art-Net Configuration ---
#define ARTNET_PORT 6454             // Standard Art-Net UDP port
#define ARTNET_DEFAULT_UNIVERSE 0    // Default Art-Net universe (0-32767)
#define ARTNET_DEFAULT_ENABLED false // Disabled by default
#define ARTNET_DEFAULT_TARGET "255.255.255.255" // Subnet broadcast

// --- Merge Modes ---
#define MERGE_HTP 0 // Highest Takes Precedence
#define MERGE_LTP 1 // Latest Takes Precedence

// --- Task Priorities (higher = more important) ---
#define DMX_RX_TASK_PRIORITY 5 // High priority for DMX receive
#define DMX_TX_TASK_PRIORITY 5 // High priority for DMX transmit
#define SACN_TASK_PRIORITY 3   // sACN processing
#define WIFI_TASK_PRIORITY 1   // WiFi/web is lowest

// --- Task Stack Sizes ---
#define DMX_RX_STACK_SIZE 4096
#define DMX_TX_STACK_SIZE 4096
#define SACN_STACK_SIZE 4096

// --- FreeRTOS Core Assignments ---
#define DMX_CORE 1  // Core 1: Real-time DMX
#define WIFI_CORE 0 // Core 0: WiFi, sACN, and web server

// --- Web Server ---
#define WEB_SERVER_PORT 80

// --- OSC Configuration ---
#define OSC_DEFAULT_PORT 8000              // Standard TouchOSC input port
#define OSC_DEFAULT_FEEDBACK_PORT 9000     // Standard TouchOSC feedback port
#define OSC_DEFAULT_ENABLED true           // OSC enabled by default
#define OSC_DEFAULT_FEEDBACK_ENABLED false // TouchOSC feedback off by default
