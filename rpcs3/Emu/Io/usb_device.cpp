#include "stdafx.h"
#include "Emu/System.h"
#include "Emu/Cell/timers.hpp"
#include "Emu/Cell/lv2/sys_usbd.h"
#include "Emu/Io/usb_device.h"
#include "Emu/system_config.h"
#include "Utilities/StrUtil.h"
#include <libusb.h>
#include <time.h>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <chrono>

LOG_CHANNEL(sys_usbd);

extern void LIBUSB_CALL callback_transfer(struct libusb_transfer* transfer);

//////////////////////////////////////////////////////////////////
// ALL DEVICES ///////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////

usb_device::usb_device(const std::array<u8, 7>& location)
{
	this->location = location;
}

void usb_device::get_location(u8* location) const
{
	memcpy(location, this->location.data(), 7);
}

void usb_device::read_descriptors()
{
}

u32 usb_device::get_configuration(u8* buf)
{
	*buf = current_config;
	return sizeof(u8);
}

bool usb_device::set_configuration(u8 cfg_num)
{
	current_config = cfg_num;
	return true;
}

bool usb_device::set_interface(u8 int_num)
{
	current_interface = int_num;
	return true;
}

u64 usb_device::get_timestamp()
{
	return (get_system_time() - Emu.GetPauseTime());
}

// https://desowin.org/usbpcap/captureformat.html
#pragma pack(push,1)
struct linktype_usbpcap_header
{
	uint16_t header_len;
	uint64_t irp_id;
	uint32_t status;
	uint16_t function;
	uint8_t info;
	uint16_t bus;
	uint16_t device;
	uint8_t endpoint;
	uint8_t transfer;
	uint32_t data_len;
};

struct linktype_usbpcap_control_header
{
	struct linktype_usbpcap_header h;
	uint8_t stage;
};

// TODO isochronous capturing, when it's needed; usbhid only uses control and interrupt

// https://ietf-opsawg-wg.github.io/draft-ietf-opsawg-pcap/draft-ietf-opsawg-pcap.html
struct pcap_record_header
{
	uint32_t time_sec;
	uint32_t time_sub_sec;
	uint32_t captured_packet_length;
	uint32_t original_packet_length;
};

struct pcap_file_header
{
	uint32_t magic_number;
	uint16_t major_version;
	uint16_t minor_version;
	uint32_t reserved_1;
	uint32_t reserved_2;
	uint32_t snap_len;
	uint32_t link_type_and_info;
};

#pragma pack(pop)

//////////////////////////////////////////////////////////////////
// PASSTHROUGH DEVICE ////////////////////////////////////////////
//////////////////////////////////////////////////////////////////
usb_device_passthrough::usb_device_passthrough(libusb_device* _device, libusb_device_descriptor& desc, const std::array<u8, 7>& location)
	: usb_device(location), lusb_device(_device)
{
	device = UsbDescriptorNode(USB_DESCRIPTOR_DEVICE, UsbDeviceDescriptor{desc.bcdUSB, desc.bDeviceClass, desc.bDeviceSubClass, desc.bDeviceProtocol, desc.bMaxPacketSize0, desc.idVendor, desc.idProduct,
														  desc.bcdDevice, desc.iManufacturer, desc.iProduct, desc.iSerialNumber, desc.bNumConfigurations});

	if (g_cfg.io.capture_usb_passthrough)
	{
		std::string capture_directory_path = fs::get_config_dir() + "usb_passthrough_captures";
		std::filesystem::create_directory(capture_directory_path);
		time_t now = time(nullptr);
		struct tm calendar_time = {0};
		localtime_r(&now, &calendar_time);
		char time_string_buf[64];
		strftime(time_string_buf, sizeof(time_string_buf), "%Y_%m_%d_%H_%M_%S", &calendar_time);
		char pointer_buf[64];
		sprintf(pointer_buf, "%p", _device);
		char device_id_buf[64];
		sprintf(device_id_buf, "%04x_%04x", desc.idProduct, desc.idVendor);
		capture_file_path = capture_directory_path + "/" + device_id_buf + "_" + time_string_buf + "_" + pointer_buf + ".pcap";
		capture_file = fopen(capture_file_path.c_str(), "wb");
		if (capture_file == nullptr)
		{
			sys_usbd.error("Failed opening %s for writing, not capturing usb passthrough\n", capture_file_path.c_str());
		}
		else
		{
			pcap_file_header header = {
				.magic_number = 0xA1B23C4D,
				.major_version = 0x0200,
				.minor_version = 0x0400,
				.reserved_1 = 0,
				.reserved_2 = 0,
				// TODO when adding isochronous capturing, might have to raise this
				.snap_len = sizeof(linktype_usbpcap_control_header) + 1024,
				.link_type_and_info = 249
			};

			int write_status = fwrite(&header, sizeof(header), 1, capture_file);
			if (write_status != 1)
			{
				sys_usbd.error("Failed writing to %s, stopping usb passthrough capture\n", capture_file_path.c_str());
				fclose(capture_file);
				capture_file = nullptr;
			}
		}
		capture_begin = std::chrono::high_resolution_clock::now();
	}
}

usb_device_passthrough::~usb_device_passthrough()
{
	if (lusb_handle)
	{
		libusb_release_interface(lusb_handle, 0);
		libusb_close(lusb_handle);
	}

	if (lusb_device)
	{
		libusb_unref_device(lusb_device);
	}

	capture_file_mutex.lock();
	if (capture_file)
	{
		fclose(capture_file);
		capture_file = nullptr;
	}
	capture_file_mutex.unlock();
}

void usb_device_passthrough::send_libusb_transfer(libusb_transfer* transfer)
{
	while (true)
	{
		auto res = libusb_submit_transfer(transfer);
		switch (res)
		{
		case LIBUSB_SUCCESS: return;
		case LIBUSB_ERROR_BUSY: continue;
		default:
		{
			sys_usbd.error("Unexpected error from libusb_submit_transfer: %d(%s)", res, libusb_error_name(res));
			return;
		}
		}
	}
}

bool usb_device_passthrough::open_device()
{
	if (libusb_open(lusb_device, &lusb_handle) == LIBUSB_SUCCESS)
	{
#ifdef __linux__
		libusb_set_auto_detach_kernel_driver(lusb_handle, true);
#endif
		return true;
	}

	return false;
}

void usb_device_passthrough::read_descriptors()
{
	// Directly getting configuration descriptors from the device instead of going through libusb parsing functions as they're not needed
	for (u8 index = 0; index < device._device.bNumConfigurations; index++)
	{
		u8 buf[1000];
		int ssize = libusb_control_transfer(lusb_handle, +LIBUSB_ENDPOINT_IN | +LIBUSB_REQUEST_TYPE_STANDARD | +LIBUSB_RECIPIENT_DEVICE, LIBUSB_REQUEST_GET_DESCRIPTOR, 0x0200 | index, 0, buf, 1000, 0);
		if (ssize < 0)
		{
			sys_usbd.fatal("Couldn't get the config from the device: %d(%s)", ssize, libusb_error_name(ssize));
			continue;
		}

		capture_file_mutex.lock();
		if (capture_file && ssize <= 1000)
		{
			// capture the request
			unsigned char capture_buffer[(sizeof(pcap_record_header) + sizeof(linktype_usbpcap_control_header)) * 2 + 8 + 1000] = {0};
			auto now = std::chrono::high_resolution_clock::now() - capture_begin;
			auto nanosec = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
			auto sec = std::chrono::duration_cast<std::chrono::seconds>(now).count();

			auto pcap_header = reinterpret_cast<pcap_record_header *>(capture_buffer);
			auto control_header = reinterpret_cast<linktype_usbpcap_control_header *>(&capture_buffer[sizeof(pcap_record_header)]);
			unsigned char *data_buf = &capture_buffer[sizeof(pcap_record_header) + sizeof(linktype_usbpcap_control_header)];

			pcap_header->time_sec = sec;
			pcap_header->time_sub_sec = nanosec % (1 << 9);
			pcap_header->captured_packet_length = sizeof(linktype_usbpcap_control_header) + 8;
			pcap_header->original_packet_length = pcap_header->captured_packet_length;

			control_header->h.header_len = sizeof(linktype_usbpcap_control_header);
			control_header->h.irp_id = index + 1;
			control_header->h.function = 0xb;
			control_header->h.transfer = 2;
			control_header->h.data_len = 8;

			libusb_fill_control_setup(data_buf, +LIBUSB_ENDPOINT_IN | +LIBUSB_REQUEST_TYPE_STANDARD | +LIBUSB_RECIPIENT_DEVICE, LIBUSB_REQUEST_GET_DESCRIPTOR, 0x0200 | index, 0, 1000);

			// capture the response
			int offset = sizeof(pcap_record_header) + sizeof(linktype_usbpcap_control_header) + 8;

			pcap_header = reinterpret_cast<pcap_record_header *>(&capture_buffer[offset]);
			control_header = reinterpret_cast<linktype_usbpcap_control_header *>(&capture_buffer[offset + sizeof(pcap_record_header)]);
			data_buf = &capture_buffer[offset + sizeof(pcap_record_header) + sizeof(linktype_usbpcap_control_header)];

			pcap_header->time_sec = sec;
			pcap_header->time_sub_sec = nanosec % (1 << 9);
			pcap_header->captured_packet_length = sizeof(linktype_usbpcap_control_header) + ssize;
			pcap_header->original_packet_length = pcap_header->captured_packet_length;

			control_header->h.header_len = sizeof(linktype_usbpcap_control_header);
			control_header->h.irp_id = index + 1;
			control_header->h.function = 0xb;
			control_header->h.endpoint = 1 << 7;
			control_header->h.info = 1;
			control_header->h.transfer = 2;
			control_header->h.data_len = ssize;
			control_header->stage = 1;

			if (ssize != 0)
			{
				memcpy(data_buf, buf, ssize);
			}

			offset += sizeof(pcap_record_header) + sizeof(linktype_usbpcap_control_header) + ssize;

			int write_status = fwrite(capture_buffer, offset, 1, capture_file);
			if (write_status != 1)
			{
				sys_usbd.error("Failed writing to %s, stopping usb passthrough capture\n", capture_file_path.c_str());
				fclose(capture_file);
				capture_file = nullptr;
			}

		}
		capture_file_mutex.unlock();

		// Minimalistic parse
		auto& conf = device.add_node(UsbDescriptorNode(buf[0], buf[1], &buf[2]));

		for (int index = buf[0]; index < ssize;)
		{
			conf.add_node(UsbDescriptorNode(buf[index], buf[index + 1], &buf[index + 2]));
			index += buf[index];
		}
	}
}

u32 usb_device_passthrough::get_configuration(u8* buf)
{
	return (libusb_get_configuration(lusb_handle, reinterpret_cast<int*>(buf)) == LIBUSB_SUCCESS) ? sizeof(u8) : 0;
};

bool usb_device_passthrough::set_configuration(u8 cfg_num)
{
	usb_device::set_configuration(cfg_num);
	return (libusb_set_configuration(lusb_handle, cfg_num) == LIBUSB_SUCCESS);
};

bool usb_device_passthrough::set_interface(u8 int_num)
{
	usb_device::set_interface(int_num);
	return (libusb_claim_interface(lusb_handle, int_num) == LIBUSB_SUCCESS);
}

struct wrapped_usb_transfer
{
	usb_device_passthrough *device;
	void *orig_user_data;
};

static void LIBUSB_CALL callback_transfer_wrapped(struct libusb_transfer* transfer)
{
	auto wrapped = reinterpret_cast<wrapped_usb_transfer *>(transfer->user_data);
	transfer->user_data = wrapped->orig_user_data;

	wrapped->device->capture_transfer(transfer);

	free(wrapped);
	callback_transfer(transfer);
}

void usb_device_passthrough::capture_transfer(const struct libusb_transfer* transfer)
{
	if (transfer->status != LIBUSB_TRANSFER_COMPLETED)
	{
		return;
	}

	capture_file_mutex.lock();

	if (!capture_file)
	{
		capture_file_mutex.unlock();
		return;
	}

	char capture_buffer[sizeof(pcap_record_header) + sizeof(linktype_usbpcap_control_header) + 1024] = {0};
	int capture_size = 0;

	auto now = std::chrono::high_resolution_clock::now() - capture_begin;
	auto nanosec = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
	auto sec = std::chrono::duration_cast<std::chrono::seconds>(now).count();

	switch (transfer->type)
	{
		case LIBUSB_TRANSFER_TYPE_CONTROL:
		{
			auto pcap_header = reinterpret_cast<pcap_record_header *>(capture_buffer);
			auto control_header = reinterpret_cast<linktype_usbpcap_control_header *>(&capture_buffer[sizeof(pcap_record_header)]);
			char *data_buf = &capture_buffer[sizeof(pcap_record_header) + sizeof(linktype_usbpcap_control_header)];

			pcap_header->time_sec = sec;
			pcap_header->time_sub_sec = nanosec % (1 << 9);
			pcap_header->captured_packet_length = sizeof(linktype_usbpcap_control_header) + transfer->actual_length;
			pcap_header->original_packet_length = pcap_header->captured_packet_length;

			control_header->h.header_len = sizeof(linktype_usbpcap_control_header);
			control_header->h.irp_id = reinterpret_cast<uint64_t>(transfer);
			control_header->h.function = 8;
			control_header->h.info = 1;
			control_header->h.endpoint = 1 << 7;
			control_header->h.transfer = 2;
			control_header->h.data_len = transfer->actual_length;
			control_header->stage = 1;

			if (transfer->actual_length != 0)
			{
				memcpy(data_buf, transfer->buffer, transfer->actual_length);
			}

			capture_size = sizeof(pcap_record_header) + sizeof(linktype_usbpcap_control_header) + transfer->actual_length;

			break;
		}
		case LIBUSB_TRANSFER_TYPE_INTERRUPT:
		{
			auto pcap_header = reinterpret_cast<pcap_record_header *>(capture_buffer);
			auto interrupt_header = reinterpret_cast<linktype_usbpcap_header *>(&capture_buffer[sizeof(pcap_record_header)]);
			char *data_buf = &capture_buffer[sizeof(pcap_record_header) + sizeof(linktype_usbpcap_header)];

			int data_size = (transfer->endpoint & (1 << 7)) ? transfer->actual_length : 0;

			pcap_header->time_sec = sec;
			pcap_header->time_sub_sec = nanosec % (1 << 9);
			pcap_header->captured_packet_length = sizeof(linktype_usbpcap_header) + data_size;
			pcap_header->original_packet_length = pcap_header->captured_packet_length;

			interrupt_header->header_len = sizeof(linktype_usbpcap_header);
			interrupt_header->irp_id = reinterpret_cast<uint64_t>(transfer);
			interrupt_header->function = 9;
			interrupt_header->info = 1;
			interrupt_header->endpoint = 1 << 7 | (transfer->endpoint & (0xff >> 1));
			interrupt_header->transfer = 1;
			interrupt_header->data_len = data_size;

			if (data_size != 0)
			{
				memcpy(data_buf, transfer->buffer, data_size);
			}

			capture_size = sizeof(pcap_record_header) + sizeof(linktype_usbpcap_header) + data_size;

			break;
		}
		case LIBUSB_TRANSFER_TYPE_ISOCHRONOUS:
			// TODO, will also require buffer size adjustment
			break;
		default:
			sys_usbd.error("Unexpected libusb transfer type %d\n", transfer->type);
	}

	if (capture_size != 0)
	{
		int write_status = fwrite(capture_buffer, capture_size, 1, capture_file);
		if (write_status != 1)
		{
			sys_usbd.error("Failed writing to %s, stopping usb passthrough capture\n", capture_file_path.c_str());
			fclose(capture_file);
			capture_file = nullptr;
		}
	}

	capture_file_mutex.unlock();
}

void usb_device_passthrough::control_transfer(u8 bmRequestType, u8 bRequest, u16 wValue, u16 wIndex, [[maybe_unused]] u16 wLength, u32 buf_size, u8* buf, UsbTransfer* transfer)
{
	if (transfer->setup_buf.size() < buf_size + LIBUSB_CONTROL_SETUP_SIZE)
		transfer->setup_buf.resize(buf_size + LIBUSB_CONTROL_SETUP_SIZE);

	transfer->control_destbuf = (bmRequestType & LIBUSB_ENDPOINT_IN) ? buf : nullptr;

	libusb_fill_control_setup(transfer->setup_buf.data(), bmRequestType, bRequest, wValue, wIndex, buf_size);
	memcpy(transfer->setup_buf.data() + LIBUSB_CONTROL_SETUP_SIZE, buf, buf_size);
	libusb_fill_control_transfer(transfer->transfer, lusb_handle, transfer->setup_buf.data(), callback_transfer, transfer, 0);

	if (capture_file && transfer->setup_buf.size() <= 1024)
	{
		capture_file_mutex.lock();
		if (capture_file)
		{
			char capture_buffer[sizeof(pcap_record_header) + sizeof(linktype_usbpcap_control_header) + 1024] = {0};
			auto pcap_header = reinterpret_cast<pcap_record_header *>(capture_buffer);
			auto control_header = reinterpret_cast<linktype_usbpcap_control_header *>(&capture_buffer[sizeof(pcap_record_header)]);
			char *data_buf = &capture_buffer[sizeof(pcap_record_header) + sizeof(linktype_usbpcap_control_header)];

			auto now = std::chrono::high_resolution_clock::now() - capture_begin;
			auto nanosec = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
			auto sec = std::chrono::duration_cast<std::chrono::seconds>(now).count();

			pcap_header->time_sec = sec;
			pcap_header->time_sub_sec = nanosec % (1 << 9);
			pcap_header->captured_packet_length = sizeof(linktype_usbpcap_control_header) + transfer->setup_buf.size();
			pcap_header->original_packet_length = pcap_header->captured_packet_length;

			control_header->h.header_len = sizeof(linktype_usbpcap_control_header);
			control_header->h.irp_id = reinterpret_cast<uint64_t>(transfer->transfer);
			control_header->h.function = 0xb;
			control_header->h.transfer = 2;
			control_header->h.data_len = transfer->setup_buf.size();

			memcpy(data_buf, transfer->setup_buf.data(), transfer->setup_buf.size());

			int capture_size = sizeof(pcap_record_header) + sizeof(linktype_usbpcap_control_header) + transfer->setup_buf.size();

			int write_status = fwrite(capture_buffer, capture_size, 1, capture_file);
			if (write_status != 1)
			{
				sys_usbd.error("Failed writing to %s, stopping usb passthrough capture\n", capture_file_path.c_str());
				fclose(capture_file);
				capture_file = nullptr;
			}
		}

		if (capture_file)
		{
			auto callback_capture_ctx = reinterpret_cast<wrapped_usb_transfer *>(malloc(sizeof(wrapped_usb_transfer)));
			if (callback_capture_ctx != nullptr)
			{
				// setup capture on callback
				transfer->transfer->callback = callback_transfer_wrapped;
				callback_capture_ctx->orig_user_data = transfer->transfer->user_data;
				callback_capture_ctx->device = this;
				transfer->transfer->user_data = reinterpret_cast<void *>(callback_capture_ctx);
			}
			else
			{
				sys_usbd.error("Out of memory while setting up usb data capture on callback");
			}
		}
		capture_file_mutex.unlock();
	}

	send_libusb_transfer(transfer->transfer);
}

void usb_device_passthrough::interrupt_transfer(u32 buf_size, u8* buf, u32 endpoint, UsbTransfer* transfer)
{
	libusb_fill_interrupt_transfer(transfer->transfer, lusb_handle, endpoint, buf, buf_size, callback_transfer, transfer, 0);

	if (capture_file && buf_size <= 1024)
	{
		capture_file_mutex.lock();
		if (capture_file)
		{
			char capture_buffer[sizeof(pcap_record_header) + sizeof(linktype_usbpcap_header) + 1024] = {0};
			auto pcap_header = reinterpret_cast<pcap_record_header *>(capture_buffer);
			auto interrupt_header = reinterpret_cast<linktype_usbpcap_header *>(&capture_buffer[sizeof(pcap_record_header)]);
			char *data_buf = &capture_buffer[sizeof(pcap_record_header) + sizeof(linktype_usbpcap_header)];

			auto now = std::chrono::high_resolution_clock::now() - capture_begin;
			auto nanosec = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
			auto sec = std::chrono::duration_cast<std::chrono::seconds>(now).count();

			int data_size = (endpoint & (1 << 7)) == 0 ? buf_size : 0;

			pcap_header->time_sec = sec;
			pcap_header->time_sub_sec = nanosec % (1 << 9);
			pcap_header->captured_packet_length = sizeof(linktype_usbpcap_header) + data_size;
			pcap_header->original_packet_length = pcap_header->captured_packet_length;

			interrupt_header->header_len = sizeof(linktype_usbpcap_header);
			interrupt_header->irp_id = reinterpret_cast<uint64_t>(transfer->transfer);
			interrupt_header->function = 9;
			interrupt_header->endpoint = (endpoint & (0xff >> 1));
			interrupt_header->transfer = 1;
			interrupt_header->data_len = data_size;

			if (data_size != 0)
			{
				memcpy(data_buf, buf, data_size);
			}

			int capture_size = sizeof(pcap_record_header) + sizeof(linktype_usbpcap_header) + data_size;

			int write_status = fwrite(capture_buffer, capture_size, 1, capture_file);
			if (write_status != 1)
			{
				sys_usbd.error("Failed writing to %s, stopping usb passthrough capture\n", capture_file_path.c_str());
				fclose(capture_file);
				capture_file = nullptr;
			}
		}

		if (capture_file)
		{
			auto callback_capture_ctx = reinterpret_cast<wrapped_usb_transfer *>(malloc(sizeof(wrapped_usb_transfer)));
			if (callback_capture_ctx != nullptr)
			{
				// setup capture on callback
				transfer->transfer->callback = callback_transfer_wrapped;
				callback_capture_ctx->orig_user_data = transfer->transfer->user_data;
				callback_capture_ctx->device = this;
				transfer->transfer->user_data = reinterpret_cast<void *>(callback_capture_ctx);
			}
			else
			{
				sys_usbd.error("Out of memory while setting up usb data capture on callback");
			}
		}
		capture_file_mutex.unlock();
	}

	send_libusb_transfer(transfer->transfer);
}

void usb_device_passthrough::isochronous_transfer(UsbTransfer* transfer)
{
	// TODO actual endpoint
	// TODO actual size?
	libusb_fill_iso_transfer(transfer->transfer, lusb_handle, 0x81, static_cast<u8*>(transfer->iso_request.buf.get_ptr()), 0xFFFF, transfer->iso_request.num_packets, callback_transfer, transfer, 0);

	for (u32 index = 0; index < transfer->iso_request.num_packets; index++)
	{
		transfer->transfer->iso_packet_desc[index].length = transfer->iso_request.packets[index];
	}

	send_libusb_transfer(transfer->transfer);
}

//////////////////////////////////////////////////////////////////
// EMULATED DEVICE ///////////////////////////////////////////////
//////////////////////////////////////////////////////////////////
usb_device_emulated::usb_device_emulated(const std::array<u8, 7>& location)
	: usb_device(location)
{
}

usb_device_emulated::usb_device_emulated(const UsbDeviceDescriptor& _device, const std::array<u8, 7>& location)
	: usb_device(location)
{
	device = UsbDescriptorNode(USB_DESCRIPTOR_DEVICE, _device);
}

bool usb_device_emulated::open_device()
{
	return true;
}

u32 usb_device_emulated::get_descriptor(u8 type, u8 index, u8* buf, u32 buf_size)
{
	if (!buf)
	{
		return 0;
	}

	std::array<u8, 2> header;
	header = {static_cast<u8>(header.size()), type};

	u32 expected_count = std::min<u32>(static_cast<u32>(header.size()), buf_size);
	std::memcpy(buf, header.data(), expected_count);

	if (expected_count < header.size())
		return expected_count;

	switch (type)
	{
	case USB_DESCRIPTOR_DEVICE:
	{
		buf[0] = device.bLength;
		expected_count = std::min(device.bLength, ::narrow<u8>(buf_size));
		std::memcpy(buf + header.size(), device.data, expected_count - header.size());
		break;
	}
	case USB_DESCRIPTOR_CONFIG:
	{
		if (index < device.subnodes.size())
		{
			buf[0] = device.subnodes[index].bLength;
			expected_count = std::min(device.subnodes[index].bLength, ::narrow<u8>(buf_size));
			std::memcpy(buf + header.size(), device.subnodes[index].data, expected_count - header.size());
		}
		break;
	}
	case USB_DESCRIPTOR_STRING:
	{
		if (index < strings.size() + 1)
		{
			if (index == 0)
			{
				constexpr u8 len = static_cast<u8>(sizeof(u16) + header.size());
				buf[0] = len;
				expected_count = std::min(len, ::narrow<u8>(buf_size));
				constexpr le_t<u16> langid = 0x0409; // English (United States)
				std::memcpy(buf + header.size(), &langid, expected_count - header.size());
			}
			else
			{
				const std::u16string u16str = utf8_to_utf16(strings[index - 1]);
				const u8 len = static_cast<u8>(std::min(u16str.size() * sizeof(u16) + header.size(), static_cast<usz>(0xFF)));
				buf[0] = len;
				expected_count = std::min(len, ::narrow<u8>(std::min<u32>(255, buf_size)));
				std::memcpy(buf + header.size(), u16str.data(), expected_count - header.size());
			}
		}
		break;
	}
	default: sys_usbd.error("Unhandled DescriptorType: get_descriptor(type=0x%02x, index=0x%02x, buf=*0x%x, buf_size=0x%x)", type, index, buf, buf_size); break;
	}

	return expected_count;
}

u32 usb_device_emulated::get_status(bool self_powered, bool remote_wakeup, u8* buf, u32 buf_size)
{
	const u32 expected_count = buf ? std::min<u32>(sizeof(u16), buf_size) : 0;
	const u16 device_status = static_cast<int>(self_powered) | static_cast<int>(remote_wakeup) << 1;
	std::memcpy(buf, &device_status, expected_count);
	return expected_count;
}

void usb_device_emulated::control_transfer(u8 bmRequestType, u8 bRequest, u16 wValue, u16 wIndex, u16 /*wLength*/, u32 buf_size, u8* buf, UsbTransfer* transfer)
{
	transfer->fake            = true;
	transfer->expected_count  = buf_size;
	transfer->expected_result = HC_CC_NOERR;
	transfer->expected_time   = usb_device::get_timestamp() + 100;

	switch (bmRequestType)
	{
	case 0U /*silences warning*/ | LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_STANDARD | LIBUSB_RECIPIENT_DEVICE: // 0x00
		switch (bRequest)
		{
		case LIBUSB_REQUEST_SET_CONFIGURATION: usb_device::set_configuration(::narrow<u8>(wValue)); break;
		default: sys_usbd.error("Unhandled control transfer(0x%02x): 0x%02x", bmRequestType, bRequest); break;
		}
		break;
	case 0U /*silences warning*/ | LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_STANDARD | LIBUSB_RECIPIENT_INTERFACE: // 0x01
		switch (bRequest)
		{
		case LIBUSB_REQUEST_SET_INTERFACE: usb_device::set_interface(::narrow<u8>(wIndex)); break;
		default: sys_usbd.error("Unhandled control transfer(0x%02x): 0x%02x", bmRequestType, bRequest); break;
		}
		break;
	case 0U /*silences warning*/ | LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_STANDARD | LIBUSB_RECIPIENT_DEVICE: // 0x80
		switch (bRequest)
		{
		case LIBUSB_REQUEST_GET_STATUS: transfer->expected_count = get_status(false, false, buf, buf_size); break;
		case LIBUSB_REQUEST_GET_DESCRIPTOR: transfer->expected_count = get_descriptor(wValue >> 8, wValue & 0xFF, buf, buf_size); break;
		case LIBUSB_REQUEST_GET_CONFIGURATION: transfer->expected_count = get_configuration(buf); break;
		default: sys_usbd.error("Unhandled control transfer(0x%02x): 0x%02x", bmRequestType, bRequest); break;
		}
		break;
	default: sys_usbd.error("Unhandled control transfer: 0x%02x", bmRequestType); break;
	}
}

// Temporarily
#ifndef _MSC_VER
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

void usb_device_emulated::interrupt_transfer(u32 buf_size, u8* buf, u32 endpoint, UsbTransfer* transfer)
{
}

void usb_device_emulated::isochronous_transfer(UsbTransfer* transfer)
{
}

void usb_device_emulated::add_string(std::string str)
{
	strings.emplace_back(std::move(str));
}
