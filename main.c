/**
 * @mainpage
 * ISPnub firmware project
 *
 * @section description Description
 * ISPnub is a stand-alone AVR programming module. The programming instructions
 * are defined within scripts. These scripts are converted with an additional
 * tool (ISPnubCreator) into binary information and are stored in flash. This
 * firmware interprets this binary programming instructions.
 * 
 * The firmware hex file is packed into the JAR file of ISPnubCreator which
 * merges the firmware hex data with programming instructions from scripts.
 * 
 * Environment:
 * - Target: ATmega1284P + ATmega16, ATmega32, ATmega644 (all TQFP-44)
 *           for testing ATmega328P@ext. 16MHZ Oscillator is supported but it has to be flashed via ISP (not bootloader) to work
 * - Compiler: avr-gcc (GCC) 4.9.2
 * 
 * @section history Change History
 * - ISPnubMod v1.0, based on ISPnub 1.3 (2017-02-26)
 *   - Improvements for Battery-Powered Devices
 *   - Added Buzzer
 *   - Fix: Made slowticker volatile
 *   - Added HAL for other TQFP44-AVRs
 *   - Added Yellow LED (red is only for errors)
 *
 */

/**
 * @file main.c
 *
 * @brief This file contains the main routine with application entry point for
 *        the ISPnub firmware project
 *
 * @author Thomas Fischl, Michael G.
 * @copyright (c) 2013-2014, 2017 Thomas Fischl, Michael G.
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#include "main.h"
#include "hal.h"
#include <inttypes.h>
#include <stdio.h>
#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include "clock.h"
#include "isp.h"
#include "counter.h"
#include "script.h"
#include "debounce.h"


/**
 * @brief Main routine of firmware and application entry point
 * @return Application return code
 */
int main(void) {

    hal_init();
    clock_init();
	set_sleep_mode(SLEEP_MODE_PWR_DOWN);
	
	
    uint8_t ticker10MS = clock_getTickerFast();
    uint8_t ticker250MS = clock_getTickerSlow();
	uint8_t sleeptimer = clock_getTickerSlow();
	
    uint8_t success = 1;
    
	uint8_t buzzer = 0;		//time to turn on in multiple by 10ms
	uint8_t toggle250MS = 0;
	
	uint8_t state = S_INIT;
	
    // enable interrupts
    sei();
	
    // main loop	
    while (1) {
		
		
		//slow ticks
		if (clock_getTickerSlowDiff(ticker250MS) > CLOCK_TICKER_SLOW_250MS) {
            ticker250MS = clock_getTickerSlow();
			
            toggle250MS = !toggle250MS;
		}
		
		//fast ticks
		if (clock_getTickerFastDiff(ticker10MS) > CLOCK_TICKER_FAST_10MS) {
            ticker10MS = clock_getTickerFast();
			
			tickDebounce();
			
			if(buzzer>0)
				buzzer--;
		}
		
		//buzzer
		if (buzzer==0) {
			hal_setBuzzer(0);
		} else {
			hal_setBuzzer(1);
		}
		//led signaling
		switch(state) {
			case S_INIT:
			case S_WAKEUP:
				hal_setLEDgreen(1);
				hal_setLEDyellow(0);
				hal_setLEDred(0);
				hal_setBuzzer(0);
				
				
				
				break;
				
			case S_IDLE:
				if(success==1) {
					hal_setLEDgreen(1);
					hal_setLEDyellow(0);
					hal_setLEDred(0);
				} else {
					hal_setLEDgreen(0);
					hal_setLEDyellow(0);
					hal_setLEDred(toggle250MS);
					
				}
				break;
			
			case S_PROGRAMMING:
				hal_setLEDgreen(0);
				#if COMPATIBILITY_MODE_ISPNUB_ORIGINAL == 1 || DONT_USE_YELLOW_LED == 1
					hal_setLEDyellow(0);
					hal_setLEDred(1);
				#else
					hal_setLEDyellow(1);
					hal_setLEDred(0);
				#endif
				
				break;
			
			case S_NO_MORE:
				hal_setLEDgreen(toggle250MS);
				hal_setLEDyellow(0);
				hal_setLEDred(toggle250MS);
				
				break;
				
			case S_NO_PROGRAM:
				hal_setLEDgreen(!toggle250MS);
				hal_setLEDyellow(0);
				hal_setLEDred(toggle250MS);
				
				break;
			case S_SLEEP:
				//disable all signals for max. power-saving
				hal_setLEDgreen(0);
				hal_setLEDyellow(0);
				hal_setLEDred(0);
				hal_setBuzzer(0);
				
				break;
		}
		
		
		
		//processing
		switch(state) {
			case S_INIT:
			case S_WAKEUP:

				//remaining cycles to program?
				if(counter_read()==0) {
					state=S_NO_MORE;
				}
				
				
				state=S_IDLE;
				break;
			
			case S_IDLE:
				
				
				if( get_key_press(1 << IO_SWITCH ) || get_key_press(1 << IO_EXT_SWITCH) ) {
					sleeptimer=clock_getTickerSlow();
					
					if(counter_read()>0) {
						state=S_PROGRAMMING;
					} else {
						buzzer=60;
						state=S_NO_MORE;
					}
				}
				
				break;
			case S_PROGRAMMING:
				
				success = script_run();
				
				if(success==1) {		// programming OK
					buzzer=3;
					state=S_IDLE;
				} else if(success==0) {	// programming failed (connection, wrong avr, ...)
					buzzer=30;
					state=S_IDLE;
				} else {				// programming failed due to missing program
					buzzer=60;
					state=S_NO_PROGRAM;
				}
				
				//update sleeptimer to prevent falling asleep after a long-lasting programming task
				sleeptimer=clock_getTickerSlow();
				
				break;

			case S_NO_MORE:
			case S_NO_PROGRAM:
				//nothing to do any more (except from going to sleep)...
				
				break;
				
			case S_SLEEP:
				
				//enable interrupts for wakeup
				hal_enableINT0();
				hal_enableINT1();
				
				sleep_enable();
#if HAS_DYNAMIC_BOD_CONTROL == 1
				sleep_bod_disable();
#endif
				sei();
				sleep_cpu();
				
				//execution is resumed here after processing interrupt
				
				sleep_disable();
				
				//update timer to prevent direct entry of sleepmode after wakeup 
				sleeptimer=clock_getTickerSlow();
				
				state=S_WAKEUP;
				
				
				break;
		}
		
		
		//go to sleep?
		cli();	//for atomic check of condition
		if (clock_getTickerSlowDiff(sleeptimer) > CLOCK_TICKER_SLOW_8S) {
			state=S_SLEEP; 	//go to sleep
		} else {
			sei();			//stay awake
		}

		
    }

    return (0);
}


ISR(INT1_vect) {
	hal_disableINT1();		//disable interrupt, since its a level interrupt, fired as long as switch is hold.
}

ISR(INT0_vect) {
	hal_disableINT0();		//disable interrupt, since its a level interrupt, fired as long as switch is hold.
}