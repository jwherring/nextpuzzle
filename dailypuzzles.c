#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

char const *dbfh = "dailypuzzles.sqlite";
char const *create_puzzles_table = "create table puzzles (id int primary key, puzzle text not null)";
char const *create_results_table = "create table results (id int primary key, puzzle text not null, date text not null, result integer default 0)";
char const *create_score_table = "create table score (id int primary key, puzzle text not null, score integer default 0)";
char const *dtformat = "%F";
mode_t fullmode = S_IRWXU|S_IRWXG|S_IRWXO;

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
    return;
  }

  rc = sqlite3_exec(dbc, create_puzzles_table, NULL, NULL, &error_message);
  if (error_message != 0) {
    printf("%s\n", error_message);
    return;
  }

  rc = sqlite3_exec(dbc, create_puzzles_table, NULL, NULL, &error_message);
  if (error_message != 0) {
    printf("%s\n", error_message);
    return;
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

/*Fibonacci implementation starting at 1, so we get 1, 1, 2, 3, 5 ... instead
 * of 0, 1, 1, 2, 3, 5...*/
int fibonacci1(int seed) {

  int a,b;
  a = 0;
  b = 1;

  for(int i = 0; i < seed; i++) {
    int temp = b;
    b = a + b;
    a = temp;
  }

  return b;
}


struct tm* get_current_time() {

  time_t currtm = time(NULL);
  struct tm * lcltm = localtime(&currtm);
  return lcltm;

}

char* get_target_day(int offset) {

  struct tm * lcltm = get_current_time();
  lcltm->tm_mday += offset;

  char * repr = malloc(sizeof(char) * 11);
  mktime(lcltm); //handle possible date overflow

  strftime(repr, 11, dtformat, lcltm);

  return repr;
}

char* get_today() {

  return get_target_day(0);

}

int main() {
  sqlite3* dbc = get_db_conn();
  sqlite3_close(dbc);
  for(int i = 0; i < 10; i++){
    printf("[%d]: %d\n", i, fibonacci1(i));
  }

  char * today = get_today();
  char * tomorrow = get_target_day(1);
  char * next_month = get_target_day(30);
  printf("TODAY: %s\n", today);
  printf("TOMORROW: %s\n", next_month);
  free(today);
  return 0;

}
