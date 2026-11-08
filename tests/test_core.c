#include "../src/student_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition, message) do { if (!(condition)) { fprintf(stderr,"FAIL: %s (%s)\n",message,sms_last_error()); return 1; } } while (0)

static int join(char *out,size_t cap,const char *root,const char *name)
{ int n=snprintf(out,cap,"%s/%s",root,name);return n>=0&&(size_t)n<cap; }

int main(int argc,char **argv)
{
    Database db,migrated;Student s;Summary summary;size_t added,skipped;char path[1024],backup[1024],report[1024],legacy[1024],migrated_csv[1024];FILE *f;
    CHECK(argc==2,"test directory argument");sms_set_error_stream(NULL);memset(&db,0,sizeof db);
    CHECK(join(db.path,sizeof db.path,argv[1],"records.csv"),"path buffer");CHECK(join(db.backup_path,sizeof db.backup_path,argv[1],"records.bak"),"backup path");db.migrate_legacy=0;
    CHECK(sms_initialize_database(&db)&&db.count==0,"initialize empty database");
    memset(&s,0,sizeof s);strcpy(s.name,"Ada Lovelace");strcpy(s.class_name,"10-A");s.marks=92.5;
    CHECK(sms_add_student(&db,&s)==1&&db.count==1,"add persists and assigns roll");
    strcpy(s.name,"Grace Hopper");strcpy(s.class_name,"10-A");s.marks=39.5;
    CHECK(sms_add_student(&db,&s)==2&&db.count==2,"add second record");
    CHECK(sms_mark_attendance(&db,1,1)&&sms_mark_attendance(&db,1,0),"record attendance");
    CHECK(db.entries[0].attended==1&&db.entries[0].total==2,"attendance counters");
    s=db.entries[0];strcpy(s.class_name,"10-B");s.marks=85.0;
    CHECK(sms_update_student(&db,1,&s)&&sms_text_equal(db.entries[0].class_name,"10-B"),"update record");
    CHECK(!sms_update_student(&db,999,&s)&&db.count==2,"missing update is harmless");
    summary=sms_summarize(db.entries,db.count);CHECK(summary.count==2&&summary.passed==1&&sms_grade(92.5)[0]=='A',"grade and summary calculations");
    CHECK(join(path,sizeof path,argv[1],"import.csv"),"import path");f=fopen(path,"w");CHECK(f!=NULL,"create import input");fputs("roll,name,class,marks,attended,total\n3,Linus Torvalds,11-B,88,0,0\n2,Duplicate,10-A,90,0,0\n",f);CHECK(fclose(f)==0,"close import input");
    CHECK(sms_import_csv(&db,path,&added,&skipped)&&added==1&&skipped==1&&db.count==3,"CSV merge import and duplicate skip");
    CHECK(join(report,sizeof report,argv[1],"report.csv"),"report path");CHECK(sms_export_report(&db,report),"CSV report export");
    CHECK(join(backup,sizeof backup,argv[1],"manual-backup.csv"),"backup path buffer");CHECK(sms_backup_database(&db,backup),"create backup");
    CHECK(sms_delete_student(&db,1)&&db.count==2,"delete record");CHECK(sms_restore_database(&db,backup)&&db.count==3,"validated restore");
    CHECK(join(legacy,sizeof legacy,argv[1],"students.txt"),"legacy path");f=fopen(legacy,"w");CHECK(f!=NULL,"create legacy seed");fputs("17 Niviya Selvan 83\n",f);CHECK(fclose(f)==0,"close legacy seed");
    memset(&migrated,0,sizeof migrated);CHECK(join(migrated.path,sizeof migrated.path,argv[1],"migration.csv"),"migration path");CHECK(join(migrated.backup_path,sizeof migrated.backup_path,argv[1],"migration.bak"),"migration backup path");strcpy(migrated.legacy_path,legacy);migrated.migrate_legacy=1;
    CHECK(sms_initialize_database(&migrated)&&migrated.count==1&&migrated.migrated_from_legacy,"legacy migration");CHECK(migrated.entries[0].roll==17&&sms_text_equal(migrated.entries[0].name,"Niviya Selvan"),"legacy student preserved");
    CHECK(join(migrated_csv,sizeof migrated_csv,argv[1],"migration.csv"),"migrated CSV path");CHECK(sms_file_exists(migrated_csv)==1&&sms_file_exists(legacy)==1,"migration retains source file");
    puts("Core data-operation tests passed.");return 0;
}
