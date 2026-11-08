#ifndef _WIN32
#define _XOPEN_SOURCE 700
#endif
#include "student_core.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#ifdef _WIN32
#include <windows.h>
#endif

static char last_error[1024];
static FILE *error_stream;
static int error_stream_set;

static void set_error(const char *format, ...)
{
    va_list ap;
    if (!error_stream_set) { error_stream = stderr; error_stream_set = 1; }
    va_start(ap, format);
    vsnprintf(last_error, sizeof last_error, format, ap);
    va_end(ap);
    if (error_stream != NULL) {
        va_start(ap, format);
        vfprintf(error_stream, format, ap);
        va_end(ap);
    }
}
void sms_set_error_stream(FILE *stream) { error_stream = stream; error_stream_set = 1; }
const char *sms_last_error(void) { return last_error[0] ? last_error : "Operation failed."; }
void sms_clear_error(void) { last_error[0] = '\0'; }

typedef struct {
    char fields[CSV_FIELDS][CSV_FIELD_CAP];
    size_t count;
} CsvRow;

static int sms_replace_file(const char *temporary, const char *destination)
{
#ifdef _WIN32
    return MoveFileExA(temporary, destination,
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(temporary, destination) == 0;
#endif
}

/* 1 = exists, 0 = missing, -1 = inaccessible (never confuse with empty data). */
int sms_file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file != NULL) {
        fclose(file);
        return 1;
    }
    return errno == ENOENT ? 0 : -1;
}

int sms_copy_file(const char *source, const char *destination)
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
static int sms_canonical_path(const char *path, char *output, size_t capacity)
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

int sms_same_path(const char *a, const char *b)
{
    char full_a[DERIVED_PATH_CAP], full_b[DERIVED_PATH_CAP];
    if (sms_canonical_path(a, full_a, sizeof full_a) &&
        sms_canonical_path(b, full_b, sizeof full_b)) {
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

static int sms_ascii_lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

int sms_text_equal(const char *a, const char *b)
{
    while (*a && *b) {
        if (sms_ascii_lower((unsigned char)*a++) != sms_ascii_lower((unsigned char)*b++))
            return 0;
    }
    return *a == *b;
}

int sms_text_contains(const char *haystack, const char *needle)
{
    size_t length = strlen(needle);
    size_t i;
    if (length == 0) return 1;
    for (i = 0; haystack[i] != '\0'; ++i) {
        size_t j = 0;
        while (j < length && haystack[i + j] != '\0' &&
               sms_ascii_lower((unsigned char)haystack[i + j]) ==
               sms_ascii_lower((unsigned char)needle[j])) ++j;
        if (j == length) return 1;
    }
    return 0;
}

void sms_trim(char *text)
{
    char *start = text;
    size_t length;
    while (*start && isspace((unsigned char)*start)) ++start;
    length = strlen(start);
    while (length > 0 && isspace((unsigned char)start[length - 1])) --length;
    start[length] = '\0';
    if (start != text) memmove(text, start, length + 1);
}

int sms_copy_trimmed(char *destination, size_t capacity, const char *source)
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

int sms_parse_integer(const char *text, int minimum, int maximum, int *value)
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

int sms_parse_number(const char *text, double minimum, double maximum,
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

static int sms_csv_append(CsvRow *row, size_t field, char c)
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
static int sms_parse_csv_row(const char *line, CsvRow *row)
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
                    if (!sms_csv_append(row, field, '"')) return 0;
                    ++i;
                } else {
                    quoted = 0;
                    closed_quote = 1;
                }
            } else if (!sms_csv_append(row, field, c)) return 0;
        } else if (closed_quote) {
            if (c != ',') return 0;
            if (++field >= CSV_FIELDS) return 0;
            closed_quote = 0;
        } else if (c == ',') {
            if (++field >= CSV_FIELDS) return 0;
        } else if (c == '"') {
            if (row->fields[field][0] != '\0') return 0;
            quoted = 1;
        } else if (!sms_csv_append(row, field, c)) return 0;
    }
    if (quoted) return 0;
    row->count = field + 1;
    return 1;
}

static void sms_write_csv_field(FILE *file, const char *text)
{
    int quote = strpbrk(text, ",\"\r\n") != NULL;
    if (quote) fputc('"', file);
    for (; *text; ++text) {
        if (*text == '"') fputc('"', file);
        fputc(*text, file);
    }
    if (quote) fputc('"', file);
}

int sms_valid_text(const char *text)
{
    if (text[0] == '\0') return 0;
    for (; *text; ++text)
        if ((unsigned char)*text < 32 || (unsigned char)*text == 127) return 0;
    return 1;
}

int sms_valid_student(const Student *student, int require_roll)
{
    return (!require_roll || student->roll > 0) &&
           student->roll >= 0 && sms_valid_text(student->name) &&
           strlen(student->name) < NAME_LEN &&
           sms_valid_text(student->class_name) &&
           strlen(student->class_name) < CLASS_LEN &&
           student->marks >= 0.0 && student->marks <= 100.0 &&
           student->attended >= 0 && student->total >= 0 &&
           student->attended <= student->total && student->total <= MAX_SESSIONS;
}

static int sms_student_from_row(const CsvRow *row, int report, int allow_blank_roll,
                            Student *student)
{
    memset(student, 0, sizeof *student);
    if (row->fields[0][0] == '\0') {
        if (!allow_blank_roll) return 0;
    } else if (!sms_parse_integer(row->fields[0], 1, INT_MAX, &student->roll)) return 0;
    if (!sms_copy_trimmed(student->name, NAME_LEN, row->fields[1]) ||
        !sms_copy_trimmed(student->class_name, CLASS_LEN, row->fields[2]) ||
        !sms_parse_number(row->fields[3], 0.0, 100.0, &student->marks) ||
        !sms_parse_integer(row->fields[report ? 6 : 4], 0, MAX_SESSIONS, &student->attended) ||
        !sms_parse_integer(row->fields[report ? 7 : 5], 0, MAX_SESSIONS, &student->total))
        return 0;
    return sms_valid_student(student, !allow_blank_roll);
}

static void sms_strip_line_end(char *line)
{
    size_t length = strlen(line);
    if (length > 0 && line[length - 1] == '\n') line[--length] = '\0';
    if (length > 0 && line[length - 1] == '\r') line[--length] = '\0';
}

static int sms_read_csv_header(FILE *file, const char *path, int *report)
{
    char line[LINE_CAP];
    CsvRow row;
    static const char *base[] = {"roll", "name", "class", "marks", "attended", "total"};
    static const char *derived[] = {"roll", "name", "class", "marks", "grade", "result",
                                    "attended", "total", "attendance_percent"};
    size_t i;
    if (fgets(line, sizeof line, file) == NULL) {
        set_error("%s: missing CSV header.\n", path);
        return 0;
    }
    if (strchr(line, '\n') == NULL && !feof(file)) {
        set_error("%s: header line is too long.\n", path);
        return 0;
    }
    sms_strip_line_end(line);
    if (strncmp(line, "\xEF\xBB\xBF", 3) == 0)
        memmove(line, line + 3, strlen(line + 3) + 1);
    if (!sms_parse_csv_row(line, &row)) {
        set_error("%s: malformed CSV header.\n", path);
        return 0;
    }
    if (row.count == 6) *report = 0;
    else if (row.count == 9) *report = 1;
    else {
        set_error("%s: expected 6 storage columns or 9 report columns.\n", path);
        return 0;
    }
    for (i = 0; i < row.count; ++i) {
        if (strcmp(row.fields[i], (*report ? derived : base)[i]) != 0) {
            set_error("%s: unsupported header column '%s'. See README.md.\n",
                    path, row.fields[i]);
            return 0;
        }
    }
    return 1;
}

static int sms_duplicate_roll(const Student *entries, size_t count, int roll)
{
    size_t i;
    for (i = 0; i < count; ++i)
        if (entries[i].roll == roll) return 1;
    return 0;
}

int sms_load_csv(const char *path, Student *entries, size_t *count,
                    int allow_blank_roll)
{
    char line[LINE_CAP];
    CsvRow row;
    FILE *file = fopen(path, "r");
    size_t line_number = 1;
    int report, ok = 1;
    *count = 0;
    if (file == NULL) {
        set_error("Cannot open %s: %s\n", path, strerror(errno));
        return 0;
    }
    if (!sms_read_csv_header(file, path, &report)) {
        fclose(file);
        return 0;
    }
    while (fgets(line, sizeof line, file) != NULL) {
        Student student;
        ++line_number;
        if (strchr(line, '\n') == NULL && !feof(file)) {
            set_error("%s:%zu: line exceeds %d bytes.\n", path, line_number, LINE_CAP - 1);
            ok = 0;
            break;
        }
        sms_strip_line_end(line);
        if (line[0] == '\0') continue;
        if (*count >= MAX_STUDENTS) {
            set_error("%s:%zu: maximum %d records exceeded.\n", path, line_number, MAX_STUDENTS);
            ok = 0;
            break;
        }
        if (!sms_parse_csv_row(line, &row) || row.count != (report ? 9u : 6u) ||
            !sms_student_from_row(&row, report, allow_blank_roll, &student) ||
            (student.roll > 0 && sms_duplicate_roll(entries, *count, student.roll))) {
            set_error("%s:%zu: invalid record or duplicate roll number; no records changed.\n",
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

int sms_load_legacy(const char *path, Student *entries, size_t *count)
{
    char line[LINE_CAP];
    FILE *file = fopen(path, "r");
    size_t line_number = 0;
    int ok = 1;
    *count = 0;
    if (file == NULL) {
        set_error("Cannot open legacy file %s: %s\n", path, strerror(errno));
        return 0;
    }
    while (fgets(line, sizeof line, file) != NULL) {
        Student student;
        char *separator, *name_start, *last_space = NULL, *mark_text, *cursor;
        double marks;
        int roll;
        ++line_number;
        if (strchr(line, '\n') == NULL && !feof(file)) {
            set_error("%s:%zu: line is too long.\n", path, line_number);
            ok = 0;
            break;
        }
        sms_strip_line_end(line);
        sms_trim(line);
        if (line[0] == '\0') continue;
        separator = strpbrk(line, " \t");
        if (*count >= MAX_STUDENTS || separator == NULL) {
            set_error("%s:%zu: invalid legacy record.\n", path, line_number);
            ok = 0;
            break;
        }
        *separator = '\0';
        if (!sms_parse_integer(line, 1, INT_MAX, &roll)) {
            set_error("%s:%zu: invalid roll number.\n", path, line_number);
            ok = 0;
            break;
        }
        name_start = separator + 1;
        for (cursor = name_start; *cursor; ++cursor)
            if (*cursor == ' ' || *cursor == '\t') last_space = cursor;
        if (last_space == NULL) {
            set_error("%s:%zu: expected roll, name and marks.\n", path, line_number);
            ok = 0;
            break;
        }
        mark_text = last_space + 1;
        *last_space = '\0';
        if (!sms_parse_number(mark_text, 0.0, 100.0, &marks)) {
            set_error("%s:%zu: invalid marks.\n", path, line_number);
            ok = 0;
            break;
        }
        memset(&student, 0, sizeof student);
        student.roll = roll;
        if (!sms_copy_trimmed(student.name, NAME_LEN, name_start)) {
            set_error("%s:%zu: missing or overlong name.\n", path, line_number);
            ok = 0;
            break;
        }
        strcpy(student.class_name, "Unassigned");
        student.marks = marks;
        student.attended = student.total = 0;
        if (!sms_valid_student(&student, 1) || sms_duplicate_roll(entries, *count, student.roll)) {
            set_error("%s:%zu: invalid record or duplicate roll.\n", path, line_number);
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
int sms_save_records(Database *db, const Student *entries, size_t count)
{
    char temporary[DERIVED_PATH_CAP], backup_temporary[DERIVED_PATH_CAP];
    FILE *file;
    size_t i;
    int existing = sms_file_exists(db->path);
    if (existing < 0 || count > MAX_STUDENTS) return 0;
    snprintf(temporary, sizeof temporary, "%s.tmp", db->path);
    memcpy(backup_temporary, db->backup_path, strlen(db->backup_path) + 1);
    strcat(backup_temporary, ".tmp");
    file = fopen(temporary, "w");
    if (file == NULL) {
        set_error("Cannot create %s: %s\n", temporary, strerror(errno));
        return 0;
    }
    fprintf(file, "%s\n", STORAGE_HEADER);
    for (i = 0; i < count; ++i) {
        fprintf(file, "%d,", entries[i].roll);
        sms_write_csv_field(file, entries[i].name);
        fputc(',', file);
        sms_write_csv_field(file, entries[i].class_name);
        fprintf(file, ",%.2f,%d,%d\n", entries[i].marks, entries[i].attended, entries[i].total);
    }
    if (ferror(file)) {
        fclose(file);
        remove(temporary);
        set_error("Write failed; the existing data file was not changed.\n");
        return 0;
    }
    if (fclose(file) != 0) {
        remove(temporary);
        set_error("Could not finish writing; existing data was not changed.\n");
        return 0;
    }
    if (existing) {
        if (!sms_copy_file(db->path, backup_temporary) ||
            !sms_replace_file(backup_temporary, db->backup_path)) {
            remove(backup_temporary);
            remove(temporary);
            set_error("Automatic backup failed; existing data was not changed.\n");
            return 0;
        }
    }
    if (!sms_replace_file(temporary, db->path)) {
        remove(temporary);
        set_error("Could not replace %s; existing data was not changed.\n", db->path);
        return 0;
    }
    return 1;
}

int sms_initialize_database(Database *db)
{
    int exists = sms_file_exists(db->path), legacy = 0;
    db->migrated_from_legacy = 0;
    db->created_new = 0;
    db->migration_source[0] = '\0';
    if (exists < 0) {
        set_error("Cannot access %s: %s\n", db->path, strerror(errno));
        return 0;
    }
    if (exists) return sms_load_csv(db->path, db->entries, &db->count, 0);
    if (db->migrate_legacy && db->legacy_path[0] != '\0') {
        legacy = sms_file_exists(db->legacy_path);
        if (legacy < 0) {
            set_error("Cannot access %s: %s\n", db->legacy_path, strerror(errno));
            return 0;
        }
    }
    if (legacy) {
        if (!sms_load_legacy(db->legacy_path, db->entries, &db->count) ||
            !sms_save_records(db, db->entries, db->count)) return 0;
        db->migrated_from_legacy = 1;
        snprintf(db->migration_source, sizeof db->migration_source, "%s", db->legacy_path);
    } else {
        if (!sms_save_records(db, db->entries, 0)) return 0;
        db->created_new = 1;
    }
    return 1;
}

size_t sms_grade_index(double marks)
{
    if (marks >= 90.0) return 0;
    if (marks >= 80.0) return 1;
    if (marks >= 70.0) return 2;
    if (marks >= 60.0) return 3;
    if (marks >= 50.0) return 4;
    if (marks >= 40.0) return 5;
    return 6;
}

const char *sms_grade(double marks)
{
    static const char *grades[] = {"A+", "A", "B", "C", "D", "E", "F"};
    return grades[sms_grade_index(marks)];
}

double sms_attendance_percent(const Student *student)
{
    return student->total == 0 ? -1.0 : 100.0 * student->attended / student->total;
}

size_t sms_find_roll(const Database *db, int roll)
{
    size_t i;
    for (i = 0; i < db->count; ++i)
        if (db->entries[i].roll == roll) return i;
    return SIZE_MAX;
}

int sms_next_roll(const Student *entries, size_t count)
{
    int maximum = 0;
    size_t i;
    for (i = 0; i < count; ++i)
        if (entries[i].roll > maximum) maximum = entries[i].roll;
    return maximum == INT_MAX ? -1 : maximum + 1;
}

Summary sms_summarize(const Student *entries, size_t count)
{
    Summary summary;
    size_t i;
    memset(&summary, 0, sizeof summary);
    summary.count = count;
    if (count == 0) return summary;
    summary.minimum = summary.maximum = entries[0].marks;
    for (i = 0; i < count; ++i) {
        const Student *s = &entries[i];
        double percent = sms_attendance_percent(s);
        summary.sum += s->marks;
        if (s->marks < summary.minimum) summary.minimum = s->marks;
        if (s->marks > summary.maximum) summary.maximum = s->marks;
        if (s->marks >= PASS_MARK) ++summary.passed;
        ++summary.grades[sms_grade_index(s->marks)];
        if (s->total > 0) {
            ++summary.recorded;
            summary.attended += s->attended;
            summary.total += s->total;
            if (percent < MIN_ATTENDANCE) ++summary.low_attendance;
        }
    }
    return summary;
}

int sms_compare_roll(const void *a, const void *b)
{
    const Student *x = a, *y = b;
    return (x->roll > y->roll) - (x->roll < y->roll);
}

int sms_compare_name(const void *a, const void *b)
{
    const Student *x = a, *y = b;
    int order = strcmp(x->name, y->name);
    return order ? order : sms_compare_roll(a, b);
}

int sms_compare_marks(const void *a, const void *b)
{
    const Student *x = a, *y = b;
    if (x->marks > y->marks) return -1;
    if (x->marks < y->marks) return 1;
    return sms_compare_roll(a, b);
}

int sms_compare_class(const void *a, const void *b)
{
    const Student *x = a, *y = b;
    int order = strcmp(x->class_name, y->class_name);
    return order ? order : sms_compare_roll(a, b);
}

Student *sms_sorted_copy(const Database *db, int (*compare)(const void *, const void *))
{
    Student *copy = malloc((db->count ? db->count : 1) * sizeof *copy);
    if (copy == NULL) {
        set_error("Not enough memory for this report.");
        return NULL;
    }
    memcpy(copy, db->entries, db->count * sizeof *copy);
    qsort(copy, db->count, sizeof *copy, compare);
    return copy;
}

int sms_protected_output_path(const Database *db, const char *path)
{
    if (sms_same_path(path, db->path) || sms_same_path(path, db->backup_path) ||
        (db->migrate_legacy && db->legacy_path[0] != '\0' && sms_same_path(path, db->legacy_path))) {
        set_error("Choose a separate output file; the database, backup and legacy source are protected.");
        return 1;
    }
    return 0;
}


/* ----------------------- Shared transactional operations ---------------- */

int sms_add_student(Database *db, const Student *input)
{
    Student student;
    int roll;
    if (db->count >= MAX_STUDENTS) {
        set_error("The database limit of 10000 students has been reached.");
        return -1;
    }
    roll = sms_next_roll(db->entries, db->count);
    if (roll < 0) {
        set_error("No automatic roll numbers remain. Existing roll numbers are preserved.");
        return -1;
    }
    student = *input;
    student.roll = roll;
    if (student.class_name[0] == '\0') strcpy(student.class_name, "General");
    if (!sms_valid_student(&student, 1)) {
        set_error("Invalid student record. Check name, class, marks and attendance totals.");
        return -1;
    }
    db->entries[db->count] = student;
    if (!sms_save_records(db, db->entries, db->count + 1)) {
        return -1;
    }
    ++db->count;
    return roll;
}

int sms_update_student(Database *db, int roll, const Student *replacement)
{
    size_t index = sms_find_roll(db, roll);
    Student old, candidate;
    if (index == SIZE_MAX) {
        set_error("No student found with roll number %d.", roll);
        return 0;
    }
    old = db->entries[index];
    candidate = *replacement;
    candidate.roll = roll;
    if (!sms_valid_student(&candidate, 1)) {
        set_error("Invalid student record. Check name, class, marks and attendance totals.");
        return 0;
    }
    db->entries[index] = candidate;
    if (!sms_save_records(db, db->entries, db->count)) {
        db->entries[index] = old;
        return 0;
    }
    return 1;
}

int sms_delete_student(Database *db, int roll)
{
    size_t index = sms_find_roll(db, roll);
    Student old;
    if (index == SIZE_MAX) {
        set_error("No student found with roll number %d.", roll);
        return 0;
    }
    old = db->entries[index];
    memmove(&db->entries[index], &db->entries[index + 1],
            (db->count - index - 1) * sizeof *db->entries);
    --db->count;
    if (!sms_save_records(db, db->entries, db->count)) {
        memmove(&db->entries[index + 1], &db->entries[index],
                (db->count - index) * sizeof *db->entries);
        db->entries[index] = old;
        ++db->count;
        return 0;
    }
    return 1;
}

int sms_mark_attendance(Database *db, int roll, int present)
{
    size_t index = sms_find_roll(db, roll);
    Student old;
    if (index == SIZE_MAX) {
        set_error("No student found with roll number %d.", roll);
        return 0;
    }
    old = db->entries[index];
    if (old.total == MAX_SESSIONS) {
        set_error("The attendance session limit has been reached; correct totals instead.");
        return 0;
    }
    ++db->entries[index].total;
    if (present) ++db->entries[index].attended;
    if (!sms_save_records(db, db->entries, db->count)) {
        db->entries[index] = old;
        return 0;
    }
    return 1;
}

int sms_mark_class_attendance_csv(Database *db, const char *class_name, const char *absent_list,
                                  size_t *marked, size_t *present_count)
{
    char line[LINE_CAP];
    size_t *members, count = 0, i, absent_count = 0;
    int *absent, roll, present_local = 0;
    Student *before;
    char *token;
    *marked = *present_count = 0;
    if (!sms_valid_text(class_name)) {
        set_error("Enter a valid class / section.");
        return 0;
    }
    if (strlen(absent_list) >= sizeof line) {
        set_error("Absent roll list is too long. No attendance was changed.");
        return 0;
    }
    strcpy(line, absent_list);
    sms_trim(line);
    members = malloc(MAX_STUDENTS * sizeof *members);
    absent = calloc(MAX_STUDENTS, sizeof *absent);
    if (members == NULL || absent == NULL) {
        free(members); free(absent);
        set_error("Not enough memory to record this session.");
        return 0;
    }
    for (i = 0; i < db->count; ++i) {
        if (sms_text_equal(class_name, db->entries[i].class_name)) {
            if (db->entries[i].total == MAX_SESSIONS) {
                set_error("Roll %d has reached the session limit. No attendance was changed.", db->entries[i].roll);
                free(members); free(absent);
                return 0;
            }
            members[count++] = i;
        }
    }
    if (count == 0) {
        set_error("No students in that class.");
        free(members); free(absent);
        return 0;
    }
    if (sms_text_equal(line, "all")) {
        for (i = 0; i < count; ++i) absent[absent_count++] = db->entries[members[i]].roll;
    } else if (!sms_text_equal(line, "none") && line[0] != '\0') {
        token = line;
        for (;;) {
            size_t index, j;
            char *comma = strchr(token, ',');
            if (comma != NULL) *comma = '\0';
            sms_trim(token);
            if (!sms_parse_integer(token, 1, INT_MAX, &roll)) {
                set_error("Invalid absent roll list. No attendance was changed.");
                goto cancel;
            }
            index = sms_find_roll(db, roll);
            if (index == SIZE_MAX || !sms_text_equal(class_name, db->entries[index].class_name)) {
                set_error("Roll %d is not in this class. No attendance was changed.", roll);
                goto cancel;
            }
            for (j = 0; j < absent_count; ++j)
                if (absent[j] == roll) {
                    set_error("Roll %d was listed twice. No attendance was changed.", roll);
                    goto cancel;
                }
            absent[absent_count++] = roll;
            if (comma == NULL) break;
            token = comma + 1;
        }
    }
    before = malloc(count * sizeof *before);
    if (before == NULL) {
        set_error("Not enough memory to record this session.");
        goto cancel;
    }
    for (i = 0; i < count; ++i) {
        Student *s = &db->entries[members[i]];
        int is_absent = 0;
        size_t j;
        before[i] = *s;
        for (j = 0; j < absent_count; ++j)
            if (absent[j] == s->roll) is_absent = 1;
        ++s->total;
        if (!is_absent) { ++s->attended; ++present_local; }
    }
    if (!sms_save_records(db, db->entries, db->count)) {
        for (i = 0; i < count; ++i) db->entries[members[i]] = before[i];
    } else {
        *marked = count;
        *present_count = (size_t)present_local;
    }
    free(before);
cancel:
    free(members);
    free(absent);
    return *marked == count && count > 0;
}

int sms_import_csv(Database *db, const char *path, size_t *added, size_t *skipped)
{
    Student *incoming, *merged;
    size_t incoming_count = 0, merged_count = db->count, i;
    *added = *skipped = 0;
    incoming = malloc(MAX_STUDENTS * sizeof *incoming);
    merged = malloc(MAX_STUDENTS * sizeof *merged);
    if (incoming == NULL || merged == NULL) {
        free(incoming); free(merged);
        set_error("Not enough memory for import.");
        return 0;
    }
    if (!sms_load_csv(path, incoming, &incoming_count, 1)) goto done;
    memcpy(merged, db->entries, db->count * sizeof *merged);
    for (i = 0; i < incoming_count; ++i) {
        if (incoming[i].roll == 0) continue;
        if (sms_duplicate_roll(merged, merged_count, incoming[i].roll)) { ++(*skipped); continue; }
        if (merged_count >= MAX_STUDENTS) {
            set_error("Import would exceed %d students. No records were changed.", MAX_STUDENTS);
            goto done;
        }
        merged[merged_count++] = incoming[i];
        ++(*added);
    }
    for (i = 0; i < incoming_count; ++i) {
        if (incoming[i].roll != 0) continue;
        if (merged_count >= MAX_STUDENTS) {
            set_error("Import would exceed %d students. No records were changed.", MAX_STUDENTS);
            goto done;
        }
        incoming[i].roll = sms_next_roll(merged, merged_count);
        if (incoming[i].roll < 0) {
            set_error("No automatic roll numbers remain. No records were changed.");
            goto done;
        }
        merged[merged_count++] = incoming[i];
        ++(*added);
    }
    if (*added > 0) {
        if (!sms_save_records(db, merged, merged_count)) { *added = 0; goto done; }
        memcpy(db->entries, merged, merged_count * sizeof *db->entries);
        db->count = merged_count;
    }
    free(incoming); free(merged);
    return 1;
done:
    free(incoming); free(merged);
    return 0;
}

int sms_import_legacy(Database *db, const char *path, size_t *added, size_t *skipped)
{
    Student *incoming, *merged;
    size_t incoming_count = 0, merged_count = db->count, i;
    *added = *skipped = 0;
    incoming = malloc(MAX_STUDENTS * sizeof *incoming);
    merged = malloc(MAX_STUDENTS * sizeof *merged);
    if (incoming == NULL || merged == NULL) {
        free(incoming); free(merged);
        set_error("Not enough memory for legacy import.");
        return 0;
    }
    if (!sms_load_legacy(path, incoming, &incoming_count)) goto fail;
    memcpy(merged, db->entries, db->count * sizeof *merged);
    for (i = 0; i < incoming_count; ++i) {
        if (sms_duplicate_roll(merged, merged_count, incoming[i].roll)) {
            ++*skipped;
            continue;
        }
        if (merged_count >= MAX_STUDENTS) {
            set_error("Legacy import would exceed %d students. No records were changed.", MAX_STUDENTS);
            goto fail;
        }
        merged[merged_count++] = incoming[i];
        ++*added;
    }
    if (*added > 0) {
        if (!sms_save_records(db, merged, merged_count)) { *added = 0; goto fail; }
        memcpy(db->entries, merged, merged_count * sizeof *db->entries);
        db->count = merged_count;
    }
    free(incoming); free(merged);
    return 1;
fail:
    free(incoming); free(merged);
    return 0;
}

int sms_export_report(const Database *db, const char *path)
{
    FILE *file;
    size_t i;
    if (sms_protected_output_path(db, path)) return 0;
    file = fopen(path, "w");
    if (file == NULL) {
        set_error("Cannot create %s: %s", path, strerror(errno));
        return 0;
    }
    fprintf(file, "%s\n", REPORT_HEADER);
    for (i = 0; i < db->count; ++i) {
        const Student *s = &db->entries[i];
        double percent = sms_attendance_percent(s);
        fprintf(file, "%d,", s->roll);
        sms_write_csv_field(file, s->name);
        fputc(',', file);
        sms_write_csv_field(file, s->class_name);
        fprintf(file, ",%.2f,%s,%s,%d,%d,", s->marks, sms_grade(s->marks),
                s->marks >= PASS_MARK ? "PASS" : "FAIL", s->attended, s->total);
        if (percent < 0.0) fprintf(file, "N/A\n");
        else fprintf(file, "%.2f\n", percent);
    }
    if (ferror(file) || fclose(file) != 0) {
        remove(path);
        set_error("Export failed; any incomplete output was removed.");
        return 0;
    }
    return 1;
}

int sms_backup_database(const Database *db, const char *path)
{
    if (sms_protected_output_path(db, path)) return 0;
    if (!sms_copy_file(db->path, path)) {
        set_error("Could not create backup %s. The live database is unchanged.", path);
        return 0;
    }
    return 1;
}

int sms_restore_database(Database *db, const char *path)
{
    Student *restored;
    size_t count = 0;
    if (sms_same_path(path, db->path)) {
        set_error("That is already the live database; nothing to restore.");
        return 0;
    }
    restored = malloc(MAX_STUDENTS * sizeof *restored);
    if (restored == NULL) {
        set_error("Not enough memory for restore.");
        return 0;
    }
    if (!sms_load_csv(path, restored, &count, 0)) { free(restored); return 0; }
    if (!sms_save_records(db, restored, count)) { free(restored); return 0; }
    memcpy(db->entries, restored, count * sizeof *restored);
    db->count = count;
    free(restored);
    return 1;
}
