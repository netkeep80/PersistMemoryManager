/**
 * @file demo_globals.cpp
 * @brief Definition of the global PMM manager pointer.
 */

#include "demo_globals.h"

namespace demo
{

std::atomic<DemoMgr*> g_pmm{ nullptr };

} // namespace demo
