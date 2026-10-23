#include "Context.h"

namespace scotykach::shim {

namespace {
Context              fallback;
thread_local Context* current = nullptr;
} // namespace

Context& Context::Current()
{
    return current != nullptr ? *current : fallback;
}

Context::Scope::Scope(Context& c) : previous(current)
{
    current = &c;
}

Context::Scope::~Scope()
{
    current = previous;
}

} // namespace scotykach::shim
