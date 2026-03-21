/***************************************************************************//**
 * @file
 * @brief IOSTREAM_USART Config for OpenThread RCP
 *******************************************************************************
 * # License
 * <b>Copyright 2020 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 ******************************************************************************/

#ifndef SL_IOSTREAM_USART_VCOM_CONFIG_H
#define SL_IOSTREAM_USART_VCOM_CONFIG_H

// <<< Use Configuration Wizard in Context Menu >>>

// <h>USART settings

// <o SL_IOSTREAM_USART_VCOM_BAUDRATE> Baud rate
// <i> Default: 460800
#define SL_IOSTREAM_USART_VCOM_BAUDRATE              460800

// <o SL_IOSTREAM_USART_VCOM_PARITY> Parity mode to use
// <usartNoParity=> No Parity
// <usartEvenParity=> Even parity
// <usartOddParity=> Odd parity
// <i> Default: usartNoParity
#define SL_IOSTREAM_USART_VCOM_PARITY                usartNoParity

// <o SL_IOSTREAM_USART_VCOM_STOP_BITS> Number of stop bits to use.
// <usartStopbits0p5=> 0.5 stop bits
// <usartStopbits1=> 1 stop bits
// <usartStopbits1p5=> 1.5 stop bits
// <usartStopbits2=> 2 stop bits
// <i> Default: usartStopbits1
#define SL_IOSTREAM_USART_VCOM_STOP_BITS             usartStopbits1

// <o SL_IOSTREAM_USART_VCOM_FLOW_CONTROL_TYPE> Flow control
// <usartHwFlowControlNone=> None
// <usartHwFlowControlCts=> CTS
// <usartHwFlowControlRts=> RTS
// <usartHwFlowControlCtsAndRts=> CTS/RTS
// <uartFlowControlSoftware=> Software Flow control (XON/XOFF)
// <i> Default: usartHwFlowControlCtsAndRts
#define SL_IOSTREAM_USART_VCOM_FLOW_CONTROL_TYPE     usartHwFlowControlCtsAndRts

// <o SL_IOSTREAM_USART_VCOM_RX_BUFFER_SIZE> Receive buffer size
// <i> Default: 1024 (increased for TCP latency tolerance)
#define SL_IOSTREAM_USART_VCOM_RX_BUFFER_SIZE    1024

// <q SL_IOSTREAM_USART_VCOM_CONVERT_BY_DEFAULT_LF_TO_CRLF> Convert \n to \r\n
// <i> It can be changed at runtime using the C API.
// <i> Default: 0
#define SL_IOSTREAM_USART_VCOM_CONVERT_BY_DEFAULT_LF_TO_CRLF     0

// <q SL_IOSTREAM_USART_VCOM_RESTRICT_ENERGY_MODE_TO_ALLOW_RECEPTION> Restrict the energy mode to allow the reception.
// <i> Default: 1
// <i> Limits the lowest energy mode the system can sleep to in order to keep the reception on. May cause higher power consumption.
#define SL_IOSTREAM_USART_VCOM_RESTRICT_ENERGY_MODE_TO_ALLOW_RECEPTION    1

// </h>

// <<< end of configuration section >>>

// <<< sl:start pin_tool >>>
// <usart signal=TX,RX,(CTS),(RTS)> SL_IOSTREAM_USART_VCOM
// $[USART_SL_IOSTREAM_USART_VCOM]

// Lidl Gateway UART configuration - USART0
#define SL_IOSTREAM_USART_VCOM_PERIPHERAL      USART0
#define SL_IOSTREAM_USART_VCOM_PERIPHERAL_NO   0

// TX on PA0, Location 0
#define SL_IOSTREAM_USART_VCOM_TX_PORT         gpioPortA
#define SL_IOSTREAM_USART_VCOM_TX_PIN          0
#define SL_IOSTREAM_USART_VCOM_TX_LOC          0

// RX on PA1, Location 0
#define SL_IOSTREAM_USART_VCOM_RX_PORT         gpioPortA
#define SL_IOSTREAM_USART_VCOM_RX_PIN          1
#define SL_IOSTREAM_USART_VCOM_RX_LOC          0

// CTS on PA5, Location 30
#define SL_IOSTREAM_USART_VCOM_CTS_PORT        gpioPortA
#define SL_IOSTREAM_USART_VCOM_CTS_PIN         5
#define SL_IOSTREAM_USART_VCOM_CTS_LOC         30

// RTS on PA4, Location 30
#define SL_IOSTREAM_USART_VCOM_RTS_PORT        gpioPortA
#define SL_IOSTREAM_USART_VCOM_RTS_PIN         4
#define SL_IOSTREAM_USART_VCOM_RTS_LOC         30

// [USART_SL_IOSTREAM_USART_VCOM]$
// <<< sl:end pin_tool >>>

#endif
