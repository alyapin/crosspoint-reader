// Arduino's NetworkManager::hostByName() calls dns_clear_cache() from the app
// task without the lwIP core lock. Clearing aborts in-flight lookups and runs
// their callbacks on that task (SNTP's calls sys_untimeout), tripping lwIP's
// thread-safety assert. Hold the lock unless the caller already does.
#ifdef ESP_PLATFORM
#include <lwip/dns.h>
#include <lwip/tcpip.h>

#if !LWIP_TCPIP_CORE_LOCKING
#error "__wrap_dns_clear_cache needs CONFIG_LWIP_TCPIP_CORE_LOCKING"
#endif

void __real_dns_clear_cache(void);

void __wrap_dns_clear_cache(void) {
  if (sys_thread_tcpip(LWIP_CORE_LOCK_QUERY_HOLDER)) {
    __real_dns_clear_cache();
    return;
  }
  LOCK_TCPIP_CORE();
  __real_dns_clear_cache();
  UNLOCK_TCPIP_CORE();
}
#endif
