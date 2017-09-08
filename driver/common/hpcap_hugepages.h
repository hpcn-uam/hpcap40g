/**
 * @author Guillermo Julián (guillermo.julian@estudiante.uam.es)
 * @brief Functions to manage and include the hugepages
 *        in the HPCAP buffers.
 *
 * @addtogroup HPCAP
 * @{
 */

#ifndef HPCAP_HUGEPAGES_H
#define HPCAP_HUGEPAGES_H

#include "hpcap.h"
#include "driver_hpcap.h"

#define has_hugepages(bufp) ((bufp)->huge_pages != NULL)

/**
 * Configures the driver to use hugepages allocated in userspace.
 * @param buf  HPCAP descriptor.
 * @param huge Structure holding the hugepage buffer information.
 * @return      0 if OK, negative if error.
 */
int hpcap_huge_use(struct hpcap_buf* bufp, struct hpcap_buffer_info* huge);

/**
* Frees the allocated hugepages.
*
* @notes This function assumes an existing lock over hpcap_buf struct to avoid
*        multiple concurrent modifications. If the lock doesn't exist, this function
*        ** is not secure for concurrency**.
*
* @param buf HPCAP descriptor.
* @return     0 if OK, or -EIDRM if there are no hugepages.
*/
int hpcap_huge_release(struct hpcap_buf* buf);


/**
 * Get the information (buffer size and file name) for the hugepages in this descriptor.
 *
 * If there are no hugepages, huge->size will be 0.
 * @param buf  HPCAP descriptor.
 * @param huge Hugepage information structure.
 */
void hpcap_huge_info(struct hpcap_buf* buf, struct hpcap_buffer_info* huge);

/** @} */

#endif
