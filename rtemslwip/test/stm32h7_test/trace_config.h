/* 
 * Uncomment the line below to ENABLE tracing.
 * Keep it commented out to DISABLE tracing (default).
 */
/* #define _TRACE_MODE_ */

#ifdef _TRACE_MODE_
  #define TRACE_PRINTF(...) printf(__VA_ARGS__)
#else
  #define TRACE_PRINTF(...) do { } while (0)
#endif