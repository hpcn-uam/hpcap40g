/**
 * @brief Header file containing the types for sysfs compatibility.
 * This avoids circular references and includes.
 *
 * @addtogroup HPCAP
 * @{
 */


#ifndef HPCAP_SYSFS_TYPES_H
#define HPCAP_SYSFS_TYPES_H

#ifdef HPCAP_SYSFS

/**
 * @internal
 * Structures to change hpcap configuration while its running using the sysfs
 * file system
 */
struct hpcap_attr {
	struct device_attribute dev_attr; /**< Actual attribute of the device */
	atomic_t* value; /**< Pointer to the adapter, so changes can be made */
};

struct hpcap_dev_attrs {
	//struct device devs[MAX_DEVICES]; /**< Array of devices for the sysfs file system */
	struct hpcap_attr* hpcap_attr_list; /**< Array of attributes of the corresponding devices */
};

/** @} */

#endif /* HPCAP_SYSFS */
#endif /* HPCAP_SYSFS_TYPES_H */
