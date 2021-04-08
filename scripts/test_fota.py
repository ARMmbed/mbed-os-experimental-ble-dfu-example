# Copyright (c) 2009-2020 Arm Limited
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License

# TODO figure out how to include/install the common dependencies from the mbed-os-experimental-ble-services TESTS folder

import platform
import asyncio
from common.fixtures import BoardAllocator, ClientAllocator
from common.device import Device
from os import urandom
from bleak.uuids import uuid16_dict
from bleak import BleakClient
from typing import Optional, Union
import logging
import time

log = logging.getLogger(__name__)

uuid16_dict = {v: k for k, v in uuid16_dict.items()}


def short_bt_sig_uuid_to_long(short: Union[str, int]) -> str:
    """
    Returns the long-format UUID of a Bluetooth SIG-specified short UUID
    :param short: Short-form BT SIG-specified UUID (as a hex string or a raw integer)
    :return: The fully-qualified long-form BT SIG UUID
    """
    hex_form = short
    if type(short) == str:
        pass
    elif type(short) == int:
        hex_form = hex(short)[2:]
    else:
        raise TypeError

    return f'0000{hex_form}-0000-1000-8000-00805f9b34fb'


UUID_FOTA_SERVICE = "53880000-65fd-4651-ba8e-91527f06c887"
UUID_BINARY_STREAM_CHAR = "53880001-65fd-4651-ba8e-91527f06c887"
UUID_CONTROL_CHAR = "53880002-65fd-4651-ba8e-91527f06c887"
UUID_STATUS_CHAR = "53880003-65fd-4651-ba8e-91527f06c887"
UUID_VERSION_CHAR = "53880004-65fd-4651-ba8e-91527f06c887"
UUID_FIRMWARE_REVISION_STRING_CHAR = short_bt_sig_uuid_to_long(uuid16_dict.get("Firmware Revision String"))
UUID_DEVICE_INFORMATION_SERVICE_UUID = short_bt_sig_uuid_to_long(uuid16_dict.get("Device Information"))
UUID_DESCRIPTOR_CUDD = short_bt_sig_uuid_to_long(uuid16_dict.get("Characteristic User Description"))

FOTA_STATUS_OK = bytearray(b'\x00')
FOTA_STATUS_UPDATE_SUCCESSFUL = bytearray(b'\x01')
FOTA_STATUS_XOFF = bytearray(b'\x02')
FOTA_STATUS_XON = bytearray(b'\x03')
FOTA_STATUS_SYNC_LOST = bytearray(b'\x04')
FOTA_STATUS_UNSPECIFIED_ERROR = bytearray(b'\x05')
FOTA_STATUS_VALIDATION_FAILURE = bytearray(b'\x06')
FOTA_STATUS_INSTALLATION_FAILURE = bytearray(b'\x07')
FOTA_STATUS_OUT_OF_MEMORY = bytearray(b'\x08')
FOTA_STATUS_MEMORY_ERROR = bytearray(b'\x09')
FOTA_STATUS_HARDWARE_ERROR = bytearray(b'\x0a')
FOTA_STATUS_NO_FOTA_SESSION = bytearray(b'\x0b')

FOTA_OP_CODE_START = bytearray(b'\x01')
FOTA_OP_CODE_STOP = bytearray(b'\x02')
FOTA_OP_CODE_COMMIT = bytearray(b'\x03')
FOTA_OP_CODE_SET_XOFF = bytearray(b'\x41')
FOTA_OP_CODE_SET_XON = bytearray(b'\x42')
FOTA_OP_CODE_SET_FRAGMENT_ID = bytearray(b'\x43')

FRAGMENT_SIZE = 128

MAXIMUM_RETRIES = 6

# TODO implement some generic utility scanner activity script that can be used by all BLE scripts...


def chunks(l, n):
    for i in range(0, len(l), n):
        yield l[i:i+n]


def get_chunk_n(data, chunksize: int, n: int):
    start = chunksize*n
    end = chunksize*(n+1)
    if len(data) <= start:
        return []
    if len(data) <= end:
        return data[start:]

    return data[start:end]


class StatusNotificationHandler:

    def __init__(self):
        self.status_val = bytearray()
        self.new_status_event = asyncio.Event()

    def handle_status_notification(self, char_handle: int, data: bytearray):
        log.info(f"Status notification: {''.join('{:02x}'.format(x) for x in data)}")
        self.status_val = data
        self.new_status_event.set()

    async def wait_for_status_notification(self):
        await self.new_status_event.wait()
        self.new_status_event.clear()


class FOTASession:

    def __init__(self, client: BleakClient):
        self.client = client
        self.handler = StatusNotificationHandler()
        self.fragment_id = 0
        self.rollover_counter = 0

    def update_fragment_id(self, fragment_id):
        # Account for rollover
        if fragment_id > self.fragment_id:
            self.rollover_counter -= 1
        self.fragment_id = fragment_id
        if self.rollover_counter < 0:
            self.rollover_counter = 0
        if self.fragment_id < 0:
            self.fragment_id = 0

    async def start(self):
        # Subscribe to notifications from the status characteristic
        await self.client.start_notify(UUID_STATUS_CHAR, self.handler.handle_status_notification)

        # Start a FOTA session
        await self.client.write_gatt_char(UUID_CONTROL_CHAR, FOTA_OP_CODE_START, True)

        # Wait for the client to write XON to the status characteristic
        timeout_counter = 0
        while timeout_counter < MAXIMUM_RETRIES:
            try:
                await asyncio.wait_for(self.handler.wait_for_status_notification(), timeout=10.0)
                status_code = bytearray([self.handler.status_val[0]])
                if status_code == FOTA_STATUS_XON:
                    log.info(
                        f'Received {"OK" if status_code == FOTA_STATUS_OK else "XON"} status notification, continuing')
                    break
                elif status_code == FOTA_STATUS_XOFF:
                    log.info(f'Received XOFF status notification')
                else:
                    log.warning(f'Received unknown type of status notification ({status_code[0]})')
            except asyncio.TimeoutError:
                log.info(f'waiting for status notification timed out ({timeout_counter}/{MAXIMUM_RETRIES})')
                timeout_counter += 1
                if timeout_counter >= MAXIMUM_RETRIES:
                    # We timed out, raise an exception
                    raise asyncio.TimeoutError

        # FOTA session started

    async def transfer_binary(self, filename: str):
        start_time = time.time()
        data = None
        with open(filename, 'rb') as f:
            data = f.read()

        send_complete = False
        flow_paused = False
        while not send_complete:
            # Check notifications
            if self.handler.new_status_event.is_set():
                self.handler.new_status_event.clear()
                # Check for XON, XOFF, SYNC_LOST, OK, anything else is an error code
                if self.handler.status_val[0:1] == FOTA_STATUS_OK:
                    log.info('Received FOTA status OK notification')
                elif self.handler.status_val[0:1] == FOTA_STATUS_XOFF:
                    fragment_id = self.handler.status_val[1]
                    log.info(f'Received FOTA status XOFF notification (paused at fragment ID: {fragment_id})')
                    flow_paused = True
                    self.update_fragment_id(fragment_id)
                elif self.handler.status_val[0:1] == FOTA_STATUS_XON:
                    fragment_id = self.handler.status_val[1]
                    log.info(f'Received FOTA status XON notification (resumed at fragment ID: {fragment_id})')
                    flow_paused = False
                    self.update_fragment_id(fragment_id)
                elif self.handler.status_val[0:1] == FOTA_STATUS_SYNC_LOST:
                    fragment_id = self.handler.status_val[1]
                    log.info(f'Received FOTA status SYNC LOST notification (sync lost at fragment ID: '
                             f'{fragment_id})')
                    self.update_fragment_id(fragment_id)

            if flow_paused:
                continue

            # Send the next packet
            packet_number = 256*self.rollover_counter + self.fragment_id
            binary_data = bytearray(get_chunk_n(data, FRAGMENT_SIZE, packet_number))
            bytes_sent = (packet_number)*FRAGMENT_SIZE + len(binary_data)
            log.info(f'Sending packet #{packet_number} (bytes sent: {bytes_sent}/{len(data)}, '
                     f'elapsed time: {(time.time() - start_time)*1000} ms)')
            # Prepend the fragment ID
            payload = bytearray([self.fragment_id])
            payload += binary_data
            if len(payload) < FRAGMENT_SIZE+1:
                # TODO what happens if the last send fails? We will miss the retransmission request!
                # TODO Check the status notification before commiting to see if we need to retransmit any packets
                send_complete = True
                if len(payload) == 0:
                    continue

            try:
                await asyncio.wait_for(self.client.write_gatt_char(UUID_BINARY_STREAM_CHAR, payload, False),
                                       timeout=0.025)
                await asyncio.sleep(0.075)
            except asyncio.TimeoutError as e:
                log.warning(f'timeout error occurred while writing bds char')
                await asyncio.sleep(0.1)

            self.fragment_id += 1
            if self.fragment_id >= 256:
                self.rollover_counter += 1
                self.fragment_id = 0

        # Send is "complete" now, explicitly read the status characteristic
        # to check for an out-of-sync condition before we commit the update
        status = await self.client.read_gatt_char(UUID_STATUS_CHAR)
        if status[0:1] == FOTA_STATUS_SYNC_LOST:
            log.warning(f'Transfer ended with sync lost condition!')
            self.update_fragment_id(status[1])
            # TODO restart binary transfer from sync lost fragment ID

        # Now we're actually done, commit the update
        await self.client.write_gatt_char(UUID_CONTROL_CHAR, FOTA_OP_CODE_COMMIT, True)

    async def get_firmware_revision(self) -> (str, Union[str, None]):
        """
        Gets the firmware revision from the optional Firmware Revision String characteristic of the DFU Service
        or from the Gatt Server's Device Information Service.

        If the Firmware Revision String characteristic exists on the DFU Service, it may have an optional
        Characteristic User Description descriptor (CUDD) that uniquely identifies which embedded device the
        target firmware is executed on. This is required if the Gatt Server has multiple DFU Service instances

        :return: tuple (fw_rev, device_description)
        """
        fota_svc = self.client.services.get_service(UUID_FOTA_SERVICE)
        fw_rev_char = fota_svc.get_characteristic(UUID_FIRMWARE_REVISION_STRING_CHAR)
        fw_rev_str = None
        fw_rev_desc_str = None
        if fw_rev_char:
            fw_rev_str = await self.client.read_gatt_char(fw_rev_char)
            fw_rev_desc = fw_rev_char.get_descriptor(UUID_DESCRIPTOR_CUDD)
            if fw_rev_desc:
                fw_rev_desc_str = await self.client.read_gatt_descriptor(fw_rev_desc.handle)
        else:
            dev_info_svc = self.client.services.get_service(UUID_DEVICE_INFORMATION_SERVICE_UUID)
            fw_rev_char = dev_info_svc.get_characteristic(UUID_FIRMWARE_REVISION_STRING_CHAR)
            fw_rev_str = await self.client.read_gatt_char(fw_rev_char)

        return fw_rev_str, fw_rev_desc_str


# TODO handle multiple FOTA services on one server
async def main():
    logging.basicConfig(level=logging.DEBUG)
    client_allocator = ClientAllocator()
    client = await client_allocator.allocate("FOTADemo")
    if not client:
        log.error("could not connect to FOTADemo")
        quit()
    session = FOTASession(client)

    fw_rev, dev_str = await session.get_firmware_revision()
    log.info(f'DFU Service found with firmware rev {fw_rev.decode("utf-8")}' +
            (f' for device "{dev_str.decode("utf-8")}"' if dev_str else ''))

    try:
        await session.start()
    except asyncio.TimeoutError:
        log.error("FOTA session failed to start within timeout period")
        await client_allocator.release(client)
        return

    log.info("FOTA session started successfully")

    # Send the binary
    log.info("starting firmware binary transfer")
    await session.transfer_binary('../OUTPUTS/signed-update.bin')
    await client_allocator.release(client)
    log.info("FOTA session complete, waiting for device to apply update...")
    for i in range(0, MAXIMUM_RETRIES):
        await asyncio.sleep(5.0)
        client = await client_allocator.allocate("FOTADemo")
        if client:
            break
        else:
            log.info(f'Failed to reconnect to FOTADemo (retry {i}/{MAXIMUM_RETRIES})')

    log.info(f'Reconnected to FOTADemo!')
    session = FOTASession(client)
    new_fw_rev, new_dev_str = await session.get_firmware_revision()
    log.info(f'DFU Service found with firmware rev {new_fw_rev.decode("utf-8")}' +
             (f' for device "{new_dev_str.decode("utf-8")}"' if new_dev_str else ''))
    if new_fw_rev != fw_rev:
        log.info("Update successful!")
    else:
        log.info("Update unsuccessful :(")
    await client_allocator.release(client)
    log.info("FOTADemo updater complete")


if __name__ == '__main__':
    loop = asyncio.get_event_loop()
    loop.run_until_complete(main())
