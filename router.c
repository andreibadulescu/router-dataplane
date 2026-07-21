#include "protocols.h"
#include "queue.h"
#include "lib.h"
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdbool.h>
#include <arpa/inet.h>

#define RTABLE_LEN 1e5
#define RTABLE_LINE_LEN 100
#define ARPTABLE_LEN 1000

struct saved_packet {
	int interface;
	size_t length;
	uint32_t next_hop;
	char buffer[MAX_PACKET_LEN];
};

struct lpm_result {
	int interface;
	uint32_t next_hop;
};

struct radix_tree_node {
	uint32_t sequence; // compressed bits
	uint8_t seq_length; // the number of compressed bits

	bool is_valid_entry; // does this node represent an actual route?
	struct lpm_result entry; // interface and next-hop for this route

	struct radix_tree_node *lhs; // left descendent
	struct radix_tree_node *rhs; // right descendent
};

struct radix_tree_node* create_radix_tree_node(uint32_t seq, uint8_t seq_len, bool is_valid, struct lpm_result entry) {
	struct radix_tree_node* node = (struct radix_tree_node*)malloc(sizeof(struct radix_tree_node));
	node->sequence = seq;
	node->seq_length = seq_len;
	node->is_valid_entry=is_valid;
	if (is_valid)
		node->entry = entry;

	node->lhs = NULL;
	node->rhs = NULL;
	return node;
}

uint8_t prefix_length(uint32_t l3_addr1, uint32_t l3_addr2, uint8_t len) {
	uint8_t res = 0;
	l3_addr1 = l3_addr1 << (32 - len);
	l3_addr2 = l3_addr2 << (32 - len);
	l3_addr1 = l3_addr1 ^ l3_addr2;
	if (l3_addr1 == 0)
		return len;

	uint32_t bit = 1U << 31;
	while ((bit & l3_addr1) == 0) {
		res++;
		bit = bit >> 1;
	}

	return (len > res) ? res : len;
}

uint8_t extract_bit(uint32_t l3_addr, uint8_t pos) {
	return (l3_addr >> (31 - pos)) & 1;
}

uint32_t extract_bitseq(uint32_t l3_addr, uint8_t len, uint8_t offset) {
	if (len == 0)
		return 0;

	if (len == 32)
		return l3_addr;

	uint32_t result = l3_addr >> (32 - offset - len);
	uint32_t mask = (1ULL << len) - 1; // to enable only the required bits, removing the ones on the 'left'
	return result & mask;
}

struct lpm_result* search_radix_tree(struct radix_tree_node *root, uint32_t dest_ip) {
	struct radix_tree_node *current = root;
	struct lpm_result *result = NULL;
	uint8_t depth = 0;

	while (current != NULL && depth < 32) {
		uint32_t bitseq = extract_bitseq(dest_ip, current->seq_length, depth);

		if (bitseq != current->sequence) {
			break; // not matching anymore
		}

		depth += current->seq_length;
		if (current->is_valid_entry) {
			result = &current->entry;
		}

		if (depth < 32) {
			uint8_t next_bit = extract_bit(dest_ip, depth++);
			if (next_bit == 0) {
				current = current->lhs;
			} else {
				current = current->rhs;
			}
		} else {
			break;
		}
	}

	return result;
}

void insert_node(struct radix_tree_node **root, uint32_t dest_ip, uint8_t prefix_len, struct lpm_result entry, uint8_t depth) {
	if (*root == NULL) {
		uint8_t remaining_len = prefix_len - depth;
		uint32_t bitseq = extract_bitseq(dest_ip, remaining_len, depth);
		*root = create_radix_tree_node(bitseq, remaining_len, true, entry);
		return;
	}

	struct radix_tree_node *current = *root;
	uint8_t compare_len = (prefix_len - depth < current->seq_length) ? (prefix_len - depth) : current->seq_length;
	uint32_t bitseq = extract_bitseq(dest_ip, compare_len, depth);
	uint8_t match_len = prefix_length(bitseq, current->sequence >> (current->seq_length - compare_len), compare_len);

	if (match_len == current->seq_length) {
		depth += match_len;

		if (depth == prefix_len) {
			current->is_valid_entry = true;
			current->entry = entry;
		} else {
			uint8_t bit = extract_bit(dest_ip, depth++);
			if (bit == 0) {
				insert_node(&current->lhs, dest_ip, prefix_len, entry, depth);
			} else {
				insert_node(&current->rhs, dest_ip, prefix_len, entry, depth);
			}
		}

		return;
	}


	uint32_t common_seq = current->sequence >> (current->seq_length - match_len);
	struct radix_tree_node *ancestor = create_radix_tree_node(common_seq, match_len, false, entry);

	uint8_t branching_bit = (current->sequence >> (current->seq_length - match_len - 1)) & 1;
	current->sequence &= (1ULL << (current->seq_length - match_len - 1)) - 1;
	current->seq_length = current->seq_length - match_len - 1;

	if (branching_bit == 0) {
		ancestor->lhs = current;
	} else {
		ancestor->rhs = current;
	}

	depth += match_len;

	if (depth == prefix_len) {
		ancestor->is_valid_entry = true;
		ancestor->entry = entry;
	} else {
		branching_bit = extract_bit(dest_ip, depth);
		uint8_t remaining_len = prefix_len - depth - 1;
		uint32_t remaining_bitseq = extract_bitseq(dest_ip, remaining_len, depth + 1);

		struct radix_tree_node *node = create_radix_tree_node(remaining_bitseq, remaining_len, true, entry);

		if (branching_bit == 0) {
			ancestor->lhs = node;
		} else {
			ancestor->rhs = node;
		}
	}

	*root = ancestor;
}

struct lpm_result lpm(uint32_t ip_dest, struct route_table_entry *rtable, unsigned int rtable_size) {
	unsigned int best_entry = 0;
	uint32_t best_mask = 0;

	for (unsigned int i = 0; i < rtable_size; i++)
		if ((ip_dest & rtable[i].mask) == rtable[i].prefix)
			if (best_mask < rtable[i].mask) {
				best_entry = i;
				best_mask = rtable[i].mask;
			}

	struct lpm_result res;
	memset(&res, 0, sizeof(struct lpm_result));

	if ((best_entry == 0) && (best_mask == 0))
		return res;

	res.interface = rtable[best_entry].interface;
	res.next_hop = rtable[best_entry].next_hop;
	return res;
}

int main(int argc, char *argv[])
{
	char buf[MAX_PACKET_LEN];

	// Do not modify this line
	init(argv + 2, argc - 2);

	// Creating routing table...
	struct radix_tree_node *root = NULL;
	/*struct route_table_entry rtable[RTABLE_LEN];
	memset(rtable, 0, sizeof(rtable));

	unsigned int rtable_size = 0;*/

	FILE* rtable_fd = fopen(argv[1], "r");
	DIE(rtable_fd == NULL, "rtable_open");

	char line[RTABLE_LINE_LEN];

	// Parsing routing table...
	while (fgets(line, RTABLE_LINE_LEN, rtable_fd)) {
		uint32_t p1, p2, p3, p4;
		uint32_t h1, h2, h3, h4;
		uint32_t m1, m2, m3, m4;
		int route_if;

		sscanf(line, "%u.%u.%u.%u %u.%u.%u.%u %u.%u.%u.%u %d",\
			&p4, &p3, &p2, &p1, &h4, &h3, &h2, &h1, &m4, &m3, &m2, &m1, &route_if);

		p4 = p4 << 24;
		p3 = p3 << 16;
		p2 = p2 << 8;
		p4 |= p3;
		p4 |= p2;
		p4 |= p1;

		h2 = h2 << 8;
		h3 = h3 << 16;
		h4 = h4 << 24;
		h4 |= h3;
		h4 |= h2;
		h4 |= h1;

		m2 = m2 << 8;
		m3 = m3 << 16;
		m4 = m4 << 24;
		m4 |= m3;
		m4 |= m2;
		m4 |= m1;

		/*
		rtable[rtable_size].prefix = p4;
		rtable[rtable_size].next_hop = h4;
		rtable[rtable_size].mask = m4;
		rtable[rtable_size++].interface = route_if;*/
		struct lpm_result temp;
		temp.next_hop = htonl(h4);
		temp.interface = route_if;

		uint8_t res = 0;
		uint32_t bit = 1 << 31;
		while ((bit & m4) != 0) {
			res++;
			bit = bit >> 1;
		}

		insert_node(&root, p4, res, temp, 0);
	}

	printf("DEBUG: Read routing table successfully!\n");

	fclose(rtable_fd);

	// Creating ARP table...
	struct arp_table_entry arptable[ARPTABLE_LEN];
	memset(arptable, 0, sizeof(arptable));

	unsigned int arptable_size = 0;

	// Creating packet queue...
	queue packetqueue = create_queue();

	// Routing packages...
	while (1) {
		size_t interface;
		size_t len;

		interface = recv_from_any_link(buf, &len);
		DIE(interface < 0, "recv_from_any_links");

		/*for (int i = 0; i < arptable_size; i++) {
			printf("%u %hhx %hhx %hhx %hhx %hhx %hhx\n", arptable[i].ip, arptable[i].mac[0], arptable[i].mac[1], arptable[i].mac[2], arptable[i].mac[3], arptable[i].mac[4], arptable[i].mac[5]);
		}*/

		// Checking Ethernet header...
		if (len < sizeof(struct ether_hdr)) {
			continue; // packet is INVALID, throwing...
		}

		uint8_t mac[6];
		uint8_t broadcast_mac[6];

		for (int i = 0; i < 6; i++) {
			broadcast_mac[i] = 255;
		}

		get_interface_mac(interface, mac);
		struct ether_hdr *l2_hdr = (struct ether_hdr *)buf;
		unsigned char packet_type = 0;

		// Checking Ether Type field...
		if (ntohs(l2_hdr->ethr_type) == (uint16_t) 2048) {
			packet_type = 1; // IPv4
		} else if (ntohs(l2_hdr->ethr_type) == (uint16_t) 2054) {
			packet_type = 2; // ARP
		} else {
			fprintf(stderr, "eth_type: %hu invalid\n", ntohs(l2_hdr->ethr_type));
			continue; // packet is INVALID, throwing...
		}

		// Checking IPv4 header (if applicable)
		if (packet_type == 1) {
			if (len < sizeof(struct ether_hdr) + sizeof(struct ip_hdr)) {
				continue; // packet is INVALID, throwing...
			}

			struct ip_hdr *l3_hdr = (struct ip_hdr *)(buf + sizeof(struct ether_hdr));
			struct icmp_hdr *diag_hdr = (struct icmp_hdr *)(buf + sizeof(struct ether_hdr) + sizeof(struct ip_hdr));

			// Verfying checksum...
			if (checksum((uint16_t *)l3_hdr, sizeof(struct ip_hdr))) {
				printf("Checksum invalid! Throwing...\n");
				continue;
			}

			uint32_t ip;
			inet_pton(AF_INET, get_interface_ip(interface), &ip);

			// Checking TTL...
			if (l3_hdr->ttl < 2) {

				// Packet expired, sending Time Exceeded message
				// Copying old ICMP header...
				//memcpy((void *)(diag_hdr + 1), (void *)diag_hdr, sizeof(struct icmp_hdr));

				// Rewriting ICMP header...
				diag_hdr->check = 0;
				diag_hdr->mtype = (uint8_t)11;
				diag_hdr->mcode = (uint8_t)0;
				diag_hdr->check = htons(checksum((uint16_t *)diag_hdr, sizeof(struct icmp_hdr)));
				diag_hdr->un_t.echo_t.id = 0;
				diag_hdr->un_t.echo_t.seq = 0;

				// Rewriting Layer3 header...
				l3_hdr->ttl = 64;
				l3_hdr->tos = 0;
				l3_hdr->tot_len = htons(sizeof(struct icmp_hdr) + sizeof(struct ip_hdr));
				l3_hdr->id = htons(4);
				l3_hdr->frag = htons(0);
				l3_hdr->checksum = 0;
				l3_hdr->dest_addr = l3_hdr->source_addr;
				l3_hdr->source_addr = ip;
				l3_hdr->proto = 1; // ICMP
				l3_hdr->checksum = htons(checksum((uint16_t *)l3_hdr, sizeof(struct ip_hdr)));

				// Rewriting Layer2 header...
				memcpy(l2_hdr->ethr_dhost, l2_hdr->ethr_shost, 6);
				memcpy(l2_hdr->ethr_shost, mac, 6);

				// Recalculating packet length...
				len = sizeof(struct icmp_hdr) + sizeof(struct ip_hdr) + sizeof(struct ether_hdr);
				send_to_link(len, buf, interface);

				// Done! Sent Time Exceeded back to sender
				continue;

			} else {
				l3_hdr->ttl--;
			}

			// Checking if packet is for router...
			if (l3_hdr->dest_addr == ip) {
				// Responding with Echo Reply...
				diag_hdr->check = 0;
				diag_hdr->mtype = (uint8_t)0;
				diag_hdr->mcode = (uint8_t)0;
				diag_hdr->check = htons(checksum((uint16_t *)diag_hdr, sizeof(struct icmp_hdr)));

				// Rewriting Layer3 header...
				l3_hdr->ttl = 64;
				l3_hdr->tos = 0;
				l3_hdr->id = htons(4);
				l3_hdr->frag = htons(0);
				l3_hdr->checksum = 0;
				l3_hdr->dest_addr = l3_hdr->source_addr;
				l3_hdr->source_addr = ip;
				l3_hdr->checksum = htons(checksum((uint16_t *)l3_hdr, sizeof(struct ip_hdr)));

				// Rewriting Layer2 header...
				memcpy(l2_hdr->ethr_dhost, l2_hdr->ethr_shost, 6);
				memcpy(l2_hdr->ethr_shost, mac, 6);

				// Recalculating packet length...
				send_to_link(len, buf, interface);

				// Done! Sent Echo Reply to sender
				continue;
			}

			// Packages directed towards the router have been treated.
			// Searching destination in the routing table...
			struct lpm_result *lpm_res = search_radix_tree(root, ntohl(l3_hdr->dest_addr)); //lpm(l3_hdr->dest_addr, rtable, rtable_size);

			// No route found...
			if (lpm_res == NULL) {
				// Packet expired, sending Destination Unreachable message
				// Copying old ICMP header...
				//memcpy((void *)(diag_hdr + 1), (void *)diag_hdr, sizeof(struct icmp_hdr));

				// Rewriting ICMP header...
				diag_hdr->check = (uint16_t)0;
				diag_hdr->mtype = (uint8_t)3;
				diag_hdr->mcode = (uint8_t)0;
				diag_hdr->check = htons(checksum((uint16_t *)diag_hdr, sizeof(struct icmp_hdr)));
				diag_hdr->un_t.echo_t.id = 0;
				diag_hdr->un_t.echo_t.seq = 0;

				// Rewriting Layer3 header...
				l3_hdr->ttl = 64;
				l3_hdr->tos = 0;
				l3_hdr->tot_len = htons(sizeof(struct icmp_hdr) + sizeof(struct ip_hdr));
				l3_hdr->id = htons(4);
				l3_hdr->frag = htons(0);
				l3_hdr->checksum = 0;
				l3_hdr->dest_addr = l3_hdr->source_addr;
				l3_hdr->source_addr = ip;
				l3_hdr->proto = 1;
				l3_hdr->checksum = htons(checksum((uint16_t *)l3_hdr, sizeof(struct ip_hdr)));

				// Rewriting Layer2 header...
				memcpy(l2_hdr->ethr_dhost, l2_hdr->ethr_shost, 6);
				memcpy(l2_hdr->ethr_shost, mac, 6);

				// Recalculating packet length...
				len = sizeof(struct icmp_hdr) + sizeof(struct ip_hdr) + sizeof(struct ether_hdr);
				send_to_link(len, buf, interface);

				// Done! Sent Destination Unreachable back to sender
				continue;
			}

			// Refreshing checksum...
			int send_if = lpm_res->interface;
			l3_hdr->checksum = 0;
			l3_hdr->checksum = htons(checksum((uint16_t *)l3_hdr, sizeof(struct ip_hdr)));

			// Rewriting Ethernet header...
			memcpy(l2_hdr->ethr_shost, mac, 6);

			unsigned char dhost_found = 0;
			for (unsigned int i = 0; i < arptable_size; i++) {
				if (lpm_res->next_hop == arptable[i].ip) {
					memcpy(l2_hdr->ethr_dhost, arptable[i].mac, 6);
					dhost_found = 1;
					break;
				}
			}

			// Getting next-hop HW address...
			get_interface_mac(send_if, mac);
			inet_pton(AF_INET, get_interface_ip(send_if), &ip);

			// Sending ARP request (if applicable)
			// If ARP request is sent, sending will be interrupted
			// Sending will resume after ARP reply is received
			// Packet is stored in a waiting queue
			if (dhost_found == 0) {
				// Stashing initial packet...
				struct saved_packet *ptr = (struct saved_packet *)malloc(sizeof(struct saved_packet));
				ptr->interface = send_if;
				ptr->length = len;
				ptr->next_hop = lpm_res->next_hop;
				memcpy(ptr->buffer, buf, len);
				queue_enq(packetqueue, (void *)ptr);

				// Creating ARP request...
				char req[MAX_PACKET_LEN];
				memset(req, 0, sizeof(req));

				// Setting up Layer2 header...
				struct ether_hdr *l2_req = (struct ether_hdr *)req;
				memcpy(l2_req->ethr_dhost, broadcast_mac, 6);
				memcpy(l2_req->ethr_shost, mac, 6);
				l2_req->ethr_type = htons((uint16_t) 0x806);

				// Setting up Layer3 header...
				struct arp_hdr *l3_req = (struct arp_hdr *)(req + sizeof(struct ether_hdr));
				l3_req->hw_type = htons((uint16_t)1);
				l3_req->proto_type = htons((uint16_t)0x800);
				l3_req->hw_len = (uint8_t)6;
				l3_req->proto_len = (uint8_t)4;
				l3_req->opcode = htons((uint16_t)1);
				memcpy(l3_req->shwa, mac, 6);
				l3_req->sprotoa = ip;
				memcpy(l3_req->thwa, broadcast_mac, 6); // might be OPTIONAL
				l3_req->tprotoa =lpm_res->next_hop;

				// Sending request...
				size_t req_len = sizeof(struct ether_hdr) + sizeof(struct arp_hdr);
				send_to_link(req_len, req, send_if);

				// Done!
				continue;
			}

			// Sending packet...
			send_to_link(len, buf, send_if);

			// Done! Processing next packet...

		}

		// Checking ARP header (if applicable)
		if (packet_type == 2) {
			if (len < sizeof(struct ether_hdr) + sizeof(struct ip_hdr)) {
				continue; // packet is INVALID, throwing...
			}

			struct arp_hdr *l3_hdr = (struct arp_hdr *)(buf + sizeof(struct ether_hdr));

			if (ntohs(l3_hdr->opcode) == (uint16_t) 1) { // ARP Request

				// Checking if request is meant for us...
				uint32_t ip;
				inet_pton(AF_INET, get_interface_ip(interface), &ip);
				if (ip != l3_hdr->tprotoa) {
					continue; // request is not for us, throwing...
				}

				// Replying via ARP
				// Rewriting Layer2 header...
				memcpy(l2_hdr->ethr_dhost, l2_hdr->ethr_shost, 6);
				memcpy(l2_hdr->ethr_shost, mac, 6);

				// Rewriting Layer3 header...
				memcpy(l3_hdr->thwa, l3_hdr->shwa, 6);
				uint32_t if_ip = l3_hdr->tprotoa;
				l3_hdr->tprotoa = l3_hdr->sprotoa;
				memcpy(l3_hdr->shwa, mac, 6);
				l3_hdr->sprotoa = if_ip;
				l3_hdr->opcode = htons((uint16_t)2);

				// Send ARP Reply
				send_to_link(len, buf, interface);

				// Done!

			} else if (ntohs(l3_hdr->opcode) == (uint16_t) 2) { // ARP Reply

				if (ntohs(l3_hdr->opcode) != (uint16_t)2) {
					continue; // packet is INVALID, throwing...
				}

				// Updating ARP table...
				unsigned char check = 0;

				for (unsigned int i = 0; i < arptable_size; i++) {
					if (arptable[i].ip == l3_hdr->sprotoa) {
						memcpy(arptable[i].mac, l3_hdr->shwa, 6);
						check = 1;
						break;
					}
				}

				// Inserting new ARP table entry...
				if (check == 0) {
					arptable[arptable_size].ip = l3_hdr->sprotoa;
					memcpy(arptable[arptable_size++].mac, l3_hdr->shwa, 6);
				}

				// Done!

			}
		}

		// Checking if any packet waiting in queue can be sent...
		unsigned int remaining = queue_size(packetqueue);
		while (remaining > 0) {
			remaining--;

			struct saved_packet *ptr = (struct saved_packet *)queue_deq(packetqueue);
			unsigned char dhost_found = 0;

			struct ether_hdr *ethernet_header = (struct ether_hdr *)(ptr->buffer);

			for (unsigned int i = 0; i < arptable_size; i++) {
				if (ptr->next_hop == arptable[i].ip) {
					memcpy(ethernet_header->ethr_dhost, arptable[i].mac, 6);
					dhost_found = 1;
					break;
				}
			}

			if (!dhost_found) {
				queue_enq(packetqueue, (void *) ptr);
			} else {
				send_to_link(ptr->length, ptr->buffer, ptr->interface);
				free(ptr);
			}
		}
	}
}

