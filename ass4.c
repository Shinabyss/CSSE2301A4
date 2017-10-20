#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>

#define MAXHOSTNAMELEN 128
#define DOOM "doomtrain"
#define STOP "stopstation"
#define ADD "add("

typedef struct Resource {
    char* name;
    int load;
    int unload;
} Resource;

typedef struct LogInfo {
    char* stationName;
    int trainsProcessed;
    int wrongStation;
    int formatError;
    int invalidNextStation;
    char* connectedStation;
    char* exitReason;
} LogInfo;

typedef struct ThreadArgs {
    int64_t fd;
    FILE* logFile;
    Resource* allResources;
    char* msg;
} ThreadArgs;


char* get_line(FILE* file, unsigned max);

/*Store the Log information
**in the form of struct
**
**
**
**
**
*/
LogInfo stationLog;

/*int resources_length(Resource* resources)
**      return the length of a resource array
**      provided the array terminates with
**      resource that has name '\0'
**
**
**
**
*/
int resources_length(Resource* resources) {
    int retval = 0;
    while (resources[retval].name != '\0') {
        retval++;
    }
    retval--;
    return retval;
}

/*void write_log(FILE* logFile)
**      writes to the logfile
**      using information stored in the
**      global variable stationLog
**
**
**
*/
void write_log(FILE* logFile, Resource* allResources) {
    int i;
    fprintf(logFile, "Processed: ");
    fprintf(logFile, "%d", stationLog.trainsProcessed);
    fprintf(logFile, "\n");
    fprintf(logFile, "Not mine: ");
    fprintf(logFile, "%d", stationLog.wrongStation);
    fprintf(logFile, "\n");
    fprintf(logFile, "Format err: ");
    fprintf(logFile, "%d", stationLog.formatError);
    fprintf(logFile, "\n");
    fprintf(logFile, "No fwd: ");
    fprintf(logFile, "%d", stationLog.invalidNextStation);
    fprintf(logFile, "\n");
    if(resources_length(allResources)) {
        for(i = 0; i < resources_length(allResources); i++) {
            fprintf(logFile, "%s %d %d\n", allResources[i].name, 
                    allResources[i].load, allResources[i].unload);
        }
    }
}

/*Resource* append_resources(Resource* resources, Resource resour)
**      append a resource to a resource array
**      terminate the resource array with a new resource that
**      has name = '\0'
**
**
**
*/
Resource* append_resources(Resource* resources, Resource resour) {
    int len = resources_length(resources);
    int i;
    Resource terminator;
    Resource* retval = (Resource*)malloc(sizeof(Resource) * (len + 2));
    for (i = 0; i < len; i++) {
        retval[i] = resources[i];
    }
    retval[len] = resour;
    terminator.name = "\0";
    retval[len + 1] = terminator;
    return retval;
}

/*int open_listen(int port)
**      open a port with given port number
**      and listen to connection
**
**
**
*/
int open_listen(int port) {
    int fd;
    struct sockaddr_in serverAddr;
    int optVal;
    struct sockaddr addr;
    socklen_t* addrlen;
    addrlen = malloc(sizeof(addr) * 128);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0) {
        perror("Error creating socket\n");
        exit(1);
    }
    optVal = 1;
    if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optVal, sizeof(int)) < 0) {
        perror("Error setting socket option\n");
        exit(1);
    }
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    if(bind(fd, (struct sockaddr*)&serverAddr, sizeof(struct sockaddr_in)) 
            < 0) {
        perror("Error binding socket to port\n");
        exit(1);
    }
    if(listen(fd, SOMAXCONN) < 0) {
        perror("Error listening\n");
        exit(1);
    }
    if(getsockname(fd, &addr, addrlen) < 0) {
        perror("Error getting port\n");
        exit(1);
    }
    printf("connected to port: %d\n", ntohs(((struct sockaddr_in*)
            &addr)->sin_port));
    return fd;
}

/*int argument_check(int argc, char* argv[])
**      argument check the invocation arguments
**      return an exit status depending on
**      argument error
**
**
*/
int argument_check(int argc, char* argv[]) {
    FILE* auth = NULL;
    FILE* log = NULL;
    char* authLine1;
    if ((argc < 4) || (argc > 6)) {
        fprintf(stderr, 
                "Usage: station name authfile logfile [port] [host]\n");
        return 1;
    }
    if (argv[1][0] == '\0') {
        fprintf(stderr, "Invalid name/auth\n");
        return 2;
    }
    auth = fopen(argv[2], "r");
    if (auth == NULL) {
        fprintf(stderr, "Invalid name/auth\n");
        return 2;
    }
    authLine1 = get_line(auth, 255);
    if ((authLine1[0] == '\0')) {
        fprintf(stderr, "Invalid name/auth\n");
        return 2;
    }
    log = fopen(argv[3], "w");
    if (log == NULL) {
        fprintf(stderr, "Unable to open log\n");
        return 3;
    }
    if (argc > 4) {
        if ((atoi(argv[4]) < 1) || (atoi(argv[4]) > 65534)) {
            fprintf(stderr, "Invalid port\n");
            return 4;
        }
    }
    return 0;

}

/*char* get_message(char* origin)
**      read a string up to ':' or '\0' or '\n'
**      and return the read string
**
**
**
**
*/
char* get_message(char* origin) {
    char* retval = (char*)malloc(1024 * sizeof(char));
    int i = 0;
    while((origin[0] != ':') && (origin[0] != '\0') && (origin[0] != '\n')) {
        retval[i] = origin[0];
        origin++;
        i++;
    }
    retval[i] = '\0';
    origin++;
    return retval;
}

/*char* get_resource(char* origin)
**      read the name of the resource
**      from a string and return it as
**      a string
**
**
*/
char* get_resource(char* origin) {
    char* retval = (char*)malloc(1024 * sizeof(char));
    int i = 0;
    while((origin[0] != '+') && (origin[0] != '\0') && (origin[0] != '-')) {
        retval[i] = origin[0];
        origin++;
        i++;
    }
    retval[i] = '\0';
    return retval;
}

/*int get_quantity(char* origin)
**      read the quantity of the resource
**      from a string and return it as int
**
**
*/
int get_quantity(char* origin) {
    char* retval = (char*)malloc(1024 * sizeof(char));
    int i = 0;
    while((origin[0] != ',') && (origin[0] != '\0') && (origin[0] != '\n')) {
        retval[i] = origin[0];
        origin++;
        i++;
    }
    retval[i] = '\0';
    return atoi(retval);
}

/*get_line(FILE* file, unsigned maxchar)
**      -read from a file pointer and return a string
**      of size maxchar or entire line which ever is
**      shorter.
**
**
**
*/
char* get_line(FILE* file, unsigned max) {
    char* line;
    int i, character;
    line = (char*)malloc((max + 1) * sizeof(char));
    if(file == NULL) {
        printf("File reading failed");
    } else {
        for(i = 0; i < max; i++) {
            character = fgetc(file);
            if((((char)character) == '\n') || (((char)character) == ':')) {
                break;
            } else if(feof(file)) {
                break;
            }
            line[i] = character;
        }
    }
    if(strlen(line) > 0) {
        line[i] = '\0';
        return line;
    } else {
        line[0] = '\0';
        return line;
    }
}

/*int update_resources(Resource* allResources, char* name,
**         char operator, int amount)
**      append/update a resource to an resource array.
**      return 1 if append successful
**      return 0 if resource updated successfully
**
**
**
*/
int update_resources(Resource* allResources, char* name, 
        char operator, int amount) {
    Resource resource;
    int i;
    for(i = 0; i < resources_length(allResources); i++) {
        if(!strcmp(name, allResources[i].name)) {
            if(operator == '+') {
                allResources[i].load += amount;
            } else if(operator == '-') {
                allResources[i].unload -= amount;
            } else {
                stationLog.formatError++;
            }
            return 0;
        }
    }
    resource.name = name;
    if(operator == '+') {
        resource.load += amount;
    } else if(operator == '-') {
        allResources[i].unload -= amount;
    } else {
        stationLog.formatError++;
    }
    append_resources(allResources, resource);
    return 1;
}

/*int sigfig(int value)
**      takes in an int value
**      and return the number of
**      significant figure the
**      int has in int
**
*/
int sigfig(int value) {
    if(value > 9999) {
        return 5;
    } else if (value > 999) {
        return 4;
    } else if (value > 99) {
        return 3;
    } else if (value > 9) {
        return 2;
    }
    return 1;
}

/*void* resource_thread(void* arg)
**      thread that deals with incoming
**      resource train
**
**
**
*/
void* resource_thread(void* arg) {
    char* resourceList; 
    char* resource;
    char divider, operator;
    int quantity;
    ThreadArgs* threadArgs = (ThreadArgs*)arg;
    resourceList = threadArgs->msg;
    divider = ',';
    while(divider == ',') {
        resource = get_resource(resourceList);
        resourceList += strlen(resource);
        operator = resourceList[0];
        resourceList++;
        quantity = get_quantity(resourceList);
        resourceList += sigfig(quantity);
        divider = resourceList[0];
        resourceList++;
        update_resources(threadArgs->allResources, resource, operator, 
                quantity);

    }
    return NULL;
}

/*void process_doom(FILE* logFile)
**      process doom train and indicate on logFile
**
**
**
**
*/
void process_doom(FILE* logFile, Resource* allResources) {
    stationLog.trainsProcessed++;
    stationLog.exitReason = "doomtrain\0";
    write_log(logFile, allResources);
    fprintf(logFile, "doomtrain\n");
    exit(0);
}

/*void process_stop(FILE* logFile)
**      process stopstation train and indicate on logFile
**
**
**
*/
void process_stop(FILE* logFile, Resource* allResources) {
    stationLog.trainsProcessed++;
    stationLog.exitReason = "stopstation\0";
    write_log(logFile, allResources);
    fprintf(logFile, "stopstation\n");
    exit(0);
}

/*void* client_thread(void* arg)
**      Main client thread for user connection
**
**
**
*/
void* client_thread(void* arg) {
    int fd;
    char buffer[1024], divider;
    char* pbuffer; 
    char* msg; 
    char* destination;
    divider = ':';
    ssize_t numBytesRead;
    pthread_t resourceID;
    ThreadArgs* threadArgs = (ThreadArgs*)arg;
    fd = (int)(threadArgs->fd);
    while((numBytesRead = read(fd, buffer, 1024)) > 0) {
        pbuffer = (char*)malloc(sizeof(char) * 1024);
        pbuffer = buffer;
        while(divider == ':') {
            if(pbuffer[0] == ':') {
                pbuffer++;
            }
            destination = get_message(pbuffer);
            pbuffer += (strlen(destination) + 1);
            msg = get_message(pbuffer);
            if(!strcmp(DOOM, msg)) {
                process_doom(threadArgs->logFile, threadArgs->allResources);
            } else if(!strcmp(STOP, msg)) {
                process_stop(threadArgs->logFile, threadArgs->allResources);
            } else if(!strncmp(ADD, msg, 4)) {
                printf("add station received: %s %s\n", destination, msg);
                //add trainstation
            } else {
                threadArgs->msg = msg;
                pthread_create(&resourceID, NULL, resource_thread, 
                        (void*)threadArgs);
                pthread_detach(resourceID);
                stationLog.trainsProcessed++;
            }
            pbuffer += (strlen(msg));
            divider = pbuffer[0];
        }
    }
    if(numBytesRead < 0) {
        perror("Error reading from socket\n");
        exit(1);
    }
    printf("Done\n");
    fflush(stdout);
    close(fd);
    pthread_exit(NULL);
    return NULL;
}

/*void process_connections(int fdServer, FILE* logFile, Resource* allResources)
**      process connection and start client thread
**
**
**
*/
void process_connections(int fdServer, FILE* logFile, Resource* allResources) {
    int fd;
    struct sockaddr_in fromAddr;
    socklen_t fromAddrSize;
    int error;
    char hostname[MAXHOSTNAMELEN];
    pthread_t threadId;
    ThreadArgs* clientThreadArgs;
    clientThreadArgs = (ThreadArgs*)malloc(sizeof(ThreadArgs));
    clientThreadArgs->logFile = logFile;
    clientThreadArgs->allResources = allResources;
    
    while(1) {
        fromAddrSize = sizeof(struct sockaddr_in);
        fd = accept(fdServer, (struct sockaddr*)&fromAddr, &fromAddrSize);
        clientThreadArgs->fd = fd;
        if(fd < 0) {
            perror("Error accepting connection\n");
            exit(1);
        }
        error = getnameinfo((struct sockaddr*)&fromAddr, fromAddrSize, 
                hostname, MAXHOSTNAMELEN, NULL, 0, 0);
        if(error) {
            fprintf(stderr, "Error getting hostname: %s\n",
                    gai_strerror(error));
        } else {
            printf("Accepted connection from %s (%s), port %d\n",
                    inet_ntoa(fromAddr.sin_addr), hostname,
                    ntohs(fromAddr.sin_port));
            write(fd, "Welcome...\n", 11);
        }
        pthread_create(&threadId, NULL, client_thread, 
                (void*)clientThreadArgs);
        pthread_detach(threadId);
    }
    free(clientThreadArgs);
}

/*int main(int argc, char* argv[])
**      main function of the program
**
**
**
**
*/
int main(int argc, char* argv[]) {
    int exitStatus;
    int portNum;
    int fdServer;
    FILE* logFile;
    Resource* allResources;
    stationLog.stationName = argv[1];
    stationLog.trainsProcessed = 0;
    stationLog.wrongStation = 0;
    stationLog.formatError = 0;
    stationLog.invalidNextStation = 0;
    allResources = (Resource*)malloc(sizeof(Resource) * 1024);
    allResources[0].name = "\0";
    allResources[0].load = 0;
    allResources[0].unload = 0;
    exitStatus = argument_check(argc, argv);
    if(exitStatus != 0) {
        return exitStatus;
    }
    if(argc > 4) {
        portNum = atoi(argv[4]);
    } else {
        portNum = 0;
    }
    logFile = fopen(argv[3], "w");
    fprintf(logFile, "=======\n");
    fprintf(logFile, stationLog.stationName);
    fprintf(logFile, "\n");
    fdServer = open_listen(portNum);
    process_connections(fdServer, logFile, allResources);
    printf("ran normally\n");
    return exitStatus;
}

