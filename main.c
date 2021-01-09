#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define __USE_XOPEN
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>

#define DEFAULT_READ_BUFFER_MAX_SIZE 10*1000
#define DEFAULT_SINGLE_FILE_MAX_SIZE 1*1024*1024
#define DEFAULT_ALL_FILES_MAX_SIZE 10*1024*1024

#define DEBUG_STD_INPUT_TO_CONST_STRING 0

void slog_debug(const char*fmt, ...);

/*
 * full file name,size,timestamp,formatted time string
 */
typedef struct _logfile_item {
    char absFileName[128 + 1];
    ssize_t fileSize;
    time_t timeStamp;
    struct _logfile_item *next;
    struct _logfile_item *prev;
}Logfile_Item;


static Logfile_Item g_logfile_list_head = {0};
static Logfile_Item *g_logfile_list_tail = NULL;
static long long g_logfile_size_sum = 0;


static int g_debug_info_dump = 0;
static long long g_single_file_max_size = DEFAULT_SINGLE_FILE_MAX_SIZE;
static long long g_all_file_max_size = DEFAULT_ALL_FILES_MAX_SIZE;
static char *g_log_file_prefix = "slog";
static char *g_log_file_symbol = "slog.log";
static char *g_log_base_path = "./";
static FILE *g_log_file_handler = NULL;


int add_head_logfile(Logfile_Item *logfileItem) {
    slog_debug("%s: filename:%s, timeStamp:%jd, filesize:%ld\n", __func__ , logfileItem->absFileName, logfileItem->timeStamp, logfileItem->fileSize);
    Logfile_Item * log_file_item = calloc(1, sizeof(Logfile_Item));
    if (log_file_item == NULL) {
        perror("memory alloc fail\n");
        return -1;
    }
    memcpy(log_file_item, logfileItem, sizeof(Logfile_Item));

    Logfile_Item *cur_logfile = &g_logfile_list_head;
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

    slog_debug("%s:%lld,%lld\n", __func__ , (long long)g_logfile_size_sum , (long long)log_file_item->fileSize);
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
                slog_debug("abs_filename_tail:%s\n", abs_filename_tail);
                strptime(abs_filename_tail, "%Y-%m-%d_%H-%M-%S", &tm);
                time_t timestamp = mktime(&tm);

                slog_debug("%s,size:%d,timestamp:%jd\n", abs_filename, sb.st_size, timestamp);

                memset(&logfileItem, 0, sizeof(Logfile_Item));
                strncpy(logfileItem.absFileName,abs_filename, 124);
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
        fprintf(stderr, "time format to string return 0");
        exit(EXIT_FAILURE);
    }
    slog_debug("gen filename:%s\n", file_name);

    snprintf(logfileItem->absFileName, sizeof(logfileItem->absFileName),
            "%s/%s_%s.log", basePath, filePrefix, file_name);
    logfileItem->timeStamp = t;

    slog_debug("gen full file path %s\n", logfileItem->absFileName);

    return 0;
}

int relink_to_file(const char *file_path, const char *sym_name) {
    int ret;
    struct stat buf;
    int result;

    result = lstat(sym_name, &buf);
    if (result == 0) {
        ret = remove(sym_name);
        slog_debug("remove symbol file %s\n", sym_name);
        if (ret != 0) {
            slog_debug("error number string:%s\n", strerror(errno));
            perror("remove file fail\n");
            return -1;
        }
    }
    ret = symlink(file_path, sym_name);
    if (ret != 0) {
        slog_debug("error number string:%s\n", strerror(errno));
        perror("remove file fail\n");
        return -2;
    }
    return 0;
}

int truncate_log_files() {
    slog_debug("check g_logfile_size_sum:%lld\n", (long long)g_logfile_size_sum);
    while ((g_logfile_size_sum + g_single_file_max_size) > g_all_file_max_size) {
        Logfile_Item logfileItem;
        int ret = retrieve_tail_logfile(&logfileItem);
        if (ret != 0) {
            perror("retrieve file fail\n");
        } else {
            ret = remove(logfileItem.absFileName);
            printf("remove file %s\n", logfileItem.absFileName);
            if (ret != 0) {
                printf("error number string:%s\n", strerror(errno));
                perror("remove file fail\n");
            }
            slog_debug("check g_logfile_size_sum:%lld\n", (long long)g_logfile_size_sum);
        }
    }
    return 0;
}

void sigterm_handler(int sig) {
    if (g_log_file_handler != NULL) {
        fflush(g_log_file_handler);
        fclose(g_log_file_handler);
        g_log_file_handler = NULL;
    }
    printf("pipe signal:%d\n", sig);
    exit(0);
}

void sigpipe_handler(int unused)
{
    if (g_log_file_handler != NULL) {
        fflush(g_log_file_handler);
        fclose(g_log_file_handler);
        g_log_file_handler = NULL;
    }
    printf("pipe signal:%d\n", unused);
    exit(0);
}
void sigint_handler(int unused)
{
    printf("int signal:%d\n", unused);
}

long long convert2byte(const char *sizeStr) {
    long long ret_size = 1;
    int last_index = 0;
    char max_size_str[128 + 1] = {0};
    strncpy(max_size_str, sizeStr, 128);
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

void usage() {
    fprintf(stderr,
            "Usage: std2file [-b base dir path]"
            " [-p] prefix of log file name"
            " [-a] all files max size"
            " [-s] single file max size"
            " [-d] dump process step debug info\n");
}

int main(int argc, char *argv[]) {
    char opt;
    while ((opt = (char)getopt(argc, argv, "b:p:a:s:d")) != -1) {
        switch (opt) {
            case 'b':
                g_log_base_path = strdup(optarg);
                break;
            case 'p':
                g_log_file_prefix = strdup(optarg);
                break;
            case 'a':
                g_all_file_max_size = convert2byte(optarg);
                break;
            case 's':
                g_single_file_max_size = convert2byte(optarg);
                break;
            case 'd':
                g_debug_info_dump = 1;
                break;
            case '?':
            default: /* '?' */
                usage();
                exit(EXIT_FAILURE);
        }
    }
    if (g_single_file_max_size > g_all_file_max_size) {
        printf("g_single_file_max_size:%lld greater than g_all_file_max_size:%lld\n",
               (long long)g_single_file_max_size, (long long)g_all_file_max_size);
        return -1;
    }
    printf("base dir:%s, log file prefix:%s, single file max size:%lld, all file sum max size:%lld\n",
           g_log_base_path, g_log_file_prefix, (long long)g_single_file_max_size, (long long)g_all_file_max_size);

    int current_file_size = 0;
    Logfile_Item logfileItem = {0};
    char *read_buffer = calloc(1, DEFAULT_READ_BUFFER_MAX_SIZE);

    printf("star logging\n");

    sigaction(SIGPIPE, &(struct sigaction){sigpipe_handler}, NULL);
    sigaction(SIGINT, &(struct sigaction){sigint_handler}, NULL);
    sigaction(SIGTERM, &(struct sigaction){sigterm_handler}, NULL);

    g_log_file_symbol = calloc(1, FILENAME_MAX + 1);
    snprintf(g_log_file_symbol, FILENAME_MAX, "%s/%s.log", g_log_base_path, g_log_file_prefix);

    list_log_files(g_log_base_path, g_log_file_prefix);

    truncate_log_files();

    if (NULL == read_buffer) {
        perror("read buffer alloc memory fail\n");
        return 1;
    }
    while (1) {
        if (current_file_size >= g_single_file_max_size) {
            fclose(g_log_file_handler);
            g_log_file_handler = NULL;
            logfileItem.fileSize = current_file_size;
            slog_debug("close file:%s,file size:%lld\n", logfileItem.absFileName, (long long)logfileItem.fileSize);
            add_head_logfile(&logfileItem);
            current_file_size = 0;
        }
        if (g_log_file_handler == NULL) {
            current_file_size = 0;
            truncate_log_files();

            memset(&logfileItem, 0, sizeof(Logfile_Item));
            int ret = gen_log_file_name(g_log_base_path, g_log_file_prefix, &logfileItem);
            if (ret !=0) {
                perror("gen file fail\n");
                return 3;
            } else {
                printf("gen file:%s\n", logfileItem.absFileName);
            }

            g_log_file_handler = fopen(logfileItem.absFileName, "w");
            if (g_log_file_handler == NULL) {
                perror(logfileItem.absFileName);
                perror("file open fail\n");
                return 2;
            }
            ret = relink_to_file(logfileItem.absFileName, g_log_file_symbol);
            if (ret !=0 ) {
                perror("link to new file fail\n");
            }
        }

        memset(read_buffer, 0, DEFAULT_READ_BUFFER_MAX_SIZE);
        char *read_line = NULL;
#if DEBUG_STD_INPUT_TO_CONST_STRING
        sleep(1);
        readline = "1234567890a";
        strncpy(read_buffer, DEFAULT_READ_BUFFER_MAX_SIZE, read_line);
#else
        read_line = fgets(read_buffer, DEFAULT_READ_BUFFER_MAX_SIZE, stdin);
#endif
        if (read_line == NULL) {
            fflush(g_log_file_handler);
            fclose(g_log_file_handler);
            printf("get from std input NULL\n");
            return 0;
        }
        fputs(read_buffer, g_log_file_handler);
        fflush(g_log_file_handler);
        current_file_size += strlen(read_buffer);
    }
    return 0;
}

void slog_debug(const char*fmt, ...) {
    if (g_debug_info_dump==1) {
        va_list vaList;
        va_start(vaList, fmt);
        vprintf(fmt, vaList);
        va_end(vaList);
    }
}