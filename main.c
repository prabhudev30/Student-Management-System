/*
 * Student Management System -- console front end.
 * The portable record engine lives in src/student_core.c and is shared with
 * the native Win32 GUI. The console workflow and CSV contract are preserved.
 */
#include "src/student_core.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static Database db;

/* 1 = read, 0 = end of input, -1 = overlong line (already discarded). */
static int read_line(char *buffer, size_t capacity)
{
    size_t length;
    if (fgets(buffer, (int)capacity, stdin) == NULL) return 0;
    length = strcspn(buffer, "\n");
    if (buffer[length] == '\n') buffer[length] = '\0';
    else if (!feof(stdin)) {
        int c;
        while ((c = getchar()) != '\n' && c != EOF) { }
        return -1;
    }
    sms_trim(buffer);
    return 1;
}

static int prompt_line(const char *prompt, char *buffer, size_t capacity,
                       int allow_empty)
{
    for (;;) {
        int status;
        fputs(prompt, stdout);
        fflush(stdout);
        status = read_line(buffer, capacity);
        if (status == 0) return 0;
        if (status < 0) {
            printf("Input is too long; maximum field size is %zu bytes.\n", capacity - 1);
            continue;
        }
        if (buffer[0] != '\0' || allow_empty) return 1;
        puts("This value is required.");
    }
}

static int prompt_integer(const char *prompt, int minimum, int maximum,
                          const int *fallback, int *value)
{
    char line[64];
    for (;;) {
        if (!prompt_line(prompt, line, sizeof line, fallback != NULL)) return 0;
        if (line[0] == '\0' && fallback != NULL) {
            *value = *fallback;
            return 1;
        }
        if (sms_parse_integer(line, minimum, maximum, value)) return 1;
        printf("Enter a whole number from %d to %d.\n", minimum, maximum);
    }
}

static int prompt_number(const char *prompt, double minimum, double maximum,
                         const double *fallback, double *value)
{
    char line[64];
    for (;;) {
        if (!prompt_line(prompt, line, sizeof line, fallback != NULL)) return 0;
        if (line[0] == '\0' && fallback != NULL) {
            *value = *fallback;
            return 1;
        }
        if (sms_parse_number(line, minimum, maximum, value)) return 1;
        printf("Enter a number from %.2f to %.2f, without units or symbols.\n",
               minimum, maximum);
    }
}

static int prompt_roll(int *roll)
{
    return prompt_integer("Roll number: ", 1, INT_MAX, NULL, roll);
}

static int confirm(const char *prompt)
{
    char answer[16];
    if (!prompt_line(prompt, answer, sizeof answer, 1)) return 0;
    return sms_text_equal(answer, "y") || sms_text_equal(answer, "yes");
}

static void print_attendance(const Student *student)
{
    double percent = sms_attendance_percent(student);
    if (percent < 0.0) printf("Not recorded");
    else printf("%.2f%% (%d/%d)", percent, student->attended, student->total);
}

static void print_table_header(void)
{
    printf("\n%-7s %-30s %-16s %7s %-6s %-24s %s\n",
           "Roll", "Name", "Class", "Marks", "Grade", "Attendance", "Result");
    puts("------------------------------------------------------------------------------------------------------------");
}

static void print_student(const Student *student)
{
    char attendance[64];
    if (student->total == 0) snprintf(attendance, sizeof attendance, "Not recorded");
    else snprintf(attendance, sizeof attendance, "%.2f%% (%d/%d)",
                  sms_attendance_percent(student), student->attended, student->total);
    printf("%-7d %-30s %-16s %7.2f %-6s %-24s %s\n", student->roll, student->name,
           student->class_name, student->marks, sms_grade(student->marks), attendance,
           student->marks >= PASS_MARK ? "PASS" : "FAIL");
}

static void print_table(const Student *entries, size_t count)
{
    size_t i;
    if (count == 0) {
        puts("No student records found. Add a student or import a CSV file.");
        return;
    }
    print_table_header();
    for (i = 0; i < count; ++i) print_student(&entries[i]);
    printf("%zu record(s).\n", count);
}

static int get_existing_roll(size_t *index)
{
    int roll;
    if (!prompt_roll(&roll)) return 0;
    *index = sms_find_roll(&db, roll);
    if (*index == SIZE_MAX) {
        printf("No student found with roll number %d.\n", roll);
        return 0;
    }
    return 1;
}

static void print_summary(const Summary *s)
{
    size_t i;
    static const char *labels[] = {"A+", "A", "B", "C", "D", "E", "F"};
    printf("\n===== Automatic class summary =====\nStudents:       %zu\n", s->count);
    if (s->count == 0) {
        puts("No data yet; averages and percentages are not available.");
        return;
    }
    printf("Average marks:  %.2f / 100\nHighest marks:  %.2f\nLowest marks:   %.2f\n",
           s->sum / (double)s->count, s->maximum, s->minimum);
    printf("Passed / failed: %zu / %zu\nPass percentage: %.2f%% (pass mark: %.0f)\n",
           s->passed, s->count - s->passed, 100.0 * (double)s->passed / (double)s->count, PASS_MARK);
    fputs("Grade counts:   ", stdout);
    for (i = 0; i < 7; ++i) printf("%s=%zu%s", labels[i], s->grades[i], i == 6 ? "\n" : ", ");
    if (s->recorded == 0) puts("Attendance:     Not recorded");
    else {
        printf("Attendance:     %.2f%% (%d attended / %d held, across %zu recorded student(s))\n",
               100.0 * s->attended / s->total, s->attended, s->total, s->recorded);
        printf("Below 75%%:      %zu student(s)\n", s->low_attendance);
    }
    printf("Not recorded:   %zu student(s)\n", s->count - s->recorded);
}

static void print_rankings(void)
{
    Student *ranked = sms_sorted_copy(&db, sms_compare_marks);
    size_t i;
    int rank = 0;
    if (ranked == NULL) return;
    if (db.count == 0) puts("No records to rank.");
    else {
        printf("\n===== Rank list (equal marks share a rank) =====\n");
        printf("%-6s %-7s %-30s %-16s %7s %s\n", "Rank", "Roll", "Name", "Class", "Marks", "Grade");
        for (i = 0; i < db.count; ++i) {
            if (i == 0 || ranked[i].marks != ranked[i - 1].marks) rank = (int)i + 1;
            printf("%-6d %-7d %-30s %-16s %7.2f %s\n", rank, ranked[i].roll,
                   ranked[i].name, ranked[i].class_name, ranked[i].marks, sms_grade(ranked[i].marks));
        }
    }
    free(ranked);
}

static void print_report_card(const Student *s)
{
    printf("\n===== Student report card =====\nRoll number: %d\nName:        %s\nClass:       %s\n",
           s->roll, s->name, s->class_name);
    printf("Marks:       %.2f / 100\nGrade:       %s\nResult:      %s\nAttendance:  ",
           s->marks, sms_grade(s->marks), s->marks >= PASS_MARK ? "PASS" : "FAIL");
    print_attendance(s);
    putchar('\n');
    if (s->total > 0 && sms_attendance_percent(s) < MIN_ATTENDANCE)
        puts("Warning:     Attendance is below 75%.");
    if (s->marks < PASS_MARK) puts("Warning:     Marks are below the pass mark.");
}

static void print_class_summaries(void)
{
    size_t i, j;
    Student *subset;
    if (db.count == 0) {
        puts("No class data yet.");
        return;
    }
    subset = malloc(db.count * sizeof *subset);
    if (subset == NULL) {
        puts("Not enough memory for this report.");
        return;
    }
    printf("\n%-20s %8s %10s %10s %12s\n", "Class", "Students", "Average", "Pass %", "Attendance");
    for (i = 0; i < db.count; ++i) {
        size_t count = 0;
        Summary s;
        int already_done = 0;
        for (j = 0; j < i; ++j)
            if (sms_text_equal(db.entries[i].class_name, db.entries[j].class_name)) already_done = 1;
        if (already_done) continue;
        for (j = i; j < db.count; ++j)
            if (sms_text_equal(db.entries[i].class_name, db.entries[j].class_name)) subset[count++] = db.entries[j];
        s = sms_summarize(subset, count);
        printf("%-20s %8zu %10.2f %9.2f%% ", db.entries[i].class_name, count,
               s.sum / (double)count, 100.0 * (double)s.passed / (double)count);
        if (s.recorded == 0) printf("%12s\n", "N/A");
        else printf("%11.2f%%\n", 100.0 * s.attended / s.total);
    }
    free(subset);
}

static void print_low_attendance(void)
{
    size_t i, count = 0, unrecorded = 0;
    for (i = 0; i < db.count; ++i) {
        const Student *s = &db.entries[i];
        if (s->total == 0) ++unrecorded;
        else if (sms_attendance_percent(s) < MIN_ATTENDANCE) {
            if (count++ == 0) print_table_header();
            print_student(s);
        }
    }
    printf("\n%zu student(s) below 75%% attendance; %zu with no attendance recorded.\n", count, unrecorded);
}

static void print_at_risk(void)
{
    size_t i, count = 0;
    for (i = 0; i < db.count; ++i) {
        const Student *s = &db.entries[i];
        if (s->marks < PASS_MARK || (s->total > 0 && sms_attendance_percent(s) < MIN_ATTENDANCE)) {
            if (count++ == 0) print_table_header();
            print_student(s);
        }
    }
    if (count == 0) puts("No students are below the pass mark or attendance threshold.");
    else printf("%zu student(s) need attention. Missing attendance is not treated as zero.\n", count);
}

static int prompt_attendance(int *attended, int *total)
{
    for (;;) {
        if (!prompt_integer("Classes attended: ", 0, MAX_SESSIONS, NULL, attended) ||
            !prompt_integer("Total classes held: ", 0, MAX_SESSIONS, NULL, total)) return 0;
        if (*attended <= *total) return 1;
        puts("Classes attended cannot exceed classes held. Please enter the pair again.");
    }
}

static void add_student(void)
{
    Student student;
    int roll;
    memset(&student, 0, sizeof student);
    if (db.count >= MAX_STUDENTS) {
        puts("The database limit of 10000 students has been reached.");
        return;
    }
    roll = sms_next_roll(db.entries, db.count);
    if (roll < 0) {
        puts("No automatic roll numbers remain. Existing roll numbers are preserved.");
        return;
    }
    printf("\nAdding a student. Automatic roll number: %d\n", roll);
    if (!prompt_line("Full name: ", student.name, NAME_LEN, 0) ||
        !prompt_line("Class / section (blank = General): ", student.class_name, CLASS_LEN, 1) ||
        !prompt_number("Marks out of 100: ", 0.0, 100.0, NULL, &student.marks)) return;
    if (student.class_name[0] == '\0') strcpy(student.class_name, "General");
    if (!sms_valid_text(student.name) || !sms_valid_text(student.class_name)) {
        puts("Names and classes cannot contain control characters.");
        return;
    }
    if (confirm("Record attendance now? (y/N): ") &&
        !prompt_attendance(&student.attended, &student.total)) return;
    roll = sms_add_student(&db, &student);
    if (roll < 0) {
        puts(sms_last_error());
        puts("Add cancelled; the in-memory records are unchanged.");
        return;
    }
    printf("Student added and saved automatically. Roll: %d; grade: %s; result: %s.\n",
           roll, sms_grade(student.marks), student.marks >= PASS_MARK ? "PASS" : "FAIL");
}

static void view_students(void)
{
    int choice, (*compare)(const void *, const void *) = sms_compare_roll;
    Student *copy;
    if (!prompt_integer("Sort by: 1=Roll, 2=Name, 3=Marks (high first), 4=Class: ",
                        1, 4, NULL, &choice)) return;
    if (choice == 2) compare = sms_compare_name;
    else if (choice == 3) compare = sms_compare_marks;
    else if (choice == 4) compare = sms_compare_class;
    copy = sms_sorted_copy(&db, compare);
    if (copy != NULL) {
        print_table(copy, db.count);
        free(copy);
    }
}

static void search_students(void)
{
    int choice, roll;
    char query[NAME_LEN];
    size_t i, count = 0;
    if (!prompt_integer("Search by: 1=Roll, 2=Name, 3=Class: ", 1, 3, NULL, &choice)) return;
    if (choice == 1) {
        size_t index;
        if (!prompt_roll(&roll)) return;
        index = sms_find_roll(&db, roll);
        if (index == SIZE_MAX) puts("No matching student.");
        else print_report_card(&db.entries[index]);
        return;
    }
    if (!prompt_line(choice == 2 ? "Full or partial name: " : "Full or partial class: ",
                     query, sizeof query, 0)) return;
    for (i = 0; i < db.count; ++i) {
        const char *field = choice == 2 ? db.entries[i].name : db.entries[i].class_name;
        if (sms_text_contains(field, query)) {
            if (count++ == 0) print_table_header();
            print_student(&db.entries[i]);
        }
    }
    if (count == 0) puts("No matching students.");
    else printf("%zu matching student(s).\n", count);
}

static void update_student(void)
{
    size_t index;
    int choice;
    Student old, candidate;
    char line[NAME_LEN];
    if (!get_existing_roll(&index)) return;
    old = db.entries[index];
    candidate = old;
    print_report_card(&old);
    if (!prompt_integer("Update: 1=Name, 2=Class, 3=Marks, 4=Attendance totals, 5=All, 0=Cancel: ",
                        0, 5, NULL, &choice) || choice == 0) return;
    if (choice == 1 || choice == 5) {
        if (!prompt_line("New name (blank keeps current): ", line, NAME_LEN, 1)) return;
        if (line[0] != '\0') strcpy(candidate.name, line);
    }
    if (choice == 2 || choice == 5) {
        if (!prompt_line("New class (blank keeps current): ", line, CLASS_LEN, 1)) return;
        if (line[0] != '\0') strcpy(candidate.class_name, line);
    }
    if (choice == 3 || choice == 5) {
        if (!prompt_number("New marks (blank keeps current): ", 0.0, 100.0,
                           &old.marks, &candidate.marks)) return;
    }
    if (choice == 4 || choice == 5) {
        if (!prompt_attendance(&candidate.attended, &candidate.total)) return;
    }
    if (!sms_update_student(&db, old.roll, &candidate)) {
        puts(sms_last_error());
        puts("Update cancelled; the previous record is retained.");
        return;
    }
    puts("Student updated and saved automatically. Grade and result recalculated.");
    print_report_card(&db.entries[index]);
}

static void delete_student(void)
{
    size_t index;
    char prompt[NAME_LEN + 64];
    if (!get_existing_roll(&index)) return;
    print_student(&db.entries[index]);
    snprintf(prompt, sizeof prompt, "Delete %s permanently? (yes/N): ", db.entries[index].name);
    if (!confirm(prompt)) {
        puts("Delete cancelled.");
        return;
    }
    if (!sms_delete_student(&db, db.entries[index].roll)) {
        puts(sms_last_error());
        puts("Delete cancelled; the previous records are retained.");
        return;
    }
    printf("Student deleted and saved automatically. Previous version: %s\n", db.backup_path);
}

static void mark_individual_attendance(void)
{
    size_t index;
    char status[16];
    int present;
    if (!get_existing_roll(&index)) return;
    if (db.entries[index].total == MAX_SESSIONS) {
        puts("The attendance session limit has been reached; correct totals instead.");
        return;
    }
    if (!prompt_line("Attendance: P=Present, A=Absent: ", status, sizeof status, 0)) return;
    present = sms_text_equal(status, "p") || sms_text_equal(status, "present");
    if (!present && !sms_text_equal(status, "a") && !sms_text_equal(status, "absent")) {
        puts("Invalid status. Enter P or A; no session was recorded.");
        return;
    }
    if (!sms_mark_attendance(&db, db.entries[index].roll, present)) {
        puts(sms_last_error());
        puts("Session not saved; the previous totals are retained.");
        return;
    }
    fputs("Session saved. Updated attendance: ", stdout);
    print_attendance(&db.entries[index]);
    putchar('\n');
}

static void mark_class_attendance(void)
{
    char class_name[CLASS_LEN], line[LINE_CAP];
    size_t marked, present;
    if (!prompt_line("Class / section for this session: ", class_name, sizeof class_name, 0)) return;
    printf("%zu student(s) found. Everyone defaults to present.\n", db.count);
    if (!prompt_line("Absent rolls separated by commas (blank/none = all present, all = all absent): ",
                     line, sizeof line, 1)) return;
    if (!sms_mark_class_attendance_csv(&db, class_name, line, &marked, &present)) {
        puts(sms_last_error());
        return;
    }
    printf("Class session saved automatically: %zu present, %zu absent, %zu total.\n",
           present, marked - present, marked);
    printf("Overall attendance percentages recalculated for all %zu student(s).\n", marked);
}

static void correct_attendance(void)
{
    size_t index;
    Student old;
    if (!get_existing_roll(&index)) return;
    old = db.entries[index];
    fputs("Current attendance: ", stdout);
    print_attendance(&old);
    putchar('\n');
    if (!prompt_attendance(&db.entries[index].attended, &db.entries[index].total)) {
        db.entries[index] = old;
        return;
    }
    if (!sms_save_records(&db, db.entries, db.count)) {
        db.entries[index] = old;
        puts("Correction cancelled; previous attendance is retained.");
        return;
    }
    puts("Attendance corrected and saved automatically.");
}

static void attendance_menu(void)
{
    int choice;
    if (!prompt_integer("\nAttendance: 1=Individual session, 2=Whole-class session, 3=Correct totals, 4=Below 75% report, 0=Back: ",
                        0, 4, NULL, &choice)) return;
    switch (choice) {
        case 1: mark_individual_attendance(); break;
        case 2: mark_class_attendance(); break;
        case 3: correct_attendance(); break;
        case 4: print_low_attendance(); break;
        default: break;
    }
}

static void reports_menu(void)
{
    int choice;
    size_t index;
    Summary summary;
    if (!prompt_integer("\nReports: 1=Overall summary, 2=Class summaries, 3=Rank list, 4=Report card, 5=Students needing attention, 0=Back: ",
                        0, 5, NULL, &choice)) return;
    switch (choice) {
        case 1:
            summary = sms_summarize(db.entries, db.count);
            print_summary(&summary);
            break;
        case 2: print_class_summaries(); break;
        case 3: print_rankings(); break;
        case 4:
            if (get_existing_roll(&index)) print_report_card(&db.entries[index]);
            break;
        case 5: print_at_risk(); break;
        default: break;
    }
}

static int protected_output_path(const char *path)
{
    if (sms_protected_output_path(&db, path)) {
        puts("Choose a separate output file; the database, backup and legacy source are protected.");
        return 1;
    }
    return 0;
}

static int prompt_output_path(const char *prompt, const char *fallback, char *path)
{
    int exists;
    if (!prompt_line(prompt, path, PATH_CAP, 1)) return 0;
    if (path[0] == '\0') snprintf(path, PATH_CAP, "%s", fallback);
    if (protected_output_path(path)) return 0;
    exists = sms_file_exists(path);
    if (exists < 0) {
        puts("That output path is not accessible.");
        return 0;
    }
    if (exists && !confirm("Output file exists. Overwrite it? (y/N): ")) {
        puts("Operation cancelled; output file unchanged.");
        return 0;
    }
    return 1;
}

static void export_report(void)
{
    char path[PATH_CAP];
    if (!prompt_output_path("Export file (blank = student_report.csv): ", "student_report.csv", path)) return;
    if (!sms_export_report(&db, path)) {
        puts(sms_last_error());
        return;
    }
    printf("Exported %zu record(s) to %s. Grades, results and attendance percentages are included.\n", db.count, path);
    puts("This report can also be imported; calculated columns are recomputed automatically.");
}

static void import_records(void)
{
    char path[PATH_CAP];
    size_t added, skipped;
    if (!prompt_line("CSV file to import (blank = student_report.csv): ", path, sizeof path, 1)) return;
    if (path[0] == '\0') strcpy(path, "student_report.csv");
    if (!sms_import_csv(&db, path, &added, &skipped)) {
        puts(sms_last_error());
        return;
    }
    printf("Import complete: %zu added, %zu duplicate roll(s) skipped. Existing records were not overwritten.\n",
           added, skipped);
    if (added > 0) puts("Merged records saved automatically; the previous database is in the automatic backup.");
}

static void backup_now(void)
{
    char path[PATH_CAP];
    if (!prompt_output_path("Backup file (blank = students_backup.csv): ", "students_backup.csv", path)) return;
    if (!sms_backup_database(&db, path)) {
        puts(sms_last_error());
        return;
    }
    printf("Backup created: %s (%zu record(s)).\n", path, db.count);
}

static void restore_backup(void)
{
    char path[DERIVED_PATH_CAP], prompt[DERIVED_PATH_CAP + 80];
    Student *restored;
    size_t count = 0;
    snprintf(prompt, sizeof prompt, "Restore file (blank = %s): ", db.backup_path);
    if (!prompt_line(prompt, path, sizeof path, 1)) return;
    if (path[0] == '\0') memcpy(path, db.backup_path, strlen(db.backup_path) + 1);
    if (sms_same_path(path, db.path)) {
        puts("That is already the live database; nothing to restore.");
        return;
    }
    restored = malloc(MAX_STUDENTS * sizeof *restored);
    if (restored == NULL) {
        puts("Not enough memory for restore.");
        return;
    }
    if (!sms_load_csv(path, restored, &count, 0)) { free(restored); return; }
    free(restored);
    printf("Validated %zu record(s) in %s.\n", count, path);
    if (!confirm("Replace ALL current records with this file? The current version will be backed up. (yes/N): ")) {
        puts("Restore cancelled.");
        return;
    }
    if (!sms_restore_database(&db, path)) {
        puts(sms_last_error());
        return;
    }
    printf("Restored %zu record(s) and saved automatically. Previous version: %s\n", count, db.backup_path);
}

static void print_menu(void)
{
    puts("\n==================================================");
    puts("       AUTOMATIC STUDENT MANAGEMENT SYSTEM");
    puts("==================================================");
    printf("Database: %s | Students: %zu\n", db.path, db.count);
    puts(" 1. Add student (automatic roll, grade and result)");
    puts(" 2. View / sort all students");
    puts(" 3. Search by roll, name or class");
    puts(" 4. Update student record");
    puts(" 5. Delete student (with confirmation)");
    puts(" 6. Attendance (individual or whole class)");
    puts(" 7. Reports, statistics and rankings");
    puts(" 8. Export CSV report");
    puts(" 9. Import / merge CSV records");
    puts("10. Create backup");
    puts("11. Restore validated backup");
    puts(" 0. Exit");
    puts("Confirmed changes save automatically. Ctrl+C / closing the window is not a save operation.");
}

static void print_usage(const char *program)
{
    printf("Usage: %s [--data FILE] [--summary] [--help]\n\n", program);
    puts("  --data FILE  Use a separate CSV database (does not auto-import students.txt).");
    puts("  --summary    Print automatic class statistics and exit without a menu.");
    puts("  --help       Show this help.\n");
    puts("With no options, run the interactive manager using students.csv.");
    puts("On first use, existing students.txt records are migrated automatically.");
}

int main(int argc, char **argv)
{
    int summary_only = 0, data_option = 0, i;
    const char *data_path = "students.csv";
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        } else if (strcmp(argv[i], "--summary") == 0) summary_only = 1;
        else if (strcmp(argv[i], "--data") == 0 && i + 1 < argc && !data_option) {
            data_path = argv[++i];
            data_option = 1;
        } else {
            fprintf(stderr, "Unknown or incomplete option: %s\nTry --help.\n", argv[i]);
            return EXIT_FAILURE;
        }
    }
    if (strlen(data_path) == 0 || strlen(data_path) >= PATH_CAP - 8) {
        fprintf(stderr, "Database path must contain between 1 and %d bytes.\n", PATH_CAP - 9);
        return EXIT_FAILURE;
    }
    snprintf(db.path, sizeof db.path, "%s", data_path);
    snprintf(db.backup_path, sizeof db.backup_path, "%s.bak", db.path);
    db.migrate_legacy = !data_option;
    if (db.migrate_legacy) snprintf(db.legacy_path, sizeof db.legacy_path, "students.txt");
    puts("Student Management System | Persistent CSV storage | Automatic calculations");
    if (!sms_initialize_database(&db)) {
        fprintf(stderr, "Startup stopped safely. Fix the reported file problem and try again.\n");
        return EXIT_FAILURE;
    }
    if (db.migrated_from_legacy) {
        printf("Automatically migrated %zu record(s) from %s to %s.\n", db.count, db.migration_source, db.path);
        puts("The original students.txt is unchanged. Class is 'Unassigned'; attendance is not recorded.");
    } else if (db.created_new) {
        printf("Created a new empty database: %s\n", db.path);
    }
    if (summary_only) {
        Summary summary = sms_summarize(db.entries, db.count);
        print_summary(&summary);
        return EXIT_SUCCESS;
    }
    printf("%zu student record(s) loaded. All confirmed changes are saved automatically.\n", db.count);
    for (;;) {
        char line[32];
        int choice, status;
        print_menu();
        fputs("Enter your choice: ", stdout);
        fflush(stdout);
        status = read_line(line, sizeof line);
        if (status == 0) {
            puts("\nInput ended. Exiting safely; previously confirmed changes are already saved.");
            break;
        }
        if (status < 0 || !sms_parse_integer(line, 0, 11, &choice)) {
            puts("Invalid choice. Enter a number from 0 to 11.");
            continue;
        }
        switch (choice) {
            case 1: add_student(); break;
            case 2: view_students(); break;
            case 3: search_students(); break;
            case 4: update_student(); break;
            case 5: delete_student(); break;
            case 6: attendance_menu(); break;
            case 7: reports_menu(); break;
            case 8: export_report(); break;
            case 9: import_records(); break;
            case 10: backup_now(); break;
            case 11: restore_backup(); break;
            case 0:
                puts("Goodbye. All confirmed student records are saved.");
                return EXIT_SUCCESS;
        }
    }
    return EXIT_SUCCESS;
}
