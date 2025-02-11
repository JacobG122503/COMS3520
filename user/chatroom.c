#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define MAX_MSG_LEN 512
#define MAX_NUM_CHATBOT 10

int fd[MAX_NUM_CHATBOT + 1][2];

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

// Script for chatbot (child process)
void chatbot(int myId, char *myName) {
    // Close un-used pipe descriptors
    for (int i = 0; i < myId - 1; i++) {
        close(fd[i][0]);
        close(fd[i][1]);
    }
    close(fd[myId - 1][1]);
    close(fd[myId][0]);

    // Loop
    while (1) {
        // To get msg from the previous chatbot
        char recvMsg[MAX_MSG_LEN];
        int n = read(fd[myId - 1][0], recvMsg, MAX_MSG_LEN);
        if (n <= 0) {
            panic("read failed");
        }
        recvMsg[n] = '\0';

        if (strcmp(recvMsg, ":CHANGE") != 0 && strcmp(recvMsg, ":change") != 0) {
            // Continue chatting with the current bot
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

                // Pass the msg to the next one on the ring
                write(fd[myId][1], msgBuf, MAX_MSG_LEN);
            }
        } else {
            // If receives :CHANGE or :change, pass the msg down and switch to the next bot
            write(fd[myId][1], recvMsg, MAX_MSG_LEN);
        }
    }
}

// Script for parent process
int main(int argc, char *argv[]) {
    if (argc < 3 || argc > MAX_NUM_CHATBOT + 1) {
        printf("Usage: %s <list of names for up to %d chatbots>\n", argv[0], MAX_NUM_CHATBOT);
        exit(1);
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

        write(fd[0][1], recvMsg, MAX_MSG_LEN);

        if (strcmp(recvMsg, ":EXIT") == 0 || strcmp(recvMsg, ":exit") == 0) {
            break; // Break from the loop if the msg is EXIT
        }
    }

    // Exit after all children exit
    for (int i = 1; i < argc; i++) {
        wait(0);
    }
    printf("Now the chatroom closes. Bye bye!\n");
    exit(0);
}