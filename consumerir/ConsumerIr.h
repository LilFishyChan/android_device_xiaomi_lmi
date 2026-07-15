/*
 * SPDX-FileCopyrightText: 2024 The Android Open Source Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/android/hardware/ir/BnConsumerIr.h>

namespace aidl {
namespace android {
namespace hardware {
namespace ir {

class ConsumerIr : public BnConsumerIr {
  public:
    ConsumerIr();
    ~ConsumerIr();

    bool isValid() const { return mFd >= 0; }

    ::ndk::ScopedAStatus getCarrierFreqs(
            ::std::vector<ConsumerIrFreqRange>* _aidl_return) override;
    ::ndk::ScopedAStatus transmit(int32_t carrierFreqHz,
                                   const ::std::vector<int32_t>& pattern) override;

  private:
    static constexpr int32_t kMinFreq = 30000;
    static constexpr int32_t kMaxFreq = 60000;
    static constexpr uint32_t kMaxTotalTime = 0x111112;  // ~1.12s
    int mFd;
};

}  // namespace ir
}  // namespace hardware
}  // namespace android
}  // namespace aidl
