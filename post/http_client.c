#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

int main(int argc, char*argv[])
{
	if (argc != 2)
        {
                printf ("%s PORT\n", argv[0]);
                return 1;
        }

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);

        struct sockaddr_in servaddr;
        bzero(&servaddr, sizeof(servaddr));
        servaddr.sin_family = AF_INET;
        inet_pton(AF_INET, "127.0.0.1", &servaddr.sin_addr);
        servaddr.sin_port = htons(atoi(argv[1]));

        int rc = connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
        if (rc == -1)
        {
                perror("connect");
                close(sockfd);
                return -1;
        }

	char sbuf[32] = {"GET /getInfo HTTP/1.1 \r\n\r\n"};
	//char sbuf[512] = {"GET /getNWInfo?nwID=1234 HTTP/1.1\r\nContent-Length: 0\r\nContent-Type: application/octet-stream\r\nCookie: client=1314520\r\naction: 789654\r\n\r\n"};
	rc = send(sockfd, sbuf, strlen(sbuf), 0);
	printf("send end, rc = %d\n", rc);

	while(1)
	{
		char rbuf[1024] = {0};
		rc = recv(sockfd, rbuf, sizeof(rbuf), 0);
		printf("recv end, rc = %d, rbuf = |%s|\n", rc, rbuf);
		sleep(1);
	}

        close(sockfd);
        return 0;
}
