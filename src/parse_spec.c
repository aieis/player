#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>

#include "parse_spec.h"

#define SEQ_SEC "[sequence"
#define JUMP_SEC "[jump]"
#define GEN_SEC "[general]"
#define START_KEY "start"

int file_exists (char *filename) {
    struct stat buffer;   
    return (stat(filename, &buffer) == 0);
}

void trim(char * s) {
    char * p = s;
    int l = strlen(p);

    while(isspace(p[l - 1]) || p[l - 1] == '\n') p[--l] = 0;
    while(* p && (isspace(* p) || *p == '\n')) ++p, --l;

    memmove(s, p, l + 1);
}   

int char_index(char* str, char c)
{
    size_t len = strlen(str);
    for (int i = 0; i < len; i++) {
        if (str[i] == c) {
            return i;
        }
    }
    return -1;
}

bool find_clip_id(char* line, int (*addr)[2])
{
    
    int seq_id = atoi(line + 1);
    int clip_id = atoi(line + char_index(line, '.') + 1);

    addr[0][0] = seq_id;
    addr[0][1] = clip_id;
    return true;
}

clip_t make_clip(char* line)
{
    clip_t clip;
    find_clip_id(line, &clip.address);
    
    char* next = line + char_index(line, ' ') + 1;
    clip.start = atoi(next);

    next = next + char_index(next, ' ') + 1;
    clip.end = atoi(next);

    clip.addresses = NULL;
    clip.njumps = 0;
    clip.addrsize = 0;
    
    return clip;
}

bool insert_jumps(clip_t** sequences, char* line)
{
    int address[2];
    find_clip_id(line, &address);

    char* next = line + char_index(line, ' ') + 1;

    int jump_address[2];
    find_clip_id(next, &jump_address);

    clip_t* clip = &sequences[address[0]][address[1]];
    int index = clip->njumps * 2;

    if (index + 1 >= clip->addrsize) {
        int nmax = clip->addrsize * 1.5 + 2;
        clip->addresses = realloc(clip->addresses, nmax * sizeof(int));
        clip->addrsize = nmax;
    }
    clip->addresses[index] = jump_address[0];
    clip->addresses[index+1] = jump_address[1];
    clip->njumps += 1;
    return true;
}

bool insert_clip(clip_t*** sequences_p, int* max_seq, int** max_clips_p, clip_t clip)
{
    clip_t** sequences = *sequences_p;
    int* max_clips = *max_clips_p;
    
    int seq_id = clip.address[0];
    int clip_id = clip.address[1];

    int cmax = *max_seq;

    if (seq_id >= cmax) {
        int nmax = seq_id * 1.5 + 1;
        printf("cmax - nmax: %d - %d\n", cmax, nmax);
        
        sequences = realloc(sequences, nmax * sizeof(clip_t*));
        max_clips = realloc(max_clips, nmax * sizeof(int));
        
        memset(sequences + cmax, 0, (nmax - cmax) * sizeof(clip_t*));
        memset(max_clips + cmax, 0, (nmax - cmax) * sizeof(int));
        *max_seq = nmax;
    }

    int cclip_max = max_clips[seq_id];
    if (clip_id >= cclip_max) {
        int nmax = clip_id * 1.5 + 1;
        sequences[seq_id] = realloc(sequences[seq_id], nmax * sizeof(clip_t));
        memset(sequences[seq_id] + cclip_max, 0, (nmax - cclip_max) * sizeof(clip_t));
        max_clips[seq_id] = nmax;
    }
    
    sequences[seq_id][clip_id] = clip;

    *sequences_p = sequences;
    *max_clips_p = max_clips;

    return true;
}

clip_t** parse_spec (char* file_name, int (*start)[2])
{

    FILE *fp = NULL;
    
    if (( fp = fopen(file_name, "r")) == NULL)
    {
        printf("Can't open %s for reading.\n", file_name);
        return 0;
    }

    char* line = NULL;
    size_t len = 0;
    ssize_t read;

    int max_seq = 0;

    int* max_clips = NULL;
    clip_t ** sequences = NULL;

    int mode = 0;
    while ((read = getline(&line, &len, fp)) != -1) {
        trim(line);
        size_t size = strlen(line);
        if (size == 0 || line[0] == '#') continue;

        printf("(%zd %zu %lu) %s\n", read, len, strlen(line), line);

        if (line[0] == '[') {
            if (strstr(line, GEN_SEC) == line) {
                mode = 0;
            } else if (strstr(line, SEQ_SEC) == line) {
                mode = 1;
            } else if (strstr(line, JUMP_SEC) == line) {
                mode = 2;
            } else {
                mode = -1;
            }
        } else if (mode == 0 && strstr(line, START_KEY) == line) {
            find_clip_id(line + strlen(START_KEY) + 1, start);
        } else if (mode == 1) { //sequencing
            clip_t clip = make_clip(line);
            printf("Inserting: s%d.%d\n", clip.address[0], clip.address[1]);
            insert_clip(&sequences, &max_seq, &max_clips, clip);
        } else if (mode == 2) { //jumps
            insert_jumps(sequences, line);
        }
    }

    for (int i = 0; i < max_seq; i++) {
        int cclip_max = max_clips[i];
        for (int j = 0; j < cclip_max; j++) {
            clip_t clip = sequences[i][j];
            printf("(s%d.%d, %d, %d)\n", clip.address[0], clip.address[1], clip.start, clip.end);
        }
    }

    free(line);

    printf("Start: [%d][%d]\n", start[0][0], start[0][1]);
    return sequences;
 }

clip_t get_clip(clip_t** sequences, int (*address)[2])
{
    return sequences[address[0][0]][address[0][1]];
}
