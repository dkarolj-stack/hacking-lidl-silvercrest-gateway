/***************************************************************************//**
 * @file app.c
 * @brief Callbacks implementation for NCP-UART-HW firmware
 *******************************************************************************
 * NCP firmware for EFR32MG1B232F256GM48
 ******************************************************************************/

#include PLATFORM_HEADER
#include "ember.h"

//----------------------
// Implemented Callbacks

/** @brief Radio Needs Calibrating Callback
 *
 * Called when the radio requires calibration (temperature drift, etc.)
 */
void emberAfRadioNeedsCalibratingCallback(void)
{
  sl_mac_calibrate_current_channel();
}

/** @brief Main Init Callback
 *
 * Called once during initialization. Can be used to set up
 * application-specific initialization.
 */
void emberAfMainInitCallback(void)
{
}
