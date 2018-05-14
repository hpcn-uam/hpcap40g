/**
* @brief Functions to allow hpcap configuration to be changed while the driver is running
*
*/
#include "hpcap_types.h"
// #include "hpcap.h"
//#include "ixgbe.h"

#ifdef HPCAP_SYSFS

#include <linux/module.h>
#include <linux/types.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/atomic.h>

#include "hpcap_sysfs.h"
#include "hpcap_debug.h"
#include "hpcap_dups.h"


static ssize_t show_hot_dups(struct device *dev, struct device_attribute *attr,
							 char *buf)
{
	struct hpcap_attr* hpcap_attr = container_of(attr, struct hpcap_attr, dev_attr);

	int dup_mode = atomic_read(hpcap_attr->value);

	// BPRINTK(WARNING, "CALLED SHOW");
	return sprintf(buf, "%d", dup_mode);
}

static ssize_t store_hot_dups(struct device *dev, struct device_attribute *attr,
							  const char *buf, size_t count)
{
	int dup_flag, dup_mode;
	struct hpcap_attr* hpcap_attr;

	kstrtoint(buf, 10, &dup_flag);

	hpcap_attr = container_of(attr, struct hpcap_attr, dev_attr);

	// If the previous duplicade mode is the same as the flag, no change is needed
	dup_mode = atomic_read(hpcap_attr->value);

	if ((dup_mode && dup_flag) || (!dup_mode && !dup_flag))
		return count;


	// Assign the new value to the variable dup_mode
	atomic_set(hpcap_attr->value, dup_flag);

	if (dup_flag)
		BPRINTK(WARNING, "Duplicate removal enabled\n");
	else
		BPRINTK(WARNING, "Duplicate removal disabled\n");


	return count;
}

static DEVICE_ATTR(hot_dups, 0660, show_hot_dups, store_hot_dups); // creates 'struct device_attribute dev_attr_hot_dups'


static ssize_t show_hot_caplen(struct device *dev, struct device_attribute *attr,
							   char *buf)
{
	int caplen;
	struct hpcap_attr* hpcap_attr = container_of(attr, struct hpcap_attr, dev_attr);

	caplen = atomic_read(hpcap_attr->value);

	// BPRINTK(WARNING, "CALLED SHOW");
	return sprintf(buf, "%d", caplen);
}

static ssize_t store_hot_caplen(struct device *dev, struct device_attribute *attr,
								const char *buf, size_t count)
{
	int caplen;
	struct hpcap_attr* hpcap_attr;

	kstrtoint(buf, 10, &caplen);

	if (caplen < 0)
		return count;

	hpcap_attr = container_of(attr, struct hpcap_attr, dev_attr);

	// Assign the new value to the variable
	atomic_set(hpcap_attr->value, caplen);

	BPRINTK(WARNING, "Caplen set to %d\n", caplen);

	return count;
}

static DEVICE_ATTR(hot_caplen, 0660, show_hot_caplen, store_hot_caplen); // creates 'struct device_attribute dev_attr_hot_dups'

/*
	static DEVICE_ATTR(name, S_IRUGO, show_name, store_name) creates a device attribute
	with the name 'dev_attr_name', permissions in the second field and with the provided
	show/store functions

	Examples:

static DEVICE_ATTR(type, 0444, show_type, NULL); 			// creates 'struct device_attribute dev_attr_type'
static DEVICE_ATTR(power, 0644, show_power, store_power);	// creates 'struct device_attribute dev_attr_power'

// Now we group them
static struct attribute *dev_attrs[] = {
	&dev_attr_type.attr,
	&dev_attr_power.attr,
	NULL, // Last element must be a NULL
};

*/

int hpcap_sysfs_init(HW_ADAPTER* adapter)
{
	int rc;
	struct hpcap_attr* hpcap_attr;

	adapter->hpcap_dev_attrs.hpcap_attr_list = kcalloc(MAX_DEVICES, sizeof(struct hpcap_attr),
			GFP_KERNEL);

	//****************** HOT_DUPS **********************
	hpcap_attr = &adapter->hpcap_dev_attrs.hpcap_attr_list[HOT_DUPS];
	// Establish the link to the variable with duplicate mode of the adapter
	hpcap_attr->value = &adapter->dup_mode;
	// Copy the previously created attribute to this structure
	memcpy(&hpcap_attr->dev_attr, &dev_attr_hot_dups, sizeof(struct device_attribute));

	rc = device_create_file(pci_dev_to_dev(adapter->pdev), &hpcap_attr->dev_attr);

	if (rc)
		BPRINTK(WARNING, "Error creating hot_dups file");

	//****************** HOT_CAPLEN **********************
	hpcap_attr = &adapter->hpcap_dev_attrs.hpcap_attr_list[HOT_CAPLEN];
	hpcap_attr->value = &adapter->caplen;
	memcpy(&hpcap_attr->dev_attr, &dev_attr_hot_caplen, sizeof(struct device_attribute));

	rc = device_create_file(pci_dev_to_dev(adapter->pdev), &hpcap_attr->dev_attr);

	if (rc)
		BPRINTK(WARNING, "Error creating hot_caplen file");

	return rc;

}

void hpcap_sysfs_exit(HW_ADAPTER* adapter)
{
	struct hpcap_dev_attrs* dev_attrs = &adapter->hpcap_dev_attrs;

	BPRINTK(WARNING, "Removing file %p %s\n", dev_attrs->hpcap_attr_list[HOT_DUPS].dev_attr.attr.name, dev_attrs->hpcap_attr_list[HOT_DUPS].dev_attr.attr.name);
	device_remove_file(pci_dev_to_dev(adapter->pdev), &dev_attrs->hpcap_attr_list[HOT_DUPS].dev_attr);
	BPRINTK(WARNING, "Removed");

	BPRINTK(WARNING, "Removing file %p %s\n", dev_attrs->hpcap_attr_list[HOT_CAPLEN].dev_attr.attr.name, dev_attrs->hpcap_attr_list[HOT_CAPLEN].dev_attr.attr.name);
	device_remove_file(pci_dev_to_dev(adapter->pdev), &dev_attrs->hpcap_attr_list[HOT_CAPLEN].dev_attr);
	BPRINTK(WARNING, "Removed");

	kfree(adapter->hpcap_dev_attrs.hpcap_attr_list);
}

#endif /* HPCAP_SYSFS */
