/***************************************************************************//**
 * @file sl_cpc_drv_uart_usart_vcom_config.h
 * @brief CPC UART driver configuration for Lidl Gateway
 *
 * USART0: TX=PA0, RX=PA1, RTS=PA4, CTS=PA5
 * Baudrate: 115200, HW Flow Control (RTS/CTS)
 ******************************************************************************/

#ifndef SL_CPC_DRV_UART_USART_VCOM_CONFIG_H
#define SL_CPC_DRV_UART_USART_VCOM_CONFIG_H

// <h> CPC - UART Driver Configuration

// <o SL_CPC_DRV_UART_VCOM_RX_QUEUE_SIZE> Number of frames in RX queue
// <i> Default: 10
#define SL_CPC_DRV_UART_VCOM_RX_QUEUE_SIZE            10

// <o SL_CPC_DRV_UART_VCOM_TX_QUEUE_SIZE> Number of frames in TX queue
// <i> Default: 10
#define SL_CPC_DRV_UART_VCOM_TX_QUEUE_SIZE            10

// <o SL_CPC_DRV_UART_VCOM_BAUDRATE> UART Baudrate
// <i> Default: 115200
#define SL_CPC_DRV_UART_VCOM_BAUDRATE                 115200

// <o SL_CPC_DRV_UART_VCOM_FLOW_CONTROL_TYPE> Flow control
// <usartHwFlowControlNone=> None
// <usartHwFlowControlCtsAndRts=> CTS/RTS
// <i> Default: usartHwFlowControlCtsAndRts
#define SL_CPC_DRV_UART_VCOM_FLOW_CONTROL_TYPE        usartHwFlowControlCtsAndRts

// </h>

// <<< sl:start pin_tool >>>
// <usart signal=TX,RX,(CTS),(RTS)> SL_CPC_DRV_UART_VCOM
// $[USART_SL_CPC_DRV_UART_VCOM]

// Lidl Gateway: USART0
#define SL_CPC_DRV_UART_VCOM_PERIPHERAL               USART0
#define SL_CPC_DRV_UART_VCOM_PERIPHERAL_NO            0

// TX on PA0, Location 0
#define SL_CPC_DRV_UART_VCOM_TX_PORT                  gpioPortA
#define SL_CPC_DRV_UART_VCOM_TX_PIN                   0
#define SL_CPC_DRV_UART_VCOM_TX_LOC                   0

// RX on PA1, Location 0
#define SL_CPC_DRV_UART_VCOM_RX_PORT                  gpioPortA
#define SL_CPC_DRV_UART_VCOM_RX_PIN                   1
#define SL_CPC_DRV_UART_VCOM_RX_LOC                   0

// CTS on PA5, Location 30
#define SL_CPC_DRV_UART_VCOM_CTS_PORT                 gpioPortA
#define SL_CPC_DRV_UART_VCOM_CTS_PIN                  5
#define SL_CPC_DRV_UART_VCOM_CTS_LOC                  30

// RTS on PA4, Location 30
#define SL_CPC_DRV_UART_VCOM_RTS_PORT                 gpioPortA
#define SL_CPC_DRV_UART_VCOM_RTS_PIN                  4
#define SL_CPC_DRV_UART_VCOM_RTS_LOC                  30

// [USART_SL_CPC_DRV_UART_VCOM]$
// <<< sl:end pin_tool >>>

#endif // SL_CPC_DRV_UART_USART_VCOM_CONFIG_H
