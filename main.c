#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define __USE_XOPEN
#include <time.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>

#define READ_BUFFER_MAX_SIZE 1000*1000

#define DEBUG_STDIN_TO_CONST_STRING 0

/*
 * full file name,size,timestamp,formatted time string
 */
typedef struct _logfile_item {
    char absFileName[128];
    ssize_t fileSize;
    time_t timeStamp;
    struct _logfile_item *next;
    struct _logfile_item *prev;
}Logfile_Item;
static Logfile_Item g_logfile_list_head = {0};
static Logfile_Item *g_logfile_list_tail = NULL;
static long long g_logfile_size_sum = 0;

int add_head_logfile(Logfile_Item *logfileItem) {
    printf("%s: filename:%s, timeStamp:%jd, filesize:%ld\n", __func__ , logfileItem->absFileName, logfileItem->timeStamp, logfileItem->fileSize);
    Logfile_Item * log_file_item = calloc(1, sizeof(Logfile_Item));
    if (log_file_item == NULL) {
        perror("memory alloc fail\n");
        return -1;
    }
    memcpy(log_file_item, logfileItem, sizeof(Logfile_Item));

    Logfile_Item *cur_logfile = &g_logfile_list_head;
    Logfile_Item *next_logfile = NULL;
    while (cur_logfile->next != NULL) {
        if (cur_logfile->next->timeStamp < log_file_item->timeStamp) {
            log_file_item->next = cur_logfile->next;

            cur_logfile->next = log_file_item;

            log_file_item->prev = cur_logfile;

            log_file_item->next->prev = log_file_item;
            break;
        }
        cur_logfile = cur_logfile->next;
    }

    if (cur_logfile->next == NULL) {
        cur_logfile->next = log_file_item;
        log_file_item->prev = cur_logfile;
        g_logfile_list_tail = log_file_item;
    }

    printf("%s:%lld,%lld\n", __func__ , (long long)g_logfile_size_sum , (long long)log_file_item->fileSize);
    g_logfile_size_sum += log_file_item->fileSize;

    return 0;
}

int retrieve_tail_logfile(Logfile_Item *logfileItem) {
    if (g_logfile_list_tail == NULL) {
        return -1;
    }

    memcpy(logfileItem, g_logfile_list_tail, sizeof(Logfile_Item));

    Logfile_Item *logfileItemTail = g_logfile_list_tail;
    if (logfileItemTail->prev == &g_logfile_list_head) {
        g_logfile_list_tail = NULL;
        logfileItemTail->prev->next = NULL;
    } else {
        g_logfile_list_tail = logfileItemTail->prev;
        g_logfile_list_tail->next = NULL;
    }

    g_logfile_size_sum -= logfileItemTail->fileSize;
    free(logfileItemTail);
    return 0;
}

/**
* @brief
* @param basePath
* @param filePrefix
* @return
*/

int list_log_files(const char *basePath,const char *filePrefix) {
    int ret = 0;
    char abs_filename[128] = {0};
    char abs_filename_tail[128] = {0};
    Logfile_Item logfileItem = {0};
    DIR *dp;
    struct dirent *ep;
    struct tm tm;

    dp = opendir (basePath);
    if (dp != NULL)
    {
        while ((ep = readdir (dp))) {
            memset(abs_filename, 0, 128);
            if (0 == (strncmp(ep->d_name, filePrefix, strlen(filePrefix)))) {
                snprintf(abs_filename, 128, "%s/%s", basePath, ep->d_name);

                struct stat sb;
                if (stat(abs_filename, &sb) == -1) {
                    perror("stat");
                }

                memset(abs_filename_tail, 0, 128);
                strncpy(abs_filename_tail, ep->d_name + strlen(filePrefix) + 1, 19);
//                printf("abs_filename_tail:%s\n", abs_filename_tail);
                strptime(abs_filename_tail, "%Y-%m-%d_%H-%M-%S", &tm);
                time_t timestamp = mktime(&tm);

//                printf("%s,size:%d,timestamp:%jd\n", abs_filename, sb.st_size, timestamp);

                memset(&logfileItem, 0, sizeof(Logfile_Item));
                strcpy(logfileItem.absFileName,abs_filename);
                logfileItem.fileSize = sb.st_size;
                logfileItem.timeStamp = timestamp;

                ret = add_head_logfile(&logfileItem);
                if (ret != 0) {
                    perror("add file to file list fail\n");
                    return -1;
                }
            }
        }
        (void) closedir (dp);
    } else {
        perror ("Couldn't open the directory");
        return -2;
    }

    return 0;
}

int gen_log_file_name(const char *basePath, const char *filePrefix, Logfile_Item *logfileItem) {
    char file_name[128] = {0};
    time_t t;
    struct tm *tmp;

    t = time(NULL);
    tmp = localtime(&t);
    if (tmp == NULL) {
        perror("localtime");
        exit(EXIT_FAILURE);
    }

    if (strftime(file_name, 128, "%Y-%m-%d_%H-%M-%S", tmp) == 0) {
        fprintf(stderr, "strftime returned 0");
        exit(EXIT_FAILURE);
    }
//    printf("%s\n", file_name);

    snprintf(logfileItem->absFileName, sizeof(logfileItem->absFileName),
            "%s/%s_%s.log", basePath, filePrefix, file_name);
    logfileItem->timeStamp = t;

//    printf("%s\n", logfileItem->absFileName);

    return 0;
}

FILE *fp = NULL;

void sigpipe_handler(int unused)
{
    if (fp != NULL) {
        fflush(fp);
        fclose(fp);
        fp = NULL;
    }
    printf("pipe signal\n");
    exit(0);
}
void sigint_handler(int unused)
{
    printf("int signal\n");
}

long long convert2byte(const char *sizeStr) {
    long long ret_size = 1;
    int last_index = 0;
    char max_size_str[128] = {0};
    strcpy(max_size_str, sizeStr);
    last_index = strlen(max_size_str) - 1;
    switch (max_size_str[last_index]) {
        case 'k':
        case 'K': {
            ret_size *=1024;
            max_size_str[last_index] = 0;
        }
            break;

        case 'M':
        case 'm':{
            ret_size *=1024LL*1024LL;
            max_size_str[last_index] = 0;
        }
            break;
        case 'g':
        case 'G': {
            ret_size *=1024LL*1024LL*1024LL;
            max_size_str[last_index] = 0;
        }
            break;
    }
    ret_size *= atoll(max_size_str);
    return ret_size;
}

int main(int argc, char *argv[]) {
    long long single_file_max_size = 10;
    long long all_file_max_size = 3*10;
    char *log_file_prefix = "stdlog";
    char *log_base_path = "/tmp/log";

    char opt;
    while ((opt = getopt(argc, argv, "b:p:a:s:")) != -1) {
        switch (opt) {
            case 'b':
                log_base_path = strdup(optarg);
                break;
            case 'p':
                log_file_prefix = strdup(optarg);
                break;
            case 'a':
                all_file_max_size = convert2byte(optarg);
                break;
            case 's':
                single_file_max_size = convert2byte(optarg);
                break;
            default: /* '?' */
                fprintf(stderr, "Usage: %s [-b base dir path]"
                                " [-p] prefix of log file name"
                                " [-a] all files max size"
                                " [-s] single file max size\n",
                        argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    printf("base dir:%s, log file prefix:%s, single file max size:%lld, all file sum max size:%lld\n",
            log_base_path, log_file_prefix, (long long)single_file_max_size, (long long)all_file_max_size);

    int current_file_size = 0;
    Logfile_Item logfileItem = {0};
    char *read_buffer = calloc(1, READ_BUFFER_MAX_SIZE);

    printf("star logging\n");

    sigaction(SIGPIPE, &(struct sigaction){sigpipe_handler}, NULL);
    sigaction(SIGINT, &(struct sigaction){sigint_handler}, NULL);

    list_log_files(log_base_path, log_file_prefix);

    if (NULL == read_buffer) {
        perror("read buffer alloc memory fail\n");
        return 1;
    }
    while (1) {
        printf("");
        if (current_file_size >= single_file_max_size) {
            fclose(fp);
            fp = NULL;
            logfileItem.fileSize = current_file_size;
//            printf("logfileItem.fileSize:%d\n", logfileItem.fileSize);
            add_head_logfile(&logfileItem);
            current_file_size = 0;
        }
        if (fp == NULL) {
            current_file_size = 0;
            printf("check g_logfile_size_sum:%lld\n", (long long)g_logfile_size_sum);
            if ((g_logfile_size_sum + single_file_max_size) > all_file_max_size) {
                Logfile_Item logfileItem;
                int ret = retrieve_tail_logfile(&logfileItem);
                if (ret != 0) {
                    perror("retrive file fail\n");
                } else {
                    ret = remove(logfileItem.absFileName);
                    printf("remove file %s\n", logfileItem.absFileName);
                    if (ret != 0) {
                        printf("error number string:%s\n", strerror(errno));
                        perror("remove file fail\n");
                    }
                }
            }

            memset(&logfileItem, 0, sizeof(Logfile_Item));
            int ret = gen_log_file_name(log_base_path, log_file_prefix, &logfileItem);
            if (ret !=0) {
                perror("gen file fail\n");
                return 3;
            } else {
                printf("gen file:%s\n", logfileItem.absFileName);
            }

            fp = fopen(logfileItem.absFileName, "w");
            if (fp == NULL) {
                perror(logfileItem.absFileName);
                perror("file open fail\n");
                return 2;
            }
        }

        memset(read_buffer, 0, READ_BUFFER_MAX_SIZE);
        char *read_line = NULL;
#if DEBUG_STDIN_TO_CONST_STRING
        sleep(1);
        readline = "1234567890a";
        strncpy(read_buffer, READ_BUFFER_MAX_SIZE, read_line);
#else
        read_line = fgets(read_buffer, READ_BUFFER_MAX_SIZE, stdin);
#endif
        if (read_line == NULL) {
            fflush(fp);
            fclose(fp);
            printf("fgets NULL\n");
            return 0;
        }
        fputs(read_buffer, fp);
        fflush(fp);
        current_file_size += strlen(read_buffer);
    }
    return 0;
}
