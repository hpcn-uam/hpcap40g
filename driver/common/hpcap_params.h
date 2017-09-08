#ifndef PARAMS_H
#define PARAMS_H

#include "hpcap_types.h"

#include <linux/module.h>

// From the IXGBE driver
//
#define OPTION_UNSET	-1
#define OPTION_DISABLED	0
#define OPTION_ENABLED	1

#define DEFAULT_PARAM_INIT { [0 ... HPCAP_MAX_NIC] = OPTION_UNSET }
#ifndef module_param_array
/* Module Parameters are always initialized to -1, so that the driver
 * can tell the difference between no user specified value or the
 * user asking for the default value.
 * The true default values are loaded in when ixgbe_check_options is called.
 *
 * This is a GCC extension to ANSI C.
 * See the item "Labeled Elements in Initializers" in the section
 * "Extensions to the C Language Family" of the GCC documentation.
 */

#define DRIVER_PARAM(X, desc) \
	const static int X[HPCAP_MAX_NIC+1] = DEFAULT_PARAM_INIT; \
	MODULE_PARM(X, "1-" __MODULE_STRING(HPCAP_MAX_NIC) "i"); \
	MODULE_PARM_DESC(X, desc);
#else
#define DRIVER_PARAM(X, desc) \
	static int X[HPCAP_MAX_NIC+1] = DEFAULT_PARAM_INIT; \
	static unsigned int num_##X; \
	module_param_array_named(X, X, int, &num_##X, 0); \
	MODULE_PARM_DESC(X, desc);
#endif // module_param_array

int hpcap_precheck_options(void);
int hpcap_parse_opts(HW_ADAPTER* adapter);
size_t hpcap_get_consumer_count(void);

#endif
