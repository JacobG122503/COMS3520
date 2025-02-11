#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define MAX_MSG_LEN 512
#define MAX_NUM_CHATBOT 10

int fd[MAX_NUM_CHATBOT + 1][2];  // Pipes for communication between chatbots

// Handle exceptions
void panic(char *s) {
    fprintf(2, "%s\n", s);
    exit(1);
}

// Create a new process
int fork1(void) {
    int pid = fork();
    if (pid == -1)
        panic("fork");
    return pid;
}

// Create a pipe
void pipe1(int fd[2]) {
    if (pipe(fd) < 0) {
        panic("Failed to create a pipe.");
    }
}

// Get a string from std input and save it to msgBuf
void gets1(char msgBuf[MAX_MSG_LEN]) {
    gets(msgBuf, MAX_MSG_LEN);
    int len = strlen(msgBuf);
    if (len > 0 && msgBuf[len - 1] == '\n') {
        msgBuf[len - 1] = '\0';
    }
}

// Check if the given bot name is valid
int isValidBotName(char *name, char *botNames[], int numBots) {
    for (int i = 0; i < numBots; i++) {
        if (strcmp(name, botNames[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

// Script for chatbot (child process)
void chatbot(int myId, char *myName, int numBots, char *botNames[]) {
    // Close un-used pipe descriptors
    for (int i = 0; i < myId - 1; i++) {
        close(fd[i][0]);
        close(fd[i][1]);
    }
    close(fd[myId - 1][1]);
    close(fd[myId][0]);

    // Chat loop
    while (1) {
        char msgBuf[MAX_MSG_LEN];
        
        // Get msg from the previous chatbot (or parent)
        int n = read(fd[myId - 1][0], msgBuf, MAX_MSG_LEN);
        if (n <= 0) {
            panic("read failed");
        }
        msgBuf[n] = '\0';

        // Check if message is :CHANGE
        if (strcmp(msgBuf, ":CHANGE") == 0 || strcmp(msgBuf, ":change") == 0) {
            write(fd[myId][1], ":CHANGE", MAX_MSG_LEN);
            continue;
        }

        // Chat loop: ask for messages and reply until change
        while (1) {
            printf("Hello, this is chatbot %s. Please type:\n", myName);
            gets1(msgBuf);
            printf("I heard you said: %s\n", msgBuf);

            if (strcmp(msgBuf, ":CHANGE") == 0 || strcmp(msgBuf, ":change") == 0) {
                write(fd[myId][1], ":CHANGE", MAX_MSG_LEN);
                break;  // Break the loop and wait for parent to switch bots
            }

            write(fd[myId][1], msgBuf, MAX_MSG_LEN);  // Send message to next bot
        }
    }
}

// Script for parent process
int main(int argc, char *argv[]) {
    if (argc < 3 || argc > MAX_NUM_CHATBOT + 1) {
        printf("Usage: %s <list of names for up to %d chatbots>\n", argv[0], MAX_NUM_CHATBOT);
        exit(1);
    }

    // Store bot names
    char *botNames[MAX_NUM_CHATBOT];
    for (int i = 0; i < argc - 1; i++) {
        botNames[i] = argv[i + 1];
    }

    // Create pipes and fork child processes for each chatbot
    for (int i = 0; i < argc - 1; i++) {
        pipe1(fd[i]);
        if (fork1() == 0) {
            chatbot(i, argv[i + 1], argc - 1, botNames);
        }
    }

    // Main process will interact with the user
    int currentBot = 0;  // Start with the first bot
    while (1) {
        char msgBuf[MAX_MSG_LEN];
        char recvMsg[MAX_MSG_LEN];
        
        // Read message from the current bot
        int n = read(fd[currentBot][0], recvMsg, MAX_MSG_LEN);
        if (n <= 0) {
            panic("read failed");
        }
        recvMsg[n] = '\0';

        // Display message from the current bot
        printf("%s: %s\n", botNames[currentBot], recvMsg);

        // Ask user for input
        printf("Type :change or :CHANGE to switch bots, or type a message to continue chatting: ");
        gets1(msgBuf);

        // If user wants to change bot
        if (strcmp(msgBuf, ":CHANGE") == 0 || strcmp(msgBuf, ":change") == 0) {
            char newBot[MAX_MSG_LEN];
            printf("Which bot would you like to switch to? ");
            gets1(newBot);

            if (isValidBotName(newBot, botNames, argc - 1)) {
                // Find the bot index
                for (int i = 0; i < argc - 1; i++) {
                    if (strcmp(newBot, botNames[i]) == 0) {
                        currentBot = i;
                        break;
                    }
                }
                // Notify the chatbot to switch
                write(fd[currentBot][1], ":CHANGE", MAX_MSG_LEN);
            } else {
                printf("Invalid bot name. Please try again.\n");
            }
        } else {
            // Forward the message to the current bot
            write(fd[currentBot][1], msgBuf, MAX_MSG_LEN);
        }
    }

    // Wait for all child processes to exit
    for (int i = 0; i < argc - 1; i++) {
        wait(0);
    }

    printf("Now the chatroom closes. Bye bye!\n");
    exit(0);
}