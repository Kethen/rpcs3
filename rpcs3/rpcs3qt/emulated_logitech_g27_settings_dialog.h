#pragma once

#include <QComboBox>
#include <QDialog>
#include <QTabWidget>
#include <QLabel>

#include <map>
#include <vector>
#include <Emu/Io/LogitechG27.h>

#include <SDL3/SDL.h>

struct joystick_state {
	std::vector<int16_t> axes;
	std::vector<bool> buttons;
	std::vector<hat_component> hats;
};

class emulated_logitech_g27_settings_dialog : public QDialog
{
	Q_OBJECT

public:
	emulated_logitech_g27_settings_dialog(QWidget* parent = nullptr);
	~emulated_logitech_g27_settings_dialog();
	void disable();
	void enable();
	void set_state_text(const char *);
	const std::map<uint32_t, joystick_state> &get_joystick_states();

private:
	void toggle_state(bool enable);

	std::map<uint32_t, joystick_state> last_joystick_states;
	std::vector<SDL_Joystick *> joystick_handles;
	uint64_t last_joystick_states_update = 0;
	bool sdl_initialized = false;

	// ui elements
	void *state_text;

	void *enabled;
	void *reverse_effects;

	void *steering;
	void *throttle;
	void *brake;
	void *clutch;
	void *shift_up;
	void *shift_down;

	void *up;
	void *down;
	void *left;
	void *right;

	void *triangle;
	void *cross;
	void *square;
	void *circle;

	void *l2;
	void *l3;
	void *r2;
	void *r3;

	void *plus;
	void *minus;

	void *dial_clockwise;
	void *dial_anticlockwise;

	void *select;
	void *pause;

	void *shifter_1;
	void *shifter_2;
	void *shifter_3;
	void *shifter_4;
	void *shifter_5;
	void *shifter_6;
	void *shifter_r;

	void *ffb_device;
	void *led_device;

	void *mapping_scroll_area;
};
