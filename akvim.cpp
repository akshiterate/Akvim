#define _DEFAULT_SOURCE //im guessing it makes code portable
#define _BSD_SOURCE

/*** includes ***/
#include <iostream>
#include <unistd.h>
#include <termios.h>
#include <cstdlib>
#include <cctype>
#include <cerrno>
#include <sys/ioctl.h>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <sys/types.h>
#include <fstream>
#include <vector>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)
#define AKVIM_VERSION "0.0.1"
#define KILO_TAB_STOP 20


enum editorKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  PAGE_UP,
  DEL_KEY,
  PAGE_DOWN
};// it seems the rest are set incrementally thats crazy

/*** data ***/
class erow{
public:
    int size;
    int rsize;
    std::string render;
    std::string chars;
};

class editorConfig{
public:
    int cx,cy;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    std::vector<erow> row;

    termios orig_termios;
};
editorConfig E;

/*** terminal ***/
void die(const char *s){
    write(STDOUT_FILENO, "\x1b[2j",4); 
    write(STDOUT_FILENO, "\x1b[H", 3);

    std::perror(s);
    std::exit(1);
}

void disableRawMode(){
    if(tcsetattr(STDIN_FILENO,TCSAFLUSH,&E.orig_termios) == -1){
        die("tcsetattr");
    }
}

void enableRawMode(){
    if(tcgetattr(STDIN_FILENO,&E.orig_termios) == -1){
        die("tcgetattr");
    }
    atexit(disableRawMode);

    termios raw = E.orig_termios;    
    raw.c_iflag &= ~(BRKINT | ICRNL | IXON | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag &= ~(CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH,&raw) == -1){
        die("tcsetattr");
    }

}

int editorReadKey(){
    int nread;
    char c;
    while(read(STDIN_FILENO, &c, 1) == -1){
        if(nread == -1 && errno!=EAGAIN) die("read");
    }

    if(c == '\x1b'){
        char seq[3];
        if(read(STDIN_FILENO,&seq[0],1)!=1) return '\x1b';
        if(read(STDIN_FILENO,&seq[1],1)!=1) return '\x1b';

        if(seq[0] == '['){
            if(seq[1]>='0' && seq[1]<='9'){
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '3': return DEL_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                    }
                }
            }
            else{
                switch(seq[1]){
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                }
            }


        }
        return '\x1b';
    }
    else{
        return c;
    }
}

int getWindowSize(int *rows, int *cols) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    return -1;
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/*** row operations***/

void editorUpdateRow(erow& row){
    int tabs =0;
    for(int j=0;j<row.chars.length();j++){
        if(row.chars[j] == '\t') tabs++;
    }
    int idx = 0;
    row.render.resize(row.chars.length() + tabs*(KILO_TAB_STOP-1) +1);
    for(int i=0;i<row.chars.length();i++){
        if (row.chars[i] == '\t'){
            row.render[idx++] = ' ';
            while(idx%KILO_TAB_STOP !=0) row.render[idx++] = ' ';
        }
        else{
            row.render[idx++] = row.chars[i];
        }
    }
    row.render.resize(idx);
    row.rsize = idx;
}

void editorAppendRow(const std::string& s) {
    erow newRow;
    newRow.size = s.length();
    newRow.chars = s;
    E.row.push_back(newRow);
    E.numrows = E.row.size();
    editorUpdateRow(E.row.back());
}

/*** file i/o***/
void editorOpen(const std::string& filename){
    std::ifstream file(filename);
    int linelen;
    std::string line;
    while(std::getline(file,line)){
        while(!line.empty() &&(line.back() == '\n' || line.back() == '\r')){
            line.pop_back();
            
        }
        editorAppendRow(line);
    }

}



/*** append buffer ***/
class abuf{
public:
    char *b;
    int len;
    abuf(){
        b=NULL;
        len =0;
    }
    ~abuf(){   //a deconstructor oooooooooooooooooooooooooooooo scary
        free(b);
    }
    void append(const char *s, int len) {
        char *new_buf = (char*) realloc(b, this->len + len);
        if (new_buf == NULL) return;

        memcpy(&new_buf[this->len], s, len);
        b = new_buf;
        this->len += len;
    }
};



/*** output ***/
void editorScroll(){
    if(E.cy<E.rowoff){
        E.rowoff = E.cy;
    }
    if(E.cy>=E.rowoff + E.screenrows){
        E.rowoff = E.cy - E.screenrows +1;
    }

    if(E.cx < E.coloff){
        E.coloff = E.cx;
    }
    if(E.cx>=E.coloff + E.screencols){
        E.coloff = E.cx - E.screencols +1;
    }
}

void editorDrawRows(abuf *ab){
    int y;
    for(y=0;y<E.screenrows;y++){
        int filerow = y + E.rowoff;
        if(filerow>=E.numrows){
            if(E.numrows==0 && y == E.screenrows/3){
                std::ostringstream oss;
                oss<<"akvim editor --version "<<AKVIM_VERSION;
                std::string welcome = oss.str();
                int welen = welcome.length();
                if(welen>E.screencols){
                    welen = E.screencols;
                }
                int padding = (E.screencols - welen)/2;
                if (padding){
                    ab->append("~",1);
                    padding--;
                }
                while(padding--) ab->append(" ",1);
                ab->append(welcome.c_str(),welen);
            }
            else{
                ab->append("~", 1);
            }
        }
        else{
            int len = E.row[filerow].render.length() - E.coloff;
            if(len < 0) len=0;
            if(len > E.screencols) len = E.screencols;
            ab->append(E.row[filerow].render.c_str()+E.coloff,len);
        }
        ab->append("\x1b[K", 3);
        if (y < E.screenrows - 1) {
            ab->append("\r\n", 2);
        }
    }
}
void editorRefreshScreen(){
    editorScroll();
    abuf ab;
    ab.append("\x1b[?25l", 6);
//    ab.append("\x1b[2J",4); 
    ab.append("\x1b[H", 3);
    
    editorDrawRows(&ab);
    std::ostringstream oss;
    oss<<"\x1b["<<(E.cy-E.rowoff)+1<<";"<<(E.cx-E.coloff)+1<<"H";
    std::string buf = oss.str();
    ab.append(buf.c_str(),buf.length());

    ab.append("\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    // apparantly the destructor runs automatically somehow, although it was called in the c tutorial for this.
}

/*** input ***/
void editorMoveCursor(int key){
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    switch(key){
        case ARROW_UP:
            if(E.cy!=0){
                E.cy--;
            }
            break;
        case ARROW_LEFT:
        if(E.cx!=0){
            E.cx--;
        }else if(E.cy>0){
            E.cy--;
            E.cx = E.row[E.cy].size;
        }

            break;
        case ARROW_DOWN:
            if(E.cy<E.numrows){
                E.cy++;
            }

            break;
        case ARROW_RIGHT:
        if (row && E.cx < row->size) {
            E.cx++;
        }else if(row && E.cx == row->size){
            E.cy++;
            E.cx = 0;
        }
            break; 
    }
    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen) {
        E.cx = rowlen;
    }
}

void editorProcessKeypress(){
    int c = editorReadKey();
    switch(c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2j",4); 
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        
        case PAGE_UP:
        case PAGE_DOWN:
            {
                int times = E.screenrows;
                while(times--){
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP: ARROW_DOWN);
                }
            }
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}


/*** init ***/
void initEditor(){
    E.cx =0;
    E.cy=0;
    E.numrows = 0;
    E.rowoff = 0;   
    E.coloff = 0;
    if(getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();
    if(argc>=2){
        editorOpen(argv[1]);

    }

    while (1){
        editorRefreshScreen();
        editorProcessKeypress();
    };
    return 0;
}