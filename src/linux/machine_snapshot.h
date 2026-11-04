#pragma once
#ifdef __APPLE__
/* Added by the pinned host_lite build overlay. Never waits for the lock;
 * successful calls nest exactly like mach_enter and need mach_leave. */
int mach_try_enter(void);
#endif
