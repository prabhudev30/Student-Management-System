#ifndef _WIN32
#define _XOPEN_SOURCE 700
#endif

/*
 * Student Management System -- a portable, file-backed C console application.
 * Build: gcc -std=c11 -Wall -Wextra -Wpedantic main.c -o StudentManagement
 * Run:   ./StudentManagement (Windows: StudentManagement.exe)
 *
 * Only the C standard library and, on Windows, the native file-replacement API
 * are required. Confirmed changes are saved automatically; the previous data
 * file is retained as students.csv.bak. See README.md for the CSV contract.
 */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

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
    int migrate_legacy;
} Database;

typedef struct {
    char fields[CSV_FIELDS][CSV_FIELD_CAP];
    size_t count;
} CsvRow;

typedef struct {
    size_t count;
    double sum, minimum, maximum;
    size_t passed, recorded, low_attendance;
    int attended, total;
    size_t grades[7];
} Summary;

static Database db;

/* --------------------------- Portable file helpers ---------------------- */

static int replace_file(const char *temporary, const char *destination)
{
#ifdef _WIN32
    return MoveFileExA(temporary, destination,
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(temporary, destination) == 0;
#endif
}

/* 1 = exists, 0 = missing, -1 = inaccessible (never confuse with empty data). */
static int file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file != NULL) {
        fclose(file);
        return 1;
    }
    return errno == ENOENT ? 0 : -1;
}

static int copy_file(const char *source, const char *destination)
{
    char buffer[4096];
    size_t amount;
    int ok = 1;
    FILE *in = fopen(source, "rb");
    FILE *out;
    if (in == NULL) return 0;
    out = fopen(destination, "wb");
    if (out == NULL) {
        fclose(in);
        return 0;
    }
    while ((amount = fread(buffer, 1, sizeof buffer, in)) > 0) {
        if (fwrite(buffer, 1, amount, out) != amount) {
            ok = 0;
            break;
        }
    }
    if (ferror(in)) ok = 0;
    if (fclose(in) != 0) ok = 0;
    if (fclose(out) != 0) ok = 0;
    if (!ok) remove(destination);
    return ok;
}

/* Resolve aliases such as ./students.csv before protecting the live file.
 * For a new output file, resolve its parent directory and append its name.
 */
static int canonical_path(const char *path, char *output, size_t capacity)
{
#ifdef _WIN32
    DWORD length = GetFullPathNameA(path, (DWORD)capacity, output, NULL);
    return length > 0 && length < capacity;
#else
    char *resolved = realpath(path, NULL);
    if (resolved != NULL) {
        int fits = strlen(resolved) < capacity;
        if (fits) memcpy(output, resolved, strlen(resolved) + 1);
        free(resolved);
        return fits;
    }
    if (errno == ENOENT) {
        const char *slash = strrchr(path, '/');
        const char *name = slash == NULL ? path : slash + 1;
        char parent[PATH_CAP];
        size_t length = slash == NULL ? 0 : (size_t)(slash - path);
        if (name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0 ||
            length >= sizeof parent) return 0;
        memcpy(parent, path, length);
        parent[length] = '\0';
        resolved = realpath(length == 0 ? "." : parent, NULL);
        if (resolved != NULL) {
            size_t needed = strlen(resolved) + strlen(name) + 2;
            int fits = needed <= capacity;
            if (fits) {
                size_t parent_length = strlen(resolved);
                memcpy(output, resolved, parent_length);
                output[parent_length] = '/';
                memcpy(output + parent_length + 1, name, strlen(name) + 1);
            }
            free(resolved);
            return fits;
        }
    }
    return 0;
#endif
}

static int same_path(const char *a, const char *b)
{
    char full_a[DERIVED_PATH_CAP], full_b[DERIVED_PATH_CAP];
    if (canonical_path(a, full_a, sizeof full_a) &&
        canonical_path(b, full_b, sizeof full_b)) {
#ifdef _WIN32
        return _stricmp(full_a, full_b) == 0;
#else
        return strcmp(full_a, full_b) == 0;
#endif
    }
#ifdef _WIN32
    return _stricmp(a, b) == 0;
#else
    return strcmp(a, b) == 0;
#endif
}

static int ascii_lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static int text_equal(const char *a, const char *b)
{
    while (*a && *b) {
        if (ascii_lower((unsigned char)*a++) != ascii_lower((unsigned char)*b++))
            return 0;
    }
    return *a == *b;
}

static int text_contains(const char *haystack, const char *needle)
{
    size_t length = strlen(needle);
    size_t i;
    if (length == 0) return 1;
    for (i = 0; haystack[i] != '\0'; ++i) {
        size_t j = 0;
        while (j < length && haystack[i + j] != '\0' &&
               ascii_lower((unsigned char)haystack[i + j]) ==
               ascii_lower((unsigned char)needle[j])) ++j;
        if (j == length) return 1;
    }
    return 0;
}

static void trim(char *text)
{
    char *start = text;
    size_t length;
    while (*start && isspace((unsigned char)*start)) ++start;
    length = strlen(start);
    while (length > 0 && isspace((unsigned char)start[length - 1])) --length;
    start[length] = '\0';
    if (start != text) memmove(text, start, length + 1);
}

static int copy_trimmed(char *destination, size_t capacity, const char *source)
{
    size_t start = 0, length = strlen(source);
    while (start < length && isspace((unsigned char)source[start])) ++start;
    while (length > start && isspace((unsigned char)source[length - 1])) --length;
    length -= start;
    if (length == 0 || length >= capacity) return 0;
    memcpy(destination, source + start, length);
    destination[length] = '\0';
    return 1;
}

/* ----------------------------- Validated input -------------------------- */

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
    trim(buffer);
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

static int parse_integer(const char *text, int minimum, int maximum, int *value)
{
    char *end;
    long number;
    if (text[0] == '\0') return 0;
    errno = 0;
    number = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        number < minimum || number > maximum) return 0;
    *value = (int)number;
    return 1;
}

static int parse_number(const char *text, double minimum, double maximum,
                        double *value)
{
    char *end;
    double number;
    if (text[0] == '\0') return 0;
    errno = 0;
    number = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' ||
        !(number >= minimum && number <= maximum)) return 0;
    /* Match the two-decimal storage precision, including after an import. */
    number = (double)((long long)(number * 100.0 + 0.5)) / 100.0;
    *value = number;
    return 1;
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
        if (parse_integer(line, minimum, maximum, value)) return 1;
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
        if (parse_number(line, minimum, maximum, value)) return 1;
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
    return text_equal(answer, "y") || text_equal(answer, "yes");
}

/* ------------------------- CSV and record validation -------------------- */

static int csv_append(CsvRow *row, size_t field, char c)
{
    size_t length = strlen(row->fields[field]);
    if (length + 1 >= CSV_FIELD_CAP) return 0;
    row->fields[field][length] = c;
    row->fields[field][length + 1] = '\0';
    return 1;
}

/* RFC-style single-line CSV: commas, escaped double quotes, and empty fields.
 * Embedded newlines are deliberately excluded because names are single-line.
 */
static int parse_csv_row(const char *line, CsvRow *row)
{
    size_t i, field = 0;
    int quoted = 0, closed_quote = 0;
    memset(row, 0, sizeof *row);
    for (i = 0; line[i] != '\0'; ++i) {
        char c = line[i];
        if ((unsigned char)c < 32) return 0;
        if (quoted) {
            if (c == '"') {
                if (line[i + 1] == '"') {
                    if (!csv_append(row, field, '"')) return 0;
                    ++i;
                } else {
                    quoted = 0;
                    closed_quote = 1;
                }
            } else if (!csv_append(row, field, c)) return 0;
        } else if (closed_quote) {
            if (c != ',') return 0;
            if (++field >= CSV_FIELDS) return 0;
            closed_quote = 0;
        } else if (c == ',') {
            if (++field >= CSV_FIELDS) return 0;
        } else if (c == '"') {
            if (row->fields[field][0] != '\0') return 0;
            quoted = 1;
        } else if (!csv_append(row, field, c)) return 0;
    }
    if (quoted) return 0;
    row->count = field + 1;
    return 1;
}

static void write_csv_field(FILE *file, const char *text)
{
    int quote = strpbrk(text, ",\"\r\n") != NULL;
    if (quote) fputc('"', file);
    for (; *text; ++text) {
        if (*text == '"') fputc('"', file);
        fputc(*text, file);
    }
    if (quote) fputc('"', file);
}

static int valid_text(const char *text)
{
    if (text[0] == '\0') return 0;
    for (; *text; ++text)
        if ((unsigned char)*text < 32 || (unsigned char)*text == 127) return 0;
    return 1;
}

static int valid_student(const Student *student, int require_roll)
{
    return (!require_roll || student->roll > 0) &&
           student->roll >= 0 && valid_text(student->name) &&
           strlen(student->name) < NAME_LEN &&
           valid_text(student->class_name) &&
           strlen(student->class_name) < CLASS_LEN &&
           student->marks >= 0.0 && student->marks <= 100.0 &&
           student->attended >= 0 && student->total >= 0 &&
           student->attended <= student->total && student->total <= MAX_SESSIONS;
}

static int student_from_row(const CsvRow *row, int report, int allow_blank_roll,
                            Student *student)
{
    memset(student, 0, sizeof *student);
    if (row->fields[0][0] == '\0') {
        if (!allow_blank_roll) return 0;
    } else if (!parse_integer(row->fields[0], 1, INT_MAX, &student->roll)) return 0;
    if (!copy_trimmed(student->name, NAME_LEN, row->fields[1]) ||
        !copy_trimmed(student->class_name, CLASS_LEN, row->fields[2]) ||
        !parse_number(row->fields[3], 0.0, 100.0, &student->marks) ||
        !parse_integer(row->fields[report ? 6 : 4], 0, MAX_SESSIONS, &student->attended) ||
        !parse_integer(row->fields[report ? 7 : 5], 0, MAX_SESSIONS, &student->total))
        return 0;
    return valid_student(student, !allow_blank_roll);
}

static void strip_line_end(char *line)
{
    size_t length = strlen(line);
    if (length > 0 && line[length - 1] == '\n') line[--length] = '\0';
    if (length > 0 && line[length - 1] == '\r') line[--length] = '\0';
}

static int read_csv_header(FILE *file, const char *path, int *report)
{
    char line[LINE_CAP];
    CsvRow row;
    static const char *base[] = {"roll", "name", "class", "marks", "attended", "total"};
    static const char *derived[] = {"roll", "name", "class", "marks", "grade", "result",
                                    "attended", "total", "attendance_percent"};
    size_t i;
    if (fgets(line, sizeof line, file) == NULL) {
        fprintf(stderr, "%s: missing CSV header.\n", path);
        return 0;
    }
    if (strchr(line, '\n') == NULL && !feof(file)) {
        fprintf(stderr, "%s: header line is too long.\n", path);
        return 0;
    }
    strip_line_end(line);
    if (strncmp(line, "\xEF\xBB\xBF", 3) == 0)
        memmove(line, line + 3, strlen(line + 3) + 1);
    if (!parse_csv_row(line, &row)) {
        fprintf(stderr, "%s: malformed CSV header.\n", path);
        return 0;
    }
    if (row.count == 6) *report = 0;
    else if (row.count == 9) *report = 1;
    else {
        fprintf(stderr, "%s: expected 6 storage columns or 9 report columns.\n", path);
        return 0;
    }
    for (i = 0; i < row.count; ++i) {
        if (strcmp(row.fields[i], (*report ? derived : base)[i]) != 0) {
            fprintf(stderr, "%s: unsupported header column '%s'. See README.md.\n",
                    path, row.fields[i]);
            return 0;
        }
    }
    return 1;
}

static int duplicate_roll(const Student *entries, size_t count, int roll)
{
    size_t i;
    for (i = 0; i < count; ++i)
        if (entries[i].roll == roll) return 1;
    return 0;
}

static int load_csv(const char *path, Student *entries, size_t *count,
                    int allow_blank_roll)
{
    char line[LINE_CAP];
    CsvRow row;
    FILE *file = fopen(path, "r");
    size_t line_number = 1;
    int report, ok = 1;
    *count = 0;
    if (file == NULL) {
        fprintf(stderr, "Cannot open %s: %s\n", path, strerror(errno));
        return 0;
    }
    if (!read_csv_header(file, path, &report)) {
        fclose(file);
        return 0;
    }
    while (fgets(line, sizeof line, file) != NULL) {
        Student student;
        ++line_number;
        if (strchr(line, '\n') == NULL && !feof(file)) {
            fprintf(stderr, "%s:%zu: line exceeds %d bytes.\n", path, line_number, LINE_CAP - 1);
            ok = 0;
            break;
        }
        strip_line_end(line);
        if (line[0] == '\0') continue;
        if (*count >= MAX_STUDENTS) {
            fprintf(stderr, "%s:%zu: maximum %d records exceeded.\n", path, line_number, MAX_STUDENTS);
            ok = 0;
            break;
        }
        if (!parse_csv_row(line, &row) || row.count != (report ? 9u : 6u) ||
            !student_from_row(&row, report, allow_blank_roll, &student) ||
            (student.roll > 0 && duplicate_roll(entries, *count, student.roll))) {
            fprintf(stderr, "%s:%zu: invalid record or duplicate roll number; no records changed.\n",
                    path, line_number);
            ok = 0;
            break;
        }
        entries[(*count)++] = student;
    }
    if (ferror(file)) ok = 0;
    if (fclose(file) != 0) ok = 0;
    if (!ok) *count = 0;
    return ok;
}

static int load_legacy(const char *path, Student *entries, size_t *count)
{
    char line[LINE_CAP];
    FILE *file = fopen(path, "r");
    size_t line_number = 0;
    int ok = 1;
    *count = 0;
    if (file == NULL) {
        fprintf(stderr, "Cannot open legacy file %s: %s\n", path, strerror(errno));
        return 0;
    }
    while (fgets(line, sizeof line, file) != NULL) {
        Student student;
        char *separator, *name_start, *last_space = NULL, *mark_text, *cursor;
        double marks;
        int roll;
        ++line_number;
        if (strchr(line, '\n') == NULL && !feof(file)) {
            fprintf(stderr, "%s:%zu: line is too long.\n", path, line_number);
            ok = 0;
            break;
        }
        strip_line_end(line);
        trim(line);
        if (line[0] == '\0') continue;
        separator = strpbrk(line, " \t");
        if (*count >= MAX_STUDENTS || separator == NULL) {
            fprintf(stderr, "%s:%zu: invalid legacy record.\n", path, line_number);
            ok = 0;
            break;
        }
        *separator = '\0';
        if (!parse_integer(line, 1, INT_MAX, &roll)) {
            fprintf(stderr, "%s:%zu: invalid roll number.\n", path, line_number);
            ok = 0;
            break;
        }
        name_start = separator + 1;
        for (cursor = name_start; *cursor; ++cursor)
            if (*cursor == ' ' || *cursor == '\t') last_space = cursor;
        if (last_space == NULL) {
            fprintf(stderr, "%s:%zu: expected roll, name and marks.\n", path, line_number);
            ok = 0;
            break;
        }
        mark_text = last_space + 1;
        *last_space = '\0';
        if (!parse_number(mark_text, 0.0, 100.0, &marks)) {
            fprintf(stderr, "%s:%zu: invalid marks.\n", path, line_number);
            ok = 0;
            break;
        }
        memset(&student, 0, sizeof student);
        student.roll = roll;
        if (!copy_trimmed(student.name, NAME_LEN, name_start)) {
            fprintf(stderr, "%s:%zu: missing or overlong name.\n", path, line_number);
            ok = 0;
            break;
        }
        strcpy(student.class_name, "Unassigned");
        student.marks = marks;
        student.attended = student.total = 0;
        if (!valid_student(&student, 1) || duplicate_roll(entries, *count, student.roll)) {
            fprintf(stderr, "%s:%zu: invalid record or duplicate roll.\n", path, line_number);
            ok = 0;
            break;
        }
        entries[(*count)++] = student;
    }
    if (ferror(file)) ok = 0;
    if (fclose(file) != 0) ok = 0;
    if (!ok) *count = 0;
    return ok;
}

/* Write a complete temporary file before touching the live file. A completed
 * backup is also installed by replacement, not by truncating the old backup.
 * POSIX rename and Windows MoveFileEx both support replacing the destination.
 */
static int save_records(const Student *entries, size_t count)
{
    char temporary[DERIVED_PATH_CAP], backup_temporary[DERIVED_PATH_CAP];
    FILE *file;
    size_t i;
    int existing = file_exists(db.path);
    if (existing < 0 || count > MAX_STUDENTS) return 0;
    snprintf(temporary, sizeof temporary, "%s.tmp", db.path);
    memcpy(backup_temporary, db.backup_path, strlen(db.backup_path) + 1);
    strcat(backup_temporary, ".tmp");
    file = fopen(temporary, "w");
    if (file == NULL) {
        fprintf(stderr, "Cannot create %s: %s\n", temporary, strerror(errno));
        return 0;
    }
    fprintf(file, "%s\n", STORAGE_HEADER);
    for (i = 0; i < count; ++i) {
        fprintf(file, "%d,", entries[i].roll);
        write_csv_field(file, entries[i].name);
        fputc(',', file);
        write_csv_field(file, entries[i].class_name);
        fprintf(file, ",%.2f,%d,%d\n", entries[i].marks, entries[i].attended, entries[i].total);
    }
    if (ferror(file)) {
        fclose(file);
        remove(temporary);
        fprintf(stderr, "Write failed; the existing data file was not changed.\n");
        return 0;
    }
    if (fclose(file) != 0) {
        remove(temporary);
        fprintf(stderr, "Could not finish writing; existing data was not changed.\n");
        return 0;
    }
    if (existing) {
        if (!copy_file(db.path, backup_temporary) ||
            !replace_file(backup_temporary, db.backup_path)) {
            remove(backup_temporary);
            remove(temporary);
            fprintf(stderr, "Automatic backup failed; existing data was not changed.\n");
            return 0;
        }
    }
    if (!replace_file(temporary, db.path)) {
        remove(temporary);
        fprintf(stderr, "Could not replace %s; existing data was not changed.\n", db.path);
        return 0;
    }
    return 1;
}

static int initialize_database(void)
{
    int exists = file_exists(db.path), legacy = 0;
    if (exists < 0) {
        fprintf(stderr, "Cannot access %s: %s\n", db.path, strerror(errno));
        return 0;
    }
    if (exists) return load_csv(db.path, db.entries, &db.count, 0);
    if (db.migrate_legacy) {
        legacy = file_exists("students.txt");
        if (legacy < 0) {
            fprintf(stderr, "Cannot access students.txt: %s\n", strerror(errno));
            return 0;
        }
    }
    if (legacy) {
        if (!load_legacy("students.txt", db.entries, &db.count) ||
            !save_records(db.entries, db.count)) return 0;
        printf("Automatically migrated %zu record(s) from students.txt to %s.\n", db.count, db.path);
        puts("The original students.txt is unchanged. Class is 'Unassigned'; attendance is not recorded.");
    } else {
        if (!save_records(db.entries, 0)) return 0;
        printf("Created a new empty database: %s\n", db.path);
    }
    return 1;
}

/* ----------------------- Automatic academic calculations --------------- */

static size_t grade_index(double marks)
{
    if (marks >= 90.0) return 0;
    if (marks >= 80.0) return 1;
    if (marks >= 70.0) return 2;
    if (marks >= 60.0) return 3;
    if (marks >= 50.0) return 4;
    if (marks >= 40.0) return 5;
    return 6;
}

static const char *grade(double marks)
{
    static const char *grades[] = {"A+", "A", "B", "C", "D", "E", "F"};
    return grades[grade_index(marks)];
}

static double attendance_percent(const Student *student)
{
    return student->total == 0 ? -1.0 : 100.0 * student->attended / student->total;
}

static void print_attendance(const Student *student)
{
    double percent = attendance_percent(student);
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
                  attendance_percent(student), student->attended, student->total);
    printf("%-7d %-30s %-16s %7.2f %-6s %-24s %s\n", student->roll, student->name,
           student->class_name, student->marks, grade(student->marks), attendance,
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

static size_t find_roll(int roll)
{
    size_t i;
    for (i = 0; i < db.count; ++i)
        if (db.entries[i].roll == roll) return i;
    return SIZE_MAX;
}

static int get_existing_roll(size_t *index)
{
    int roll;
    if (!prompt_roll(&roll)) return 0;
    *index = find_roll(roll);
    if (*index == SIZE_MAX) {
        printf("No student found with roll number %d.\n", roll);
        return 0;
    }
    return 1;
}

static int next_roll(const Student *entries, size_t count)
{
    int maximum = 0;
    size_t i;
    for (i = 0; i < count; ++i)
        if (entries[i].roll > maximum) maximum = entries[i].roll;
    return maximum == INT_MAX ? -1 : maximum + 1;
}

static Summary summarize(const Student *entries, size_t count)
{
    Summary summary;
    size_t i;
    memset(&summary, 0, sizeof summary);
    summary.count = count;
    if (count == 0) return summary;
    summary.minimum = summary.maximum = entries[0].marks;
    for (i = 0; i < count; ++i) {
        const Student *s = &entries[i];
        double percent = attendance_percent(s);
        summary.sum += s->marks;
        if (s->marks < summary.minimum) summary.minimum = s->marks;
        if (s->marks > summary.maximum) summary.maximum = s->marks;
        if (s->marks >= PASS_MARK) ++summary.passed;
        ++summary.grades[grade_index(s->marks)];
        if (s->total > 0) {
            ++summary.recorded;
            summary.attended += s->attended;
            summary.total += s->total;
            if (percent < MIN_ATTENDANCE) ++summary.low_attendance;
        }
    }
    return summary;
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

static int compare_roll(const void *a, const void *b)
{
    const Student *x = a, *y = b;
    return (x->roll > y->roll) - (x->roll < y->roll);
}

static int compare_name(const void *a, const void *b)
{
    const Student *x = a, *y = b;
    int order = strcmp(x->name, y->name);
    return order ? order : compare_roll(a, b);
}

static int compare_marks(const void *a, const void *b)
{
    const Student *x = a, *y = b;
    if (x->marks > y->marks) return -1;
    if (x->marks < y->marks) return 1;
    return compare_roll(a, b);
}

static int compare_class(const void *a, const void *b)
{
    const Student *x = a, *y = b;
    int order = strcmp(x->class_name, y->class_name);
    return order ? order : compare_roll(a, b);
}

static Student *sorted_copy(int (*compare)(const void *, const void *))
{
    Student *copy = malloc((db.count ? db.count : 1) * sizeof *copy);
    if (copy == NULL) {
        puts("Not enough memory for this report.");
        return NULL;
    }
    memcpy(copy, db.entries, db.count * sizeof *copy);
    qsort(copy, db.count, sizeof *copy, compare);
    return copy;
}

static void print_rankings(void)
{
    Student *ranked = sorted_copy(compare_marks);
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
                   ranked[i].name, ranked[i].class_name, ranked[i].marks, grade(ranked[i].marks));
        }
    }
    free(ranked);
}

static void print_report_card(const Student *s)
{
    printf("\n===== Student report card =====\nRoll number: %d\nName:        %s\nClass:       %s\n",
           s->roll, s->name, s->class_name);
    printf("Marks:       %.2f / 100\nGrade:       %s\nResult:      %s\nAttendance:  ",
           s->marks, grade(s->marks), s->marks >= PASS_MARK ? "PASS" : "FAIL");
    print_attendance(s);
    putchar('\n');
    if (s->total > 0 && attendance_percent(s) < MIN_ATTENDANCE)
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
            if (text_equal(db.entries[i].class_name, db.entries[j].class_name)) already_done = 1;
        if (already_done) continue;
        for (j = i; j < db.count; ++j)
            if (text_equal(db.entries[i].class_name, db.entries[j].class_name)) subset[count++] = db.entries[j];
        s = summarize(subset, count);
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
        else if (attendance_percent(s) < MIN_ATTENDANCE) {
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
        if (s->marks < PASS_MARK || (s->total > 0 && attendance_percent(s) < MIN_ATTENDANCE)) {
            if (count++ == 0) print_table_header();
            print_student(s);
        }
    }
    if (count == 0) puts("No students are below the pass mark or attendance threshold.");
    else printf("%zu student(s) need attention. Missing attendance is not treated as zero.\n", count);
}

/* ------------------------------- CRUD ---------------------------------- */

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
    int roll = next_roll(db.entries, db.count);
    if (db.count >= MAX_STUDENTS) {
        puts("The database limit of 10000 students has been reached.");
        return;
    }
    if (roll < 0) {
        puts("No automatic roll numbers remain. Existing roll numbers are preserved.");
        return;
    }
    memset(&student, 0, sizeof student);
    student.roll = roll;
    printf("\nAdding a student. Automatic roll number: %d\n", roll);
    if (!prompt_line("Full name: ", student.name, NAME_LEN, 0) ||
        !prompt_line("Class / section (blank = General): ", student.class_name, CLASS_LEN, 1) ||
        !prompt_number("Marks out of 100: ", 0.0, 100.0, NULL, &student.marks)) return;
    if (student.class_name[0] == '\0') strcpy(student.class_name, "General");
    if (!valid_text(student.name) || !valid_text(student.class_name)) {
        puts("Names and classes cannot contain control characters.");
        return;
    }
    if (confirm("Record attendance now? (y/N): ") &&
        !prompt_attendance(&student.attended, &student.total)) return;
    db.entries[db.count] = student;
    if (!save_records(db.entries, db.count + 1)) {
        puts("Add cancelled; the in-memory records are unchanged.");
        return;
    }
    ++db.count;
    printf("Student added and saved automatically. Roll: %d; grade: %s; result: %s.\n",
           student.roll, grade(student.marks), student.marks >= PASS_MARK ? "PASS" : "FAIL");
}

static void view_students(void)
{
    int choice, (*compare)(const void *, const void *) = compare_roll;
    Student *copy;
    if (!prompt_integer("Sort by: 1=Roll, 2=Name, 3=Marks (high first), 4=Class: ",
                        1, 4, NULL, &choice)) return;
    if (choice == 2) compare = compare_name;
    else if (choice == 3) compare = compare_marks;
    else if (choice == 4) compare = compare_class;
    copy = sorted_copy(compare);
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
        index = find_roll(roll);
        if (index == SIZE_MAX) puts("No matching student.");
        else print_report_card(&db.entries[index]);
        return;
    }
    if (!prompt_line(choice == 2 ? "Full or partial name: " : "Full or partial class: ",
                     query, sizeof query, 0)) return;
    for (i = 0; i < db.count; ++i) {
        const char *field = choice == 2 ? db.entries[i].name : db.entries[i].class_name;
        if (text_contains(field, query)) {
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
    Student old;
    char line[NAME_LEN];
    if (!get_existing_roll(&index)) return;
    old = db.entries[index];
    print_report_card(&old);
    if (!prompt_integer("Update: 1=Name, 2=Class, 3=Marks, 4=Attendance totals, 5=All, 0=Cancel: ",
                        0, 5, NULL, &choice) || choice == 0) return;
    if (choice == 1 || choice == 5) {
        if (!prompt_line("New name (blank keeps current): ", line, NAME_LEN, 1)) return;
        if (line[0] != '\0') strcpy(db.entries[index].name, line);
    }
    if (choice == 2 || choice == 5) {
        if (!prompt_line("New class (blank keeps current): ", line, CLASS_LEN, 1)) return;
        if (line[0] != '\0') strcpy(db.entries[index].class_name, line);
    }
    if (choice == 3 || choice == 5) {
        if (!prompt_number("New marks (blank keeps current): ", 0.0, 100.0,
                           &old.marks, &db.entries[index].marks)) return;
    }
    if (choice == 4 || choice == 5) {
        if (!prompt_attendance(&db.entries[index].attended, &db.entries[index].total)) {
            db.entries[index] = old;
            return;
        }
    }
    if (!valid_student(&db.entries[index], 1)) {
        db.entries[index] = old;
        puts("Invalid name or class; update cancelled.");
        return;
    }
    if (!save_records(db.entries, db.count)) {
        db.entries[index] = old;
        puts("Update cancelled; the previous record is retained.");
        return;
    }
    puts("Student updated and saved automatically. Grade and result recalculated.");
    print_report_card(&db.entries[index]);
}

static void delete_student(void)
{
    size_t index;
    Student old;
    char prompt[NAME_LEN + 64];
    if (!get_existing_roll(&index)) return;
    old = db.entries[index];
    print_student(&old);
    snprintf(prompt, sizeof prompt, "Delete %s permanently? (yes/N): ", old.name);
    if (!confirm(prompt)) {
        puts("Delete cancelled.");
        return;
    }
    memmove(&db.entries[index], &db.entries[index + 1],
            (db.count - index - 1) * sizeof *db.entries);
    --db.count;
    if (!save_records(db.entries, db.count)) {
        memmove(&db.entries[index + 1], &db.entries[index],
                (db.count - index) * sizeof *db.entries);
        db.entries[index] = old;
        ++db.count;
        puts("Delete cancelled; the previous records are retained.");
        return;
    }
    printf("Student deleted and saved automatically. Previous version: %s\n", db.backup_path);
}

/* ----------------------------- Attendance ------------------------------ */

static void mark_individual_attendance(void)
{
    size_t index;
    Student old;
    char status[16];
    if (!get_existing_roll(&index)) return;
    old = db.entries[index];
    if (old.total == MAX_SESSIONS) {
        puts("The attendance session limit has been reached; correct totals instead.");
        return;
    }
    if (!prompt_line("Attendance: P=Present, A=Absent: ", status, sizeof status, 0)) return;
    if (!text_equal(status, "p") && !text_equal(status, "present") &&
        !text_equal(status, "a") && !text_equal(status, "absent")) {
        puts("Invalid status. Enter P or A; no session was recorded.");
        return;
    }
    ++db.entries[index].total;
    if (text_equal(status, "p") || text_equal(status, "present")) ++db.entries[index].attended;
    if (!save_records(db.entries, db.count)) {
        db.entries[index] = old;
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
    size_t *members, count = 0, i, absent_count = 0;
    int *absent, roll, present_count = 0;
    Student *before;
    char *token;
    if (!prompt_line("Class / section for this session: ", class_name, sizeof class_name, 0)) return;
    members = malloc(MAX_STUDENTS * sizeof *members);
    absent = calloc(MAX_STUDENTS, sizeof *absent);
    if (members == NULL || absent == NULL) {
        free(members);
        free(absent);
        puts("Not enough memory to record this session.");
        return;
    }
    for (i = 0; i < db.count; ++i) {
        if (text_equal(class_name, db.entries[i].class_name)) {
            if (db.entries[i].total == MAX_SESSIONS) {
                printf("Roll %d has reached the session limit. No attendance was changed.\n", db.entries[i].roll);
                free(members);
                free(absent);
                return;
            }
            members[count++] = i;
        }
    }
    if (count == 0) {
        puts("No students in that class.");
        free(members);
        free(absent);
        return;
    }
    printf("%zu student(s) found. Everyone defaults to present.\n", count);
    if (!prompt_line("Absent rolls separated by commas (blank/none = all present, all = all absent): ",
                     line, sizeof line, 1)) {
        free(members);
        free(absent);
        return;
    }
    if (text_equal(line, "all")) {
        for (i = 0; i < count; ++i) absent[absent_count++] = db.entries[members[i]].roll;
    } else if (!text_equal(line, "none") && line[0] != '\0') {
        token = line;
        for (;;) {
            size_t index, j;
            char *comma = strchr(token, ',');
            if (comma != NULL) *comma = '\0';
            trim(token);
            if (!parse_integer(token, 1, INT_MAX, &roll)) {
                puts("Invalid absent roll list. No attendance was changed.");
                goto cancel;
            }
            index = find_roll(roll);
            if (index == SIZE_MAX || !text_equal(class_name, db.entries[index].class_name)) {
                printf("Roll %d is not in this class. No attendance was changed.\n", roll);
                goto cancel;
            }
            for (j = 0; j < absent_count; ++j)
                if (absent[j] == roll) {
                    printf("Roll %d was listed twice. No attendance was changed.\n", roll);
                    goto cancel;
                }
            absent[absent_count++] = roll;
            if (comma == NULL) break;
            token = comma + 1;
        }
    }
    before = malloc(count * sizeof *before);
    if (before == NULL) {
        puts("Not enough memory to record this session.");
        goto cancel;
    }
    for (i = 0; i < count; ++i) {
        Student *s = &db.entries[members[i]];
        int is_absent = 0;
        size_t j;
        before[i] = *s;
        for (j = 0; j < absent_count; ++j)
            if (absent[j] == s->roll) is_absent = 1;
        ++s->total;
        if (!is_absent) {
            ++s->attended;
            ++present_count;
        }
    }
    if (!save_records(db.entries, db.count)) {
        for (i = 0; i < count; ++i) db.entries[members[i]] = before[i];
        puts("Class session cancelled; previous attendance is retained.");
    } else {
        printf("Class session saved automatically: %d present, %zu absent, %zu total.\n",
               present_count, absent_count, count);
        printf("Overall attendance percentages recalculated for all %zu student(s).\n", count);
    }
    free(before);
cancel:
    free(members);
    free(absent);
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
    if (!save_records(db.entries, db.count)) {
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
            summary = summarize(db.entries, db.count);
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

/* ----------------------- Import, export and recovery ------------------- */

static int protected_output_path(const char *path)
{
    if (same_path(path, db.path) || same_path(path, db.backup_path) ||
        (db.migrate_legacy && same_path(path, "students.txt"))) {
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
    exists = file_exists(path);
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
    FILE *file;
    size_t i;
    if (!prompt_output_path("Export file (blank = student_report.csv): ", "student_report.csv", path)) return;
    file = fopen(path, "w");
    if (file == NULL) {
        printf("Cannot create %s: %s\n", path, strerror(errno));
        return;
    }
    fprintf(file, "%s\n", REPORT_HEADER);
    for (i = 0; i < db.count; ++i) {
        const Student *s = &db.entries[i];
        double percent = attendance_percent(s);
        fprintf(file, "%d,", s->roll);
        write_csv_field(file, s->name);
        fputc(',', file);
        write_csv_field(file, s->class_name);
        fprintf(file, ",%.2f,%s,%s,%d,%d,", s->marks, grade(s->marks),
                s->marks >= PASS_MARK ? "PASS" : "FAIL", s->attended, s->total);
        if (percent < 0.0) fprintf(file, "N/A\n");
        else fprintf(file, "%.2f\n", percent);
    }
    if (ferror(file) || fclose(file) != 0) {
        remove(path);
        puts("Export failed; any incomplete output was removed.");
        return;
    }
    printf("Exported %zu record(s) to %s. Grades, results and attendance percentages are included.\n", db.count, path);
    puts("This report can also be imported; calculated columns are recomputed automatically.");
}

static void import_records(void)
{
    char path[PATH_CAP];
    Student *incoming, *merged;
    size_t incoming_count = 0, merged_count = db.count, i, added = 0, skipped = 0;
    if (!prompt_line("CSV file to import (blank = student_report.csv): ", path, sizeof path, 1)) return;
    if (path[0] == '\0') strcpy(path, "student_report.csv");
    incoming = malloc(MAX_STUDENTS * sizeof *incoming);
    merged = malloc(MAX_STUDENTS * sizeof *merged);
    if (incoming == NULL || merged == NULL) {
        free(incoming);
        free(merged);
        puts("Not enough memory for import.");
        return;
    }
    if (!load_csv(path, incoming, &incoming_count, 1)) goto done;
    memcpy(merged, db.entries, db.count * sizeof *merged);
    /* Reserve explicit incoming rolls first, so a blank roll cannot consume a
     * number that a later row explicitly requests. Blank rolls are appended.
     */
    for (i = 0; i < incoming_count; ++i) {
        if (incoming[i].roll == 0) continue;
        if (duplicate_roll(merged, merged_count, incoming[i].roll)) {
            ++skipped;
            continue;
        }
        if (merged_count >= MAX_STUDENTS) {
            printf("Import would exceed %d students. No records were changed.\n", MAX_STUDENTS);
            goto done;
        }
        merged[merged_count++] = incoming[i];
        ++added;
    }
    for (i = 0; i < incoming_count; ++i) {
        if (incoming[i].roll != 0) continue;
        if (merged_count >= MAX_STUDENTS) {
            printf("Import would exceed %d students. No records were changed.\n", MAX_STUDENTS);
            goto done;
        }
        incoming[i].roll = next_roll(merged, merged_count);
        if (incoming[i].roll < 0) {
            puts("No automatic roll numbers remain. No records were changed.");
            goto done;
        }
        merged[merged_count++] = incoming[i];
        ++added;
    }
    if (added > 0) {
        if (!save_records(merged, merged_count)) goto done;
        memcpy(db.entries, merged, merged_count * sizeof *db.entries);
        db.count = merged_count;
    }
    printf("Import complete: %zu added, %zu duplicate roll(s) skipped. Existing records were not overwritten.\n",
           added, skipped);
    if (added > 0) puts("Merged records saved automatically; the previous database is in the automatic backup.");
done:
    free(incoming);
    free(merged);
}

static void backup_now(void)
{
    char path[PATH_CAP];
    if (!prompt_output_path("Backup file (blank = students_backup.csv): ", "students_backup.csv", path)) return;
    if (!copy_file(db.path, path)) {
        printf("Could not create backup %s. The live database is unchanged.\n", path);
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
    if (same_path(path, db.path)) {
        puts("That is already the live database; nothing to restore.");
        return;
    }
    restored = malloc(MAX_STUDENTS * sizeof *restored);
    if (restored == NULL) {
        puts("Not enough memory for restore.");
        return;
    }
    if (!load_csv(path, restored, &count, 0)) goto done;
    printf("Validated %zu record(s) in %s.\n", count, path);
    if (!confirm("Replace ALL current records with this file? The current version will be backed up. (yes/N): ")) {
        puts("Restore cancelled.");
        goto done;
    }
    if (save_records(restored, count)) {
        memcpy(db.entries, restored, count * sizeof *restored);
        db.count = count;
        printf("Restored %zu record(s) and saved automatically. Previous version: %s\n", count, db.backup_path);
    }
done:
    free(restored);
}

/* ------------------------------- Console ------------------------------- */

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
    puts("Student Management System | Persistent CSV storage | Automatic calculations");
    if (!initialize_database()) {
        fprintf(stderr, "Startup stopped safely. Fix the reported file problem and try again.\n");
        return EXIT_FAILURE;
    }
    if (summary_only) {
        Summary summary = summarize(db.entries, db.count);
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
        if (status < 0 || !parse_integer(line, 0, 11, &choice)) {
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
