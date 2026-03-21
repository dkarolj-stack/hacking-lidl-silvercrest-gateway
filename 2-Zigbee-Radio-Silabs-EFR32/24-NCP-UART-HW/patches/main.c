/***************************************************************************//**
 * @file main.c
 * @brief main() function for NCP-UART-HW firmware
 *******************************************************************************
 * NCP firmware for EFR32MG1B232F256GM48 with RTL8196E boot delay
 ******************************************************************************/

#ifdef SL_COMPONENT_CATALOG_PRESENT
#include "sl_component_catalog.h"
#endif
#include "sl_system_init.h"
#if defined(SL_CATALOG_POWER_MANAGER_PRESENT)
#include "sl_power_manager.h"
#endif
#if defined(SL_CATALOG_KERNEL_PRESENT)
#include "sl_system_kernel.h"
#else
#include "sl_system_process_action.h"
#endif

#ifdef EMBER_TEST
#define main nodeMain
#endif

#include "sl_udelay.h"

void app_init(void)
{
}

void app_process_action(void)
{
}

int main(void)
{
  // Add 1sec delay before any reset operation to accommodate RTL8196E boot
  // The RTL8196E main SoC needs time to initialize its UART before the EFR32
  // starts communicating. Without this delay, early EZSP messages may be lost.
  sl_udelay_wait(1000000);  // 1 second delay

  // Initialize Silicon Labs device, system, service(s) and protocol stack(s)
  sl_system_init();

  // Initialize the application
  app_init();

#if defined(SL_CATALOG_KERNEL_PRESENT)
  // Start the kernel
  sl_system_kernel_start();
#else
  while (1) {
    // Process stack and application actions
    sl_system_process_action();
    app_process_action();

#if defined(SL_CATALOG_POWER_MANAGER_PRESENT)
    // Let the CPU go to sleep if the system allows it
    sl_power_manager_sleep();
#endif
  }
#endif

  return 0;
}
