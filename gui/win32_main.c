#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
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
#define ID_BRAND 1017
#define ID_PAGE_LABEL 1018
#define ID_STATUS 1019
#define ID_SEARCH_LABEL 1020
#define ID_FOOTER 1021
#define ID_BRAND_DETAIL 1022
#define ID_FOOTER_DETAIL 1023
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
static HWND main_window, table, search_box, search_label, status_bar, page_label, dashboard;
static HWND nav_dash, nav_students, nav_rank, brand, brand_detail, side_footer, side_footer_detail;
static Database db;
static int current_page = 0, sort_mode = 0, sort_reverse = 0;
static char data_dir[PATH_CAP],external_source_path[PATH_CAP];
static UINT dpi = 96;
static HFONT font_body, font_title, font_brand, font_small, font_stat;
static HBRUSH brush_background, brush_sidebar, brush_white, brush_blue;
static const COLORREF NAVY = RGB(22, 35, 61);
static const COLORREF BLUE = RGB(45, 103, 220);
static const COLORREF BG = RGB(244, 247, 251);
static const COLORREF INK = RGB(34, 46, 66);
static const COLORREF MUTED = RGB(103, 116, 136);
static const COLORREF BORDER = RGB(222, 228, 237);
static const COLORREF RED = RGB(191, 67, 72);

typedef struct { Student student; int is_edit; int attendance; int present; int accepted; } FormData;

#define S(value) ((int)(((long)(value) * (long)dpi + 48L) / 96L))

static void set_status(const char *text){if(status_bar)SetWindowTextA(status_bar,text);}
static void utf8_to_wide(const char *text,wchar_t *wide,int capacity);
static BOOL CALLBACK update_child_font(HWND child,LPARAM unused);
static void show_error(const char *title)
{
    wchar_t wide_title[128],wide_error[1024];utf8_to_wide(title,wide_title,128);utf8_to_wide(sms_last_error(),wide_error,1024);MessageBoxW(main_window,wide_error,wide_title,MB_OK|MB_ICONERROR);
}
static void rebuild_fonts(void)
{
    HFONT old_body=font_body,old_title=font_title,old_brand=font_brand,old_small=font_small,old_stat=font_stat;
    font_body=CreateFontA(-S(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,"Segoe UI");
    font_title=CreateFontA(-S(24),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,"Segoe UI");
    font_brand=CreateFontA(-S(17),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,"Segoe UI");
    font_small=CreateFontA(-S(12),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,"Segoe UI");
    font_stat=CreateFontA(-S(29),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,"Segoe UI");
    if(main_window)EnumChildWindows(main_window,update_child_font,0);
    if(old_body)DeleteObject(old_body);if(old_title)DeleteObject(old_title);if(old_brand)DeleteObject(old_brand);if(old_small)DeleteObject(old_small);if(old_stat)DeleteObject(old_stat);
}
static void set_font(HWND h,HFONT font){if(h&&font)SendMessageA(h,WM_SETFONT,(WPARAM)font,TRUE);}
static BOOL CALLBACK update_child_font(HWND child,LPARAM unused)
{
    int id=GetDlgCtrlID(child);(void)unused;
    set_font(child,id==ID_BRAND||id==ID_BRAND_DETAIL?font_brand:id==ID_PAGE_LABEL?font_title:id==ID_STATUS||id==ID_FOOTER||id==ID_FOOTER_DETAIL?font_small:font_body);return TRUE;
}
static void utf8_to_wide(const char *text,wchar_t *wide,int capacity)
{
    int converted=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,wide,capacity);
    if(!converted){MultiByteToWideChar(CP_ACP,0,text,-1,wide,capacity);wide[capacity-1]=L'\0';}
}
static int wide_to_utf8(HWND control,char *text,size_t capacity)
{
    wchar_t wide[512];int length=GetWindowTextW(control,wide,(int)(sizeof wide/sizeof wide[0]));int bytes;
    if(length<0||capacity>INT_MAX)return 0;
    bytes=WideCharToMultiByte(CP_UTF8,0,wide,-1,text,(int)capacity,NULL,NULL);
    return bytes>0;
}
static void add_control(const char *klass,const char *text,DWORD style,int x,int y,int w,int h,int id,HWND parent)
{
    HWND control=CreateWindowExA(0,klass,text,WS_CHILD|WS_VISIBLE|style,S(x),S(y),S(w),S(h),parent,(HMENU)(INT_PTR)id,instance,NULL);
    set_font(control,font_body);
}
static HWND button(HWND parent,const char *text,int id,int x,int y,int w,int h)
{
    HWND control=CreateWindowExA(0,"BUTTON",text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,S(x),S(y),S(w),S(h),parent,(HMENU)(INT_PTR)id,instance,NULL);
    SetWindowLongPtrA(control,GWLP_USERDATA,(LONG_PTR)text);set_font(control,font_body);return control;
}
static void draw_button(const DRAWITEMSTRUCT *item)
{
    HDC dc=item->hDC;RECT r=item->rcItem;int id=(int)item->CtlID;BOOL navigation=id==ID_DASH||id==ID_STUDENTS||id==ID_RANK;
    COLORREF fill,edge,text;const char *label=(const char *)GetWindowLongPtrA(item->hwndItem,GWLP_USERDATA);
    if(navigation){fill=(id==ID_DASH&&current_page==0)||(id==ID_STUDENTS&&current_page==1)||(id==ID_RANK&&current_page==2)?BLUE:NAVY;edge=fill;text=RGB(255,255,255);}
    else if(id==ID_ADD){fill=BLUE;edge=BLUE;text=RGB(255,255,255);}
    else{fill=RGB(255,255,255);edge=BORDER;text=id==ID_DELETE?RED:INK;}
    {HBRUSH b=CreateSolidBrush(fill),e=CreateSolidBrush(edge);FillRect(dc,&r,b);FrameRect(dc,&r,e);DeleteObject(b);DeleteObject(e);}
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,text);SelectObject(dc,font_body);
    if(navigation){r.left+=S(18);DrawTextA(dc,label,-1,&r,DT_SINGLELINE|DT_VCENTER|DT_LEFT);}
    else DrawTextA(dc,label,-1,&r,DT_SINGLELINE|DT_VCENTER|DT_CENTER|DT_END_ELLIPSIS);
    if(item->itemState&ODS_FOCUS){RECT focus=item->rcItem;InflateRect(&focus,-S(5),-S(5));DrawFocusRect(dc,&focus);}
}
static int set_list_text(int row,int column,const char *text)
{
    wchar_t wide[CSV_FIELD_CAP];LVITEMW item;utf8_to_wide(text,wide,(int)(sizeof wide/sizeof wide[0]));
    memset(&item,0,sizeof item);item.iSubItem=column;item.pszText=wide;
    return (int)SendMessageW(table,LVM_SETITEMTEXTW,(WPARAM)row,(LPARAM)&item);
}
static void fill_table(void)
{
    Student *copy;size_t i;char b[128];
    if(!table)return;ListView_DeleteAllItems(table);
    copy=sms_sorted_copy(&db,sort_mode==1?sms_compare_name:sort_mode==2?sms_compare_marks:sort_mode==3?sms_compare_class:sms_compare_roll);
    if(copy==NULL&&db.count!=0)return;
    if(sort_reverse&&copy)for(i=0;i<db.count/2;i++){Student t=copy[i];copy[i]=copy[db.count-1-i];copy[db.count-1-i]=t;}
    for(i=0;i<db.count;i++){
        LVITEMW item;int row;size_t j,rank=1;wchar_t wide[CSV_FIELD_CAP];
        for(j=0;j<db.count;j++)if(db.entries[j].marks>copy[i].marks)++rank;
        if((current_page==1||current_page==2)&&search_box){wchar_t qwide[256];char query[512],roll[32];query[0]='\0';GetWindowTextW(search_box,qwide,256);WideCharToMultiByte(CP_UTF8,0,qwide,-1,query,sizeof query,NULL,NULL);snprintf(roll,sizeof roll,"%d",copy[i].roll);if(query[0]&&!sms_text_contains(copy[i].name,query)&&!sms_text_contains(copy[i].class_name,query)&&!strstr(roll,query))continue;}
        memset(&item,0,sizeof item);item.mask=LVIF_TEXT;item.iItem=ListView_GetItemCount(table);snprintf(b,sizeof b,"%d",copy[i].roll);utf8_to_wide(b,wide,CSV_FIELD_CAP);item.pszText=wide;row=(int)SendMessageW(table,LVM_INSERTITEMW,0,(LPARAM)&item);if(row<0)continue;
        set_list_text(row,1,copy[i].name);set_list_text(row,2,copy[i].class_name);
        snprintf(b,sizeof b,"%.2f",copy[i].marks);set_list_text(row,3,b);set_list_text(row,4,sms_grade(copy[i].marks));set_list_text(row,5,copy[i].marks>=PASS_MARK?"PASS":"FAIL");
        if(copy[i].total==0)strcpy(b,"Not recorded");else snprintf(b,sizeof b,"%.1f%%  %d/%d",sms_attendance_percent(&copy[i]),copy[i].attended,copy[i].total);set_list_text(row,6,b);
        snprintf(b,sizeof b,"%zu",rank);set_list_text(row,7,b);
        {LVITEMW data_item;memset(&data_item,0,sizeof data_item);data_item.mask=LVIF_PARAM;data_item.iItem=row;data_item.lParam=(LPARAM)copy[i].roll;SendMessageW(table,LVM_SETITEMW,0,(LPARAM)&data_item);}
    }
    free(copy);
}
static void draw_stat_card(HDC dc,int x,int y,int width,int height,const char *label,const char *value,const char *note)
{
    RECT card={x,y,x+width,y+height},r;HGDIOBJ oldfont;HPEN pen;HGDIOBJ oldpen;
    pen=CreatePen(PS_SOLID,S(1),BORDER);oldpen=SelectObject(dc,pen);SelectObject(dc,brush_white);RoundRect(dc,card.left,card.top,card.right,card.bottom,S(10),S(10));
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,MUTED);oldfont=SelectObject(dc,font_small);r=card;r.left+=S(18);r.top+=S(13);r.bottom=r.top+S(20);DrawTextA(dc,label,-1,&r,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
    SetTextColor(dc,NAVY);SelectObject(dc,font_stat);r.top+=S(24);r.bottom=r.top+S(42);DrawTextA(dc,value,-1,&r,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
    SetTextColor(dc,MUTED);SelectObject(dc,font_small);r.top+=S(40);r.bottom=card.bottom-S(8);DrawTextA(dc,note,-1,&r,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
    SelectObject(dc,oldfont);SelectObject(dc,oldpen);DeleteObject(pen);
}
static LRESULT CALLBACK dashboard_proc(HWND w,UINT message,WPARAM wp,LPARAM lp)
{
    if(message==WM_ERASEBKGND){RECT r;GetClientRect(w,&r);FillRect((HDC)wp,&r,brush_background);return 1;}
    if(message==WM_PAINT){PAINTSTRUCT ps;RECT r;int margin=S(10),gap=S(12),card_h=S(112),top_width,bottom_width;char value[80],note[120];Summary s=sms_summarize(db.entries,db.count);HDC dc=BeginPaint(w,&ps);GetClientRect(w,&r);top_width=(r.right-2*margin-2*gap)/3;bottom_width=(r.right-2*margin-gap)/2;
        snprintf(value,sizeof value,"%zu",s.count);draw_stat_card(dc,margin,margin,top_width,card_h,"STUDENTS",value,"Current records");
        snprintf(value,sizeof value,"%.2f",s.count?s.sum/(double)s.count:0.0);draw_stat_card(dc,margin+top_width+gap,margin,top_width,card_h,"CLASS AVERAGE",value,"Marks out of 100");
        snprintf(value,sizeof value,"%.0f%%",s.count?100.0*(double)s.passed/(double)s.count:0.0);snprintf(note,sizeof note,"%zu passed / %zu failed",s.passed,s.count-s.passed);draw_stat_card(dc,margin+2*(top_width+gap),margin,top_width,card_h,"PASS RATE",value,note);
        snprintf(value,sizeof value,"%.2f",s.count?s.maximum:0.0);draw_stat_card(dc,margin,margin+card_h+gap,bottom_width,card_h,"HIGHEST MARKS",value,"Best result in this group");
        snprintf(value,sizeof value,"%zu",s.low_attendance);draw_stat_card(dc,margin+bottom_width+gap,margin+card_h+gap,bottom_width,card_h,"LOW ATTENDANCE",value,"Below 75 percent");
        EndPaint(w,&ps);return 0;}
    return DefWindowProcA(w,message,wp,lp);
}
static void refresh_dashboard(void){if(dashboard)InvalidateRect(dashboard,NULL,TRUE);}
static int selected_roll(void)
{
    int row=ListView_GetNextItem(table,-1,LVNI_SELECTED);LVITEMW item;
    if(row<0)return 0;memset(&item,0,sizeof item);item.mask=LVIF_PARAM;item.iItem=row;
    return SendMessageW(table,LVM_GETITEMW,0,(LPARAM)&item)?(int)item.lParam:0;
}
static Student *student_by_roll(int roll)
{
    size_t i;for(i=0;i<db.count;i++)if(db.entries[i].roll==roll)return &db.entries[i];return NULL;
}
static void layout_controls(HWND w)
{
    RECT r;int width,height,left=S(250),right=S(24),content,gap=S(8),button_w,row,x,i;
    int ids[]={ID_ADD,ID_EDIT,ID_DELETE,ID_VIEW,ID_ATTEND,ID_IMPORT,ID_EXPORT,ID_BACKUP,ID_RESTORE,ID_REPORT};
    GetClientRect(w,&r);width=r.right;height=r.bottom;content=width-left-right;
    if(content<S(440))content=S(440);
    if(brand)MoveWindow(brand,S(22),S(25),S(184),S(25),TRUE);
    if(brand_detail)MoveWindow(brand_detail,S(22),S(49),S(184),S(25),TRUE);
    if(nav_dash)MoveWindow(nav_dash,S(16),S(112),S(190),S(42),TRUE);
    if(nav_students)MoveWindow(nav_students,S(16),S(162),S(190),S(42),TRUE);
    if(nav_rank)MoveWindow(nav_rank,S(16),S(212),S(190),S(42),TRUE);
    if(side_footer)MoveWindow(side_footer,S(22),height-S(74),S(184),S(20),TRUE);
    if(side_footer_detail)MoveWindow(side_footer_detail,S(22),height-S(52),S(184),S(20),TRUE);
    if(page_label)MoveWindow(page_label,left,S(22),content,S(38),TRUE);
    button_w=(content-4*gap)/5;
    for(i=0;i<10;i++){row=i/5;x=left+(i%5)*(button_w+gap);if(GetDlgItem(w,ids[i]))MoveWindow(GetDlgItem(w,ids[i]),x,S(72+row*42),button_w,S(34),TRUE);}
    if(search_label)MoveWindow(search_label,left,S(164),S(92),S(32),TRUE);
    if(search_box)MoveWindow(search_box,left+S(94),S(162),content-S(94),S(34),TRUE);
    if(dashboard)MoveWindow(dashboard,left,S(164),content,height-S(238),TRUE);
    if(table)MoveWindow(table,left,S(204),content,height-S(260),TRUE);
    if(status_bar)MoveWindow(status_bar,left,height-S(42),content,S(24),TRUE);
}
static void set_page(int page)
{
    current_page=page;
    if(page_label)SetWindowTextA(page_label,page==0?"Dashboard":page==1?"Student directory":"Academic rankings");
    if(search_label)ShowWindow(search_label,page==1||page==2?SW_SHOW:SW_HIDE);
    if(search_box)ShowWindow(search_box,page==1||page==2?SW_SHOW:SW_HIDE);
    if(dashboard)ShowWindow(dashboard,page==0?SW_SHOW:SW_HIDE);
    if(table)ShowWindow(table,page==0?SW_HIDE:SW_SHOW);
    if(page==2){sort_mode=2;sort_reverse=0;}
    if(nav_dash)InvalidateRect(nav_dash,NULL,TRUE);if(nav_students)InvalidateRect(nav_students,NULL,TRUE);if(nav_rank)InvalidateRect(nav_rank,NULL,TRUE);
    fill_table();refresh_dashboard();
}
static void columns_init(HWND list)
{
    static const char *names[]={"Roll","Student name","Class","Marks","Grade","Result","Attendance","Rank"};
    static const int widths[]={66,190,112,78,62,76,142,58};int i;LVCOLUMNW c;
    for(i=0;i<8;i++){wchar_t wide[64];utf8_to_wide(names[i],wide,64);memset(&c,0,sizeof c);c.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_SUBITEM;c.pszText=wide;c.cx=S(widths[i]);c.iSubItem=i;SendMessageW(list,LVM_INSERTCOLUMNW,(WPARAM)i,(LPARAM)&c);}
}
static void add_label(HWND w,const char *text,int x,int y,int width)
{ add_control("STATIC",text,SS_LEFT,x,y,width,22,0,w); }
static int form_window(HWND owner,const char *title,FormData *data)
{
    HWND w,e_name,e_class,e_marks,e_roll,status=NULL;MSG msg;int result;
    w=CreateWindowExA(WS_EX_DLGMODALFRAME,"SMS_FORM",title,WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,S(430),S(350),owner,NULL,instance,data);
    if(!w)return 0;
    EnableWindow(owner,FALSE);
    add_label(w,"Roll number",24,24,110);e_roll=CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT","",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,S(145),S(22),S(240),S(30),w,(HMENU)ID_FORM_ROLL,instance,NULL);
    add_label(w,"Student name",24,70,110);e_name=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,S(145),S(68),S(240),S(30),w,(HMENU)ID_FORM_NAME,instance,NULL);
    add_label(w,"Class",24,116,110);e_class=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,S(145),S(114),S(240),S(30),w,(HMENU)ID_FORM_CLASS,instance,NULL);
    add_label(w,"Marks (0-100)",24,162,110);e_marks=CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT","",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,S(145),S(160),S(240),S(30),w,(HMENU)ID_FORM_MARKS,instance,NULL);
    if(data->attendance){
        EnableWindow(e_roll,FALSE);EnableWindow(e_name,FALSE);EnableWindow(e_class,FALSE);EnableWindow(e_marks,FALSE);
        add_label(w,"Mark this session",24,210,110);
        status=CreateWindowExA(0,"COMBOBOX","",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST,S(145),S(206),S(240),S(100),w,(HMENU)ID_FORM_STATUS,instance,NULL);
        SendMessageA(status,CB_ADDSTRING,0,(LPARAM)"Present");SendMessageA(status,CB_ADDSTRING,0,(LPARAM)"Absent");SendMessageA(status,CB_SETCURSEL,0,0);
    }
    set_font(e_roll,font_body);set_font(e_name,font_body);set_font(e_class,font_body);set_font(e_marks,font_body);set_font(status,font_body);
    if(data->student.roll>0){char text[40];snprintf(text,sizeof text,"%d",data->student.roll);SetWindowTextA(e_roll,text);SendMessageA(e_roll,EM_SETREADONLY,TRUE,0);}
    if(data->student.name[0]){wchar_t wide[NAME_LEN];utf8_to_wide(data->student.name,wide,NAME_LEN);SetWindowTextW(e_name,wide);}
    if(data->student.class_name[0]){wchar_t wide[CLASS_LEN];utf8_to_wide(data->student.class_name,wide,CLASS_LEN);SetWindowTextW(e_class,wide);}
    if(data->student.marks>=0){char text[40];snprintf(text,sizeof text,"%.2f",data->student.marks);SetWindowTextA(e_marks,text);}
    button(w,data->attendance?"Save attendance":"Save student",ID_FORM_OK,145,285,118,36);button(w,"Cancel",IDCANCEL,275,285,110,36);
    SetFocus(data->attendance?status:e_name);
    while(IsWindow(w)&&GetMessageA(&msg,NULL,0,0)>0){if(!IsDialogMessageA(w,&msg)){TranslateMessage(&msg);DispatchMessageA(&msg);}}
    EnableWindow(owner,TRUE);SetForegroundWindow(owner);result=data->accepted;return result;
}
static LRESULT CALLBACK form_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp)
{
    if(msg==WM_CREATE){SetPropA(w,"SMS_DATA",(HANDLE)((CREATESTRUCTA*)lp)->lpCreateParams);return 0;}
    if(msg==WM_COMMAND && LOWORD(wp)==ID_FORM_OK){
        FormData *d=(FormData*)GetPropA(w,"SMS_DATA"); char a[256]; int roll; double marks;
        if(!d->attendance){
            GetWindowTextA(GetDlgItem(w,ID_FORM_ROLL),a,sizeof a);
            if(!sms_parse_integer(a,1,INT_MAX,&roll)||!wide_to_utf8(GetDlgItem(w,ID_FORM_NAME),d->student.name,sizeof d->student.name)||!wide_to_utf8(GetDlgItem(w,ID_FORM_CLASS),d->student.class_name,sizeof d->student.class_name)||!sms_valid_text(d->student.name)||!sms_valid_text(d->student.class_name)||!sms_parse_number(GetWindowTextA(GetDlgItem(w,ID_FORM_MARKS),a,sizeof a)?a:"",0,100,&marks)){
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
    int roll=selected_roll();Student *student=student_by_roll(roll);wchar_t question[512],name[NAME_LEN];
    if(!student){MessageBoxA(main_window,"Select a student row first.",APP_TITLE,MB_OK|MB_ICONINFORMATION);return;}
    utf8_to_wide(student->name,name,NAME_LEN);swprintf(question,512,L"Delete %ls (roll %d)? This cannot be undone.",name,roll);
    if(MessageBoxW(main_window,question,L"Confirm deletion",MB_YESNO|MB_ICONWARNING)!=IDYES)return;
    if(!sms_delete_student(&db,roll)){show_error("Could not delete student");return;}fill_table();refresh_dashboard();set_status("Student deleted.");
}
static void view_student(void)
{
    int roll=selected_roll();Student *student=student_by_roll(roll);wchar_t text[1200],name[NAME_LEN],class_name[CLASS_LEN],grade[8],result[16],attendance[32];
    if(!student){MessageBoxA(main_window,"Select a student row first.",APP_TITLE,MB_OK|MB_ICONINFORMATION);return;}
    utf8_to_wide(student->name,name,NAME_LEN);utf8_to_wide(student->class_name,class_name,CLASS_LEN);utf8_to_wide(sms_grade(student->marks),grade,8);utf8_to_wide(student->marks>=PASS_MARK?"PASS":"FAIL",result,16);utf8_to_wide(student->total?"Recorded":"Not recorded",attendance,32);
    swprintf(text,1200,L"Roll: %d\nName: %ls\nClass: %ls\nMarks: %.2f / 100\nGrade: %ls\nResult: %ls\nAttendance: %ls",student->roll,name,class_name,student->marks,grade,result,attendance);
    MessageBoxW(main_window,text,L"Student profile",MB_OK|MB_ICONINFORMATION);
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
static int remember_external_source(const char *source)
{
    char destination[PATH_CAP],temporary[DERIVED_PATH_CAP];int n=snprintf(destination,sizeof destination,"%s\\migration-external-source.path",data_dir),ok=1;FILE *file;
    if(n<0||(size_t)n>=sizeof destination)return 0;
    n=snprintf(temporary,sizeof temporary,"%s.tmp",destination);if(n<0||(size_t)n>=sizeof temporary)return 0;
    file=fopen(temporary,"w");if(!file)return 0;
    if(fputs(source,file)==EOF||fputc('\n',file)==EOF)ok=0;
    if(fflush(file)!=0)ok=0;
    if(fclose(file)!=0)ok=0;
    if(!ok){remove(temporary);return 0;}
    if(!MoveFileExA(temporary,destination,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){remove(temporary);return 0;}
    return 1;
}
static void load_external_source(void)
{
    char path[PATH_CAP];int n=snprintf(path,sizeof path,"%s\\migration-external-source.path",data_dir);FILE *file;
    if(n<0||(size_t)n>=sizeof path)return;file=fopen(path,"r");if(!file)return;
    if(fgets(external_source_path,sizeof external_source_path,file))sms_trim(external_source_path);fclose(file);
}
static int is_preserved_source_path(const char *path)
{
    char module[PATH_CAP],*slash;DWORD length;const char *base=path,*cursor;
    for(cursor=path;*cursor;++cursor)if(*cursor=='\\'||*cursor=='/')base=cursor+1;
    if(sms_text_equal(base,"students.csv")||sms_text_equal(base,"students.txt"))return 1;
    if(external_source_path[0]&&sms_same_path(path,external_source_path))return 1;
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
        INITCOMMONCONTROLSEX ic={sizeof ic,ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES};HWND h;
        InitCommonControlsEx(&ic);
        brand=CreateWindowExA(0,"STATIC","STUDENT",WS_CHILD|WS_VISIBLE|SS_LEFT,0,0,0,0,w,(HMENU)ID_BRAND,instance,NULL);
        brand_detail=CreateWindowExA(0,"STATIC","MANAGEMENT",WS_CHILD|WS_VISIBLE|SS_LEFT,0,0,0,0,w,(HMENU)ID_BRAND_DETAIL,instance,NULL);
        side_footer=CreateWindowExA(0,"STATIC","OFFLINE APPLICATION",WS_CHILD|WS_VISIBLE|SS_LEFT,0,0,0,0,w,(HMENU)ID_FOOTER,instance,NULL);
        side_footer_detail=CreateWindowExA(0,"STATIC","Saved in your user folder",WS_CHILD|WS_VISIBLE|SS_LEFT,0,0,0,0,w,(HMENU)ID_FOOTER_DETAIL,instance,NULL);
        nav_dash=button(w,"Dashboard",ID_DASH,0,0,1,1);nav_students=button(w,"Students",ID_STUDENTS,0,0,1,1);nav_rank=button(w,"Rankings",ID_RANK,0,0,1,1);
        page_label=CreateWindowExA(0,"STATIC","Dashboard",WS_CHILD|WS_VISIBLE|SS_LEFT,0,0,0,0,w,(HMENU)ID_PAGE_LABEL,instance,NULL);set_font(page_label,font_title);
        search_label=CreateWindowExA(0,"STATIC","Search",WS_CHILD|SS_LEFT,0,0,0,0,w,(HMENU)ID_SEARCH_LABEL,instance,NULL);set_font(search_label,font_body);
        search_box=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|ES_AUTOHSCROLL|WS_TABSTOP,0,0,0,0,w,(HMENU)ID_SEARCH,instance,NULL);set_font(search_box,font_body);
        dashboard=CreateWindowExA(0,"SMS_DASHBOARD","",WS_CHILD|WS_VISIBLE,0,0,0,0,w,NULL,instance,NULL);
        table=CreateWindowExW(WS_EX_CLIENTEDGE,WC_LISTVIEWW,L"",WS_CHILD|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_TABSTOP,0,0,0,0,w,(HMENU)ID_TABLE,instance,NULL);
        ListView_SetExtendedListViewStyle(table,LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);set_font(table,font_body);columns_init(table);
        button(w,"Add student",ID_ADD,0,0,1,1);button(w,"Edit",ID_EDIT,0,0,1,1);button(w,"Delete",ID_DELETE,0,0,1,1);button(w,"View",ID_VIEW,0,0,1,1);button(w,"Attendance",ID_ATTEND,0,0,1,1);
        button(w,"Import CSV",ID_IMPORT,0,0,1,1);button(w,"Export CSV",ID_EXPORT,0,0,1,1);button(w,"Backup",ID_BACKUP,0,0,1,1);button(w,"Restore",ID_RESTORE,0,0,1,1);button(w,"Report",ID_REPORT,0,0,1,1);
        status_bar=CreateWindowExA(0,"STATIC","Ready - changes save automatically",WS_CHILD|WS_VISIBLE|SS_LEFT,0,0,0,0,w,(HMENU)ID_STATUS,instance,NULL);
        (void)h;EnumChildWindows(w,update_child_font,0);layout_controls(w);set_page(0);return 0;}
    case WM_SIZE:layout_controls(w);return 0;
    case WM_GETMINMAXINFO:{MINMAXINFO *info=(MINMAXINFO*)lp;info->ptMinTrackSize.x=S(940);info->ptMinTrackSize.y=S(650);return 0;}
    case WM_DPICHANGED:{RECT *suggested=(RECT*)lp;static const int widths[]={66,190,112,78,62,76,142,58};int i;dpi=HIWORD(wp);if(!dpi)dpi=96;rebuild_fonts();SetWindowPos(w,NULL,suggested->left,suggested->top,suggested->right-suggested->left,suggested->bottom-suggested->top,SWP_NOZORDER|SWP_NOACTIVATE);layout_controls(w);for(i=0;i<8;i++)ListView_SetColumnWidth(table,i,S(widths[i]));InvalidateRect(w,NULL,TRUE);refresh_dashboard();return 0;}
    case WM_ERASEBKGND:{RECT r,side,accent;GetClientRect(w,&r);FillRect((HDC)wp,&r,brush_background);side=r;side.right=S(224);FillRect((HDC)wp,&side,brush_sidebar);accent=side;accent.right=S(4);accent.bottom=S(320);FillRect((HDC)wp,&accent,brush_blue);return 1;}
    case WM_CTLCOLORSTATIC:{HDC dc=(HDC)wp;HWND child=(HWND)lp;int id=GetDlgCtrlID(child);if(id==ID_BRAND||id==ID_BRAND_DETAIL||id==ID_FOOTER||id==ID_FOOTER_DETAIL){SetTextColor(dc,RGB(255,255,255));SetBkColor(dc,NAVY);SetBkMode(dc,OPAQUE);return (LRESULT)brush_sidebar;}SetTextColor(dc,id==ID_STATUS?MUTED:INK);SetBkColor(dc,BG);SetBkMode(dc,OPAQUE);return (LRESULT)brush_background;}
    case WM_DRAWITEM:draw_button((const DRAWITEMSTRUCT*)lp);return TRUE;
    case WM_NOTIFY:{LPNMHDR n=(LPNMHDR)lp;if(n->idFrom==ID_TABLE&&n->code==LVN_COLUMNCLICK){LPNMLISTVIEW v=(LPNMLISTVIEW)lp;sort_mode=v->iSubItem==1?1:v->iSubItem==3||v->iSubItem==4||v->iSubItem==7?2:v->iSubItem==2?3:0;sort_reverse=!sort_reverse;fill_table();}return 0;}
    case WM_COMMAND:switch(LOWORD(wp)){
    case ID_DASH:set_page(0);break;case ID_STUDENTS:set_page(1);break;case ID_RANK:set_page(2);break;
    case ID_ADD:add_student();break;case ID_EDIT:edit_student();break;case ID_DELETE:delete_student();break;case ID_VIEW:view_student();break;case ID_ATTEND:attendance_form();break;
    case ID_IMPORT:choose_csv(0,"Import CSV");break;case ID_EXPORT:choose_csv(1,"Export CSV");break;case ID_BACKUP:backup_dialog();break;case ID_RESTORE:restore_dialog();break;case ID_REPORT:show_report();break;
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
static int make_marker(const char *name)
{
    char path[PATH_CAP];DWORD written;HANDLE file;int n=snprintf(path,sizeof path,"%s\\%s",data_dir,name);
    static const char marker[]="migration decision recorded\r\n";
    if(n<0||(size_t)n>=sizeof path)return 0;
    file=CreateFileA(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_HIDDEN,NULL);
    if(file==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_FILE_EXISTS||GetLastError()==ERROR_ALREADY_EXISTS;
    if(!WriteFile(file,marker,(DWORD)(sizeof marker-1),&written,NULL)||written!=sizeof marker-1||!FlushFileBuffers(file)){CloseHandle(file);DeleteFileA(path);return 0;}
    CloseHandle(file);return 1;
}
static int marker_exists(const char *name)
{
    char path[PATH_CAP];int n=snprintf(path,sizeof path,"%s\\%s",data_dir,name);
    if(n<0||(size_t)n>=sizeof path)return -1;return sms_file_exists(path);
}
static void merge_source_once(const char *path,const char *marker,int legacy)
{
    int done=marker_exists(marker),source_state,answer;char question[1200],marker_file[PATH_CAP];size_t added=0,skipped=0;
    if(done!=0)return;
    source_state=sms_file_exists(path);if(source_state!=1)return;
    snprintf(question,sizeof question,"An existing %s file was found:\n\n%s\n\nMerge records whose roll numbers are not already in the app? Existing students will not be changed, and this source file will remain untouched.",legacy?"legacy students.txt":"students.csv",path);
    answer=MessageBoxA(NULL,question,"Preserve and merge existing records?",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON1);
    if(strcmp(marker,"migration-external-csv.done")==0){
        if(!remember_external_source(path)){MessageBoxA(NULL,"The selected source path could not be protected in your user-data folder, so no merge was performed.",APP_TITLE,MB_OK|MB_ICONERROR);return;}
        snprintf(external_source_path,sizeof external_source_path,"%s",path);
    }
    if(!make_marker(marker)){ MessageBoxA(NULL,"The migration decision could not be recorded, so no records were changed.",APP_TITLE,MB_OK|MB_ICONWARNING);return;}
    if(answer==IDYES){
        int ok=legacy?sms_import_legacy(&db,path,&added,&skipped):sms_import_csv(&db,path,&added,&skipped);
        if(!ok){snprintf(marker_file,sizeof marker_file,"%s\\%s",data_dir,marker);DeleteFileA(marker_file);show_error("Record migration was not completed");return;}
        {char message[256];snprintf(message,sizeof message,"Safe merge complete: %zu new record(s), %zu existing roll(s) skipped. The source file was preserved.",added,skipped);MessageBoxA(NULL,message,"Migration complete",MB_OK|MB_ICONINFORMATION);}
    }
}
static void offer_external_csv_once(void)
{
    int done=marker_exists("migration-external-offer-v2.done");
    if(done!=0)return;
    if(MessageBoxA(NULL,"If your earlier console database is in another folder, choose Yes to locate its students.csv and safely merge missing rolls. Choose No to continue without importing another file.","Find previous console records?",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON1)==IDYES){
        char path[MAX_PATH]="";OPENFILENAMEA ofn;memset(&ofn,0,sizeof ofn);ofn.lStructSize=sizeof ofn;ofn.lpstrTitle="Select the previous console CSV";ofn.lpstrFilter="CSV files (*.csv)\0*.csv\0All files (*.*)\0*.*\0";ofn.lpstrFile=path;ofn.nMaxFile=sizeof path;ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
        if(GetOpenFileNameA(&ofn))merge_source_once(path,"migration-external-csv.done",0);
        if(marker_exists("migration-external-csv.done")==1)make_marker("migration-external-offer-v2.done");
    }else make_marker("migration-external-offer-v2.done");
}
static int prepare_storage(void)
{
    char appdata[PATH_CAP],local_csv[PATH_CAP],local_text[PATH_CAP],local_dir[PATH_CAP],*separator,app_legacy[PATH_CAP];
    DWORD n=GetEnvironmentVariableA("APPDATA",appdata,sizeof appdata),exe_length;
    int database_exists,local_csv_state,local_text_state,app_legacy_state,copied_csv=0;
    if(!n||n>=sizeof appdata)return 0;
    if(snprintf(data_dir,sizeof data_dir,"%s\\StudentManagementSystem",appdata)>=(int)sizeof data_dir)return 0;
    if(!CreateDirectoryA(data_dir,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS)return 0;
    load_external_source();
    if(snprintf(db.path,sizeof db.path,"%s\\students.csv",data_dir)>=(int)sizeof db.path)return 0;
    if(snprintf(db.backup_path,sizeof db.backup_path,"%s\\students.csv.bak",data_dir)>=(int)sizeof db.backup_path)return 0;
    db.migrate_legacy=1;
    if(snprintf(db.legacy_path,sizeof db.legacy_path,"%s\\students.txt",data_dir)>=(int)sizeof db.legacy_path)return 0;
    if(snprintf(app_legacy,sizeof app_legacy,"%s\\students.txt",data_dir)>=(int)sizeof app_legacy)return 0;
    database_exists=sms_file_exists(db.path);if(database_exists<0)return 0;
    exe_length=GetModuleFileNameA(NULL,local_dir,sizeof local_dir);if(!exe_length||exe_length>=sizeof local_dir)return 0;
    separator=strrchr(local_dir,'\\');if(!separator)return 0;
    if(separator==local_dir+2&&local_dir[1]==':')separator[1]='\0';else *separator='\0';
    if(snprintf(local_csv,sizeof local_csv,"%s\\students.csv",local_dir)>=(int)sizeof local_csv)return 0;
    if(snprintf(local_text,sizeof local_text,"%s\\students.txt",local_dir)>=(int)sizeof local_text)return 0;
    local_csv_state=sms_file_exists(local_csv);local_text_state=sms_file_exists(local_text);app_legacy_state=sms_file_exists(app_legacy);
    if(local_csv_state<0||local_text_state<0||app_legacy_state<0)return 0;
    if(database_exists==0&&local_csv_state==1){
        if(!copy_csv_without_replace(local_csv,db.path)){MessageBoxA(NULL,"Could not safely copy the existing students.csv into the user-data folder. The source was left untouched.",APP_TITLE,MB_OK|MB_ICONERROR);return 0;}
        copied_csv=1;
    }
    if(database_exists==0&&local_csv_state==0&&local_text_state==1)strncpy(db.legacy_path,local_text,sizeof db.legacy_path-1);
    if(!sms_initialize_database(&db))return 0;
    if(copied_csv&&!make_marker("migration-local-csv.done"))MessageBoxA(NULL,"The database was copied safely, but a migration marker could not be saved.",APP_TITLE,MB_OK|MB_ICONWARNING);
    if(db.migrated_from_legacy){
        int marker_ok=1;
        if(sms_same_path(db.migration_source,local_text))marker_ok=make_marker("migration-local-text.done");
        if(sms_same_path(db.migration_source,app_legacy))marker_ok=make_marker("migration-appdata-text.done")&&marker_ok;
        if(!marker_ok)MessageBoxA(NULL,"Legacy records were migrated safely, but the migration marker could not be saved. The source remains untouched.",APP_TITLE,MB_OK|MB_ICONWARNING);
    }
    if(local_csv_state==1)merge_source_once(local_csv,"migration-local-csv.done",0);
    if(local_text_state==1)merge_source_once(local_text,"migration-local-text.done",1);
    if(app_legacy_state==1&&!sms_same_path(app_legacy,local_text))merge_source_once(app_legacy,"migration-appdata-text.done",1);
    if(local_csv_state==1||local_text_state==1||app_legacy_state==1)make_marker("migration-external-offer-v2.done");
    else offer_external_csv_once();
    return 1;
}
int WINAPI WinMain(HINSTANCE hInst,HINSTANCE previous,LPSTR command,int show)
{
    WNDCLASSA wc,dash;MSG msg;RECT work;int width,height;(void)previous;(void)command;(void)show;instance=hInst;
    {typedef UINT (WINAPI *GET_DPI_FOR_SYSTEM)(void);HMODULE user=GetModuleHandleA("user32.dll");GET_DPI_FOR_SYSTEM get_dpi=(GET_DPI_FOR_SYSTEM)GetProcAddress(user,"GetDpiForSystem");if(get_dpi)dpi=get_dpi();}
    if(!dpi)dpi=96;
    rebuild_fonts();brush_background=CreateSolidBrush(BG);brush_sidebar=CreateSolidBrush(NAVY);brush_white=CreateSolidBrush(RGB(255,255,255));brush_blue=CreateSolidBrush(BLUE);
    InitCommonControls();sms_set_error_stream(NULL);
    if(!prepare_storage()){wchar_t error[1024];utf8_to_wide(sms_last_error(),error,1024);MessageBoxW(NULL,error,L"Student Management Studio",MB_OK|MB_ICONERROR);return 1;}
    memset(&dash,0,sizeof dash);dash.lpfnWndProc=dashboard_proc;dash.hInstance=hInst;dash.lpszClassName="SMS_DASHBOARD";dash.hCursor=LoadCursor(NULL,IDC_ARROW);dash.hbrBackground=brush_background;RegisterClassA(&dash);
    memset(&wc,0,sizeof wc);wc.lpfnWndProc=wndproc;wc.hInstance=hInst;wc.lpszClassName="StudentManagementStudioWindow";wc.hCursor=LoadCursor(NULL,IDC_ARROW);wc.hbrBackground=brush_background;wc.style=CS_HREDRAW|CS_VREDRAW;wc.hIcon=LoadIcon(NULL,IDI_APPLICATION);if(!RegisterClassA(&wc))return 1;
    if(!SystemParametersInfoA(SPI_GETWORKAREA,0,&work,0)){work.left=0;work.top=0;work.right=GetSystemMetrics(SM_CXSCREEN);work.bottom=GetSystemMetrics(SM_CYSCREEN);}
    width=(work.right-work.left)*96/(int)dpi-40;height=(work.bottom-work.top)*96/(int)dpi-70;if(width>1180)width=1180;if(width<940)width=940;if(height>820)height=820;if(height<650)height=650;
    main_window=CreateWindowExA(0,wc.lpszClassName,APP_TITLE,WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,S(width),S(height),NULL,NULL,hInst,NULL);
    if(!main_window)return 1;ShowWindow(main_window,show?show:SW_SHOW);UpdateWindow(main_window);
    while(GetMessageA(&msg,NULL,0,0)>0){TranslateMessage(&msg);DispatchMessageA(&msg);}return (int)msg.wParam;
}
