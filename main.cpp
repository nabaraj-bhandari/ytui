#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <ncurses.h>
#include <unistd.h>

#include "config.h"
#include "globals.h"
#include "types.h"
#include "ui.h"
#include "utils.h"

int main() {
    mkdirs();
    load_search_hist();
    load_history();
    signal(SIGPIPE, SIG_IGN);
    init_ui();
    bool run = true;
    while(run) {
        draw();
        run = handle_input();
        napms(16);
    }
    save_history();
    cleanup_ui();
    return 0;
}
