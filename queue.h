#ifndef EXT2CHECK_C_QUEUE_H
#define EXT2CHECK_C_QUEUE_H
#include<stdio.h>
#include<fcntl.h>
#include<unistd.h>
#include<stdlib.h>

struct queue_error_inodes{
    int in;
    struct queue_error_inodes* next;
};
struct queue_error_inodes *h_occup = NULL, *t_occup = NULL;//очередь хранения поврежденных инодов с ссылками на один и тот же блок данных
struct queue_error_inodes *h_perm = NULL, *t_perm = NULL;//очередь хранения поврежденных инодов без разрешений
struct queue_error_inodes *h_lost = NULL, *t_lost = NULL;//очередь хранения поврежденных инодов без ссылок из коталогов
struct queue_error_inodes *h_freeButOccup = NULL, *t_freeButOccup = NULL;//очередь хранения поврежденных инодов, отмеченных в битмапе инодов как занятые, но на самом деле свободные
struct queue_error_inodes *h_occupButFree = NULL, *t_occupButFree = NULL;//очередь хранения поврежденных инодов, отмеченных в битмапе инодов как свободные, но на самом деле занятые

void push_error_inode(struct queue_error_inodes** h, struct queue_error_inodes** t, int in) {
    struct queue_error_inodes* temp;
    if (!(temp = (struct queue_error_inodes*)calloc(1, sizeof(struct queue_error_inodes)))) {
        printf("Memory is not allocated\n");
        return;
    }
    temp->next = NULL;
    temp->in = in;
    if (!(*h))
        *t=*h=temp;
    else {
        (*t)->next=temp;
        *t = temp;
    }
}
void print_error_inode(struct queue_error_inodes* h){
    if (!h)
    {
        puts("Queue is empty\n");
        return;
    }
    do
    {
        printf("%d ", h->in);
        h = h->next;
    } while (h);
}

#endif //EXT2CHECK_C_QUEUE_H
