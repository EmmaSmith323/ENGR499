#include "../lib/scan.h"

int bt_scan(inquiry_info **ii, int max_rsp, int *num_rsp, int *dev_id,
	int *sock) {
	// calculate timeout time
	int len = (int) ((double) max_rsp / 255.0 * 8.0);

	// call helper function
	return bt_scan_helper(ii, max_rsp, num_rsp, dev_id, sock, len, IREQ_CACHE_FLUSH);
}

int bt_scan_helper(inquiry_info **ii, int max_rsp, int *num_rsp, int *dev_id,
	int *sock, int len, int flags)
{
	*dev_id = hci_get_route(NULL);

	*sock = hci_open_dev(*dev_id);

	if (*dev_id < 0 || *sock < 0) {
		perror("opening socket");
		//exit(1);
		return 1;
	}

	printf("Searching for bluetooth device...\n");
	*ii = (inquiry_info*) malloc(max_rsp * sizeof(inquiry_info));
	*num_rsp = hci_inquiry(*dev_id, len, max_rsp, NULL, ii, flags);

	if (*num_rsp < 0) {
		printf("Failed!\n");
		perror("hci_inquiry");
	}
	else {
		printf("Search Complete!\n");
	}

	return 0;
}