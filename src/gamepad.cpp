/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: Copyright (c) 2021 Jason Skuby (mytechtoybox.com)
 */

// GP2040 Libraries
#include "gamepad.h"
#include "snes_rx.pio.h"
#include "storagemanager.h"

#include "FlashPROM.h"
#include "CRC32.h"

// MUST BE DEFINED for mpgs
uint32_t getMillis() {
	return to_ms_since_boot(get_absolute_time());
}

uint64_t getMicro() {
	return to_us_since_boot(get_absolute_time());
}

// NeoPico might use pio0. Hopefully we won't enable NeoPico, but switch to pio1 just
// to be safe.
static const PIO MY_PIO = pio1;

// 6 and 7 are clock and strobe.
constexpr static uint PIN_CLK = 6;
constexpr static uint PIN_DATA = 8;

void Gamepad::setup()
{
	load(); // MPGS loads

	// Configure pin mapping
	f2Mask = (GAMEPAD_MASK_A1 | GAMEPAD_MASK_S2);

	const PIO pio = MY_PIO;

	// https://github.com/raspberrypi/pico-examples/blob/master/pio/hello_pio/hello.c
	// Our assembled program needs to be loaded into this PIO's instruction
	// memory. This SDK function will find a location (offset) in the
	// instruction memory where there is enough space for our program. We need
	// to remember this location!
	this->offset = pio_add_program(pio, &snes_rx_program);

	// Find a free state machine on our chosen PIO (erroring if there are
	// none). Configure it to run our program, and start it, using the
	// helper function we included in our .pio file.
	this->sm = pio_claim_unused_sm(pio, true);
	snes_rx_program_init(MY_PIO, sm, offset, PIN_CLK, PIN_DATA);

	#ifdef PIN_SETTINGS
		gpio_init(PIN_SETTINGS);             // Initialize pin
		gpio_set_dir(PIN_SETTINGS, GPIO_IN); // Set as INPUT
		gpio_pull_up(PIN_SETTINGS);          // Set as PULLUP
	#endif
}

void Gamepad::process()
{
	memcpy(&rawState, &state, sizeof(GamepadState));
	MPGS::process();
}

void Gamepad::read()
{
	// TODO blocking or PIO read from shift register
	auto values = snes_rx_program_get(MY_PIO, this->sm);

	#ifdef PIN_SETTINGS
	state.aux = 0
		| ((values & (1 << PIN_SETTINGS)) ? (1 << 0) : 0)
	;
	#endif

	// https://en.wikibooks.org/wiki/Super_NES_Programming/Joypad_Input#Joypad_Registers
	state.dpad = 0
		| ((values & 0x0800) ? GAMEPAD_MASK_UP    : 0)
		| ((values & 0x0400) ? GAMEPAD_MASK_DOWN  : 0)
		| ((values & 0x0200) ? GAMEPAD_MASK_LEFT  : 0)
		| ((values & 0x0100) ? GAMEPAD_MASK_RIGHT : 0)
	;

	state.buttons = 0
		| ((values & 0x8000) ? GAMEPAD_MASK_B1 : 0) // Switch B
		| ((values & 0x4000) ? GAMEPAD_MASK_B3 : 0) // Switch Y
		| ((values & 0x2000) ? GAMEPAD_MASK_S1 : 0) // PS3 Select
		| ((values & 0x1000) ? GAMEPAD_MASK_S2 : 0) // PS3 Start

		| ((values & 0x0080) ? GAMEPAD_MASK_B2 : 0) // Switch A
		| ((values & 0x0040) ? GAMEPAD_MASK_B4 : 0) // Switch X
		| ((values & 0x0020) ? GAMEPAD_MASK_L1 : 0) // Switch L
		| ((values & 0x0010) ? GAMEPAD_MASK_R1 : 0) // Switch R
	;
	state.lx = GAMEPAD_JOYSTICK_MID;
	state.ly = GAMEPAD_JOYSTICK_MID;
	state.rx = GAMEPAD_JOYSTICK_MID;
	state.ry = GAMEPAD_JOYSTICK_MID;
	state.lt = 0;
	state.rt = 0;
}


/* Gamepad stuffs */
void GamepadStorage::start()
{
	//EEPROM.start();
}

void GamepadStorage::save()
{
	EEPROM.commit();
}

GamepadOptions GamepadStorage::getGamepadOptions()
{
	GamepadOptions options;
	EEPROM.get(GAMEPAD_STORAGE_INDEX, options);

	uint32_t lastCRC = options.checksum;
	options.checksum = CHECKSUM_MAGIC;
	if (CRC32::calculate(&options) != lastCRC)
	{
		options.inputMode = InputMode::INPUT_MODE_XINPUT; // Default?
		options.dpadMode = DpadMode::DPAD_MODE_DIGITAL; // Default?
#ifdef DEFAULT_SOCD_MODE
		options.socdMode = DEFAULT_SOCD_MODE;
#else
		options.socdMode = SOCD_MODE_NEUTRAL;
#endif
		options.invertXAxis = false;
		options.invertYAxis = false;
		setGamepadOptions(options);
	}

	return options;
}

void GamepadStorage::setGamepadOptions(GamepadOptions options)
{
	options.checksum = 0;
	options.checksum = CRC32::calculate(&options);
	EEPROM.set(GAMEPAD_STORAGE_INDEX, options);
}

