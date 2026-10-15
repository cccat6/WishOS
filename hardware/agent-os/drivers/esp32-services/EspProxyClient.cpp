#include "EspProxyClient.h"
namespace notesos {NetworkProxy networkProxy;}
// Factory hooks in the pinned WebSockets adapter. HTTP uses the same clients.
NetworkClient*wishosCreateNetworkClient(){return new notesos::ProxyClient();}
NetworkClientSecure*wishosCreateSecureClient(){return new notesos::ProxySecureClient();}
