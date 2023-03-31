#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
int main(int argc, char **argv)
{
	struct in_addr addr;
	struct hostent *phost;
	if (inet_pton(AF_INET, argv[1], &addr) <= 0) {
		printf("inet_pton error:%s\n", strerror(errno));
		return -1;
	}
	phost = gethostbyaddr((const char*)&addr, sizeof(addr), AF_INET);
	if (phost == NULL) {
		printf("gethostbyaddr error:%s\n", strerror(h_errno));
		return -1;
	}	
	printf("host name:%s\n", phost->h_name);
	return 0;
}