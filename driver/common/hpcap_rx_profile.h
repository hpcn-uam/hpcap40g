#ifndef HPCAP_PROFILE_H
#define HPCAP_PROFILE_H

#include "hpcap_types.h"

#define PROFILE_SAMPLES 5000000

#ifdef HPCAP_PROFILING

void hpcap_profile_mark_batch(struct hpcap_profile* prof, size_t bytes, size_t frames, size_t descriptors, short owns_next_rxd, short budget_exhausted);
void hpcap_profile_mark_rxd_return(struct hpcap_profile* prof, size_t rxd_index);
void hpcap_profile_try_print(struct hpcap_profile* prof, struct hpcap_rx_thinfo* thinfo);
void hpcap_profile_reset(struct hpcap_profile* prof);
void hpcap_profile_mark_schedtimeout_start(struct hpcap_profile* prof);
void hpcap_profile_mark_schedtimeout_end(struct hpcap_profile* prof);
#else

#define hpcap_profile_mark_batch(a, b, c, d, e, f) do {} while (0)
#define hpcap_profile_mark_rxd_return(a, b) do {} while (0)
#define hpcap_profile_try_print(a, b) do {} while (0)
#define hpcap_profile_reset(a) do {} while (0)
#define hpcap_profile_mark_schedtimeout_start(a) do {} while (0)
#define hpcap_profile_mark_schedtimeout_end(a) do {} while (0)

#endif

#endif
