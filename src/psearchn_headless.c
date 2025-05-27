/*

    File: psearchn_headless.c

    Copyright (C) 1998-2013 Christophe GRENIER <grenier@cgsecurity.org>
    Copyright (C) 2024 Headless modifications for non-interactive mode

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

/* Force disable ncurses for headless mode */
#undef HAVE_NCURSES

#include <stdio.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_TIME_H
#include <time.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_WINDEF_H
#include <windef.h>
#endif
#ifdef HAVE_WINBASE_H
#include <stdarg.h>
#include <winbase.h>
#endif
#include "types.h"
#include "common.h"
#include "intrf.h"
#include <errno.h>
#include "dir.h"
#include "fat.h"
#include "fat_dir.h"
#include "list.h"
#include "filegen.h"
#include "photorec.h"
#include "sessionp.h"
#include "log.h"
#include "file_tar.h"
#include "pnext.h"
#include "file_found.h"
#include "psearch.h"
#include "psearchn.h"
#include "psearchn_headless.h"
#include "photorec_check_header.h"
#define READ_SIZE 1024*512
extern int need_to_stop;

/* Progress reporting for headless mode */
static void report_progress_json(const struct ph_param *params, uint64_t offset, time_t current_time)
{
    static time_t start_time = 0;
    static time_t last_report = 0;
    
    if (start_time == 0) {
        start_time = current_time;
    }
    
    /* Report progress every second */
    if (current_time > last_report) {
        last_report = current_time;
        
        uint64_t total_size = params->partition->part_size;
        uint64_t current_pos = offset - params->partition->part_offset;
        double percent = (total_size > 0) ? (100.0 * current_pos / total_size) : 0;
        
        printf("{\"type\": \"progress\", \"pass\": %u, \"percent\": %.2f, \"elapsed\": %ld}\n",
               params->pass, percent, (long)(current_time - start_time));
        fflush(stdout);
    }
}

pstatus_t photorec_aux_headless(struct ph_param *params, const struct ph_options *options, alloc_data_t *list_search_space)
{
  pstatus_t ind_stop=PSTATUS_OK;
#ifndef DISABLED_FOR_FRAMAC
  uint64_t offset;
  unsigned char *buffer_start;
  unsigned char *buffer_olddata;
  unsigned char *buffer;
  time_t start_time;
  time_t previous_time;
  time_t next_checkpoint;
  const unsigned int blocksize=params->blocksize; 
  const unsigned int buffer_size=blocksize + READ_SIZE;
  const unsigned int read_size=(blocksize>65536?blocksize:65536);
  uint64_t offset_before_back=0;
  unsigned int back=0;
#ifdef DISABLED_FOR_FRAMAC
  char buffer_start_tmp[512+READ_SIZE];
#endif
  alloc_data_t *current_search_space;
  file_recovery_t file_recovery;
  memset(&file_recovery, 0, sizeof(file_recovery));
  reset_file_recovery(&file_recovery);
  file_recovery.blocksize=blocksize;
  start_time=time(NULL);
  previous_time=start_time;
  next_checkpoint=start_time+5*60;
#ifdef DISABLED_FOR_FRAMAC
  buffer_start=buffer_start_tmp;
#else
  buffer_start=(unsigned char *)MALLOC(buffer_size);
#endif
  buffer_olddata=buffer_start;
  buffer=buffer_olddata+blocksize;
  current_search_space=td_list_entry(list_search_space->list.next, alloc_data_t, list);
  if(current_search_space!=list_search_space)
    offset=set_search_start(params, &current_search_space, list_search_space);
  else
  {
    offset=params->partition->part_offset;
  }
  if(options->verbose > 0)
    info_list_search_space(list_search_space, current_search_space, params->disk->sector_size, 0, options->verbose);
  if(options->verbose > 1)
  {
    log_verbose("Reading sector %10llu/%llu\n",
	(unsigned long long)((offset-params->partition->part_offset)/params->disk->sector_size),
	(unsigned long long)((params->partition->part_size-1)/params->disk->sector_size));
  }
  params->offset=offset;
  /* Main loop */
  while(current_search_space!=list_search_space)
  {
    pfstatus_t file_recovered=PFSTATUS_BAD;
    data_check_t data_check_status=DC_SCAN;
    int file_recovered_old=PFSTATUS_BAD;
    uint64_t old_offset=offset;
    int need_to_check_file=0;
    if(need_to_stop!=0)
    {
      ind_stop=PSTATUS_STOP;
    }
    if(ind_stop!=PSTATUS_OK)
    {
      log_info("PhotoRec has been stopped\n");
      file_recovery_aborted(&file_recovery, params, list_search_space);
#ifndef DISABLED_FOR_FRAMAC
      free(buffer_start);
#endif
      return ind_stop;
    }
    if(offset==params->partition->part_offset)
    {
      params->offset=offset;
      {
	const time_t current_time=time(NULL);
	if(current_time >= next_checkpoint)
	{
	  session_save(list_search_space, params, options);
	  next_checkpoint=current_time+5*60;
	}
      }
    }
    {
      unsigned int buffer_read_size=read_size;
      const uint64_t offset_test=(offset_before_back!=0 && offset_before_back < offset ? offset_before_back : offset);
      if(buffer_read_size > offset_test - current_search_space->start + blocksize)
	buffer_read_size=offset_test - current_search_space->start + blocksize;
      if(params->disk->pread(params->disk, buffer, buffer_read_size, offset) != buffer_read_size)
      {
	const time_t current_time=time(NULL);
	if(current_time<=previous_time+1)
	  need_to_stop=1;
      }
      if(need_to_stop!=0)
      {
	ind_stop=PSTATUS_STOP;
      }
    }
    if(ind_stop==PSTATUS_OK)
    {
      const uint64_t old_offset_end=file_recovery.location.end;
      file_recovered=file_finish_bf(&file_recovery, params, list_search_space);
      if(file_recovered==PFSTATUS_BAD)
      {
	if(data_check_status==DC_SCAN)
	{
	  if(file_recovered_old==PFSTATUS_OK)
	  {
	    offset_before_back=offset;
	    if(back < 5 &&
		get_prev_file_header(list_search_space, &current_search_space, &offset)==0)
	    {
	      back++;
	    }
	    else
	    {
	      back=0;
	      get_prev_location_smart(list_search_space, &current_search_space, &offset, file_recovery.location.start);
	    }
	  }
	  else
	  {
	    get_next_sector(list_search_space, &current_search_space,&offset,blocksize);
	    if(offset > offset_before_back)
	      back=0;
	  }
	}
      }
      else if(file_recovered==PFSTATUS_OK_TRUNCATED)
      {
	offset_before_back=offset;
	if(back < 5 &&
	    get_prev_file_header(list_search_space, &current_search_space, &offset)==0)
	{
	  back++;
	}
	else
	{
	  back=0;
	  get_next_sector(list_search_space, &current_search_space, &offset, blocksize);
	}
      }
      else
      {
	if(options->lowmem==0 && file_recovery.file_stat!=NULL && file_recovery.file_stat->file_hint!=NULL &&
	    file_recovery.file_stat->file_hint->recover==1)
	  forget(list_search_space, current_search_space);
	if(old_offset_end < offset)
	  offset=old_offset_end;
	back=0;
	offset_before_back=0;
	get_next_sector(list_search_space, &current_search_space, &offset, blocksize);
	if(options->verbose > 1)
	{
	  log_trace("sector %llu filename=%s\n",
	      (unsigned long long)(old_offset/params->disk->sector_size), file_recovery.filename);
	}
      }
    }
    if(current_search_space==list_search_space)
    {
#ifndef DISABLED_FOR_FRAMAC
      free(buffer_start);
#endif
      return ind_stop;
    }
    if(offset > current_search_space->end)
    {
      log_verbose("offset=%llu > current_search_space->end=%llu\n",
	  (unsigned long long)(offset / params->disk->sector_size),
	  (unsigned long long)(current_search_space->end / params->disk->sector_size));
      current_search_space=td_list_entry(current_search_space->list.next, alloc_data_t, list);
      offset=current_search_space->start;
    }
    if(old_offset==offset)
      break;
    if(ind_stop==PSTATUS_OK)
    {
      {
        const time_t current_time=time(NULL);
        if(current_time>previous_time)
        {
          previous_time=current_time;
          /* Report progress in JSON format for headless mode */
          report_progress_json(params, offset, current_time);
          params->offset=offset;
          if(need_to_stop!=0 || ind_stop!=PSTATUS_OK)
          {
            log_info("PhotoRec has been stopped\n");
            file_recovery_aborted(&file_recovery, params, list_search_space);
#ifndef DISABLED_FOR_FRAMAC
            free(buffer_start);
#endif
            return ind_stop;
          }
        }
      }
    }
    if(file_recovery.file_stat!=NULL)
    {
      buffer_olddata+=blocksize;
      buffer+=blocksize;
      if(file_recovery.file_stat->file_hint!=NULL && 
	  file_recovery.data_check!=NULL)
      {
	int i;
	for(i=0; i<8 && i<read_size/blocksize && data_check_status==DC_CONTINUE; i++)
	{
	  data_check_status=file_recovery.data_check(buffer_olddata, blocksize, &file_recovery);
	  if(data_check_status==DC_CONTINUE)
	  {
	    buffer_olddata+=blocksize;
	    buffer+=blocksize;
	  }
	}
	buffer_olddata=buffer_start;
	buffer=buffer_olddata+blocksize;
	need_to_check_file=0;
      }
      else
      {
	buffer_olddata=buffer_start;
	buffer=buffer_olddata+blocksize;
	data_check_status=DC_CONTINUE;
      }
      if(data_check_status==DC_CONTINUE)
      {
	file_block_append(&file_recovery, list_search_space, &current_search_space, &offset, blocksize, 0);
	if(file_recovery.data_check!=NULL)
	{
	  const unsigned int read_size_test=(offset%read_size==0?read_size:blocksize);
	  data_check_status=file_recovery.data_check(buffer_olddata,read_size_test,&file_recovery);
	  if(data_check_status==DC_STOP || data_check_status==DC_ERROR)
	  {
	    need_to_check_file=0;
	  }
	}
	else if(need_to_check_file==1)
	{
	  data_check_status=DC_SCAN;
	}
      }
      if(need_to_check_file==1)
      {
	if(file_recovery.file_stat->file_hint->max_filesize>0 && file_recovery.file_size>=file_recovery.file_stat->file_hint->max_filesize)
	{
	  data_check_status=DC_STOP;
	}
	else
	{
	  /* Skip file_data_check in headless mode */
	  data_check_status=DC_CONTINUE;
	}
      }
      if(data_check_status!=DC_STOP && data_check_status!=DC_ERROR && file_recovery.file_stat->file_hint->max_filesize>0 && file_recovery.file_size>=file_recovery.file_stat->file_hint->max_filesize)
      {
	data_check_status=DC_STOP;
	log_verbose("File should not be bigger than %llu, stopped adding data\n",
	    (long long unsigned)file_recovery.file_stat->file_hint->max_filesize);
      }
      if(data_check_status!=DC_STOP && data_check_status!=DC_ERROR && file_recovery.file_size + blocksize >= PHOTOREC_MAX_SIZE_32 && is_fat(params->partition))
      {
	data_check_status=DC_STOP;
	log_verbose("File should not be bigger than %llu, stopped adding data\n",
	    (long long unsigned)file_recovery.file_stat->file_hint->max_filesize);
      }
      if(data_check_status==DC_STOP || data_check_status==DC_ERROR)
      {
	if(data_check_status==DC_ERROR)
	  file_recovery.file_size=0;
	file_recovered=file_finish2(&file_recovery, params, options->paranoid, list_search_space);
	if(options->lowmem > 0)
	  forget(list_search_space,current_search_space);
      }
    }
    if(ind_stop!=PSTATUS_OK)
    {
      log_info("PhotoRec has been stopped\n");
      file_recovery_aborted(&file_recovery, params, list_search_space);
#ifndef DISABLED_FOR_FRAMAC
      free(buffer_start);
#endif
      return ind_stop;
    }
    if(file_recovered==PFSTATUS_BAD)
    {
      if(data_check_status==DC_SCAN)
      {
	if(file_recovered_old==PFSTATUS_OK)
	{
	  offset_before_back=offset;
	  if(back < 5 &&
	      get_prev_file_header(list_search_space, &current_search_space, &offset)==0)
	  {
	    back++;
	  }
	  else
	  {
	    back=0;
	    get_prev_location_smart(list_search_space, &current_search_space, &offset, file_recovery.location.start);
	  }
	}
	else
	{
	  get_next_sector(list_search_space, &current_search_space,&offset,blocksize);
	  if(offset > offset_before_back)
	    back=0;
	}
      }
    }
    else if(file_recovered==PFSTATUS_OK_TRUNCATED)
    {
      offset_before_back=offset;
      if(back < 5 &&
	  get_prev_file_header(list_search_space, &current_search_space, &offset)==0)
      {
	back++;
      }
      else
      {
	back=0;
	get_next_sector(list_search_space, &current_search_space, &offset, blocksize);
      }
    }
    else
    {
      if(options->lowmem==0 && file_recovery.file_stat!=NULL && file_recovery.file_stat->file_hint!=NULL &&
	  file_recovery.file_stat->file_hint->recover==1)
	forget(list_search_space, current_search_space);
      back=0;
      offset_before_back=0;
      get_next_sector(list_search_space, &current_search_space, &offset, blocksize);
      if(options->verbose > 1)
      {
	log_trace("sector %llu filename=%s\n",
	    (unsigned long long)(old_offset/params->disk->sector_size), file_recovery.filename);
      }
    }
    file_recovered_old=file_recovered;
  } /* end while(current_search_space!=list_search_space) */
#ifndef DISABLED_FOR_FRAMAC
  free(buffer_start);
#endif
#endif
  /* Final progress report */
  report_progress_json(params, offset, time(NULL));
  return ind_stop;
}