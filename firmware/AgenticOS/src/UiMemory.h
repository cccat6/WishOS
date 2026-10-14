#pragma once
#include "ResourceBudget.h"
namespace notesos {
bool bindUiMemory(ResourceBudget&);
void clearUiMemoryFailure();
bool uiMemoryFailed();
size_t uiMemoryUsed();
}
