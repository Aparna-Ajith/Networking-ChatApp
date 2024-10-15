#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 2048
#define USERNAME_SIZE 32
#define PASSWORD_SIZE 32
#define MESSAGE_SIZE 1024

typedef unsigned char U8;
typedef signed char S8;
typedef unsigned short U16;
typedef signed short S16;
typedef signed int S32;
typedef unsigned int U32;
typedef signed long S64;
typedef unsigned long U64;

pthread_mutex_t FileMutex = PTHREAD_MUTEX_INITIALIZER;                              /* Mutex for file operations. */
FILE *fpfile;                                                                       /* File pointer for logging. */

S32 s32Running = 1;                                                                 /* Flag to check if the client is running. */
U8 u8Username[USERNAME_SIZE];


void *pReceiveHandler(void *pSockfd)                                                /* Recieving message from other clients. */
{ 
    S32 s32Socket = *((int *)pSockfd);
    U8 u8Buffer[BUFFER_SIZE];
    U8 u8Sender[USERNAME_SIZE];
    U8 U8Message[MESSAGE_SIZE];

    while (s32Running) 
    {

        S32 s32Receive = recv(s32Socket, u8Buffer, BUFFER_SIZE - 1, 0);               /* -1 for safety */
        if (s32Receive > 0) 
        {
            u8Buffer[s32Receive] = '\0';                                           /* Null-terminate the received string. */
            
            sscanf(u8Buffer, "%[^:]: %[^\n]", u8Sender, U8Message);                /* Extract the sender and the message (format: "sender: message"). */                       

            printf("\n%s\n> ", u8Buffer);                                          /* Print the received message and prompt for new input.*/
            fflush(stdout);

            if (strncmp(u8Sender, "No active clients", 17) != 0 &&strncmp(u8Sender, "Active clients", 14) != 0 && strncmp(U8Message, "#Chat", 5) != 0) 
            {                                                                      /* Log the message to the user's CSV file */
                pthread_mutex_lock(&FileMutex);
                fprintf(fpfile, "%s;%s;%s\n", u8Sender, u8Username, U8Message);    /* Using semicolon as a delimiter */
                fflush(fpfile);                                                    /* Ensure the message is written immediately */
                pthread_mutex_unlock(&FileMutex);
            }
        } 
        else if (s32Receive == 0) 
        {
            printf("Server disconnected.\n");
            s32Running = 0;
        } 
        else 
        {
            perror("recv failed");
            s32Running = 0;
        }
    }

    return NULL;
}


void *pSendHandler(void *pSockfd)                                                  /* For sending messages to other clients.*/
{ 
    U8 u8Action[USERNAME_SIZE];
    S32 s32Socket = *((int *)pSockfd);
    U8 u8Recipient[USERNAME_SIZE];
    U8 u8Message[MESSAGE_SIZE];                                                    /* Separate buffer for the message */

    while (s32Running) 
    {
        printf("Enter Action (#History , #Chat, 0 to disconnect): ");
        fflush(stdout);
        fgets(u8Action, USERNAME_SIZE, stdin);
        u8Action[strcspn(u8Action, "\n")] = 0;       
        if (strcmp(u8Action, "0") == 0)                                            /* Send a disconnect message to the server*/
        {
            printf("Disconnect command received. Sending disconnect message to server.\n");
            send(s32Socket, "DISCONNECT", strlen("DISCONNECT"), 0);
            s32Running = 0;                                                        /* Set running flag to 0 to stop the client*/
            close(s32Socket);
            break;
        }
        else if (strncmp(u8Action, "#History", 8) == 0) 
        {
            S32 s32No;
            printf("> Enter the number of lines to retrieve from history: ");
            fflush(stdout);
            scanf("%d", &s32No);
            getchar();

            pthread_mutex_lock(&FileMutex);                                        /* Read the last n lines from the CSV file */
            fseek(fpfile, 0, SEEK_END);
            S64 s64FileSize = ftell(fpfile);
            S64 s64Pos = s64FileSize;
            S32 s32LineCount = 0;
            U8 u8Ch;

            while (s64Pos > 0 && s32LineCount <= s32No-1) 
            {
                fseek(fpfile, --s64Pos, SEEK_SET);
                u8Ch = fgetc(fpfile);
                if (u8Ch == '\n' && s64Pos != s64FileSize - 1) 
                {
                    s32LineCount++;
                }
            }

            if (s32LineCount <= s32No-1) 
            {
                fseek(fpfile, 0, SEEK_SET);                                        /* If fewer lines, go to the start. */
            }

            U8 u8Line[BUFFER_SIZE];
            while (fgets(u8Line, sizeof(u8Line), fpfile)) 
            {
                printf("%s", u8Line);
            }
            pthread_mutex_unlock(&FileMutex);

        } 
        else if (strncmp(u8Action, "#Chat", 5) == 0) 
        {
            U8 u8Buffer[BUFFER_SIZE];
            send(s32Socket, "LIST_ACTIVE", strlen("LIST_ACTIVE"), 0);
    
            sleep(1);
            
            printf("> Enter recipient's username: ");
            fflush(stdout);
            fgets(u8Recipient, USERNAME_SIZE, stdin);
            u8Recipient[strcspn(u8Recipient, "\n")] = 0;  
            
            snprintf(u8Buffer, sizeof(u8Buffer), "CHECK %s", u8Recipient);            /* Check if recipient is active. */
            send(s32Socket, u8Buffer, strlen(u8Buffer), 0);

            S32 s32BytesReceived = recv(s32Socket, u8Buffer, BUFFER_SIZE - 1, 0);        /* Receive the response from the server about recipient status.*/
            if (s32BytesReceived > 0) 
            {
                u8Buffer[s32BytesReceived] = '\0';                                    /* Null-terminate the received string.*/

                if (strncmp(u8Buffer, "ACTIVE", 6) == 0) 
                {
                    printf("Recipient is active. You can now send a message.\n");

                    while (1) 
                    {
                        printf("> Enter your message (type -1 to end chat): ");
                        fflush(stdout);
                        fgets(u8Message, MESSAGE_SIZE, stdin);
                        u8Message[strcspn(u8Message, "\n")] = 0;  

                        if (strcmp(u8Message, "-1") == 0) 
                        {
                            break;                                                     /* Exit chat loop and prompt for Action again. */
                        }

                        snprintf(u8Buffer, sizeof(u8Buffer), "MSG %s %s", u8Recipient, u8Message);
                        send(s32Socket, u8Buffer, strlen(u8Buffer), 0);

                        pthread_mutex_lock(&FileMutex);                                    /* Log the message to the user's CSV file. */
                        fprintf(fpfile, "%s;%s;%s\n", u8Username, u8Recipient, u8Message); /* MAKING THE DELIMITER A SEMICOLON INSTEAD OF A COMMA. */
                        fflush(fpfile);  
                        pthread_mutex_unlock(&FileMutex);
                    }
                } 
                else 
                {
                    printf("Recipient is not active.\n");
                }
            } 
            else 
            {
                perror("recv failed");
                s32Running = 0;
            }
        }
    }

    return NULL;
}


int main() 
{
    S32 s32Sockfd;
    struct sockaddr_in stServerAddr;
    U8 u8Password[PASSWORD_SIZE];
    U8 u8Filename[USERNAME_SIZE + 10]; 
    pthread_t SendThread;
    pthread_t ReceiveThread;

    s32Sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (s32Sockfd < 0) 
    {
        perror("socket creation failed");
        return EXIT_FAILURE;
    }

    stServerAddr.sin_family = AF_INET;
    stServerAddr.sin_port = htons(8888);
    stServerAddr.sin_addr.s_addr = inet_addr("192.168.0.110");

    if (connect(s32Sockfd, (struct sockaddr *)&stServerAddr, sizeof(stServerAddr)) < 0) 
    {
        perror("connection failed");
        close(s32Sockfd);
        return EXIT_FAILURE;
    }

    printf("Enter your username: ");                                          /* User login or registration */
    fgets(u8Username, sizeof(u8Username), stdin);
    u8Username[strcspn(u8Username, "\n")] = 0; 
    send(s32Sockfd, u8Username, USERNAME_SIZE, 0);

    printf("Enter your password: ");
    fgets(u8Password, sizeof(u8Password), stdin);
    u8Password[strcspn(u8Password, "\n")] = 0; 
    send(s32Sockfd, u8Password, PASSWORD_SIZE, 0);

    U8 u8Buffer[BUFFER_SIZE];                                               /* Receive authentication response */
    recv(s32Sockfd, u8Buffer, sizeof(u8Buffer) - 1, 0);
    u8Buffer[strcspn(u8Buffer, "\n")] = 0;
    if (strcmp(u8Buffer, "AUTH_SUCCESS") == 0) 
    {
        printf("Logged in successfully.\n");
        snprintf(u8Filename, sizeof(u8Filename), "logs/%s.csv", u8Username);
        fpfile = fopen(u8Filename, "a+");
        if (!fpfile) 
        {
            perror("Can't open file");
            close(s32Sockfd);
            return EXIT_FAILURE;
        } 
        else 
        {
            printf("Log file opened in append mode.\n");
        }
    } 
    else if (strcmp(u8Buffer, "REG_SUCCESS") == 0) 
    {
        printf("Registration successful.\n");
        snprintf(u8Filename, sizeof(u8Filename), "logs/%s.csv", u8Username);
        fpfile = fopen(u8Filename, "w");
        if (!fpfile) 
        {
            perror("Error creating file");
            close(s32Sockfd);
            return EXIT_FAILURE;
        } 
        else 
        {
            printf("Log file created.\n");
        }
    } 
    else 
    {
        printf("Authentication failed: %s\n", u8Buffer);
        close(s32Sockfd);
        return EXIT_FAILURE;
    }

    pthread_create(&ReceiveThread, NULL, pReceiveHandler, (void *)&s32Sockfd);/* Create threads for sending and receiving messages */
    pthread_create(&SendThread, NULL, pSendHandler, (void *)&s32Sockfd);

    pthread_join(SendThread, NULL);
    pthread_join(ReceiveThread, NULL);

    close(s32Sockfd);
    fclose(fpfile);
    return EXIT_SUCCESS;
}
