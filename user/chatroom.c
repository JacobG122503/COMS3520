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

    // Loop
    while (1) {
        // Get msg from the previous chatbot
        char recvMsg[MAX_MSG_LEN];
        int n = read(fd[myId - 1][0], recvMsg, MAX_MSG_LEN);
        if (n <= 0) {
            panic("read failed");
        }
        recvMsg[n] = '\0';

        // If the received message is :CHANGE or :change, switch to the next bot
        if (strcmp(recvMsg, ":CHANGE") == 0 || strcmp(recvMsg, ":change") == 0) {
            write(fd[myId][1], recvMsg, MAX_MSG_LEN);
            continue;
        }

        // Chat loop for continuous chatting until :CHANGE or :change
        while (1) {
            printf("Hello, this is chatbot %s. Please type:\n", myName);

            // Get a string from std input and save it to msgBuf
            char msgBuf[MAX_MSG_LEN];
            gets1(msgBuf);

            printf("I heard you said: %s\n", msgBuf);

            // If user inputs :CHANGE or :change, pass the msg down and switch to the next bot
            if (strcmp(msgBuf, ":CHANGE") == 0 || strcmp(msgBuf, ":change") == 0) {
                write(fd[myId][1], msgBuf, MAX_MSG_LEN);
                break;
            }

            // Pass the msg to the next bot in the ring
            write(fd[myId][1], msgBuf, MAX_MSG_LEN);
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
    pipe1(fd[0]);
    for (int i = 1; i < argc; i++) {
        pipe1(fd[i]);
        if (fork1() == 0) {
            chatbot(i, argv[i], argc - 1, botNames);
        }
    }

    // Close the fds not used any longer
    close(fd[0][0]);
    close(fd[argc - 1][1]);
    for (int i = 1; i < argc - 1; i++) {
        close(fd[i][0]);
        close(fd[i][1]);
    }

    // Send the START message to the first chatbot
    write(fd[0][1], ":START", 6);

    // Main chat loop for the user to interact with chatbots
    int currentBot = 0;
    while (1) {
        char recvMsg[MAX_MSG_LEN];
        int n = read(fd[argc - 1][0], recvMsg, MAX_MSG_LEN);
        if (n <= 0) {
            panic("read failed");
        }
        recvMsg[n] = '\0';

        // If the message is :EXIT or :exit, exit the program
        if (strcmp(recvMsg, ":EXIT") == 0 || strcmp(recvMsg, ":exit") == 0) {
            break;
        }

        // Display the message from the current bot
        printf("%s: %s\n", botNames[currentBot], recvMsg);

        // Ask the user if they want to change the bot
        char msgBuf[MAX_MSG_LEN];
        printf("Type :change or :CHANGE to switch bots, or type a message to continue chatting: ");
        gets1(msgBuf);

        // Handle bot change request
        if (strcmp(msgBuf, ":CHANGE") == 0 || strcmp(msgBuf, ":change") == 0) {
            char newBot[MAX_MSG_LEN];
            printf("Which bot would you like to switch to? ");
            gets1(newBot);

            // Validate the new bot name
            if (isValidBotName(newBot, botNames, argc - 1)) {
                currentBot = -1;
                for (int i = 0; i < argc - 1; i++) {
                    if (strcmp(newBot, botNames[i]) == 0) {
                        currentBot = i;
                        break;
                    }
                }
                // Inform the new bot about the switch
                write(fd[currentBot][1], ":CHANGE", MAX_MSG_LEN);
            } else {
                printf("Invalid bot name. Please try again.\n");
            }
        } else {
            // Pass the message to the first bot
            write(fd[0][1], msgBuf, MAX_MSG_LEN);
        }
    }

    // Wait for all child processes to exit
    for (int i = 1; i < argc; i++) {
        wait(0);
    }
    printf("Now the chatroom closes. Bye bye!\n");
    exit(0);
}