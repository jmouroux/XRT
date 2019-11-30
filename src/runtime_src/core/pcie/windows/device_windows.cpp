/**
 * Copyright (C) 2019 Xilinx, Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License"). You may
 * not use this file except in compliance with the License. A copy of the
 * License is located at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */


#define INITGUID
#include "device_windows.h"
#include "common/utils.h"
#include "xrt.h"
#include "boost/format.hpp"
#include <string>
#include <iostream>
#include <map>

#pragma warning(disable : 4100 4996)

namespace xrt_core {

const device_windows::IOCTLEntry &
device_windows::
get_IOCTL_entry( QueryRequest _eQueryRequest) const
{
  // Initialize our lookup table
  static const std::map<QueryRequest, IOCTLEntry> QueryRequestToIOCTLTable =
  {
    { QR_PCIE_VENDOR,               { 0 }},
    { QR_PCIE_DEVICE,               { 0 }},
    { QR_PCIE_SUBSYSTEM_VENDOR,     { 0 }},
    { QR_PCIE_SUBSYSTEM_ID,         { 0 }},
    { QR_PCIE_LINK_SPEED,           { 0 }},
    { QR_PCIE_EXPRESS_LANE_WIDTH,   { 0 }},
    { QR_DMA_THREADS_RAW,           { 0 }},
    { QR_ROM_VBNV,                  { 0 }},
    { OR_ROM_DDR_BANK_SIZE,         { 0 }},
    { QR_ROM_DDR_BANK_COUNT_MAX,    { 0 }},
    { QR_ROM_FPGA_NAME,             { 0 }},
    { QR_XMC_VERSION,               { 0 }},
    { QR_XMC_SERIAL_NUM,            { 0 }},
    { QR_XMC_MAX_POWER,             { 0 }},
    { QR_XMC_BMC_VERSION,           { 0 }},
    { QR_DNA_SERIAL_NUM,            { 0 }},
    { QR_CLOCK_FREQS,               { 0 }},
    { QR_IDCODE,                    { 0 }},
    { QR_STATUS_MIG_CALIBRATED,     { 0 }},
    { QR_STATUS_P2P_ENABLED,        { 0 }},
    { QR_TEMP_CARD_TOP_FRONT,       { 0 }},
    { QR_TEMP_CARD_TOP_REAR,        { 0 }},
    { QR_TEMP_CARD_BOTTOM_FRONT,    { 0 }},
    { QR_TEMP_FPGA,                 { 0 }},
    { QR_FAN_TRIGGER_CRITICAL_TEMP, { 0 }},
    { QR_FAN_FAN_PRESENCE,          { 0 }},
    { QR_FAN_SPEED_RPM,             { 0 }},
    { QR_CAGE_TEMP_0,               { 0 }},
    { QR_CAGE_TEMP_1,               { 0 }},
    { QR_CAGE_TEMP_2,               { 0 }},
    { QR_CAGE_TEMP_3,               { 0 }},
    { QR_12V_PEX_MILLIVOLTS,        { 0 }},
    { QR_12V_PEX_MILLIAMPS,         { 0 }},
    { QR_12V_AUX_MILLIVOLTS,        { 0 }},
    { QR_12V_AUX_MILLIAMPS,         { 0 }},
    { QR_3V3_PEX_MILLIVOLTS,        { 0 }},
    { QR_3V3_AUX_MILLIVOLTS,        { 0 }},
    { QR_DDR_VPP_BOTTOM_MILLIVOLTS, { 0 }},
    { QR_DDR_VPP_TOP_MILLIVOLTS,    { 0 }},

    { QR_5V5_SYSTEM_MILLIVOLTS,     { 0 }},
    { QR_1V2_VCC_TOP_MILLIVOLTS,    { 0 }},
    { QR_1V2_VCC_BOTTOM_MILLIVOLTS, { 0 }},
    { QR_1V8_MILLIVOLTS,            { 0 }},
    { QR_0V85_MILLIVOLTS,           { 0 }},
    { QR_0V9_VCC_MILLIVOLTS,        { 0 }},
    { QR_12V_SW_MILLIVOLTS,         { 0 }},
    { QR_MGT_VTT_MILLIVOLTS,        { 0 }},
    { QR_INT_VCC_MILLIVOLTS,        { 0 }},
    { QR_INT_VCC_MILLIAMPS,         { 0 }},

    { QR_3V3_PEX_MILLIAMPS,         { 0 }},
    { QR_0V85_MILLIAMPS,            { 0 }},
    { QR_3V3_VCC_MILLIVOLTS,        { 0 }},
    { QR_HBM_1V2_MILLIVOLTS,        { 0 }},
    { QR_2V5_VPP_MILLIVOLTS,        { 0 }},
    { QR_INT_BRAM_VCC_MILLIVOLTS,   { 0 }},

    { QR_FIREWALL_DETECT_LEVEL,     { 0 }},
    { QR_FIREWALL_STATUS,           { 0 }},
    { QR_FIREWALL_TIME_SEC,         { 0 }},

    { QR_POWER_MICROWATTS,          { 0 }}
};
  // Find the translation entry
  std::map<QueryRequest, IOCTLEntry>::const_iterator it = QueryRequestToIOCTLTable.find(_eQueryRequest);

  if (it == QueryRequestToIOCTLTable.end()) {
    std::string errMsg = boost::str( boost::format("The given query request ID (%d) is not supported.") % _eQueryRequest);
    throw no_such_query(_eQueryRequest, errMsg);
  }

  return it->second;
}



void
device_windows::
query_device(uint64_t _deviceID, QueryRequest _eQueryRequest, const std::type_info & _typeInfo, boost::any &_returnValue) const
{
  // Initialize return data to being empty container.
  // Note: CentOS Boost 1.53 doesn't support the clear() method.
  boost::any anyEmpty;
  _returnValue.swap(anyEmpty);

  // Get the sysdev and entry values to call
  const IOCTLEntry & entry = get_IOCTL_entry(_eQueryRequest);

  std::string sErrorMsg;

  if (entry.IOCTLValue == 0) {
    sErrorMsg = "IOCTLEntry is initialized with zeros.";
  }

  // Removes compile warnings for unused variables
  _deviceID = _deviceID;

  // Reference linux code:
//  if (_typeInfo == typeid(std::string)) {
//    // -- Typeid: std::string --
//    _returnValue = std::string("");
//    std::string *stringValue = boost::any_cast<std::string>(&_returnValue);
//    pcidev::get_dev(_deviceID)->sysfs_get( entry.sSubDevice, entry.sEntry, sErrorMsg, *stringValue);
//
//  } else if (_typeInfo == typeid(uint64_t)) {
//    // -- Typeid: uint64_t --
//    _returnValue = (uint64_t) -1;
//    std::vector<uint64_t> uint64Vector;
//    pcidev::get_dev(_deviceID)->sysfs_get( entry.sSubDevice, entry.sEntry, sErrorMsg, uint64Vector);
//    if (!uint64Vector.empty()) {
//      _returnValue = uint64Vector[0];
//    }
//
//  } else if (_typeInfo == typeid(bool)) {
//    // -- Typeid: bool --
//    _returnValue = (bool) 0;
//    std::vector<uint64_t> uint64Vector;
//    pcidev::get_dev(_deviceID)->sysfs_get( entry.sSubDevice, entry.sEntry, sErrorMsg, uint64Vector);
//    if (!uint64Vector.empty()) {
//      _returnValue = (bool) uint64Vector[0];
//    }
//
//  } else if (_typeInfo == typeid(std::vector<std::string>)) {
//    // -- Typeid: std::vector<std::string>
//    _returnValue = std::vector<std::string>();
//    std::vector<std::string> *stringVector = boost::any_cast<std::vector<std::string>>(&_returnValue);
//    pcidev::get_dev(_deviceID)->sysfs_get( entry.sSubDevice, entry.sEntry, sErrorMsg, *stringVector);
//
//  } else {
//  }

  sErrorMsg = boost::str( boost::format("Error: Unsupported query_device return type: '%s'") % _typeInfo.name());

  if (!sErrorMsg.empty()) {
    throw std::runtime_error(sErrorMsg);
  }
}

device_core*
device_core_child_ctor()
{
  static device_windows dw;
  return &dw;
}

device_windows::
device_windows()
{
  // Do nothing
}

device_windows::
~device_windows() {
  // Do nothing
}

std::pair<uint64_t, uint64_t>
device_windows::
get_total_devices() const
{
  auto user_count = xclProbe();
  return std::make_pair(user_count, user_count);
}

void
device_windows::
read_device_dma_stats(uint64_t _deviceID, boost::property_tree::ptree &_pt) const
{
  // Removes compiler warnings
  _deviceID = _deviceID;
  _pt = _pt;
  // Linux reference code
//  _deviceID = _deviceID;
//  _pt = _pt;
//  xclDeviceHandle handle = xclOpen(_deviceID, nullptr, XCL_QUIET);
//
//  if (!handle) {
//    // Unable to get a handle
//    return;
//  }
//
//  xclDeviceUsage devstat = { 0 };
//  xclGetUsageInfo(handle, &devstat);
//
//  // Clean up after ourselves
//  xclClose(handle);
//
//  boost::property_tree::ptree ptChannels;
//  for (unsigned index = 0; index < XCL_DEVICE_USAGE_COUNT; ++index) {
//      boost::property_tree::ptree ptDMA;
//      ptDMA.put( "id", std::to_string(index).c_str());
//      ptDMA.put( "h2c", unitConvert(devstat.h2c[index]) );
//      ptDMA.put( "c2h", unitConvert(devstat.c2h[index]) );
//
//      // Create our array of data
//      ptChannels.push_back(std::make_pair("", ptDMA));
//  }
//
//  _pt.add_child( "transfer_metrics.channels", ptChannels);
}

void
device_windows::
scan_devices(bool verbose, bool json) const
{
  std::cout << "TO-DO: scan_devices\n";
  verbose = verbose;
  json = json;
}

void
device_windows::
auto_flash(uint64_t _deviceID, std::string& shell, std::string& id, bool force) const
{
  std::cout << "TO-DO: auto_flash\n";
  _deviceID = _deviceID;
  shell = shell;
  id = id;
  force = force;
}

void
device_windows::
reset_shell(uint64_t _deviceID) const
{
  std::cout << "TO-DO: reset_shell\n";
  _deviceID = _deviceID;
}

void
device_windows::
update_shell(uint64_t _deviceID, std::string flashType, std::string& primary, std::string& secondary) const
{
  std::cout << "TO-DO: update_shell\n";
  _deviceID = _deviceID;
  flashType = flashType;
  primary = primary;
  secondary = secondary;
}

void
device_windows::
update_SC(uint64_t _deviceID, std::string& file) const
{
  std::cout << "TO-DO: update_SC\n";
  _deviceID = _deviceID;
  file = file;
}

} // xrt_core