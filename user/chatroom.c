//jacobgar homework 1

/*
So my program doesn't entirely work but I want to explain the process for partial credit
I think my approach is quite simple in that it just has a loop until :change is inputted
Then the user is asked what bot to switch to and it does. If the user inputs a wrong bot name 
it correctly tells it to retype and try again. 

When the user does change bots, 8 out of 10 times it does change to the chosen bot.
As the user goes on, the changes start to mess up. Maybe it has something to do with the
memory limitations of the OS? I am not sure. I've spent a long time trying to fix it to no avail. 
*/

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define MAX_MSG_LEN 512
#define MAX_NUM_CHATBOT 10

int fd[MAX_NUM_CHATBOT + 1][2];
char *botNames[MAX_NUM_CHATBOT]; 
int numBots; 

void panic(char *s) {
    fprintf(2, "%s\n", s);
    exit(1);
}

int fork1(void) {
    int pid = fork();
    if (pid == -1)
        panic("fork");
    return pid;
}

void pipe1(int fd[2]) {
    if (pipe(fd) < 0) {
        panic("Fail to create a pipe.");
    }
}

void gets1(char msgBuf[MAX_MSG_LEN]) {
    gets(msgBuf, MAX_MSG_LEN);
    int len = strlen(msgBuf);
    if (len > 0 && msgBuf[len - 1] == '\n') {
        msgBuf[len - 1] = '\0';
    }
}

//Simple function to test if bot name is right
int isValidBotName(char *name) {
    for (int i = 0; i < numBots; i++) {
        if (strcmp(botNames[i], name) == 0) {
            return 1; 
        }
    }
    return 0; 
}

void chatbot(int myId, char *myName) {
    //Close old pipes
    for (int i = 0; i < myId - 1; i++) {
        close(fd[i][0]);
        close(fd[i][1]);
    }
    close(fd[myId - 1][1]);
    close(fd[myId][0]);

    char currentBot[MAX_MSG_LEN];
    strcpy(currentBot, myName);

    while (1) {
        char recvMsg[MAX_MSG_LEN];
        int n = read(fd[myId - 1][0], recvMsg, MAX_MSG_LEN);
        if (n <= 0) {
            panic("read failed");
        }
        recvMsg[n] = '\0';

        if (strcmp(recvMsg, ":EXIT") == 0 || strcmp(recvMsg, ":exit") == 0) {
            write(fd[myId][1], recvMsg, MAX_MSG_LEN);
            exit(0);
        }

        while (1) {
            printf("Hello, this is chatbot %s. Please type:\n", currentBot);

            char msgBuf[MAX_MSG_LEN];
            gets1(msgBuf);

            //If user types change then ask what bot and change
            if (strcmp(msgBuf, ":CHANGE") == 0 || strcmp(msgBuf, ":change") == 0) {
                printf("Enter the name of the bot you want to chat with next:\n");
                char nextBot[MAX_MSG_LEN];
                gets1(nextBot);

                //Make sure bot name is a real name
                if (!isValidBotName(nextBot)) {
                    printf("Invalid bot name. Please try again.\n");
                    continue;
                }

                //If already changed to that bot, stay
                if (strcmp(nextBot, currentBot) == 0) {
                    printf("You are already chatting with %s. Continue typing messages.\n", currentBot);
                    continue;
                }

                //Change to the bot
                printf("Switching to chatbot %s...\n", nextBot);
                strcpy(currentBot, nextBot);

                //Pass the bot name to the parent pipe
                write(fd[myId][1], nextBot, MAX_MSG_LEN);
                break;
            }

            if (strcmp(msgBuf, "EXIT") == 0 || strcmp(msgBuf, "exit") == 0) {
                write(fd[myId][1], msgBuf, MAX_MSG_LEN);
                exit(0);
            }

            write(fd[myId][1], msgBuf, MAX_MSG_LEN);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3 || argc > MAX_NUM_CHATBOT + 1) {
        printf("Usage: %s <list of names for up to %d chatbots>\n", argv[0], MAX_NUM_CHATBOT);
        exit(1);
    }

    numBots = argc - 1;
    for (int i = 0; i < numBots; i++) {
        botNames[i] = argv[i + 1];
    }

    pipe1(fd[0]); 
    for (int i = 1; i < argc; i++) {
        pipe1(fd[i]);
        if (fork1() == 0) {
            chatbot(i, argv[i]);
        }
    }

    close(fd[0][0]);
    close(fd[argc - 1][1]);
    for (int i = 1; i < argc - 1; i++) {
        close(fd[i][0]);
        close(fd[i][1]);
    }

    write(fd[0][1], ":START", 6);

    while (1) {
        char recvMsg[MAX_MSG_LEN];
        int n = read(fd[argc - 1][0], recvMsg, MAX_MSG_LEN);
        if (n <= 0) {
            panic("read failed");
        }
        recvMsg[n] = '\0';

        if (strcmp(recvMsg, ":EXIT") == 0 || strcmp(recvMsg, ":exit") == 0) {
            break;
        }

        write(fd[0][1], recvMsg, MAX_MSG_LEN);
    }

    for (int i = 1; i < argc; i++) {
        wait(0);
    }
    printf("Now the chatroom closes. Bye bye!\n");
    exit(0);
}