/*
 * SPDX-FileCopyrightText: 2024 The Android Open Source Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * Based on reverse-engineered MIUI consumerir.default.so for Xiaomi LMI (kona).
 * The original HAL performs software carrier modulation: Android API pulse/space
 * timing values are converted to a 38kHz-modulated SPI bitstream and sent to
 * the /dev/ir_spi driver via LIRC_SET_SEND_MODE + write().
 */

#define LOG_TAG "ConsumerIrService.xiaomi_lmi"

#include <errno.h>
#include <fcntl.h>
#include <linux/lirc.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <android-base/logging.h>

#include "ConsumerIr.h"

#define IR_SPI_DEVICE "/dev/ir_spi"

// ---- MIUI HAL constants (from reverse-engineered consumerir.default.so) ----

// timing → bit-count conversion: bit_count ≈ total_us * 1920 * K / 2^38
static constexpr int32_t  kTimingBase       = 758;       // calibrated for lmi SPI clock
static constexpr int64_t  kBitCountMul      = 0x10624dd3LL;
static constexpr int32_t  kBitCountShift    = 38;
static constexpr int32_t  kAlignMask        = 0x3FF;      // align to 1024

// duty-cycle calculation for carrier modulation
static constexpr int32_t  kDutyBase         = 1919488;    // 0x1d4c00
static constexpr int64_t  kDutyDivMul       = 0x51eb851fLL;
static constexpr int32_t  kDutyDivShift     = 35;

// millisecond-position calculation
static constexpr uint64_t kMsPosMul         = 0x88888889ULL;
static constexpr int32_t  kMsPosShift       = 42;

namespace aidl {
namespace android {
namespace hardware {
namespace ir {

ConsumerIr::ConsumerIr() : mFd(-1) {
    mFd = open(IR_SPI_DEVICE, O_WRONLY);
    if (mFd < 0) {
        LOG(ERROR) << "Failed to open " << IR_SPI_DEVICE << ": " << strerror(errno);
        return;
    }
    LOG(INFO) << "Opened " << IR_SPI_DEVICE << " fd=" << mFd;
}

ConsumerIr::~ConsumerIr() {
    if (mFd >= 0) {
        close(mFd);
        mFd = -1;
    }
}

::ndk::ScopedAStatus ConsumerIr::transmit(int32_t carrierFreqHz,
                                           const ::std::vector<int32_t>& pattern) {
    size_t entries = pattern.size();

    LOG(INFO) << "transmit freq=" << carrierFreqHz << " entries=" << entries;

    if (mFd < 0) {
        LOG(ERROR) << "Device not open";
        return ::ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }

    if (entries == 0) {
        return ::ndk::ScopedAStatus::ok();
    }

    // ---- Step 1: build cumulative timing array (MIUI: modified[i] = Σ pattern[0..i]) ----
    auto modified = std::make_unique<int32_t[]>(entries);
    int32_t running_total = 0;
    for (size_t i = 0; i < entries; i++) {
        int32_t val = pattern[i];
        if (val <= 0) val = 1;
        running_total += val;
        modified[i] = running_total;
    }

    if (static_cast<uint32_t>(running_total) >= kMaxTotalTime) {
        LOG(ERROR) << "total_time out of range " << running_total;
        return ::ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }

    // ---- Step 2: compute bit-slot count ----
    int64_t t = static_cast<int64_t>(running_total) * kTimingBase;
    t = t * kBitCountMul;
    int32_t sign_bit = static_cast<int32_t>(t >> 63);
    int32_t bit_count = static_cast<int32_t>(t >> kBitCountShift) + sign_bit;

    // align up to 1024 boundary
    if (bit_count & kAlignMask) {
        if (bit_count < 0) bit_count = 0;
        bit_count = (bit_count + kAlignMask) & ~kAlignMask;
    }

    LOG(INFO) << "total_time=" << running_total << " bit_count=" << bit_count;

    // ---- Step 3: allocate marker + output buffers ----
    auto marker = std::make_unique<uint8_t[]>(bit_count);
    auto output = std::make_unique<uint32_t[]>(bit_count);
    memset(marker.get(), 0, bit_count);
    memset(output.get(), 0, bit_count * sizeof(uint32_t));

    // ---- Step 4: compute carrier duty limit ----
    // duty_limit = how many time-slots equal half a carrier period
    int32_t duty_base = kDutyBase + (static_cast<int32_t>(entries) >> 1);
    int32_t duty_base_scaled = (duty_base / (carrierFreqHz > 0 ? carrierFreqHz : 1)) * 10;
    int64_t d64 = static_cast<int64_t>(duty_base_scaled) * kDutyDivMul;
    int32_t duty_limit = static_cast<int32_t>(d64 >> kDutyDivShift)
                       + static_cast<int32_t>(d64 >> 63);

    LOG(INFO) << "duty_limit=" << duty_limit;

    // ---- Step 5: RLE encoding ----
    int32_t  output_idx    = 0;
    int32_t  duty_cnt      = 0;
    int32_t  bit_shift     = 31;
    int32_t  pat_idx       = 0;

    for (int32_t slot = 0; slot < bit_count; slot++) {
        // find which pattern interval this time-slot falls into
        int32_t ms_pos;
        {
            uint64_t pos = static_cast<uint64_t>(slot) * kMsPosMul;
            ms_pos = static_cast<int32_t>(pos >> kMsPosShift);
        }

        while (pat_idx < static_cast<int32_t>(entries)) {
            uint64_t v = static_cast<uint64_t>(modified[pat_idx]) * kMsPosMul;
            int32_t cur = static_cast<int32_t>(v >> kMsPosShift);
            if (cur >= ms_pos) break;
            pat_idx++;
        }

        if (pat_idx & 1) {
            // odd interval → OFF (space between carrier bursts)
            duty_cnt = 0;
            // Inverted: drive output HIGH during OFF period
            marker[slot] = 1;
            output[output_idx] |= (1U << bit_shift);
        } else {
            // even interval → ON (carrier modulated)
            duty_cnt++;
            if (duty_cnt >= duty_limit) {
                duty_cnt = 0;
            }
            // Inverted: drive output LOW during ON period
            if (duty_cnt >= (duty_limit >> 1)) {
                marker[slot] = 1;
                output[output_idx] |= (1U << bit_shift);
            }
        }

        bit_shift--;
        if (bit_shift < 0) {
            bit_shift = 31;
            output_idx++;
        }
    }

    // ---- Step 6: SPI transfer ----
    // only transfer the actual populated words, not the full slot count
    uint32_t word_count = output_idx + 1;
    uint32_t xfer_len = word_count * sizeof(uint32_t);
    if (ioctl(mFd, LIRC_SET_SEND_MODE, &xfer_len) < 0) {
        LOG(ERROR) << "ir-spi set length error: " << strerror(errno);
        return ::ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }

    ssize_t written = write(mFd, output.get(), xfer_len);
    if (written != static_cast<ssize_t>(xfer_len)) {
        LOG(ERROR) << "ir-spi write data error: " << strerror(errno);
        return ::ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }

    LOG(INFO) << "Transmitted " << word_count << " words (" << bit_count << " slots) at "
              << carrierFreqHz << " Hz";
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus ConsumerIr::getCarrierFreqs(
        ::std::vector<ConsumerIrFreqRange>* _aidl_return) {
    _aidl_return->push_back({kMinFreq, kMaxFreq});
    return ::ndk::ScopedAStatus::ok();
}

}  // namespace ir
}  // namespace hardware
}  // namespace android
}  // namespace aidl
