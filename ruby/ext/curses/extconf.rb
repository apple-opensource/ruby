require 'mkmf'

dir_config('curses')
dir_config('ncurses')
dir_config('termcap')

make=false
have_library("mytinfo", "tgetent") if /bow/ =~ RUBY_PLATFORM
have_library("tinfo", "tgetent") or have_library("termcap", "tgetent")
if have_header("ncurses.h") and have_library("ncurses", "initscr")
  make=true
elsif have_header("ncurses/curses.h") and have_library("ncurses", "initscr")
  make=true
elsif have_header("curses_colr/curses.h") and have_library("cur_colr", "initscr")
  make=true
else
  if have_header("curses.h") and have_library("curses", "initscr")
    make=true
  end
end

if make
  for f in %w(isendwin ungetch beep getnstr wgetnstr doupdate flash deleteln wdeleteln keypad keyname init_color wresize resizeterm)
    have_func(f)
  end
  create_makefile("curses")
end
