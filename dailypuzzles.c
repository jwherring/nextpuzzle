#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sqlite3.h>
#include <string.h>

char const *dbfh = "dailypuzzles.sqlite";
char const *create_puzzles_table = "create table puzzles (id int primary key, puzzle text not null)";
mode_t fullmode = S_IRWXU|S_IRWXG|S_IRWXO;

void create_database() {
}

int database_file_exists() {
  return access(dbfh, R_OK|W_OK|X_OK) == 0;
}

void print_error(int er_num, int ln_num) {
  printf("ERROR: (%s - %d) %s\n", __FILE__, ln_num, strerror(er_num));
}

void touch_dbfile() {

    int fd = open(dbfh, (int)fullmode);
    int errnum;
    if(fd == 0){
      errnum = errno;
      print_error( errnum, (__LINE__ - 2));
    }
    close(fd);

}

void create_tables(sqlite3* dbc) {
  char * error_message = 0;
  int rc;
  rc = sqlite3_exec(dbc, create_puzzles_table, NULL, NULL, &error_message);
  if (error_message != 0) {
    printf("%s\n", error_message);
  }
}

sqlite3* get_db_conn() {

  sqlite3* dbc = 0;
  int errnum;
  int db_exists = database_file_exists();
  if (!db_exists) {
    touch_dbfile();
  }
  sqlite3_open(dbfh, &dbc);
  if (!db_exists){ //create table if file wasnt there
    create_tables(dbc);
  }
  return dbc;

}


int main() {
  sqlite3* dbc = get_db_conn();
  sqlite3_close(dbc);
  return 0;
}
