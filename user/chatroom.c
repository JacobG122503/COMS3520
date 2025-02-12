#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define MAX_MSG_LEN 512
#define MAX_NUM_CHATBOT 10

int fd[MAX_NUM_CHATBOT + 1][2];
char *botNames[MAX_NUM_CHATBOT]; // Array to store chatbot names
int numBots; // Number of chatbots

// Handle exception
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
        panic("Fail to create a pipe.");
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

// Check if a chatbot name is valid
int isValidBotName(char *name) {
    for (int i = 0; i < numBots; i++) {
        if (strcmp(botNames[i], name) == 0) {
            return 1; // Valid name
        }
    }
    return 0; // Invalid name
}

// Script for chatbot (child process)
void chatbot(int myId, char *myName) {
    // Close un-used pipe descriptors
    for (int i = 0; i < myId - 1; i++) {
        close(fd[i][0]);
        close(fd[i][1]);
    }
    close(fd[myId - 1][1]);
    close(fd[myId][0]);

    char currentBot[MAX_MSG_LEN];
    strcpy(currentBot, myName); // Initially chatting with the current bot

    while (1) {
        // To get msg from the previous chatbot
        char recvMsg[MAX_MSG_LEN];
        int n = read(fd[myId - 1][0], recvMsg, MAX_MSG_LEN);
        if (n <= 0) {
            panic("read failed");
        }
        recvMsg[n] = '\0';

        // If the received message is :EXIT or :exit, exit
        if (strcmp(recvMsg, ":EXIT") == 0 || strcmp(recvMsg, ":exit") == 0) {
            write(fd[myId][1], recvMsg, MAX_MSG_LEN);
            exit(0);
        }

        // Continue chatting with the current bot
        while (1) {
            printf("Hello, this is chatbot %s. Please type:\n", currentBot);

            // Get a string from std input and save it to msgBuf
            char msgBuf[MAX_MSG_LEN];
            gets1(msgBuf);

            // If user inputs :CHANGE or :change, prompt for the next bot
            if (strcmp(msgBuf, ":CHANGE") == 0 || strcmp(msgBuf, ":change") == 0) {
                printf("Enter the name of the bot you want to chat with next:\n");
                char nextBot[MAX_MSG_LEN];
                gets1(nextBot);

                // Validate the bot name
                if (!isValidBotName(nextBot)) {
                    printf("Invalid bot name. Please try again.\n");
                    continue;
                }

                // If the user types the current bot name, continue chatting
                if (strcmp(nextBot, currentBot) == 0) {
                    printf("You are already chatting with %s. Continue typing messages.\n", currentBot);
                    continue;
                }

                // Change to the new bot
                printf("Switching to chatbot %s...\n", nextBot);
                strcpy(currentBot, nextBot);

                // Pass the bot name to the parent process to switch
                write(fd[myId][1], nextBot, MAX_MSG_LEN);
                break;
            }

            // If user inputs EXIT/exit, exit
            if (strcmp(msgBuf, "EXIT") == 0 || strcmp(msgBuf, "exit") == 0) {
                write(fd[myId][1], msgBuf, MAX_MSG_LEN);
                exit(0);
            }

            // Pass the msg to the next one on the ring
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

    // Store chatbot names
    numBots = argc - 1;
    for (int i = 0; i < numBots; i++) {
        botNames[i] = argv[i + 1];
    }

    pipe1(fd[0]); // Create the first pipe #0
    for (int i = 1; i < argc; i++) {
        pipe1(fd[i]); // Create one new pipe for each chatbot
        // To create child proc #i (emulating chatbot #i)
        if (fork1() == 0) {
            chatbot(i, argv[i]);
        }
    }

    // Close the fds not used any longer
    close(fd[0][0]);
    close(fd[argc - 1][1]);
    for (int i = 1; i < argc - 1; i++) {
        close(fd[i][0]);
        close(fd[i][1]);
    }

    // Send the START msg to the first chatbot
    write(fd[0][1], ":START", 6);

    // Loop: when receive a token from predecessor, pass it to successor
    while (1) {
        char recvMsg[MAX_MSG_LEN];
        int n = read(fd[argc - 1][0], recvMsg, MAX_MSG_LEN);
        if (n <= 0) {
            panic("read failed");
        }
        recvMsg[n] = '\0';

        // If the message is :EXIT or :exit, break the loop
        if (strcmp(recvMsg, ":EXIT") == 0 || strcmp(recvMsg, ":exit") == 0) {
            break;
        }

        // Pass the message to the first chatbot
        write(fd[0][1], recvMsg, MAX_MSG_LEN);
    }

    // Exit after all children exit
    for (int i = 1; i < argc; i++) {
        wait(0);
    }
    printf("Now the chatroom closes. Bye bye!\n");
    exit(0);
}