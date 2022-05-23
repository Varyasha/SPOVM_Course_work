#include<stdio.h>
#include<fcntl.h>
#include<unistd.h>
#include<stdlib.h>
#include<linux/fs.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<string.h>
#include<ext2fs/ext2_fs.h>
#include<errno.h>

#define block_size (1024 << super.s_log_block_size)
#define BASE_OFFSET 1024
#define BLOCK_OFFSET(block) (BASE_OFFSET + (block-1)*block_size)
#define EXT2_NDIR_BLOCKS    12
#define EXT2_IND_BLOCK      EXT2_NDIR_BLOCKS
#define EXT2_DIND_BLOCK     (EXT2_IND_BLOCK + 1)
#define EXT2_TIND_BLOCK     (EXT2_DIND_BLOCK + 1)
#define EXT2_N_BLOCKS     (EXT2_TIND_BLOCK + 1)

struct queue_error_inodes{
    int in;
    struct queue_error_inodes* next;
};
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
    return;
}

int sd;
struct ext2_super_block super;
//struct ext2_group_desc desc;
//struct ext2_inode inode;

char** actual_inode_bitmap;
int *occupied_blocks = 0;//1, если блок уже имеет ссылку на такой блок. 0 в противном случае
int group_count;
int actual_inode_count;
int is_clean = 1;
struct queue_error_inodes *h_occup = NULL, *t_occup = NULL;//очередь хранения поврежденных инодов с ссылками на один и тот же блок данных
struct queue_error_inodes *h_perm = NULL, *t_perm = NULL;//очередь хранения поврежденных инодов без разрешений
struct queue_error_inodes *h_lost = NULL, *t_lost = NULL;//очередь хранения поврежденных инодов без ссылок из коталогов
struct queue_error_inodes *h_freeButOccup = NULL, *t_freeButOccup = NULL;//очередь хранения поврежденных инодов, отмеченных в битмапе инодов как занятые, но на самом деле свободные
struct queue_error_inodes *h_occupButFree = NULL, *t_occupButFree = NULL;//очередь хранения поврежденных инодов, отмеченных в битмапе инодов как свободные, но на самом деле занятые

void free_memory() {
    int i;
    for (i = 0; i < group_count; i++) {
        free(actual_inode_bitmap[i]);
    }
    free(actual_inode_bitmap);
    free(occupied_blocks);
}

void printInfo (int sd){
    lseek(sd, 1024, SEEK_SET);
    read(sd, &super, sizeof(struct ext2_super_block));
    printf("--- FILESYSTEM INFO ---\n");
    printf("Inodes count: %d\n", super.s_inodes_count);
    printf("Blocks count: %d\n", super.s_blocks_count);
    printf("Free inodes: %d\n", super.s_free_inodes_count);
    printf("Free blocks: %d\n", super.s_free_blocks_count);
    printf("First data block: %d\n", super.s_first_data_block);
    printf("Block size: ");
    switch(super.s_log_block_size){
        case 0:{
            printf("1024\n");
        }break;
        case 1:{
            printf("2048\n");
        }break;
        case 2:{
            printf("4096\n");
        }break;
    }
    printf("Inode size: %d\n", super.s_inode_size);
    printf("Blocks per group: %d\n", super.s_blocks_per_group);
    printf("Inodes per group: %d\n", super.s_inodes_per_group);
    printf("Magic number: %d\n", super.s_magic);
    printf("First inode: %d\n", super.s_first_ino);
    printf("Filesystem state: ");
    switch(super.s_state){
        case 0:{
            printf("not clean\n");
        }break;
        case 1:{
            printf("clean\n");
        }break;
    }
}

int checkSuperblock(int sd) {
    lseek(sd, 1024, SEEK_SET);
    read(sd, &super, sizeof(super));
    printf("magic is %d\n", super.s_magic);
    if(super.s_magic != 0xEF53) {//супер блок неправильный
        return 0;
    }
    return 1;
}

int inode_allocated(uint32_t inode_num, char* inode_bitmap) {
    int inode_index = (inode_num - 1) % super.s_inodes_per_group;
    int inode_byte_index = inode_index / 8; // номер байта
    int inode_byte_offset = inode_index % 8; // номер бита в байте
    if (inode_bitmap[inode_byte_index] & (1 << (inode_byte_offset)))
        return 1;
    else
        return 0;
}
void get_inode(int i_num, struct ext2_inode *in, __u32 inode_table, __u32 inodes_per_group, __u32 inode_size){
    int index = (i_num - 1) % inodes_per_group;

    lseek(sd, (__u64)(inode_table) * block_size + index * inode_size, SEEK_SET);
    if(!read(sd, in, inode_size)){
        printf("Error read inode\n");
        exit(-1);
    }
}
void get_inode_bitmap(struct ext2_group_desc _desc, int group){
    lseek(sd,  block_size * _desc.bg_inode_bitmap, SEEK_SET);//сдвигаюсь на позицию блока с битмапой
    int	size = super.s_inodes_per_group / 8;
    ssize_t	actual;
    actual = read(sd, actual_inode_bitmap[group], size);

    if (actual == -1) {
        printf("Ошибка чтиния битмапы инодов (-1) \n");
    }
    if (actual != size) {
        printf("Ошибка чтиния битмапы инодов (size)\n");
    }
}
void checkPermissions(struct ext2_inode _inode, int i) {
    fprintf(stderr, "\nПоиск inode с отсутствием разрешений....\n");
    if(_inode.i_mode == 0) {
        printf("Inode %d не имеет разрешений. \n", i);
        push_error_inode(&h_perm, &t_perm, i);
        is_clean = 0;
    }
}
void checkLostInodes(struct ext2_inode _inode, int i) {
    if(_inode.i_links_count == 0) {
        printf("Ни одина из записей каталогов не ссылается на inode %d\n", i);
        push_error_inode(&h_lost, &t_lost, i);
        is_clean = 0;
    }

}
void checkMultiLink(__u32* i_block, int i){
    // проверка прямых ссылок
    for(int j = 0; j < 12; j++) {//адрес инф. блока (прямая ссылка) 0 - 11
        unsigned int block = *(i_block+j);
        if(block>super.s_blocks_count){
            printf("Недопустимый адресуемый блок %d\n", block);
            is_clean = 0;
        }
        if(occupied_blocks[block]) {
            printf("В таблице блоков inode-а несколько ссылок на одинаковый блок %d.\n", i);
            push_error_inode(&h_occup, &t_occup, i);
            is_clean = 0;
        }
        if(block != 0) {
            occupied_blocks[block] = 1;
        }
    }
    // проверка косвенной ссылки
    int indirect_position = i_block[12];//номер блока, в котором хранятся блоки с данными
    if(indirect_position == 0) return;//если = 0, то косвенной ссылки на блоки данных нет, продолжаем проверку
    int indirect_blocks[block_size >> 2];//так как имя (номер) блока занимает 4 байта, то соответственно в 1 блоке может храниться количество block_size/4 имен блоков с данными
    lseek(sd, BLOCK_OFFSET(indirect_position), SEEK_SET);//смещаюсь на начало блока, в котором записаны имена блоков, в которых хранятся данные
    read(sd, &indirect_blocks, sizeof(indirect_blocks));//считываю номера блоков с данными файла
    for(int j = 0; j < block_size >> 2; j++) {
        int block = indirect_blocks[j];
        if(occupied_blocks[block]) {
            printf("Ошибка косвенной ссылки на блок с данными файла в иноде %d.\n", i);
            push_error_inode(&h_occup, &t_occup, i);
            is_clean = 0;
        }
        if(block != 0) {
            occupied_blocks[block] = 1;
        }
    }
    // проверка двойной косвенной ссылки
    int doubly_position = i_block[13];
    if(doubly_position == 0) return;
    int doubly_blocks[block_size >> 2];
    lseek(sd, BLOCK_OFFSET(doubly_position), SEEK_SET);
    read(sd, &doubly_blocks, sizeof(doubly_blocks));
    for(int k = 0; k < block_size >> 2; k++) {
        lseek(sd, BLOCK_OFFSET(doubly_blocks[k]), SEEK_SET);
        read(sd, &indirect_blocks, sizeof(indirect_blocks));
        for(int j = 0; j < block_size >> 2; j++) {
            int block = indirect_blocks[j];
            if(occupied_blocks[block]) {
                printf("Ошибка двойной косвенной ссылки на блок с данными файла в иноде %d.\n", i);
                push_error_inode(&h_occup, &t_occup, i);
                is_clean = 0;
            }
            occupied_blocks[block] = 1;
        }
    }
    // проверка тройной косвенной ссылки
    int triply_position = i_block[14];
    if(triply_position == 0) return;
    int triply_blocks[block_size >> 2];
    lseek(sd, BLOCK_OFFSET(triply_position), SEEK_SET);
    read(sd, &triply_blocks, sizeof(triply_blocks));
    for(int x = 0; x < block_size >> 2; x++) {
        if(triply_blocks[x] == 0) {
            break;
        }
        lseek(sd, BLOCK_OFFSET(triply_blocks[x]), SEEK_SET);
        read(sd, &doubly_blocks, sizeof(doubly_blocks));
        for(int k = 0; k < block_size >> 2; k++) {
            lseek(sd, BLOCK_OFFSET(doubly_blocks[k]), SEEK_SET);
            read(sd, &indirect_blocks, sizeof(indirect_blocks));
            for(int j = 0; j < block_size >> 2; j++) {
                int block = indirect_blocks[j];
                if(occupied_blocks[block]) {
                    printf("Ошибка тройной косвенной ссылки на блок с данными файла в иноде %d.\n", i);
                    push_error_inode(&h_occup, &t_occup, i);
                    is_clean = 0;
                }
                occupied_blocks[block] = 1;
            }
        }
    }

}
void movingThroughInodes(int group) {
    struct ext2_inode inode;
    struct ext2_group_desc desc;
    int size = super.s_blocks_per_group * group_count;
    if(!(occupied_blocks = (int*)calloc(size, sizeof (int)))) {
        printf("Memory allocation error\n");
    }
    lseek(sd, block_size +  group * sizeof(desc), SEEK_SET);
    memset(&desc, 0, sizeof(struct ext2_group_desc));
    read(sd, &desc, sizeof(desc));
    fprintf(stderr, "\nПроверка ссылок на одинаковые блоки....\n");
    get_inode_bitmap(desc, group);
    int i = 0;
    if(group == 0) {
        i = super.s_first_ino;
    }
    else {
        i = group * super.s_inodes_per_group + 1; //первый инод группы
    }
    for( ; i <= super.s_inodes_per_group * (group + 1); i++) {
        if(inode_allocated(i, actual_inode_bitmap[group]) == 1){
            printf("Inode %d размещен на биткарте инодов\n", i);
            get_inode(i, &inode, desc.bg_inode_table, super.s_inodes_per_group, super.s_inode_size);
            if(inode.i_size == 0){
                printf("Inode %d помечен как занятый, но на самом деле свободен\n", i);
                push_error_inode(&h_occupButFree, &t_occupButFree, i);
            }
            else {
                checkMultiLink(inode.i_block, i);
                checkPermissions(inode, i);
                checkLostInodes(inode, i);
            }
        }
        else{
            get_inode(i, &inode, desc.bg_inode_table, super.s_inodes_per_group, super.s_inode_size);
            if(inode.i_size > 0){
                printf("Inode %d помечен как свободный, но на самом деле занят\n", i);
                push_error_inode(&h_freeButOccup, &t_freeButOccup, i);
            }

        }
    }

}

void blockMovement(){
    //struct ext2_super_block super;
    //struct ext2_group_desc desc;
    lseek(sd, BASE_OFFSET, SEEK_SET);
    read(sd, &super, sizeof(super));
    //lseek(sd, BASE_OFFSET + block_size, SEEK_SET);
    //read(sd, &desc, sizeof(desc));
    group_count = 1 + (super.s_blocks_count-1) / super.s_blocks_per_group;
    int gp = group_count;
    actual_inode_bitmap = (char**)malloc(group_count * sizeof(char*));
    int size = super.s_inodes_per_group / 8;
    int j;
    for (j = 0; j < group_count; j++) {
        actual_inode_bitmap[j] = malloc(size);//кол-во инодов в группе / кол-во битов в байте
    }

    for(int i = 0; i < group_count; i++) {
        //printf("\n=== Группа блоков %d ===\n", i);
        movingThroughInodes(i);
    }
}

int diskAccessCheck(char* path){
    sd = open(path, O_RDWR);
    if(sd == -1){
        return -1;
    }
    //fprintf(stdout, "\nopen return %d\n", sd);
    fprintf(stdout, "File system type check...\n");
    if(!checkSuperblock(sd)){
        //fprintf(stdout, "Causes:\n");
        //fprintf(stdout,"- - - Superblock is not corrected. Magic number error.\n");
        return 0;
    }

    return sd;
}

void printResult(){
    if(is_clean)
        printf("System is clean\n");
    else{
        if(h_occup!=NULL){
            printf("\nInodes that refer to the same data blocks: ");
            print_error_inode(h_occup);
        }
        if(h_lost!=NULL){
            printf("\nLost inodes (not referenced by any directory): ");
            print_error_inode(h_lost);
        }
        if(h_perm!=NULL){
            printf("\nInodes without permissions: ");
            print_error_inode(h_perm);
        }
        if(h_occupButFree!=NULL){
            printf("\nInodes marked as occupied, but actually free: ");
            print_error_inode(h_occupButFree);
        }
        if(h_freeButOccup!=NULL){
            printf("\nInodes marked as free, but actually occupied: ");
            print_error_inode(h_freeButOccup);
        }
    }
}

int main(int argc, char **argv) {
    if(argc<2){
        fprintf(stdout, "The path to the disk with the file system is not specified\n");
        //fprintf(stdout, "Путь к диску с файловой системой не указан.\n");
        return 0;
    }
    sd = diskAccessCheck(argv[1]);
    switch(sd){
        case -1:{
            fprintf(stdout, "Error opening the file.\n");
            //fprintf(stdout, "Ошибка открытия файла.\n");
            return -1;
        }break;
        case 0:{
            fprintf(stdout, "The drive %s is not an ext2 file system.\n", argv[1]);
            //fprintf(stdout, "Указанный диск %s не является файловой системой ext2.\n", argv[1]);
            return -2;
        }break;
        default:{
            fprintf(stdout, "The drive %s is an ext2 file system.\n\n", argv[1]);
            //fprintf(stdout, "Указанный диск %s является файловой системой ext2.\n\n", argv[1]);
        }
    }

    printInfo(sd);
    blockMovement();
    printResult();
    free_memory();

    if (close(sd) < 0){
        perror("close");
        return -1;
    }
    return 0;
}
