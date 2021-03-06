/* sd2snes - SD card based universal cartridge for the SNES
   Copyright (C) 2009-2010 Maximilian Rehkopf <otakon@gmx.net>
   AVR firmware portion

   Inspired by and based on code from sd2iec, written by Ingo Korb et al.
   See sdcard.c|h, config.h.

   FAT file system access based on code by ChaN, Jim Brain, Ingo Korb,
   see ff.c|h.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License only.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

   snes.c: SNES hardware control and monitoring
*/

#include <avr/io.h>
#include "avrcompat.h"
#include "config.h"
#include "uart.h"
#include "snes.h"
#include "memory.h"
#include "fileops.h"
#include "ff.h"
#include "led.h"
#include "smc.h"

uint8_t initloop=1;
uint32_t saveram_crc, saveram_crc_old;
void snes_init() {
	DDRD |= _BV(PD5);	// PD5 = RESET_DIR
	DDRD |= _BV(PD6); 	// PD6 = RESET
	snes_reset(1); 
}

/*
 * sets the SNES reset state.
 *
 *  state: put SNES in reset state when 1, release when 0
 */
void snes_reset(int state) {
	if(state) {
		DDRD |= _BV(PD6);	// /RESET pin -> out
		PORTD &= ~_BV(PD6);	// /RESET = 0
		PORTD |= _BV(PD5);	// RESET_DIR = 1;
	} else {
		PORTD &= ~_BV(PD5);	// RESET_DIR = 0;
		DDRD &= ~_BV(PD6);	// /RESET pin -> in
		PORTD |= _BV(PD6);	// /RESET = pullup
	}
}

/*
 * gets the SNES reset state.
 *
 * returns: 1 when reset, 0 when not reset
 */
uint8_t get_snes_reset() {
//	DDRD &= ~_BV(PD6);	// /RESET pin -> in
//	PORTD &= ~_BV(PD5);	// RESET_DIR (external buffer) = 0
	return !(PIND & _BV(PD6));
}

/*
 * SD2SNES main loop.
 * monitors SRAM changes and other things
 */
uint32_t diffcount = 0, samecount = 0, didnotsave = 0;
uint8_t sram_valid = 0;
void snes_main_loop() {
	if(!romprops.ramsize_bytes)return;
	if(initloop) {
		saveram_crc_old = calc_sram_crc(SRAM_SAVE_ADDR, romprops.ramsize_bytes);
		initloop=0;
	}
	saveram_crc = calc_sram_crc(SRAM_SAVE_ADDR, romprops.ramsize_bytes);
	sram_valid = sram_reliable();
	if(crc_valid && sram_valid) {
		if(saveram_crc != saveram_crc_old) {
			if(samecount) {
				diffcount=1;
			} else {
				diffcount++;
				didnotsave++;
			}
			samecount=0;
		}
		if(saveram_crc == saveram_crc_old) {
			samecount++;
		}
		if(diffcount>=1 && samecount==5) {
			uart_putc('U');
			uart_puthexshort(saveram_crc);
			uart_putcrlf();
			set_busy_led(1);
			save_sram(file_lfn, romprops.ramsize_bytes, SRAM_SAVE_ADDR);
			set_busy_led(0);
			didnotsave=0;
		}
		if(didnotsave>50) {
			diffcount=0;
			uart_putc('V');
			set_busy_led(1);
			save_sram(file_lfn, romprops.ramsize_bytes, SRAM_SAVE_ADDR);
			didnotsave=0;	
                        set_busy_led(0);
		}
		saveram_crc_old = saveram_crc;
	}
	dprintf("crc_valid=%d sram_valid=%d diffcount=%ld samecount=%ld, didnotsave=%ld\n", crc_valid, sram_valid, diffcount, samecount, didnotsave);
}

/*
 * SD2SNES menu loop.
 * monitors menu selection. return when selection was made.
 */
uint8_t menu_main_loop() {
	uint8_t cmd = 0;
	sram_writebyte(0, SRAM_CMD_ADDR);
	while(!cmd) {
		if(!get_snes_reset()) {
			while(!sram_reliable());
			cmd = sram_readbyte(SRAM_CMD_ADDR);
		}
		if(get_snes_reset()) {
			cmd = 0;
		}
	}
	return cmd;
}

void get_selected_name(uint8_t* fn) {
	uint32_t addr = sram_readlong(SRAM_FD_ADDR);
	dprintf("fd addr=%lX\n", addr);
	sram_readblock(fn, addr+0x41+SRAM_MENU_ADDR, 256);
}
