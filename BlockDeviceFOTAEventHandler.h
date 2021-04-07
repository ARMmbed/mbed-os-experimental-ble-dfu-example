/*
 * Mbed-OS Microcontroller Library
 * Copyright (c) 2020-2021 Embedded Planet
 * Copyright (c) 2020-2021 ARM Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License
 */

#ifndef BLOCKDEVICEFOTAEVENTHANDLER_H_
#define BLOCKDEVICEFOTAEVENTHANDLER_H_

#include "ble-service-fota/FOTAService.h"

#include "blockdevice/BlockDevice.h"
#include "events/EventQueue.h"

#include "PeriodicBlockDeviceEraser.h"

/**
 * FOTAService EventHandler that writes data to the given BlockDevice
 */
class BlockDeviceFOTAEventHandler : public FOTAService::EventHandler
{

public:

    BlockDeviceFOTAEventHandler(mbed::BlockDevice& bd, events::EventQueue& queue);

    ~BlockDeviceFOTAEventHandler();

    /* Handler overrides for FOTAService */
    FOTAService::StatusCode_t on_binary_stream_written(FOTAService &svc, mbed::Span<const uint8_t> buffer) override;
    GattAuthCallbackReply_t on_control_written(FOTAService &svc, mbed::Span<const uint8_t> buffer) override;

    /* Callback for PeriodicBlocKDeviceEraser */
    void on_bd_erased(int result);

protected:

    mbed::BlockDevice &_bd;
    events::EventQueue &_queue;

    /* BlockDevice eraser that handles non-blocking, periodic erase operations */
    PeriodicBlockDeviceEraser *_bd_eraser = nullptr;

    /* Address pointer */
    bd_addr_t _addr = 0;

    FOTAService *_fota_svc = nullptr;

};



#endif /* BLOCKDEVICEFOTAEVENTHANDLER_H_ */
