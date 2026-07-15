/*
 * SPDX-FileCopyrightText: 2024 The Android Open Source Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ConsumerIr.h"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

using aidl::android::hardware::ir::ConsumerIr;

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    std::shared_ptr<ConsumerIr> hal = ::ndk::SharedRefBase::make<ConsumerIr>();

    if (!hal->isValid()) {
        LOG(ERROR) << "ConsumerIr HAL failed to initialize, exiting";
        return EXIT_FAILURE;
    }

    const std::string instance = std::string(ConsumerIr::descriptor) + "/default";
    binder_status_t status = AServiceManager_addService(hal->asBinder().get(), instance.c_str());
    CHECK_EQ(status, STATUS_OK);

    LOG(INFO) << "ConsumerIr AIDL service registered";

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;
}
