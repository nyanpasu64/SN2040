#ifndef _GAMEPAD_H_
#define _GAMEPAD_H_

#include "BoardConfig.h"
#include <string.h>
#include <MPGS.h>
#include "pico/stdlib.h"

// MUST BE DEFINED FOR MPG
extern uint32_t getMillis();
extern uint64_t getMicro();

#define GAMEPAD_POLL_MS 1
#define GAMEPAD_POLL_MICRO 100

#define GAMEPAD_FEATURE_REPORT_SIZE 32

class Gamepad : public MPGS
{
public:
	Gamepad(int debounceMS = 5, GamepadStorage *storage = &GamepadStore)
			: MPGS(debounceMS, storage) {}

	void setup();
	void process();
	void read();

	inline bool __attribute__((always_inline)) pressedF1()
	{
#ifdef PIN_SETTINGS
		return state.aux & (1 << 0);
#else
		return MPGS::pressedF1();
#endif
	}
	GamepadState rawState;
};

#endif
