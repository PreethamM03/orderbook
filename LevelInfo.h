#pragma once

#include "Usings.h"

struct level_info
{
    Price price_;
    Quantity quantity_;
};  

using LevelInfos = std::vector<level_info>;