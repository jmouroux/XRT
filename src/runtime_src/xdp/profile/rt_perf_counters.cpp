/**
 * Copyright (C) 2016-2017 Xilinx, Inc
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

#include "rt_perf_counters.h"
#include "debug.h"
#include "rt_profile_xocl.h"
#include "rt_profile_results.h"
#include "rt_profile_writers.h"
#include "rt_profile_device.h"
#include "rt_profile_rule_checks.h"
#include "xdp/rt_singleton.h"

#include "xocl/core/device.h"
#include "xocl/xclbin/xclbin.h"

#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <ctime>

namespace XCL {
  // ****************
  // Sorted Top Usage
  // ****************
  template <typename T>
  void TimeTraceSortedTopUsage<T>::push(T* newElement)
  {
    auto itr = Storage.begin();
    while (itr != Storage.end()) {
      if ((*itr)->getDuration() < newElement->getDuration()) {
        break;
      }
      itr++;
    }
    if (itr == Storage.end()) {
      Storage.push_back(newElement);
    }
    else {
      Storage.insert(itr, newElement);
    }
    if (Storage.size() > Limit) {
      T::recycle(Storage.back());
      Storage.pop_back();
    }
  }

  template <typename T>
  void TimeTraceSortedTopUsage<T>::writeTopUsageSummary(WriterI* writer) const
  {
    for (const auto &it : Storage) {
      it->write(writer);
    }
  }

  // ********************
  // Performance Counters
  // ********************
  PerformanceCounter::PerformanceCounter() :
    TopKernelTimes(), TopBufferReadTimes(), TopBufferWriteTimes(),
    TopKernelReadTimes(), TopKernelWriteTimes(),
    TopDeviceBufferReadTimes(), TopDeviceBufferWriteTimes()
  {
    // do nothing
  }

  void PerformanceCounter::logBufferRead(size_t size, double duration, uint32_t contextId, uint32_t numDevices) {
#ifdef BUFFER_STAT_PER_CONTEXT
    if (BufferReadStat.find(contextId) == BufferReadStat.end()) {
      BufferStats bufferStats;
      BufferReadStat[contextId] = bufferStats;
    }
    BufferReadStat[contextId].log(size, duration);
    BufferReadStat[contextId].setContextId(contextId);
    BufferReadStat[contextId].setNumDevices(numDevices);
#else
    BufferReadStat.log(size, duration);
    BufferReadStat.setContextId(contextId);
    BufferReadStat.setNumDevices(numDevices);
#endif
  }

  void PerformanceCounter::logBufferWrite(size_t size, double duration, uint32_t contextId, uint32_t numDevices) {
#ifdef BUFFER_STAT_PER_CONTEXT
    if (BufferWriteStat.find(contextId) == BufferWriteStat.end()) {
      BufferStats bufferStats;
      BufferWriteStat[contextId] = bufferStats;
    }

    //XOCL_DEBUGF("logBufferWrite: size=%d, duration=%.3f, context ID=%d\n", size, duration, contextId);
    BufferWriteStat[contextId].log(size, duration);
    BufferWriteStat[contextId].setContextId(contextId);
    BufferWriteStat[contextId].setNumDevices(numDevices);
#else
    BufferWriteStat.log(size, duration);
    BufferWriteStat.setContextId(contextId);
    BufferWriteStat.setNumDevices(numDevices);
#endif
  }

  void PerformanceCounter::logDeviceRead(size_t size, double duration)
  {
    DeviceBufferReadStat.log(size, duration);
  }

  void PerformanceCounter::logDeviceWrite(size_t size, double duration)
  {
    DeviceBufferWriteStat.log(size, duration);
  }

  void PerformanceCounter::logDeviceKernel(size_t size, double duration)
  {
    DeviceKernelStat.log(size, duration);
  }

  void PerformanceCounter::logDeviceKernelTransfer(std::string& deviceName, std::string& kernelName,
      size_t size, double duration, uint32_t bitWidth, double clockFreqMhz, bool isRead)
  {
    //IMPORTANT NOTE: We cannot correctly classify kernel traffic until v1.1 of the Alpha Data
    //base platform. Then, we'll know the AXI IDs and hence, the exact compute unit that
    //requested a given transaction. Until then, let's just classify everything under 'ALL'.
    //std::string name = deviceName + "|" + "ALL";
    std::string name = "ALL";

    if (isRead)
      DeviceKernelReadSummaryStats[name].log(size, duration, bitWidth, clockFreqMhz);
    else
      DeviceKernelWriteSummaryStats[name].log(size, duration, bitWidth, clockFreqMhz);
  }

  void PerformanceCounter::logFunctionCallStart(const std::string& functionName, double timePoint)
  {
    CallCount[functionName].logStart(timePoint);
  }

  void PerformanceCounter::logFunctionCallEnd(const std::string& functionName, double timePoint)
  {
    CallCount[functionName].logEnd(timePoint);
  }

  void PerformanceCounter::logKernelExecutionStart(const std::string& kernelName, const std::string& deviceName,
                                                   double timePoint)
  {
    KernelExecutionStats[kernelName].logStart(timePoint);

    auto iter = DeviceStartTimes.find(deviceName);
    if (iter == DeviceStartTimes.end())
      DeviceStartTimes[deviceName] = timePoint;
    else if (timePoint < iter->second)
      DeviceStartTimes[deviceName] = timePoint;
  }

  void PerformanceCounter::logKernelExecutionEnd(const std::string& kernelName, const std::string& deviceName,
                                                 double timePoint)
  {
    KernelExecutionStats[kernelName].logEnd(timePoint);

    auto iter = DeviceEndTimes.find(deviceName);
    if (iter == DeviceEndTimes.end())
      DeviceEndTimes[deviceName] = timePoint;
    else if (timePoint > iter->second)
      DeviceEndTimes[deviceName] = timePoint;
  }

  void PerformanceCounter::logComputeUnitDeviceStart(const std::string& deviceName, double timePoint)
  {
    auto iter = DeviceCUStartTimes.find(deviceName);
    if (iter == DeviceCUStartTimes.end())
      DeviceCUStartTimes[deviceName] = timePoint;
    else if (timePoint < iter->second)
      DeviceCUStartTimes[deviceName] = timePoint;
  }

  void PerformanceCounter::logComputeUnitExecutionStart(const std::string& cuName, double timePoint)
  {
    ComputeUnitExecutionStats[cuName].logStart(timePoint);
  }

  void PerformanceCounter::logComputeUnitExecutionEnd(const std::string& cuName, double timePoint)
  {
    ComputeUnitExecutionStats[cuName].logEnd(timePoint);
  }

  void PerformanceCounter::logComputeUnitStats(const std::string& cuName, const std::string& kernelName, double totalTimeStat,
    double maxTimeStat,  double minTimeStat, uint32_t totalCalls, uint32_t clockFreqMhz)
  {
    std::string newCU;
    bool foundKernel = false;
    for (const auto &pair : ComputeUnitExecutionStats) {
      auto fullName = pair.first;
      size_t first_index = fullName.find_first_of("|");
      size_t second_index = fullName.find('|', first_index+1);
      size_t third_index = fullName.find('|', second_index+1);
      size_t fourth_index = fullName.find("|", third_index+1);
      size_t fifth_index = fullName.find_last_of("|");
      std::string currCUName = fullName.substr(fourth_index + 1, fifth_index - fourth_index - 1);
      std::string currKernelName = fullName.substr(first_index + 1, second_index - first_index - 1);
      if (currCUName == cuName) {
        ComputeUnitExecutionStats[fullName].logStats(totalTimeStat, maxTimeStat, minTimeStat, totalCalls, clockFreqMhz);
        return;
      }
      else if (currKernelName == kernelName) {
        newCU = fullName.substr(0, fourth_index) + "|" + cuName + "|" + fullName.substr(fifth_index + 1);
        foundKernel = true;
      }
    }
    // CR 1003380 - Runtime does not send all CU Names so we create a key
    if (foundKernel && totalTimeStat > 0.0) {
      ComputeUnitExecutionStats[newCU].logStats(totalTimeStat, maxTimeStat, minTimeStat, totalCalls, clockFreqMhz);
    }
  }

  void PerformanceCounter::logDeviceEvent(std::string deviceName, std::string kernelName, size_t size,
                                          double duration, uint32_t bitWidth, double clockFreqMhz,
                                          bool isKernel, bool isRead, bool isKernelTransfer)
  {
    if (isKernel) {
      logDeviceKernel(size, duration);
    }
    else {
      if (isKernelTransfer) {
        logDeviceKernelTransfer(deviceName, kernelName, size, duration, bitWidth, clockFreqMhz, isRead);
      }
      else {
        if (isRead)
          logDeviceRead(size, duration);
        else
          logDeviceWrite(size, duration);
      }
    }
  }

  void PerformanceCounter::pushToSortedTopUsage(KernelTrace* trace)
  {
    TopKernelTimes.push(trace);
  }

  void PerformanceCounter::pushToSortedTopUsage(BufferTrace* trace, bool isRead)
  {
    if (isRead)
      TopBufferReadTimes.push(trace);
    else
      TopBufferWriteTimes.push(trace);
  }

  void PerformanceCounter::pushToSortedTopUsage(DeviceTrace* trace, bool isRead, bool isKernel)
  {
    if (isKernel) {
      if (isRead)
        TopKernelReadTimes.push(trace);
      else
        TopKernelWriteTimes.push(trace);
    }
    else {
      if (isRead)
        TopDeviceBufferReadTimes.push(trace);
      else
        TopDeviceBufferWriteTimes.push(trace);
    }
  }

  //
  // Writers
  //
  void PerformanceCounter::writeTopHardwareSummary(WriterI* writer) const {
    TopKernelTimes.writeTopUsageSummary(writer);
  }

  void PerformanceCounter::writeTopKernelSummary(WriterI* writer) const {
    TopKernelTimes.writeTopUsageSummary(writer);
  }

  void PerformanceCounter::writeTopDataTransferSummary(WriterI* writer, bool isRead) const {
    if (isRead)
      TopBufferReadTimes.writeTopUsageSummary(writer);
    else
      TopBufferWriteTimes.writeTopUsageSummary(writer);
  }

  void PerformanceCounter::writeTopDeviceTransferSummary(WriterI* writer, bool isRead) const {
    if (isRead)
      TopDeviceBufferReadTimes.writeTopUsageSummary(writer);
    else
      TopDeviceBufferWriteTimes.writeTopUsageSummary(writer);
  }

  void PerformanceCounter::setAllDeviceBufferBitWidth(uint32_t bitWidth)
  {
    DeviceBufferReadStat.setBitWidth(bitWidth);
    DeviceBufferWriteStat.setBitWidth(bitWidth);

#ifdef BUFFER_STAT_PER_CONTEXT
    for (auto readIter : BufferReadStat) {
      readIter.second.setBitWidth(bitWidth);
    }
    for (auto writeIter : BufferWriteStat) {
      writeIter.second.setBitWidth(bitWidth);
    }
#else
    BufferReadStat.setBitWidth(bitWidth);
    BufferWriteStat.setBitWidth(bitWidth);
#endif
  }

  void PerformanceCounter::setAllDeviceKernelBitWidth(uint32_t bitWidth)
  {
    DeviceKernelStat.setBitWidth(bitWidth);
  }

  void PerformanceCounter::setAllDeviceClockFreqMhz(double clockFreqMhz)
  {
    DeviceBufferReadStat.setClockFreqMhz(clockFreqMhz);
    DeviceBufferWriteStat.setClockFreqMhz(clockFreqMhz);
    DeviceKernelStat.setClockFreqMhz(clockFreqMhz);

#ifdef BUFFER_STAT_PER_CONTEXT
    for (auto readIter : BufferReadStat) {
      readIter.second.setClockFreqMhz(clockFreqMhz);
    }
    for (auto writeIter : BufferWriteStat) {
      writeIter.second.setClockFreqMhz(clockFreqMhz);
    }
#else
    BufferReadStat.setClockFreqMhz(clockFreqMhz);
    BufferWriteStat.setClockFreqMhz(clockFreqMhz);
#endif
  }

  // Get device start time (NOTE: when first CU starts)
  double PerformanceCounter::getDeviceStartTime(const std::string& deviceName) const
  {
    auto startIter = DeviceStartTimes.find(deviceName);

    // If device is not found, return 0
    if (startIter == DeviceStartTimes.end())
      return 0.0;

    return startIter->second;
  }

  double PerformanceCounter::getTotalKernelExecutionTime(const std::string& deviceName) const
  {
    auto startIter = DeviceStartTimes.find(deviceName);
    auto endIter = DeviceEndTimes.find(deviceName);

    // If device is not found, return 0
    if ((startIter == DeviceStartTimes.end()) || (endIter == DeviceEndTimes.end()))
      return 0.0;

#if 1
    double totalTime = endIter->second - startIter->second;
#else
    // FYI, method used pre-2015.4
    double totalTime = 0.0;
    for (const auto &pair : KernelExecutionStats) {
      TimeStats stats = pair.second;
      totalTime += stats.getTotalTime();
    }
#endif

    XDP_LOG("getTotalKernelExecutionTime: total kernel time = %f - %f = %f for device: %s\n",
            endIter->second, startIter->second, totalTime, deviceName.c_str());
    return totalTime;
  }

  uint32_t PerformanceCounter::getComputeUnitCalls(const std::string& deviceName,
      const std::string& cuName) const
  {
    auto cuIter = ComputeUnitExecutionStats.begin();
    auto cuEnd  = ComputeUnitExecutionStats.end();

    while (cuIter != cuEnd) {
      //"name" is of the form "deviceName|kernelName|globalSize|localSize|cuName|objId"
      std::string name = cuIter->first;
      name = name.substr(0, name.find_last_of("|"));
      if (name.find(deviceName) != std::string::npos && name.find(cuName) != std::string::npos) {
        TimeStats cuStats = cuIter->second;
        return cuStats.getNoOfCalls();
      }
      cuIter++;
    }

    // If CU is not found, return 0
    return 0;
  }

  double PerformanceCounter::getComputeUnitTotalTime(const std::string& deviceName,
                                                     const std::string& cuName) const
  {
    for (const auto &pair : ComputeUnitExecutionStats) {
      auto fullName = pair.first;
      if (fullName.find(deviceName) != std::string::npos
          && fullName.find(cuName)  != std::string::npos) {
        auto stat = pair.second;
        return stat.getTotalTime();
      }
    }
    return getTotalKernelExecutionTime(deviceName);
  }

  void PerformanceCounter::writeKernelSummary(WriterI* writer) const
  {
    for (const auto &pair : KernelExecutionStats) {
      auto fullName = pair.first;
      auto kernelName = fullName.substr(0, fullName.find_first_of("|"));
      writer->writeSummary(kernelName, pair.second);
    }
  }

  void PerformanceCounter::writeComputeUnitSummary(WriterI* writer) const
  {
    for (const auto &pair : ComputeUnitExecutionStats) {
      auto fullName = pair.first;
      auto cuName = fullName.substr(0, fullName.find_last_of("|"));
      writer->writeComputeUnitSummary(cuName, pair.second);
    }
  }

  void PerformanceCounter::writeAcceleratorSummary(WriterI* writer) const
  {
    for (const auto &pair : ComputeUnitExecutionStats) {
      auto fullName = pair.first;
      auto cuName = fullName.substr(0, fullName.find_last_of("|"));
      writer->writeAcceleratorSummary(cuName, pair.second);
    }
  }

  void PerformanceCounter::writeAPISummary(WriterI* writer) const
  {
    using std::vector;
    using std::pair;
    using std::sort;
    using std::string;
    // Print it in sorted order of Total Time. To sort it by duration
    // populate a vector and then using lambda function sort it by duration

    vector<pair<string, TimeStats>> callPairs(CallCount.begin(),
        CallCount.end());
    sort(callPairs.begin(), callPairs.end(),
        [](const pair<string, TimeStats>& A, const pair<string, TimeStats>& B) {
      return A.second.getTotalTime() > B.second.getTotalTime();
    });

    for (const auto &pair : callPairs) {
      writer->writeSummary(pair.first, pair.second);
    }
  }

  // CR 951564: Use APM counters to calculate write throughput (i.e., byte count / total latency)
  // CR 956489: APM is reporting 2x the write bytes; let's use the buffer stat bytes instead
  void PerformanceCounter::writeBufferStat(WriterI* writer, const std::string transferType,
      const BufferStats &bufferStat, double maxTransferRateMBps) const
  {
    uint64_t totalTranx  = bufferStat.getCount();
    uint64_t totalBytes  = bufferStat.getTotalSize();

    double minTotalTimeMsec = totalBytes / (1000.0 * maxTransferRateMBps);
    double totalTimeMsec = (bufferStat.getTotalTime() < minTotalTimeMsec) ?
        minTotalTimeMsec : bufferStat.getTotalTime();

    writer->writeHostTransferSummary(transferType, bufferStat, totalBytes, totalTranx,
      totalTimeMsec, maxTransferRateMBps);

  }

  // Write host data transfer stats
  void PerformanceCounter::writeHostTransferSummary(WriterI* writer, bool isRead,
      uint64_t totalBytes, double totalTimeMsec, double maxTransferRateMBps) const
  {
#ifdef BUFFER_STAT_PER_CONTEXT
    if (isRead) {
      for (auto readIter : BufferReadStat)
        writeBufferStat(writer, "READ", readIter.second, maxTransferRateMBps);
    }
    else {
      for (auto writeIter : BufferWriteStat)
        writeBufferStat(writer, "WRITE", writeIter.second, maxTransferRateMBps);
    }
#else
    if (isRead)
      writeBufferStat(writer, "READ", BufferReadStat, maxTransferRateMBps);
    else
      writeBufferStat(writer, "WRITE", BufferWriteStat, maxTransferRateMBps);
#endif
  }

  void PerformanceCounter::writeKernelTransferSummary(WriterI* writer, std::string& deviceName,
      std::string& cuPortName, const std::string& argNames, uint32_t ddrBank, bool isRead,
	  uint64_t totalBytes, uint64_t totalTranx, double totalKernelTimeMsec,
	  double totalTransferTimeMsec, double maxTransferRateMBps) const
  {
    std::string transferType = (isRead) ? "READ" : "WRITE";

    writer->writeKernelTransferSummary(deviceName, cuPortName, argNames, ddrBank, transferType,
    	totalBytes, totalTranx, totalKernelTimeMsec, totalTransferTimeMsec, maxTransferRateMBps);
  }

  void PerformanceCounter::writeTopKernelTransferSummary(
      WriterI* writer, std::string& deviceName, std::string &cuName,
      uint64_t totalWriteBytes, uint64_t totalReadBytes,
      uint64_t totalWriteTranx, uint64_t totalReadTranx,
      double totalWriteTimeMsec, double totalReadTimeMsec,
      uint32_t maxBytesPerTransfer, double maxTransferRateMBps) const
  {
    writer->writeTopKernelTransferSummary(
        deviceName, cuName,
        totalWriteBytes, totalReadBytes,
        totalWriteTranx, totalReadTranx,
        totalWriteTimeMsec, totalReadTimeMsec,
        maxBytesPerTransfer, maxTransferRateMBps);
  }

  void PerformanceCounter::writeDeviceTransferSummary(WriterI* writer, bool isRead) const
  {
    std::string transferType("DEVICE WRITE BUFFER");
    const BufferStats* bufferStat = &DeviceBufferWriteStat;
    if (isRead) {
      transferType = "DEVICE READ BUFFER";
      bufferStat = &DeviceBufferReadStat;
    }
    writer->writeSummary(transferType, *bufferStat);
  }

}
