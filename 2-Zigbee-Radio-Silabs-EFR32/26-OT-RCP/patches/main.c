/*******************************************************************************
 * @file
 * @brief main() function for OpenThread RCP (Lidl Gateway)
 *******************************************************************************
 * Based on SDK sample with RTL8196E boot delay
 ******************************************************************************/
#include "app.h"
#include "sl_component_catalog.h"
#include "sl_system_init.h"
#include "sl_udelay.h"

#if defined(SL_CATALOG_POWER_MANAGER_PRESENT)
#include "sl_power_manager.h"
#endif
#if defined(SL_CATALOG_KERNEL_PRESENT)
#include "sl_system_kernel.h"
#else
#include "sl_system_process_action.h"
#endif

int main(void)
{
    // RTL8196E boot delay: wait 1 second for host UART to be ready
    // The RTL8196E main SoC needs time to initialize its UART before
    // the EFR32 starts communicating. Without this delay, early
    // Spinel messages may be lost.
    sl_udelay_wait(1000000);  // 1 second delay

    // Initialize Silicon Labs device, system, service(s) and protocol stack(s).
    sl_system_init();

    // Initialize the application.
    app_init();

#if defined(SL_CATALOG_KERNEL_PRESENT)
    // Start the kernel.
    sl_system_kernel_start();
#else
    while (1)
    {
        // Process Silicon Labs components.
        sl_system_process_action();

        // Application process.
        app_process_action();

#if defined(SL_CATALOG_POWER_MANAGER_PRESENT)
        // Let the CPU go to sleep if the system allows it.
        sl_power_manager_sleep();
#endif
    }
    // Clean-up when exiting the application.
    app_exit();
#endif
}
