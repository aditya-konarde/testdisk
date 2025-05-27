/*

    File: phcli_auto.c

    Copyright (C) 2025 Modified for non-interactive operation

    This software is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write the Free Software Foundation, Inc., 51
    Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#include <ctype.h>
#include "types.h"
#include "common.h"
#include "filegen.h"
#include "log.h"
#include "photorec.h"
#include "ext2grp.h"
#include "geometry.h"
#include "poptions.h"
#include "phcli.h"
#include "phcli_auto.h"
#include "list_add_sorted_uniq.h"
#include "partauto.h"
#include "sessionp.h"
#include "phrecn.h"
#include "psearchn.h"
#include "pblocksize.h"
#include "pfree_whole.h"
#include "ppartseln.h"
#include "pdiskseln.h"
#include "fnctdsk.h"
#include "list.h"
#include "phcfg.h"
#include "phbs.h"
#include "hdaccess.h"
#include "autoset.h"

extern const arch_fnct_t arch_none;

/* Structure for non-interactive options */
struct ph_auto_options {
    char *disk_path;           /* Disk/image path */
    char *dest_dir;            /* Destination directory */
    int partition_num;         /* Partition number (-1 for whole disk) */
    int file_formats;          /* 0=all, 1=specified list */
    char **format_list;        /* List of file formats to recover */
    int format_count;          /* Number of formats in list */
    int free_space_only;       /* 0=all space, 1=free space only */
    int keep_corrupted;        /* 0=skip corrupted, 1=keep corrupted */
    int paranoid;              /* 0=normal, 1=paranoid mode */
    int expert;                /* Expert mode */
    int lowmem;                /* Low memory mode */
    unsigned int blocksize;    /* Block size (0=auto) */
    int verbose;               /* Verbosity level */
    int json_progress;         /* Output progress in JSON format */
};

static void print_json_progress(uint64_t current, uint64_t total, const char *status)
{
    if (total > 0) {
        printf("{\"type\":\"progress\",\"current\":%llu,\"total\":%llu,\"percent\":%.2f,\"status\":\"%s\"}\n",
               (unsigned long long)current,
               (unsigned long long)total,
               (float)current * 100.0 / total,
               status);
    } else {
        printf("{\"type\":\"progress\",\"status\":\"%s\"}\n", status);
    }
    fflush(stdout);
}

static void print_json_result(int files_recovered, const char *dest_dir, const char *status)
{
    printf("{\"type\":\"result\",\"files_recovered\":%d,\"destination\":\"%s\",\"status\":\"%s\"}\n",
           files_recovered, dest_dir, status);
    fflush(stdout);
}

static void print_usage(void)
{
    printf("\nPhotorec Non-Interactive Mode Usage:\n");
    printf("photorec --auto [options] disk_or_image\n\n");
    printf("Options:\n");
    printf("  --auto                Run in non-interactive mode (required)\n");
    printf("  --disk PATH           Disk or image file path\n");
    printf("  --destination DIR     Destination directory for recovered files\n");
    printf("  --partition NUM       Partition number (0=whole disk, 1=first partition, etc.)\n");
    printf("  --formats LIST        Comma-separated list of file formats (e.g., jpg,doc,pdf)\n");
    printf("                        Use 'all' for all formats (default)\n");
    printf("  --free-space-only     Recover only from free space\n");
    printf("  --keep-corrupted      Keep corrupted files\n");
    printf("  --paranoid            Enable paranoid mode (more thorough but slower)\n");
    printf("  --expert              Enable expert mode\n");
    printf("  --lowmem              Enable low memory mode\n");
    printf("  --blocksize SIZE      Set block size (default: auto-detect)\n");
    printf("  --verbose             Increase verbosity\n");
    printf("  --json                Output progress in JSON format\n");
    printf("  --list-formats        List all available file formats\n");
    printf("\nExamples:\n");
    printf("  # Recover all files from disk\n");
    printf("  photorec --auto --disk /dev/sdb --destination /tmp/recovered\n");
    printf("\n");
    printf("  # Recover only JPG and PDF from partition 1\n");
    printf("  photorec --auto --disk /dev/sdb --partition 1 --formats jpg,pdf --destination /tmp/recovered\n");
    printf("\n");
    printf("  # Recover from disk image, free space only\n");
    printf("  photorec --auto --disk disk.img --free-space-only --destination /tmp/recovered\n");
    printf("\n");
}

static void list_available_formats(file_enable_t *files_enable)
{
    file_enable_t *file_enable;
    printf("Available file formats:\n");
    printf("Format  Description\n");
    printf("------  -----------\n");
    
    for(file_enable = &files_enable[0]; file_enable->file_hint != NULL; file_enable++)
    {
        if(file_enable->file_hint->extension != NULL)
        {
            printf("%-6s  %s\n", 
                   file_enable->file_hint->extension,
                   file_enable->file_hint->description);
        }
    }
}

static int parse_format_list(struct ph_auto_options *auto_opts, const char *format_str, file_enable_t *files_enable)
{
    char *formats = strdup(format_str);
    char *token;
    int count = 0;
    
    if (!formats)
        return -1;
    
    /* Count formats */
    for (token = strtok(formats, ","); token != NULL; token = strtok(NULL, ","))
        count++;
    
    /* Allocate format list */
    auto_opts->format_list = (char **)malloc(sizeof(char *) * count);
    if (!auto_opts->format_list) {
        free(formats);
        return -1;
    }
    
    /* Parse formats */
    strcpy(formats, format_str);
    count = 0;
    for (token = strtok(formats, ","); token != NULL; token = strtok(NULL, ",")) {
        auto_opts->format_list[count] = strdup(token);
        count++;
    }
    auto_opts->format_count = count;
    
    /* Apply format selection */
    if (strcmp(format_str, "all") != 0) {
        file_enable_t *file_enable;
        
        /* Disable all formats first */
        for(file_enable = &files_enable[0]; file_enable->file_hint != NULL; file_enable++)
            file_enable->enable = 0;
        
        /* Enable selected formats */
        for (int i = 0; i < auto_opts->format_count; i++) {
            int found = 0;
            for(file_enable = &files_enable[0]; file_enable->file_hint != NULL; file_enable++) {
                if(file_enable->file_hint->extension != NULL &&
                   strcasecmp(auto_opts->format_list[i], file_enable->file_hint->extension) == 0) {
                    file_enable->enable = 1;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                log_warning("Unknown format: %s\n", auto_opts->format_list[i]);
            }
        }
    }
    
    free(formats);
    return 0;
}

int photorec_auto_mode(int argc, char **argv, struct ph_options *options)
{
    struct ph_auto_options auto_opts = {0};
    struct ph_param params = {0};
    disk_t *disk = NULL;
    list_part_t *list_part = NULL;
    list_disk_t *list_disk = NULL;
    alloc_data_t list_search_space = {0};
    int i;
    int result = 0;
    int files_recovered = 0;
    
    /* Initialize defaults */
    auto_opts.partition_num = -1;  /* Whole disk by default */
    auto_opts.paranoid = 1;
    
    /* Parse command line arguments */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--auto") == 0) {
            /* Already handled */
        }
        else if (strcmp(argv[i], "--disk") == 0 && i+1 < argc) {
            auto_opts.disk_path = argv[++i];
        }
        else if (strcmp(argv[i], "--destination") == 0 && i+1 < argc) {
            auto_opts.dest_dir = argv[++i];
        }
        else if (strcmp(argv[i], "--partition") == 0 && i+1 < argc) {
            auto_opts.partition_num = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--formats") == 0 && i+1 < argc) {
            auto_opts.file_formats = 1;
            if (parse_format_list(&auto_opts, argv[++i], options->list_file_format) < 0) {
                log_error("Failed to parse format list\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "--free-space-only") == 0) {
            auto_opts.free_space_only = 1;
        }
        else if (strcmp(argv[i], "--keep-corrupted") == 0) {
            auto_opts.keep_corrupted = 1;
        }
        else if (strcmp(argv[i], "--paranoid") == 0) {
            auto_opts.paranoid = 1;
        }
        else if (strcmp(argv[i], "--no-paranoid") == 0) {
            auto_opts.paranoid = 0;
        }
        else if (strcmp(argv[i], "--expert") == 0) {
            auto_opts.expert = 1;
        }
        else if (strcmp(argv[i], "--lowmem") == 0) {
            auto_opts.lowmem = 1;
        }
        else if (strcmp(argv[i], "--blocksize") == 0 && i+1 < argc) {
            auto_opts.blocksize = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--verbose") == 0) {
            auto_opts.verbose++;
        }
        else if (strcmp(argv[i], "--json") == 0) {
            auto_opts.json_progress = 1;
        }
        else if (strcmp(argv[i], "--list-formats") == 0) {
            list_available_formats(options->list_file_format);
            return 0;
        }
        else if (strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        }
        else if (auto_opts.disk_path == NULL) {
            /* Assume it's the disk path if not already set */
            auto_opts.disk_path = argv[i];
        }
    }
    
    /* Validate required parameters */
    if (!auto_opts.disk_path) {
        log_error("Error: Disk path is required\n");
        print_usage();
        return 1;
    }
    
    if (!auto_opts.dest_dir) {
        /* Use default directory */
        auto_opts.dest_dir = strdup("recup_dir");
    }
    
    /* Apply options */
    options->paranoid = auto_opts.paranoid;
    options->keep_corrupted_file = auto_opts.keep_corrupted;
    options->expert = auto_opts.expert;
    options->lowmem = auto_opts.lowmem;
    options->verbose = auto_opts.verbose;
    
    /* Open disk/image */
    if (auto_opts.json_progress) {
        print_json_progress(0, 100, "Opening disk");
    }
    
    disk = file_test_availability(auto_opts.disk_path, options->verbose, TESTDISK_O_RDONLY|TESTDISK_O_READAHEAD_32K);
    if (!disk) {
        log_error("Unable to open disk/image: %s\n", auto_opts.disk_path);
        if (auto_opts.json_progress) {
            print_json_result(0, auto_opts.dest_dir, "error: unable to open disk");
        }
        return 1;
    }
    
    /* Create disk list */
    list_disk = insert_new_disk(NULL, disk);
    
    /* Get partition list */
    list_part = init_list_part(disk, NULL);
    if (!list_part) {
        log_error("Failed to initialize partition list\n");
        if (auto_opts.json_progress) {
            print_json_result(0, auto_opts.dest_dir, "error: failed to get partitions");
        }
        delete_list_disk(list_disk);
        return 1;
    }
    
    /* Autodetect partitions */
    autodetect_arch(disk, &arch_none);
    disk->arch->check_part(disk, options->verbose, list_part, 0);
    
    /* Select partition or whole disk */
    params.disk = disk;
    params.recup_dir = strdup(auto_opts.dest_dir);
    params.carve_free_space_only = auto_opts.free_space_only;
    
    if (auto_opts.partition_num < 0) {
        /* Use whole disk */
        params.partition = NULL;
    } else {
        /* Find specified partition */
        list_part_t *element;
        int part_num = 0;
        
        for(element = list_part; element != NULL; element = element->next) {
            if (element->part->order != NO_ORDER && part_num == auto_opts.partition_num) {
                params.partition = element->part;
                break;
            }
            if (element->part->order != NO_ORDER)
                part_num++;
        }
        
        if (!params.partition && auto_opts.partition_num > 0) {
            log_error("Partition %d not found\n", auto_opts.partition_num);
            if (auto_opts.json_progress) {
                print_json_result(0, auto_opts.dest_dir, "error: partition not found");
            }
            part_free_list(list_part);
            delete_list_disk(list_disk);
            free(params.recup_dir);
            return 1;
        }
    }
    
    /* Initialize search space */
    TD_INIT_LIST_HEAD(&list_search_space.list);
    if (params.partition) {
        list_search_space.start = params.partition->part_offset;
        list_search_space.end = params.partition->part_offset + params.partition->part_size - 1;
    } else {
        list_search_space.start = 0;
        list_search_space.end = disk->disk_size - 1;
    }
    td_list_add(&list_search_space.list, &list_search_space.list);
    
    /* Set block size */
    if (auto_opts.blocksize > 0) {
        params.blocksize = auto_opts.blocksize;
    } else {
        /* Auto-detect block size */
        uint64_t offset = 0;
        params.blocksize = find_blocksize(&list_search_space, disk->sector_size, &offset);
    }
    
    /* Create destination directory */
    params.dir_num = photorec_mkdir(params.recup_dir, 1);
    if (params.dir_num == 0) {
        log_error("Failed to create destination directory: %s\n", params.recup_dir);
        if (auto_opts.json_progress) {
            print_json_result(0, auto_opts.dest_dir, "error: failed to create directory");
        }
        part_free_list(list_part);
        delete_list_disk(list_disk);
        free(params.recup_dir);
        return 1;
    }
    
    /* Run recovery */
    if (auto_opts.json_progress) {
        print_json_progress(0, 100, "Starting recovery");
    }
    
    log_info("Starting recovery on %s\n", auto_opts.disk_path);
    if (params.partition) {
        log_info("Partition: %s\n", params.partition->fsname);
    } else {
        log_info("Using whole disk\n");
    }
    log_info("Destination: %s\n", params.recup_dir);
    log_info("Block size: %u\n", params.blocksize);
    
    /* Run the actual recovery */
    {
        pstatus_t ind_stop;
        
        /* Initialize recovery parameters */
        params_reset(&params, options);
        
        /* Run the recovery */
        ind_stop = photorec_aux(&params, options, &list_search_space);
        
        if (ind_stop != PSTATUS_OK) {
            log_error("Recovery stopped: %s\n", 
                     ind_stop == PSTATUS_STOP ? "User requested stop" :
                     ind_stop == PSTATUS_ENOSPC ? "No space left" :
                     ind_stop == PSTATUS_EACCES ? "Access denied" : "Unknown error");
            result = 1;
        } else {
            log_info("Recovery completed successfully\n");
        }
        
        /* Get number of recovered files - this would need to be extracted from params or session */
        files_recovered = params.file_nbr;
    }
    
    /* Cleanup */
    part_free_list(list_part);
    delete_list_disk(list_disk);
    free(params.recup_dir);
    
    /* Free format list */
    if (auto_opts.format_list) {
        for (i = 0; i < auto_opts.format_count; i++) {
            free(auto_opts.format_list[i]);
        }
        free(auto_opts.format_list);
    }
    
    if (auto_opts.json_progress) {
        print_json_result(files_recovered, auto_opts.dest_dir, "completed");
    }
    
    return result;
}