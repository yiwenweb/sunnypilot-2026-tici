#include "selfdrive/pandad/panda.h"

#include <cassert>
#include <memory>
#include <stdexcept>

#include "common/swaglog.h"

namespace {

libusb_context *init_usb_ctx() {
  libusb_context *context = nullptr;
  if (libusb_init(&context) != 0) {
    LOGE("libusb initialization error");
    return nullptr;
  }

#if LIBUSB_API_VERSION >= 0x01000106
  libusb_set_option(context, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_INFO);
#else
  libusb_set_debug(context, 3);
#endif
  return context;
}

bool is_panda(const libusb_device_descriptor &desc) {
  return desc.idVendor == 0x3801 && desc.idProduct == 0xddcc;
}

}

PandaUsbHandle::PandaUsbHandle(std::string serial) : PandaCommsHandle(serial) {
  libusb_device **device_list = nullptr;
  ctx = init_usb_ctx();
  if (ctx == nullptr) goto fail;

  const ssize_t device_count = libusb_get_device_list(ctx, &device_list);
  if (device_count < 0) goto fail;

  for (ssize_t index = 0; index < device_count; ++index) {
    libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(device_list[index], &desc) != 0 || !is_panda(desc)) continue;

    if (libusb_open(device_list[index], &dev_handle) < 0 || dev_handle == nullptr) goto fail;

    unsigned char descriptor_serial[26] = {};
    const int result = libusb_get_string_descriptor_ascii(dev_handle, desc.iSerialNumber, descriptor_serial, sizeof(descriptor_serial));
    if (result < 0) goto fail;

    hw_serial = std::string(reinterpret_cast<char *>(descriptor_serial), result);
    if (serial.empty() || serial == hw_serial) break;

    libusb_close(dev_handle);
    dev_handle = nullptr;
  }

  if (dev_handle == nullptr) goto fail;
  libusb_free_device_list(device_list, 1);
  device_list = nullptr;

  if (libusb_kernel_driver_active(dev_handle, 0) == 1) libusb_detach_kernel_driver(dev_handle, 0);
  if (libusb_set_configuration(dev_handle, 1) != 0) goto fail;
  if (libusb_claim_interface(dev_handle, 0) != 0) goto fail;
  return;

fail:
  if (device_list != nullptr) libusb_free_device_list(device_list, 1);
  cleanup();
  throw std::runtime_error("Error connecting to panda over USB");
}

PandaUsbHandle::~PandaUsbHandle() {
  std::lock_guard lock(hw_lock);
  cleanup();
  connected = false;
}

void PandaUsbHandle::cleanup() {
  if (dev_handle != nullptr) {
    libusb_release_interface(dev_handle, 0);
    libusb_close(dev_handle);
    dev_handle = nullptr;
  }
  if (ctx != nullptr) {
    libusb_exit(ctx);
    ctx = nullptr;
  }
}

std::vector<std::string> PandaUsbHandle::list() {
  static std::unique_ptr<libusb_context, decltype(&libusb_exit)> context(init_usb_ctx(), libusb_exit);
  std::vector<std::string> serials;
  libusb_device **device_list = nullptr;
  if (!context || libusb_get_device_list(context.get(), &device_list) < 0) return serials;

  for (size_t index = 0; device_list[index] != nullptr; ++index) {
    libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(device_list[index], &desc) != 0 || !is_panda(desc)) continue;

    libusb_device_handle *handle = nullptr;
    if (libusb_open(device_list[index], &handle) < 0 || handle == nullptr) continue;

    unsigned char descriptor_serial[26] = {};
    const int result = libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber, descriptor_serial, sizeof(descriptor_serial));
    libusb_close(handle);
    if (result >= 0) serials.emplace_back(reinterpret_cast<char *>(descriptor_serial), result);
  }

  libusb_free_device_list(device_list, 1);
  return serials;
}

void PandaUsbHandle::handle_usb_issue(int err, const char func[]) {
  LOGE_100("usb error %d \"%s\" in %s", err, libusb_strerror(static_cast<libusb_error>(err)), func);
  if (err == LIBUSB_ERROR_NO_DEVICE) {
    LOGE("lost USB panda connection");
    connected = false;
  }
}

int PandaUsbHandle::control_write(uint8_t request, uint16_t param1, uint16_t param2, unsigned int timeout) {
  if (!connected) return LIBUSB_ERROR_NO_DEVICE;
  std::lock_guard lock(hw_lock);
  int result;
  do {
    result = libusb_control_transfer(dev_handle, LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
                                     request, param1, param2, nullptr, 0, timeout);
    if (result < 0) handle_usb_issue(result, __func__);
  } while (result < 0 && connected);
  return result;
}

int PandaUsbHandle::control_read(uint8_t request, uint16_t param1, uint16_t param2, unsigned char *data, uint16_t length, unsigned int timeout) {
  if (!connected) return LIBUSB_ERROR_NO_DEVICE;
  std::lock_guard lock(hw_lock);
  int result;
  do {
    result = libusb_control_transfer(dev_handle, LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
                                     request, param1, param2, data, length, timeout);
    if (result < 0) handle_usb_issue(result, __func__);
  } while (result < 0 && connected);
  return result;
}

int PandaUsbHandle::bulk_write(unsigned char endpoint, unsigned char *data, int length, unsigned int timeout) {
  if (!connected) return 0;
  std::lock_guard lock(hw_lock);
  int transferred = 0;
  int result;
  do {
    result = libusb_bulk_transfer(dev_handle, endpoint, data, length, &transferred, timeout);
    if (result == LIBUSB_ERROR_TIMEOUT) break;
    if (result != 0) {
      handle_usb_issue(result, __func__);
    } else if (length != transferred) {
      LOGW_100("USB short write: sent %d of %d bytes", transferred, length);
      break;
    }
  } while (result != 0 && connected);
  return transferred;
}

int PandaUsbHandle::bulk_read(unsigned char endpoint, unsigned char *data, int length, unsigned int timeout) {
  if (!connected) return 0;
  std::lock_guard lock(hw_lock);
  int transferred = 0;
  int result;
  do {
    result = libusb_bulk_transfer(dev_handle, endpoint, data, length, &transferred, timeout);
    if (result == LIBUSB_ERROR_TIMEOUT) break;
    if (result == LIBUSB_ERROR_OVERFLOW) {
      comms_healthy = false;
      LOGE_100("USB receive overflow got 0x%x", transferred);
    } else if (result != 0) {
      handle_usb_issue(result, __func__);
    }
  } while (result != 0 && connected);
  return transferred;
}
