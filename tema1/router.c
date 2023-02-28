#include "include/queue.h"
#include "include/list.h"
#include <stdbool.h>
#include "include/skel.h"

#define MAX_TTL 62
#define ECHO_REPLY_TYPE 0
#define ECHO_REQUEST_TYPE 8
#define ICMP_TLE_TYPE 11
#define ICMP_DEST_UNREACHABLE 3
#define IP_ICMP_PROTOCOL 1
#define HLEN_MAC 6
#define PLEN_IP 4
#define ARP_REQUEST 1
#define ARP_REPLY 2

struct route_table_entry route_table[100005];
queue arp_entries;
queue messages_to_send;
int arp_len;
int rtable_len;

struct queue
{
	list head;
	list tail;
};

typedef struct 
{
	struct route_table_entry best;
	packet p;
} packet_with_best;

// o functie care compara entry-urile dupa prefix mai intai si apoi dupa
// length-ul prefixului (mask-ului) practic, pentru ca se poate 1010 cu
// netmask 1111 si 1010 cu netmask 111
int rtable_comp(const void *a, const void *b)
{
	struct route_table_entry * aa = (struct route_table_entry *)a;
	struct route_table_entry * bb = (struct route_table_entry *)b;
	if (aa->prefix > bb->prefix)
		return +1;
	if (aa->prefix < bb->prefix)
		return -1;
	if (aa->mask > bb->mask)
		return +1;
	if (aa->mask < bb->mask)
		return -1;
	return 0;
}

// cautare binara Jul-style. Am preferat sa mentin high-ul ca diferenta intre
// pozitia cea mai mare si cea mai mica.
struct route_table_entry* binary_search(struct route_table_entry *low, int high, uint32_t search)
{
	if (high == 0)
		return low;
	if (high == 1)
	{
		if ((search & ((low + 1)->mask)) == (low + 1)->prefix)
			return low + 1;
		return low;
	}
	struct route_table_entry *mid = low + (high / 2);
	if (mid->prefix > (search & (mid->mask)))
		return binary_search(low, mid - low, search);
	return binary_search(mid, high - (mid - low), search);
}

// efectiv rescrie un pachet dat de IP astfel incat sa aiba
// si ICMP.
void create_error_icmp(packet *m, int type, int code)
{
	uint8_t *in_bytes = (uint8_t *)(&m->payload);
	in_bytes += sizeof(struct ether_header);
	struct iphdr *ip_hdr = (struct iphdr *)(in_bytes);
	in_bytes += sizeof(struct iphdr);
	struct icmphdr *icmp_hdr = (struct icmphdr *)(in_bytes);
	in_bytes += sizeof(struct icmphdr);
	uint8_t *payload = in_bytes;
	memcpy(payload, icmp_hdr, 64); // ia ce era in locul icmp_hdr in original
	icmp_hdr->type = type;
	icmp_hdr->code = code;
	icmp_hdr->checksum = 0;
	icmp_hdr->checksum = icmp_checksum((uint16_t*)icmp_hdr, sizeof(struct icmphdr));
	uint32_t ip = ip_hdr->saddr;
	uint32_t my_ip;
	inet_pton(AF_INET, get_interface_ip(m->interface), &my_ip);
	ip_hdr->saddr = my_ip;
	ip_hdr->daddr = ip;
	ip_hdr->protocol = IP_ICMP_PROTOCOL;
	ip_hdr->tot_len = sizeof(struct ether_header) + sizeof(struct icmphdr) + 64;
	ip_hdr->ttl = MAX_TTL;
	m->len = sizeof(struct ether_header) + ip_hdr->tot_len;
}

// incearca sa gaseasca cea mai buna ruta din tabel.
bool best_route_search(packet *m, struct route_table_entry *best, struct iphdr *ip_hdr)
{
	// cautare in tabela de rutare in O(log N), unde N este numarul
	// de intrari
	uint32_t ip = ip_hdr->daddr;
	(*best) = *binary_search(route_table, rtable_len - 1, ip);
	if ((best->mask & ip) != best->prefix)
	{
		create_error_icmp(m, ICMP_DEST_UNREACHABLE, 0);
		ip_hdr->check = 0;
		ip_hdr->check = ip_checksum((uint8_t *) ip_hdr, sizeof(struct iphdr));
		return best_route_search(m, best, ip_hdr);
	}
	return true;
}

// schimba un pachet existent de icmp request intr-unul reply.
void create_echo_reply(packet *m)
{
	uint8_t *in_bytes = (uint8_t *)(&m->payload);
	in_bytes += sizeof(struct ether_header);
	struct iphdr *ip_hdr = (struct iphdr *)(in_bytes);
	in_bytes += sizeof(struct iphdr);
	struct icmphdr *icmp_hdr = (struct icmphdr *)(in_bytes);
	in_bytes += sizeof(struct icmphdr);
	if (icmp_hdr->type != ECHO_REQUEST_TYPE || icmp_hdr->code != 0)
		return;
	icmp_hdr->type = ECHO_REPLY_TYPE;
	icmp_hdr->code = 0;
	icmp_hdr->checksum = 0;
	icmp_hdr->checksum = icmp_checksum((uint16_t*)icmp_hdr, sizeof(struct icmphdr));
	icmp_hdr->un.echo.sequence++;
	//lasa cum era payload-ul...
	ip_hdr->ttl = MAX_TTL;
}

// schimba un pachet existent de IP cum trebuie pentru noile campuri.
bool forward_ip(packet *m, struct route_table_entry *best)
{
	struct iphdr *ip_hdr = (struct iphdr *)(m->payload + sizeof(struct ether_header));
	uint32_t ip = ip_hdr->daddr;
	uint32_t my_ip;
	inet_pton(AF_INET, get_interface_ip(m->interface), &my_ip);
	// checksum
	int received_checksum = ip_hdr->check;
	ip_hdr->check = 0;
	int actual_checksum = ip_checksum((uint8_t*) ip_hdr, sizeof(struct iphdr));
	if (received_checksum != actual_checksum)
		return false; // si iara, bataie de joc cu aruncatul asta...
	// ttl
	uint16_t prev_ttl = ip_hdr->ttl;
	ip_hdr->ttl--;
	uint16_t new_ttl = ip_hdr->ttl;
	ip_hdr->check = actual_checksum - ~prev_ttl - new_ttl - 1;
	bool isMine = ip == my_ip;

	// daca este destinat pentru pachet atunci este pentru echo reply.
	if (isMine && ip_hdr->protocol == IP_ICMP_PROTOCOL)
		create_echo_reply(m);
	else if (ip_hdr->ttl <= 0)
	{
		// trimitem icmp cu problema
		create_error_icmp(m, ICMP_TLE_TYPE, 0);
		ip_hdr->check = 0;
		ip_hdr->check = ip_checksum((uint8_t *) ip_hdr, sizeof(struct iphdr));
	}

	return best_route_search(m, best, ip_hdr);
}

// creaza un pachet de ARP request.
packet create_arp(struct route_table_entry best)
{
	packet result;
	struct arp_header *arp_hdr = (struct arp_header *)malloc(sizeof(struct arp_header));
	result.interface = best.interface;

	arp_hdr->htype = htons(ARPHRD_ETHER);
	arp_hdr->ptype = htons(2048);
	arp_hdr->hlen = HLEN_MAC;
	arp_hdr->plen = PLEN_IP;
	arp_hdr->op = htons(ARP_REQUEST);
	//nu avem ce scrie la tha ca pe aia o dorim
	arp_hdr->spa = inet_addr(get_interface_ip(best.interface));
	arp_hdr->spa = arp_hdr->spa;
	get_interface_mac(best.interface, arp_hdr->sha);
	arp_hdr->tpa = best.next_hop;

	struct ether_header eth_hdr;
	get_interface_mac(best.interface, eth_hdr.ether_shost);
	for (int j = 0; j < ETH_ALEN; j++)
		eth_hdr.ether_dhost[j] = 255;
	eth_hdr.ether_type = htons(ETHERTYPE_ARP);

	memcpy(result.payload, &eth_hdr, sizeof(struct ether_header));
	memcpy(result.payload + sizeof(struct ether_header), arp_hdr, sizeof(struct arp_header));

	result.len = sizeof(struct ether_header) + sizeof(struct arp_header);
	return result;
}

void do_arp(packet m, struct route_table_entry best)
{
	struct ether_header *eth = (struct ether_header*)(&m.payload);
	// cautare in tabela de arp + rescriere in adresa de L2 (daca e cazul)
	bool found = false;
	for (list current = arp_entries->head; current != NULL; current = current->next)
	{
		struct arp_entry* a = current->element;
		if (a->ip == best.next_hop)
		{
			found = true;
			for (int j = 0; j < ETH_ALEN; j++)
				eth->ether_dhost[j] = a->mac[j];
		}
	}
	m.interface = best.interface;
	get_interface_mac(m.interface, eth->ether_shost);
	// salveaza pachetul pentru mai tarziu
	if (!found)
	{
		packet_with_best *saved_packet = (packet_with_best *) malloc(sizeof(packet_with_best));
		memcpy(&saved_packet->p, &m, sizeof(packet));
		saved_packet->best = best;
		queue_enq(messages_to_send, saved_packet);
		packet arp_packet = create_arp(best);
		send_packet(&arp_packet);
		return;
	}
	// trimiterea noului pachet
	send_packet(&m);
}

int main(int argc, char *argv[])
{
	packet m;
	int rc;

	// Do not modify this line
	init(argc - 2, argv + 2);

	rtable_len = read_rtable(argv[1], route_table);
	qsort(route_table, rtable_len, sizeof(struct route_table_entry), rtable_comp);
	messages_to_send = queue_create();
	arp_entries = queue_create();

 	while (1) {
		rc = get_packet(&m);
		DIE(rc < 0, "get_packet");
		if (m.len < sizeof(struct ether_header))
			continue; // lol arunca-l pe jos
		struct ether_header *eth = (struct ether_header*)(&m.payload);
		uint8_t my_mac[ETH_ALEN];
		get_interface_mac(m.interface, my_mac);
		bool mine = true;
		// verifica daca este destinat pentru el
		for (int i = 0; i < ETH_ALEN; i++)
			if (my_mac[i] != eth->ether_dhost[i])
			{
				mine = false;
				break;
			}
		// verifica daca este broadcast
		bool is_broadcast = true;
		for (int i = 0; i < ETH_ALEN; i++)
			if (255 != eth->ether_dhost[i])
			{
				is_broadcast = false;
				break;
			}
		
		// protocolul ipv4
		if (ntohs(eth->ether_type) == ETHERTYPE_IP)
		{
			if (!mine && !is_broadcast)
				continue; // nu imi mai aruncati pachetele pe jos :(
						  // stiu ca nu arata prea bine, dar sunt si ele fiinte.
			struct route_table_entry best;

			if (forward_ip(&m, &best))
				do_arp(m, best);
		}
		else if (ntohs(eth->ether_type) == ETHERTYPE_ARP)
		{
			struct arp_header *arp_hdr = (struct arp_header *)(m.payload + sizeof(struct ether_header));
			// updateaza mesajul in arp_reply si il trimite.
			if (arp_hdr->op == ntohs(ARP_REQUEST) && inet_addr(get_interface_ip(m.interface)) == arp_hdr->tpa)
			{
				arp_hdr->op = ntohs(ARP_REPLY);
				for (int i = 0; i < ETH_ALEN; i++)
					eth->ether_dhost[i] = eth->ether_shost[i];
				get_interface_mac(m.interface, eth->ether_shost);
				memcpy(arp_hdr->tha, arp_hdr->sha, ETH_ALEN);
				uint32_t aux = arp_hdr->spa;
				arp_hdr->spa = arp_hdr->tpa;
				arp_hdr->tpa = aux;
				get_interface_mac(m.interface, arp_hdr->sha);
				m.len = sizeof(struct ether_header) + sizeof(struct arp_header);
				send_packet(&m);
				continue;
			}
			struct arp_entry *arp = malloc(sizeof(struct arp_entry));
			arp->ip = arp_hdr->spa;
			for (int j = 0; j < ETH_ALEN; j++)
				arp->mac[j] = arp_hdr->sha[j];
			// updateaza coada de arp-uri.
			queue_enq(arp_entries, arp);
			// cauta ce mesaje sunt de trimis. Parcurgem coada scotand
			// cu queue_deq. Elementele scoase daca inca nu am putut
			// sa le trimitem le punem intr-o noua coada. La final
			// schimbam coada de mesaje la noua coada.
			queue new_q = queue_create();
			while (messages_to_send->head != NULL)
			{
				packet_with_best *x = queue_deq(messages_to_send);
				struct ether_header *eth2 = (struct ether_header*)(x->p.payload);
				if (x->best.next_hop == arp_hdr->spa)
				{
					for (int j = 0; j < ETH_ALEN; j++)
						eth2->ether_dhost[j] = arp_hdr->sha[j];
					send_packet(&(x->p));
					free(x);
				}
				else
				{
					queue_enq(new_q, x);
				}
			}
			messages_to_send = new_q;
		}
		else
			continue;// iar il aruncara pe Vasile pe jos
	}
}
