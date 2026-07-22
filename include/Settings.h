#pragma once

#include "REX/REX.h"

class Config : public REX::Singleton<Config> {
public:
    bool ModActive{true};
    bool EnableTimeLogging{false};
};