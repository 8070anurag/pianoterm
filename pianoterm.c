#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <wait.h>

#define OUT STDOUT_FILENO
#define IN STDIN_FILENO
#define ASEQ_HEADER_LEN 78
#define ASEQ_LOG_LEN 58
#define PORT_DIGITS 4
#define CONF_PATH ".config/pianoterm/config"

const uint test_note = 21;
const char *N_OFF = "Note off";
const char *N_ON = "Note on";

// first argument - midi port (24)
// TODO: if no argument is passed try to find the port, using aconnect -i
// TODO: add support for multiple commands (&&) and commands not in path $HOME/my_script.sh
// TODO: config to run command if (note on vs note off) (press/release)
// TODO: allow config to use standard notation and convert to key code (C#1 = "echo hello")
// TODO: allow usage just by passing in arguments, without config file
//
// alsactl (aseqdump) version 1.2.15.2
//

struct shell_command {
    char *path;
    uint argc;
    char **argv;
};
typedef struct shell_command ShellCommand;

struct user_command {
    uint note;
    char *str;
};
typedef struct user_command UserCommand;

struct app_data {
    int channel[2];
    char buffer[124];
    uint port;
    bool act_on_release;
    UserCommand *commands;
    uint n_commands;
};
typedef struct app_data Data;

int readLine(Data *app, int len);
int getNote(Data app);
void runCommand(Data app, uint for_note);
ShellCommand *parseCommand(const char* src);
void freeCommand(ShellCommand *cmd);
void loadConfig(Data *app);

int main(int argc, char**argv) {
    Data app;
    app.port = 24;
    app.act_on_release = false;
    loadConfig(&app);

    if(argc == 2){
        // if(strlen(argv[1]))
    }

    if(argc <= 1){
        // try to find port using aconnect -i
    }

    if(pipe(app.channel) == -1){
        fprintf(stderr, "pipe error\n");
        return 1;
    };

    int pid = fork();
    if(pid == -1) {
        fprintf(stderr, "fork error\n");
        return 1;
    }

    if(pid == 0){
        close(app.channel[IN]);
        dup2(app.channel[OUT], OUT);
        dup2(app.channel[OUT], STDERR_FILENO);

        char port_str[PORT_DIGITS];
        sprintf(port_str, "%u", app.port);

        execlp("aseqdump", "aseqdump", "-p", port_str, 0);
        printf("_exit\n");
    } else {
        printf("Listening for MIDI input on port %u\n", app.port);
        bool blocked = true;
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(app.channel[IN], &fds);

        // ignore any lingering messages
        struct timeval timeout = {.tv_sec = 0, .tv_usec = 100000};
        int leftover_msgs = select(app.channel[IN] + 1, &fds, 0, 0, &timeout);
        while(leftover_msgs){
            readLine(&app, ASEQ_HEADER_LEN);
            leftover_msgs = select(app.channel[IN] + 1, &fds, 0, 0, &timeout);
            blocked = false;
        }

        while(1) {
            if(blocked) { 
                readLine(&app, ASEQ_HEADER_LEN);
                blocked = false;
            }

            if(readLine(&app, ASEQ_LOG_LEN) == -1)
                break;

            int note = getNote(app);
            if(note) runCommand(app, note);

        }

        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
    }

    return 0;
}

void loadConfig(Data *app){
    const char* home = getenv("HOME");
    if(!home) {
        fprintf(stderr, "$HOME variable not set\n");
        return;
    }
    char path[strlen(home) + strlen(CONF_PATH)];
    sprintf(path, "%s/%s", home, CONF_PATH);

    FILE *f = fopen(path, "r");
    if(!f) {
        fprintf(stderr, "Config file not found at %s\n", path);
        return;
    }

    fclose(f);
}

void runCommand(Data app, uint for_note){
    // 21 = notify-send 'hello'
    const char *test_cmd_0 = "notify-send hello";
    // 21 = notify-send hello
    const char *test_cmd = "notify-send 'hello hello'";
    // 21 = notify-send hello goodbye
    const char *test_cmd_2 = "  notify-send   hello goodbye ";
    // 21 = notify-send 'command 1' && notify-send 'command 2'!
    const char *test_cmd_3 = "notify-send 'command 1!' && notify-send 'command 2!'";
    const char *test_cmd_4 = "/home/guts/.config/user/scripts/calendar.sh";

    const char *str = test_cmd_2;

    // for(int i = 0; i < app.n_commands; i++)
    // {
        ShellCommand *c = parseCommand(str);
        if(c){
            int pid = fork();
            if(pid == 0)
                execvp(c->path, c->argv);
            else
                waitpid(pid, 0, 0);

            freeCommand(c);
        };
    // }

    // if(for_note == test_note)
    // {
    //     const char *test[] = {"playerctl", "play-pause", 0};
    //     //"playerctl play-pause"
    //     if(fork() == 0)
    //         execlp("playerctl", "playerctl", "play-pause", 0);
    // }
}

ShellCommand* parseCommand(const char* src){
    ShellCommand *c = (ShellCommand*) malloc(sizeof(ShellCommand));
    uint len = strlen(src);
    bool in_quote = false;
    uint pos = 0;

    while(src[pos] == ' ') pos++;
    uint word_start = pos;
mainloop: while(pos <= len){
       if(src[pos] == '\'') in_quote = !in_quote;
       if(in_quote) { pos++; continue; };

       if(src[pos] == ' ' || pos == len){
           uint word_size = pos - word_start + 1;
           char *new_word = (char*)malloc(sizeof(char)*word_size);
           snprintf(new_word, word_size, "%s", &src[word_start]);

           char **tmp = (char**)malloc((c->argc + 1)* sizeof(char*));
           for(int i = 0; i < c->argc; i++)
               tmp[i] = c->argv[i];

           if(c->argv)
               free(c->argv);

           c->argv = tmp;
           c->argv[c->argc] = new_word;
           c->argc++;

           while(src[pos] == ' ' || src[pos] == 0){
               pos++; if(pos >= len) break mainloop;
           }
           word_start = pos;
           continue;
       }

       pos++;
    }

    if(in_quote){
        fprintf(stderr, "command syntax error: unclosed quote\n");
        freeCommand(c);
        return 0;
    }

    // prepare for execvp
    char **tmp = (char**)malloc((c->argc + 1)* sizeof(char*));
    for(int i = 0; i < c->argc; i++)
        tmp[i] = c->argv[i];

    if(c->argv)
        free(c->argv);

    c->argv = tmp;
    c->argv[c->argc] = 0;
    c->path = c->argv[0];

    return c;
}

void freeCommand(ShellCommand *cmd) {
    if(!cmd) return;
    for(int i = 0; i < cmd->argc; i++)
        free(cmd->argv[i]);

    free(cmd);
}

// read line from aseqdump and update buffer
int readLine(Data *app, int len){
    int bytes = read(app->channel[IN], app->buffer, len);

    if(strncmp(app->buffer, "_exit", 5) == 0){
        fprintf(stderr, "could not find/start aseqdump\n");
        return -1;
    }

    if(strstr(app->buffer, "Cannot connect") != 0){
        fprintf(stderr, "Could not connect to port %d\n", app->port);
        return -1;
    }

    if(strstr(app->buffer, "Port unsubscribed") != 0){
        fprintf(stderr, "Lost connection to port %d\n", app->port);
        return -1;
    }

    if(bytes == -1){
        fprintf(stderr, "read error\n");
        return -1;
    }
    app->buffer[bytes] = 0;

    return 0;
}

// return the code of the key pressed (0 if no note)
int getNote(Data app){
    uint note = 0, port = 0;
    int n;

    n = sscanf(app.buffer, "%3u:", &port);
    if(n != 1 || port != app.port) return 0;

    const char *trigger = app.act_on_release ? N_OFF : N_ON;
    if(strstr(app.buffer, trigger) == NULL) return 0;

    char *note_pos = strstr(app.buffer, "note ");
    if(!note_pos) return 0;

    n = sscanf(note_pos, "note %3u,", &note);
    if(n != 1) return 0;

    return note;
}
