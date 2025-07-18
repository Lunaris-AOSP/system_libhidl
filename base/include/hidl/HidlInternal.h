/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_HIDL_INTERNAL_H
#define ANDROID_HIDL_INTERNAL_H

#include <cstdint>
#include <dirent.h>
#include <functional>
#include <string>
#include <vector>
#include <utility>

namespace android {
namespace hardware {
namespace details {

// tag for pure interfaces (e.x. IFoo)
struct i_tag {};

// tag for server interfaces (e.x. BnHwFoo)
struct bnhw_tag {};

// tag for proxy interfaces (e.x. BpHwFoo)
struct bphw_tag {};

// tag for passthrough interfaces (e.x. BsFoo)
struct bs_tag {};

//Templated classes can use the below method
//to avoid creating dependencies on liblog.
void logAlwaysFatal(const char *message);

// Returns VNDK-SP hw path according to "ro.vndk.version"
#if defined(__LP64__)
std::string getVndkSpHwPath(const char* lib = "lib64");
#else
std::string getVndkSpHwPath(const char* lib = "lib");
#endif

// Explicitly invokes the parameterized element's destructor;
// intended to be used alongside the placement new operator.
template<typename T>
void destructElement(T* element) {
    if (element != nullptr) {
        element->~T();
    }
}

// HIDL client/server code should *NOT* use this class.
//
// hidl_pointer wraps a pointer without taking ownership,
// and stores it in a union with a uint64_t. This ensures
// that we always have enough space to store a pointer,
// regardless of whether we're running in a 32-bit or 64-bit
// process.
template<typename T>
struct hidl_pointer {
    hidl_pointer()
        : _pad(0) {
        static_assert(sizeof(*this) == 8, "wrong size");
    }
    hidl_pointer(T* ptr) : hidl_pointer() { mPointer = ptr; }
    hidl_pointer(const hidl_pointer<T>& other) : hidl_pointer() { mPointer = other.mPointer; }
    hidl_pointer(hidl_pointer<T>&& other) noexcept : hidl_pointer() { *this = std::move(other); }

    hidl_pointer &operator=(const hidl_pointer<T>& other) {
        mPointer = other.mPointer;
        return *this;
    }
    hidl_pointer& operator=(hidl_pointer<T>&& other) noexcept {
        mPointer = other.mPointer;
        other.mPointer = nullptr;
        return *this;
    }
    hidl_pointer &operator=(T* ptr) {
        mPointer = ptr;
        return *this;
    }

    operator T*() const {
        return mPointer;
    }
    explicit operator void*() const { // requires explicit cast to avoid ambiguity
        return mPointer;
    }
    T& operator*() const {
        return *mPointer;
    }
    T* operator->() const {
        return mPointer;
    }
    T &operator[](size_t index) {
        return mPointer[index];
    }
    const T &operator[](size_t index) const {
        return mPointer[index];
    }

private:
    union {
        T* mPointer;
        uint64_t _pad;
    };
};

#define HAL_LIBRARY_PATH_SYSTEM_64BIT "/system/lib64/hw/"
#define HAL_LIBRARY_PATH_SYSTEM_EXT_64BIT "/system_ext/lib64/hw/"
#define HAL_LIBRARY_PATH_VENDOR_64BIT "/vendor/lib64/hw/"
#define HAL_LIBRARY_PATH_ODM_64BIT    "/odm/lib64/hw/"
#define HAL_LIBRARY_PATH_SYSTEM_32BIT "/system/lib/hw/"
#define HAL_LIBRARY_PATH_SYSTEM_EXT_32BIT "/system_ext/lib/hw/"
#define HAL_LIBRARY_PATH_VENDOR_32BIT "/vendor/lib/hw/"
#define HAL_LIBRARY_PATH_ODM_32BIT    "/odm/lib/hw/"

#if defined(__LP64__)
#define HAL_LIBRARY_PATH_SYSTEM HAL_LIBRARY_PATH_SYSTEM_64BIT
#define HAL_LIBRARY_PATH_SYSTEM_EXT HAL_LIBRARY_PATH_SYSTEM_EXT_64BIT
#define HAL_LIBRARY_PATH_VENDOR HAL_LIBRARY_PATH_VENDOR_64BIT
#define HAL_LIBRARY_PATH_ODM    HAL_LIBRARY_PATH_ODM_64BIT
#else
#define HAL_LIBRARY_PATH_SYSTEM HAL_LIBRARY_PATH_SYSTEM_32BIT
#define HAL_LIBRARY_PATH_SYSTEM_EXT HAL_LIBRARY_PATH_SYSTEM_EXT_32BIT
#define HAL_LIBRARY_PATH_VENDOR HAL_LIBRARY_PATH_VENDOR_32BIT
#define HAL_LIBRARY_PATH_ODM    HAL_LIBRARY_PATH_ODM_32BIT
#endif

// ----------------------------------------------------------------------
// Class that provides Hidl instrumentation utilities.
struct HidlInstrumentor {
    // Event that triggers the instrumentation. e.g. enter of an API call on
    // the server/client side, exit of an API call on the server/client side
    // etc.
    enum InstrumentationEvent {
        SERVER_API_ENTRY = 0,
        SERVER_API_EXIT,
        CLIENT_API_ENTRY,
        CLIENT_API_EXIT,
        SYNC_CALLBACK_ENTRY,
        SYNC_CALLBACK_EXIT,
        ASYNC_CALLBACK_ENTRY,
        ASYNC_CALLBACK_EXIT,
        PASSTHROUGH_ENTRY,
        PASSTHROUGH_EXIT,
    };

    // Signature of the instrumentation callback function.
    using InstrumentationCallback = std::function<void(
            const InstrumentationEvent event,
            const char *package,
            const char *version,
            const char *interface,
            const char *method,
            std::vector<void *> *args)>;

    explicit HidlInstrumentor(
            const std::string &package,
            const std::string &insterface);
    virtual ~HidlInstrumentor();

   public:
    const std::vector<InstrumentationCallback>& getInstrumentationCallbacks() {
        return mInstrumentationCallbacks;
    }
    bool isInstrumentationEnabled() { return mEnableInstrumentation; }

   protected:
    // Set mEnableInstrumentation based on system property
    // hal.instrumentation.enable, register/de-register instrumentation
    // callbacks if mEnableInstrumentation is true/false.
    void configureInstrumentation(bool log=true);
    // Function that lookup and dynamically loads the hidl instrumentation
    // libraries and registers the instrumentation callback functions.
    //
    // The instrumentation libraries should be stored under any of the following
    // directories: HAL_LIBRARY_PATH_SYSTEM, HAL_LIBRARY_PATH_VNDK_SP,
    // HAL_LIBRARY_PATH_VENDOR and HAL_LIBRARY_PATH_ODM.
    // The name of instrumentation libraries should follow pattern:
    // ^profilerPrefix(.*).profiler.so$
    //
    // Each instrumentation library is expected to implement the instrumentation
    // function called HIDL_INSTRUMENTATION_FUNCTION.
    //
    // A no-op for user build.
    void registerInstrumentationCallbacks(
            std::vector<InstrumentationCallback> *instrumentationCallbacks);

    // Utility function to determine whether a give file is a instrumentation
    // library (i.e. the file name follow the expected pattern).
    bool isInstrumentationLib(const dirent *file);

    // A list of registered instrumentation callbacks.
    std::vector<InstrumentationCallback> mInstrumentationCallbacks;
    // Flag whether to enable instrumentation.
    union {
        bool mEnableInstrumentation;
        void* mReserved0;
    };
    // Prefix to lookup the instrumentation libraries.
    std::string mInstrumentationLibPackage;
    // Used for dlsym to load the profiling method for given interface.
    std::string mInterfaceName;

};

#ifdef __LP64__
static_assert(sizeof(HidlInstrumentor) == 88, "HidlInstrumentor size frozen by prebuilts");
#else
static_assert(sizeof(HidlInstrumentor) == 44, "HidlInstrumentor size frozen by prebuilts");
#endif

}  // namespace details
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HIDL_INTERNAL_H
