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

#include "ble/BLE.h"
#include "ble/Gap.h"
#include "ble-service-fota/FOTAService.h"

#include "ble_logging.h"
#include "BlockDeviceFOTAEventHandler.h"

#include "fw_version.h"

#include "mbed-trace/mbed_trace.h"
#include "platform/mbed_power_mgmt.h"

#include "bootutil/bootutil.h"
#include "secondary_bd.h"

#define TRACE_GROUP "MAIN"

using namespace std::literals::chrono_literals;

const static char DEVICE_NAME[] = "FOTADemo";

static events::EventQueue event_queue(/* event count */ 10 * EVENTS_EVENT_SIZE);
static ChainableGapEventHandler chainable_gap_event_handler;
static ChainableGattServerEventHandler chainable_gatt_server_event_handler;

void initiate_system_reset(void);

class FOTADemoEventHandler : public BlockDeviceFOTAEventHandler {

public:

    FOTADemoEventHandler(mbed::BlockDevice &bd, events::EventQueue &queue) :
        BlockDeviceFOTAEventHandler(bd, queue) { }

    GattAuthCallbackReply_t on_control_written(FOTAService &svc, mbed::Span<const uint8_t> buffer) override {
        /* Capture the FOTA_COMMIT op code */
        if(buffer[0] == FOTAService::FOTA_COMMIT) {
            int err = boot_set_pending(false);
            if(err) {
                tr_error("error setting the update candidate as pending: %d", err);
                svc.notify_status(FOTAService::FOTA_STATUS_INSTALLATION_FAILURE);
                return AUTH_CALLBACK_REPLY_ATTERR_UNLIKELY_ERROR;
            } else {
                tr_info("successfully set the update candidate as pending");
                /* The delay may not be necessary here */
                event_queue.call_in(250ms, initiate_system_reset);
                return AUTH_CALLBACK_REPLY_SUCCESS;
            }
        } else {
            /* Let the BlockDeviceFOTAEventHandler handle the other op codes */
            return BlockDeviceFOTAEventHandler::on_control_written(svc, buffer);
        }
    }

};

class FOTAServiceDemo : ble::Gap::EventHandler {

public:
    FOTAServiceDemo(BLE &ble, events::EventQueue &event_queue, ChainableGapEventHandler &chainable_gap_eh,
            ChainableGattServerEventHandler &chainable_gatt_server_eh) :
            _ble(ble),
            _event_queue(event_queue),
            _chainable_gap_eh(chainable_gap_eh),
            _chainable_gatt_server_eh(chainable_gatt_server_eh),
            _fota_handler(*get_secondary_bd(), event_queue),
            _fota_service(_ble, _event_queue, _chainable_gap_eh, _chainable_gatt_server_eh,
                    "1.0.0", FW_VERSION, "primary mcu"),
            _adv_data_builder(_adv_buffer)
    {
    }

    virtual ~FOTAServiceDemo() {
    }

    void start()
    {
        _ble.init(this, &FOTAServiceDemo::on_init_complete);

        _event_queue.dispatch_forever();
    }

    void disconnect(ble::local_disconnection_reason_t reason) {
        _ble.gap().disconnect(_connection_handle, reason);
    }

private:
    void on_init_complete(BLE::InitializationCompleteCallbackContext *params)
    {
        if (params->error != BLE_ERROR_NONE) {
            ble_log_error(params->error, "Ble initialization failed");
            return;
        }

        /* The ChainableGapEventHandler allows us to dispatch events from GAP to more than a single event handler */
        _chainable_gap_eh.addEventHandler(this);
        _ble.gap().setEventHandler(&_chainable_gap_eh);
        _ble.gattServer().setEventHandler(&_chainable_gatt_server_eh);

        _fota_service.init();

        _fota_service.set_event_handler(&_fota_handler);

        start_advertising();
    }

    void start_advertising()
    {
        ble::AdvertisingParameters adv_parameters(
                ble::advertising_type_t::CONNECTABLE_UNDIRECTED,
                ble::adv_interval_t(ble::millisecond_t(100))
        );

        _adv_data_builder.setFlags();
        _adv_data_builder.setAppearance(ble::adv_data_appearance_t::UNKNOWN);
        _adv_data_builder.setName(DEVICE_NAME);

        ble_error_t error = _ble.gap().setAdvertisingParameters(
                ble::LEGACY_ADVERTISING_HANDLE,
                adv_parameters
        );

        if (error) {
            ble_log_error(error, "_ble.gap().setAdvertisingParameters() failed");
            return;
        }

        error = _ble.gap().setAdvertisingPayload(
                ble::LEGACY_ADVERTISING_HANDLE,
                _adv_data_builder.getAdvertisingData()
        );

        if (error) {
            ble_log_error(error, "_ble.gap().setAdvertisingPayload() failed");
            return;
        }

        error = _ble.gap().startAdvertising(ble::LEGACY_ADVERTISING_HANDLE);

        if (error) {
            ble_log_error(error, "_ble.gap().startAdvertising() failed");
            return;
        }

        tr_info("Device advertising, please connect");
    }

private:
    void onConnectionComplete(const ble::ConnectionCompleteEvent &event) override
    {
        if (event.getStatus() == ble_error_t::BLE_ERROR_NONE) {
            _connection_handle = event.getConnectionHandle();
            tr_info("Client connected, you may now subscribe to updates");
        }
    }

    void onDisconnectionComplete(const ble::DisconnectionCompleteEvent &event) override
    {
        tr_info("Client disconnected, restarting advertising");

        ble_error_t error = _ble.gap().startAdvertising(ble::LEGACY_ADVERTISING_HANDLE);

        if (error) {
            ble_log_error(error, "_ble.gap().startAdvertising() failed");
            return;
        }
    }

    void onUpdateConnectionParametersRequest(
            const ble::UpdateConnectionParametersRequestEvent &event) override {
        tr_info("connection parameters update requested - connection: 0x%08X",
                event.getConnectionHandle());
        tr_info("connection interval (min/max): <%lu ms, %lu ms>",
                event.getMinConnectionInterval().valueInMs(),
                event.getMaxConnectionInterval().valueInMs());
        tr_info("slave latency: %i", event.getSlaveLatency().value());
        tr_info("supervision timeout: %i",
                event.getSupervisionTimeout().value());

        _ble.gap().acceptConnectionParametersUpdate(event.getConnectionHandle(),
                event.getMinConnectionInterval(),
                event.getMaxConnectionInterval(), event.getSlaveLatency(),
                event.getSupervisionTimeout());
    }

    void onConnectionParametersUpdateComplete(
            const ble::ConnectionParametersUpdateCompleteEvent &event)
                    override {
        tr_info("connection parameters update complete - connection: 0x%08X",
                event.getConnectionHandle());
        tr_info("connection interval: %lu ms",
                event.getConnectionInterval().valueInMs());
        tr_info("slave latency: %i", event.getSlaveLatency().value());
        tr_info("supervision timeout: %i",
                event.getSupervisionTimeout().value());
    }

    void onPhyUpdateComplete(ble_error_t status,
            ble::connection_handle_t connectionHandle, ble::phy_t txPhy,
            ble::phy_t rxPhy) override {
        tr_info("phy update complete - status: 0x%02X", status);
        tr_info("tx_phy: %i, rx_phy: %i", txPhy.value(), rxPhy.value());
    }

    void onDataLengthChange(ble::connection_handle_t connectionHandle,
            uint16_t txSize, uint16_t rxSize) override {
        tr_info("data length change - connection: 0x%08X", connectionHandle);
        tr_info("tx_size: %i, rx_size: %i", txSize, rxSize);
    }

private:
    BLE &_ble;
    events::EventQueue &_event_queue;
    ChainableGapEventHandler &_chainable_gap_eh;
    ChainableGattServerEventHandler &_chainable_gatt_server_eh;

    FOTADemoEventHandler _fota_handler;
    FOTAService _fota_service;

    uint8_t _adv_buffer[ble::LEGACY_ADVERTISING_MAX_SIZE];
    ble::AdvertisingDataBuilder _adv_data_builder;

    ble::connection_handle_t _connection_handle = 0;
};

void schedule_ble_events(BLE::OnEventsToProcessCallbackContext *context)
{
    event_queue.call(mbed::Callback<void()>(&context->ble, &BLE::processEvents));
}

void initiate_system_reset(void) {
    tr_info("initiating system reset...");
    event_queue.break_dispatch();
}

int main()
{
    mbed_trace_init();

    /**
     *  Do whatever is needed to verify the firmware is okay
     *  (eg: self test, connect to server, etc)
     *
     *  And then mark that the update succeeded
     */
    //run_self_test();
    int ret = boot_set_confirmed();
    if (ret == 0) {
        tr_info("boot confirmed");
    } else {
        tr_error("failed to confirm boot: %d", ret);
    }

    get_secondary_bd()->init();

    BLE &ble = BLE::Instance();
    ble.onEventsToProcess(schedule_ble_events);

    FOTAServiceDemo demo(ble, event_queue, chainable_gap_event_handler,
            chainable_gatt_server_event_handler);
    demo.start();

    tr_info("FOTADemo complete, restarting to apply update...");
    tr_info("Issuing disconnection");
    demo.disconnect(ble::local_disconnection_reason_t::POWER_OFF);
    /* Dispatch any lingering events (is this necessary?) */
    event_queue.dispatch_for(500ms);
    /* System reset */
    system_reset();

    return 0;
}
