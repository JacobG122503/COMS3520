#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#define MAX_BOTS 10
#define MAX_MSG_LEN 256

int fd[MAX_BOTS][2]; // Pipes for communication

void chatbot(int myId, char *myName, int botCount, char *botNames[]) {
    close(fd[myId][1]); // Close write end of pipe (only reading)

    while (1) {
        char msgBuf[MAX_MSG_LEN];

        // Read message from parent process
        int n = read(fd[myId][0], msgBuf, MAX_MSG_LEN);
        if (n <= 0) {
            printf("Chatbot %s: Read error. Exiting.\n", myName);
            exit(1);
        }
        msgBuf[n] = '\0';

        // Handle ":CHANGE" command
        if (strcmp(msgBuf, ":CHANGE") == 0 || strcmp(msgBuf, ":change") == 0) {
            write(fd[myId][1], ":CHANGE", MAX_MSG_LEN);
            continue;
        }

        // Respond to user
        printf("Hello, this is chatbot %s. Please type:\n", myName);
        char userMsg[MAX_MSG_LEN];
        fgets(userMsg, MAX_MSG_LEN, stdin);
        userMsg[strcspn(userMsg, "\n")] = 0; // Remove newline

        printf("I heard you said: %s\n", userMsg);

        // If user wants to switch, notify parent
        if (strcmp(userMsg, ":CHANGE") == 0 || strcmp(userMsg, ":change") == 0) {
            write(fd[myId][1], ":CHANGE", MAX_MSG_LEN);
            break;
        }

        // Send message back to parent
        write(fd[myId][1], userMsg, MAX_MSG_LEN);
    }

    close(fd[myId][0]);
    exit(0);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s bot1 bot2 ... botN\n", argv[0]);
        exit(1);
    }

    int botCount = argc - 1;
    char *botNames[MAX_BOTS];

    for (int i = 0; i < botCount; i++) {
        botNames[i] = argv[i + 1];
        pipe(fd[i]); // Create a pipe for each bot

        if (fork() == 0) {
            chatbot(i, botNames[i], botCount, botNames);
        }
        close(fd[i][0]); // Parent closes read end of pipes
    }

    int currentBot = 0; // Start with the first bot

    while (1) {
        printf("Hello, this is chatbot %s. Please type:\n", botNames[currentBot]);
        char userMsg[MAX_MSG_LEN];
        fgets(userMsg, MAX_MSG_LEN, stdin);
        userMsg[strcspn(userMsg, "\n")] = 0; // Remove newline

        // Send message to current bot
        write(fd[currentBot][1], userMsg, MAX_MSG_LEN);

        // Read response from bot
        char recvMsg[MAX_MSG_LEN];
        int n = read(fd[currentBot][0], recvMsg, MAX_MSG_LEN);
        if (n <= 0) {
            printf("Error: Chatbot %s failed to respond. Exiting.\n", botNames[currentBot]);
            break;
        }
        recvMsg[n] = '\0';

        if (strcmp(recvMsg, ":CHANGE") == 0) {
            while (1) {
                printf("Enter the name of the bot to switch to: ");
                fgets(userMsg, MAX_MSG_LEN, stdin);
                userMsg[strcspn(userMsg, "\n")] = 0; // Remove newline

                int found = -1;
                for (int i = 0; i < botCount; i++) {
                    if (strcmp(userMsg, botNames[i]) == 0) {
                        found = i;
                        break;
                    }
                }

                if (found == -1) {
                    printf("Invalid bot name. Try again.\n");
                } else {
                    currentBot = found;
                    break;
                }
            }
        }
    }

    // Close pipes and wait for children to exit
    for (int i = 0; i < botCount; i++) {
        close(fd[i][1]);
    }

    return 0;
}
