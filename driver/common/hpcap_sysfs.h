/**
 * @brief Code for creating and erasing the sysfs directories for the devices
 *
 *
 * @addtogroup HPCAP
 * @{
 */

#ifndef HPCAP_SYSFS_H
#define HPCAP_SYSFS_H

#ifdef HPCAP_SYSFS

#include "hpcap_types.h"

/**
 * Initializes the sysfs attributes
 * @param  adapter Adapter with devices to associate the attributes to
 * @return   0 if initialization was successful
 */
int hpcap_sysfs_init(HW_ADAPTER* adapter);

/**
 * Deletes the sysfs attributes
 * @param  adapter Adapter with devices to delete the attributes from
 */
void hpcap_sysfs_exit(HW_ADAPTER* adapter);

/** @} */

#endif
#endif
