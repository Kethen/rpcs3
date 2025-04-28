#include "stdafx.h"

#include "emulated_logitech_g27_settings_dialog.h"

#include "Emu/Io/LogitechG27.h"

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QTextEdit>
#include <QLabel>

#include <thread>
#include <chrono>

class DeviceChoice : public QTextEdit
{

public:
	DeviceChoice(QWidget *parent, uint32_t device_type_id)
		: QTextEdit(parent)
	{
		this->device_type_id = device_type_id;
		update_display();
	}

	uint32_t get_device_type_id()
	{
		return device_type_id;
	}

	void set_device_type_id(uint32_t device_type_id)
	{
		this->device_type_id = device_type_id;
		setReadOnly(true);
		update_display();
	}

private:
	uint32_t device_type_id;
	void update_display(){
		char text_buf[32];
		sprintf(text_buf, "%04x:%04x", device_type_id >> 16, device_type_id & 0xFFFF);
		setText(QString(text_buf));
	}
};

class Mapping : public QGroupBox
{

public:
	Mapping(emulated_logitech_g27_settings_dialog *parent, DeviceChoice *ffb_device, DeviceChoice *led_device, sdl_mapping mapping, bool is_axis, const char *name)
		: QGroupBox(parent)
	{
		auto layout = new QHBoxLayout(this);

		setLayout(layout);

		this->setting_dialog = parent;
		this->ffb_device = ffb_device;
		this->led_device = led_device;
		this->mapping = mapping;
		this->is_axis = is_axis;
		this->name = std::string(name);

		QLabel *label = new QLabel(this);
		label->setText(QString(name));

		display_box = new QTextEdit(this);
		display_box->setReadOnly(true);

		ffb_set_button = new QPushButton(QString("FFB"), this);
		led_set_button = new QPushButton(QString("LED"), this);
		map_button = new QPushButton(QString("MAP"), this);
		reverse_checkbox = new QCheckBox(QString("Reverse"), this);

		update_display();

		layout->addWidget(label);
		layout->addWidget(display_box);
		layout->addWidget(ffb_set_button);
		layout->addWidget(led_set_button);
		layout->addWidget(map_button);
		layout->addWidget(reverse_checkbox);

		connect(ffb_set_button, &QPushButton::clicked, this, [this]()
		{
			this->ffb_device->set_device_type_id(this->mapping.device_type_id);
		});

		connect(led_set_button, &QPushButton::clicked, this, [this]()
		{
			this->led_device->set_device_type_id(this->mapping.device_type_id);
		});

		connect(map_button, &QPushButton::clicked, this, [this](){
			this->setting_dialog->disable();
			int timeout = 20;

			while (timeout >= 0)
			{
				char text_buf[128];
				int timeout_sec = 125 * timeout / 1000;
				sprintf(text_buf, "Input %s for %s, timeout in %d %s\n", this->is_axis ? "axis" : "button/hat", this->name.c_str(), timeout_sec, timeout_sec >= 2 ? "seconds" : "second");
				this->setting_dialog->set_state_text(text_buf);
				// TODO pump SDL state and sample current input state

				std::this_thread::sleep_for(std::chrono::milliseconds(125));
				// TODO pump SDL state again and sample new input state, check for changes

				update_display();

				timeout--;
			}
			this->setting_dialog->enable();
		});

		connect(reverse_checkbox, &QCheckBox::clicked, this, [this](){
			this->mapping.reverse = this->reverse_checkbox->isChecked();
		});
	}

	void disable()
	{
		ffb_set_button->setDisabled(true);
		led_set_button->setDisabled(true);
		map_button->setDisabled(true);
		reverse_checkbox->setDisabled(true);
	}

	void enable()
	{
		ffb_set_button->setEnabled(true);
		led_set_button->setEnabled(true);
		map_button->setEnabled(true);
		reverse_checkbox->setEnabled(true);
	}

	void set_mapping(sdl_mapping mapping)
	{
		this->mapping = mapping;
		update_display();
	}

	sdl_mapping get_mapping()
	{
		return mapping;
	}

private:
	sdl_mapping mapping;
	bool is_axis;
	std::string name;

	DeviceChoice *ffb_device;
	DeviceChoice *led_device;
	QTextEdit *display_box;
	QPushButton *ffb_set_button;
	QPushButton *led_set_button;
	QPushButton *map_button;
	QCheckBox *reverse_checkbox;

	emulated_logitech_g27_settings_dialog *setting_dialog;

	void update_display(){
		char text_buf[64];
		const char *type_string = nullptr;
		switch(mapping.type)
		{
			case MAPPING_BUTTON:
				type_string = "button";
				break;
			case MAPPING_HAT:
				type_string = "hat";
				break;
			case MAPPING_AXIS:
				type_string = "axis";
				break;
		}
		sprintf(text_buf, "%04x:%04x, %s %u", mapping.device_type_id >> 16, mapping.device_type_id & 0xFFFF, type_string, mapping.id);
		display_box->setText(QString(text_buf));

		reverse_checkbox->setChecked(mapping.reverse);
	}
};

emulated_logitech_g27_settings_dialog::emulated_logitech_g27_settings_dialog(QWidget* parent)
	: QDialog(parent)
{
	setObjectName("emulated_logitech_g27_settings_dialog");
	setWindowTitle(tr("Configure Emulated Logitech G27 Wheel"));
	setAttribute(Qt::WA_DeleteOnClose);
	setAttribute(Qt::WA_StyledBackground);
	setModal(true);

	QVBoxLayout* v_layout = new QVBoxLayout(this);

	QDialogButtonBox* buttons = new QDialogButtonBox(this);
	buttons->setStandardButtons(QDialogButtonBox::Apply | QDialogButtonBox::Cancel | QDialogButtonBox::Save | QDialogButtonBox::RestoreDefaults);

	g_cfg_logitech_g27.load();

	// TODO create UI elements
	// TODO load UI states from config

	connect(buttons, &QDialogButtonBox::clicked, this, [this, buttons](QAbstractButton* button)
	{
		if (button == buttons->button(QDialogButtonBox::Apply))
		{
			// TODO apply ui state to config
			g_cfg_logitech_g27.save();
			// TODO reload UI states from config
		}
		else if (button == buttons->button(QDialogButtonBox::Save))
		{
			// TODO apply ui state to config
			g_cfg_logitech_g27.save();
			accept();
		}
		else if (button == buttons->button(QDialogButtonBox::RestoreDefaults))
		{
			if (QMessageBox::question(this, tr("Confirm Reset"), tr("Reset all buttons?")) != QMessageBox::Yes)
				return;
			g_cfg_logitech_g27.fill_defaults();
			// TODO reload UI states from config
		}
		else if (button == buttons->button(QDialogButtonBox::Cancel))
		{
			reject();
		}
	});

	state_text = reinterpret_cast<void *>(new QLabel(this));
	v_layout->addWidget(reinterpret_cast<Mapping *>(state_text));

	ffb_device = reinterpret_cast<void *>(new DeviceChoice(this, g_cfg_logitech_g27.ffb_device_type_id.get()));
	led_device = reinterpret_cast<void *>(new DeviceChoice(this, g_cfg_logitech_g27.led_device_type_id.get()));

	#define ADD_MAPPING_SETTING(name, is_axis, display_name) \
	{ \
		sdl_mapping m = { \
			.device_type_id = static_cast<uint32_t>(g_cfg_logitech_g27.name##_device_type_id.get()), \
			.type = static_cast<sdl_mapping_type>(g_cfg_logitech_g27.name##_type.get()), \
			.id = static_cast<uint8_t>(g_cfg_logitech_g27.name##_id.get()), \
			.hat = static_cast<hat_component>(g_cfg_logitech_g27.name##_hat.get()), \
			.reverse = g_cfg_logitech_g27.name##_reverse.get(), \
			.positive_axis = false \
		}; \
		name = reinterpret_cast<void *>(new Mapping(this, reinterpret_cast<DeviceChoice*>(ffb_device), reinterpret_cast<DeviceChoice*>(led_device), m, is_axis, display_name)); \
		v_layout->addWidget(reinterpret_cast<Mapping *>(name)); \
	}

	ADD_MAPPING_SETTING(steering, true, "Steering");
	ADD_MAPPING_SETTING(throttle, true, "Throttle");
	ADD_MAPPING_SETTING(brake, true, "Brake");
	ADD_MAPPING_SETTING(clutch, true, "Clutch");
	ADD_MAPPING_SETTING(shift_up, false, "Shift up");
	ADD_MAPPING_SETTING(shift_down, false, "Shift down");

	ADD_MAPPING_SETTING(up, false, "Up");
	ADD_MAPPING_SETTING(down, false, "Down");
	ADD_MAPPING_SETTING(left, false, "Left");
	ADD_MAPPING_SETTING(right, false, "Right");

	ADD_MAPPING_SETTING(triangle, false, "Triangle");
	ADD_MAPPING_SETTING(cross, false, "Cross");
	ADD_MAPPING_SETTING(square, false, "Square");
	ADD_MAPPING_SETTING(circle, false, "Circle");

	ADD_MAPPING_SETTING(l2, false, "L2");
	ADD_MAPPING_SETTING(l3, false, "L3");
	ADD_MAPPING_SETTING(r2, false, "R2");
	ADD_MAPPING_SETTING(r3, false, "R3");

	ADD_MAPPING_SETTING(plus, false, "L4");
	ADD_MAPPING_SETTING(minus, false, "L5");

	ADD_MAPPING_SETTING(dial_clockwise, false, "R4");
	ADD_MAPPING_SETTING(dial_anticlockwise, false, "R5");

	ADD_MAPPING_SETTING(select, false, "Select");
	ADD_MAPPING_SETTING(pause, false, "Start");

	ADD_MAPPING_SETTING(shifter_1, false, "Gear 1");
	ADD_MAPPING_SETTING(shifter_2, false, "Gear 2");
	ADD_MAPPING_SETTING(shifter_3, false, "Gear 3");
	ADD_MAPPING_SETTING(shifter_4, false, "Gear 4");
	ADD_MAPPING_SETTING(shifter_5, false, "Gear 5");
	ADD_MAPPING_SETTING(shifter_6, false, "Gear 6");
	ADD_MAPPING_SETTING(shifter_r, false, "Gear r");

	#undef ADD_MAPPING_SETTING

	v_layout->addWidget(reinterpret_cast<DeviceChoice *>(ffb_device));
	v_layout->addWidget(reinterpret_cast<DeviceChoice *>(led_device));

	v_layout->addWidget(buttons);
	setLayout(v_layout);
}

void emulated_logitech_g27_settings_dialog::set_state_text(const char *text)
{
	reinterpret_cast<QLabel *>(state_text)->setText(QString(text));
}

void emulated_logitech_g27_settings_dialog::toggle_state(bool enable)
{
	#define TOGGLE_STATE(name) \
	{ \
		auto m = reinterpret_cast<Mapping *>(name); \
		if (enable) \
			m->enable(); \
		else \
			m->disable(); \
	}
	TOGGLE_STATE(steering);
	TOGGLE_STATE(throttle);
	TOGGLE_STATE(brake);
	TOGGLE_STATE(clutch);
	TOGGLE_STATE(shift_up);
	TOGGLE_STATE(shift_down);

	TOGGLE_STATE(up);
	TOGGLE_STATE(down);
	TOGGLE_STATE(left);
	TOGGLE_STATE(right);

	TOGGLE_STATE(triangle);
	TOGGLE_STATE(cross);
	TOGGLE_STATE(square);
	TOGGLE_STATE(circle);

	TOGGLE_STATE(l2);
	TOGGLE_STATE(l3);
	TOGGLE_STATE(r2);
	TOGGLE_STATE(r3);

	TOGGLE_STATE(plus);
	TOGGLE_STATE(minus);

	TOGGLE_STATE(dial_clockwise);
	TOGGLE_STATE(dial_anticlockwise);

	TOGGLE_STATE(select);
	TOGGLE_STATE(pause);

	TOGGLE_STATE(shifter_1);
	TOGGLE_STATE(shifter_2);
	TOGGLE_STATE(shifter_3);
	TOGGLE_STATE(shifter_4);
	TOGGLE_STATE(shifter_5);
	TOGGLE_STATE(shifter_6);
	TOGGLE_STATE(shifter_r);

	#undef TOGGLE_STATE
}

void emulated_logitech_g27_settings_dialog::enable(){
	toggle_state(true);
}

void emulated_logitech_g27_settings_dialog::disable(){
	toggle_state(false);
}
