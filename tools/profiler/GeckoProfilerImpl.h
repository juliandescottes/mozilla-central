/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef TOOLS_SPS_SAMPLER_H_
#define TOOLS_SPS_SAMPLER_H_

#include <stdlib.h>
#include <signal.h>
#include <stdarg.h>
#include <algorithm>
#include "mozilla/ThreadLocal.h"
#include "mozilla/Assertions.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/Util.h"
#include "nsAlgorithm.h"
#include "nscore.h"
#include "jsfriendapi.h"
#include "GeckoProfilerFunc.h"
#include "PseudoStack.h"


/* QT has a #define for the word "slots" and jsfriendapi.h has a struct with
 * this variable name, causing compilation problems. Alleviate this for now by
 * removing this #define */
#ifdef MOZ_WIDGET_QT
#undef slots
#endif

// Make sure that we can use std::min here without the Windows headers messing with us.
#ifdef min
#undef min
#endif

struct PseudoStack;
class TableTicker;
class JSCustomObject;

extern mozilla::ThreadLocal<PseudoStack *> tlsPseudoStack;
extern mozilla::ThreadLocal<TableTicker *> tlsTicker;
extern bool stack_key_initialized;

#ifndef SAMPLE_FUNCTION_NAME
# ifdef __GNUC__
#  define SAMPLE_FUNCTION_NAME __FUNCTION__
# elif defined(_MSC_VER)
#  define SAMPLE_FUNCTION_NAME __FUNCTION__
# else
#  define SAMPLE_FUNCTION_NAME __func__  // defined in C99, supported in various C++ compilers. Just raw function name.
# endif
#endif

static inline
void profiler_init()
{
  mozilla_sampler_init();
}

static inline
void profiler_shutdown()
{
  mozilla_sampler_shutdown();
}

static inline
void profiler_start(int aProfileEntries, int aInterval,
                       const char** aFeatures, uint32_t aFeatureCount)
{
  mozilla_sampler_start(aProfileEntries, aInterval, aFeatures, aFeatureCount);
}

static inline
void profiler_stop()
{
  mozilla_sampler_stop();
}

static inline
bool profiler_is_active()
{
  return mozilla_sampler_is_active();
}

static inline
void profiler_responsiveness(const TimeStamp& aTime)
{
  mozilla_sampler_responsiveness(aTime);
}

static inline
const double* profiler_get_responsiveness()
{
  return mozilla_sampler_get_responsiveness();
}

static inline
void profiler_set_frame_number(int frameNumber)
{
  return mozilla_sampler_frame_number(frameNumber);
}

static inline
char* profiler_get_profile()
{
  return mozilla_sampler_get_profile();
}

static inline
JSObject* profiler_get_profile_jsobject(JSContext* aCx)
{
  return mozilla_sampler_get_profile_data(aCx);
}

static inline
const char** profiler_get_features()
{
  return mozilla_sampler_get_features();
}

static inline
void profiler_print_location()
{
  if (!sps_version2()) {
    return mozilla_sampler_print_location1();
  } else {
    return mozilla_sampler_print_location2();
  }
}

static inline
void profiler_lock()
{
  return mozilla_sampler_lock();
}

static inline
void profiler_unlock()
{
  return mozilla_sampler_unlock();
}

static inline
void profiler_register_thread(const char* name)
{
  mozilla_sampler_register_thread(name);
}

static inline
void profiler_unregister_thread()
{
  mozilla_sampler_unregister_thread();
}

static inline
void profiler_js_operation_callback()
{
  PseudoStack *stack = tlsPseudoStack.get();
  if (!stack) {
    return;
  }

  stack->jsOperationCallback();
}

static inline
double profiler_time()
{
  return mozilla_sampler_time();
}

// we want the class and function name but can't easily get that using preprocessor macros
// __func__ doesn't have the class name and __PRETTY_FUNCTION__ has the parameters

#define SAMPLER_APPEND_LINE_NUMBER_PASTE(id, line) id ## line
#define SAMPLER_APPEND_LINE_NUMBER_EXPAND(id, line) SAMPLER_APPEND_LINE_NUMBER_PASTE(id, line)
#define SAMPLER_APPEND_LINE_NUMBER(id) SAMPLER_APPEND_LINE_NUMBER_EXPAND(id, __LINE__)

#define PROFILER_LABEL(name_space, info) mozilla::SamplerStackFrameRAII SAMPLER_APPEND_LINE_NUMBER(sampler_raii)(name_space "::" info, __LINE__)
#define PROFILER_LABEL_PRINTF(name_space, info, ...) mozilla::SamplerStackFramePrintfRAII SAMPLER_APPEND_LINE_NUMBER(sampler_raii)(name_space "::" info, __LINE__, __VA_ARGS__)
#define PROFILER_MARKER(info) mozilla_sampler_add_marker(info)
#define PROFILER_MAIN_THREAD_LABEL(name_space, info)  MOZ_ASSERT(NS_IsMainThread(), "This can only be called on the main thread"); mozilla::SamplerStackFrameRAII SAMPLER_APPEND_LINE_NUMBER(sampler_raii)(name_space "::" info, __LINE__)
#define PROFILER_MAIN_THREAD_LABEL_PRINTF(name_space, info, ...)  MOZ_ASSERT(NS_IsMainThread(), "This can only be called on the main thread"); mozilla::SamplerStackFramePrintfRAII SAMPLER_APPEND_LINE_NUMBER(sampler_raii)(name_space "::" info, __LINE__, __VA_ARGS__)
#define PROFILER_MAIN_THREAD_MARKER(info)  MOZ_ASSERT(NS_IsMainThread(), "This can only be called on the main thread"); mozilla_sampler_add_marker(info)


/* FIXME/bug 789667: memory constraints wouldn't much of a problem for
 * this small a sample buffer size, except that serializing the
 * profile data is extremely, unnecessarily memory intensive. */
#ifdef MOZ_WIDGET_GONK
# define PLATFORM_LIKELY_MEMORY_CONSTRAINED
#endif

#if !defined(PLATFORM_LIKELY_MEMORY_CONSTRAINED) && !defined(ARCH_ARMV6)
# define PROFILE_DEFAULT_ENTRY 1000000
#else
# define PROFILE_DEFAULT_ENTRY 100000
#endif

#if defined(PLATFORM_LIKELY_MEMORY_CONSTRAINED)
/* A 1ms sampling interval has been shown to be a large perf hit
 * (10fps) on memory-contrained (low-end) platforms, and additionally
 * to yield different results from the profiler.  Where this is the
 * important case, b2g, there are also many gecko processes which
 * magnify these effects. */
# define PROFILE_DEFAULT_INTERVAL 10
#elif defined(ANDROID)
// We use a lower frequency on Android, in order to make things work
// more smoothly on phones.  This value can be adjusted later with
// some libunwind optimizations.
// In one sample measurement on Galaxy Nexus, out of about 700 backtraces,
// 60 of them took more than 25ms, and the average and standard deviation
// were 6.17ms and 9.71ms respectively.

// For now since we don't support stackwalking let's use 1ms since it's fast
// enough.
#define PROFILE_DEFAULT_INTERVAL 1
#else
#define PROFILE_DEFAULT_INTERVAL 1
#endif
#define PROFILE_DEFAULT_FEATURES NULL
#define PROFILE_DEFAULT_FEATURE_COUNT 0

namespace mozilla {

class MOZ_STACK_CLASS SamplerStackFrameRAII {
public:
  // we only copy the strings at save time, so to take multiple parameters we'd need to copy them then.
  SamplerStackFrameRAII(const char *aInfo, uint32_t line) {
    mHandle = mozilla_sampler_call_enter(aInfo, this, false, line);
  }
  ~SamplerStackFrameRAII() {
    mozilla_sampler_call_exit(mHandle);
  }
private:
  void* mHandle;
};

static const int SAMPLER_MAX_STRING = 128;
class MOZ_STACK_CLASS SamplerStackFramePrintfRAII {
public:
  // we only copy the strings at save time, so to take multiple parameters we'd need to copy them then.
  SamplerStackFramePrintfRAII(const char *aDefault, uint32_t line, const char *aFormat, ...) {
    if (profiler_is_active()) {
      va_list args;
      va_start(args, aFormat);
      char buff[SAMPLER_MAX_STRING];

      // We have to use seperate printf's because we're using
      // the vargs.
#if _MSC_VER
      _vsnprintf(buff, SAMPLER_MAX_STRING, aFormat, args);
      _snprintf(mDest, SAMPLER_MAX_STRING, "%s %s", aDefault, buff);
#else
      vsnprintf(buff, SAMPLER_MAX_STRING, aFormat, args);
      snprintf(mDest, SAMPLER_MAX_STRING, "%s %s", aDefault, buff);
#endif
      mHandle = mozilla_sampler_call_enter(mDest, this, true, line);
      va_end(args);
    } else {
      mHandle = mozilla_sampler_call_enter(aDefault, NULL, false, line);
    }
  }
  ~SamplerStackFramePrintfRAII() {
    mozilla_sampler_call_exit(mHandle);
  }
private:
  char mDest[SAMPLER_MAX_STRING];
  void* mHandle;
};

} //mozilla

inline PseudoStack* mozilla_get_pseudo_stack(void)
{
  if (!stack_key_initialized)
    return NULL;
  return tlsPseudoStack.get();
}

inline void* mozilla_sampler_call_enter(const char *aInfo, void *aFrameAddress,
                                        bool aCopy, uint32_t line)
{
  // check if we've been initialized to avoid calling pthread_getspecific
  // with a null tlsStack which will return undefined results.
  if (!stack_key_initialized)
    return NULL;

  PseudoStack *stack = tlsPseudoStack.get();
  // we can't infer whether 'stack' has been initialized
  // based on the value of stack_key_intiailized because
  // 'stack' is only intialized when a thread is being
  // profiled.
  if (!stack) {
    return stack;
  }
  stack->push(aInfo, aFrameAddress, aCopy, line);

  // The handle is meant to support future changes
  // but for now it is simply use to save a call to
  // pthread_getspecific on exit. It also supports the
  // case where the sampler is initialized between
  // enter and exit.
  return stack;
}

inline void mozilla_sampler_call_exit(void *aHandle)
{
  if (!aHandle)
    return;

  PseudoStack *stack = (PseudoStack*)aHandle;
  stack->pop();
}

inline void mozilla_sampler_add_marker(const char *aMarker)
{
  if (!stack_key_initialized)
    return;

  // Don't insert a marker if we're not profiling to avoid
  // the heap copy (malloc).
  if (!profiler_is_active()) {
    return;
  }

  PseudoStack *stack = tlsPseudoStack.get();
  if (!stack) {
    return;
  }
  stack->addMarker(aMarker);
}

#endif /* ndef TOOLS_SPS_SAMPLER_H_ */
