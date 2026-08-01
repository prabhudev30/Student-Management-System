#include <stdio.h>

struct Student
{
    int roll;
    char name[50];
    float marks;
};

void addStudent()
{
    struct Student s;
    FILE *fp = fopen("students.txt", "a");

    printf("\nEnter Roll Number: ");
    scanf("%d", &s.roll);

    printf("Enter Name: ");
    scanf("%s", s.name);

    printf("Enter Marks: ");
    scanf("%f", &s.marks);

    fprintf(fp, "%d %s %.2f\n", s.roll, s.name, s.marks);

    fclose(fp);

    printf("\nStudent Added Successfully!\n");
}

void viewStudents()
{
    struct Student s;

    FILE *fp = fopen("students.txt", "r");

    if(fp == NULL)
    {
        printf("\nNo records found\n");
        return;
    }

    printf("\nRoll\tName\tMarks\n");

    while(fscanf(fp,"%d %s %f",&s.roll,s.name,&s.marks)!=EOF)
    {
        printf("%d\t%s\t%.2f\n", s.roll, s.name, s.marks);
    }

    fclose(fp);
}

int main()
{
    int choice;

    while(1)
    {
        printf("\n===== Student Management System =====\n");
        printf("1. Add Student\n");
        printf("2. View Students\n");
        printf("3. Exit\n");

        printf("Enter your choice: ");
        scanf("%d",&choice);

        switch(choice)
        {
            case 1:
                addStudent();
                break;

            case 2:
                viewStudents();
                break;

            case 3:
                printf("Exiting...\n");
                return 0;

            default:
                printf("Invalid choice\n");
        }
    }

    return 0;
}