#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <wait.h>

#define UINT16_MAX 65535
#define _out STDOUT_FILENO
#define _in STDIN_FILENO
#define _err STDERR_FILENO
#define _aseq_header_len 78
#define _aseq_log_len 58
#define _port_digits 4
#define _conf_path "/.config/pianoterm/config"
#define _wlen(str) str, strlen(str)
#define _wsize(str) str, sizeof(str)
#define _wstart(c)   while(*(c) == ' ') c++;
#define _wend(c)     while(!(*c == ' ' || *c == 0 || *c== '#')) c++;
#define _cmdend(c)   while(!(*c == 0 || *c== '#')) c++;

const char *N_OFF = "Note off";
const char *N_ON = "Note on";

// first argument - midi port (24)
// TODO: support for repeating command while holding down key
//
// TODO: chord on_press support (store multiple notes in a cmd)
// define a limit (10 for 10 fingers)
// change user_cmd to store an array of notes (can be static, since only 10)
// read the next 10 lines, instead of one-by-one, check if 'note on' matches notes in cmd (for n lines in cmd)
// also figure out syntax for config file
//
// TODO: if no argument is passed try to find the port, using aconnect -i
// TODO: allow config to use standard notation and convert to key code (C#1 = "echo hello")
// TODO: option to reload config file?
// TODO: check if instance already running on the same port
// TODO: reconnect automatically on disconnect/connect
//
// alsactl (aseqdump) version 1.2.15.2
//

enum trigger {
    on_press,
    on_release,
    on_hold,
};
typedef enum trigger Trigger;

struct shell_command {
    char *path;
    uint argc;
    char **argv;
};
typedef struct shell_command ShellCommand;

struct user_command {
    uint note; // uint note[10]; uint n_notes;
    Trigger trigger;
    char *str;
};
typedef struct user_command UserCommand;

struct midi_event {
    uint note;
    Trigger trigger;
};
typedef struct midi_event MidiEvent;

struct app_data {
    int channel[2];
    char buffer[124];
    uint port;
    Trigger trigger_state;
    UserCommand *commands;
    uint n_commands;
};
typedef struct app_data Data;

int readLine(Data *app, int len);
MidiEvent getEvent(Data app);
void runCommand(Data app, MidiEvent e);
ShellCommand *parseCommand(char* src);
void freeCommand(ShellCommand *cmd);
void loadConfig(Data *app);
char* seekToNext(char* cur, char target);

int main(int argc, char**argv) {
    Data app;
    app.trigger_state = on_press;

    if(argc >= 2){
        long int port = strtol(argv[1],NULL,10);
        if(port <= 0 || port >= UINT16_MAX){
            write(_out, _wlen("Invalid port\n"));
            return 1;
        }
        app.port = (uint)port;
    }

    if(argc < 2){
        // TODO: try to find port automatically using aconnect -i
        write(_out, _wlen("Usage: "));
        write(_out, _wlen(argv[0]));
        write(_out, _wlen(" <port>\n"));
        return 1;
    }

    char port_str[_port_digits];
    snprintf(port_str, _port_digits, "%u", app.port);
    loadConfig(&app);

    if(pipe(app.channel) == -1){
        write(_err, _wlen("pipe error\n"));
        return 1;
    };

    int pid = fork();
    if(pid == -1) {
        write(_err, _wlen("fork error\n"));
        return 1;
    }

    if(pid == 0){
        close(app.channel[_in]);
        dup2(app.channel[_out], _out);
        dup2(app.channel[_out], _err);

        execlp("aseqdump", "aseqdump", "-p", port_str, 0);
        write(_out, _wlen("_exit\n"));
    } else {
        write(_out, _wlen("Listening for MIDI input on port "));
        write(_out, _wlen(port_str));
        write(_out, _wlen("\n"));

        bool blocked = true;
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(app.channel[_in], &fds);

        // ignore any lingering messages
        struct timeval timeout = {.tv_sec = 0, .tv_usec = 100000};
        int leftover_msgs = select(app.channel[_in] + 1, &fds, 0, 0, &timeout);
        while(leftover_msgs) {
            readLine(&app, _aseq_header_len);
            leftover_msgs = select(app.channel[_in] + 1, &fds, 0, 0, &timeout);
            blocked = false;
        }

        while(1) {
            if(blocked) {
                readLine(&app, _aseq_header_len);
                blocked = false;
            }

            int res = readLine(&app, _aseq_log_len);
            if(res == -1) // error
                break;
            if(res == 0) // ignore
                continue;

            MidiEvent event = getEvent(app);
            if(event.note == -1) {
                //TODO: handle error
                continue;
            }
            // printf("note: %d trigger: %d\n", event.note, event.trigger);
            runCommand(app, event);
            //if(note) runCommand(app, note);
        }

        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
    }

    return 0;
}

void loadConfig(Data *app){
    const char* home = getenv("HOME");
    if(!home) {
        write(_err, _wlen("$HOME variable not set\n"));
        return;
    }

    char path[strlen(home) + strlen(_conf_path) + 1];
    snprintf(_wsize(path), "%s%s", home, _conf_path);

    int fd = open(path, O_RDONLY);
    if(fd == -1) {
        write(_err, _wlen("Error opening config file "));
        write(_err, _wlen(path));
        write(_err, _wlen("\n"));
        return;
    }

    char b;
    uint l_count = 0;
    while(read(fd, &b, 1) > 0){
       if(b == '\n') l_count++;
    }
    lseek(fd, 0, SEEK_SET);

    char *lines[l_count];

    uint l_cur = 0;
    uint l_bytes = 0;
    uint pos = 0;
    while(read(fd, &b, 1) > 0){
       pos++;
       l_bytes++;
       if(b != '\n') continue;

       //TODO: error handling
       lines[l_cur] = (char*)malloc(l_bytes*sizeof(char));
       lseek(fd, pos - l_bytes, SEEK_SET);
       for(int i = 0; i < l_bytes; i++){
           read(fd, &b, 1);
           lines[l_cur][i] = b;
       }

       lines[l_cur][l_bytes-1] = 0;
       l_bytes = 0;
       l_cur++;
    }

    //TODO: break this up into functions
    for(l_cur = 0; l_cur < l_count; l_cur++){
        char *c = lines[l_cur];
        _wstart(c);
        if(*c == '#' || *c == 0)
            continue;

        // check for on_press/on_release keyword
        {
            char *w = c;
            _wend(w);
            int size = (int)(w-c);
            if(size <= 0) continue;
            char word[size + 1];
            w = c;
            for(int i = 0; i < size; i++)
                word[i] = *(w++);
            word[size] = 0;

            if(strcmp(word, "on_press") == 0){
                app->trigger_state = on_press;
                continue;
            }
            if(strcmp(word, "on_release") == 0){
                app->trigger_state = on_release;
                continue;
            }
            if(strcmp(word, "on_hold") == 0){
                // TODO: make this work
                app->trigger_state = on_hold;
                continue;
            }
        }

        char *end;
        long int note = strtol(c, &end, 10);
        if(note == 0) continue;
        if(end) c = end;

        _wstart(c);
        if(*c != '=') continue;

        c++; _wstart(c);
        if(*c == 0) continue;

        char *w = c;
        _cmdend(w);

        int cmd_len = (int)(w - c);
        if(cmd_len <= 0) continue;

        if(app->n_commands == 0)
            app->commands = (UserCommand*)malloc(sizeof(UserCommand));
        else
            app->commands = (UserCommand*)realloc(app->commands, sizeof(UserCommand)*(app->n_commands + 1));

        app->commands[app->n_commands].str = (char*) malloc(sizeof(char)*(cmd_len+1));
        for(int i = 0; i < cmd_len; i++)
            app->commands[app->n_commands].str[i] = *(c++);

        app->commands[app->n_commands].str[cmd_len] = 0;
        app->commands[app->n_commands].note = note;
        app->commands[app->n_commands].trigger = app->trigger_state;

        app->n_commands++;
    }

    for(l_cur = 0; l_cur < l_count; l_cur++)
        free(lines[l_cur]);

    close(fd);
}

void runCommand(Data app, MidiEvent e){
    for(int i = 0; i < app.n_commands; i++){
        if(!(app.commands[i].note == e.note && app.commands[i].trigger == e.trigger))
            continue;

        ShellCommand *c = parseCommand(app.commands[i].str);
        if(c){
            if(fork() == 0){
                execvp(c->path, c->argv);
                exit(0);
            }

            freeCommand(c);
        };
    }
}

//separate cmd string into multiple args
ShellCommand* parseCommand(char* src){
    ShellCommand *c = (ShellCommand*) malloc(sizeof(ShellCommand));
    c->argv = 0;
    c->path = 0;

    char *cur = src;
    while(*cur != 0){
        _wstart(cur);
        const char* start = cur;
        if(*start == '\"' || *start == '\''){
            cur++; cur = seekToNext(cur, *start);
            if(*cur == 0) goto err_unclosed;
        }
        _wend(cur);

        int len = (int)(cur - start);
        if(c->argc == 0)
            c->argv = malloc(sizeof(char*));
        else
            c->argv = realloc(c->argv, sizeof(char*) * (c->argc + 1));

        c->argv[c->argc] = malloc(sizeof(char) * (len + 1));
        snprintf(c->argv[c->argc], len + 1, "%s", start);
        c->argv[c->argc][len] = 0;
        c->argc++;
    }
    if(c->argc == 0) return 0;

    // prepare for execvp
    c->argv = realloc(c->argv, (c->argc + 1) * sizeof(char*));
    c->argv[c->argc] = 0;
    c->path = c->argv[0];

    return c;

err_unclosed:
    write(_err, _wlen("command syntax error: unclosed quote\n"));
    freeCommand(c);
    return 0;
}

void freeCommand(ShellCommand *cmd) {
    if(!cmd) return;
    for(int i = 0; i < cmd->argc; i++)
        free(cmd->argv[i]);

    free(cmd);
}

// read line from aseqdump and update buffer
// return values:
// (-1) error
// (0) ignore, wrong format
// (1) ok, expected format
int readLine(Data *app, int len){
    int bytes = read(app->channel[_in], app->buffer, len);
    if(bytes == -1){
        write(_err, _wlen("read error\n"));
        return -1;
    }
    app->buffer[bytes] = 0;

    if(strncmp(app->buffer, "_exit", 5) == 0){
        write(_err, _wlen("could not find/start aseqdump\n"));
        return -1;
    }

    if(strstr(app->buffer, "Cannot connect") != 0){
        write(_err, _wlen("Could not connect to port\n"));
        return -1;
    }

    if(strstr(app->buffer, "Port unsubscribed") != 0){
        write(_err, _wlen("Lost connection to port %d\n"));
        return -1;
    }

    uint port = 0;
    int n = sscanf(app->buffer, "%3u:", &port);
    if(!(n == 1 && port == app->port)) // wrong format
        return 0;

    return 1;
}

MidiEvent getEvent(Data app) {
    MidiEvent e;
    e.trigger = on_press;
    e.note = 0;

    if(strstr(app.buffer, N_ON))
        e.trigger = on_press;
    else if(strstr(app.buffer, N_OFF))
        e.trigger = on_release;

    char *note_pos = strstr(app.buffer, "note ");
    if(!note_pos)
        goto err_unexpected_format;
    if(sscanf(note_pos, "note %3u,", &e.note) != 1)
        goto err_unexpected_format;

    return e;

// should not trigger, unless aseqdump format changes
err_unexpected_format:
    e.note = -1;
    return e;
}

// can I turn this into a macro?
char* seekToNext(char* cur, char target){
    while(!(*cur == target || *cur == 0)) cur++;
    return cur;
};

