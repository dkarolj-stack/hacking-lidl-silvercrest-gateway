/**
 * @file app.c
 * @brief OpenThread RCP application
 *
 * Provides OpenThread RCP initialization for Thread/Matter networks.
 *
 * Features:
 *   - Hardware Watchdog (2s timeout) for system reliability
 *   - Spinel protocol over UART (HDLC framing)
 */

#include "sl_component_catalog.h"
#include <assert.h>
#include <stdint.h>
#include <stddef.h>

#include <openthread-core-config.h>
#include <openthread/config.h>
#include <openthread/ncp.h>
#include <openthread/tasklet.h>
#include <openthread/instance.h>

#include "openthread-system.h"

#include "em_wdog.h"
#include "em_cmu.h"

/*******************************************************************************
 * Hardware Watchdog Configuration
 * Timeout: ~2 seconds using 1kHz ULFRCO
 ******************************************************************************/
static void wdog_init(void)
{
  WDOG_Init_TypeDef wdogInit = WDOG_INIT_DEFAULT;

  // Configure for ~2 second timeout
  // ULFRCO = 1kHz, perSel = wdogPeriod_2k = 2048 cycles = ~2.048s
  wdogInit.enable = true;
  wdogInit.debugRun = false;       // Stop in debug mode
  wdogInit.em2Run = true;          // Run in EM2
  wdogInit.em3Run = true;          // Run in EM3
  wdogInit.em4Block = false;
  wdogInit.swoscBlock = false;
  wdogInit.lock = false;
  wdogInit.clkSel = wdogClkSelULFRCO;  // 1kHz clock
  wdogInit.perSel = wdogPeriod_2k;     // ~2 seconds timeout

  // Enable WDOG clock and initialize
  CMU_ClockEnable(cmuClock_HFLE, true);
  WDOGn_Init(WDOG0, &wdogInit);
}

/*******************************************************************************
 * OpenThread Instance
 ******************************************************************************/

static otInstance *sInstance = NULL;

extern void otAppNcpInit(otInstance *aInstance);

otInstance *otGetInstance(void)
{
  return sInstance;
}

/**
 * @brief Create OpenThread instance (called by sl_ot_init)
 */
void sl_ot_create_instance(void)
{
  sInstance = otInstanceInitSingle();
  assert(sInstance);
}

/**
 * @brief Initialize NCP interface (called by sl_ot_init)
 */
void sl_ot_ncp_init(void)
{
  otAppNcpInit(sInstance);
}

/*******************************************************************************
 * Application Callbacks
 ******************************************************************************/

/**
 * @brief Application initialization
 */
void app_init(void)
{
  // Initialize hardware watchdog (2s timeout)
  wdog_init();

  // OpenThread RCP initialized by sl_ot_init() via sl_system_init()
}

/**
 * @brief Application process action (called from main loop)
 */
void app_process_action(void)
{
  // Feed the watchdog - system is alive
  WDOGn_Feed(WDOG0);

  // Process OpenThread tasks
  otTaskletsProcess(sInstance);
  otSysProcessDrivers(sInstance);
}
