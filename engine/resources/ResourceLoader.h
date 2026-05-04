#pragma once

#include <string>

template <typename T>
struct ResourceLoadResult
{
    bool success = false;
    T data{};
    std::string errorMessage;
};
