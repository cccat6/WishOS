#pragma once
#include <stdint.h>
namespace notesos {
// An input adapter passes the host's actual visible presentation as capture
// metadata. Application code decides how to handle input from an older frame.
struct InputPresentation {
 uint32_t view=0,revision=0,activation=0;
 bool operator==(const InputPresentation&o)const{return view==o.view&&revision==o.revision&&activation==o.activation;}
 bool operator!=(const InputPresentation&o)const{return !(*this==o);}
};
}
