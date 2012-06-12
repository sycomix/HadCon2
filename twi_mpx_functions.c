#include <stdint.h>
#include "twi_mpx_functions.h"
#include "twi_master.h"

void Twim_Mpx_Switch_Channel(uint8_t Channel)
{
	twi_data[0] = Channel;
	twi_bytes_to_transceive = 1;
	Twim_Write_Data(TWI_MPX_ADDRESS, twi_bytes_to_transceive, twi_data);
}
