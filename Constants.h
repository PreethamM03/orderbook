#pragma once

# include <limits>

#include "Usings.h"

struct constants
{
    static const Price invalidPrice = std::numeric_limits<Price>::quiet_NaN();
};