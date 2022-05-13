#include<stdio.h>
#include<fcntl.h>
#include<unistd.h>
#include<stdlib.h>
#include<linux/fs.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<string.h>
#include"ext2_reserv.h"

#define block_size (1024 << super.s_log_block_size)
#define BASE_OFFSET 1024
#define BLOCK_OFFSET(block) (BASE_OFFSET + (block-1)*block_size)
#define EXT2_NDIR_BLOCKS    12
#define EXT2_IND_BLOCK      EXT2_NDIR_BLOCKS
#define EXT2_DIND_BLOCK     (EXT2_IND_BLOCK + 1)
#define EXT2_TIND_BLOCK     (EXT2_DIND_BLOCK + 1)
#define EXT2_N_BLOCKS     (EXT2_TIND_BLOCK + 1)

struct ext2_super_block super;
struct ext2_group_desc desc;
struct ext2_inode inode;

void printInfo (){
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

int alpha(char c) {
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
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

int get_answer() {
    char answer = ' ';
    while(!alpha(answer)) answer = getchar();
    if(answer == 'Y' || answer == 'y') {
        printf("\n");
        return 1;
    }
    printf("\n");
    return 0;
}

void checkMultiLink(int group, int sd, int *taken) {
    int errors = 0;
    fprintf(stderr, "\nПроверка на одинаковые ссылки....\n");
    lseek(sd, 1024, SEEK_SET);
    read(sd, &super, sizeof(super));

    lseek(sd, BASE_OFFSET + block_size + group * sizeof(desc), SEEK_SET);
    read(sd, &desc, sizeof(desc));
    for(int i = 0; i < super.s_inodes_per_group; i++) {
        int inode_position = BLOCK_OFFSET(desc.bg_inode_table) + i * sizeof(inode);
        lseek(sd, inode_position, SEEK_SET);
        read(sd, &inode, sizeof(inode));
        if(inode.i_size == 0) continue;

        int finished = 0;
        for(int j = 0; j < 1; j++) {//адрес инф. блока (прямая ссылка) 0 - 11
            int block = inode.i_block[j];
            if(block>super.s_blocks_count){
                printf("Недопустимый адресуемый блок %d\n", block);
                errors++;
            }
            if(block == 0) {
                finished = 1;
                break;
            }
            if(taken[block]) {
                printf("Одинаковые прямые ссылки %d на таблицу индексов файла.\n", block);
                errors++;
            }
            taken[block] = 1;
        }
        if(finished) continue;

        int indirect_position = inode.i_block[12];
        if(indirect_position == 0) continue;
        int indirect_blocks[block_size >> 2];
        lseek(sd, BLOCK_OFFSET(indirect_position), SEEK_SET);
        read(sd, &indirect_blocks, sizeof(indirect_blocks));
        for(int j = 0; j < block_size >> 2; j++) {
            int block = indirect_blocks[j];
            if(block == 0) {
                finished = 1;
                break;
            }
            if(taken[block]) {
                printf("Ошибка косвенной ссылки %d на таблицу индексов файла.\n", block);
                errors++;
            }
            taken[block] = 1;
        }
        if(finished) continue;

        int doubly_position = inode.i_block[13];
        if(doubly_position == 0) continue;
        int doubly_blocks[block_size >> 2];
        lseek(sd, BLOCK_OFFSET(doubly_position), SEEK_SET);
        read(sd, &doubly_blocks, sizeof(doubly_blocks));
        for(int k = 0; k < block_size >> 2; k++) {
            if(finished) break;
            if(doubly_blocks[k] == 0) {
                finished = 1;
                break;
            }
            lseek(sd, BLOCK_OFFSET(doubly_blocks[k]), SEEK_SET);
            read(sd, &indirect_blocks, sizeof(indirect_blocks));
            for(int j = 0; j < block_size >> 2; j++) {
                int block = indirect_blocks[j];
                if(block == 0) {
                    finished = 1;
                    break;
                }
                if(taken[block]) {
                    printf("Ошибка двойной косвенной ссылки %d на таблицу индексов файла.\n", block);
                    errors++;
                }
                taken[block] = 1;
            }
        }
        if(finished) continue;

        int triply_position = inode.i_block[14];
        if(triply_position == 0) continue;
        int triply_blocks[block_size >> 2];
        lseek(sd, BLOCK_OFFSET(triply_position), SEEK_SET);
        read(sd, &triply_blocks, sizeof(triply_blocks));
        for(int x = 0; x < block_size >> 2; x++) {
            if(finished) break;
            if(triply_blocks[x] == 0) {
                break;
            }
            lseek(sd, BLOCK_OFFSET(triply_blocks[x]), SEEK_SET);
            read(sd, &doubly_blocks, sizeof(doubly_blocks));
            for(int k = 0; k < block_size >> 2; k++) {
                if(finished) break;
                if(doubly_blocks[k] == 0) {
                    finished = 1;
                    break;
                }
                lseek(sd, BLOCK_OFFSET(doubly_blocks[k]), SEEK_SET);
                read(sd, &indirect_blocks, sizeof(indirect_blocks));
                for(int j = 0; j < block_size >> 2; j++) {
                    int block = indirect_blocks[j];
                    if(block == 0) {
                        finished = 1;
                        break;
                    }
                    if(taken[block]) {
                        printf("Ошибка тройной косвенной ссылки %d на таблицу индексов файла.\n", block);
                        errors++;
                    }
                    taken[block] = 1;
                }
            }
        }
    }
    if(errors == 0)
        printf("Ошибок не обнаружено\n");
}

void checkPermissions(int group, int sd) {
    int errors = 0;
    fprintf(stderr, "\nПоиск inode с отсутствием разрешений....\n");
    lseek(sd, 1024, SEEK_SET);
    read(sd, &super, sizeof(super));

    lseek(sd, BASE_OFFSET + group * sizeof(desc), SEEK_SET);
    read(sd, &desc, sizeof(desc));
    struct ext2_group_desc desc2;
    lseek(sd, BASE_OFFSET + block_size, SEEK_SET);
    read(sd, &desc2, sizeof(desc2));

    for(int i = 0; i < super.s_inodes_per_group; i++) {
        int inode_position = BLOCK_OFFSET(desc.bg_inode_table) + i * sizeof(inode);
        lseek(sd, inode_position, SEEK_SET);
        read(sd, &inode, sizeof(inode));
        if(inode.i_size == 0) continue;
        if(inode.i_mode == 0) {
            printf("Inode %d не имеет разрешений. \n", i + super.s_inodes_per_group * group);
            errors++;
            /*if(get_answer()) {
                printf("Введите целое число, соответствующее желаемому разрешению.\n");
                int perm;
                scanf("%d", &perm);
                inode.i_mode = perm;
                lseek(sd, inode_position, SEEK_SET);
                write(sd, &inode, sizeof(inode));
            }*/
        }
    }
    if(errors == 0)
        printf("Ошибок не обнаружено\n");
}

char *int_to_string(int number) {
    char *ret = malloc(sizeof(char) * 20);
    if(number == 0) {
        ret[0] = '0';
        ret[1] = '\0';
        return ret;
    }
    int len = 0;
    int n = number;
    while(n > 0) {
        len++, n /= 10;
    }
    n = number;
    ret[len--] = '\0';
    while(n > 0) {
        ret[len--] = '0' + n % 10;
        n /= 10;
    }
    return ret;
}

void checkLostInodes(int group, int sd) {
    int errors = 0;
    fprintf(stderr, "\nПоиск 'потерянных' инодов....\n");
    lseek(sd, 1024, SEEK_SET);
    read(sd, &super, sizeof(super));

    lseek(sd, BASE_OFFSET + block_size + group * sizeof(desc), SEEK_SET);
    read(sd, &desc, sizeof(desc));

    for(int i = 0; i < super.s_inodes_per_group; i++) {
        int inode_position = BLOCK_OFFSET(desc.bg_inode_table) + i * sizeof(inode);
        lseek(sd, inode_position, SEEK_SET);
        read(sd, &inode, sizeof(inode));
        if(inode.i_size == 0) continue;
        if(inode.i_links_count == 0) {
            printf("Ни одина из записей каталогов не ссылается на inode %d\n", i + super.s_inodes_per_group * group);
            errors++;
        }
    }
    if(errors == 0)
        printf("Ошибок не обнаружено\n");
}

void blockMovement(int sd){
    lseek(sd, BASE_OFFSET, SEEK_SET);
    read(sd, &super, sizeof(super));
    lseek(sd, BASE_OFFSET + block_size, SEEK_SET);
    read(sd, &desc, sizeof(desc));
    unsigned int group_count = 1 + (super.s_blocks_count-1) / super.s_blocks_per_group;
    int *taken;//1, если блок уже имеет такую ссылку. 0 в противном случае
    taken = malloc(((super.s_blocks_per_group * group_count) * 4 + 5) * sizeof(int));
    memset(taken, 0, sizeof(taken));
    for(int i = 0; i < group_count; i++) {
        printf("\n=== Группа блоков %d ===\n", i);
        checkMultiLink(i, sd, taken);
        checkPermissions(i, sd);
        checkLostInodes(i, sd);
    }
}

int diskAccessCheck(char* path){
    int sd = open(path, O_RDWR);
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

int main(int argc, char **argv) {
    int sd;
    if(argc<2){
        //fprintf(stdout, "The path to the disk with the file system is not specified\n");
        fprintf(stdout, "Путь к диску с файловой системой не указан.\n");
        return 0;
    }
    sd = diskAccessCheck(argv[1]);
    switch(sd){
        case -1:{
            //fprintf(stdout, "Error opening the file.\n");
            fprintf(stdout, "Ошибка открытия файла.\n");
            return -1;
        }break;
        case 0:{
            //fprintf(stdout, "The drive %s is not an ext2 file system.\n", argv[1]);
            fprintf(stdout, "Указанный диск %s не является файловой системой ext2.\n", argv[1]);
            return -2;
        }break;
        default:{
            //fprintf(stdout, "The drive %s is an ext2 file system.\n\n", argv[1]);
            fprintf(stdout, "Указанный диск %s является файловой системой ext2.\n\n", argv[1]);
        }
    }

    printInfo();

    blockMovement(sd);

    return 0;
}
