/*
 * This file is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License,
 * or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once

#include <inttypes.h>
#include <AP_HAL/HAL.h>
#include <AP_HAL/SPIDevice.h>
#include "AP_HAL_ChibiOS.h"

#if HAL_USE_SPI == TRUE

#include "Semaphores.h"
#include "Scheduler.h"
#include "Device.h"

#ifndef HAL_SPI_SCK_SAVE_RESTORE
#define HAL_SPI_SCK_SAVE_RESTORE !defined(STM32F1)
#endif

namespace ChibiOS {

class SPIBus : public DeviceBus {
public:
    SPIBus(uint8_t bus);
    struct spi_dev_s *dev;
    uint8_t bus;
    SPIConfig spicfg;
    void dma_allocate(Shared_DMA *ctx);
    void dma_deallocate(Shared_DMA *ctx);
    uint8_t slowdown;

    // we need an additional lock in the dma_allocate and
    // dma_deallocate functions to cope with 3-way contention as we
    // have two DMA channels that we are handling with the shared_dma
    // code
    mutex_t dma_lock;

    // store the last spi mode for stop_peripheral()
    uint32_t spi_mode;

    // start and stop the hardware peripheral
    void start_peripheral(void);
    void stop_peripheral(void);

private:
    bool spi_started;

    // mode line for SCK pin
#if HAL_SPI_SCK_SAVE_RESTORE
    iomode_t sck_mode;
#endif
};

struct SPIDesc {
    SPIDesc(const char *_name, uint8_t _bus,
            uint8_t _device, ioline_t _pal_line,
            uint32_t _mode, uint32_t _lowspeed, uint32_t _highspeed)
        : name(_name), bus(_bus), device(_device),
          pal_line(_pal_line), mode(_mode),
          lowspeed(_lowspeed), highspeed(_highspeed),
          bank_select_cb(nullptr), register_rw_cb(nullptr)
    {
    }

    const char *name;
    uint8_t bus;
    uint8_t device;
    ioline_t pal_line;
    uint32_t mode;
    uint32_t lowspeed;
    uint32_t highspeed;
    AP_HAL::Device::BankSelectCb bank_select_cb;
    AP_HAL::Device::RegisterRWCb register_rw_cb;
};


class SPIDevice : public AP_HAL::SPIDevice {
public:
    SPIDevice(SPIBus &_bus, SPIDesc &_device_desc);

    virtual ~SPIDevice();

    /* See AP_HAL::Device::set_speed() */
    bool set_speed(AP_HAL::Device::Speed speed) override;

    /* See AP_HAL::Device::transfer() */
    bool transfer(const uint8_t *send, uint32_t send_len,
                  uint8_t *recv, uint32_t recv_len) override;

    /* See AP_HAL::SPIDevice::transfer_fullduplex() */
    bool transfer_fullduplex(const uint8_t *send, uint8_t *recv,
                             uint32_t len) override;

    /* See AP_HAL::SPIDevice::transfer_fullduplex() */
    bool transfer_fullduplex(uint8_t *send_recv, uint32_t len) override;

    /*
        Links the bank select callback to the spi bus, so that even when
        used outside of the driver bank selection can be done.
    */
    void setup_bankselect_callback(BankSelectCb bank_select) override {
        device_desc.bank_select_cb = bank_select;
        AP_HAL::SPIDevice::setup_bankselect_callback(bank_select);
    }

    /* 
       Ensure to deregister bankselect callback in destructor of user 
       that could potentially be deleted. otherewise the orphaned functor
       can be called causing memory corruption.
    */
    void deregister_bankselect_callback() override {
        device_desc.bank_select_cb = nullptr;
        AP_HAL::SPIDevice::deregister_bankselect_callback();
    }

    /*
     *  send N bytes of clock pulses without taking CS. This is used
     *  when initialising microSD interfaces over SPI
    */
    bool clock_pulse(uint32_t len) override;

    /* See AP_HAL::Device::get_semaphore() */
    AP_HAL::Semaphore *get_semaphore() override;

    /* See AP_HAL::Device::register_periodic_callback() */
    AP_HAL::Device::PeriodicHandle register_periodic_callback(
        uint32_t period_usec, AP_HAL::Device::PeriodicCb) override;

    /* See AP_HAL::Device::adjust_periodic_callback() */
    bool adjust_periodic_callback(AP_HAL::Device::PeriodicHandle h, uint32_t period_usec) override;

    bool set_chip_select(bool set) override;

    bool acquire_bus(bool acquire, bool skip_cs);

    SPIDriver * get_driver();

#ifdef HAL_SPI_CHECK_CLOCK_FREQ
    // used to measure clock frequencies
    static void test_clock_freq(void);
#endif

    // setup a bus clock slowdown factor
    void set_slowdown(uint8_t slowdown) override;

private:
    SPIBus &bus;
    SPIDesc &device_desc;
    uint32_t frequency;
    uint32_t freq_flag;
    uint32_t freq_flag_low;
    uint32_t freq_flag_high;
    char *pname;
    bool cs_forced;
    static void *spi_thread(void *arg);
    static uint32_t derive_freq_flag_bus(uint8_t busid, uint32_t _frequency);
    uint32_t derive_freq_flag(uint32_t _frequency);
    // low level transfer function
    bool do_transfer(const uint8_t *send, uint8_t *recv, uint32_t len) WARN_IF_UNUSED;
};

class SPIDeviceManager : public AP_HAL::SPIDeviceManager {
public:
    friend class SPIDevice;

    static SPIDeviceManager *from(AP_HAL::SPIDeviceManager *spi_mgr)
    {
        return static_cast<SPIDeviceManager*>(spi_mgr);
    }

    AP_HAL::SPIDevice *get_device_ptr(const char *name) override;

    void set_register_rw_callback(const char* name, AP_HAL::Device::RegisterRWCb cb) override;

private:
    static SPIDesc device_table[];
    SPIBus *buses;
};
}

#endif // HAL_USE_SPI
