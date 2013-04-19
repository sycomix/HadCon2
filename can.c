/*
 * VERSION 1.0 Januar 7th 2010 LATE  File: 'can.c'
 * Author: Linda Fouedjio
 * modified (heavily rather rebuild): Peter Zumbruch
 * modified: Florian Feldbauer
 * modified: Peter Zumbruch, July 2011
 * modified: Peter Zumbruch, April 2013
 */
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <stddef.h>
#include <util/delay.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/iocan128.h>
#include <avr/iocanxx.h>
#include <avr/eeprom.h>
#include <avr/wdt.h>
#include <avr/pgmspace.h>

#include "one_wire.h"
#include "one_wire_adc.h"
#include "one_wire_dualSwitch.h"
#include "one_wire_simpleSwitch.h"
#include "read_write_register.h"
#include "one_wire_temperature.h"

#include "adc.h"
#include "api.h"
#include "api_define.h"
#include "api_global.h"

#include "api_debug.h"
#include "can.h"
#include "mem-check.h"

volatile unsigned char canUseOldRecvMessage_flag;
volatile unsigned char canUseNewRecvMessage_flag;

/*variable to subscribe and unsubscribe some messages via CAN bus */
uint32_t subscribe_ID[MAX_LENGTH_SUBSCRIBE];
uint32_t subscribe_mask[MAX_LENGTH_SUBSCRIBE];

/*
 * Subscribe_Message creates/sets a listener to an ID/mask for a free MOb
 * the function has a pointer of the serial structure as input and returns no parameter
 */

void Subscribe_Message( struct uartStruct *ptr_uartStruct )
{
   int8_t findMob;
   uint8_t equality = 1;
#warning TODO: CAN report success ?
   for ( uint8_t count_subscribe = 2 ; count_subscribe <= 14 ; count_subscribe++ )
   {
      if ( ( ptr_uartStruct->Uart_Message_ID ) == ( subscribe_ID[count_subscribe] ) && ( ptr_uartStruct->Uart_Mask ) == ( subscribe_mask[count_subscribe] ) )
      {
         equality = 0;
         mailbox_errorCode = CommunicationError_p(ERRM, MOB_ERROR_this_message_already_exists, FALSE, NULL);
      }
   }
   if ( 1 == equality )
   {
      subscribe_ID[ptr_subscribe] = ( ptr_uartStruct->Uart_Message_ID );
      subscribe_mask[ptr_subscribe] = ( ptr_uartStruct->Uart_Mask );
      ptr_subscribe++;

      findMob = Get_FreeMob();

      if ( 14 < ptr_subscribe )
      {
         ptr_subscribe = 2;
      }

      if ( ( -1 ) == findMob )
      {
         mailbox_errorCode = CommunicationError_p(ERRM, MOB_ERROR_all_mailboxes_already_in_use, FALSE, NULL);
      }
      else
      {
         CANPAGE = ( findMob << MOBNB0 );
         CANSTMOB = 0x00; /* cancel pending operation */
         CANCDMOB = 0x00;
         CANHPMOB = 0x00; /* enable direct canMob indexing */

         canSetMObCanIDandMask(ptr_uartStruct->Uart_Message_ID, ptr_uartStruct->Uart_Mask, TRUE, FALSE);

         CANCDMOB = ( 1 << CONMOB1 ); /* enable reception mode */

         uint16_t mask = 1 << findMob;
         CANIE2 |= mask;
         CANIE1 |= ( mask >> 8 );
      }
   }
}//END of Subscribe_Message function

/*
 * Unsubscribe_Message removes a listener for an ID/mask
 * the function has a pointer of the serial structure as input and returns no parameter
 */

void Unsubscribe_Message( struct uartStruct *ptr_uartStruct )
{
#warning TODO: CAN report success ?
   uint8_t inequality = 1;

   for ( uint8_t count_mob = 2 ; count_mob <= 14 ; count_mob++ )
   {
      CANPAGE = ( count_mob << MOBNB0 );

      if ( ((uint32_t) (( CANIDT2 >> 5 ) | ( CANIDT1 << 3 ) ) == ptr_uartStruct->Uart_Message_ID ) &&
           ((uint32_t) (( CANIDM2 >> 5 ) | ( CANIDM1 << 3 ) ) == ptr_uartStruct->Uart_Mask )         )
      {
         CANSTMOB = 0x00; /* cancel pending operation */
         CANCDMOB = 0x00; /* very important,that disable MOB*/
         CANHPMOB = 0x00;

         canSetMObCanIDandMask(0x0, 0x0, FALSE, FALSE);

         unsigned mask = 1 << count_mob;
         CANIE2 &= ~mask;
         CANIE1 &= ~( mask >> 8 );
         inequality = 0;
         for ( int count_subscribe = 2 ; count_subscribe <= 14 ; count_subscribe++ )
         {
            if ( ( ( ptr_uartStruct->Uart_Message_ID ) == ( subscribe_ID[count_subscribe] ) ) && ( ( ( ptr_uartStruct->Uart_Mask )
                     == ( subscribe_mask[count_subscribe] ) ) ) )
            {
               subscribe_ID[count_subscribe] = 0;
               subscribe_mask[count_subscribe] = 0;
            }
         }
      }
   }
   if ( 1 == inequality )
   {
      mailbox_errorCode = CommunicationError_p(ERRM, MOB_ERROR_message_ID_not_found, FALSE, NULL);
   }
} //END of Unsubscribe_Message


/*
 * this function runs a command and might expect (RTR=1) data
 * the function has a pointer of the serial structure as input and returns no parameter
 */

void canSendMessage( struct uartStruct *ptr_uartStruct )
{
	uint32_t id     = ptr_uartStruct->Uart_Message_ID;
	uint32_t mask   = ptr_uartStruct->Uart_Mask;
	uint8_t  rtr    = ptr_uartStruct->Uart_Rtr;
	uint8_t  length = ptr_uartStruct->Uart_Length;

	if ( 0 == rtr )
	{
		/* set channel number to 1 */
		CANPAGE = ( 1 << MOBNB0 );
	}
	else /*RTR*/
	{
		/* set channel number to 0 */
		CANPAGE = ( 0 << MOBNB0 );
	}

	CANSTMOB = 0x00; /* cancel pending operation */
	CANCDMOB = 0x00; /* disable communication of current MOb */

	/* set remote transmission request bit */
	CANIDT4 = ( rtr << RTRTAG );

	/* set length of data*/
	CANCDMOB = ( length << DLC0 );

	/*set ID / MASK*/

    if ( MAX_ELEVEN_BIT >= id && MAX_ELEVEN_BIT >= mask)
	{
		/* enable CAN standard 11 bit */
		CANCDMOB &= ~( 1 << IDE );
		if (0 == rtr )
		{
			canSetMObCanIDandMask(id, mask, FALSE, FALSE);
		}
		else /*RTR*/
		{
			// set ID to send
			// set mask to receive only this message */
			canSetMObCanIDandMask(id, 0 != mask ? mask : 0x000007FF, TRUE, TRUE);
		}
	}
	else
	{
		/* enable CAN standard 29 bit */
		CANCDMOB |= ( 1 << IDE );
		if (0 == rtr )
		{
			canSetMObCanIDandMask(id, mask, FALSE, FALSE);
		}
		else /*RTR*/
		{
			// set ID to send
			// set mask to receive only this message */
			canSetMObCanIDandMask(id, 0 != mask ? mask : 0x1FFFFFFF, TRUE, TRUE);
		}
	}

	if ( 0 == rtr )
	{
		/*put data in mailbox*/
		for ( uint8_t count_data = 0 ; count_data < length ; count_data++ )
		{
			CANMSG = ptr_uartStruct->Uart_Data[count_data];
		}
	}

	CANSTMOB = 0x00;
	CANCDMOB |= ( 1 << CONMOB0 ); /*enable transmission mode*/

	/*call the function verify that the sending (and receiving) of data is complete*/
	if ( 0 == rtr )
	{
		canWaitForCanSendMessageFinished();
	}
	else /*RTR*/
	{
		canWaitForCanSendRemoteTransmissionRequestMessageFinished();
	}
}//END of canSendMessage function

/*
 *this function wait until the command is sent
 *the function has no input and output parameter
 */
void canWaitForCanSendMessageFinished( void )
{
	uint32_t can_timeout1 = CAN_TIMEOUT_US; /*Timeout for CAN-communication*/
	while ( !( CANSTMOB & ( 1 << TXOK ) ) && ( --can_timeout1 > 0 ) )
	{
		_delay_us(1);
	}
	CANSTMOB &= ~( 1 << TXOK ); /* reset transmission flag */
	CANCDMOB &= ~( 1 << CONMOB0 ); /* disable transmission mode */

	if ( 0 == can_timeout1 )
	{
	    /* timeout */
		can_errorCode = CommunicationError_p(ERRC, CAN_ERROR_timeout_for_CAN_communication, FALSE, PSTR("(%i us)"), CAN_TIMEOUT_US);
		can_timeout1 = CAN_TIMEOUT_US;
	}
	else
	{
		/* give feedback for successful transmit */
		if ( debugLevelVerboseDebug <= globalDebugLevel && ( ( globalDebugSystemMask >> debugSystemCAN ) & 0x1 ) )
		{
			snprintf_P(uart_message_string, BUFFER_SIZE - 1, PSTR("RECV (%s)"), READY);
			UART0_Send_Message_String_p(NULL,0);
		}
	}

	/* all parameter initializing */
	canResetParametersCANSend();

}//END of canWaitForCanSendMessageFinished

/*
 *this function wait until the command in case the request is sent
 *the function has no input and output parameter
 */

void canWaitForCanSendRemoteTransmissionRequestMessageFinished( void )
{
	uint32_t can_timeout2 = CAN_TIMEOUT_US; /*Timeout for CAN-communication*/
	while ( !( CANSTMOB & ( 1 << RXOK ) ) && ( --can_timeout2 > 0 ) )
	{
		_delay_us(1);
	}

	if ( ( 0 == can_timeout2 ) && ( CANSTMOB & ( 1 << TXOK ) ) )
	{
		can_errorCode = CommunicationError_p(ERRC, CAN_ERROR_timeout_for_CAN_communication, FALSE, PSTR("(%i us)"), CAN_TIMEOUT_US);
		can_timeout2 = CAN_TIMEOUT_US;
	}
	else
	{
		CANSTMOB &= ~( 1 << TXOK ); /* reset transmission flag */
		CANCDMOB &= ~( 1 << CONMOB0 ); /* disable transmission mode */
	}

	/* reset all send parameters */
	canResetParametersCANSend();

}//END of canWaitForCanSendRemoteTransmissionRequestMessageFinished function

/*
 *This function grabs the CAN data received in a string
 *the function has a pointer of the CAN structure as input and returns no parameter
 */

void canConvertCanFrameToUartFormat( struct canStruct *ptr_canStruct )
{

#warning TODO RECV messages twice with and without obsolete mob. Which Keyword?

	   if (TRUE == canUseOldRecvMessage_flag)
	   {
		   clearString(message, BUFFER_SIZE);

		   strncat_P(message, (const char*) ( pgm_read_word( &(responseKeywords[responseKeyNumber_RECV])) ), BUFFER_SIZE - 1);
		   snprintf_P(message, BUFFER_SIZE - 1, PSTR("%s %x %x %x"),message, ptr_canStruct->mob, ptr_canStruct->id, ptr_canStruct->length );
		   for ( uint8_t dataIndex = 0 ; dataIndex < ptr_canStruct->length && dataIndex < CAN_MAX_DATA_ELEMENTS ; dataIndex++ )
		   {
			   snprintf_P(message, BUFFER_SIZE - 1, PSTR("%s %x"),message, ptr_canStruct->data[dataIndex] );
		   }

		   UART0_Send_Message_String_p(message,0);

		   clearString(message, BUFFER_SIZE);
	   }
	   if (TRUE == canUseNewRecvMessage_flag)
	   {
		   clearString(message, BUFFER_SIZE);

		   strncat_P(message, (const char*) ( pgm_read_word( &(responseKeywords[responseKeyNumber_RECV])) ), BUFFER_SIZE - 1);
		   strncat_P(message, PSTR(" "), BUFFER_SIZE - 1);
		   strncat_P(message, (const char*) ( pgm_read_word( &(responseKeywords[responseKeyNumber_CANR])) ), BUFFER_SIZE - 1);
		   snprintf_P(message, BUFFER_SIZE - 1, PSTR("%s %x %x"),message, ptr_canStruct->id, ptr_canStruct->length );
		   for ( uint8_t dataIndex = 0 ; dataIndex < ptr_canStruct->length && dataIndex < CAN_MAX_DATA_ELEMENTS ; dataIndex++ )
		   {
			   snprintf_P(message, BUFFER_SIZE - 1, PSTR("%s %x"),message, ptr_canStruct->data[dataIndex] );
		   }

		   UART0_Send_Message_String_p(message,0);

		   clearString(message, BUFFER_SIZE);
	   }

   /*all parameter initialisieren*/
	canResetParametersCANReceive();
}//END of canConvertCanFrameToUartFormat function

/*
 *this function deletes some variable
 *the  function has no input and output variable
 */

void canResetParametersCANSend( void )
{
	/* rest all parameters */
	clearString(decrypt_uartString, BUFFER_SIZE);
	clearString(uart_message_string, BUFFER_SIZE);
	clearUartStruct(ptr_uartStruct);
	canClearCanStruct(ptr_canStruct);

}//END of canResetParametersCANSend


/*
 *this function deletes some variable
 *the  function has no input and output parameters
 */

void canResetParametersCANReceive( void )
{
	/* initialize all parameters of structure  */
	canClearCanStruct(ptr_canStruct);
	clearUartStruct(ptr_uartStruct);
}//END of canResetParametersCANReceive function


/*
 *this function deletes some variable
 *the  function has no input and output parameters
 */

void canClearCanStruct( struct canStruct *ptr_canStruct)
{ /*resets the canStruct structure to "0" */
   if ( NULL != ptr_canStruct )
   {
      ptr_canStruct->length = 0;
      ptr_canStruct->mask = 0;
      ptr_canStruct->id = 0;
      ptr_canStruct->mob = 0;
      for ( uint8_t clearIndex = 0 ; clearIndex < CAN_MAX_DATA_ELEMENTS ; clearIndex++ )
      {
         ptr_canStruct->data[clearIndex] = 0;
      }
   }
}//END of canClearCanStruct

/* setCanBitTimingTQUnits
 *
 * This functions sets/alters the can bit timing in units of the timing quanta TQ aka T_sql
 *
 * Based on a given integer F_CPU/Baudrate ratio two option are possible:
 * 		since N_TQ = F_CPU/Baudrate * 1/BitRatePrescaler
 * 			either the number of TQ is given and bitRateScaler<0
 * 				-> bit rate Scaler will be calculated
 * 			or the bit rate Scaler is given and number of TQ < 0
 * 				-> number of TQ will be calculated
 *
 * 	With those settings boundary and consistency checks are performed
 *  on the segment timings:
 *  	propagation time segment		: integer number of TQ
 *  	phase 1 segment               	: ""
 *  	phase 2 segment					: ""
 *  	optional, if > 0              	: ""
 *  		synchronization jump width	: ""
 *
 *  the flags:
 *  	multiple sample point sampling 	 		: TRUE/FALSE,
 *  		enables triple sampling before the sampling point
 *  	auto correct baud rate prescaler null	: TRUE/FALSE,
 *  		if baud rate prescaler = 1, phases1/2 have to be adopted +/-,
 *  		enables automatic correction
 *
 *  return values:
 *  	0	: everything OK
 *  	>0	: else
 */
uint8_t setCanBitTimingTQUnits(uint8_t numberOfTimeQuanta, uint16_t freq2BaudRatio, int8_t bitRatePreScaler,
		                       uint8_t propagationTimeSegment, uint8_t phaseSegment1, uint8_t phaseSegment2, uint8_t syncJumpWidth,
				               uint8_t multipleSamplePointSampling_flag, uint8_t autoCorrectBaudRatePreScalerNull_flag)
{
    int8_t tBitTime = 0;
    int8_t a = 0;
	// checks
    // numberOfTimeQuanta < 0 xor bitRateScaler < 0
    if (!(( 0 < numberOfTimeQuanta ) ^ ( 0 < bitRatePreScaler)))
    {
		CommunicationError_p(ERRC, dynamicMessage_ErrorIndex, FALSE,
				PSTR("setCanBitTiming: no. of Time Quanta (%i) and bit rate scaler (%i) are both (un)set"),
				numberOfTimeQuanta, bitRatePreScaler);
		return 1;
    }

    if ( 0 != canBitTimingTQBasicBoundaryChecks(propagationTimeSegment, phaseSegment1, phaseSegment2, syncJumpWidth))
    {
		return 1;
    }

	// - resulting T_bit [8,25] in units of TQ
    tBitTime = 0;
    tBitTime += CAN_BIT_TIMING_SYNCHRONIZATION_SEGMENT_TIME;
    tBitTime += propagationTimeSegment;
    tBitTime += phaseSegment1;
    tBitTime += phaseSegment2;

    // calculate bitRateScaler or numberOfTimeQuanta
    if ( 0 > bitRatePreScaler )
	{
		// calculate integer bitRatePreScaler without using division
		// replacing bitRatePreScaler = (freq2BaudRatio/numberOfTimeQuanta);

		a = freq2BaudRatio;
		bitRatePreScaler=0;
		while ( a >= numberOfTimeQuanta )
		{
			a -= numberOfTimeQuanta;
			bitRatePreScaler++;
		}
		printDebug_p(debugLevelEventDebug, debugSystemCAN, __LINE__, PSTR(__FILE__),
				PSTR("setCanBitTiming: bit rate pre scaler calculated: %i"),
				bitRatePreScaler);
	}

    if ( 0 > numberOfTimeQuanta)
    {
//    	numberOfTimeQuanta = freq2BaudRatio / bitRatePreScaler;
		a = freq2BaudRatio;
		numberOfTimeQuanta=0;
		while ( a >= bitRatePreScaler )
		{
			a -= bitRatePreScaler;
			numberOfTimeQuanta++;
		}
		printDebug_p(debugLevelEventDebug, debugSystemCAN, __LINE__, PSTR(__FILE__),
				PSTR("setCanBitTiming: no. of Time Quanta calculated: %i"),
				numberOfTimeQuanta);
    }

	// - N_TQ [8,25]
    if (0 < numberOfTimeQuanta)
    {
    	if ( 8 > numberOfTimeQuanta || 25 < numberOfTimeQuanta)
    	{
    		printDebug_p(debugLevelEventDebug, debugSystemCAN, __LINE__, PSTR(__FILE__),
    				PSTR("setCanBitTiming: no. of Time Quanta (%i) out of [8,25]"),
    				numberOfTimeQuanta);
    		CommunicationError_p(ERRC, dynamicMessage_ErrorIndex, FALSE,
    				PSTR("setCanBitTiming: no. of Time Quanta (%i) out of [8,25]"),
    				numberOfTimeQuanta);
    		return 1;
    	}
	}

    // bitRatePreScaler [1,64]
	if ( CAN_BIT_TIMING_BIT_RATE_PRESCALER_MIN > bitRatePreScaler || CAN_BIT_TIMING_BIT_RATE_PRESCALER_MAX < bitRatePreScaler)
	{
		CommunicationError_p(ERRC, dynamicMessage_ErrorIndex, FALSE,
				PSTR("setCanBitTiming: bit rate pre scaler (%i) out of [%i,%i]"),
				CAN_BIT_TIMING_BIT_RATE_PRESCALER_MIN, CAN_BIT_TIMING_BIT_RATE_PRESCALER_MAX, bitRatePreScaler);
		return 1;
	}

    // bitRatePreScaler == 1 [BRP = 0]
	// -- swj not allowed
	// -- multipleSamplePointSampling
	// -- autoCorrectBaudRatePreScalerNull_flag TRUE boundary check on phase segments

	if ( 1 == bitRatePreScaler )
	{
	    // -- swj not allowed
		if ( 0 < syncJumpWidth )
		{
			CommunicationError_p(ERRC, dynamicMessage_ErrorIndex, FALSE,
					PSTR("setCanBitTiming: bit rate pre scaler (%i) forbids sync jump width (%i) > 0"),
					bitRatePreScaler, syncJumpWidth);
			return 1;
		}
		// -- multipleSamplePointSampling
		if ( FALSE != multipleSamplePointSampling_flag )
		{
			CommunicationError_p(ERRC, dynamicMessage_ErrorIndex, FALSE,
					PSTR("setCanBitTiming: bit rate pre scaler (%i) forbids multiple sample points"),
					bitRatePreScaler);
			return 1;
		}

		// apply corrections PHS1 + 1 TQ and PHS2 - 1 TQ
		if ( FALSE != autoCorrectBaudRatePreScalerNull_flag)
		{
			printDebug_p(debugLevelEventDebug, debugSystemCAN, __LINE__, PSTR(__FILE__),
					PSTR("setCanBitTiming: bit rate pre scaler (%i) auto corrected phase 1/2: %i/%i"),
					bitRatePreScaler, phaseSegment1, phaseSegment2);
			phaseSegment1++;
			phaseSegment2--;
		}

		if ( 0 != canBitTimingTQBasicBoundaryChecks(propagationTimeSegment, phaseSegment1, phaseSegment2, syncJumpWidth))
	    {
			return 1;
	    }

	}

	CANBT1 = ( (  (bitRatePreScaler - 1) << BRP0) & 0xFF );
	CANBT2 = ( ( ((propagationTimeSegment -1 ) << PRS0) | (( syncJumpWidth -1 ) << SJW0) ) & 0xFF );
	CANBT3 = ( ( (( phaseSegment2 -1 ) << PHS20) | (( phaseSegment1 -1 ) << PHS10) | ( ((FALSE != multipleSamplePointSampling_flag)?1:0) << SMP)) && 0xFF );

	printDebug_p(debugLevelEventDebug, debugSystemCAN, __LINE__, PSTR(__FILE__),
			PSTR("setCanBitTiming: register CANBT1/2/3: %x / %x / %x"), CANBT1, CANBT2, CANBT3);

	return 0;
}

/* performs a boundary check on the basic bit timing times in units of the time quanta TQ
 * input: in units of TQ
 * 	propagationTimeSegments
 * 	phaseSegment1
 * 	phaseSegment2
 * 	syncJumpWidth
 *
 * returns:
 * 	0 ok
 * 	1 else
 */

uint8_t canBitTimingTQBasicBoundaryChecks(uint8_t propagationTimeSegment, uint8_t phaseSegment1, uint8_t phaseSegment2, uint8_t syncJumpWidth)
{
	// - propagation segment time [1,8]
	if ( CAN_BIT_TIMING_PROPAGATION_SEGMENT_TIME_MIN > propagationTimeSegment ||
		 CAN_BIT_TIMING_PROPAGATION_SEGMENT_TIME_MAX < propagationTimeSegment)
	{
		CommunicationError_p(ERRC, dynamicMessage_ErrorIndex, FALSE,
				PSTR("setCanBitTiming: propagation time segment (%i TQ) out of [%i,%i]"),
				propagationTimeSegment, CAN_BIT_TIMING_PROPAGATION_SEGMENT_TIME_MIN, CAN_BIT_TIMING_PROPAGATION_SEGMENT_TIME_MAX);
		return 1;
	}

	// - phase segment 1 PHYS1 [1,8]
	if ( CAN_BIT_TIMING_PHASE_SEGMENT_1_TIME_MIN > phaseSegment1 || CAN_BIT_TIMING_PHASE_SEGMENT_1_TIME_MAX < phaseSegment1)
	{
		CommunicationError_p(ERRC, dynamicMessage_ErrorIndex, FALSE,
				PSTR("setCanBitTiming: phase segment 1 (%i TQ) out of [%i,%i]"),
				phaseSegment1, CAN_BIT_TIMING_PHASE_SEGMENT_1_TIME_MIN, CAN_BIT_TIMING_PHASE_SEGMENT_1_TIME_MAX);
		return 1;
	}

	// - phase segment 2 PHYS2 [INFORMATION PROCESSING TIME, PHS1]
	if ( CAN_BIT_TIMING_INFORMATION_PROCESSING_TIME > phaseSegment2 ||
			phaseSegment1 < phaseSegment2)
	{
		CommunicationError_p(ERRC, dynamicMessage_ErrorIndex, FALSE,
				PSTR("setCanBitTiming: phase segment 2 (%i TQ) out of [%i,%i]"),
				phaseSegment2, CAN_BIT_TIMING_INFORMATION_PROCESSING_TIME, phaseSegment1);
		return 1;
	}

	// - sync jump width 0 or [1,min(4, phase segment 1 ] <= PHS2
	if ( 0 < syncJumpWidth )
	{
		if ( CAN_BIT_TIMING_SYNC_JUMP_WIDTH_TIME_MIN > syncJumpWidth ||
			 min(CAN_BIT_TIMING_SYNC_JUMP_WIDTH_TIME_MAX, phaseSegment1) < syncJumpWidth ||
			 phaseSegment2 < syncJumpWidth)
		{
			CommunicationError_p(ERRC, dynamicMessage_ErrorIndex, FALSE,
					PSTR(__FILE__), PSTR("setCanBitTiming: sync jump width (%i TQ) out of [%i,%i]"),
					syncJumpWidth, min(CAN_BIT_TIMING_SYNC_JUMP_WIDTH_TIME_MIN, min(phaseSegment1,phaseSegment2)));
			return 1;
		}
	}
	return 0;
}

/*
 * this function calculates settings for the CanBus timing registers CANBTx
 * to achieve the given baudrate
 * return value:
 * 0: if ok
 * errors else
 * input values baudrate in kbps and i/o frequency Hz
 *
 * slow function, not to be used within fast code segments, eg. IRQ !
 * if freq == 0, assume F_CPU
 */

int setCanBaudRate( const uint32_t rate, const uint32_t freq )
{
	/*max data rate @ 8000 kHz 1Mbit*/

	uint16_t freq2BbaudRatio = 0;
    uint8_t result = 0;
	switch( freq )
	{
	case 10000000UL: /* 10 MHz */
	{
		switch( rate )
		{
		case ONETHOUSAND_KBPS:
			freq2BbaudRatio = 10;
			CANBT1 = ( 0 << BRP0 ); /* 0x00 */
			CANBT2 = ( 2 << PRS0 ) | ( 1 << SJW0 ); /* 0x24 */
			CANBT3 = ( 1 << PHS20 ) | ( 1 << PHS10 ); /* 0x12 */
			break;
		case FIVEHUNDERT_KBPS:
			freq2BbaudRatio = 20;
			CANBT1 = ( 3 << BRP0 ); /* 0x06 */
			CANBT2 = ( 4 << PRS0 ) | ( 0 << SJW0 ); /* 0x08 */
			CANBT3 = ( 1 << PHS20 ) | ( 1 << PHS10 ) | ( 1 << SMP ); /* 0x13 */
			break;
		case TWOHUNDERTFIFTY_KBPS:
			freq2BbaudRatio = 40;
			CANBT1 = ( 3 << BRP0 ); /* 0x06 */
			CANBT2 = ( 4 << PRS0 ) | ( 0 << SJW0 ); /* 0x08 */
			/*CANBT3 = ( 1 << PHS20 ) | ( 1 << PHS10 ) | ( 1 << SMP );*/ /* 0x13 */
			CANBT3 = ( 1 << PHS20 ) | ( 1 << PHS10 );
			break;
		case ONEHUNDERTTWENTYFIVE_KBPS:
#define ONEHUNDERTTWENTYFIVE_NUMBER_OF_TQ 10
			freq2BbaudRatio = 80;
			switch (ONEHUNDERTTWENTYFIVE_NUMBER_OF_TQ)
			{
			case 20:
				break;
			case 16:
				break;
			case 10:
				// --- use 10 TQ (Prs = 5, Phs1 = 2, Phs2 = 2, Swj = 2, sample Points)
				result = setCanBitTimingTQUnits(10, freq2BbaudRatio, -1, 5, 2, 2, 2, TRUE, TRUE);
				break;
			case 8:
				break;
			}
			break;
		case ONEHUNDERT_KBPS:
			freq2BbaudRatio = 100;
			CANBT1 = ( 7 << BRP0 ); /* 0x0e*/
			CANBT2 = ( 4 << PRS0 ) | ( 0 << SJW0 ); /* 0x08 */
			CANBT3 = ( 1 << PHS20 ) | ( 1 << PHS10 ) | ( 1 << SMP ) ; /* 0x13 */
			break;
		default:
			CommunicationError_p(ERRC, dynamicMessage_ErrorIndex, TRUE, PSTR("not supported CAN Baudrate (%i) / CPU freq. (%i) combination"), rate, freq);
			return -1;
			break;
		}
	}
	break;
	case 16000000UL: /* 16 MHz */
	{
		switch( rate )
		{
		case ONETHOUSAND_KBPS:
			freq2BbaudRatio = 16;
			CANBT1 = ( 1 << BRP0 );                                  /*0x02*/
			CANBT2 = ( 2 << PRS0 )  | ( 0 << SJW0 );                 /*0x04*/
			CANBT3 = ( 1 << PHS20 ) | ( 1 << PHS10 ) | ( 1 << SMP ); /*0x13*/
			break;
		case FIVEHUNDERT_KBPS:
			freq2BbaudRatio = 32;
			CANBT1 = ( 1 << BRP0 );                                  /*0x02*/
			CANBT2 = ( 6 << PRS0 )  | ( 0 << SJW0 );                 /*0x0C*/
			CANBT3 = ( 3 << PHS20 ) | ( 3 << PHS10 ) | ( 1 << SMP ); /*0x37*/
			break;
		case TWOHUNDERTFIFTY_KBPS:
			freq2BbaudRatio = 64;
			CANBT1 = ( 3 << BRP0 );                                  /*0x06*/
			CANBT2 = ( 6 << PRS0 )  | ( 0 << SJW0 );                 /*0x0C*/
			CANBT3 = ( 3 << PHS20 ) | ( 3 << PHS10 ) | ( 1 << SMP ); /*0x37*/
			break;
		case TWOHUNDERT_KBPS:
			freq2BbaudRatio = 80;
			CANBT1 = ( 4 << BRP0 );                                  /*0x08*/
			CANBT2 = ( 6 << PRS0 )  | ( 0 << SJW0 );                 /*0x0C*/
			CANBT3 = ( 3 << PHS20 ) | ( 3 << PHS10 ) | ( 1 << SMP ); /*0x37*/
			break;
		case ONEHUNDERTTWENTYFIVE_KBPS:
			freq2BbaudRatio = 128;
			CANBT1 = ( 7 << BRP0 );                                  /*0x0E*/
			CANBT2 = ( 6 << PRS0 )  | ( 0 << SJW0 );                 /*0x0C*/
			CANBT3 = ( 3 << PHS20 ) | ( 3 << PHS10 ) | ( 1 << SMP ); /*0x37*/
			break;
		case ONEHUNDERT_KBPS:
			freq2BbaudRatio = 160;
			CANBT1 = ( 9 << BRP0 );                                  /*0x12*/
			CANBT2 = ( 6 << PRS0 )  | ( 0 << SJW0 );                 /*0x0C*/
			CANBT3 = ( 3 << PHS20 ) | ( 3 << PHS10 ) | ( 1 << SMP ); /*0x37*/
			break;
		default:
			CommunicationError_p(ERRC, dynamicMessage_ErrorIndex, TRUE, PSTR("not supported CAN Baudrate (%i) / CPU freq. (%i) combination"), rate, freq);
			return -1;
			break;
		}
	}
	break;
	default:
		CommunicationError_p(ERRC, dynamicMessage_ErrorIndex, TRUE, PSTR("not supported CAN Baudrate (%i) / CPU freq. (%i) combination"), rate, freq);
		return -1;
		break;
	}

	if (0 != result)
	{
		CommunicationError_p(ERRC, dynamicMessage_ErrorIndex, TRUE, PSTR("CAN bit timing failed"), rate, freq);
		return -1;
	}
	return 0;
}

/*
 *this function initializes the CAN register for AT90CAN128 and enable the CAN-controller
 * the function has not input variable
 * the return value is an integer:
 * 1  -> the CAN initialization is successful
 * -1 -> the CAN initialization is unsuccessful
 */

int8_t canInit( int32_t Baudrate )
{
	uint8_t intstate2 = SREG;/*save global interrupt flag*/
	/*disable interrupt*/
	cli();

	/*resets the CAN controller*/
	CANGCON |= ( 1 << SWRES );
	CANGCON &= ~( 1 << SWRES );

	/* CAN General Interrupt Enable Register - CANGIE
	 *    enable interupts*/
	CANGIE = ( 1 << ENIT )  | ( 1 << ENBOFF ) | ( 1 << ENRX )  | ( 1 << ENTX ) |
			 ( 1 << ENERR ) | ( 1 << ENBX )   | ( 1 << ENERG ) | ( 0 << ENOVRT );

	/* CAN Enable Interrupt of MOBs register*/
	CANIE2 = 0xFF;
	CANIE1 = 0x7F;

	/* clear (some) CAN registers */
	clearCanRegisters();

	/* set CAN baudrate register */
	if ( 0 < Baudrate)
	{
		if ( 0 != setCanBaudRate(Baudrate, F_CPU) )
		{
			SREG = intstate2; /*restore global interrupt flag*/
#warning Error handling/notify ?
			return -1;
		}
	}

	canUseOldRecvMessage_flag = CAN_USE_OLD_RECV_MESSAGE_FLAG;
	canUseNewRecvMessage_flag = CAN_USE_NEW_RECV_MESSAGE_FLAG;

	enableCan();
	SREG = intstate2; /*restore global interrupt flag*/
	return 1;

}// END of canInit function

void enableCan()
{
    /* set ENA/STB enable mode  */
    CANGCON |= ( 1 << ENASTB );

    /*enable reception mode here so that information for CAN come automatically*/

    //    CAN MOb Control and DLC Register - CANCDMOB
    //    • Bit 7:6 – CONMOB1:0: Configuration of Message Object
    //    These bits set the communication to be performed (no initial value after RESET).
    //    – 00 - disable.
    //    – 01 - enable transmission.
    //    – 10 - enable reception.
    //    – 11 - enable frame buffer reception
    //    These bits are not cleared once the communication is performed. The user must re-write the
    //    configuration to enable a new communication.
    //    • This operation is necessary to be able to reset the BXOK flag.
    //    • This operation also set the corresponding bit in the CANEN registers.

    /* reset to 00 */
    CANCDMOB &= ~(( 0x3 << CONMOB0 ));
    /* set to 10 */
    CANCDMOB |= ( 1 << CONMOB1 );
}

/*
 *this function deletes some CAN register for AT90CAN128
 * the function has no input and output parameters
 */

void clearCanRegisters( void )
{
    for ( uint8_t canMob = 0 ; canMob < 15 ; canMob++ )
    {
       CANPAGE = ( canMob << MOBNB0 ); /* clear all  mailbox*/
       CANSTMOB = 0x00; /*clear  MOB status register*/
       CANCDMOB = 0x00; /*clear MOB control register*/
       CANGSTA = 0x00; /*clear CAN general status register*/
       /*clear all identifier tag register*/
       CANIDT1 = 0X00;
       CANIDT2 = 0X00;
       CANIDT3 = 0X00;
       CANIDT4 = 0X00;
       /*clear all identifier mask register*/
       CANIDM1 = 0X00;
       CANIDM2 = 0X00;
       CANIDM3 = 0X00;
       CANIDM4 = 0X00;
       for ( unsigned char j = 0 ; j < 8 ; j++ )
       {
          CANMSG = 0; /*clear all CAN message register*/
       }
    }
}//END of clearCanRegisters function

/* this function gets the free communication channel
 * the function has no input parameter
 * and returns a integer value
 * freemob -> valid message object block
 * -1 -> no valid message object block
 */

int8_t Get_FreeMob( void )
{
	uint8_t ctrlReg;

	for ( uint8_t freemob = 2 ; freemob <= 14 ; freemob++ )
	{

		CANPAGE = freemob << 4;
		ctrlReg = ( CANCDMOB & ( ( 1 << CONMOB0 ) | ( 1 << CONMOB1 ) ) );
		if ( ctrlReg == 0 )
		{
			return freemob;
		}
	}
	return -1;

}//END of Get_FreeMob function


/*
 *this function gives the various CAN error on the channel
 *the function has no input and output parameters
 */

void canGetGeneralStatusError( void )
{
	if ( canCurrentGeneralStatus & ( 1 << BOFF ) )
	{
		can_errorCode = CommunicationError_p(ERRC, CAN_ERROR_Can_Bus_is_off, FALSE, PSTR("TEC: %3i REC: %3i"),
											 canCurrentTransmitErrorCounter, canCurrentReceiveErrorCounter);
		//    canInit(0); /* CAN reinit */
	}

	if ( canCurrentGeneralStatus & ( 1 << ERRP ) )
	{
		can_errorCode = CommunicationError_p(ERRC, CAN_ERROR_Can_Bus_is_passive, FALSE, PSTR("TEC: %3i REC: %3i"),
				                             canCurrentTransmitErrorCounter, canCurrentReceiveErrorCounter);
		//    canInit(0); /* CAN reinit*/
	}
}//END of canGetGeneralStatusError function


uint8_t canIsGeneralStatusError( void )
{
	/* testing the following
	 * errors of CANGSTA:
	 * BOFF, ERRP */
	canCurrentGeneralStatus = CANGSTA;
	canCurrentReceiveErrorCounter = CANREC;
	canCurrentTransmitErrorCounter = CANTEC;
	return 0xFF && (canCurrentGeneralStatus & ( 1 << BOFF | 1 << ERRP ));

}//END of canIsMObErrorAndAcknowledge


/*
 *this function prints the various CAN errors
 *the function has no input and output parameters
 */
void canGetMObError( void )
{
	static const uint16_t errorNumbers[5] = { CAN_ERROR_MOb_Acknowledgement_Error,
				                              CAN_ERROR_MOb_Form_Error,
				                              CAN_ERROR_MOb_CRC_Error,
			                                  CAN_ERROR_MOb_Stuff_Error,
			                                  CAN_ERROR_MOb_Bit_Error};

	for (uint8_t  bit = 4; bit >= 0; bit--)
	{
		if ( canCurrentMObStatus & ( 1 << bit ) )
		{
			//	• Bit 0 – AERR: Acknowledgment Error
			//		No detection of the dominant bit in the acknowledge slot.
			//	• Bit 1 – FERR: Form Error
			//		The form error results from one or more violations of the fixed form in the following bit fields:
			//		• CRC delimiter.
			//		• Acknowledgment delimiter.
			//		• EOF
			//	• Bit 2 – CERR: CRC Error
			//		The receiver performs a CRC check on every de-stuffed received message from the start of
			//		frame up to the data field. If this checking does not match with the de-stuffed CRC field, a CRC
			//		error is set.
			//	• Bit 3 – SERR: Stuff Error
			//		Detection of more than five consecutive bits with the same polarity. This flag can generate an
			//		interrupt.
			//	• Bit 4 – BERR: Bit Error (Only in Transmission)
			//		The bit value monitored is different from the bit value sent.
			//		Exceptions: the monitored recessive bit sent as a dominant bit during the arbitration field and the
			//		acknowledge slot detecting a dominant bit during the sending of an error frame.

			can_errorCode = CommunicationError_p(ERRC, errorNumbers[bit], FALSE, PSTR("MOb#: %i CANSTMOB: 0x%x"),
					                             canMob, canCurrentMObStatus);
		}
	}

}//END of canGetMObError

uint8_t canIsMObErrorAndAcknowledge( void )
{
	/* testing (and resetting) the following
	 * errors of CANSTMOB:
	 * BERR, SERR, CERR, FERR, AERR*/

	static uint8_t errorBits = ( 1 << BERR | 1 << SERR | 1 << CERR | 1 << FERR | 1 << AERR );
    uint8_t bit=0;
	canCurrentMObStatus = CANSTMOB;

	// To acknowledge a mob interrupt, the corresponding bits of CANSTMOB register (RXOK,
	// TXOK,...) must be cleared by the software application. This operation needs a read-modify-write
	// software routine.
    //
	// from manual: chapter 19.8.2, p250

	for (bit = 0; bit < sizeof(errorBits); bit++)
	{
		CANSTMOB = CANSTMOB & ~(errorBits & (1 << bit));
	}

	return (canCurrentMObStatus & errorBits);

}//END of canIsMObErrorAndAcknowledge

uint8_t canErrorHandling( void )
{
#warning TODO: detailed error handling needed

	  canGetGeneralStatusError();
	  canGetMObError();
	  return 0;
}

uint8_t canCheckInputParameterError( uartMessage *ptr_uartStruct )
{
	uint8_t error = FALSE;

	//#error missing break statement for number of arguments
	/* type check */
	for ( uint8_t parameterIndex = 0 ; parameterIndex < MAX_PARAMETER ; parameterIndex++ )
	{
		switch (parameterIndex)
		{
		case 1:
			if ( ( 0x7FFFFFF ) < ptr_uartStruct->Uart_Message_ID )
			{
				uart_errorCode = CommunicationError_p(ERRA, SERIAL_ERROR_ID_is_too_long, FALSE, NULL);
				error = TRUE;
				break;
			}
			break;
		case 2:
			if ( ( 0x7FFFFFF ) < ptr_uartStruct->Uart_Mask )
			{
				uart_errorCode = CommunicationError_p(ERRA, SERIAL_ERROR_mask_is_too_long, FALSE, NULL);
				error = TRUE;
				break;
			}
			break;
		case 3:
			if ( ( 1 ) < ptr_uartStruct->Uart_Rtr )
			{
				uart_errorCode = CommunicationError_p(ERRA, SERIAL_ERROR_rtr_is_too_long, FALSE, NULL);
				error = TRUE;
				break;
			}
			break;
		case 4:
			if ( ( 8 ) < ptr_uartStruct->Uart_Length )
			{
				uart_errorCode = CommunicationError_p(ERRA, SERIAL_ERROR_length_is_too_long, FALSE, NULL);
				error = TRUE;
				break;
			}
			break;
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
		case 10:
		case 11:
		case 12:
			for ( uint8_t i = 0 ; i < 8 ; i++ )
			{
				if ( ( 0XFF ) < ptr_uartStruct->Uart_Data[i] )
				{
					uart_errorCode = CommunicationError_p(ERRA, SERIAL_ERROR_data_0_is_too_long + i, 0, NULL);
					error = TRUE;
					break;
				}
			}
			break;
		}

	}

	return error;
}

/*
 * canSetMObCanIDandMask
 * 		sets for the current MOb, set via CANPAGE,
 * 		the
 * 			ID
 * 			MASK
 * 		    flags:
 * 		    	enable RTR mask bit comparison
 * 		    	enable ID Extension mask bit comparison
 * 		Depending on the size (> 2**11) of id and mask either V2.0 part A or part B format is chosen
 */

void canSetMObCanIDandMask(uint32_t id, uint32_t mask, uint8_t enableRTRMaskBitComparison_flag, uint8_t enableIDExtensionMaskBitComparison_flag)
{
    // set identifier to send
    if ( ( MAX_ELEVEN_BIT >= id ) || ( MAX_ELEVEN_BIT >= mask ) )
    {
    	// V2.0 part A
    	CANIDT4 &= 0x5;
        CANIDT3  = 0x0;
    	CANIDT2  = 0xff & ( id << 5 );
        CANIDT1  = 0xff & ( id >> 3 );
    }
    else
    {
    	// V2.0 part B
    	CANIDT4 &= 0xff & (( id <<  3 ) | 0x7)  ;
        CANIDT3  = 0xff & (  id >>  5 );
    	CANIDT2  = 0xff & (  id >> 13 );
        CANIDT1  = 0xff & (  id >> 21 );
    }

    // mask comparison bits
    /* RTR mask bit comparison*/
    if ( enableRTRMaskBitComparison_flag )
    {
    	// compare RTR
    	CANIDM4 |= ( 1 << RTRMSK );
    }
    else
    {
    	// always true
    	CANIDM4 &= ~( 1 << RTRMSK );
    }

    // ID Extension (IDE) mask bit comparison
    if ( enableIDExtensionMaskBitComparison_flag )
    {
    	// compare IDE
    	CANIDM4 |= ( 1 << IDEMSK );
    }
    else
    {
    	// always true
    	CANIDM4 &= ~( 1 << IDEMSK );
    }

    // set identifier Mask
    if ( ( MAX_ELEVEN_BIT >= id ) || ( MAX_ELEVEN_BIT >= mask ) )
    {
    	// V2.0 part A
        CANIDM4 &= 0x5;
        CANIDM3  = 0x0;
        CANIDM2  = 0xff & (( mask & 0x7 ) << 5);
        CANIDM1  = 0xff & (  mask >> 3  );
    }
    else
    {
    	// V2.0 part B
        CANIDM4 &= 0xff & (( mask <<  3 ) | 0x5 );
        CANIDM3  = 0xff & (  mask >>  5 );
        CANIDM2  = 0xff & (  mask >> 13 );
        CANIDM1  = 0xff & (  mask >> 21 );
    }
}

/* this function checks whether all the received parameters are valid
 * the function has a pointer of the serial structure as input and returns no parameter
 * returns TRUE if all checks are passed
 * returns FALSE else
 */

int8_t canCheckParameterCanFormat( struct uartStruct *ptr_uartStruct)
{
   /*check range of parameter*/

   for (uint8_t index = 1; index < MAX_PARAMETER; index ++)
   {
      if ( 0 != *ptr_setParameter[index] ) return FALSE;
   }

   if ( 0x7FFFFFF  < ptr_uartStruct->Uart_Message_ID ) return FALSE;
   if ( 0x7FFFFFF  < ptr_uartStruct->Uart_Mask       ) return FALSE;
   if ( 1          < ptr_uartStruct->Uart_Rtr        ) return FALSE;
   if ( 8          < ptr_uartStruct->Uart_Length     ) return FALSE;
   if ( 0XFF       < ptr_uartStruct->Uart_Data[0]    ) return FALSE;
   if ( 0XFF       < ptr_uartStruct->Uart_Data[1]    ) return FALSE;
   if ( 0XFF       < ptr_uartStruct->Uart_Data[2]    ) return FALSE;
   if ( 0XFF       < ptr_uartStruct->Uart_Data[3]    ) return FALSE;
   if ( 0XFF       < ptr_uartStruct->Uart_Data[4]    ) return FALSE;
   if ( 0XFF       < ptr_uartStruct->Uart_Data[5]    ) return FALSE;
   if ( 0XFF       < ptr_uartStruct->Uart_Data[6]    ) return FALSE;
   if ( 0XFF       < ptr_uartStruct->Uart_Data[7]    ) return FALSE;
   return TRUE;

}
