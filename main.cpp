#include <ncurses.h>
#include <cstdlib>
#include <unistd.h>
#include <cstdio>
#include <signal.h>
#include <ctime>
#include "config.h"
#include "types.h"
#include "utils.h"
#include "ui.h"

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
