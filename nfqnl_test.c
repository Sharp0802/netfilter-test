#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <linux/types.h>
#include <linux/netfilter.h>
#include <net/ethernet.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <errno.h>

#include <libnetfilter_queue/libnetfilter_queue.h>


static char* block;


typedef uint32_t id_t;

static id_t print_pkt(struct nfq_data* tb)
{
	int id = 0;
	struct nfqnl_msg_packet_hdr* ph;
	struct nfqnl_msg_packet_hw* hwph;
	u_int32_t mark, ifi;
	int ret;
	unsigned char* data;

	ph = nfq_get_msg_packet_hdr(tb);
	if (ph)
	{
		id = ntohl(ph->packet_id);
		printf("hw_protocol=0x%04x hook=%u id=%u ",
				ntohs(ph->hw_protocol), ph->hook, id);
	}

	hwph = nfq_get_packet_hw(tb);
	if (hwph)
	{
		int i, hlen = ntohs(hwph->hw_addrlen);

		printf("hw_src_addr=");
		for (i = 0; i < hlen - 1; i++)
			printf("%02x:", hwph->hw_addr[i]);
		printf("%02x ", hwph->hw_addr[hlen - 1]);
	}

	mark = nfq_get_nfmark(tb);
	if (mark)
		printf("mark=%u ", mark);

	ifi = nfq_get_indev(tb);
	if (ifi)
		printf("indev=%u ", ifi);

	ifi = nfq_get_outdev(tb);
	if (ifi)
		printf("outdev=%u ", ifi);
	ifi = nfq_get_physindev(tb);
	if (ifi)
		printf("physindev=%u ", ifi);

	ifi = nfq_get_physoutdev(tb);
	if (ifi)
		printf("physoutdev=%u ", ifi);

	ret = nfq_get_payload(tb, &data);
	if (ret >= 0)
		printf("payload_len=%d\n", ret);

	fputc('\n', stdout);

	return id;
}

static int callback(
		struct nfq_q_handle* qh,
		struct nfgenmsg*,
		struct nfq_data* nfa,
		void*)
{
	unsigned char* data;
	int r = nfq_get_payload(nfa, &data);

	u_int32_t id = print_pkt(nfa);
	struct iphdr* ip = (struct iphdr*)data;
	if (ip->protocol != IPPROTO_TCP)
		goto PASS;

	char* http = (char*)(data + ip->ihl + ((struct tcphdr*)data + ip->ihl)->th_off);
	r -= ip->ihl + ip->ihl + ((struct tcphdr*)data + ip->ihl)->th_off;
	if (r > 0)
	{
		write(1, http, r);
		printf("\n");
		printf("\n");

		size_t i;
		size_t l = strlen(block);
		for (i = 0; i < r - l; ++i)
		{
			write(1, http + i, l);
			printf("\n");
			if (memcmp(http + i, block, l) == 0)
				return nfq_set_verdict(qh, id, NF_DROP, 0, NULL);
		}
	}

PASS:
	return nfq_set_verdict(qh, id, NF_ACCEPT, 0, NULL);
}

int main(int argc, char* argv[])
{
	struct nfq_handle* h;
	struct nfq_q_handle* qh;
	struct nfnl_handle* nh;
	int fd;
	int rv;
	char buf[4096] __attribute__((aligned));

	if (argc < 2)
	{
		fprintf(stderr,
				"error: required argument missing\n"
				"note: netfilter-test <host>\n"
				"sample: netfilter-test test.gilgil.net");
		return -1;
	}
	else if (argc > 2)
	{
		fprintf(stderr,
				"warn: unrecognized argument will be ignored\n"
				"note: netfilter-test %s %s\n"
				"                       ^ here\n",
				argv[1], argv[2]);
	}

	block = argv[1];

	printf("opening library handle\n");
	h = nfq_open();
	if (!h)
	{
		fprintf(stderr, "error during nfq_open()\n");
		exit(1);
	}

	printf("unbinding existing nf_queue handler for AF_INET (if any)\n");
	if (nfq_unbind_pf(h, AF_INET) < 0)
	{
		fprintf(stderr, "error during nfq_unbind_pf()\n");
		exit(1);
	}

	printf("binding nfnetlink_queue as nf_queue handler for AF_INET\n");
	if (nfq_bind_pf(h, AF_INET) < 0)
	{
		fprintf(stderr, "error during nfq_bind_pf()\n");
		exit(1);
	}

	printf("binding this socket to queue '0'\n");
	qh = nfq_create_queue(h, 0, &callback, NULL);
	if (!qh)
	{
		fprintf(stderr, "error during nfq_create_queue()\n");
		exit(1);
	}

	printf("setting copy_packet mode\n");
	if (nfq_set_mode(qh, NFQNL_COPY_PACKET, 0xffff) < 0)
	{
		fprintf(stderr, "can't set packet_copy mode\n");
		exit(1);
	}

	fd = nfq_fd(h);

	for (;;)
	{
		if ((rv = recv(fd, buf, sizeof(buf), 0)) >= 0)
		{
			nfq_handle_packet(h, buf, rv);
			continue;
		}
		if (rv < 0 && errno == ENOBUFS)
		{
			printf("losing packets!\n");
			continue;
		}
		break;
	}

	printf("unbinding from queue 0\n");
	nfq_destroy_queue(qh);

#ifdef INSANE
	/* normally, applications SHOULD NOT issue this command, since
	 * it detaches other programs/sockets from AF_INET, too ! */
	printf("unbinding from AF_INET\n");
	nfq_unbind_pf(h, AF_INET);
#endif

	printf("closing library handle\n");
	nfq_close(h);

	exit(0);
}

