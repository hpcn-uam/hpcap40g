#include "hpcap_params.h"
#include "hpcap_debug.h"
#include "driver_hpcap.h"

static size_t _consumers;

size_t hpcap_get_consumer_count()
{
	return _consumers;
}

struct hpcap_option {
	enum { enable_option, range_option, list_option } type;
	const char *name;
	const char *err;
	const char *msg;
	int def;
	union {
		struct { /* range_option info */
			int min;
			int max;
		} r;
		struct { /* list_option info */
			int nr;
			const struct hpcap_opt_list {
				int i;
				char *str;
			} *p;
		} l;
	} arg;
};

int hpcap_validate_option(unsigned int* value, struct hpcap_option* opt);


DRIVER_PARAM(Core, "set the starting core to allocate receive on, default -1");

/* RXQ - The number of RX queues
 *
 * Valid Range: 0-16
 *  - 0 - min(16, num_online_cpus())
 *  - 1-16 - sets the Desc. Q's to the specified value.
 *
 * Default Value: 1
 */
DRIVER_PARAM(RXQ, "Number of RX queues, default 1. 0 = number of cpus");

/* TXQ - The number of TX queues
 *
 * Valid Range: 0-16
 *  - 0 - min(16, num_online_cpus())
 *  - 1-16 - sets the Desc. Q's to the specified value.
 *
 * Default Value: 1
 */
DRIVER_PARAM(TXQ, "Number of TX queues, default 1. 0 = number of cpus");

/* Consumers - The number of consumers per queue.
 *
 * Valid Range: 0-16
 *  - 0 - min(16, num_online_cpus())
 *  - 1-16 - sets the Desc. Q's to the specified value.
 *
 * Default Value: 1
 */
DRIVER_PARAM(Consumers, "Number of threads reading data from the NIC, default 1. 0 = number of cpus");

/* Mode - working mode
 *
 * Valid Range: 1-3
 *  - 1 - standard ixgbe behaviour
 *  - 2 - high performance RX
 *
 * Default Value: 1
 */
DRIVER_PARAM(Mode, "RX mode (1=standard ixgbe, 2=high performance RX). Default 1");

/* Dup - switching duplicates policy
 *
 * Valid Range: 0-1
 *  - 0 - don't check for duplicated packets
 *  - 1 - remove witching duplicates
 *
 *  Default value: 0
 */
#ifndef REMOVE_DUPS
DRIVER_PARAM(Dup, "Dup (0=don't check, 1=remove switching duplicates). Default 0");
#else
//#define CADENA_DUP "Dup (0=don't check, 1=remove switching duplicates). Default 0 [CHECK_LEN " DUP_CHECK_LEN ", WINDOW_SIZE " DUP_WINDOW_SIZE ", WINDOW_LEVELS " DUP_WINDOW_LEVELS
#define CADENA_DUP "Dup (0=don't check, 1=remove switching duplicates). Default 0 [CHECK_LEN " SCL ", WINDOW_SIZE " SWS ", WINDOW_LEVELS " SWL ", TIME_WINDOW " STW "]"
DRIVER_PARAM(Dup, CADENA_DUP);
#endif

/* Pages - Amount of pages for the interfaces's kernel buffer in hpcap mode
 *
 * Valid Range: >=1
 *
 *  Default value: 0
 */
DRIVER_PARAM(Pages, "Pages (>=1). Amount of pages for the interfaces's kernel buffer in hpcap mode");

/* Caplen - maximum amount of bytes captured per packet
 *
 * Valid Range: >=0
 *  - 0 - full packet
 *
 * Default Value: 0
 */
DRIVER_PARAM(Caplen, "Capture length (BYTES). Default 0 (full packet).");


int hpcap_validate_option(unsigned int *value,
						  struct hpcap_option *opt)
{
	if (*value == OPTION_UNSET) {
		BPRINTK(INFO, "Invalid %s specified (%d),  %s\n",
				opt->name, *value, opt->err);
		*value = opt->def;
		return 0;
	}

	switch (opt->type) {
		case enable_option:
			switch (*value) {
				case OPTION_ENABLED:
					BPRINTK(INFO, "%s Enabled\n", opt->name);
					return 0;

				case OPTION_DISABLED:
					BPRINTK(INFO, "%s Disabled\n", opt->name);
					return 0;
			}

			break;

		case range_option:
			if ((*value >= opt->arg.r.min && *value <= opt->arg.r.max) ||
				*value == opt->def) {
				if (opt->msg)
					BPRINTK(INFO, "%s set to %d, %s\n",
							opt->name, *value, opt->msg);
				else
					BPRINTK(INFO, "%s set to %d\n",
							opt->name, *value);

				return 0;
			}

			break;

		default:
			BUG();
	}

	BPRINTK(INFO, "Invalid %s specified (%d),  %s\n",
			opt->name, *value, opt->err);
	*value = opt->def;
	return -1;
}

int hpcap_precheck_options(void)
{
	size_t option_count, i;
	size_t hpcap_assigned_pages = 0, adapter_pages, hpcap_max_pages = HPCAP_BUF_SIZE / PAGE_SIZE;
	int mode;

	_consumers = Consumers[0];

	BPRINTK(INFO, "Enabled HPCAP with %zu consumers\n", _consumers);

	option_count = HPCAP_MAX_NIC + 1;

	for (i = 0; i < option_count; i++) {
		mode = Mode[i];
		adapter_pages = Pages[i];

		if (mode != 1 && mode != 2)
			mode = HPCAP_DEFAULT_MODE; // Default mode is 1.

		if (adapter_pages < 1)
			adapter_pages = HPCAP_DEFAULT_PAGES_PER_ADAPTER;

		if (is_hpcap_mode(mode)) {
			printdbg(DBG_DRV, "Adapter %zu requests %zu pages.\n", i, adapter_pages);
			hpcap_assigned_pages += adapter_pages;

			if (hpcap_assigned_pages > hpcap_max_pages) {
				BPRINTK(ERR, "Page configuration is faulty: adapter %zu requests for %zu pages, "
						"but %zu out of %zu available pages have already been assigned\n",
						i, adapter_pages, hpcap_assigned_pages - adapter_pages, hpcap_max_pages);
				return -ENOMEM;
			}
		}
	}

	return 0;
}

int hpcap_parse_opts(HW_ADAPTER* adapter)
{
	int bd = adapter->bd_number;

#if defined(HPCAP_IXGBE)
	u32 *aflags = &adapter->flags;
	struct ixgbe_ring_feature *feature = adapter->ring_feature;
#endif

	if (bd >= HPCAP_MAX_NIC) {
		printk(KERN_NOTICE
			   "Warning: no configuration for board #%d\n", bd);
		printk(KERN_NOTICE "Using defaults for all values\n");
#ifndef module_param_array
		bd = HPCAP_MAX_NIC;
#endif
	}

	{ /* # of RX queues with RSS (RXQ) */
		static struct hpcap_option opt = {
			.type = range_option,
			.name = "RX queues (RXQ)",
			.err  = "using default.",
			.def  = 1,
			.arg  = {
				.r = {
					.min = 0,
					.max = HPCAP_MAX_RXQ
				}
			}
		};
		unsigned int rxq = RXQ[bd];

#ifdef module_param_array

		if (!(num_RXQ > bd))
			rxq = opt.def;

#endif

		switch (rxq) {
			case 0:
				/*
				* Base it off num_online_cpus() with
				* a hardware limit cap.
				*/
				rxq = min(HPCAP_MAX_RXQ, (int)num_online_cpus());
				break;

			default:
				hpcap_validate_option(&rxq, &opt);
				break;
		}

#if defined(HPCAP_IXGBE)
		feature[RING_F_RXQ].indices = rxq;
		*aflags |= IXGBE_FLAG_RSS_ENABLED;
#elif defined(HPCAP_MLNX) || defined(HPCAP_I40E)
		adapter->num_rx_queues = rxq;
#endif
	}

	{ /* # of TX queues (TXQ) */
		static struct hpcap_option opt = {
			.type = range_option,
			.name = "TX queues (TXQ)",
			.err  = "using default.",
			.def  = 1,
			.arg  = {
				.r = {
					.min = 0,
					.max = HPCAP_MAX_RXQ
				}
			}
		};
		unsigned int txq = TXQ[bd];

#ifdef module_param_array

		if (!(num_TXQ > bd))
			txq = opt.def;

#endif

		switch (txq) {
			case 0:
				/*
				* Base it off num_online_cpus() with
				* a hardware limit cap.
				*/
				txq = min(HPCAP_MAX_RXQ, (int)num_online_cpus());
				break;

			default:
				hpcap_validate_option(&txq, &opt);
				break;
		}

#if defined(HPCAP_IXGBE)
		feature[RING_F_TXQ].indices = txq;
#endif
	}

	{ /* CORE assignment */
		static struct hpcap_option opt = {
			.type = range_option,
			.name = "Core to copy from",
			.err  = "defaulting to 0",
			.def  = 0,
			.arg  = {
				.r = {
					.min = 0,
					.max = (MAX_NUMNODES - 1)
				}
			}
		};
		int core_param = opt.def;

		/* if the default was zero then we need to set the
		* default value to an online node, which is not
		* necessarily zero, and the constant initializer
		* above can't take first_online_node */
		if (core_param == 0)
			/* must set opt.def for validate */
			opt.def = core_param = first_online_node;

#ifdef module_param_array

		if (num_Core > bd) {
#endif
			core_param = Core[bd];
			hpcap_validate_option((uint *)&core_param, &opt);

			if (core_param != OPTION_UNSET)
				DPRINTK(PROBE, INFO, "core set to %d\n", core_param);

#ifdef module_param_array
		}

#endif

		adapter->core = core_param;
		BPRINTK(INFO, "PARAM: Adapter %u core = %d\n", adapter->bd_number, adapter->core);
	}

	{ /* Consumer assignment */
		static struct hpcap_option opt = {
			.type = range_option,
			.name = "Consumer threads per queue",
			.err  = "Defaulting to 1",
			.def  = 1,
			.arg  = {
				.r = {
					.min = 1,
					.max = (MAX_NUMNODES - 1)
				}
			}
		};

		int cons_param = opt.def;

		/* if the default was zero then we need to set the
		* default value to an online node, which is not
		* necessarily zero, and the constant initializer
		* above can't take first_online_node */
		if (cons_param == 0)
			/* must set opt.def for validate */
			opt.def = cons_param = 1;

#ifdef module_param_array

		if (num_Core > bd) {
#endif
			cons_param = Consumers[bd];
			hpcap_validate_option((uint *)&cons_param, &opt);

			if (cons_param != OPTION_UNSET)
				DPRINTK(PROBE, INFO, "core set to %d\n", cons_param);

#ifdef module_param_array
		}

#endif

		adapter->consumers = cons_param;
		BPRINTK(INFO, "PARAM: Adapter %u consumers = %zu\n", adapter->bd_number, adapter->consumers);
	}

	{ /* Mode assignment */
		static struct hpcap_option opt = {
			.type = range_option,
			.name = "RX mode",
			.err  = "defaulting to 1",
			.def  = HPCAP_DEFAULT_MODE,
			.arg  = {
				.r = {
					.min = 1,
					.max = 2
				}
			}
		};
		int mode_param = opt.def;

#ifdef module_param_array

		if (num_Mode > bd) {
#endif
			mode_param = Mode[bd];
			hpcap_validate_option((uint *)&mode_param, &opt);
#ifdef module_param_array
		}

#endif

		adapter->work_mode = mode_param;
		BPRINTK(INFO, "PARAM: Adapter %u mode = %d\n", adapter->bd_number, adapter->work_mode);
	}

	{ /* Dup assignment */
		static struct hpcap_option opt = {
			.type = range_option,
			.name = "Dup",
			.err  = "defaulting to 0",
			.def  = 0,
			.arg  = {
				.r = {
					.min = 0,
					.max = 1
				}
			}
		};
		int dup_param = opt.def;

#ifdef module_param_array

		if (num_Dup > bd) {
#endif
			dup_param = Dup[bd];
			hpcap_validate_option((uint *)&dup_param, &opt);
#ifdef module_param_array
		}

#endif
		adapter->dup_mode = dup_param;
		BPRINTK(INFO, "PARAM: Adapter %u dup = %d\n", adapter->bd_number, adapter->dup_mode);
	}

	{ /* Caplen assignment */
		static struct hpcap_option opt = {
			.type = range_option,
			.name = "Capture length",
			.err  = "defaulting to 0",
			.def  = 0,
			.arg  = {
				.r = {
					.min = 0,
					.max = MAX_PACKET_SIZE
				}
			}
		};
		int caplen_param = opt.def;

#ifdef module_param_array

		if (num_Caplen > bd) {
#endif
			caplen_param = Caplen[bd];
			hpcap_validate_option((uint *)&caplen_param, &opt);
#ifdef module_param_array
		}

#endif

		adapter->caplen = caplen_param;
		BPRINTK(INFO, "PARAM: Adapter %u Caplen = %zu\n", adapter->bd_number, adapter->caplen);
	}

	{ /* Pages assignment */
		static struct hpcap_option opt = {
			.type = range_option,
			.name = "Kernel buffer pages",
			.err  = "Invalid value",
			.def  = HPCAP_DEFAULT_PAGES_PER_ADAPTER,
			.arg  = {
				.r = {
					.min = 1,
					.max = HPCAP_BUF_SIZE / PAGE_SIZE
				}
			}
		};
		int pages_param = opt.def;

#ifdef module_param_array

		if (num_Pages > bd) {
#endif
			pages_param = Pages[bd];
			hpcap_validate_option((uint *)&pages_param, &opt);
#ifdef module_param_array
		}

#endif

		adapter->bufpages = pages_param;

		BPRINTK(INFO, "PARAM: Adapter %u bufpages = %zu\n", adapter->bd_number, adapter->bufpages);
	}

	return 0;
}
