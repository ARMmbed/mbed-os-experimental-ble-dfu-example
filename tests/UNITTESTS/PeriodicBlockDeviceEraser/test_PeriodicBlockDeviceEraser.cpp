/**
 * ep-oc-mcu
 * Embedded Planet Open Core for Microcontrollers
 *
 * Built with ARM Mbed-OS
 *
 * Copyright (c) 2019 Embedded Planet, Inc.
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
 * limitations under the License.
 */

#include "gtest/gtest.h"

#include "PeriodicBlockDeviceEraser.h"
#include "events/EventQueue.h"
#include "blockdevice/HeapBlockDevice.h"
#include "platform/Span.h"

#include <cstdio>

#include <chrono>

using namespace std::chrono;

#define BD_SIZE 0x800
#define BD_READ_SIZE 1
#define BD_PROGRAM_SIZE 1
#define BD_ERASE_SIZE 0x100
#define BD_ERASE_VALUE 0xFF

/**
 * The standard HeapBlockDevice in Mbed-OS does not actually do anything
 * when erase is called. This implementation sets the data to the _erase_val
 * when erase is called.
 */
class HeapBlockDeviceRealErase : public mbed::HeapBlockDevice
{
    HeapBlockDeviceRealErase(bd_size_t size, bd_size_t read, bd_size_t program, bd_size_t erase, uint8_t erase_val = 0xFF) :
        mbed::HeapBlockDevice(size, read, program, erase), _erase_val(erase_val) {
    }


    virtual int erase(bd_addr_t addr, bd_size_t size) {

        if (!_is_initialized) {
            return BD_ERROR_DEVICE_ERROR;
        }
        if (!is_valid_erase(addr, size)) {
            return BD_ERROR_DEVICE_ERROR;
        }

        /* Simply call program and with a buffer full of _erase_val */
        uint8_t *buf = (uint8_t *)malloc(size);
        if(!buf) {
            return BD_ERROR_DEVICE_ERROR;
        }

        memset(buf, _erase_val, size);

        return program(buf, addr, size);

    }

protected:

    uint8_t _erase_val;

};

class TestPeriodicBlockDeviceEraser : public testing::Test {

	virtual void SetUp()
    {

	}

    virtual void TearDown()
    {

    }
};

/**
 * Utility to assert that each element of the input buffer is equal to the test value
 */
void assert_buffer_equals(mbed::Span<uint8_t> buf, uint8_t test_val) {
    for(int i = 0; i < buf.size(); i++) {
        ASSERT_EQ(buf[i], test_val);
    }
}


/**
 * Test PeriodicBlockDeviceEraser
 *
 * TODO perhaps put queue and bd initialization in setup functions so each test can be separate
 */
TEST_F(TestPeriodicBlockDeviceEraser, test_erase)
{
    events::EventQueue queue;
    HeapBlockDeviceRealErase bd(BD_SIZE, BD_READ_SIZE, BD_PROGRAM_SIZE, BD_ERASE_SIZE, BD_ERASE_VALUE);
    bd.init();
    PeriodicBlockDeviceEraser eraser(bd, queue);
    int err = eraser.start_erase(0, BD_SIZE-3);
    /* We expect an invalid parameters error to be returned.
     * Reason: The size of the erase operation is not a multiple of BD_ERASE_SIZE */
    ASSERT_EQ(err, 1);

    /* First, erase the entire block device to allow us to program it
     * In reality, with the HeapBlockDevice (as it is while this was written) there
     * are no actual checks to see if a "block" was erased before programming.
     *
     * But according to the BlockDevice API, this is the "correct" way to do it.
     */
    err = eraser.start_erase(0, BD_SIZE, nullptr, BD_ERASE_SIZE);

    /* Dispatch the queue until the eraser is done */
    while(!eraser.is_done()) {
        queue.dispatch_once();
    }

    uint8_t test_buffer[BD_SIZE];
    err = bd.read(test_buffer, 0, BD_SIZE);
    ASSERT_EQ(err, BD_ERROR_OK);
    assert_buffer_equals(test_buffer, BD_ERASE_VALUE);

    /* Now, program a new value into the BlockDevice */
    uint8_t pgm_val = 0x18;
    ASSERT_NE(pgm_val, BD_ERASE_VALUE);
    memset(test_buffer, pgm_val, BD_SIZE);
    err = bd.program(test_buffer, 0, BD_SIZE);
    ASSERT_EQ(err, BD_ERROR_OK);
    uint8_t readback_buffer[BD_SIZE];
    err = bd.read(readback_buffer, 0, BD_SIZE);
    ASSERT_EQ(err, BD_ERROR_OK);
    assert_buffer_equals(readback_buffer, pgm_val);

    /* Now, erase the block device again with the PeriodicBlockDeviceEraser */
    err = eraser.start_erase(0, BD_SIZE, nullptr, BD_ERASE_SIZE);

    /* Dispatch the queue until the eraser is done */
    while(!eraser.is_done()) {
        queue.dispatch_once();
    }

    uint8_t test_buffer[BD_SIZE];
    err = bd.read(test_buffer, 0, BD_SIZE);
    ASSERT_EQ(err, BD_ERROR_OK);
    assert_buffer_equals(test_buffer, BD_ERASE_VALUE);

}


