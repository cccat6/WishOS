#pragma once
#include "ResourceBudget.h"
#include <ArduinoJson.h>
namespace notesos {
// Temporary control metadata uses the same allocator as other system work.
class BudgetJson:public ArduinoJson::Allocator {
 ResourceBudget*resources_;
public:
 explicit BudgetJson(ResourceBudget&r):resources_(&r){}
 explicit BudgetJson(ResourceBudget*r=nullptr):resources_(r){}
 ResourceBudget*resources()const{return resources_;}
 void*allocate(size_t n)override{return resources_?resources_->allocate(n):malloc(n);}
 void deallocate(void*p)override{if(resources_)resources_->freeAllocation(p);else free(p);}
 void*reallocate(void*p,size_t n)override{return resources_?resources_->reallocate(p,n):realloc(p,n);}
};
}
