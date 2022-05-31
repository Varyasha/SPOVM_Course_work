#include<linux/fs.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<string.h>
#include<ext2fs/ext2_fs.h>
#include<errno.h>

#include"queue.h"

#define block_size (1024 << super.s_log_block_size)
#define BASE_OFFSET 1024
#define BLOCK_OFFSET(block) (BASE_OFFSET + (block-1)*block_size)
#define EXT2_NDIR_BLOCKS    12
#define EXT2_IND_BLOCK      EXT2_NDIR_BLOCKS
#define EXT2_DIND_BLOCK     (EXT2_IND_BLOCK + 1)
#define EXT2_TIND_BLOCK     (EXT2_DIND_BLOCK + 1)
#define EXT2_N_BLOCKS     (EXT2_TIND_BLOCK + 1)


int sd;
struct ext2_super_block super;
typedef struct ext2_inode INODE;

char* actual_inode_bitmap;
int* occupied_blocks;//1, если блок уже имеет ссылку на такой блок. 0 в противном случае
int group_count;
int is_clean = 1;

void free_memory() {
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

void get_inode_bitmap(struct ext2_group_desc _desc, int group){
    lseek(sd,  block_size * _desc.bg_inode_bitmap, SEEK_SET);//сдвигаюсь на позицию блока с битмапой
    int	size = super.s_inodes_per_group / 8;
    ssize_t	actual;
    actual = read(sd, actual_inode_bitmap, size);
    if (actual == -1) {
        printf("Error reading the inode bitmap (-1) \n");
        exit(-5);
    }
    if (actual != size) {
        printf("Error reading the inode bitmap (size)\n");
        exit(-5);
    }
}
void checkInodeMode(__u16 i_mode, int i) {
    //fprintf(stderr, "\nПоиск inode с отсутствием разрешений....\n");
    if(i_mode == 0) {
        printf("The inode %d has the wrong format of the described file and the access rights. \n", i);
        push_error_inode(&h_perm, &t_perm, i);
        is_clean = 0;
    }
}
void checkLostInodes(__u16 i_links_count, int i) {
    if(i_links_count == 0) {
        printf("None of the directory entries refer to inode %d\n", i);
        push_error_inode(&h_lost, &t_lost, i);
        is_clean = 0;
    }
}
void checkMultiLink(__u32* i_block, int i){
    // проверка прямых ссылок
    for(int j = 0; j < 12; j++) {//адрес инф. блока (прямая ссылка) 0 - 11
        unsigned int block = *(i_block+j);
        if(block>super.s_blocks_count){
            printf("Invalid address block %d\n", block);
            is_clean = 0;
        }
        if(occupied_blocks[block]) {
            printf("Several references to the same block were found in the array of addresses of blocks with data in the inode %d.\n", i);
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
            printf("Error indirectly linking to a block with file data in the inode %d.\n", i);
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
                printf("Error of double indirect reference to the block with file data in the inode %d.\n", i);
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
                    printf("Error of triple indirect reference to the block with file data in the inode %d.\n", i);
                    push_error_inode(&h_occup, &t_occup, i);
                    is_clean = 0;
                }
                occupied_blocks[block] = 1;
            }
        }
    }

}//valgrind запустить программу

void get_inode(int i_num, char* buf, __u32 inode_table){
    int index = (i_num - 1) % super.s_inodes_per_group;
    int offset = (inode_table) * block_size + index * super.s_inode_size;
    lseek(sd, offset, SEEK_SET);
    if(!read(sd, buf, super.s_inode_size)){
        printf("Error read inode\n");
        exit(-1);
    }
}

void movingThroughInodes(int group) {
    INODE *ip;
    char buf[256];
    struct ext2_group_desc desc;
    int size = super.s_blocks_per_group * group_count;
    //group = 9;
    lseek(sd, block_size +  group * sizeof(desc), SEEK_SET);
    memset(&desc, 0, sizeof(struct ext2_group_desc));
    read(sd, &desc, sizeof(desc));
    //fprintf(stderr, "\nПроверка ссылок на одинаковые блоки.... группа %d\n", group);
    get_inode_bitmap(desc, group);
    int i;
    if(group == 0) {
        i = super.s_first_ino;
    }
    else {
        i = group * super.s_inodes_per_group + 1; //первый инод группы
    }
    int limit = super.s_inodes_per_group * (group+1);
    for( ; i <= limit; i++) {
        if(inode_allocated(i, actual_inode_bitmap) == 1){
            //printf("Inode %d размещен на биткарте инодов\n", i);
            get_inode(i, buf, desc.bg_inode_table);
            ip = (INODE*)buf;
            if(ip->i_size == 0){
                //printf("Inode %d помечен как занятый, но на самом деле свободен\n", i);
                push_error_inode(&h_occupButFree, &t_occupButFree, i);
                is_clean = 0;
            }
            else {
                checkMultiLink(ip->i_block, i);
                checkInodeMode(ip->i_mode, i);
                checkLostInodes(ip->i_links_count, i);
            }
        }
        else {
            get_inode(i, buf, desc.bg_inode_table);
            ip = (INODE*)buf;

            if (ip->i_size > 0) {
                //printf("Inode %d помечен как свободный, но на самом деле занят\n", i);
                push_error_inode(&h_freeButOccup, &t_freeButOccup, i);
                is_clean = 0;
            }

        }
    }
}

void blockMovement(){
    lseek(sd, BASE_OFFSET, SEEK_SET);
    read(sd, &super, sizeof(super));
    group_count = 1 + (super.s_blocks_count-1) / super.s_blocks_per_group;
    int size = super.s_inodes_per_group / 8;

    actual_inode_bitmap = calloc(size, sizeof (char));

    if(!(occupied_blocks = (int*)calloc(super.s_blocks_count, sizeof(int)))) {
        printf("Memory allocation error\n");
    }
    printf("\nChecking for references to the same block in the array of addresses of blocks with data in the inode.... \n");
    printf("Checking inodes for the wrong format of the described file and the access rights.... \n");
    printf("Checking inodes for directory entries links.... \n");
    for(int i = 0; i < group_count; i++) {
        //printf("\n=== Группа блоков %d ===\n", i);
        movingThroughInodes(i);
    }
}

int openDisk(char* path){
    sd = open(path, O_RDONLY);
    if(sd == -1){
        return 0;
    }
    return 1;
}

int closeDisk(){
    if (close(sd) < 0){
        perror("close");
        return 0;
    }
    return 1;
}

int diskAccessCheck(char* path){
    if(!openDisk(path)){
        return -1;
    }
    //fprintf(stdout, "\nopen return %d\n", sd);
    fprintf(stdout, "File system type check...\n");
    if(!checkSuperblock(sd)){
        return 0;
    }
    return sd;
}

void printResult(){
    if(is_clean)
        printf("\n- - - System is clean\n");
    else{
        printf("\n- - - System is not clean\n");
        if(h_occup!=NULL){
            printf("\nInodes that refer to the same data blocks: ");
            print_error_inode(h_occup);
            printf("\n");
        }
        if(h_lost!=NULL){
            printf("Lost inodes (not referenced by any directory): ");
            print_error_inode(h_lost);
            printf("\n");
        }
        if(h_perm!=NULL){
            printf("Inodes without permissions: ");
            print_error_inode(h_perm);
            printf("\n");
        }
        if(h_occupButFree!=NULL){
            printf("Inodes marked as occupied, but actually free: ");
            print_error_inode(h_occupButFree);
            printf("\n");
        }
        if(h_freeButOccup!=NULL){
            printf("Inodes marked as free, but actually occupied: ");
            print_error_inode(h_freeButOccup);
            printf("\n");
        }
    }
}

int main(int argc, char **argv) {
    if(argc<2){
        fprintf(stdout, "The path to the disk with the file system is not specified\n");
        fprintf(stdout, "\nCorrect format of the utility call:\n");
        fprintf(stdout, "sudo ./ext2check <path to the disk with the file system>\n");
        fprintf(stdout, "\nFor example: sudo ./ext2check /dev/sdb1\n");
        //fprintf(stdout, "Путь к диску с файловой системой не указан.\n");
        return 0;
    }
    sd = diskAccessCheck(argv[1]);
    switch(sd){
        case -1:{
            fprintf(stdout, "Error opening the disk. Please run with superuser rights (sudo)\n");
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

    if(!closeDisk()){
        printf("Error closing the disk.\n");
        return -3;
    }
    return 0;
}
