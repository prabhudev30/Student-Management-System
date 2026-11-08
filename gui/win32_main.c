#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "../src/student_core.h"

#define APP_TITLE "Student Management Studio"
#define ID_SEARCH 1001
#define ID_TABLE 1002
#define ID_ADD 1003
#define ID_EDIT 1004
#define ID_DELETE 1005
#define ID_VIEW 1006
#define ID_ATTEND 1007
#define ID_IMPORT 1008
#define ID_EXPORT 1009
#define ID_BACKUP 1010
#define ID_RESTORE 1011
#define ID_REPORT 1012
#define ID_ALL 1013
#define ID_DASH 1014
#define ID_STUDENTS 1015
#define ID_RANK 1016
#define ID_FORM_NAME 1101
#define ID_FORM_CLASS 1102
#define ID_FORM_MARKS 1103
#define ID_FORM_ROLL 1104
#define ID_FORM_OK 1105
#define ID_FORM_ATTEND 1106
#define ID_FORM_TOTAL 1107
#define ID_FORM_PRESENT 1108
#define ID_FORM_STATUS 1109

static HINSTANCE instance;
static HWND main_window, table, search_box, status_bar, page_label, dashboard;
static HWND nav_dash, nav_students, nav_rank;
static Database db;
static int current_page = 0, sort_mode = 0, sort_reverse = 0;
static char data_dir[PATH_CAP];
static const COLORREF NAVY = RGB(19, 32, 58), BLUE = RGB(45, 103, 220), BG = RGB(244, 247, 251);

typedef struct { Student student; int is_edit; int attendance; int present; int accepted; } FormData;

static void set_status(const char *text) { if (status_bar) SetWindowTextA(status_bar, text); }
static void show_error(const char *title) { MessageBoxA(main_window, sms_last_error(), title, MB_OK | MB_ICONERROR); }
static void add_control(const char *klass, const char *text, DWORD style, int x, int y, int w, int h, int id, HWND parent)
{
    CreateWindowExA(0, klass, text, WS_CHILD | WS_VISIBLE | style, x,y,w,h,parent,(HMENU)(INT_PTR)id,instance,NULL);
}
static HWND button(HWND parent, const char *text, int id, int x, int y, int w, int h)
{
    return CreateWindowExA(0,"BUTTON",text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,x,y,w,h,parent,(HMENU)(INT_PTR)id,instance,NULL);
}
static void style_button(HWND h)
{
    SendMessageA(h,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
}
static void fill_table(void)
{
    Student *copy;
    size_t i;
    char b[128];
    if (!table) return;
    ListView_DeleteAllItems(table);
    copy = sms_sorted_copy(&db, sort_mode == 1 ? sms_compare_name : sort_mode == 2 ? sms_compare_marks : sort_mode == 3 ? sms_compare_class : sms_compare_roll);
    if (copy == NULL && db.count != 0) return;
    if (sort_reverse && copy) for (i=0;i<db.count/2;i++) { Student t=copy[i];copy[i]=copy[db.count-1-i];copy[db.count-1-i]=t; }
    for (i=0;i<db.count;i++) {
        LVITEMA item; int row; size_t j, rank=1;
        for(j=0;j<db.count;j++)if(db.entries[j].marks>copy[i].marks)++rank;
        if (current_page == 1 && search_box) {
            char query[256], roll[32]; GetWindowTextA(search_box,query,sizeof query);
            snprintf(roll,sizeof roll,"%d",copy[i].roll);
            if (query[0] && !sms_text_contains(copy[i].name,query) && !sms_text_contains(copy[i].class_name,query) && !strstr(roll,query)) continue;
        }
        if (current_page == 2 && i != 0) { /* ranking: table already sorted by marks */ }
        memset(&item,0,sizeof item); item.mask=LVIF_TEXT; item.iItem=ListView_GetItemCount(table);
        snprintf(b,sizeof b,"%d",copy[i].roll); item.pszText=b; row=ListView_InsertItem(table,&item);
        ListView_SetItemText(table,row,1,copy[i].name);
        ListView_SetItemText(table,row,2,copy[i].class_name);
        snprintf(b,sizeof b,"%.2f",copy[i].marks); ListView_SetItemText(table,row,3,b);
        ListView_SetItemText(table,row,4,(LPSTR)sms_grade(copy[i].marks));
        ListView_SetItemText(table,row,5,copy[i].marks>=PASS_MARK?"PASS":"FAIL");
        if (copy[i].total==0) strcpy(b,"Not recorded"); else snprintf(b,sizeof b,"%.1f%%  %d/%d",sms_attendance_percent(&copy[i]),copy[i].attended,copy[i].total);
        ListView_SetItemText(table,row,6,b);
        snprintf(b,sizeof b,"%zu",rank);ListView_SetItemText(table,row,7,b);
        { LVITEMA data_item; memset(&data_item,0,sizeof data_item); data_item.mask=LVIF_PARAM; data_item.iItem=row; data_item.lParam=(LPARAM)copy[i].roll; SendMessageA(table,LVM_SETITEMA,0,(LPARAM)&data_item); }
    }
    free(copy);
}
static void refresh_dashboard(void)
{
    Summary s=sms_summarize(db.entries,db.count); char text[768];
    if (!dashboard) return;
    snprintf(text,sizeof text,"OVERVIEW\n\n%zu     STUDENTS\n\n%.2f     CLASS AVERAGE\n\n%zu / %zu     PASSED / FAILED\n\n%.2f     HIGHEST MARKS\n\n%zu     LOW ATTENDANCE",s.count,s.count?s.sum/(double)s.count:0.0,s.passed,s.count-s.passed,s.count?s.maximum:0.0,s.low_attendance);
    SetWindowTextA(dashboard,text);
}
static int selected_roll(void)
{
    int row=ListView_GetNextItem(table,-1,LVNI_SELECTED); LVITEMA item;
    if(row<0)return 0;memset(&item,0,sizeof item);item.mask=LVIF_PARAM;item.iItem=row;
    return SendMessageA(table,LVM_GETITEMA,0,(LPARAM)&item)?(int)item.lParam:0;
}
static Student *student_by_roll(int roll)
{
    size_t i; for(i=0;i<db.count;i++) if(db.entries[i].roll==roll) return &db.entries[i]; return NULL;
}
static void layout_controls(HWND w)
{
    RECT r; int width,height;
    GetClientRect(w,&r); width=r.right; height=r.bottom;
    if(nav_dash) MoveWindow(nav_dash,20,105,188,42,TRUE);
    if(nav_students) MoveWindow(nav_students,20,153,188,42,TRUE);
    if(nav_rank) MoveWindow(nav_rank,20,201,188,42,TRUE);
    if(page_label) MoveWindow(page_label,245,22,width-285,34,TRUE);
    if(search_box) MoveWindow(search_box,245,118,320,30,TRUE);
    if(dashboard) MoveWindow(dashboard,245,80,width-285,120,TRUE);
    if(table) MoveWindow(table,245,160,width-285,height-225,TRUE);
    { int x=245; int ids[]={ID_ADD,ID_EDIT,ID_DELETE,ID_VIEW,ID_ATTEND,ID_IMPORT,ID_EXPORT,ID_BACKUP,ID_RESTORE,ID_REPORT}; int widths[]={80,58,62,58,80,75,75,58,58,58}; int i;
      for(i=0;i<10;i++){HWND h=GetDlgItem(w,ids[i]);if(h){MoveWindow(h,x,68,widths[i],32,TRUE);x+=widths[i]+5;}} }
    if(status_bar) MoveWindow(status_bar,245,height-48,width-285,24,TRUE);
}
static void set_page(int page)
{
    current_page=page;
    if(page_label) SetWindowTextA(page_label,page==0?"Dashboard":page==1?"Student directory":"Academic rankings");
    if(search_box) ShowWindow(search_box,page==1?SW_SHOW:SW_HIDE);
    if(dashboard) ShowWindow(dashboard,page==0?SW_SHOW:SW_HIDE);
    if(table) ShowWindow(table,page==0?SW_HIDE:SW_SHOW);
    if(page==2){sort_mode=2;sort_reverse=0;}
    if(nav_dash) InvalidateRect(nav_dash,NULL,TRUE);
    fill_table(); refresh_dashboard();
}
static void columns_init(HWND list)
{
    static const char *names[]={"Roll","Student name","Class","Marks","Grade","Result","Attendance","Rank"};
    static const int widths[]={70,205,125,85,70,80,150,60};
    int i; LVCOLUMNA c;
    for(i=0;i<8;i++){memset(&c,0,sizeof c);c.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_SUBITEM;c.pszText=(LPSTR)names[i];c.cx=widths[i];c.iSubItem=i;ListView_InsertColumn(list,i,&c);}
}
static void add_label(HWND w,const char *text,int x,int y,int width)
{ add_control("STATIC",text,SS_LEFT,x,y,width,22,0,w); }
static int form_window(HWND owner,const char *title,FormData *data)
{
    HWND w, e_name,e_class,e_marks,e_roll; MSG msg; int result=0;
    w=CreateWindowExA(WS_EX_DLGMODALFRAME,"SMS_FORM",title,WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE, CW_USEDEFAULT,CW_USEDEFAULT,410,310,owner,NULL,instance,data);
    if(!w) return 0;
    EnableWindow(owner,FALSE);
    add_label(w,"Roll number",24,20,110); e_roll=CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT","",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,145,18,220,25,w,(HMENU)ID_FORM_ROLL,instance,NULL);
    add_label(w,"Student name",24,57,110); e_name=CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT","",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,145,55,220,25,w,(HMENU)ID_FORM_NAME,instance,NULL);
    add_label(w,"Class",24,94,110); e_class=CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT","",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,145,92,220,25,w,(HMENU)ID_FORM_CLASS,instance,NULL);
    add_label(w,"Marks (0–100)",24,131,110); e_marks=CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT","",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,145,129,220,25,w,(HMENU)ID_FORM_MARKS,instance,NULL);
    if(data->attendance){
        HWND status;
        EnableWindow(e_roll,FALSE);EnableWindow(e_name,FALSE);EnableWindow(e_class,FALSE);EnableWindow(e_marks,FALSE);
        add_label(w,"Mark this session",24,174,110);
        status=CreateWindowExA(0,"COMBOBOX","",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST,145,170,220,90,w,(HMENU)ID_FORM_STATUS,instance,NULL);
        SendMessageA(status,CB_ADDSTRING,0,(LPARAM)"Present");SendMessageA(status,CB_ADDSTRING,0,(LPARAM)"Absent");SendMessageA(status,CB_SETCURSEL,0,0);
    }
    if(data->student.roll>0){char b[40];snprintf(b,sizeof b,"%d",data->student.roll);SetWindowTextA(e_roll,b);SendMessageA(e_roll,EM_SETREADONLY,TRUE,0);}
    if(data->student.name[0])SetWindowTextA(e_name,data->student.name);
    if(data->student.class_name[0])SetWindowTextA(e_class,data->student.class_name);
    if(data->student.marks>=0){char b[40];snprintf(b,sizeof b,"%.2f",data->student.marks);SetWindowTextA(e_marks,b);}
    button(w,data->attendance?"Save attendance":"Save student",ID_FORM_OK,145,data->attendance?220:235,105,32);
    button(w,"Cancel",IDCANCEL,260,data->attendance?220:235,105,32);
    SetFocus(e_name);
    while(IsWindow(w)&&GetMessageA(&msg,NULL,0,0)>0){if(!IsDialogMessageA(w,&msg)){TranslateMessage(&msg);DispatchMessageA(&msg);}}
    EnableWindow(owner,TRUE); SetForegroundWindow(owner);
    /* Result is placed in window user data before it closes. */
    result=data->accepted;
    (void)e_class;(void)e_marks;(void)e_roll;
    return result;
}
static LRESULT CALLBACK form_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp)
{
    if(msg==WM_CREATE){SetPropA(w,"SMS_DATA",(HANDLE)((CREATESTRUCTA*)lp)->lpCreateParams);return 0;}
    if(msg==WM_COMMAND && LOWORD(wp)==ID_FORM_OK){
        FormData *d=(FormData*)GetPropA(w,"SMS_DATA"); char a[256],b[128],c[64]; int roll; double marks;
        GetWindowTextA(GetDlgItem(w,ID_FORM_ROLL),a,sizeof a);GetWindowTextA(GetDlgItem(w,ID_FORM_NAME),b,sizeof b);GetWindowTextA(GetDlgItem(w,ID_FORM_CLASS),c,sizeof c);
        if(!d->attendance){
            if(!sms_parse_integer(a,1,INT_MAX,&roll)||!sms_copy_trimmed(d->student.name,sizeof d->student.name,b)||!sms_copy_trimmed(d->student.class_name,sizeof d->student.class_name,c)||!sms_parse_number(GetWindowTextA(GetDlgItem(w,ID_FORM_MARKS),a,sizeof a)?a:"",0,100,&marks)){
                MessageBoxA(w,"Enter a valid roll, name, class and marks from 0 to 100.","Check the form",MB_OK|MB_ICONWARNING);return 0;}
            d->student.roll=roll;d->student.marks=marks;d->student.attended=0;d->student.total=0;
        }else{
            LRESULT choice=SendMessageA(GetDlgItem(w,ID_FORM_STATUS),CB_GETCURSEL,0,0);
            if(choice==CB_ERR){MessageBoxA(w,"Choose Present or Absent.","Check the form",MB_OK|MB_ICONWARNING);return 0;}
            d->present=choice==0;
        }
        d->accepted=1;DestroyWindow(w);return 0;
    }
    if(msg==WM_COMMAND&&LOWORD(wp)==IDCANCEL){DestroyWindow(w);return 0;}
    if(msg==WM_CLOSE){DestroyWindow(w);return 0;}
    return DefWindowProcA(w,msg,wp,lp);
}
static int form_dispatch(HWND owner,const char *title,FormData *data)
{
    static int registered=0; WNDCLASSA wc;
    if(!registered){memset(&wc,0,sizeof wc);wc.lpfnWndProc=form_proc;wc.hInstance=instance;wc.lpszClassName="SMS_FORM";wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);wc.hCursor=LoadCursor(NULL,IDC_ARROW);RegisterClassA(&wc);registered=1;}
    return form_window(owner,title,data);
}
static void add_student(void)
{
    FormData d; int newroll; memset(&d,0,sizeof d);d.student.marks=-1;newroll=sms_next_roll(db.entries,db.count);if(newroll<1){MessageBoxA(main_window,"No roll numbers available.",APP_TITLE,MB_OK|MB_ICONERROR);return;}d.student.roll=newroll;
    if(!form_dispatch(main_window,"Add student",&d))return;
    if(!sms_add_student(&db,&d.student)){show_error("Could not add student");return;}fill_table();refresh_dashboard();set_status("Student added and saved automatically.");
}
static void edit_student(void)
{
    int roll=selected_roll();Student *s=student_by_roll(roll);FormData d;
    if(!s){MessageBoxA(main_window,"Select a student row first.",APP_TITLE,MB_OK|MB_ICONINFORMATION);return;}memset(&d,0,sizeof d);d.student=*s;
    if(!form_dispatch(main_window,"Edit student",&d))return;
    if(!sms_update_student(&db,roll,&d.student)){show_error("Could not update student");return;}fill_table();refresh_dashboard();set_status("Changes saved automatically.");
}
static void delete_student(void)
{
    int roll=selected_roll();Student *s=student_by_roll(roll);char prompt[256];if(!s){MessageBoxA(main_window,"Select a student row first.",APP_TITLE,MB_OK|MB_ICONINFORMATION);return;}
    snprintf(prompt,sizeof prompt,"Delete %s (roll %d)? This cannot be undone.",s->name,roll);if(MessageBoxA(main_window,prompt,"Confirm deletion",MB_YESNO|MB_ICONWARNING)!=IDYES)return;
    if(!sms_delete_student(&db,roll)){show_error("Could not delete student");return;}fill_table();refresh_dashboard();set_status("Student deleted.");
}
static void view_student(void)
{
    int roll=selected_roll();Student *s=student_by_roll(roll);char text[1024];if(!s){MessageBoxA(main_window,"Select a student row first.",APP_TITLE,MB_OK|MB_ICONINFORMATION);return;}
    snprintf(text,sizeof text,"Roll: %d\nName: %s\nClass: %s\nMarks: %.2f / 100\nGrade: %s\nResult: %s\nAttendance: %s",s->roll,s->name,s->class_name,s->marks,sms_grade(s->marks),s->marks>=PASS_MARK?"PASS":"FAIL",s->total?"Recorded":"Not recorded");MessageBoxA(main_window,text,"Student profile",MB_OK|MB_ICONINFORMATION);
}
static void attendance_form(void)
{
    int roll=selected_roll();Student *s=student_by_roll(roll);FormData d;if(!s){MessageBoxA(main_window,"Select a student row first.",APP_TITLE,MB_OK|MB_ICONINFORMATION);return;}memset(&d,0,sizeof d);d.student=*s;d.attendance=1;
    if(!form_dispatch(main_window,"Record attendance",&d))return;
    if(!sms_mark_attendance(&db,roll,d.present)){show_error("Could not save attendance");return;}
    fill_table();refresh_dashboard();set_status(d.present?"Student marked present.":"Student marked absent.");
}
static int path_matches_file(const char *path,const char *directory,const char *filename)
{
    char candidate[PATH_CAP];int n=snprintf(candidate,sizeof candidate,"%s\\%s",directory,filename);
    return n>=0&&(size_t)n<sizeof candidate&&sms_same_path(path,candidate);
}
static int is_preserved_source_path(const char *path)
{
    char module[PATH_CAP],*slash;DWORD length;
    if(db.legacy_path[0]&&sms_same_path(path,db.legacy_path))return 1;
    if(path_matches_file(path,data_dir,"students.txt"))return 1;
    length=GetModuleFileNameA(NULL,module,sizeof module);
    if(!length||length>=sizeof module)return 0;
    slash=strrchr(module,'\\');if(!slash)return 0;
    if(slash==module+2&&module[1]==':')slash[1]='\0';else *slash='\0';
    return path_matches_file(path,module,"students.csv")||path_matches_file(path,module,"students.txt");
}
static void choose_csv(int save,const char *title)
{
    char path[MAX_PATH]="";OPENFILENAMEA ofn;memset(&ofn,0,sizeof ofn);ofn.lStructSize=sizeof ofn;ofn.hwndOwner=main_window;ofn.lpstrTitle=title;ofn.lpstrFilter="CSV files (*.csv)\0*.csv\0All files (*.*)\0*.*\0";ofn.lpstrFile=path;ofn.nMaxFile=sizeof path;ofn.lpstrDefExt="csv";ofn.Flags=OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    if(!(save?GetSaveFileNameA(&ofn):GetOpenFileNameA(&ofn)))return;
    if(save){if(is_preserved_source_path(path)){MessageBoxA(main_window,"This file is part of the original project data and is protected from overwrite.",APP_TITLE,MB_OK|MB_ICONWARNING);return;}if(!sms_export_report(&db,path)){show_error("CSV export failed");return;}set_status("Report exported as CSV.");}
    else{size_t added=0,skipped=0;if(!sms_import_csv(&db,path,&added,&skipped)){show_error("CSV import failed");return;}fill_table();refresh_dashboard();{char msg[160];snprintf(msg,sizeof msg,"Import complete: %zu added, %zu skipped.",added,skipped);set_status(msg);}}
}
static void backup_dialog(void)
{
    char path[MAX_PATH]="students_backup.csv";OPENFILENAMEA ofn;memset(&ofn,0,sizeof ofn);ofn.lStructSize=sizeof ofn;ofn.hwndOwner=main_window;ofn.lpstrFilter="CSV backups (*.csv)\0*.csv\0";ofn.lpstrFile=path;ofn.nMaxFile=sizeof path;ofn.lpstrDefExt="csv";ofn.Flags=OFN_PATHMUSTEXIST|OFN_OVERWRITEPROMPT;
    if(!GetSaveFileNameA(&ofn))return;if(is_preserved_source_path(path)){MessageBoxA(main_window,"This file is part of the original project data and is protected from overwrite.",APP_TITLE,MB_OK|MB_ICONWARNING);return;}if(!sms_backup_database(&db,path)){show_error("Backup failed");return;}set_status("Validated backup created.");
}
static void restore_dialog(void)
{
    char path[MAX_PATH]="";OPENFILENAMEA ofn;memset(&ofn,0,sizeof ofn);ofn.lStructSize=sizeof ofn;ofn.hwndOwner=main_window;ofn.lpstrFilter="CSV backups (*.csv)\0*.csv\0";ofn.lpstrFile=path;ofn.nMaxFile=sizeof path;ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
    if(!GetOpenFileNameA(&ofn))return;if(MessageBoxA(main_window,"Restore this validated backup? The current database will be backed up first.","Confirm restore",MB_YESNO|MB_ICONWARNING)!=IDYES)return;
    if(!sms_restore_database(&db,path)){show_error("Restore failed");return;}fill_table();refresh_dashboard();set_status("Backup restored and validated.");
}
static void show_report(void)
{
    Summary s=sms_summarize(db.entries,db.count);char text[900];snprintf(text,sizeof text,"ACADEMIC REPORT\n\nStudents: %zu\nAverage marks: %.2f\nPassed: %zu    Failed: %zu\nHighest: %.2f    Lowest: %.2f\nLow attendance (<75%%): %zu\n\nRankings are available from the Rankings navigation button. Export the detailed CSV report from Export CSV.",s.count,s.count?s.sum/(double)s.count:0,s.passed,s.count-s.passed,s.count?s.maximum:0,s.count?s.minimum:0,s.low_attendance);MessageBoxA(main_window,text,"Class report",MB_OK|MB_ICONINFORMATION);
}
static LRESULT CALLBACK wndproc(HWND w,UINT m,WPARAM wp,LPARAM lp)
{
    switch(m){
    case WM_CREATE:{
        HWND h;INITCOMMONCONTROLSEX ic={sizeof ic,ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&ic);
        h=CreateWindowExA(0,"STATIC",APP_TITLE,WS_CHILD|WS_VISIBLE|SS_LEFT,20,24,190,32,w,NULL,instance,NULL);SendMessageA(h,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
        nav_dash=button(w,"  Dashboard",ID_DASH,20,105,188,42);nav_students=button(w,"  Students",ID_STUDENTS,20,153,188,42);nav_rank=button(w,"  Rankings",ID_RANK,20,201,188,42);
        style_button(nav_dash);style_button(nav_students);style_button(nav_rank);
        page_label=CreateWindowExA(0,"STATIC","Dashboard",WS_CHILD|WS_VISIBLE|SS_LEFT,245,25,500,35,w,NULL,instance,NULL);
        search_box=CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT","",WS_CHILD|ES_AUTOHSCROLL|WS_TABSTOP,245,75,300,30,w,(HMENU)ID_SEARCH,instance,NULL);
        dashboard=CreateWindowExA(WS_EX_CLIENTEDGE,"STATIC","",WS_CHILD|WS_VISIBLE|SS_LEFT,245,120,700,120,w,NULL,instance,NULL);SendMessageA(dashboard,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
        table=CreateWindowExA(WS_EX_CLIENTEDGE,WC_LISTVIEWA,"",WS_CHILD|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_TABSTOP,245,250,700,300,w,(HMENU)ID_TABLE,instance,NULL);ListView_SetExtendedListViewStyle(table,LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);columns_init(table);
        button(w,"Add student",ID_ADD,245,0,100,30);button(w,"Edit",ID_EDIT,350,0,72,30);button(w,"Delete",ID_DELETE,427,0,78,30);button(w,"View",ID_VIEW,510,0,72,30);button(w,"Attendance",ID_ATTEND,587,0,100,30);button(w,"Import CSV",ID_IMPORT,692,0,95,30);button(w,"Export CSV",ID_EXPORT,792,0,95,30);button(w,"Backup",ID_BACKUP,892,0,75,30);button(w,"Restore",ID_RESTORE,972,0,75,30);button(w,"Report",ID_REPORT,1052,0,75,30);
        status_bar=CreateWindowExA(0,"STATIC","Ready · All changes save automatically",WS_CHILD|WS_VISIBLE|SS_LEFT,245,0,700,24,w,NULL,instance,NULL);
        layout_controls(w);set_page(0);return 0;}
    case WM_SIZE:layout_controls(w);return 0;
    case WM_ERASEBKGND:{RECT r,accent;HBRUSH bg=CreateSolidBrush(BG),nav=CreateSolidBrush(NAVY),blue=CreateSolidBrush(BLUE);GetClientRect(w,&r);FillRect((HDC)wp,&r,bg);r.right=225;FillRect((HDC)wp,&r,nav);accent.left=0;accent.top=0;accent.right=4;accent.bottom=300;FillRect((HDC)wp,&accent,blue);DeleteObject(bg);DeleteObject(nav);DeleteObject(blue);return 1;}
    case WM_CTLCOLORSTATIC:{HDC dc=(HDC)wp;HWND child=(HWND)lp;RECT r;GetWindowRect(child,&r);ScreenToClient(w,(POINT*)&r.left);SetBkMode(dc,TRANSPARENT);if(r.left<225){SetTextColor(dc,RGB(255,255,255));return (LRESULT)GetStockObject(NULL_BRUSH);}return (LRESULT)GetStockObject(WHITE_BRUSH);}
    case WM_NOTIFY:{LPNMHDR n=(LPNMHDR)lp;if(n->idFrom==ID_TABLE&&n->code==LVN_COLUMNCLICK){LPNMLISTVIEW v=(LPNMLISTVIEW)lp;sort_mode=v->iSubItem==1?1:v->iSubItem==3||v->iSubItem==4||v->iSubItem==7?2:v->iSubItem==2?3:0;sort_reverse=!sort_reverse;fill_table();}return 0;}
    case WM_COMMAND:switch(LOWORD(wp)){
    case ID_DASH:set_page(0);break;case ID_STUDENTS:set_page(1);break;case ID_RANK:set_page(2);break;
    case ID_ADD:add_student();break;case ID_EDIT:edit_student();break;case ID_DELETE:delete_student();break;case ID_VIEW:view_student();break;case ID_ATTEND:attendance_form();break;
    case ID_IMPORT:choose_csv(0,"Import");break;case ID_EXPORT:choose_csv(1,"Export");break;case ID_BACKUP:backup_dialog();break;case ID_RESTORE:restore_dialog();break;case ID_REPORT:show_report();break;
    case ID_SEARCH:if(HIWORD(wp)==EN_CHANGE)fill_table();break;}
        return 0;
    case WM_CLOSE:DestroyWindow(w);return 0;case WM_DESTROY:PostQuitMessage(0);return 0;
    }
    return DefWindowProcA(w,m,wp,lp);
}
static int copy_csv_without_replace(const char *source,const char *destination)
{
    char temporary[DERIVED_PATH_CAP];
    int n=snprintf(temporary,sizeof temporary,"%s.migration.tmp",destination);
    if(n<0||(size_t)n>=sizeof temporary||!sms_copy_file(source,temporary))return 0;
    if(!MoveFileExA(temporary,destination,MOVEFILE_WRITE_THROUGH)){
        DWORD error=GetLastError();DeleteFileA(temporary);
        return error==ERROR_ALREADY_EXISTS||error==ERROR_FILE_EXISTS;
    }
    return 1;
}
static int prepare_storage(void)
{
    char appdata[PATH_CAP],local_file[PATH_CAP],local_dir[PATH_CAP],*separator;
    DWORD n=GetEnvironmentVariableA("APPDATA",appdata,sizeof appdata),exe_length;
    int database_exists;
    if(!n||n>=sizeof appdata)return 0;
    if(snprintf(data_dir,sizeof data_dir,"%s\\StudentManagementSystem",appdata)>=(int)sizeof data_dir)return 0;
    if(!CreateDirectoryA(data_dir,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS)return 0;
    if(snprintf(db.path,sizeof db.path,"%s\\students.csv",data_dir)>=(int)sizeof db.path)return 0;
    if(snprintf(db.backup_path,sizeof db.backup_path,"%s\\students.csv.bak",data_dir)>=(int)sizeof db.backup_path)return 0;
    db.migrate_legacy=1;
    if(snprintf(db.legacy_path,sizeof db.legacy_path,"%s\\students.txt",data_dir)>=(int)sizeof db.legacy_path)return 0;
    database_exists=sms_file_exists(db.path);
    if(database_exists<0)return 0;
    exe_length=GetModuleFileNameA(NULL,local_dir,sizeof local_dir);
    if(!exe_length||exe_length>=sizeof local_dir)return 0;
    separator=strrchr(local_dir,'\\');
    if(!separator)return 0;
    if(separator==local_dir+2&&local_dir[1]==':')separator[1]='\0';else *separator='\0';
    {
        if(database_exists==0){
            int source_state;
            if(snprintf(local_file,sizeof local_file,"%s\\students.csv",local_dir)>=(int)sizeof local_file)return 0;
            source_state=sms_file_exists(local_file);
            if(source_state<0){MessageBoxA(NULL,"Could not read the existing students.csv. The source was left untouched.",APP_TITLE,MB_OK|MB_ICONERROR);return 0;}
            if(source_state==1&&!copy_csv_without_replace(local_file,db.path)){MessageBoxA(NULL,"Could not safely copy the existing students.csv into the user-data folder. The source file was left untouched.",APP_TITLE,MB_OK|MB_ICONERROR);return 0;}
        }
        if(database_exists==0){
            int legacy_state;
            if(snprintf(local_file,sizeof local_file,"%s\\students.txt",local_dir)>=(int)sizeof local_file)return 0;
            legacy_state=sms_file_exists(local_file);
            if(legacy_state<0){MessageBoxA(NULL,"Could not read the legacy students.txt. No migration was attempted.",APP_TITLE,MB_OK|MB_ICONERROR);return 0;}
            if(legacy_state==1)snprintf(db.legacy_path,sizeof db.legacy_path,"%s",local_file);
        }
    }
    return sms_initialize_database(&db);
}
int WINAPI WinMain(HINSTANCE hInst,HINSTANCE previous,LPSTR command,int show)
{
    WNDCLASSA wc;MSG msg;int argc;(void)previous;(void)command;(void)show;instance=hInst;
    InitCommonControls();sms_set_error_stream(NULL);
    if(!prepare_storage()){MessageBoxA(NULL,sms_last_error(),APP_TITLE,MB_OK|MB_ICONERROR);return 1;}
    memset(&wc,0,sizeof wc);wc.lpfnWndProc=wndproc;wc.hInstance=hInst;wc.lpszClassName="StudentManagementStudioWindow";wc.hCursor=LoadCursor(NULL,IDC_ARROW);wc.hbrBackground=CreateSolidBrush(BG);wc.style=CS_HREDRAW|CS_VREDRAW;wc.hIcon=LoadIcon(NULL,IDI_APPLICATION);RegisterClassA(&wc);
    argc=GetSystemMetrics(SM_CXSCREEN);main_window=CreateWindowExA(0,wc.lpszClassName,APP_TITLE,WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,argc>1200?1180:1000,760,NULL,NULL,hInst,NULL);
    if(!main_window)return 1;ShowWindow(main_window,SW_SHOW);UpdateWindow(main_window);
    while(GetMessageA(&msg,NULL,0,0)>0){TranslateMessage(&msg);DispatchMessageA(&msg);}return (int)msg.wParam;
}
