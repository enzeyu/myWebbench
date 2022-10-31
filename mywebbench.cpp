//
// Created by enzeyu on 2022/10/28.
//
//* Return codes:
//*    0 - sucess
//*    1 - benchmark failed (server is not on-line)
//*    2 - bad param
//*    3 - internal error, fork failed

#include <sys/param.h>
#include "socket.cpp"
#include <strings.h>
#include <getopt.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>


/* globals */
volatile int timerexpired = 0; // flag for expiring
int success = 0; // time for success
int failure = 0; // time for failure
int bytes = 0; // throughput

int http10=1; /* 0 - http/0.9, 1 - http/1.0, 2 - http/1.1 */
/* Allow: GET, HEAD, OPTIONS, TRACE */
#define METHOD_GET 0
#define METHOD_HEAD 1
#define METHOD_OPTIONS 2
#define METHOD_TRACE 3
#define PROGRAM_VERSION "1.6"
#define REQUEST_MAXIMUM 2048
int method = METHOD_GET;
int client; // number of clients
char* proxyhostname = NULL;
int proxyport = 80;
int force = 0; // Don't wait for reply from server
int force_reload = 0; // force to reload without cache
int runtime = 30;

int mypipe[2]; // pipe for reading and wrinting
char host[MAXHOSTNAMELEN];
char request[REQUEST_MAXIMUM]; // storage for request header

// if flag no NULL, set *flag to val
static const struct option options[]={
        {"force",no_argument,&force,1},
        {"reload",no_argument,&force_reload,1},
        {"time",required_argument,NULL,'t'},
        {"help",no_argument,NULL,'?'},
        {"http09",no_argument,NULL,'9'},
        {"http10",no_argument,NULL,'1'},
        {"http11",no_argument,NULL,'2'},
        {"get",no_argument,&method,METHOD_GET},
        {"head",no_argument,&method,METHOD_HEAD},
        {"options",no_argument,&method,METHOD_OPTIONS},
        {"trace",no_argument,&method,METHOD_TRACE},
        {"version",no_argument,NULL,'V'},
        {"proxy",required_argument,NULL,'p'},
        {"clients",required_argument,NULL,'c'}
};

/* prototypes */
static void benchcore(const char* host,const int port, const char *request); // send request
static int bench();
static void build_request(const char *url); // construct request

// hanlder function for alarm, it will be called when receiving alarm
static void alarm_handler(int signal)
{
    timerexpired=1;
}

static void usage(){
    fprintf(stderr,
           "webbench [option]... URL\n"
           "  -f|--force               Don't wait for reply from server.\n"
           "  -r|--reload              Send reload request - Pragma: no-cache.\n"
           "  -t|--time <sec>          Run benchmark for <sec> seconds. Default 30.\n"
           "  -p|--proxy <server:port> Use proxy server for request.\n"
           "  -c|--clients <n>         Run <n> HTTP clients at once. Default one.\n"
           "  -9|--http09              Use HTTP/0.9 style requests.\n"
           "  -1|--http10              Use HTTP/1.0 protocol.\n"
           "  -2|--http11              Use HTTP/1.1 protocol.\n"
           "  --get                    Use GET request method.\n"
           "  --head                   Use HEAD request method.\n"
           "  --options                Use OPTIONS request method.\n"
           "  --trace                  Use TRACE request method.\n"
           "  -?|-h|--help             This information.\n"
           "  -V|--version             Display program version.\n"
           );
}

int main(int argc, char *argv[]){
    int options_index = 0;
    char * tmp = NULL;
    int opt=0;

    if(argc==1){
        usage();
        return 2; // argument err
    }

    while((opt=getopt_long(argc,argv,"912Vfrt:p:c:?h",options,&options_index))!=EOF ){
        switch (opt) {
            case  0 : break;
            case 'f': force=1;break;
            case 'r': force_reload=1;break;
            case '9': http10=0;break;
            case '1': http10=1;break;
            case '2': http10=2;break;
            case 'V': printf(PROGRAM_VERSION"\n");exit(0);
            case 't': runtime=atoi(optarg);break;
            case 'p':
                tmp=strrchr(optarg,':');
                proxyhostname=optarg;
                if(tmp==NULL)
                {
                    break;
                }
                if(tmp==optarg){ // : in the initial position of optarg
                    fprintf(stderr,"Error in option --proxy %s: Missing hostname.\n",optarg);
                    return 2;
                }
                if(tmp==optarg+strlen(optarg)-1) // : in the last position of optarg
                {
                    fprintf(stderr,"Error in option --proxy %s Port number is missing.\n",optarg);
                    return 2;
                }
                *tmp='\0'; // change : to \0
                proxyport=atoi(tmp+1);
                break;
            case '?': usage();return 2;break;
            case 'c': client=atoi(optarg);break;
        }
    }

    if(optind==argc) { // no url
        fprintf(stderr,"webbench: Missing URL!\n");
        usage();
        return 2;
    }

    // set client and runtime
    if(client==0) client = 1;
    if(runtime==0) runtime = 30;

    fprintf(stderr,"Webbench - Simple Web Benchmark "PROGRAM_VERSION"\n");
    build_request(argv[optind]);

    return bench();
}

// construct request
void build_request(const char *url){
    char tmp[10];
    int i;

    memset(host,0,MAXHOSTNAMELEN); // 将host和request置为0
    memset(request,0,REQUEST_MAXIMUM);

    if(force_reload && proxyhostname!=NULL && http10<1) http10=1;
    if(method==METHOD_HEAD && http10<1) http10=1;
    if(method==METHOD_OPTIONS && http10<2) http10=2;
    if(method==METHOD_TRACE && http10<2) http10=2;

    switch(method)
    {
        default:
        case METHOD_GET: strcpy(request,"GET");break;
        case METHOD_HEAD: strcpy(request,"HEAD");break;
        case METHOD_OPTIONS: strcpy(request,"OPTIONS");break;
        case METHOD_TRACE: strcpy(request,"TRACE");break;
    }

    strcat(request," ");

    if(NULL==strstr(url,"://"))
    {
        fprintf(stderr, "\n%s: is not a valid URL.\n",url);
        exit(2);
    }
    if(strlen(url)>1500)
    {
        fprintf(stderr,"URL is too long.\n");
        exit(2);
    }
    if (0!=strncasecmp("http://",url,7))
    {
        fprintf(stderr,"\nOnly HTTP protocol is directly supported, set --proxy for others.\n");
        exit(2);
    }

    i=strstr(url,"://")-url+3;

    if(strchr(url+i,'/')==NULL) {
        fprintf(stderr,"\nInvalid URL syntax - hostname don't ends with '/'.\n");
        exit(2);
    }

    if(proxyhostname==NULL)
    {
        /* get port from hostname */
        // url+i represents hostname
        if(index(url+i,':')!=NULL && index(url+i,':')<index(url+i,'/'))
        {
            strncpy(host,url+i,strchr(url+i,':')-url-i);
            //bzero(tmp,10);
            memset(tmp,0,10); // 获取端口号
            strncpy(tmp,index(url+i,':')+1,strchr(url+i,'/')-index(url+i,':')-1);
            /* printf("tmp=%s\n",tmp); */
            proxyport=atoi(tmp);
            if(proxyport==0) proxyport=80;
        }
        else
        {
            strncpy(host,url+i,strcspn(url+i,"/")); // 没有端口直接复制hostname
        }
        // 在request后面添加 请求目录
        strcat(request+strlen(request),url+i+strcspn(url+i,"/"));
    }
    else
    {
        // 直接添加url
        strcat(request,url);
    }

    if(http10==1) // 添加HTTP版本
        strcat(request," HTTP/1.0");
    else if (http10==2)
        strcat(request," HTTP/1.1");

    strcat(request,"\r\n");

    if(http10>0) // 添加User Agent
        strcat(request,"User-Agent: WebBench "PROGRAM_VERSION"\r\n");
    if(proxyhostname==NULL && http10>0) // 添加主机
    {
        strcat(request,"Host: ");
        strcat(request,host);
        strcat(request,"\r\n");
    }

    if(force_reload && proxyhostname!=NULL) // 添加no-cache
    {
        strcat(request,"Pragma: no-cache\r\n");
    }

    if(http10>1) // 添加连接
        strcat(request,"Connection: close\r\n");

    /* add empty line at end */
    if(http10>0) strcat(request,"\r\n");

    //printf("\nRequest:\n%s\n",request);

}

static int bench(){
    int i,j,k; // for success, failure, bytes
    FILE *f;
    pid_t pid=0;

    // check for server availability
    i = Socket(proxyhostname==NULL ? host : proxyhostname,proxyport);
    if(i < 0){
        fprintf(stderr,"Can not connect target server, aborting...\n");
        return 1;
    }
    close(i);

    // create pipe
    if(pipe(mypipe))
    {
        fprintf(stderr,"Create pipe error!");
        return 3;
    }

    // fork process for every client
    for(i=0; i<client; i++){
        pid = fork();
        if(pid <= 0) { // error or child
            break;
        }
    }

    if (pid < 0){
        fprintf(stderr,"\n%d process create error!\n",i);
        perror("fork failed.");
        return 3;
    }

    // son process call benchcore, open the write pipe and write results
    if (pid==0){ // son process
        if(proxyhostname==NULL){
            benchcore(host,proxyport,request);
        }else{
            benchcore(proxyhostname,proxyport,request);
        }
        f = fdopen(mypipe[1],"w");
        if(f==NULL){
            perror("open pipe for writing error!");
            return 3;
        }
        fprintf(f,"%d %d %d\n",success,failure,bytes);
        fclose(f);
        return 0;
    }else{ // father process
        f = fdopen(mypipe[0],"r");
        if(f==NULL){
            perror("open pipe for reading error!");
            return 3;
        }
        // not set for buffer
        setvbuf(f,NULL,_IONBF,0);
        success = 0;
        failure = 0;
        bytes = 0;

        while(1){
            pid = fscanf(f,"%d %d %d",&i,&j,&k);
            if(pid<2){
                fprintf(stderr,"Reading from pipe error!");
                break;
            }
            success += i;
            failure += j;
            bytes += k;

            if(--client==0){ break; }
        }
        fclose(f);

        // ouptput results(speed, bytes, success, err)
        // have flush
        //fflush(stdin);
        printf("\nSpeed=%d pages/min, %d bytes/sec.\nRequests: %d susceed, %d failed.\n",
                (int)((success+failure)/(runtime/60.0f)),
                (int)(bytes/(float)runtime),
                success,
                failure);
//        fprintf(stderr,"\nSpeed=%d pages/min, %d bytes/sec.\nRequests: %d susceed, %d failed.\n",
//               (int)((success+failure)/(runtime/60.0f)),
//               (int)(bytes/(float)runtime),
//               success,
//               failure);
    }
    return i;
}

// send request continuously until timerexpired ends.
// return when timerexpired ends, break when socket error
static void benchcore(const char* host,const int port, const char *request){
    struct sigaction sa;
    int buf[1500];
    int reqlen;
    int s; // file description returb by socket
    int i; // whether success write by read

    // setup alarm signal and its handler
    sa.sa_handler = alarm_handler;
    sa.sa_flags = 0;
    if(sigaction(SIGALRM,&sa,NULL)){ exit(3); }

    alarm(runtime); // call handler after runtime

    reqlen = strlen(request);
    nexttry:while (1){
        if(timerexpired){
            if(failure>0){
                failure--;
            }
            // deduct the failed caused by signal
            return;
        }

        s = Socket(host,port);
        if(s<0) { failure++; continue; }
        if(reqlen!=write(s,buf,reqlen)) {
            failure++;
            close(s);
            continue;
        }
        if(shutdown(s,SHUT_WR)) { // try to close write end
            failure++;
            close(s);
            continue;
        }
        // force = 0 means that wait for reply from server
        if(force==0){
            while(1){
                if(timerexpired) break;
                i= read(s,buf,1500);
                if(i<0){ // if read err, start next read
                    failure++;
                    close(s);
                    goto nexttry;
                }else if(i==0){
                    break;
                }else{
                    bytes+=i;
                }
            }
        }
        if(close(s)) {failure++;continue;}
        success++;
    }
}


