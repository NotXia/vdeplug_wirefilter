#include "./wf_conn.h"
#include <stdlib.h>
#include <string.h>

Packet *packetCopy(const Packet *to_copy) {
	Packet *packet_copy = malloc(sizeof(Packet));
	memcpy(packet_copy, to_copy, sizeof(Packet));
	memcpy(packet_copy->buf, to_copy->buf, to_copy->len);

	return packet_copy;
}