#include "Utilities/File.h"
#include "LogitechG27.h"

emulated_logitech_g27_config g_cfg_logitech_g27;

LOG_CHANNEL(cfg_log, "CFG");

void emulated_logitech_g27_config::fill_defaults(){
	#define INIT_AXIS_MAPPING(name) \
	{ \
		name##_device_type_id.set(0); \
		name##_type.set(MAPPING_AXIS); \
		name##_id.set(0); \
		name##_hat.set(HAT_NONE); \
		name##_reverse.set(false); \
	}

	INIT_AXIS_MAPPING(steering);
	INIT_AXIS_MAPPING(throttle);
	INIT_AXIS_MAPPING(brake);
	INIT_AXIS_MAPPING(clutch);

	#undef INIT_AXIS_MAPPING

	#define INIT_BUTTON_MAPPING(name) \
	{ \
		name##_device_type_id.set(0); \
		name##_type.set(MAPPING_BUTTON); \
		name##_id.set(0); \
		name##_hat.set(HAT_NONE); \
		name##_reverse.set(false); \
	}

	INIT_BUTTON_MAPPING(shift_up);
	INIT_BUTTON_MAPPING(shift_down);

	INIT_BUTTON_MAPPING(up);
	INIT_BUTTON_MAPPING(down);
	INIT_BUTTON_MAPPING(left);
	INIT_BUTTON_MAPPING(right);

	INIT_BUTTON_MAPPING(triangle);
	INIT_BUTTON_MAPPING(cross);
	INIT_BUTTON_MAPPING(square);
	INIT_BUTTON_MAPPING(circle);

	INIT_BUTTON_MAPPING(l2);
	INIT_BUTTON_MAPPING(l3);
	INIT_BUTTON_MAPPING(r2);
	INIT_BUTTON_MAPPING(r3);

	INIT_BUTTON_MAPPING(plus);
	INIT_BUTTON_MAPPING(minus);

	INIT_BUTTON_MAPPING(dial_clockwise);
	INIT_BUTTON_MAPPING(dial_anticlockwise);

	INIT_BUTTON_MAPPING(select);
	INIT_BUTTON_MAPPING(pause);

	INIT_BUTTON_MAPPING(shifter_1);
	INIT_BUTTON_MAPPING(shifter_2);
	INIT_BUTTON_MAPPING(shifter_3);
	INIT_BUTTON_MAPPING(shifter_4);
	INIT_BUTTON_MAPPING(shifter_5);
	INIT_BUTTON_MAPPING(shifter_6);
	INIT_BUTTON_MAPPING(shifter_r);

	#undef INIT_BUTTON_MAPPING
}

void emulated_logitech_g27_config::save(){
	const std::string cfg_name = fmt::format("%s%s.yml", fs::get_config_dir(true), "LogitechG27");
	cfg_log.notice("Saving LogitechG27 config: %s", cfg_name);

	if (!fs::create_path(fs::get_parent_dir(cfg_name)))
	{
		cfg_log.fatal("Failed to create path: %s (%s)", cfg_name, fs::g_tls_error);
	}

	if (!cfg::node::save(cfg_name))
	{
		cfg_log.error("Failed to save LogitechG27 config to '%s' (error=%s)", cfg_name, fs::g_tls_error);
	}
}

bool emulated_logitech_g27_config::load()
{
	m_mutex.lock();
	bool result = false;
	const std::string cfg_name = fmt::format("%s%s.yml", fs::get_config_dir(true), "LogitechG27");
	cfg_log.notice("Loading LogitechG27 config: %s", cfg_name);

	fill_defaults();

	if (fs::file cfg_file{ cfg_name, fs::read })
	{
		if (const std::string content = cfg_file.to_string(); !content.empty())
		{
			m_mutex.unlock();
			result = from_string(content);
		}
	}
	else
	{
		m_mutex.unlock();
		save();
	}

	return result;
}
