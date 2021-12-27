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
char const *create_puzzles_table = "create table puzzles (id integer primary key autoincrement, puzzle_id text not null, score integer default 0, next_test_date text not null)";
char const *create_results_table = "create table results (id integer primary key autoincrement, puzzle_id text not null, date text not null, result text not null)";
char const *puzzle_exists_statement = "select 1 from puzzles where puzzle_id=:puzzleid";
char const *insert_puzzle_statement = "insert into puzzles (puzzle_id, score, next_test_date) values (:puzzle_id, :score, :next_test_date)";
char const *insert_result_statement = "insert into results (puzzle_id, date, result) values (:puzzle_id, :date, :result)";
char const *update_puzzle_statement = "update puzzles set score=:score, next_test_date=:next_test_date where puzzle_id=:puzzle_id";
char const *get_next_test_statement = "select puzzle_id from puzzles where next_test_date=:next_test_date";
char const *get_score_for_puzzle_statement = "select score from puzzles where puzzle_id=:puzzle_id";
char const *get_overall_failure_success_rate_statement = "select sum(case when result=\"f\" then 1.0 else 0.0 end)/count(*) * 100 as failure_rate, sum(case when result=\"s\" then 1.0 else 0.0 end)/count(*) * 100 as success_rate from results";
char const *dtformat = "%F";
char const *useage = 
  "Useage dailypuzzles <command>\n"
  "COMMAND\n"
  " \"next\" -- prints the next puzzle for the day, if available\n"
  " \"stats\" -- prints the overall success and failure rates\n"
  " if command is none of these it should be a puzzle number followed by the character 's' or 'f' indicating success or failure\n";

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

  rc = sqlite3_exec(dbc, create_results_table, NULL, NULL, &error_message);
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

void print_useage() {
  puts(useage);
}

void get_next() {

  sqlite3_stmt * next_test_stmt;
  sqlite3 * dbc = get_db_conn();
  char * today = get_today();

  sqlite3_prepare_v2(dbc, get_next_test_statement, strlen(get_next_test_statement) + 50, &next_test_stmt, NULL);

  sqlite3_bind_text(next_test_stmt,1,today,strlen(today),NULL);

  int result = sqlite3_step(next_test_stmt);

  if(result == SQLITE_ERROR){
    printf("ERROR getting next test: %s\n", sqlite3_errmsg(dbc));
    return;
  }

  if(result == SQLITE_ROW) {
    const unsigned char* next_test_id = sqlite3_column_text(next_test_stmt,0);

    printf("https://www.chess.com/puzzles/problem/%s\n", next_test_id);
    sqlite3_finalize(next_test_stmt);
    return;
  }

  if(result == SQLITE_DONE){
    puts("No more tests today!!!");
    sqlite3_finalize(next_test_stmt);
    return;
  }

  sqlite3_finalize(next_test_stmt);

}

int is_fail(char * success_arg) {
  return strcmp(success_arg, "f") == 0;
}

int is_pass(char * success_arg) {
  return strcmp(success_arg, "s") == 0;
}

int check_success_arg(char * success_arg) {
  return is_fail(success_arg) || is_pass(success_arg);
}

int check_puzzle_exists(sqlite3* dbc, char * puzzle_id) {
  sqlite3_stmt * stmt;
  sqlite3_prepare_v2(dbc, puzzle_exists_statement, 50, &stmt, NULL);
  sqlite3_bind_text(stmt,1,puzzle_id,strlen(puzzle_id),NULL);
  int result = sqlite3_step(stmt);
  return result == SQLITE_ROW;
}

void reset_puzzle_for_failure(sqlite3* dbc, char * puzzle_id) {

  sqlite3_stmt * update_puzzle_stmt;

  char * next_test_day = get_target_day(1);

  sqlite3_prepare_v2(dbc, update_puzzle_statement, strlen(update_puzzle_statement) + 20, &update_puzzle_stmt, NULL);

  sqlite3_bind_int(update_puzzle_stmt,1,0);
  sqlite3_bind_text(update_puzzle_stmt,2,next_test_day,strlen(next_test_day),NULL);
  sqlite3_bind_text(update_puzzle_stmt,3,puzzle_id,strlen(puzzle_id),NULL);

  int result = sqlite3_step(update_puzzle_stmt);
  if(result == SQLITE_ERROR || result != SQLITE_DONE){
    printf("ERROR resetting puzzle for failure: %s\n", sqlite3_errmsg(dbc));
  }

  sqlite3_finalize(update_puzzle_stmt);

}

int get_score_for_puzzle(sqlite3 * dbc, char * puzzle_id){

  sqlite3_stmt * get_score_stmt;
  int score = 0;

  sqlite3_prepare_v2(dbc, get_score_for_puzzle_statement,strlen(get_score_for_puzzle_statement) + 20, &get_score_stmt, NULL);

  sqlite3_bind_text(get_score_stmt, 1, puzzle_id, strlen(puzzle_id),NULL);

  int result = sqlite3_step(get_score_stmt);
  if(result == SQLITE_ERROR || result != SQLITE_ROW){
    printf("ERROR getting score for puzzle: %s\n", sqlite3_errmsg(dbc));
    sqlite3_finalize(get_score_stmt);
    return score;
  }

  score = sqlite3_column_int(get_score_stmt, 0);

  sqlite3_finalize(get_score_stmt);

  return score;
  
}

void advance_puzzle_on_success(sqlite3* dbc, char * puzzle_id) {

  sqlite3_stmt * update_puzzle_stmt;
  int current_score = get_score_for_puzzle(dbc, puzzle_id) + 1;
  int day_offset = fibonacci1(current_score);
  char * next_test_day = get_target_day(day_offset);

  sqlite3_prepare_v2(dbc, update_puzzle_statement, strlen(update_puzzle_statement) + 20, &update_puzzle_stmt, NULL);

  sqlite3_bind_int(update_puzzle_stmt,1,current_score);
  sqlite3_bind_text(update_puzzle_stmt,2,next_test_day,strlen(next_test_day),NULL);
  sqlite3_bind_text(update_puzzle_stmt,3,puzzle_id,strlen(puzzle_id),NULL);

  int result = sqlite3_step(update_puzzle_stmt);
  if(result == SQLITE_ERROR || result != SQLITE_DONE){
    printf("ERROR updating puzzle for success: %s\n", sqlite3_errmsg(dbc));
  }

  sqlite3_finalize(update_puzzle_stmt);

}

void log_result(sqlite3 *dbc, char * puzzle_id, char * success_arg) {

  sqlite3_stmt * insert_result_stmt;
  char * today = get_today();

  sqlite3_prepare_v2(dbc, insert_result_statement, strlen(insert_result_statement) + 20, &insert_result_stmt, NULL);

  sqlite3_bind_text(insert_result_stmt,1,puzzle_id,strlen(puzzle_id),NULL);
  sqlite3_bind_text(insert_result_stmt,2,today,strlen(today),NULL);
  sqlite3_bind_text(insert_result_stmt,3,success_arg,strlen(success_arg),NULL);

  int result = sqlite3_step(insert_result_stmt);
  if(result == SQLITE_ERROR || result != SQLITE_DONE){
    printf("ERROR inserting new puzzle result: %s\n", sqlite3_errmsg(dbc));
  }
  sqlite3_finalize(insert_result_stmt);
}

void update_existing_puzzle(sqlite3* dbc, char * puzzle_id, char * success_arg) {

  if(is_fail(success_arg)){
    reset_puzzle_for_failure(dbc, puzzle_id);
  } else {
    advance_puzzle_on_success(dbc, puzzle_id);
  } 

  log_result(dbc, puzzle_id, success_arg);

}

void create_new_puzzle_entry(sqlite3* dbc, char * puzzle_id, char * success_arg) {

  sqlite3_stmt * insert_puzzle_stmt;

  char * next_test_day = get_target_day(1);

  sqlite3_prepare_v2(dbc, insert_puzzle_statement, strlen(insert_puzzle_statement) + 20, &insert_puzzle_stmt, NULL);

  sqlite3_bind_text(insert_puzzle_stmt,1,puzzle_id,strlen(puzzle_id),NULL);
  sqlite3_bind_int(insert_puzzle_stmt,2,0);
  sqlite3_bind_text(insert_puzzle_stmt,3,next_test_day,strlen(next_test_day),NULL);

  int result = sqlite3_step(insert_puzzle_stmt);
  if(result == SQLITE_ERROR || result != SQLITE_DONE){
    printf("ERROR inserting new puzzle: %s\n", sqlite3_errmsg(dbc));
  }
  sqlite3_finalize(insert_puzzle_stmt);

  log_result(dbc, puzzle_id, success_arg);

}

void update_puzzle(char * puzzle_id, char * success_arg) {

  sqlite3* dbc = get_db_conn();

  int exists = check_puzzle_exists(dbc, puzzle_id);
  if(exists){
    update_existing_puzzle(dbc, puzzle_id, success_arg);
  } else {
    create_new_puzzle_entry(dbc, puzzle_id, success_arg);
  }

  sqlite3_close(dbc);

}

void get_stats() {

  sqlite3 * dbc = get_db_conn();
  sqlite3_stmt * fail_success_rate_stmt;

  double failure_rate, success_rate;

  sqlite3_prepare_v2(dbc, get_overall_failure_success_rate_statement, strlen(get_overall_failure_success_rate_statement), &fail_success_rate_stmt,NULL);

  int result = sqlite3_step(fail_success_rate_stmt);

  if(result != SQLITE_ROW) {
    printf("ERROR getting next test: %s\n", sqlite3_errmsg(dbc));
    return;
  }

  failure_rate = sqlite3_column_double(fail_success_rate_stmt, 0);
  success_rate = sqlite3_column_double(fail_success_rate_stmt, 1);

  printf("FAIL: %.2f\nSUCCESS: %.2f\n", failure_rate, success_rate);

  sqlite3_close(dbc);

}

int main(int argc, char** argv) {

  char * command_arg;
  char * success_arg;
  if(argc < 2 || argc > 3){
    print_useage();
    return 0;
  }

  command_arg = argv[1];

  if(argc == 2) {

    if(strcmp(command_arg, "next") == 0){
      get_next();
      return 0;
    } 

    if(strcmp(command_arg, "stats") == 0){
      get_stats();
      return 0;
    }

    print_useage();
    return 0;

  }

  success_arg = argv[2];

  if(!check_success_arg(success_arg)){
    print_useage();
    return 0;
  }

  update_puzzle(command_arg, success_arg);


  return 0;

}
