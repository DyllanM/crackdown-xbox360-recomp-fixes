#pragma once

#include <cstdint>
#include <rex/logging/api.h>

#ifndef REX_RUNTIME_OBSERVER_API

namespace rex {
using RuntimeObserver = void (*)(const char*, const char*, uint64_t, uint64_t, uint64_t,
                                uint64_t, uint64_t, const char*, uint32_t, uint32_t);
void SetRuntimeObserver(RuntimeObserver observer);
void ObserveRuntime(const char* category, const char* event, uint64_t object = 0,
                    uint64_t a = 0, uint64_t b = 0, uint64_t c = 0, uint64_t d = 0,
                    const char* detail = nullptr);
}

#endif

void StartRuntimeHealth();
void StopRuntimeHealth();
void DumpRuntimeHistory(const char* reason, bool fault = false);
void RuntimeHealthGameplayStarted();
