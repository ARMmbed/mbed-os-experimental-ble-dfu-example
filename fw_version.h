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

#ifndef FW_VERSION_H_
#define FW_VERSION_H_

#define fw_version_xstr(s) fw_version_str(s)
#define fw_version_str(s) #s

// TODO - this string is not updated if a clean build is not done!!!!
// TODO - this used to be in a .cpp file, I moved it to a header, it should update every build now?
static const char FW_VERSION[] = MBED_CONF_APP_VERSION_NUMBER "." fw_version_xstr(MBED_BUILD_TIMESTAMP);

#endif /* FW_VERSION_H_ */
