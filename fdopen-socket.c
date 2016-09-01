////////////////////////////serve.c////////////////////////////////// 
#include <sys/types.h> 
#include <sys/socket.h> 
#include <stdlib.h> 
#include <sys/stat.h> 
#include <fcntl.h> 
#include <unistd.h> 
#include <stdio.h> 
#include <string.h> 
#include <signal.h> 
#include <sys/wait.h> 
#include <netinet/in.h> 

char buf[]="Welcome!"; 
static void sigchld_hander( int signo ) 
{ 
    pid_t pid; 
    int status; 

    do { 
        pid = waitpid ( -1, &status, WNOHANG ); 
    } while ( pid != -1 ); 

    //reset the signal hander 
    signal( SIGCHLD, sigchld_hander ); 
} 

int main(int argc, char **argv) 
{ 
    int socketfd,bindfd,acceptfd; 
    //buf for write 
    char wbuf[256]; 
    //open a socket into read socket and write socket 
    FILE *rfd, *wfd; 
    struct sockaddr_in *seraddr,*clientaddr; 
    seraddr=malloc(sizeof(struct sockaddr_in)); 
    clientaddr=malloc(sizeof(struct sockaddr_in)); 

    //catch the signal SIGCHLD 
    signal( SIGCHLD, sigchld_hander); 
    socketfd = socket(PF_INET,SOCK_STREAM,0); 
    if( socketfd < 0 ) { 
        perror("create socket error"); 
        exit(-1); 
    } 

    bzero(seraddr, sizeof(struct sockaddr_in) ); 
    seraddr->sin_family=PF_INET; 
    if( (seraddr->sin_addr.s_addr=INADDR_ANY) <0 ) 
        perror("INADDR_ANY"); 

    if(argc == 2) { 
        int portnum = atoi(argv[1]); 
        seraddr->sin_port=htons(portnum); 
    } else { 
        seraddr->sin_port=htons(9091); 
    } 

    bindfd = bind(socketfd, (struct sockaddr*)seraddr, 
            sizeof(struct sockaddr)); 
    if( bindfd < 0 ){ 
        perror("creat bindfd error"); 
        exit(-1); 
    } 

    if(listen( socketfd, 10) == -1 ) { 
        perror("listen error"); 
        exit(-1); 
    } 

    while(1) { 
        int len; 
        pid_t pid; 

        len = sizeof(struct sockaddr); 
        acceptfd = accept(socketfd, 
                (struct sockaddr *)clientaddr, 
                &len ); 

        if( acceptfd < 0 ) { 
            perror( "accetpfd "); 
            exit(1); 
        } 

        if( (pid=fork()) == 0 ) { 
            rfd = fdopen(acceptfd, "r"); 
            if (rfd == NULL ) { 
                perror("fdopen ( acceptfd , r ) "); 
                fclose( rfd ); 
                continue; 
            } 

            wfd = fdopen( dup(acceptfd), "w"); 
            if ( wfd == NULL ) { 
                perror("fdopen ( acceptfd , w ) "); 
                fclose( wfd ); 
                continue; 
            } 

            setlinebuf( rfd ); 
            setlinebuf( wfd ); 

            if( fputs( buf, wfd ) > 0 ) { 
                printf( "write successful "); 
            } //doesn't work 

            printf("before fgets "); //stop here 
            while ( fgets( wbuf, sizeof wbuf , rfd ) ) { 
                printf("after fgets "); 
                fputs ( wbuf, stdout ); 
                fputs( " ", stdout); 
            } 
            if ( fclose( wfd ) < 0 ) { 
                perror("fclose (rfd) "); 
                exit(1); 
            } 
            shutdown( fileno( rfd ), SHUT_RDWR ); 
            if ( fclose( rfd ) < 0 ) { 
                perror("fclose (rfd) "); 
                exit(1); 
            } 
            exit( 0 ); 
        } 
    } 

    free(seraddr); 
    free(clientaddr); 
    exit(0); 
} 

//////////////////////////////////client.c///////////////////////////////////////////// 
#include <unistd.h> 
#include <string.h> 
#include <stdlib.h> 
#include <stdio.h> 
#include <sys/types.h> 
#include <sys/stat.h> 
#include <sys/socket.h> 
#include <netinet/in.h> 
#include <arpa/inet.h> 

int main(int argc, char **argv) 
{ 
    int socketfd; 
    char buf[256]; 
    struct sockaddr_in *clientaddr=malloc(sizeof(struct sockaddr_in)); 
    char *addr=NULL; 

    socketfd = socket(PF_INET, SOCK_STREAM, 0 ); 
    if(socketfd < 0 ) 
    { 
        perror("socketfd error"); 
        exit(-1); 
    } 

    memset(clientaddr, 0,sizeof clientaddr ); 

    clientaddr->sin_family=PF_INET; 
    if(argc == 2 ) 
    { 
        inet_aton(argv[1], &(clientaddr->sin_addr) ); 
    } else 
    { 
        addr="127.0.0.1"; 
        clientaddr->sin_addr.s_addr=inet_addr("127.0.0.1"); 
    } 
    clientaddr->sin_port=htons(9091); 

    if( connect( socketfd, ( struct sockaddr *)clientaddr, 
                sizeof( struct sockaddr) ) ==0 ) 
    { 
        int n; 

        if ( ( n=read(socketfd, buf, sizeof( buf) ) ) > 0 ) //didn't receive any, but it should receive welcome frome server 
        { 
            buf[n]=0; 
            printf(buf); 
            printf(" "); 
            bzero(buf,strlen(buf)); 
        } 

        if ( fgets(buf,sizeof(buf),stdin) ) 
        { 
            buf[ strlen(buf)-1 ]=0; 
            write(socketfd,buf,strlen(buf) ); //it should write to server , but serve doesn't recieve 
        } 

    } else 
    { 
        perror("connect error "); 
    } 

    printf("connect closed! "); 
    close(socketfd); 
    free(clientaddr); 

    exit(0); 
} 

//////////////////////////////end//////////////////////////////////////// 
// http://unixresources.net/linux/clf/program/archive/00/00/30/44/304463.html
