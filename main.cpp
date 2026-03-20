#include <csignal>
#include <ncurses.h>
#include <unistd.h>

#include "ui.h"
#include "utils.h"

int main() {
  mkdirs();
  load_search_hist();
  load_history();
  signal(SIGPIPE, SIG_IGN);
  init_ui();
  bool run = true;
  while (run) {
    draw();
    redraw_thumbnail();
    run = handle_input();
    napms(16);
  }
  save_history();
  hide_thumbnail();
  cleanup_ui();
  return 0;
}
