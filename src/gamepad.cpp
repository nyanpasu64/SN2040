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

static HIDReport hidReport
{
	.square_btn = 0, .cross_btn = 0, .circle_btn = 0, .triangle_btn = 0,
	.l1_btn = 0, .r1_btn = 0, .l2_btn = 0, .r2_btn = 0,
	.select_btn = 0, .start_btn = 0, .l3_btn = 0, .r3_btn = 0, .ps_btn = 0,
	.direction = 0x08,
	.l_x_axis = 0x80, .l_y_axis = 0x80, .r_x_axis = 0x80, .r_y_axis = 0x80,
	.right_axis = 0x00, .left_axis = 0x00, .up_axis = 0x00, .down_axis = 0x00,
	.triangle_axis = 0x00, .circle_axis = 0x00, .cross_axis = 0x00, .square_axis = 0x00,
	.l1_axis = 0x00, .r1_axis = 0x00, .l2_axis = 0x00, .r2_axis = 0x00
};

static SwitchReport switchReport
{
	.buttons = 0,
	.hat = SWITCH_HAT_NOTHING,
	.lx = SWITCH_JOYSTICK_MID,
	.ly = SWITCH_JOYSTICK_MID,
	.rx = SWITCH_JOYSTICK_MID,
	.ry = SWITCH_JOYSTICK_MID,
	.vendor = 0,
};

static XInputReport xinputReport
{
	.report_id = 0,
	.report_size = XINPUT_ENDPOINT_SIZE,
	.buttons1 = 0,
	.buttons2 = 0,
	.lt = 0,
	.rt = 0,
	.lx = GAMEPAD_JOYSTICK_MID,
	.ly = GAMEPAD_JOYSTICK_MID,
	.rx = GAMEPAD_JOYSTICK_MID,
	.ry = GAMEPAD_JOYSTICK_MID,
	._reserved = { },
};

// NeoPico might use pio0. Hopefully we won't enable NeoPico, but switch to pio1 just
// to be safe.
static const PIO MY_PIO = pio1;

// 6 and 7 are clock and strobe.
constexpr static uint PIN_CLK = 6;
constexpr static uint PIN_DATA = 8;

void Gamepad::setup()
{
	//load(); // MPGS loads
	options = mpgStorage->getGamepadOptions();

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

	state.dpad = runSOCDCleaner(options.socdMode, state.dpad);

	switch (options.dpadMode)
	{
		case DpadMode::DPAD_MODE_LEFT_ANALOG:
			if (!hasRightAnalogStick) {
				state.rx = GAMEPAD_JOYSTICK_MID;
				state.ry = GAMEPAD_JOYSTICK_MID;
			}
			state.lx = dpadToAnalogX(state.dpad);
			state.ly = dpadToAnalogY(state.dpad);
			state.dpad = 0;
			break;

		case DpadMode::DPAD_MODE_RIGHT_ANALOG:
			if (!hasLeftAnalogStick) {
				state.lx = GAMEPAD_JOYSTICK_MID;
				state.ly = GAMEPAD_JOYSTICK_MID;
			}
			state.rx = dpadToAnalogX(state.dpad);
			state.ry = dpadToAnalogY(state.dpad);
			state.dpad = 0;
			break;

		default:
			if (!hasLeftAnalogStick) {
				state.lx = GAMEPAD_JOYSTICK_MID;
				state.ly = GAMEPAD_JOYSTICK_MID;
			}
			if (!hasRightAnalogStick) {
				state.rx = GAMEPAD_JOYSTICK_MID;
				state.ry = GAMEPAD_JOYSTICK_MID;
			}
			break;
	}
}

void Gamepad::read()
{
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

void Gamepad::debounce() {
	debouncer.debounce(&state);
}

void Gamepad::save()
{
	bool dirty = false;
	GamepadOptions savedOptions = mpgStorage->getGamepadOptions();
	if (memcmp(&savedOptions, &options, sizeof(GamepadOptions)))
	{
		mpgStorage->setGamepadOptions(options);
		dirty = true;
	}

	if (dirty)
		mpgStorage->save();
}

GamepadHotkey Gamepad::hotkey()
{
	static GamepadHotkey lastAction = HOTKEY_NONE;
	GamepadHotkey action = HOTKEY_NONE;
	if (pressedF1())
	{
		switch (state.dpad & GAMEPAD_MASK_DPAD)
		{
			case GAMEPAD_MASK_LEFT:
				action = HOTKEY_DPAD_LEFT_ANALOG;
				options.dpadMode = DPAD_MODE_LEFT_ANALOG;
				state.dpad = 0;
				state.buttons &= ~(f1Mask);
				break;

			case GAMEPAD_MASK_RIGHT:
				action = HOTKEY_DPAD_RIGHT_ANALOG;
				options.dpadMode = DPAD_MODE_RIGHT_ANALOG;
				state.dpad = 0;
				state.buttons &= ~(f1Mask);
				break;

			case GAMEPAD_MASK_DOWN:
				action = HOTKEY_DPAD_DIGITAL;
				options.dpadMode = DPAD_MODE_DIGITAL;
				state.dpad = 0;
				state.buttons &= ~(f1Mask);
				break;

			case GAMEPAD_MASK_UP:
				action = HOTKEY_HOME_BUTTON;
				state.dpad = 0;
				state.buttons &= ~(f1Mask);
				state.buttons |= GAMEPAD_MASK_A1; // Press the Home button
				break;
		}
	}
	else if (pressedF2())
	{
		switch (state.dpad & GAMEPAD_MASK_DPAD)
		{
			case GAMEPAD_MASK_DOWN:
				action = HOTKEY_SOCD_NEUTRAL;
				options.socdMode = SOCD_MODE_NEUTRAL;
				state.dpad = 0;
				state.buttons &= ~(f2Mask);
				break;

			case GAMEPAD_MASK_UP:
				action = HOTKEY_SOCD_UP_PRIORITY;
				options.socdMode = SOCD_MODE_UP_PRIORITY;
				state.dpad = 0;
				state.buttons &= ~(f2Mask);
				break;

			case GAMEPAD_MASK_LEFT:
				action = HOTKEY_SOCD_LAST_INPUT;
				options.socdMode = SOCD_MODE_SECOND_INPUT_PRIORITY;
				state.dpad = 0;
				state.buttons &= ~(f2Mask);
				break;

			case GAMEPAD_MASK_RIGHT:
				if (lastAction != HOTKEY_INVERT_Y_AXIS)
					options.invertYAxis = !options.invertYAxis;
				action = HOTKEY_INVERT_Y_AXIS;
				state.dpad = 0;
				state.buttons &= ~(f2Mask);
				break;
		}
	}

	GamepadHotkey hotkey = action;
	if (hotkey != GamepadHotkey::HOTKEY_NONE)
		save();

	return hotkey;
}


void * Gamepad::getReport()
{
	switch (options.inputMode)
	{
		case INPUT_MODE_XINPUT:
			return getXInputReport();

		case INPUT_MODE_SWITCH:
			return getSwitchReport();

		default:
			return getHIDReport();
	}
}


uint16_t Gamepad::getReportSize()
{
	switch (options.inputMode)
	{
		case INPUT_MODE_XINPUT:
			return sizeof(XInputReport);

		case INPUT_MODE_SWITCH:
			return sizeof(SwitchReport);

		default:
			return sizeof(HIDReport);
	}
}


HIDReport *Gamepad::getHIDReport()
{
	switch (state.dpad & GAMEPAD_MASK_DPAD)
	{
		case GAMEPAD_MASK_UP:                        hidReport.direction = HID_HAT_UP;        break;
		case GAMEPAD_MASK_UP | GAMEPAD_MASK_RIGHT:   hidReport.direction = HID_HAT_UPRIGHT;   break;
		case GAMEPAD_MASK_RIGHT:                     hidReport.direction = HID_HAT_RIGHT;     break;
		case GAMEPAD_MASK_DOWN | GAMEPAD_MASK_RIGHT: hidReport.direction = HID_HAT_DOWNRIGHT; break;
		case GAMEPAD_MASK_DOWN:                      hidReport.direction = HID_HAT_DOWN;      break;
		case GAMEPAD_MASK_DOWN | GAMEPAD_MASK_LEFT:  hidReport.direction = HID_HAT_DOWNLEFT;  break;
		case GAMEPAD_MASK_LEFT:                      hidReport.direction = HID_HAT_LEFT;      break;
		case GAMEPAD_MASK_UP | GAMEPAD_MASK_LEFT:    hidReport.direction = HID_HAT_UPLEFT;    break;
		default:                                     hidReport.direction = HID_HAT_NOTHING;   break;
	}

	hidReport.cross_btn    = pressedB1();
	hidReport.circle_btn   = pressedB2();
	hidReport.square_btn   = pressedB3();
	hidReport.triangle_btn = pressedB4();
	hidReport.l1_btn       = pressedL1();
	hidReport.r1_btn       = pressedR1();
	hidReport.l2_btn       = pressedL2();
	hidReport.r2_btn       = pressedR2();
	hidReport.select_btn   = pressedS1();
	hidReport.start_btn    = pressedS2();
	hidReport.l3_btn       = pressedL3();
	hidReport.r3_btn       = pressedR3();
	hidReport.ps_btn       = pressedA1();
//	hidReport.cross_btn = pressedA2();

	hidReport.l_x_axis = static_cast<uint8_t>(state.lx >> 8);
	hidReport.l_y_axis = static_cast<uint8_t>(state.ly >> 8);
	hidReport.r_x_axis = static_cast<uint8_t>(state.rx >> 8);
	hidReport.r_y_axis = static_cast<uint8_t>(state.ry >> 8);

	return &hidReport;
}


SwitchReport *Gamepad::getSwitchReport()
{
	switch (state.dpad & GAMEPAD_MASK_DPAD)
	{
		case GAMEPAD_MASK_UP:                        switchReport.hat = SWITCH_HAT_UP;        break;
		case GAMEPAD_MASK_UP | GAMEPAD_MASK_RIGHT:   switchReport.hat = SWITCH_HAT_UPRIGHT;   break;
		case GAMEPAD_MASK_RIGHT:                     switchReport.hat = SWITCH_HAT_RIGHT;     break;
		case GAMEPAD_MASK_DOWN | GAMEPAD_MASK_RIGHT: switchReport.hat = SWITCH_HAT_DOWNRIGHT; break;
		case GAMEPAD_MASK_DOWN:                      switchReport.hat = SWITCH_HAT_DOWN;      break;
		case GAMEPAD_MASK_DOWN | GAMEPAD_MASK_LEFT:  switchReport.hat = SWITCH_HAT_DOWNLEFT;  break;
		case GAMEPAD_MASK_LEFT:                      switchReport.hat = SWITCH_HAT_LEFT;      break;
		case GAMEPAD_MASK_UP | GAMEPAD_MASK_LEFT:    switchReport.hat = SWITCH_HAT_UPLEFT;    break;
		default:                                     switchReport.hat = SWITCH_HAT_NOTHING;   break;
	}

	switchReport.buttons = 0
		| (pressedB1() ? SWITCH_MASK_B       : 0)
		| (pressedB2() ? SWITCH_MASK_A       : 0)
		| (pressedB3() ? SWITCH_MASK_Y       : 0)
		| (pressedB4() ? SWITCH_MASK_X       : 0)
		| (pressedL1() ? SWITCH_MASK_L       : 0)
		| (pressedR1() ? SWITCH_MASK_R       : 0)
		| (pressedL2() ? SWITCH_MASK_ZL      : 0)
		| (pressedR2() ? SWITCH_MASK_ZR      : 0)
		| (pressedS1() ? SWITCH_MASK_MINUS   : 0)
		| (pressedS2() ? SWITCH_MASK_PLUS    : 0)
		| (pressedL3() ? SWITCH_MASK_L3      : 0)
		| (pressedR3() ? SWITCH_MASK_R3      : 0)
		| (pressedA1() ? SWITCH_MASK_HOME    : 0)
		| (pressedA2() ? SWITCH_MASK_CAPTURE : 0)
	;

	switchReport.lx = static_cast<uint8_t>(state.lx >> 8);
	switchReport.ly = static_cast<uint8_t>(state.ly >> 8);
	switchReport.rx = static_cast<uint8_t>(state.rx >> 8);
	switchReport.ry = static_cast<uint8_t>(state.ry >> 8);

	return &switchReport;
}


XInputReport *Gamepad::getXInputReport()
{
	xinputReport.buttons1 = 0
		| (pressedUp()    ? XBOX_MASK_UP    : 0)
		| (pressedDown()  ? XBOX_MASK_DOWN  : 0)
		| (pressedLeft()  ? XBOX_MASK_LEFT  : 0)
		| (pressedRight() ? XBOX_MASK_RIGHT : 0)
		| (pressedS2()    ? XBOX_MASK_START : 0)
		| (pressedS1()    ? XBOX_MASK_BACK  : 0)
		| (pressedL3()    ? XBOX_MASK_LS    : 0)
		| (pressedR3()    ? XBOX_MASK_RS    : 0)
	;

	xinputReport.buttons2 = 0
		| (pressedL1() ? XBOX_MASK_LB   : 0)
		| (pressedR1() ? XBOX_MASK_RB   : 0)
		| (pressedA1() ? XBOX_MASK_HOME : 0)
		| (pressedB1() ? XBOX_MASK_A    : 0)
		| (pressedB2() ? XBOX_MASK_B    : 0)
		| (pressedB3() ? XBOX_MASK_X    : 0)
		| (pressedB4() ? XBOX_MASK_Y    : 0)
	;

	xinputReport.lx = static_cast<int16_t>(state.lx) + INT16_MIN;
	xinputReport.ly = static_cast<int16_t>(~state.ly) + INT16_MIN;
	xinputReport.rx = static_cast<int16_t>(state.rx) + INT16_MIN;
	xinputReport.ry = static_cast<int16_t>(~state.ry) + INT16_MIN;

	if (hasAnalogTriggers)
	{
		xinputReport.lt = state.lt;
		xinputReport.rt = state.rt;
	}
	else
	{
		xinputReport.lt = pressedL2() ? 0xFF : 0;
		xinputReport.rt = pressedR2() ? 0xFF : 0;
	}

	return &xinputReport;
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

