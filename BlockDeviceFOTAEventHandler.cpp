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

#include "BlockDeviceFOTAEventHandler.h"

#include "mbed-trace/mbed_trace.h"

#include <assert.h>

#define TRACE_GROUP "FOTA"

BlockDeviceFOTAEventHandler::BlockDeviceFOTAEventHandler(mbed::BlockDevice &bd,
        events::EventQueue &queue) : _bd(bd), _queue(queue) {
}

BlockDeviceFOTAEventHandler::~BlockDeviceFOTAEventHandler() {
    if(_bd_eraser) {
        delete _bd_eraser;
    }
}

FOTAService::StatusCode_t BlockDeviceFOTAEventHandler::on_binary_stream_written(
        FOTAService &svc, mbed::Span<const uint8_t> buffer) {

    tr_info("bsc written, programming %llu bytes at address %llu",
            (unsigned long long) buffer.size(), _addr);
    int err = _bd.program(buffer.data(), _addr, buffer.size());
    if(err) {
        tr_error("programming block device failed: 0x%X", err);
        return FOTAService::FOTA_STATUS_MEMORY_ERROR;
    }

    _addr += buffer.size();

    return FOTAService::FOTA_STATUS_OK;

}

GattAuthCallbackReply_t BlockDeviceFOTAEventHandler::on_control_written(
        FOTAService &svc, mbed::Span<const uint8_t> buffer) {

    _fota_svc = &svc;

    switch(buffer[0]) {
    case FOTAService::FOTA_NO_OP:
    {
        break;
    }

    case FOTAService::FOTA_START:
    {
        /* If the client has already started a FOTA session, the FOTA
         * service itself will reject another FOTA_START control write
         */
        tr_info("fota session started");
        svc.start_fota_session();

        /* We will do a "delayed start" */
        svc.set_xoff();

        /* Initiate erase of the update block device */
        if(_bd_eraser != nullptr) {
            delete _bd_eraser;
        }
        _bd_eraser = new PeriodicBlockDeviceEraser(_bd, _queue);
        tr_info("erasing fota bd, size: %llu", (unsigned long long) _bd.size());
        int err = _bd_eraser->start_erase(0, _bd.size(),
                mbed::callback(this, &BlockDeviceFOTAEventHandler::on_bd_erased));

        assert(!err);
        break;
    }

    case FOTAService::FOTA_STOP:
    {
        svc.stop_fota_session();
        tr_info("fota session cancelled");
        // TODO do we need to do anything here to handle cancelled FOTA?
        break;
    }

    case FOTAService::FOTA_COMMIT:
    {
        tr_info("fota commit");
        svc.stop_fota_session();
        break;
    }

    default:
    {
        return (GattAuthCallbackReply_t) FOTAService::AUTH_CALLBACK_REPLY_ATTERR_UNSUPPORTED_OPCODE;
        break;
    }
    }

    return AUTH_CALLBACK_REPLY_SUCCESS;
}

void BlockDeviceFOTAEventHandler::on_bd_erased(int result) {
    if(result != mbed::BD_ERROR_OK) {
        tr_error("error when erasing block device: 0x%X", -result);
        _fota_svc->notify_status(FOTAService::FOTA_STATUS_MEMORY_ERROR);
    } else {
        tr_info("successfully erased the update BlockDevice");
        _fota_svc->set_xon();
    }
}
