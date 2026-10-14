# Actuator capabilities

Only use a registered actuator instance. The generic service supplies ownership,
bounded native execution, operation receipts and cleanup; a driver describes its
actual command fields, units, limits, feedback and native stop behavior. No current
Agentic Notes or C3 runtime package advertises a physical actuator.

Acquire its exclusive lease before commanding it. Follow the advertised lease
duration and renew while the application intends to retain control. A native lease
expiry or owner exit issues the driver's stop independently of a Lua callback or
network response. Do not disable bounds unless the driver explicitly permits it
and the application deliberately selects that policy.

Use the returned lease/command identities for changes, completion and cancellation.
Retarget only when advertised, with the explicit replacement option. Rejected
parameters preserve the current command. A replaced command gets a superseded
receipt; an issued stop is not a measurement that motion has stopped. Check the
driver's completion/feedback basis before describing physical results.

Motion sequences, navigation, choreography and remote control routing are runtime
application logic. A UART-connected motor controller and an on-board motor driver
may implement the same resource contract with different native restrictions.
Physical closed-loop guarantees require hardware feedback actually provided by
that adapter; simulation fixtures validate lifecycle behavior only.
