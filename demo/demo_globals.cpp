/**
 * @file demo_globals.cpp
 * @brief Definition of the global PMM manager active flag.
 */

#include "demo_globals.h"

namespace demo
{

std::atomic<bool> g_pmm{ false };

} // namespace demo
