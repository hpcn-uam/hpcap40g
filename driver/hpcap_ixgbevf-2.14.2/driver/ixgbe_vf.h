/*******************************************************************************

  Intel 82599 Virtual Function driver
  Copyright (c) 1999 - 2014 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#ifndef __IXGBE_VF_H__
#define __IXGBE_VF_H__

#define IXGBE_VF_IRQ_CLEAR_MASK	7
#define IXGBE_VF_MAX_TX_QUEUES	8
#define IXGBE_VF_MAX_RX_QUEUES	8

/* DCB define */
#define IXGBE_VF_MAX_TRAFFIC_CLASS	8

#define IXGBE_VFCTRL		0x00000
#define IXGBE_VFSTATUS		0x00008
#define IXGBE_VFLINKS		0x00010
#define IXGBE_VFFRTIMER		0x00048
#define IXGBE_VFRXMEMWRAP	0x03190
#define IXGBE_VTEICR		0x00100
#define IXGBE_VTEICS		0x00104
#define IXGBE_VTEIMS		0x00108
#define IXGBE_VTEIMC		0x0010C
#define IXGBE_VTEIAC		0x00110
#define IXGBE_VTEIAM		0x00114
#define IXGBE_VTEITR(x)		(0x00820 + (4 * (x)))
#define IXGBE_VTIVAR(x)		(0x00120 + (4 * (x)))
#define IXGBE_VTIVAR_MISC	0x00140
#define IXGBE_VTRSCINT(x)	(0x00180 + (4 * (x)))
/* define IXGBE_VFPBACL  still says TBD in EAS */
#define IXGBE_VFRDBAL(x)	(0x01000 + (0x40 * (x)))
#define IXGBE_VFRDBAH(x)	(0x01004 + (0x40 * (x)))
#define IXGBE_VFRDLEN(x)	(0x01008 + (0x40 * (x)))
#define IXGBE_VFRDH(x)		(0x01010 + (0x40 * (x)))
#define IXGBE_VFRDT(x)		(0x01018 + (0x40 * (x)))
#define IXGBE_VFRXDCTL(x)	(0x01028 + (0x40 * (x)))
#define IXGBE_VFSRRCTL(x)	(0x01014 + (0x40 * (x)))
#define IXGBE_VFRSCCTL(x)	(0x0102C + (0x40 * (x)))
#define IXGBE_VFPSRTYPE		0x00300
#define IXGBE_VFTDBAL(x)	(0x02000 + (0x40 * (x)))
#define IXGBE_VFTDBAH(x)	(0x02004 + (0x40 * (x)))
#define IXGBE_VFTDLEN(x)	(0x02008 + (0x40 * (x)))
#define IXGBE_VFTDH(x)		(0x02010 + (0x40 * (x)))
#define IXGBE_VFTDT(x)		(0x02018 + (0x40 * (x)))
#define IXGBE_VFTXDCTL(x)	(0x02028 + (0x40 * (x)))
#define IXGBE_VFTDWBAL(x)	(0x02038 + (0x40 * (x)))
#define IXGBE_VFTDWBAH(x)	(0x0203C + (0x40 * (x)))
#define IXGBE_VFDCA_RXCTRL(x)	(0x0100C + (0x40 * (x)))
#define IXGBE_VFDCA_TXCTRL(x)	(0x0200c + (0x40 * (x)))
#define IXGBE_VFGPRC		0x0101C
#define IXGBE_VFGPTC		0x0201C
#define IXGBE_VFGORC_LSB	0x01020
#define IXGBE_VFGORC_MSB	0x01024
#define IXGBE_VFGOTC_LSB	0x02020
#define IXGBE_VFGOTC_MSB	0x02024
#define IXGBE_VFMPRC		0x01034

struct ixgbe_hw;

#include "ixgbe_type.h"

#include "ixgbe_mbx.h"

struct ixgbe_mac_operations {
	s32(*init_hw)(struct ixgbe_hw *);
	s32(*reset_hw)(struct ixgbe_hw *);
	s32(*start_hw)(struct ixgbe_hw *);
	s32(*clear_hw_cntrs)(struct ixgbe_hw *);
	enum ixgbe_media_type(*get_media_type)(struct ixgbe_hw *);
	u32(*get_supported_physical_layer)(struct ixgbe_hw *);
	s32(*get_mac_addr)(struct ixgbe_hw *, u8 *);
	s32(*stop_adapter)(struct ixgbe_hw *);
	s32(*get_bus_info)(struct ixgbe_hw *);

	/* Link */
	s32(*setup_link)(struct ixgbe_hw *, ixgbe_link_speed, bool);
	s32(*check_link)(struct ixgbe_hw *, ixgbe_link_speed *, bool *, bool);
	s32(*get_link_capabilities)(struct ixgbe_hw *, ixgbe_link_speed *,
								bool *);

	/* RAR, Multicast, VLAN */
	s32(*set_rar)(struct ixgbe_hw *, u32, u8 *, u32, u32);
	s32(*set_uc_addr)(struct ixgbe_hw *, u32, u8 *);
	s32(*init_rx_addrs)(struct ixgbe_hw *);
	s32(*update_mc_addr_list)(struct ixgbe_hw *, u8 *, u32,
							  ixgbe_mc_addr_itr, bool);
	s32(*enable_mc)(struct ixgbe_hw *);
	s32(*disable_mc)(struct ixgbe_hw *);
	s32(*clear_vfta)(struct ixgbe_hw *);
	s32(*set_vfta)(struct ixgbe_hw *, u32, u32, bool);
};

struct ixgbe_mac_info {
	struct ixgbe_mac_operations ops;
	u8 addr[6];
	u8 perm_addr[6];

	enum ixgbe_mac_type type;

	s32  mc_filter_type;

	bool get_link_status;
	u32  max_tx_queues;
	u32  max_rx_queues;
	u32  max_msix_vectors;
};

struct ixgbe_mbx_operations {
	void (*init_params)(struct ixgbe_hw *hw);
	s32(*read)(struct ixgbe_hw *, u32 *, u16,  u16);
	s32(*write)(struct ixgbe_hw *, u32 *, u16, u16);
	s32(*read_posted)(struct ixgbe_hw *, u32 *, u16,  u16);
	s32(*write_posted)(struct ixgbe_hw *, u32 *, u16, u16);
	s32(*check_for_msg)(struct ixgbe_hw *, u16);
	s32(*check_for_ack)(struct ixgbe_hw *, u16);
	s32(*check_for_rst)(struct ixgbe_hw *, u16);
};

struct ixgbe_mbx_stats {
	u32 msgs_tx;
	u32 msgs_rx;

	u32 acks;
	u32 reqs;
	u32 rsts;
};

struct ixgbe_mbx_info {
	struct ixgbe_mbx_operations ops;
	struct ixgbe_mbx_stats stats;
	u32 timeout;
	u32 udelay;
	u32 v2p_mailbox;
	u16 size;
};

struct ixgbe_hw {
	void *back;

	u8 __iomem *hw_addr;

	struct ixgbe_mac_info mac;
	struct ixgbe_mbx_info mbx;

	u16 device_id;
	u16 subsystem_vendor_id;
	u16 subsystem_device_id;
	u16 vendor_id;

	u8  revision_id;
	bool adapter_stopped;

	int api_version;
};

struct ixgbevf_hw_stats {
	u64 base_vfgprc;
	u64 base_vfgptc;
	u64 base_vfgorc;
	u64 base_vfgotc;
	u64 base_vfmprc;

	u64 last_vfgprc;
	u64 last_vfgptc;
	u64 last_vfgorc;
	u64 last_vfgotc;
	u64 last_vfmprc;

	u64 vfgprc;
	u64 vfgptc;
	u64 vfgorc;
	u64 vfgotc;
	u64 vfmprc;

	u64 saved_reset_vfgprc;
	u64 saved_reset_vfgptc;
	u64 saved_reset_vfgorc;
	u64 saved_reset_vfgotc;
	u64 saved_reset_vfmprc;
};

s32 ixgbe_init_ops_vf(struct ixgbe_hw *hw);
s32 ixgbe_init_hw_vf(struct ixgbe_hw *hw);
s32 ixgbe_start_hw_vf(struct ixgbe_hw *hw);
s32 ixgbe_reset_hw_vf(struct ixgbe_hw *hw);
s32 ixgbe_stop_adapter_vf(struct ixgbe_hw *hw);
u32 ixgbe_get_num_of_tx_queues_vf(struct ixgbe_hw *hw);
u32 ixgbe_get_num_of_rx_queues_vf(struct ixgbe_hw *hw);
s32 ixgbe_get_mac_addr_vf(struct ixgbe_hw *hw, u8 *mac_addr);
s32 ixgbe_setup_mac_link_vf(struct ixgbe_hw *hw, ixgbe_link_speed speed,
							bool autoneg_wait_to_complete);
s32 ixgbe_check_mac_link_vf(struct ixgbe_hw *hw, ixgbe_link_speed *speed,
							bool *link_up, bool autoneg_wait_to_complete);
s32 ixgbe_set_rar_vf(struct ixgbe_hw *hw, u32 index, u8 *addr, u32 vmdq,
					 u32 enable_addr);
s32 ixgbevf_set_uc_addr_vf(struct ixgbe_hw *hw, u32 index, u8 *addr);
s32 ixgbe_update_mc_addr_list_vf(struct ixgbe_hw *hw, u8 *mc_addr_list,
								 u32 mc_addr_count, ixgbe_mc_addr_itr,
								 bool clear);
s32 ixgbe_set_vfta_vf(struct ixgbe_hw *hw, u32 vlan, u32 vind, bool vlan_on);
void ixgbevf_rlpml_set_vf(struct ixgbe_hw *hw, u16 max_size);
int ixgbevf_negotiate_api_version(struct ixgbe_hw *hw, int api);
int ixgbevf_get_queues(struct ixgbe_hw *hw, unsigned int *num_tcs,
					   unsigned int *default_tc);

#include "ixgbevf_osdep2.h"

#endif /* __IXGBE_VF_H__ */
