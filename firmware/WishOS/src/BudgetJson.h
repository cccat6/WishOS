#pragma once
#include "ResourceBudget.h"
#include <ArduinoJson.h>
namespace notesos {
// Temporary control metadata uses the same allocator as other system work.
class BudgetJson:public ArduinoJson::Allocator {
 ResourceBudget&resources_;
public:
 explicit BudgetJson(ResourceBudget&r):resources_(r){}
 void*allocate(size_t n)override{return resources_.allocate(n);}
 void deallocate(void*p)override{resources_.freeAllocation(p);}
 void*reallocate(void*p,size_t n)override{return resources_.reallocate(p,n);}
};
}
