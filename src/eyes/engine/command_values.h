#pragma once
#include "eyes.h"
namespace Eyes {
bool parseExpression(const char* value, Expression& result);
bool parseColor(const char* value, uint16_t& result);
bool parseInteger(const char* value, long& result);
}
