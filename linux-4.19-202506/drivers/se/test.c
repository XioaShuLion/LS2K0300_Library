#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <poll.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#define SW_GO_DSP		0x0001
#define SW_GO_FPGA0		0x0002
#define SW_GO_FPGA1		0x0000

static unsigned char dsp[8] = {
	0x11, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10
};
static unsigned char eccby[64] = {
	0x15, 0x01, 0x40, 0x00, 0x40, 0x01, 0xff, 0xff,
	0xfd, 0x2f, 0x55, 0xaa, 0x02, 0x00, 0xff, 0xff,
	0x00, 0x00, 0xff, 0xff, 0x01, 0x00, 0x02, 0x00,
	0x03, 0x00, 0x04, 0x00, 0xff, 0xff, 0xff, 0xff,
	0x20, 0x00, 0x20, 0x00, 0x44, 0x00, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0x20, 0x00, 0x20, 0x00,
	0x08, 0x41, 0x72, 0xd3, 0xf4, 0xe5, 0x96, 0xb9,
	0x4a, 0x5b, 0x66, 0xac, 0x00, 0x00, 0x00, 0x00
};
static unsigned char scetest[64] = {
	0x15, 0x01, 0x40, 0x00, 0x80, 0x00, 0x00, 0x00,
	0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
typedef struct tagSWCOMMUDATA {
	unsigned short usFlags;
	unsigned short usInputLen;
	unsigned short usOutputLen;
	unsigned short usReserve;
	unsigned char *pucInbuf;
	unsigned char *pucOutbuf;
} SWCommuData;

inline static int dumph (char *title, void *data, int len)
{
	int j = 0;
	unsigned char *ptr = data;
	printf ("++++++++++++++++[%s](%d bytes)++++++++++++++++", title, len);
	if (data == NULL) {
		printf ("data is null\n");
		return 0;
	}
	for (j = 0; j < len; j++) {
		if (j % 16 == 0) {
			printf ("\n");
		}
		printf ("%02X ", ptr[j]);
	}
	printf ("\n");
	return 0;
}

int main(int argc, char **argv)
{
	int len = 160;
	int fd;
	unsigned char bufsnd[2048] = {0};
	unsigned char bufrcv[2048] = {0};
	char fname[] = "/dev/wst-se";
	unsigned short *pussendbuf = NULL;

	int ch = SW_GO_DSP;

	int option;

	while ((option = getopt(argc, argv, "c:l::h")) != -1) {
		switch (option) {
		case 'c':
			fprintf(stdout, "option -t channel\n");
			ch = atoi(optarg);
			if (ch < 0 || ch > 2) {
				printf("channel num %d is invalid\n", ch);
				exit(EXIT_FAILURE);
			}
			break;
		case 'l': //其后可跟可不跟,不跟默认2048
			len = optarg ? atoi(optarg) : (2048 - 64); // 去掉包头最大包长
			fprintf(stdout, "option -l %d\n", len);
			if (len < 16) {
				printf("len too short\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'h':
			fprintf(stdout, "print help here!\n");
			fprintf(stdout, "arg -c channel interface\n");
			fprintf(stdout, "arg -l send length\n");
			exit(EXIT_FAILURE);
		case '?':
			fprintf(stderr, "unsupport options '%c'\n", optopt);
			exit(EXIT_FAILURE);
		}
	}

	/* Print remaining arguments. */
	for (; optind < argc; optind++)
		printf("invalid arg: %s\n", argv[optind]);
	int iret;
	SWCommuData transpacket;


	transpacket.usFlags = ch;
	transpacket.usInputLen	= len;
	transpacket.usOutputLen = len;
	transpacket.pucInbuf = bufsnd;
	transpacket.pucOutbuf = bufrcv;

	fd = open(fname, O_RDWR);
	if (fd < 0) {
		printf("open error\n");
		return -1;
	}

	memset(bufsnd, 0x5A, 2048);

	if (SW_GO_DSP == ch) //dsp - ch 1
		memcpy(bufsnd, dsp, sizeof(dsp));
	else if (SW_GO_FPGA1 == ch) { //fpga1 - ch 0
		memcpy(bufsnd, eccby, sizeof(eccby));
		pussendbuf = (unsigned short*)bufsnd;
		pussendbuf[2] = len;
		pussendbuf[18] = len;
		transpacket.usInputLen	+= 64; //发送包头
		transpacket.usOutputLen += 16; //接收包头
	} else { // fpga0 - ch 2
		memcpy(bufsnd, scetest, sizeof(scetest));
		pussendbuf = (unsigned short*)bufsnd;
		pussendbuf[2] = len;
		pussendbuf[18] = len;
		transpacket.usInputLen	+= 64; //发送包头
		transpacket.usOutputLen += 16; //接收包头
	}

	dumph("snd", transpacket.pucInbuf, transpacket.usInputLen);
	iret = write(fd, &transpacket, sizeof(SWCommuData));
	if (iret)
		printf("write error %d\n", iret);
	else
		dumph("rcv", transpacket.pucOutbuf, transpacket.usOutputLen);

	close(fd);

	return iret;
}
