/***************************************************************************//**
 * @file app.c
 * @brief Zigbee 3.0 Router application callbacks
 *******************************************************************************
 * Minimal router application that:
 * - Automatically attempts to join an existing Zigbee network
 * - Routes messages between devices in the mesh
 * - Supports end devices as children
 * - Provides minimal CLI for bootloader access (universal-silabs-flasher)
 ******************************************************************************/

#include "app/framework/include/af.h"
#include "app/framework/plugin/network-steering/network-steering.h"
#include "sl_iostream.h"
#include "api/btl_interface.h"
#include <stdio.h>
#include <string.h>

// Event for delayed network steering start
static sl_zigbee_event_t networkSteeringEventControl;
static void networkSteeringEventHandler(sl_zigbee_event_t *event);

// Mini-CLI for bootloader access
#define CLI_BUFFER_SIZE 64
static char cliBuffer[CLI_BUFFER_SIZE];
static uint8_t cliBufferIndex = 0;

// Event for CLI processing
static sl_zigbee_event_t cliEventControl;
static void cliEventHandler(sl_zigbee_event_t *event);

/***************************************************************************//**
 * Send string over UART
 ******************************************************************************/
static void cliPrint(const char *str)
{
  sl_iostream_write(sl_iostream_get_default(), str, strlen(str));
}

/***************************************************************************//**
 * Process CLI command
 ******************************************************************************/
static void cliProcessCommand(void)
{
  // Null-terminate the buffer
  cliBuffer[cliBufferIndex] = '\0';

  // Strip trailing whitespace
  while (cliBufferIndex > 0 &&
         (cliBuffer[cliBufferIndex-1] == ' ' ||
          cliBuffer[cliBufferIndex-1] == '\r' ||
          cliBuffer[cliBufferIndex-1] == '\n')) {
    cliBuffer[--cliBufferIndex] = '\0';
  }

  // Check for commands
  if (cliBufferIndex == 0) {
    // Empty command - just show prompt
    cliPrint("> ");
  } else if (strcmp(cliBuffer, "version") == 0) {
    // Version command - format expected by universal-silabs-flasher
    char versionStr[32];
    snprintf(versionStr, sizeof(versionStr), "stack ver. [%d.%d.%d.0]\r\n> ",
             EMBER_MAJOR_VERSION, EMBER_MINOR_VERSION, EMBER_PATCH_VERSION);
    cliPrint(versionStr);
  } else if (strcmp(cliBuffer, "bootloader reboot") == 0) {
    // Reboot into bootloader
    cliPrint("Rebooting...\r\n");
    halCommonDelayMicroseconds(50000);  // 50ms for message to send
    bootloader_rebootAndInstall();
  } else if (strcmp(cliBuffer, "info") == 0) {
    // Basic info
    char infoStr[64];
    snprintf(infoStr, sizeof(infoStr), "Zigbee Router - EmberZNet %d.%d.%d\r\n> ",
             EMBER_MAJOR_VERSION, EMBER_MINOR_VERSION, EMBER_PATCH_VERSION);
    cliPrint(infoStr);
  } else if (strcmp(cliBuffer, "network status") == 0) {
    // Show network status
    EmberNetworkStatus state = emberAfNetworkState();
    char statusStr[80];
    if (state == EMBER_JOINED_NETWORK) {
      uint8_t channel = emberGetRadioChannel();
      EmberPanId panId = emberAfGetPanId();
      snprintf(statusStr, sizeof(statusStr), "Network: JOINED (channel %d, PAN 0x%04X)\r\n> ",
               channel, panId);
    } else if (state == EMBER_NO_NETWORK) {
      snprintf(statusStr, sizeof(statusStr), "Network: NOT JOINED\r\n> ");
    } else {
      snprintf(statusStr, sizeof(statusStr), "Network: state %d\r\n> ", state);
    }
    cliPrint(statusStr);
  } else if (strcmp(cliBuffer, "network leave") == 0) {
    // Leave the network
    EmberStatus status = emberLeaveNetwork();
    if (status == EMBER_SUCCESS) {
      cliPrint("Leaving network...\r\n> ");
    } else {
      char errStr[40];
      snprintf(errStr, sizeof(errStr), "Leave failed: 0x%02X\r\n> ", status);
      cliPrint(errStr);
    }
  } else if (strcmp(cliBuffer, "network steer") == 0) {
    // Start network steering to join a network
    EmberStatus status = emberAfPluginNetworkSteeringStart();
    if (status == EMBER_SUCCESS) {
      cliPrint("Starting network steering...\r\n> ");
    } else {
      char errStr[40];
      snprintf(errStr, sizeof(errStr), "Steering failed: 0x%02X\r\n> ", status);
      cliPrint(errStr);
    }
  } else if (strcmp(cliBuffer, "help") == 0) {
    cliPrint("Commands:\r\n");
    cliPrint("  version           - Show stack version\r\n");
    cliPrint("  bootloader reboot - Enter bootloader\r\n");
    cliPrint("  info              - Show device info\r\n");
    cliPrint("  network status    - Show network status\r\n");
    cliPrint("  network leave     - Leave current network\r\n");
    cliPrint("  network steer     - Join an open network\r\n");
    cliPrint("  help              - Show this help\r\n");
    cliPrint("> ");
  } else {
    cliPrint("Unknown command. Type 'help' for available commands.\r\n> ");
  }

  // Reset buffer
  cliBufferIndex = 0;
}

/***************************************************************************//**
 * CLI event handler - polls UART for incoming data
 ******************************************************************************/
static void cliEventHandler(sl_zigbee_event_t *event)
{
  sl_zigbee_event_set_inactive(event);

  char c;
  size_t bytesRead;
  sl_status_t status;

  // Read available characters
  while (1) {
    status = sl_iostream_read(sl_iostream_get_default(), &c, 1, &bytesRead);
    if (status != SL_STATUS_OK || bytesRead == 0) {
      break;
    }

    // Handle character
    if (c == '\r' || c == '\n') {
      cliProcessCommand();
    } else if (c == '\b' || c == 0x7F) {
      // Backspace
      if (cliBufferIndex > 0) {
        cliBufferIndex--;
      }
    } else if (cliBufferIndex < CLI_BUFFER_SIZE - 1) {
      cliBuffer[cliBufferIndex++] = c;
    }
  }

  // Schedule next poll (every 100ms)
  sl_zigbee_event_set_delay_ms(event, 100);
}

/***************************************************************************//**
 * Application initialization callback
 * Called once during startup
 ******************************************************************************/
void emberAfMainInitCallback(void)
{
  // Initialize the network steering event
  sl_zigbee_event_init(&networkSteeringEventControl, networkSteeringEventHandler);

  // Schedule network steering to start after 3 seconds
  // This gives the stack time to fully initialize
  sl_zigbee_event_set_delay_ms(&networkSteeringEventControl, 3000);

  // Initialize CLI event
  sl_zigbee_event_init(&cliEventControl, cliEventHandler);
  sl_zigbee_event_set_delay_ms(&cliEventControl, 500);
}

/***************************************************************************//**
 * Stack status change callback
 * Called when the network status changes
 ******************************************************************************/
void emberAfStackStatusCallback(EmberStatus status)
{
  if (status == EMBER_NETWORK_UP) {
    // Successfully joined a network - cancel any pending steering
    sl_zigbee_event_set_inactive(&networkSteeringEventControl);
  } else if (status == EMBER_NETWORK_DOWN) {
    // Lost network connection - schedule rejoin attempt
    sl_zigbee_event_set_delay_ms(&networkSteeringEventControl, 1000);
  }
}

/***************************************************************************//**
 * Network steering complete callback
 * Called when network steering finishes (success or failure)
 ******************************************************************************/
void emberAfPluginNetworkSteeringCompleteCallback(EmberStatus status,
                                                   uint8_t totalBeacons,
                                                   uint8_t joinAttempts,
                                                   uint8_t finalState)
{
  (void)totalBeacons;
  (void)joinAttempts;
  (void)finalState;

  if (status != EMBER_SUCCESS) {
    // Failed to join - retry after 10 seconds
    sl_zigbee_event_set_delay_ms(&networkSteeringEventControl, 10000);
  }
}

/***************************************************************************//**
 * Radio calibration callback
 * Called when the radio needs calibration
 ******************************************************************************/
void emberAfRadioNeedsCalibratingCallback(void)
{
  // Use the MAC layer calibration function
  sl_mac_calibrate_current_channel();
}

/***************************************************************************//**
 * Network steering event handler
 * Starts network steering if not already on a network
 ******************************************************************************/
static void networkSteeringEventHandler(sl_zigbee_event_t *event)
{
  sl_zigbee_event_set_inactive(event);

  // Check if we're already on a network
  EmberNetworkStatus state = emberAfNetworkState();
  if (state == EMBER_JOINED_NETWORK) {
    return;
  }

  // Start network steering to find and join a network
  EmberStatus status = emberAfPluginNetworkSteeringStart();

  if (status != EMBER_SUCCESS) {
    // Failed to start steering - retry after 5 seconds
    sl_zigbee_event_set_delay_ms(event, 5000);
  }
}
