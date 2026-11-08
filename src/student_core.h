/* Shared student-record engine used by both the console and Win32 GUI. */
#ifndef STUDENT_CORE_H
#define STUDENT_CORE_H

#include <stddef.h>
#include <stdio.h>

#define MAX_STUDENTS 10000
#define NAME_LEN 80
#define CLASS_LEN 32
#define MAX_SESSIONS 10000
#define PASS_MARK 40.0
#define MIN_ATTENDANCE 75.0
#define PATH_CAP 512
#define DERIVED_PATH_CAP (PATH_CAP + 32)
#define LINE_CAP 2048
#define CSV_FIELDS 10
#define CSV_FIELD_CAP 512
#define STORAGE_HEADER "roll,name,class,marks,attended,total"
#define REPORT_HEADER "roll,name,class,marks,grade,result,attended,total,attendance_percent"

typedef struct {
    int roll;
    char name[NAME_LEN];
    char class_name[CLASS_LEN];
    double marks;
    int attended;
    int total;
} Student;

typedef struct {
    Student entries[MAX_STUDENTS];
    size_t count;
    char path[PATH_CAP];
    char backup_path[DERIVED_PATH_CAP];
    char legacy_path[PATH_CAP];
    char migration_source[PATH_CAP];
    int migrate_legacy;
    int migrated_from_legacy;
    int created_new;
} Database;

typedef struct {
    size_t count;
    double sum, minimum, maximum;
    size_t passed, recorded, low_attendance;
    int attended, total;
    size_t grades[7];
} Summary;

void sms_set_error_stream(FILE *stream);
const char *sms_last_error(void);
void sms_clear_error(void);

int sms_file_exists(const char *path);
int sms_copy_file(const char *source, const char *destination);
int sms_same_path(const char *a, const char *b);
int sms_text_equal(const char *a, const char *b);
int sms_text_contains(const char *haystack, const char *needle);
void sms_trim(char *text);
int sms_copy_trimmed(char *destination, size_t capacity, const char *source);
int sms_parse_integer(const char *text, int minimum, int maximum, int *value);
int sms_parse_number(const char *text, double minimum, double maximum, double *value);
int sms_valid_text(const char *text);
int sms_valid_student(const Student *student, int require_roll);
int sms_load_csv(const char *path, Student *entries, size_t *count, int allow_blank_roll);
int sms_load_legacy(const char *path, Student *entries, size_t *count);
int sms_save_records(Database *db, const Student *entries, size_t count);
int sms_initialize_database(Database *db);
size_t sms_grade_index(double marks);
const char *sms_grade(double marks);
double sms_attendance_percent(const Student *student);
size_t sms_find_roll(const Database *db, int roll);
int sms_next_roll(const Student *entries, size_t count);
Summary sms_summarize(const Student *entries, size_t count);
int sms_compare_roll(const void *a, const void *b);
int sms_compare_name(const void *a, const void *b);
int sms_compare_marks(const void *a, const void *b);
int sms_compare_class(const void *a, const void *b);
Student *sms_sorted_copy(const Database *db, int (*compare)(const void *, const void *));
int sms_protected_output_path(const Database *db, const char *path);

int sms_add_student(Database *db, const Student *input);
int sms_update_student(Database *db, int roll, const Student *replacement);
int sms_delete_student(Database *db, int roll);
int sms_mark_attendance(Database *db, int roll, int present);
int sms_mark_class_attendance_csv(Database *db, const char *class_name, const char *absent_list,
                                  size_t *marked, size_t *present_count);
int sms_import_csv(Database *db, const char *path, size_t *added, size_t *skipped);
int sms_export_report(const Database *db, const char *path);
int sms_backup_database(const Database *db, const char *path);
int sms_restore_database(Database *db, const char *path);

#endif
