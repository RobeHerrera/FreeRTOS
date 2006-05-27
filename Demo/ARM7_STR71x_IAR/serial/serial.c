/*
	FreeRTOS V4.0.1 - Copyright (C) 2003-2006 Richard Barry.

	This file is part of the FreeRTOS distribution.

	FreeRTOS is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	FreeRTOS is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with FreeRTOS; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

	A special exception to the GPL can be applied should you wish to distribute
	a combined work that includes FreeRTOS, without being obliged to provide
	the source code for any proprietary components.  See the licensing section
	of http://www.FreeRTOS.org for full details of how and when the exception
	can be applied.

	***************************************************************************
	See http://www.FreeRTOS.org for documentation, latest information, license
	and contact details.  Please ensure to read the configuration and relevant
	port sections of the online documentation.
	***************************************************************************
*/

/*
	BASIC INTERRUPT DRIVEN SERIAL PORT DRIVER FOR UART0.
*/

/* Library includes. */
#include "uart.h"
#include "gpio.h"
#include "eic.h"

/* Scheduler includes. */
#include "FreeRTOS.h"
#include "queue.h"

/* Demo application includes. */
#include "serial.h"

#define UART0_Rx_Pin 					( 0x0001<< 8 )
#define UART0_Tx_Pin 					( 0x0001<< 9 )

#define serINVALID_QUEUE				( ( xQueueHandle ) 0 )
#define serNO_BLOCK						( ( portTickType ) 0 )

/* Macros to turn on and off the Tx empty interrupt. */
#define serINTERRUPT_ON()				UART0->IER |= UART_TxHalfEmpty
#define serINTERRUPT_OFF()				UART0->IER &= ~UART_TxHalfEmpty

/*-----------------------------------------------------------*/

/* Queues used to hold received characters, and characters waiting to be
transmitted. */
static xQueueHandle xRxedChars;
static xQueueHandle xCharsForTx;

/*-----------------------------------------------------------*/

/* Interrupt entry point written in the assembler file serialISR.s79. */
extern void vSerialISREntry( void );

/* The interrupt service routine - called from the assembly entry point. */
__arm void vSerialISR( void );

/*-----------------------------------------------------------*/

/*
 * See the serial2.h header file.
 */
xComPortHandle xSerialPortInitMinimal( unsigned portLONG ulWantedBaud, unsigned portBASE_TYPE uxQueueLength )
{
xComPortHandle xReturn;
	
	/* Create the queues used to hold Rx and Tx characters. */
	xRxedChars = xQueueCreate( uxQueueLength, ( unsigned portBASE_TYPE ) sizeof( signed portCHAR ) );
	xCharsForTx = xQueueCreate( uxQueueLength + 1, ( unsigned portBASE_TYPE ) sizeof( signed portCHAR ) );

	/* If the queues were created correctly then setup the serial port
	hardware. */
	if( ( xRxedChars != serINVALID_QUEUE ) && ( xCharsForTx != serINVALID_QUEUE ) )
	{
		portENTER_CRITICAL();
		{
			/* Setup the UART port pins. */
			GPIO_Config( GPIO0, UART0_Tx_Pin, GPIO_AF_PP );
			GPIO_Config( GPIO0, UART0_Rx_Pin, GPIO_IN_TRI_CMOS );

			/* Configure the UART. */
			UART_OnOffConfig( UART0, ENABLE );
			UART_FifoConfig( UART0, DISABLE );
			UART_FifoReset( UART0, UART_RxFIFO );
			UART_FifoReset( UART0, UART_TxFIFO );
			UART_LoopBackConfig(UART0, DISABLE );
			UART_Config( UART0, ulWantedBaud, UART_NO_PARITY, UART_1_StopBits, UARTM_8D );
			UART_RxConfig( UART0, ENABLE );

			/* Configure the IEC for the UART interrupts. */
			EIC_IRQChannelPriorityConfig( UART0_IRQChannel, 1 );
			EIC_IRQChannelConfig( UART0_IRQChannel, ENABLE );
			EIC_IRQConfig( ENABLE );
			UART_ItConfig( UART0, UART_RxBufFull, ENABLE );
		}
		portEXIT_CRITICAL();
	}
	else
	{
		xReturn = ( xComPortHandle ) 0;
	}

	/* This demo file only supports a single port but we have to return
	something to comply with the standard demo header file. */
	return xReturn;
}
/*-----------------------------------------------------------*/

signed portBASE_TYPE xSerialGetChar( xComPortHandle pxPort, signed portCHAR *pcRxedChar, portTickType xBlockTime )
{
	/* The port handle is not required as this driver only supports one port. */
	( void ) pxPort;

	/* Get the next character from the buffer.  Return false if no characters
	are available, or arrive before xBlockTime expires. */
	if( xQueueReceive( xRxedChars, pcRxedChar, xBlockTime ) )
	{
		return pdTRUE;
	}
	else
	{
		return pdFALSE;
	}
}
/*-----------------------------------------------------------*/

void vSerialPutString( xComPortHandle pxPort, const signed portCHAR * const pcString, unsigned portSHORT usStringLength )
{
signed portCHAR *pxNext;

	/* A couple of parameters that this port does not use. */
	( void ) usStringLength;
	( void ) pxPort;

	/* NOTE: This implementation does not handle the queue being full as no
	block time is used! */

	/* The port handle is not required as this driver only supports UART0. */
	( void ) pxPort;

	/* Send each character in the string, one at a time. */
	pxNext = ( signed portCHAR * ) pcString;
	while( *pxNext )
	{
		xSerialPutChar( pxPort, *pxNext, serNO_BLOCK );
		pxNext++;
	}
}
/*-----------------------------------------------------------*/

signed portBASE_TYPE xSerialPutChar( xComPortHandle pxPort, signed portCHAR cOutChar, portTickType xBlockTime )
{
	/* Place the character in the queue of characters to be transmitted. */
	if( xQueueSend( xCharsForTx, &cOutChar, xBlockTime ) != pdPASS )
	{
		return pdFAIL;
	}

	/* Turn on the Tx interrupt so the ISR will remove the character from the
	queue and send it.   This does not need to be in a critical section as
	if the interrupt has already removed the character the next interrupt
	will simply turn off the Tx interrupt again. */
	serINTERRUPT_ON();

	return pdPASS;
}
/*-----------------------------------------------------------*/

void vSerialClose( xComPortHandle xPort )
{
	/* Not supported as not required by the demo application. */
}
/*-----------------------------------------------------------*/

/* Serial port ISR.  This can cause a context switch so is not defined as a
standard ISR using the __irq keyword.  Instead a wrapper function is defined
within serialISR.s79 which in turn calls this function.  See the port
documentation on the FreeRTOS.org website for more information. */
__arm void vSerialISR( void )
{
unsigned portSHORT usStatus;
signed portCHAR cChar;
portBASE_TYPE xTaskWokenByTx = pdFALSE, xTaskWokenByPost = pdFALSE;

	/* What caused the interrupt? */
	usStatus = UART_FlagStatus( UART0 );

	if( usStatus & UART_TxHalfEmpty )
	{
		/* The interrupt was caused by the THR becoming empty.  Are there any
		more characters to transmit? */
		if( xQueueReceiveFromISR( xCharsForTx, &cChar, &xTaskWokenByTx ) == pdTRUE )
		{
			/* A character was retrieved from the queue so can be sent to the
			THR now. */
			UART0->TxBUFR = cChar;
		}
		else
		{
			/* Queue empty, nothing to send so turn off the Tx interrupt. */
			serINTERRUPT_OFF();
		}		
	}

	if( usStatus & 	UART_RxBufFull )
	{
		/* The interrupt was caused by a character being received.  Grab the
		character from the RHR and place it in the queue of received
		characters. */
		cChar = UART0->RxBUFR;
		xTaskWokenByPost = xQueueSendFromISR( xRxedChars, &cChar, xTaskWokenByPost );
	}

	/* If a task was woken by either a character being received or a character
	being transmitted then we may need to switch to another task. */
	portEND_SWITCHING_ISR( ( xTaskWokenByPost || xTaskWokenByTx ) );

	/* End the interrupt in the EIC. */
	portCLEAR_EIC();
}





	