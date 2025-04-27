#pragma once

#include "Emu/Io/usb_device.h"
#include "Input/sdl_pad_handler.h"
#include "Utilities/Config.h"

#include "SDL3/SDL.h"
#include <mutex>
#include <map>
#include <vector>

enum logitech_g27_ffb_state
{
	G27_FFB_INACTIVE,
	G27_FFB_DOWNLOADED,
	G27_FFB_PLAYING
};

struct logitech_g27_ffb_slot
{
	logitech_g27_ffb_state state;
	uint64_t last_update;
	SDL_HapticEffect last_effect;
	int effect_id;
};

// TODO maybe push these into cfg
enum sdl_mapping_type
{
	MAPPING_BUTTON = 0,
	MAPPING_HAT,
	MAPPING_AXIS,
};

enum hat_component
{
	HAT_NONE = 0,
	HAT_UP,
	HAT_DOWN,
	HAT_LEFT,
	HAT_RIGHT
};

struct sdl_mapping
{
	uint32_t device_type_id; // (vendor_id << 16) | product_id
	sdl_mapping_type type;
	uint8_t id;
	hat_component hat;
	bool reverse;
	bool positive_axis;
};

struct logitech_g27_sdl_mapping
{
	sdl_mapping steering;
	sdl_mapping throttle;
	sdl_mapping brake;
	sdl_mapping clutch;
	sdl_mapping shift_up;
	sdl_mapping shift_down;

	sdl_mapping up;
	sdl_mapping down;
	sdl_mapping left;
	sdl_mapping right;

	sdl_mapping triangle;
	sdl_mapping cross;
	sdl_mapping square;
	sdl_mapping circle;

	// mappings based on g27 compat mode on g29
	sdl_mapping l2;
	sdl_mapping l3;
	sdl_mapping r2;
	sdl_mapping r3;

	sdl_mapping plus;
	sdl_mapping minus;

	sdl_mapping dial_clockwise;
	sdl_mapping dial_anticlockwise;

	sdl_mapping select;
	sdl_mapping pause;

	sdl_mapping shifter_1;
	sdl_mapping shifter_2;
	sdl_mapping shifter_3;
	sdl_mapping shifter_4;
	sdl_mapping shifter_5;
	sdl_mapping shifter_6;
	sdl_mapping shifter_r;
};

class usb_device_logitech_g27 : public usb_device_emulated
{
public:
	usb_device_logitech_g27(u32 controller_index, const std::array<u8, 7>& location);
	~usb_device_logitech_g27();

	static std::shared_ptr<usb_device> make_instance(u32 controller_index, const std::array<u8, 7>& location);
	static u16 get_num_emu_devices();

	void control_transfer(u8 bmRequestType, u8 bRequest, u16 wValue, u16 wIndex, u16 wLength, u32 buf_size, u8* buf, UsbTransfer* transfer) override;
	void interrupt_transfer(u32 buf_size, u8* buf, u32 endpoint, UsbTransfer* transfer) override;

	std::mutex thread_control_mutex;
	bool stop_thread;
	char thread_name[64];
	SDL_Thread *thread;
	void sdl_refresh();
private:
	u32 m_controller_index;

	logitech_g27_sdl_mapping mapping;
	bool reverse_effects;

	std::mutex sdl_handles_mutex;
	SDL_Joystick *led_joystick_handle = nullptr;
	SDL_Haptic *haptic_handle = nullptr;
	std::map<uint32_t, std::vector<SDL_Joystick *>> joysticks;
	bool fixed_loop = false;
	uint16_t wheel_range = 200;
	logitech_g27_ffb_slot effect_slots[4];
	SDL_HapticEffect default_spring_effect = {0};
	int default_spring_effect_id = -1;

	// just to initialize the global sdl instance
	sdl_pad_handler pad_handler;
};

struct emulated_logitech_g27_config : cfg::node {
	std::mutex m_mutex;
	bool load();
	void save();
	void fill_defaults();
	logitech_g27_sdl_mapping to_runtime_mapping();

	#define STR(s) #s
	#define MAPPING_ENTRY(name) \
		cfg::uint<0, 0xFFFFFFFF> name##_device_type_id{this, STR(name##_device_type_id)}; \
		cfg::uint<0, 0xFFFFFFFF> name##_type{this, STR(name##_type)}; \
		cfg::uint<0, 0xFF> name##_id{this, STR(name##_id)}; \
		cfg::uint<0, 0xFFFFFFFF> name##_hat{this, STR(name##_hat)}; \
		cfg::_bool name##_reverse{this, STR(name##_reverse)};

	MAPPING_ENTRY(steering);
	MAPPING_ENTRY(throttle);
	MAPPING_ENTRY(brake);
	MAPPING_ENTRY(clutch);
	MAPPING_ENTRY(shift_up);
	MAPPING_ENTRY(shift_down);

	MAPPING_ENTRY(up);
	MAPPING_ENTRY(down);
	MAPPING_ENTRY(left);
	MAPPING_ENTRY(right);

	MAPPING_ENTRY(triangle);
	MAPPING_ENTRY(cross);
	MAPPING_ENTRY(square);
	MAPPING_ENTRY(circle);

	MAPPING_ENTRY(l2);
	MAPPING_ENTRY(l3);
	MAPPING_ENTRY(r2);
	MAPPING_ENTRY(r3);

	MAPPING_ENTRY(plus);
	MAPPING_ENTRY(minus);

	MAPPING_ENTRY(dial_clockwise);
	MAPPING_ENTRY(dial_anticlockwise);

	MAPPING_ENTRY(select);
	MAPPING_ENTRY(pause);

	MAPPING_ENTRY(shifter_1);
	MAPPING_ENTRY(shifter_2);
	MAPPING_ENTRY(shifter_3);
	MAPPING_ENTRY(shifter_4);
	MAPPING_ENTRY(shifter_5);
	MAPPING_ENTRY(shifter_6);
	MAPPING_ENTRY(shifter_r);

	#undef MAPPING_ENTRY
	#undef STR

	cfg::_bool reverse_effects{this, "reverse_effects"};
	cfg::uint<0, 0xFFFFFFFF> ffb_device_type_id{this, "ffb_device_type_id"};
	cfg::uint<0, 0xFFFFFFFF> led_device_type_id{this, "led_device_type_id"};
};

extern emulated_logitech_g27_config g_cfg_logitech_g27;
