#pragma once

#include <QComboBox>
#include <QDialog>
#include <QTabWidget>
#include <QLabel>

#include <vector>

class emulated_logitech_g27_settings_dialog : public QDialog
{
	Q_OBJECT

public:
	emulated_logitech_g27_settings_dialog(QWidget* parent = nullptr);
	void disable();
	void enable();
	void set_state_text(const char *);

private:
	void load_config();
	void save_config();
	void reset_config();

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

	void toggle_state(bool enable);
};
