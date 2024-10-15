//Server file
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 2048
#define USERNAME_SIZE 32
#define PASSWORD_SIZE 32

typedef struct {
    int sockfd;
    char username[USERNAME_SIZE];
} client_t;

client_t *clients[10];
int num_clients = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;  // Mutex for file operations

FILE *fp;

// Function to handle login and registration
int login(client_t *client) {
    char line[BUFFER_SIZE];
    char stored_username[USERNAME_SIZE];
    char stored_password[PASSWORD_SIZE];
    char received_password[PASSWORD_SIZE];

    FILE *usr = fopen("logs/Users.csv", "a+");
    if (usr == NULL) {
        perror("Unable to open Users.csv");
        return 0;
    }

    // Receive password from client
    recv(client->sockfd, received_password, PASSWORD_SIZE, 0);
    received_password[strcspn(received_password, "\n")] = 0; // Remove newline character

    // Check if the username already exists in Users.csv
    int user_found = 0;
    pthread_mutex_lock(&file_mutex);
    rewind(usr);  // Move file pointer to the beginning
    while (fgets(line, sizeof(line), usr)) {
        sscanf(line, "%[^,],%s", stored_username, stored_password);
        if (strcmp(client->username, stored_username) == 0) {
            user_found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&file_mutex);

    if (user_found) {
        // Verify the password
        if (strcmp(received_password, stored_password) == 0) {
            send(client->sockfd, "AUTH_SUCCESS\n", 13, 0);
            fclose(usr);
            return 1;
        } else {
            send(client->sockfd, "AUTH_FAILED\n", 12, 0);
            fclose(usr);
            return 0;
        }
    } else {
        // Register the new user
        pthread_mutex_lock(&file_mutex);
        fprintf(usr, "%s,%s\n", client->username, received_password);
        fflush(usr);  // Ensure it's written to the file immediately
        pthread_mutex_unlock(&file_mutex);

        send(client->sockfd, "REG_SUCCESS\n", 12, 0);
        fclose(usr);
        return 1;
    }
}

void send_active_clients(int sender_socket) {
    char buffer[BUFFER_SIZE];
    pthread_mutex_lock(&clients_mutex);
    if (num_clients <= 1) {
        snprintf(buffer, sizeof(buffer), "No active clients.\n");
    } else {
        snprintf(buffer, sizeof(buffer), "Active clients: ");
        for (int i = 0; i < num_clients; i++) {
            if (clients[i]->sockfd != sender_socket) {
                strcat(buffer, clients[i]->username);
                strcat(buffer, ", ");
            }
        }
        buffer[strlen(buffer) - 2] = '\0'; // Remove trailing comma and space
        strcat(buffer, "\n");
    }
    send(sender_socket, buffer, strlen(buffer), 0);
    pthread_mutex_unlock(&clients_mutex);
}

int is_client_active(const char *username) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < num_clients; ++i) {
        if (strcmp(clients[i]->username, username) == 0) {
            pthread_mutex_unlock(&clients_mutex);
            return 1;  // Client is active
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return 0;  // Client is not active
}

// Function to handle communication with a client
void *handle_client(void *arg) {
    client_t *client = (client_t *)arg;
    char buffer[BUFFER_SIZE];
    char recipient[USERNAME_SIZE], message[BUFFER_SIZE];
    int bytes_received;

    printf("Client %s connected.\n", client->username);

    while ((bytes_received = recv(client->sockfd, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_received] = '\0'; // Null-terminate the received string

        //send_active_clients(client->sockfd);

        // Parse message type and content
        char *message_type = strtok(buffer, " ");
        char *message_content = strtok(NULL, "");
        if (strncmp(message_type, "DISCONNECT",11) == 0) {
            printf("Client %s disconnected.\n", client->username);
            break;
        }
        else if (strcmp(message_type, "MSG") == 0) {
            // Extract recipient and message from the content
            sscanf(message_content, "%s %[^\n]", recipient, message);

            if (is_client_active(recipient)) {
                // Send the message to the recipient
                pthread_mutex_lock(&clients_mutex);
                for (int i = 0; i < num_clients; ++i) {
                    if (strcmp(clients[i]->username, recipient) == 0) {
                        char msg[BUFFER_SIZE];
                        snprintf(msg, sizeof(msg), "%s: %s", client->username, message);
                        send(clients[i]->sockfd, msg, strlen(msg), 0);
                        break;
                    }
                }
                pthread_mutex_unlock(&clients_mutex);

                // Log the message
                pthread_mutex_lock(&file_mutex);
                fprintf(fp, "%s,%s,%s\n", client->username, recipient, message);
                fflush(fp);
                pthread_mutex_unlock(&file_mutex);
                printf("Message stored in file: %s -> %s: %s\n", client->username, recipient, message);
            } 
            
        }
        else if (strcmp(message_type, "LIST_ACTIVE") == 0) { // Handle LIST_ACTIVE request
            send_active_clients(client->sockfd);  // Send active clients list to requesting client
        }
         else if (strncmp(message_type, "CHECK", 5) == 0) {
            // Handle recipient status check
            char *recipient = strtok(message_content, " ");
            if (is_client_active(recipient)) {
                send(client->sockfd, "ACTIVE\n", 7, 0);
            } else {
                send(client->sockfd, "INACTIVE\n", 9, 0);
            }
        }
         else {
            printf("Unknown message type: %s\n", message_type);
        }
    }

    close(client->sockfd);

    // Remove client from the list
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < num_clients; ++i) {
        if (clients[i] == client) {
            for (int j = i; j < num_clients - 1; ++j) {
                clients[j] = clients[j + 1];
            }
            --num_clients;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    free(client);

    return NULL;
}

int main() {
    int server_sockfd, client_sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    pthread_t thread;
    int opt = 1; // to reuse port

    // Open the file for appending
    fp = fopen("logs/Serv.csv", "a+");
    if (!fp) {
        perror("Can't open Serv.csv");
        return EXIT_FAILURE;
    } else {
        printf("Found Serv.csv\n");
    }

    server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sockfd < 0) {
        perror("socket creation failed");
        return EXIT_FAILURE;
    }

    // Allow server to reuse the address and port
    if (setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8888);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(server_sockfd);
        return EXIT_FAILURE;
    }

    if (listen(server_sockfd, 10) < 0) {
        perror("listen failed");
        close(server_sockfd);
        return EXIT_FAILURE;
    }

    printf("Server listening on port 8888...\n");

    while (1) {
        client_t *client = malloc(sizeof(client_t));
        if (!client) {
            perror("malloc failed");
            continue;
        }

        client->sockfd = accept(server_sockfd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client->sockfd < 0) {
            perror("accept failed");
            free(client);
            continue;
        }
    

        // Receive and store the client's username
        recv(client->sockfd, client->username, USERNAME_SIZE, 0);
        client->username[strcspn(client->username, "\n")] = 0; // Remove newline 

        // Handle user login or registration
        if (!login(client)) {
            close(client->sockfd);
            free(client);
            continue;
        }

        pthread_mutex_lock(&clients_mutex);
        if (num_clients < sizeof(clients) / sizeof(clients[0])) {
            clients[num_clients++] = client; //Add client to client_t
            pthread_create(&thread, NULL, handle_client, client);
            pthread_detach(thread);
        } else {
            fprintf(stderr, "Too many clients connected.\n");
            close(client->sockfd);
            free(client);
        }
        pthread_mutex_unlock(&clients_mutex);
    }

    // Close the file at the end of the program (if it ever exits)
    fclose(fp);
    close(server_sockfd);
    return EXIT_SUCCESS;
}
