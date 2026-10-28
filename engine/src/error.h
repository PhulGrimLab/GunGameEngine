#pragma once
#include <stdexcept>
#include <string>

namespace gge {

inline std::string& LastError()
{
    thread_local std::string error;
    return error;
}

}  // namespace gge
