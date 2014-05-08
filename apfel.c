/*
 * apfel.c
 *
 *  Created on: 12.06.2013
 *      Author: Florian Brabetz, GSI, f.brabetz@gsi.de
 */


#include <stdio.h>
#include "api.h"
#include "apfel.h"
#include "read_write_register.h"
#include <avr/io.h>
#include <stdbool.h>
#include <util/delay.h>

/*eclipse specific setting, not used during build process*/
#ifndef __AVR_AT90CAN128__
#include <avr/iocan128.h>
#endif

// initial configuration for chipselectarray -> all chipselects are unused
apfelPin apfelChipSelectArray[APFEL_CHIPSELECT_MAXIMUM] = { { 0 , 0 , false  },
					    { 0 , 0 , false  },
					    { 0 , 0 , false  },
					    { 0 , 0 , false  },
					    { 0 , 0 , false  },
					    { 0 , 0 , false  },
					    { 0 , 0 , false  },
					    { 0 , 0 , false  } };

uint8_t apfelInternalChipSelectMask = 0;




//----------------
void apfelInit(void)
{
	uint8_t i;

	apfelUsToDelay = APFEL_DEFAULT_US_TO_DELAY;

}

apiCommandResult apfelWritePortA(uint8_t value, uint8_t mask)
{
	return apfelWritePort(value, PORTA, mask);
}

apiCommandResult apfelWritePort(uint8_t value, uint8_t portAddress, uint8_t mask)
{
	uint8_t readback_register = 0xFF; //dummy value;
	//uint8_t read = REGISTER_READ_FROM_8BIT_REGISTER(portAddress);
	uint8_t read = registerReadFrom8bitRegister(portAddress);

	// just modify the masked out bits and keep the rest as is:
	// read & (~mask) | ( value & mask ) => read ^ (mask & ( read ^ value))
	uint8_t set = read ^ ( mask & ( read ^ value));
	//readback_register = REGISTER_WRITE_INTO_8BIT_REGISTER_AND_READBACK(portAddress, set);
	readback_register = registerWriteInto8bitRegister(portAddress, set);
    _delay_us(apfelUsToDelay);

	if (readback_register == set)
	{
		return apiCommandResult_SUCCESS_QUIET;
	}
	else
	{
		return apiCommandResult_FAILURE;
	}
}

apiCommandResult apfelReadPortA(uint8_t *value)
{
	return apfelReadPort(value, PORTA);
}

apiCommandResult apfelReadPort(uint8_t *value, uint8_t portAddress)
{
	uint8_t apiCommandResult = apiCommandResult_FAILURE;

	if (NULL == value)
	{
		CommunicationError_p(ERRA, GENERAL_ERROR_value_has_invalid_type, TRUE, NULL);
		return apiCommandResult_FAILURE_QUIET;
	}

	*value = registerReadFrom8bitRegister(portAddress);
    _delay_us(apfelUsToDelay);
	return apiCommandResult_SUCCESS_QUIET;
}

apiCommandResult apfelWriteBit(uint8_t bit, uint8_t portAddress, uint8_t ss, uint8_t pinCLK, uint8_t pinDOUT, uint8_t pinSS)
{
	if (pinCLK > 7 || pinDOUT > 7 || pinSS > 7)
	{
		return apiCommandResult_FAILURE_QUIET;
	}

	uint8_t mask = (1 << pinCLK | 1 << pinDOUT | 1 << pinSS);

	bit = bit?1:0;
    ss  = ss?1:0;

	//clock Low + data low
	if ( false == apfelSetClockAndDataLine( portAddress, ((uint8_t)(0 << pinCLK | 0   << pinDOUT | ss << pinSS)), mask)) { return apiCommandResult_FAILURE_QUIET; }
	//# clock Low  + data "bit"
	if ( false == apfelSetClockAndDataLine( portAddress, ((uint8_t)(0 << pinCLK | bit << pinDOUT | ss << pinSS)), mask)) { return apiCommandResult_FAILURE_QUIET; }
	//# clock High + data "bit"
	if ( false == apfelSetClockAndDataLine( portAddress, ((uint8_t)(1 << pinCLK | bit << pinDOUT | ss << pinSS)), mask)) { return apiCommandResult_FAILURE_QUIET; }
	//# clock High + data low
	if ( false == apfelSetClockAndDataLine( portAddress, ((uint8_t)(1 << pinCLK | 0   << pinDOUT | ss << pinSS)), mask)) { return apiCommandResult_FAILURE_QUIET; }
	//# clock low  + data low
	if ( false == apfelSetClockAndDataLine( portAddress, ((uint8_t)(0 << pinCLK | 0   << pinDOUT | ss << pinSS)), mask)) { return apiCommandResult_FAILURE_QUIET; }

#if 0
	//clock Low + data low
	if (result < apiCommandResult_FAILURE)
	{
		result = apfelWritePort((0 << pinCLK | 0 << pinDOUT | ss << pinSS), portAddress, mask);
	}
	//# clock Low  + data high
	if (result < apiCommandResult_FAILURE)
	{
		result = apfelWritePort((0 << pinCLK | bit << pinDOUT | ss << pinSS), portAddress, mask);
	}
	//# clock High + data high
	if (result < apiCommandResult_FAILURE)
	{
		result = apfelWritePort((1 << pinCLK | bit << pinDOUT | ss << pinSS), portAddress, mask);
	}
	//# clock High + data low
	if (result < apiCommandResult_FAILURE)
	{
		result = apfelWritePort((1 << pinCLK | 0 << pinDOUT | ss << pinSS), portAddress, mask);
	}
	//# clock low  + data low
	if (result < apiCommandResult_FAILURE)
	{
		result = apfelWritePort((0 << pinCLK | 0 << pinDOUT | ss << pinSS), portAddress, mask);
	}
#endif

	return apiCommandResult_SUCCESS_QUIET;
}

apiCommandResult readBitSequence(uint8_t nBits, uint32_t* bits, uint8_t portAddress, uint8_t ss, uint8_t pinDIN, uint8_t pinCLK, uint8_t pinSS)
{
	if (pinCLK > 7 || pinDIN > 7 || pinSS > 7)
	{
		return apiCommandResult_FAILURE_QUIET;
	}
	if (NULL == bits)
	{
		return apiCommandResult_FAILURE_QUIET;
	}
	if (32 < nBits)
	{
		return apiCommandResult_FAILURE_QUIET;
	}

	ss = ss?1:0;
    *bits = 0;

	//make sure we start from clock 0
	if ( false == apfelSetClockAndDataLine(portAddress, (uint8_t)(0 << pinCLK | ss << pinSS), (uint8_t)(1 << pinCLK)))
	{
		return apiCommandResult_FAILURE_QUIET;
	}

	// initial bit read differently !!! TNX^6 Peter Wieczorek ;-)
	// since apfel needs first falling clock edge to activate the output pad
	// therefore first cycle then read data.
	//clock high
	if ( false == apfelSetClockAndDataLine(portAddress, (uint8_t)(1 << pinCLK | ss << pinSS), (uint8_t)(1 << pinCLK)))
	{
		return apiCommandResult_FAILURE_QUIET;
	}
	//clock low
	if ( false == apfelSetClockAndDataLine(portAddress, (uint8_t)(0 << pinCLK | ss << pinSS), (uint8_t)(1 << pinCLK)))
	{
		return apiCommandResult_FAILURE_QUIET;
	}

	//read first data bit in
    //LSB first

	*bits |= apfelGetDataInLine( portAddress, pinDIN );

	#warning check correct output
    //MSB first
	//*bits |= apfelGetDataInLine( portAddress, pinDIN ) << (nBits -1) ;

	//read remaining (max 31 bits)
	for (uint8_t n=1; n<nBits-1; n++)
	{
		//clock high
		if ( false == apfelSetClockAndDataLine(portAddress, (uint8_t)(1 << pinCLK | ss << pinSS), (uint8_t)(1 << pinCLK)))
		{
			return apiCommandResult_FAILURE_QUIET;
		}

		//read data bit in
	    //LSB first

		*bits |= apfelGetDataInLine( portAddress, pinDIN ) << n;
#warning check correct output
		//MSB first
		//*bits |= apfelGetDataInLine( portAddress, pinDIN ) << (nBits -1 - n) ;
		//_delay_us(apfelUsToDelay);

		//clock low
		if ( false == apfelSetClockAndDataLine(portAddress, (uint8_t)(0 << pinCLK | ss << pinSS), (uint8_t)(1 << pinCLK)))
		{
			return apiCommandResult_FAILURE_QUIET;
		}
	}

	return apiCommandResult_SUCCESS_QUIET;
}

apiCommandResult apfelWriteClockSequence(uint8_t num, uint8_t portAddress, uint8_t ss, uint8_t pinCLK, uint8_t pinDOUT, uint8_t pinSS)
{
	while (num)
	{
		if (apiCommandResult_FAILURE < apfelWriteBit(0, portAddress, ss, pinCLK, pinDOUT, pinSS))
		{
			return apiCommandResult_FAILURE_QUIET;
		}
		num--;
	}
	return apiCommandResult_SUCCESS_QUIET;
}

uint8_t apfelSetClockAndDataLine( uint8_t portAddress, uint8_t value, uint8_t mask)
{
	uint8_t set = ((REGISTER_READ_FROM_8BIT_REGISTER(portAddress) & (~mask)) | (value & mask ));
	if (set && mask != mask && (REGISTER_WRITE_INTO_8BIT_REGISTER_AND_READBACK(portAddress, set)))
	{
		return false;
	}
	_delay_us(apfelUsToDelay);
	return true;
}


apiCommandResult apfelClearDataInput(uint8_t num, uint8_t portAddress, uint8_t ss, uint8_t pinCLK, uint8_t pinDOUT, uint8_t pinSS)
{
	if (pinCLK > 7 || pinDOUT > 7 || pinSS > 7)
	{
		return apiCommandResult_FAILURE_QUIET;
	}

	uint8_t mask = (1 << pinCLK | 1 << pinDOUT | 1 << pinSS);

	static uint8_t nBits = APFEL_N_COMMAND_BITS + APFEL_N_VALUE_BITS + APFEL_N_CHIP_ID_BITS;

    ss  = ss?1:0;

    for (int i = 0; i < nBits; i++)
    {
    	//clock Low + data low
    	if ( false == apfelSetClockAndDataLine( portAddress, ((uint8_t)(0 << pinCLK | 0   << pinDOUT | ss << pinSS)), mask)) { return apiCommandResult_FAILURE_QUIET; }

    	//clock Low + data low
    	if ( false == apfelSetClockAndDataLine( portAddress, ((uint8_t)(0 << pinCLK | 0   << pinDOUT | ss << pinSS)), mask)) { return apiCommandResult_FAILURE_QUIET; }
    	_delay_us(apfelUsToDelay);

    	apfelWriteClockSequence(1, portAddress, ss, pinCLK, pinDOUT, pinSS);
    }

    // tuning
    for (int i = 0; i < 10; i++)
    {
    	//clock Low + data low
    	if ( false == apfelSetClockAndDataLine( portAddress, ((uint8_t)(0 << pinCLK | 0   << pinDOUT | ss << pinSS)), mask)) { return apiCommandResult_FAILURE_QUIET; }
    	_delay_us(apfelUsToDelay);
    }

	return apiCommandResult_SUCCESS_QUIET;
}

//writeBitSequence #bits #data #endianess (0: little, 1:big)
apiCommandResult apfelWriteBitSequence(uint8_t num, uint32_t data, uint8_t portAddress, uint8_t ss, uint8_t pinCLK, uint8_t pinDOUT, uint8_t pinSS)
{
	if (pinCLK > 7 || pinDOUT > 7 || pinSS > 7)
	{
		return apiCommandResult_FAILURE_QUIET;
	}

	while (num)
	{
		if ( apiCommandResult_FAILURE > apfelWriteBit( (data >> ( num -1 ) ) & 0x1 , portAddress, ss, pinCLK, pinDOUT, pinSS))
		{
			return apiCommandResult_FAILURE_QUIET;
		}
		num--;
	}

	return apiCommandResult_SUCCESS_QUIET;
}

/*
 * High Level commands
 */
