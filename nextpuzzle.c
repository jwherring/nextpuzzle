#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <regex.h>
#include <sys/stat.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "nextpuzzle.h"

char const *dbfh = "dailypuzzles.sqlite";
char const *create_puzzles_table = "create table puzzles (id integer primary key autoincrement, puzzle_id text not null, score integer default 0, next_test_date text not null)";
char const *create_results_table = "create table results (id integer primary key autoincrement, puzzle_id text not null, date text not null, result text not null)";
char const *puzzle_exists_statement = "select 1 from puzzles where puzzle_id=:puzzleid";
char const *insert_puzzle_statement = "insert into puzzles (puzzle_id, score, next_test_date) values (:puzzle_id, :score, :next_test_date)";
char const *insert_result_statement = "insert into results (puzzle_id, date, result) values (:puzzle_id, :date, :result)";
char const *update_puzzle_statement = "update puzzles set score=:score, next_test_date=:next_test_date where puzzle_id=:puzzle_id";
char const *get_next_test_statement = "select puzzle_id from puzzles where next_test_date<=:next_test_date";
char const *get_puzzle_at_offset_statement = "select puzzle_id from puzzles where next_test_date<=:next_test_date limit 1 offset :offset";
char const *get_total_remaining_tests_statement = "select count(*) from puzzles where next_test_date<=:next_test_date";
char const *get_upcomming_puzzles_count_by_date = "select next_test_date, count(*) as total from puzzles group by next_test_date";
char const *get_score_for_puzzle_statement = "select score from puzzles where puzzle_id=:puzzle_id";
char const *get_overall_failure_success_rate_statement = "select sum(case when result=\"f\" then 1.0 else 0.0 end)/count(*) * 100 as failure_rate, sum(case when result=\"s\" then 1.0 else 0.0 end)/count(*) * 100 as success_rate from results";
char const *get_individual_puzzle_stats_statement = "select puzzle_id, score, (select count(1) from results rs where result='s' and pz.puzzle_id=rs.puzzle_id) as success, (select count(1) from results rs where result='f' and pz.puzzle_id=rs.puzzle_id) as failure, (select count(1) from results rs where pz.puzzle_id=rs.puzzle_id) as attempts from puzzles pz order by score desc, success desc, failure asc";
char const *set_puzzle_date_statement = "update puzzles set next_test_date=:next_test_date where puzzle_id=:puzzle_id";
char const *delete_puzzle_from_puzzles_statement = "delete from puzzles where puzzle_id=:puzzle_id";
char const *delete_puzzle_from_results_statement = "delete from results where puzzle_id=:puzzle_id";
char const *begin_transaction_statement = "begin transacton";
char const *commit_transaction_statement = "commit";
char const *rollback_transaction_statememt = "rollback";
char const *dtformat = "%F";
char const *success_fail_string_regex = "^[sf]+$";
char const *useage = 
  "Useage dailypuzzles <command> [args...]\n"
  "COMMAND\n"
  " \"<no arg>\" -- prints the next puzzle for the day, if available\n"
  " \"s\" -- marks the current puzzle for success\n"
  " \"f\" -- marks the current puzzle for success\n"
  " \"delete <puzzle__id>\" -- removes all references to puzzle <puzzle_id> from the database\n"
  " \"future\" -- prints a list of dates paired with the number of tests schedules for that date\n"
  " \"next\" -- prints the next puzzle for the day, if available\n"
  " \"n <number>\" -- prints the next n puzzles for the day, if so many are available\n"
  " \"stats\" -- prints the overall success and failure rates\n"
  " \"useage\" -- prints this message\n"
  " if command is none of these it should be a puzzle number (or url) followed by the character 's' or 'f' indicating success or failure\n";

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

/* create_tables takes an sqlite3 database connection and creats two tables:
 * puzzles: to store puzzle id, current score and next test date for each
 * puzzle
 */
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

/* get_db_conn() returns an sqlite3 database connection to an sqlite3  database
 * file called dailypuzzles.sqlite in the same directory as the current script,
 * creating it if it does not exist. */
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

struct interval_update {
  int successes;
  double easiness_factor;
  int interval;
};

/* Implementaton of the SM2 algoriithm from SuperMemo: n - number of successful
 * repetitions in a row q - user grade for how difficult recall was - (>= 3
 * indiicates success) RETURNS - interval in days before next test
 */
void sm2(int q, struct interval_update* iu) {

  if (q >= 3) { //success
    if(iu->successes > MAX_SUCCESS){
      iu->interval = MAX_INTERVAL;
    } else if(iu->successes == 0) {
      iu->interval = 1;
    } else if (iu->successes == 1) {
      iu->interval = BASE_INTERVAL;
    } else {
      iu->interval = round(iu->interval * iu->easiness_factor);
    }
    iu->successes += 1;
  } else {//failure
    iu->interval = 1;
    iu->successes = 0;
  }

  iu->easiness_factor += (0.1 - (5 - q) * (0.08 + (5 - q) * 0.02));
  if(iu->easiness_factor < 1.3){
    iu->easiness_factor = 1.3;
  }


}


/* get_current_time() returns a pointer to a c tm struct representing the
 * current system clock time */
struct tm* get_current_time() {

  time_t currtm = time(NULL);
  struct tm * lcltm = localtime(&currtm);
  return lcltm;

}

/* get_target_day takes an int <offset> representing a number of days and gets
 * the string representation of the day <offset> number of days from today in
 * YYYY-MM-DD  format */
void get_target_day(char * repr, int offset) {

  struct tm * lcltm = get_current_time();
  lcltm->tm_mday += offset;

  //char * repr = malloc(sizeof(char) * 11);
  mktime(lcltm); //handle possible date overflow

  strftime(repr, 11, dtformat, lcltm);

}

/* get_today() gets the current day in YYYY-MM-DD format - proxying to
 * get_target_day for the result  */
void get_today(char * repr) {

  get_target_day(repr, 0);

}

/* print instructions on how to use this program */
void print_useage() {
  puts(useage);
}

/* get_stats returns a string showing the overall success and fail rate across
 * all attempts on all puzzles */
char * get_stats(sqlite3* dbc) {

  sqlite3_stmt * fail_success_rate_stmt;

  double failure_rate, success_rate;

  sqlite3_prepare_v2(dbc, get_overall_failure_success_rate_statement, strlen(get_overall_failure_success_rate_statement), &fail_success_rate_stmt,NULL);

  int result = sqlite3_step(fail_success_rate_stmt);

  if(result != SQLITE_ROW) {
    printf("ERROR getting stats: %s\n", sqlite3_errmsg(dbc));
    return "";
  }

  failure_rate = sqlite3_column_double(fail_success_rate_stmt, 0);
  success_rate = sqlite3_column_double(fail_success_rate_stmt, 1);
  sqlite3_finalize(fail_success_rate_stmt);


  char today[11];
  get_today(today);
  int tests_remaining = get_total_tests_for_day(dbc, today);
  char * buffer = malloc(sizeof(char) * 50);
  sprintf(buffer, "REMAINING: %d\nFAIL: %.2f\nSUCCESS: %.2f\n", tests_remaining, failure_rate, success_rate);
  return buffer;

}

/* get_total_tests_for_day takes a database connection and a string
 * representation of a day in YYYY-MM-DD format and returns the total number of
 * tests slated to be worked on that day */
int get_total_tests_for_day(sqlite3 *dbc, char * day) {

  sqlite3_stmt * total_test_stmt;

  sqlite3_prepare_v2(dbc, get_total_remaining_tests_statement, strlen(get_total_remaining_tests_statement), &total_test_stmt, NULL);

  sqlite3_bind_text(total_test_stmt,1,day,strlen(day),NULL);

  int result = sqlite3_step(total_test_stmt);

  if(result == SQLITE_ERROR){
    printf("ERROR getting test count: %s\n", sqlite3_errmsg(dbc));
    return 0;
  }

  if(result == SQLITE_ROW){
    int total_tests = sqlite3_column_int(total_test_stmt, 0);
    return total_tests;
  }

  printf("ERROR getting test count\n");

  return 0;

}

/* current_puzzle takes a database connection and returns the puzzle_id of the
 * next  puzzle to be worked today */
void current_puzzle(sqlite3* dbc, char * retval) {

  sqlite3_stmt * next_test_stmt;
  char today[11];
  get_today(today);

  sqlite3_prepare_v2(dbc, get_next_test_statement, strlen(get_next_test_statement) + 50, &next_test_stmt, NULL);

  sqlite3_bind_text(next_test_stmt,1,today,strlen(today),NULL);

  int result = sqlite3_step(next_test_stmt);

  if(result == SQLITE_ERROR){
    printf("ERROR getting next test: %s\n", sqlite3_errmsg(dbc));
    strcpy(retval, "\0"); 
    return;
  }

  if(result == SQLITE_ROW) {
    const unsigned char* next_test_id = sqlite3_column_text(next_test_stmt,0);


    strcpy(retval, next_test_id);
    sqlite3_finalize(next_test_stmt);
    return;
  }

  // There are no more tests, return empty string.  Shouldn't actually get here
  // if you call get_total_tests_for_day and verify it's greater than 0 first
  strcpy(retval, "\0"); 
  sqlite3_finalize(next_test_stmt);

}

/* get_puzzle_at_offset takes a database connection, and integer offset and a
 * representation of a day in YYYY-MM-DD format and returns the puzzle at
 * <offset> position in line to be worked on that day */
void get_puzzle_at_offset(sqlite3 * dbc, char * retval, int offset, char * day) {

  sqlite3_stmt * get_puzzle_at_offset_stmt;

  sqlite3_prepare_v2(dbc, get_puzzle_at_offset_statement, strlen(get_puzzle_at_offset_statement) + 50, &get_puzzle_at_offset_stmt, NULL);

  sqlite3_bind_text(get_puzzle_at_offset_stmt,1,day,strlen(day),NULL);
  sqlite3_bind_int(get_puzzle_at_offset_stmt,2,offset);

  int result = sqlite3_step(get_puzzle_at_offset_stmt);
  if(result == SQLITE_ROW){
    const unsigned char* next_test_id = sqlite3_column_text(get_puzzle_at_offset_stmt,0);

    strcpy(retval, next_test_id);
    sqlite3_finalize(get_puzzle_at_offset_stmt);
    return;
  }

  sqlite3_finalize(get_puzzle_at_offset_stmt);
  strcpy(retval, "\0");

}

/* get_next() displays the next puzzle to be worked as a link to chess.com and
 * also shows the number of puzzles remaining to be worked on the current day
 * as well as the current pass/fail rate against all attempts on all puzzles */
void get_next() {

  sqlite3 * dbc = get_db_conn();
  char today[11];
  get_today(today);
  int tests_remaining = get_total_tests_for_day(dbc, today);

  if(tests_remaining > 0){
    char next_test_id[50];
    current_puzzle(dbc, next_test_id);
    if(strlen(next_test_id) == 0){
      printf("No more tests today!!!");
      return;
    }

    char * stats = get_stats(dbc);
    printf("https://www.chess.com/puzzles/problem/%s\n", next_test_id);
    printf("REMAINING: %d\n", tests_remaining - 1);
    puts(stats);
    free(stats);

    return;

  } else {
    printf("No more tests today!!!\n");
    return;
  }

}

/* get_next_count takes an int count and displays the next <count> number of
 * puzzles to be worked on the current day, provided there are that many left
 * */
void get_next_count(int count) {

  sqlite3 * dbc = get_db_conn();
  char today[11];
  get_today(today);
  int tests_remaining = get_total_tests_for_day(dbc, today);

  if(tests_remaining >= count){
    for(int i = 0; i < count; i++) {
      char puzzle_id[50];
      get_puzzle_at_offset(dbc,puzzle_id,i,today);
      printf("https://www.chess.com/puzzles/problem/%s\n", puzzle_id);
    }
    char * stats = get_stats(dbc);
    printf("REMAINING: %d\n", tests_remaining - 1);
    puts(stats);
    free(stats);

  } else {

    printf("There are only %d tests remaining today\n", tests_remaining);

  }

  sqlite3_close(dbc);

}

/* check_success_string_arg checks whether the argument is a string of 'f' and
 * 's' */
int check_success_string_arg(char * success_arg){
  regex_t expression;

  regcomp(&expression, success_fail_string_regex, REG_EXTENDED);
  
  if(regexec(&expression, success_arg, 0, NULL, 0) == 0){
    return 1; //Matches, return true
  }

  return 0;//Otherwise, false

}

/* record_batch_results takes a string consisting of only s and f -  each
 * character of which represents a result for a puzzle - and applies them one
 * by one to the next puzzle in line.  Returns early if there are not enough
 * puzzles to match the string */
void record_batch_results(char * success_arg) {

  sqlite3 * dbc = get_db_conn();

  char today[11];
  get_today(today);
  int batch_count = strlen(success_arg);
  int tests_remaining = get_total_tests_for_day(dbc, today);

  if(batch_count > tests_remaining) {
    printf("Cannot batch record results - there are only %d tests remaining and there are %d items in the request.\n", tests_remaining, batch_count);
    return;
  }

  for(int i = 0; i < batch_count; i++) {
    char s_arg[2];
    char puzzle_id_arg[MAX_PUZZLE_LEN];
    sprintf(s_arg, "%c", success_arg[i]);
    get_puzzle_at_offset(dbc,puzzle_id_arg,i,today);
    update_existing_puzzle(dbc, puzzle_id_arg, s_arg);
  }

  sqlite3_close(dbc);

}

int is_fail(char * success_arg) {
  return strcmp(success_arg, "f") == 0;
}

int is_pass(char * success_arg) {
  return strcmp(success_arg, "s") == 0;
}

/* check_success_arg takes a string and returns true if it is either 'f' or 's'
 * and false otherwise */
int check_success_arg(char * success_arg) {
  return is_fail(success_arg) || is_pass(success_arg);
}

int check_advance_arg(char * advance_arg) {
  return strcmp(advance_arg, "a") == 0;
}

/* check_puzzle_exists takes a database connection and a string representing a
 * puzzle_id and checks whehter <puzzle_id> in fact represents a puzzle in the
 * database */
int check_puzzle_exists(sqlite3* dbc, char * puzzle_id) {
  sqlite3_stmt * stmt;
  sqlite3_prepare_v2(dbc, puzzle_exists_statement, 50, &stmt, NULL);
  sqlite3_bind_text(stmt,1,puzzle_id,strlen(puzzle_id),NULL);
  int result = sqlite3_step(stmt);
  return result == SQLITE_ROW;
}

/* reset_puzzle_for_failure takes a database connection and a string
 * representing a puzzle id and sets the score for that puzzle to 0 and the
 * next test day to tomorrow, effectively starting the process for that puzzle
 * over */
void reset_puzzle_for_failure(sqlite3* dbc, char * puzzle_id_arg) {

  char puzzle_id[MAX_PUZZLE_LEN];
  strcpy(puzzle_id, puzzle_id_arg); //Copy in because otherwise there's weird behavior after finalize, I think???

  sqlite3_stmt * update_puzzle_stmt;

  char next_test_day[11];
  get_target_day(next_test_day, 1);

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

/* get_score_for_puzzle takes a database connection and a string representing a
 * puzzle id and returns the current score for that puzzle.  The score
 * represents an input to an algorithm to determine how many days in the future
 * to work the puzzle again. */
int get_score_for_puzzle(sqlite3 * dbc, char * puzzle_id){
  printf("GET SCORE FOR: %s\n", puzzle_id);

  sqlite3_stmt * get_score_stmt;
  int score = 0;
  char puzzle_buffer[MAX_PUZZLE_LEN];

  strcpy(puzzle_buffer, puzzle_id);

  int res = sqlite3_prepare_v2(dbc, get_score_for_puzzle_statement,strlen(get_score_for_puzzle_statement) + 20, &get_score_stmt, NULL);

  int resii = sqlite3_bind_text(get_score_stmt, 1, puzzle_buffer, strlen(puzzle_buffer),NULL);

  int result = sqlite3_step(get_score_stmt);
  if(result == SQLITE_ERROR || result != SQLITE_ROW){
    printf("ERROR getting score for puzzle: %s - %d - %s\n", sqlite3_errmsg(dbc), result, puzzle_buffer);
    sqlite3_finalize(get_score_stmt);
    return score;
  }

  score = sqlite3_column_int(get_score_stmt, 0);

  sqlite3_finalize(get_score_stmt);

  return score;
  
}

/* advance_puzzle_on_success takes a database connection and a puzzle id,
 * increments the score for that puzzle, calculates -  based on the updated
 * score - what the next test day should be and then saves this in the database
 * */
void advance_puzzle_on_success(sqlite3* dbc, char * puzzle_id_arg) {

  char puzzle_id[MAX_PUZZLE_LEN];
  strcpy(puzzle_id, puzzle_id_arg);// copy puzzle_id because otherwise it drops after sqlite3_finalize - I think???

  sqlite3_stmt * update_puzzle_stmt;
  int current_score = get_score_for_puzzle(dbc, puzzle_id) + 1;
  int day_offset = fibonacci1(current_score);
  char next_test_day[11];
  get_target_day(next_test_day, day_offset);

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

/* log_result takes a database connection, a string representing a puzzle id
 * and a string indicating success or failure and logs this result in the
 * database */
void log_result(sqlite3 *dbc, char * puzzle_id, char * success_arg) {

  sqlite3_stmt * insert_result_stmt;
  char today[11];
  get_today(today);

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

/* update_existing_puzzle takes a database connection, a string representing a
 * puzzle id and a string representing success or failure, logs this result for
 * the puzzle in the database and then calculates and stores the next day the
 * puzzle should be run */
void update_existing_puzzle(sqlite3* dbc, char * puzzle_id, char * success_arg) {

  if(is_fail(success_arg)){
    reset_puzzle_for_failure(dbc, puzzle_id);
    log_result(dbc, puzzle_id, success_arg);
    printf("Puzzle %s reset for failure\n", puzzle_id);
    char * stats = get_stats(dbc);
    puts(stats);
    free(stats);
  } else {
    advance_puzzle_on_success(dbc, puzzle_id);
    log_result(dbc, puzzle_id, success_arg);
    printf("Puzzle %s incremented for success\n", puzzle_id);
    char * stats = get_stats(dbc);
    puts(stats);
    free(stats);
  } 


}

void create_new_puzzle_entry(sqlite3* dbc, char * puzzle_id, char * success_arg) {

  sqlite3_stmt * insert_puzzle_stmt;

  char next_test_day[11];
  get_target_day(next_test_day, 1);

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

/* update_puzzle is a driver function that takes a string representing a puzzle
 * id and a string representing success or failure and calls the appropriate
 * function to log it according to whether the puzzle already exists in the
 * database or not */
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

void show_stats() {

  sqlite3 * dbc = get_db_conn();

  char * stats = get_stats(dbc);
  puts(stats);
  free(stats);
  sqlite3_close(dbc);

}

/* mark_current_puzzle takes a success_arg - a string of either 'f' or 's' -
 * fetches the current puzzle and updates it as a failure or a success
 * according to the success argument  */
void mark_current_puzzle(char * success_arg) {

  sqlite3 * dbc = get_db_conn();
  char puzzle_id[MAX_PUZZLE_LEN];
  current_puzzle(dbc, puzzle_id);
  update_existing_puzzle(dbc, puzzle_id, success_arg);
  sqlite3_close(dbc);

}


/* set_puzzle_date takes a database connection, a string representing a puzzle
 * id and a string representing a target day and reassigned the puzzle
 * identified by <puzzle_id> to the <target_da>.  This is a convenience
 * function used by advance_current_puzzle */
void set_puzzle_date(sqlite3 * dbc, char * puzzle_id, char * target_day) {

  sqlite3_stmt * set_date_stmt;
  sqlite3_prepare_v2(dbc, set_puzzle_date_statement, strlen(set_puzzle_date_statement)+50,&set_date_stmt,NULL);

  sqlite3_bind_text(set_date_stmt,1,target_day,strlen(target_day),NULL);
  sqlite3_bind_text(set_date_stmt,2,puzzle_id,strlen(puzzle_id),NULL);

  int result = sqlite3_step(set_date_stmt);
  if(result == SQLITE_ERROR || result != SQLITE_DONE){
    printf("ERROR setting date to %s on puzzle  %s: %s\n", target_day, puzzle_id, sqlite3_errmsg(dbc));
  }
  sqlite3_finalize(set_date_stmt);


  sqlite3_close(dbc);

}

/* advance_current_puzzle takes an int <days> and advances the next puzzle to
 * be worked <days> days from today instead of today */
void advance_current_puzzle(int days) {

  sqlite3 * dbc = get_db_conn();
  char puzzle_id[MAX_PUZZLE_LEN];
  current_puzzle(dbc, puzzle_id);
  char target_day[11];
  get_target_day(target_day, days);
  set_puzzle_date(dbc, puzzle_id, target_day);

}

/* get_puzzle_id takes a string believed to represent a puzzle id and scans it
 * to accept only integers, returning the string of integers from the original
 * string */
void get_puzzle_id(char * buffer, char * command_arg){

  int i = 0;
  char * nums = "0123456789";
  char * pch = strpbrk(command_arg, nums);
  while(pch != NULL){
    buffer[i] = *pch;
    i++;
    pch = strpbrk(pch+1, nums);
  }
  buffer[i] = '\0';
}

/* delete_puzzle takes a puzzle_id and creates a database connecton which it
 * uses to delete the puzzle from the database completely, including records of
 * results */
void delete_puzzle(char * puzzle_id) {
  sqlite3 * dbc = get_db_conn();
  sqlite3_stmt * delete_puzzle_stmt;
  sqlite3_stmt * delete_puzzle_results_stmt;
  sqlite3_prepare_v2(dbc, delete_puzzle_from_puzzles_statement, strlen(delete_puzzle_from_puzzles_statement), &delete_puzzle_stmt, NULL);
  sqlite3_prepare_v2(dbc, delete_puzzle_from_results_statement, strlen(delete_puzzle_from_results_statement), &delete_puzzle_results_stmt, NULL);

  sqlite3_bind_text(delete_puzzle_stmt,1,puzzle_id,strlen(puzzle_id),NULL);
  sqlite3_bind_text(delete_puzzle_results_stmt,1,puzzle_id,strlen(puzzle_id),NULL);


  sqlite3_exec(dbc, begin_transaction_statement, NULL, NULL, NULL);
  int result = sqlite3_step(delete_puzzle_stmt);
  if(result == SQLITE_ERROR) {
    printf("ERROR deleting puzzle: %s\n", sqlite3_errmsg(dbc));
    sqlite3_exec(dbc, rollback_transaction_statememt, NULL, NULL, NULL);
    return;
  }

  result = sqlite3_step(delete_puzzle_results_stmt);
  if(result == SQLITE_ERROR) {
    printf("ERROR deleting puzzle: %s\n", sqlite3_errmsg(dbc));
    sqlite3_exec(dbc, rollback_transaction_statememt, NULL, NULL, NULL);
    return;
  }

  sqlite3_exec(dbc, commit_transaction_statement, NULL, NULL, NULL);
  sqlite3_close(dbc);

}

void show_upcoming() {

  sqlite3 * dbc = get_db_conn();
  sqlite3_stmt * upcomming_puzzles_count_stmt;
  
  sqlite3_prepare_v2(dbc,get_upcomming_puzzles_count_by_date,strlen(get_upcomming_puzzles_count_by_date),&upcomming_puzzles_count_stmt,NULL);
  while(sqlite3_step(upcomming_puzzles_count_stmt) == SQLITE_ROW){
    const char * fmt = "%s - %s\n";
    char output[20];
    const char * date = sqlite3_column_text(upcomming_puzzles_count_stmt,0);
    const char * test_count = sqlite3_column_text(upcomming_puzzles_count_stmt,1);
    sprintf(output, fmt, date, test_count);
    puts(output);
  }

  sqlite3_finalize(upcomming_puzzles_count_stmt);

  sqlite3_close(dbc);

}

int main(int argc, char** argv) {

  char * command_arg;
  char * success_arg;
  if(argc > 3){
    print_useage();
    return 0;
  }


  if(argc == 1){
    get_next();
    return 0;
  }

  command_arg = argv[1];

  if(argc == 2) {

    if(strcmp(command_arg, "next") == 0){
      get_next();
      return 0;
    } 

    if(strcmp(command_arg, "stats") == 0){
      show_stats();
      return 0;
    }

    if(strcmp(command_arg, "future") == 0){
      show_upcoming();
      return 0;
    }

    if(strcmp(command_arg, "useage") == 0){
      print_useage();
      return 0;
    }

    // Argument is a string of 's' and 'f' and represents a batch update
    if(check_success_string_arg(command_arg)){
      record_batch_results(command_arg);
      return 0;
    }

    //Single argument is s or f, so update the current test
    if(check_success_arg(command_arg)){
      mark_current_puzzle(command_arg);
      return 0;
    }

    if(check_advance_arg(command_arg)){
      advance_current_puzzle(1);
      return 0;
    }

    print_useage();
    return 0;

  }

  success_arg = argv[2];

  if(strcmp(command_arg, "delete") == 0){
    delete_puzzle(success_arg);
    return 0;
  }

  if(strcmp(command_arg, "n") == 0 && strlen(success_arg) < 10 && isdigit(success_arg[0])){
    get_next_count(atoi(success_arg));
    return 0;
  }


  if(!check_success_arg(success_arg)){
    print_useage();
    return 0;
  }

  char puzzle_id[MAX_PUZZLE_LEN];
  get_puzzle_id(puzzle_id, command_arg);
  update_puzzle(puzzle_id, success_arg);

  return 0;

}
