#define MAX_PUZZLE_LEN 20
#define BASE_INTERVAL 6
#define MAX_SUCCESS 4
#define MAX_INTERVAL 60

void current_puzzle(sqlite3 *, char *);
void get_puzzle_at_offset(sqlite3 *, char *, int, char *);
void get_puzzle_id(char *, char *);
char * get_stats(sqlite3 *);
void get_target_day(char *, int);
void get_today(char*);
int check_advance_arg(char *);
int check_puzzle_exists(sqlite3 * , char *);
int check_success_arg(char *);
int check_success_string_arg(char *);
int database_file_exists(void);
int fibonacci1(int);
int get_score_for_puzzle(sqlite3 *, char *);
int get_total_tests_for_day(sqlite3 *, char *);
int is_fail(char *);
int is_pass(char *);
sqlite3* get_db_conn(void);
struct tm* get_current_time(void);
void advance_current_puzzle(int);
void advance_puzzle_on_success(sqlite3 * , char *);
void create_new_puzzle_entry(sqlite3 *, char *, char *);
void create_tables(sqlite3 *);
void delete_puzzle(char *);
void get_next_count(int);
void get_next(void);
void log_result(sqlite3 *, char *, char *);
void mark_current_puzzle(char *);
void print_error(int, int);
void print_useage(void);
void reset_puzzle_for_failure(sqlite3 *, char *);
void set_puzzle_date(sqlite3 *, char *, char *);
void record_batch_results(char *);
void show_stats(void);
void show_upcoming(void);
void touch_dbfile(void);
void update_existing_puzzle(sqlite3 *, char *, char *);
void update_puzzle(char *, char *);
