#include "./wf_conn.h"
#include <stdlib.h>
#include <string.h>
#include "./wf_log.h"

Packet *packetCopy(const Packet *to_copy) {
	Packet *packet_copy = malloc(sizeof(Packet));
	handle_error( packet_copy == NULL, { return NULL; }, "Error while copying packet (malloc)" );
	
	memcpy(packet_copy, to_copy, sizeof(Packet));
	packet_copy->buf = malloc(to_copy->len);
	handle_error( packet_copy->buf == NULL, { return NULL; }, "Error while copying packet payload (malloc)" );
	memcpy(packet_copy->buf, to_copy->buf, to_copy->len);

	return packet_copy;
}

void packetDestroy(Packet *to_destroy) {
	free(to_destroy->buf);
	free(to_destroy);
}