#pragma once

#include <stdexcept>
#include <string>

namespace pe_raw {

    class PeError : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

}
