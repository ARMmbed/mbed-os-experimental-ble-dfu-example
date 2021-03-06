/**
 * Created on: Apr 1, 2021
 * Created by: gdbeckstein
 *
 * Built with ARM Mbed-OS
 *
 * Copyright (c) Embedded Planet, Inc - All rights reserved
 */

#ifndef _PERIODICBLOCKDEVICEERASER_H_
#define _PERIODICBLOCKDEVICEERASER_H_

#include "blockdevice/BlockDevice.h"
#include "events/EventQueue.h"

/**
 * This class encapsulates logic for erasing a given section of a block device
 * using periodic erase events. This prevents a large erase operation from
 * blocking the processor for a long periodic of time.
 */
class PeriodicBlockDeviceEraser
{
public:

    using PeriodicBlockDeviceCallback_t = mbed::Callback<void(int)>;

public:

    PeriodicBlockDeviceEraser(mbed::BlockDevice& bd, events::EventQueue& queue);

    ~PeriodicBlockDeviceEraser();

    /**
     * Start a periodic erase operation
     * @param[in] addr Address to start erase operation at
     * @param[in] size Size of erase operation
     * @param[in] erase_size Erase size of each operation.
     * @param[in] cb (Optional) Callback to execute on completion/error
     *
     * @note If the callback is nullptr, you will have to poll "is_done" to determine if the operation is complete
     * @note The size of the erase operation must be a multiple of the erase_size parameter.
     * @note The erase_size must be a multiple of the BlockDevice's erase size
     *
     * @retval 0 on success, 1 if any parameters are invalid
     */
    int start_erase(bd_addr_t addr, bd_size_t size, bd_size_t erase_size, PeriodicBlockDeviceCallback_t cb = nullptr);

    /**
     * Same as previous definition except the erase_size defaults to  the value returned by bd.get_erase_size()
     */
    int start_erase(bd_addr_t addr, bd_size_t size, PeriodicBlockDeviceCallback_t cb = nullptr) {
        return start_erase(addr, size, _bd.get_erase_size(), cb);
    }

    bool is_done() const {
        return _done;
    }

    int get_error() const {
        return _bd_error;
    }

protected:

    void erase();

protected:

    mbed::BlockDevice& _bd;
    events::EventQueue& _queue;

    int _erase_event_id = 0;

    /* Callback executed when the erase function completes or encounters an error */
    PeriodicBlockDeviceCallback_t _cb = nullptr;

    /* Done flag */
    bool _done = false;

    /* Current address location */
    bd_addr_t _addr = 0;

    /* End address location */
    bd_addr_t _end_addr = 0;

    /* Erase size */
    bd_size_t _erase_size = 0;

    /* Error code */
    int _bd_error = mbed::BD_ERROR_OK;

};

#endif /*_PERIODICBLOCKDEVICEERASER_H_ */
