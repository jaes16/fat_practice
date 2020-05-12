// fat (file allocation table) file system - not using fuse - not very accurately fatfs either.
// 2020 (c) me.inc I suppose
// Simulated by using a file named fat_disk instead of a disk.
// Supports: ls, cd, mkdir, rmdir, more, echo, >, rm, cp, directories greater than one block, reorganization of blocks. To a degree.
// Plan to support chmod, i_date(?), i_stat, symbolic/hard links, resistance to (pseudo)power outages, (also have to update size of paremt in its children)
// I really should divide this up into separate files




#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>




/////////////////////////////////////////////////////// SIZES //////////////////////////////////////////////////////
#define DISK_SIZE 10485759
#define BLOCK_SIZE 4096
#define CLUSTER_TOTALNUM 2560
#define FAT_LENGTH 2556
#define ENDOFFILE 3000 // fat table entry value for eof (ie, being used)
#define FREE 4000 // fat table entry value for free
#define PATH_MAX 4096 //unix definition.
#define NAME_MAX 32
#define SYM_MAX 100
#define COM_LENGTH 4096
#define NUM_DIR_ENTRIES 85 // number of dir entries that fit into a disk block

// errors
#define EPERM 1 //operation not permitted
#define ENOENT 2 //no such file or directory
#define EIO 5 //I/O error
#define ENOMEM 12 //out of memory
#define EACCES 13 //permission denied
#define EEXIST 17 //file already exists
#define ENOTDIR 20 //not a directory
#define EISDIR 21 //is a directory
#define ENOSPC 28 //no more space
#define EMLINK 31 //too many links
#define ENAMETOOLONG 36 //file name too long
#define ENOTEMPTY 39 //directory not empty
#define ELOOP 40 //too many symbolic links

#define ECOMMAND 666 //command in the wrong format. My own shtuf


/////////////////////////////////////////////////////// DEFINITION OF DATA STRUCTURES  /////////////////////////////
struct fat_superblock
{
  // if uninitialized
  int fat_location;
  int fat_size;
  int data_location; //root dir first cluster location
  int data_size;
  //  int freelist_location; //essentially pointer to the first free cluster
};

union {
  struct fat_superblock s;
  char pad[BLOCK_SIZE];
} superblock;


typedef struct
{
  int cur;
  struct free_node *next;
}free_node;

//if we change this, change number of directory entries in block (current size = 4+4+4+2+32 = 48)
typedef struct
{
  int first_cluster;
  int file_size; // in bytes
  int dir_type; // 0 for empty, 1 for directory, 2 for file, 3 for symlink. can be removed if merged with mode
  mode_t mode; // mode
  char name[NAME_MAX];
}dir_entry;




/////////////////////////////////////////////////////// GLOBAL VARIABLES ////////////////////////////////////////////////////////
char disk_path[1024];
FILE *disk;
int fd_disk;


int fat[3072]; // size of 3 blocks, but only using 2556


int num_sym_links = 0;

char rootdir[PATH_MAX];
char workdir[PATH_MAX];
//int workdir_clusternum;


int user = 0;
int u_write_perm = W_OK;
int u_read_perm = R_OK;

char output[PATH_MAX];
int output_offset = 0;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////// CODE ///////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* pre: ret has to be of size <COM_LENGTH
*/
void my_dirname(char *ret, const char *original){
  memset(ret, 0, PATH_MAX);
  char* dup = strdup(original);

  strcpy(ret, dirname(dup));

  free(dup);
  return;
}


/* pre: ret has to be of size <COM_LENGTH
*/
void my_basename(char *ret, const char *original){
  memset(ret, 0, NAME_MAX);
  char* dup = strdup(original);

  strcpy(ret, basename(dup));

  free(dup);
  return;
}






// When theres no disk, create generic disk
void create_init_disk()
{
  // write out superblock
  fd_disk = fileno(disk);
  pwrite(fd_disk,&superblock,BLOCK_SIZE,0);

  // write out fat
  memset(&fat,0,sizeof(fat));
  for(int i = 0; i < (CLUSTER_TOTALNUM - 4); i++){
    // fill buffers with value for free
    fat[i] = FREE;
  }
  //make sure root dir cluster is allocated
  fat[0] = ENDOFFILE;

  pwrite(fd_disk,&fat,BLOCK_SIZE*3,BLOCK_SIZE);


  // write out root dir
  dir_entry dir_entries[NUM_DIR_ENTRIES]; //rather arbitrary size, but point is its bigger than block size
  memset(dir_entries, 0, sizeof(dir_entries));
  //creating .
  dir_entries[0].first_cluster = 4;
  dir_entries[0].file_size = 2 * sizeof(dir_entry);
  dir_entries[0].dir_type = 1;
  dir_entries[0].mode = S_IFDIR | 0755;
  memset(dir_entries[0].name,0,NAME_MAX);
  strcpy(dir_entries[0].name,".");
  // creating ..
  dir_entries[1].first_cluster = 4;
  dir_entries[1].file_size = 2 * sizeof(dir_entry);
  dir_entries[1].dir_type = 1;
  dir_entries[1].mode = S_IFDIR | 0755;
  memset(dir_entries[1].name,0,NAME_MAX);
  strcpy(dir_entries[1].name,"..");
  pwrite(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (4*BLOCK_SIZE));

}





static void* fat_init()
{
  // find where "disk" file should be
  memset(disk_path, 0, sizeof(disk_path));
  getcwd(disk_path, sizeof(disk_path));
  strcat(disk_path, "/fat_disk");

  // see if "disk" file is initialized
  if((disk = fopen(disk_path, "r+"))){
    // if it is, read in superblock
    fd_disk = fileno(disk);
    pread(fd_disk,&superblock,sizeof(superblock),0);

    pread(fd_disk,&fat, sizeof(fat),BLOCK_SIZE);

  }else{
    // if it isn't, create file and write out initial superblock
    disk = fopen(disk_path, "w+");
    fseek(disk, DISK_SIZE, SEEK_SET);
    fputc('\0', disk);

    // initial locations for fat and data
    superblock.s.fat_location = 1;
    superblock.s.fat_size = 3;
    superblock.s.data_location = 4;
    superblock.s.data_size = FAT_LENGTH;
    // superblock.s.freelist_location = 5;

    // write out initialized disk
    create_init_disk();

  }

  // set root and working directories
  memset(rootdir,0,PATH_MAX);
  memset(workdir,0,PATH_MAX);
  strcpy(rootdir,"/");
  strcpy(workdir,rootdir);
  //workdir_clusternum = 4;
  return NULL;
}



// return errors: -1 if path doesn't exist, -2 if path name is too long, -3 if component of path is file, -4 if too many symlinks
// if no error, returns the index of the block the directory entry was in.
// later, can remove last and just find it from path in this function
static int dir_exists(const char *path, int cluster_num, dir_entry *de)
{

  // bring in all the dir entries
  dir_entry dir_entries[NUM_DIR_ENTRIES];
  memset(dir_entries, 0, sizeof(dir_entry) * NUM_DIR_ENTRIES);
  pread(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*cluster_num));

  // name of the next directory in path to look for
  char dup[PATH_MAX];
  strcpy(dup, path);
  char* current = strtok(dup, "/"); //strtok, because its not used again in this method

  // check whether name is too long
  if(strlen(current) > NAME_MAX){
    return -2;
  }

  //search through dir_entries in current block to see if a component of the path is here
  for (int i = 0; i < NUM_DIR_ENTRIES; i++){
    if(dir_entries[i].dir_type != 0){
      // will the real slim shady please stand up?
      if(strcmp(dir_entries[i].name, current) == 0){ //if we find a match, check if last component
        if (strchr(path, '/') == NULL){ // we're in the endgame now
          strcpy(de->name,dir_entries[i].name);
          de->first_cluster = dir_entries[i].first_cluster;
          de->file_size = dir_entries[i].file_size;
          de->mode = dir_entries[i].mode;
          de->dir_type = dir_entries[i].dir_type;

          return cluster_num;
        }else{ // another component in the path
          if (dir_entries[i].dir_type == 2) { //if match is file
            return -3;
          }
          else if (dir_entries[i].dir_type == 3) { //if match is a symlink
            if(num_sym_links == SYM_MAX) return -4;
            num_sym_links++;

            char next_path[PATH_MAX];
            memset(next_path, 0, PATH_MAX);
            pread(fd_disk, next_path, BLOCK_SIZE, BLOCK_SIZE * dir_entries[i].first_cluster); // because PATH_MAX == BLOCK_SIZE

            if(next_path[PATH_MAX -1] != 0) return -2; // if path length is too long
            else { // if not, go through path of symlink
              int temp_result = dir_exists(next_path, cluster_num, de);
              if(temp_result < 0) return temp_result;
              if(de->dir_type == 2) return -3; // if what was returned is a file, return error, because we have a path left to peruse
              else{ // go through rest of path
                memset(next_path, 0, PATH_MAX);
                strcpy(next_path, strchr(path, '/') + 1);
                int cluster_n = de->first_cluster;
                memset(de, 0, sizeof(dir_entry));
                return dir_exists(next_path, cluster_n, de);
              }
            }
          }
          else{ //if match is dir
            char next_path[PATH_MAX];
            strcpy(next_path, (strchr(path, '/') + 1)); // skip to next (+1 so we're after /)!
            return dir_exists(next_path, dir_entries[i].first_cluster, de);
          }
        }
      }
    }
  }
  // if we didn't find a match in this block, check if there are other blocks in the file to peruse, and peruse those
  if(fat[cluster_num - 4] !=  ENDOFFILE){
    return dir_exists(path, fat[cluster_num - 4] + 4, de);
  }

  de = NULL;
  return -1;

}






/*
key: finish the find_dir method, getattr should check if target dir exists, if so
fill the stat struct *stbuf with the information in the dir_entry and return
if desired dir doesn't exist, return -ENOENT?
mkdir is gonna suck
*/
//return file attributes, for path name, should fill in the elements of the stat struc
//if a field is meaningless then it should be set to 0
static int fat_getattr(const char *path, struct stat *stbuf)
{

  int res = 0;

  memset(stbuf, 0, sizeof(struct stat));

  // see if path is too long
  if(strlen(path) > PATH_MAX){
    return -ENAMETOOLONG;
  }

  // if root
  if (strcmp(path, "/") == 0) {
    // get directory size, and the rest is the usual
    dir_entry dir_entries[NUM_DIR_ENTRIES];//[((4096/sizeof(dir_entries)) + 1)];
    memset(dir_entries, 0, sizeof(dir_entry) * NUM_DIR_ENTRIES);
    pread(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*4));

    stbuf -> st_mode = S_IFDIR | dir_entries[0].mode;
    stbuf -> st_nlink = 1;
    stbuf -> st_size = dir_entries[0].file_size;
    int num_blocks = 0;
    int cluster = dir_entries[0].first_cluster;
    while(cluster != (ENDOFFILE + 4)){
      num_blocks++;
      cluster = fat[cluster - 4] + 4;
    }
    stbuf -> st_blocks = num_blocks;

    return res;
  } else {

    // prepare for dir_exists call
    int cluster_num = 4;
    dir_entry de;

    //if path doesn't exist or name is too long, return error
    int call_rslt = dir_exists(path,cluster_num,&de);
    if (call_rslt == -1) return -ENOENT;
    if (call_rslt == -2) return -ENAMETOOLONG;
    if (call_rslt == -3) return -ENOTDIR;

    // if no errors and entry is a directory
    else if (de.dir_type == 1) {
      //stbuf -> st_mode = S_IFDIR | 0755;
      stbuf -> st_mode = S_IFDIR | de.mode;

      stbuf -> st_nlink = 1;
      stbuf -> st_size = de.file_size;
      int num_blocks = 0;
      int cluster = de.first_cluster;
      while(cluster != (ENDOFFILE + 4)){
        num_blocks++;
        cluster = fat[cluster - 4] + 4;
      }
      stbuf -> st_blocks = num_blocks;

      // NEED TO CHANGE IF YOU HAVE SYM LINKS
    }
    else{ // if no errors and entry is a file
      // check permissions
      //if(de.read_only == 1) stbuf -> st_mode = S_IFREG | 0444;
      //else stbuf->st_mode = S_IFREG | 0666;

      stbuf->st_mode = S_IFREG | de.mode;

      stbuf -> st_nlink = 1;
      stbuf -> st_size = de.file_size;
      int num_blocks = 0;
      int cluster = de.first_cluster;
      while(cluster != (ENDOFFILE + 4)){
        num_blocks++;
        cluster = fat[cluster - 4] + 4;
      }
      stbuf -> st_blocks = num_blocks;
    }
  }
  return res;
}





static int fat_access(const char *path, int mask)
{

  // see if name is too long
  if(strlen(path) > PATH_MAX){
    return -ENAMETOOLONG;
  }


  // i am groot
  if (strcmp(path, "/") == 0) {
    if((0755 & mask) == 0) return -EACCES;
    else return 0;
  }

  else {

    // prepare for dir_exists call
    int cluster_num = 4;
    dir_entry de;

    // if path doesn't exist, return error
    int call_rslt = dir_exists(path,cluster_num,&de);
    if (call_rslt == -1) return -ENOENT;
    if (call_rslt == -2) return -ENAMETOOLONG;
    if (call_rslt == -3) return -ENOTDIR;

    // if path does exist, check for permissions
    if((de.mode & mask) == 0) return -EACCES;
    else return 0;

  }

}





// index should be by number of dir entries
// returns <0 for error, but otherwise, returns the number of dir_entries read
static int fat_readdir(const char *path, dir_entry *ret, off_t offset)
{
  // see if name is too long
  if(strlen(path) > PATH_MAX){
    return -ENAMETOOLONG;
  }

  // get respective cluster number (4 for groot)
  int cluster_num = 4;

  if(strcmp(path, "/") != 0){
    // prepare for dir_exists call
    dir_entry de;

    int call_rslt = dir_exists(path,cluster_num,&de);
    if (call_rslt == -1) return -ENOENT;
    if (call_rslt == -2) return -ENAMETOOLONG;
    if (call_rslt == -3) return -ENOTDIR;
    if ((de.mode & u_read_perm) == 0) return -EACCES;

    cluster_num = de.first_cluster;
  }

  int num_cluster_skip = offset/NUM_DIR_ENTRIES;
  int off = offset - (NUM_DIR_ENTRIES * num_cluster_skip); // now the offset within the block

  // find the cluster to begin reading from, according to offset
  for(int i = 0; i < num_cluster_skip; i++){
    if(fat[cluster_num - 4] == ENDOFFILE) return 0;
    else{
      cluster_num = fat[cluster_num - 4] + 4;
    }
  }

  dir_entry dir_entries[NUM_DIR_ENTRIES];
  memset(dir_entries, 0, sizeof(dir_entry) * NUM_DIR_ENTRIES);
  pread(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*cluster_num));
  //if we should begin reading from here, do so.

  //search through to add all dir_entries from index in this block
  int count = 0;
  for (int i = off; i < NUM_DIR_ENTRIES; i++){
    if(dir_entries[i].dir_type !=0){
      strcpy(ret[i].name, dir_entries[i].name);
      ret[i].first_cluster = dir_entries[i].first_cluster;
      ret[i].file_size = dir_entries[i].file_size;
      ret[i].dir_type = dir_entries[i].dir_type;
      ret[i].mode = dir_entries[i].mode;
    }
    count++;
  }

  //return the number of direntries read
  return count;
}






static int fat_mkdir(const char* path, mode_t mode)
{


  // can't make root
  if(strcmp(path,"/") == 0) return -ENOENT;

  // see if name is too long
  if(strlen(path) > PATH_MAX){
    return -ENAMETOOLONG;
  }

  // separate path and name of last dir
  char parent_path[PATH_MAX];
  memset(parent_path, 0, PATH_MAX);
  if(strchr(path, '/') == NULL) parent_path[0] = '/'; // only one component in path
  else my_dirname(parent_path, path); // more than one component


  dir_entry de;
  dir_entry parent_de;
  memset(&de, 0, sizeof(dir_entry));
  memset(&parent_de, 0, sizeof(dir_entry));

  // check if directory already exists
  int call_rslt = dir_exists(path, 4, &de);
  if (call_rslt == -2) return -ENAMETOOLONG;
  if (call_rslt >= 0){
    // if it exists, but is a file.
    if((de.mode & S_IFREG) != 0) return -ENOTDIR;
    // else it is already an existing directory
    else return -EEXIST;
  }
  if (call_rslt == -3) return -ENOTDIR;
  memset(&de, 0, sizeof(dir_entry));


  dir_entry dir_entries[NUM_DIR_ENTRIES];//[((4096/sizeof(dir_entries)) + 1)];
  memset(dir_entries, 0, NUM_DIR_ENTRIES * sizeof(dir_entry));
  int cluster_parent = 4; // used later to see where in grandparent's blocks parent is stored

  // check if path leading to directory doesn't exist
  if(strcmp(parent_path,"/") == 0) { // i am groot
    //get appropriate directory entry for root
    pread(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*4));
    parent_de.first_cluster = 4;
    parent_de.file_size = dir_entries[0].file_size;
    parent_de.mode = dir_entries[0].mode;
    strcpy(parent_de.name,".");
    parent_de.dir_type = 1;
  }
  else { //if not root,
    // check if parent exists, and if it does, get info
    cluster_parent = dir_exists(parent_path, 4, &parent_de);
    if (cluster_parent == -1) return -ENOENT; // if parent doesn't exist
  }


  //permissions
  if ((parent_de.mode & u_write_perm) == 0) return -EACCES;


  int target_cluster = parent_de.first_cluster;
  int target_index = -1;


  // check if there is space for more directory entries in parent
  while(target_cluster != (ENDOFFILE + 4)){
    memset(dir_entries, 0, sizeof(dir_entry) * NUM_DIR_ENTRIES);
    pread(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE * target_cluster));

    for(int i = 0; i < NUM_DIR_ENTRIES; i++){ // go through the block to see if there are empty spots
      if(dir_entries[i].dir_type == 0){ // this is empty!
        target_index = i;
        break;
      }
    }
    // found an empty spot, we good
    if(target_index != -1) break;
    // this was last block, we need to allocate new block
    else if(fat[target_cluster-4] == ENDOFFILE) break;
    // this wasn't last block, look at next one
    else target_cluster = fat[target_cluster-4] + 4;
  }

  // didn't find an empty spot, need to allocate more space for parent.
  if(target_index == -1){
    // find an empty block that we can write out to
    for(int i = 0; i < FAT_LENGTH; i++){
      if(fat[i] == FREE){
        fat[target_cluster-4] = i;
        fat[i] = ENDOFFILE;
        target_cluster = i + 4;
        target_index = 0;
        pwrite(fd_disk, &fat, BLOCK_SIZE * 3, BLOCK_SIZE);
        break;
      }
    }
    // no space in fat table
    if(target_index == -1) return -ENOSPC;
  }



  // adding new cluster
  for(int i= 0; i <FAT_LENGTH; i++){
    // if we find an empty cluster
    if(fat[i] == FREE){
      // set cluster to endoffile and write out
      fat[i] = ENDOFFILE;
      pwrite(fd_disk,&fat, (BLOCK_SIZE*3), BLOCK_SIZE);


      // FOR NEW DIR ENTRY IN NEW CLUSTER
      // creating new dir entry
      memset(dir_entries,0,sizeof(dir_entries));

      de.first_cluster = i+4;
      de.file_size = sizeof(dir_entry) * 2;
      de.dir_type = 1;
      de.mode = (mode | S_IFDIR);
      memset(de.name,0,sizeof(de.name));
      strcpy(de.name,".");
      dir_entries[0] = de;

      dir_entries[1].first_cluster = parent_de.first_cluster;
      dir_entries[1].file_size = parent_de.file_size + sizeof(dir_entry);
      dir_entries[1].dir_type = 1;
      dir_entries[1].mode = parent_de.mode;
      strcpy(dir_entries[1].name,"..");

      //write out new directory cluster
      pwrite(fd_disk,&dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*(i+4)));




      // FOR CHANGING PARENT CLUSTER
      // read in parent cluster
      pread(fd_disk,&dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*target_cluster));

      // change parent cluster
      my_basename(de.name, path);
      dir_entries[target_index] = de;

      pwrite(fd_disk,&dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*target_cluster));

      memset(dir_entries, 0, sizeof(dir_entry) * NUM_DIR_ENTRIES);
      // update size in .
      pread(fd_disk,&dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*parent_de.first_cluster));
      dir_entries[0].file_size += sizeof(dir_entry);

      // check if parent cluster is root
      if(parent_de.first_cluster == 4){
        // then also have to update ..
        dir_entries[1].file_size += sizeof(dir_entry);
      }
      // write out parent cluster
      pwrite(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*parent_de.first_cluster));



      // POSSIBLY CHANGING GRANDPARENT CLUSTER
      // if there is a grandparent
      if(parent_de.first_cluster != 4){ // if not root, then has grandparent
        // read in grandparent cluster
        pread(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*cluster_parent));
        // change parent dir entry in gp cluster
        for (int j = 0; j < NUM_DIR_ENTRIES; j++){
          // check if is the parent dir entry
          if(strcmp(dir_entries[j].name,parent_de.name) == 0){
            dir_entries[j].file_size += sizeof(dir_entry);
            break;
          }
        }
        pwrite(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry),(BLOCK_SIZE*cluster_parent));
      }


      return 0;
    }
  }

  return -ENOSPC;
}







static int fat_rmdir(const char* path)
{

  // can't remove root
  if(strcmp(path,"/") == 0) return -ENOENT;

  // see if name is too long
  if(strlen(path) > PATH_MAX){
    return -ENAMETOOLONG;
  }

  // separate path and name of last dir
  char parent_path[PATH_MAX];
  memset(parent_path, 0, PATH_MAX);
  if(strchr(path, '/') == NULL) parent_path[0] = '/'; // only one component in path
  else my_dirname(parent_path, path); // more than one component

  dir_entry de;
  dir_entry parent_de;
  memset(&de, 0, sizeof(dir_entry));
  memset(&parent_de, 0, sizeof(dir_entry));

  // check if directory exists
  int target_cluster = dir_exists(path, 4, &de);
  if (target_cluster == -2) return -ENAMETOOLONG;
  if (target_cluster == -1) return -ENOENT;
  if (target_cluster == -3) return -ENOTDIR;
  if (de.dir_type == 2) return -ENOTDIR;
  if ((de.mode & u_write_perm) == 0) return -EACCES;



  dir_entry dir_entries[NUM_DIR_ENTRIES];//[((4096/sizeof(dir_entries)) + 1)];
  memset(dir_entries, 0, sizeof(dir_entry) * NUM_DIR_ENTRIES);
  int cluster_parent = 0;

  // check if path leading to directory doesn't exist
  if(strcmp(parent_path,"/") == 0) { // i am groot
    //get appropriate directory entry for root
    pread(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*4));
    parent_de.first_cluster = 4;
    parent_de.file_size = dir_entries[0].file_size;
    strcpy(parent_de.name,".");
    parent_de.dir_type = 1;
    parent_de.mode = dir_entries[0].mode;
  } else { //if not root,
    // get info
    cluster_parent = dir_exists(parent_path, 4, &parent_de);
    }

  //permissions
  if ((parent_de.mode & u_write_perm) == 0) return -EACCES;


  // check if directory is empty
  int cluster = de.first_cluster;
  while(cluster != (ENDOFFILE + 4)){
    memset(dir_entries, 0, NUM_DIR_ENTRIES*sizeof(dir_entry));
    pread(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*cluster));
    for (int i = 0; i < NUM_DIR_ENTRIES; i++){
      if (dir_entries[i].dir_type != 0){
        if((cluster == de.first_cluster) && ((i == 0) || (i == 1))) {} //if its . or .., ignore
        else return -ENOTEMPTY;
      }
    }
    cluster = fat[cluster-4]+4;
  }

  // 0 out the dir entry in parent
  memset(dir_entries, 0, NUM_DIR_ENTRIES*sizeof(dir_entry));
  pread(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*target_cluster));

  for (int i = 0; i < NUM_DIR_ENTRIES; i++){
    if (strcmp(dir_entries[i].name, de.name) == 0){
      memset(&dir_entries[i], 0, sizeof(dir_entry));
      break;
    }
  }
  pwrite(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*target_cluster));

  // reduce size of parent
  memset(dir_entries, 0, NUM_DIR_ENTRIES*sizeof(dir_entry));
  pread(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*parent_de.first_cluster));
  dir_entries[0].file_size -= sizeof(dir_entry);
  if (parent_de.first_cluster == 4){ //if root, . == ..
    dir_entries[1].file_size -= sizeof(dir_entry);
  }
  pwrite(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*parent_de.first_cluster));


  // 0 out block and label it free in fat table
  cluster = de.first_cluster;
  while(cluster != (ENDOFFILE +4)){
    // 0 out block
    memset(dir_entries, 0, NUM_DIR_ENTRIES * sizeof(dir_entry));
    pwrite(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*de.first_cluster));

    // next cell block!
    int temp = cluster;
    cluster = fat[cluster - 4] + 4;
    fat[temp - 4] = FREE;
  }
  pwrite(fd_disk, &fat, 3*BLOCK_SIZE, BLOCK_SIZE);

  // decrease file size of parent in grandparent
  if (parent_de.first_cluster != 4){
    pread(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*cluster_parent));
    for (int i = 0; i < NUM_DIR_ENTRIES; i++){
      if (strcmp(dir_entries[i].name, parent_de.name) == 0){
        dir_entries[i].file_size  -= sizeof(dir_entry);
        break;
      }
    }
    pwrite(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*cluster_parent));
  }
  return 0;
}







static int fat_mknod(const char* path, mode_t mode)
{
  // can't make root
  if(strcmp(path,"/") == 0) return -EISDIR;
  // see if name is too long
  if(strlen(path) > PATH_MAX){
    return -ENAMETOOLONG;
  }

  //check mode
  if((mode & S_IFREG) == 0) return -EACCES;

  // separate path and name of last dir
  char parent_path[PATH_MAX];
  memset(parent_path, 0, PATH_MAX);
  if(strchr(path, '/') == NULL) parent_path[0] = '/'; // only one component in path
  else my_dirname(parent_path, path); // more than one component
  char file_name[PATH_MAX];
  my_basename(file_name, path);

  dir_entry de;
  dir_entry parent_de;
  memset(&de, 0, sizeof(dir_entry));
  memset(&parent_de, 0, sizeof(dir_entry));

  // check if file already exists (using parent_de, but not looking for parent directory entry, just for convenience)
  int call_rslt = dir_exists(path, 4, &de);
  if (call_rslt == -2) return -ENAMETOOLONG;
  if (call_rslt >= 0) {
    if((de.mode & S_IFDIR) != 0) return -EISDIR;
    return -EEXIST;
  }
  if (call_rslt == -3) return -ENOTDIR;
  memset(&de, 0, sizeof(dir_entry));

  dir_entry dir_entries[NUM_DIR_ENTRIES];//[((4096/sizeof(dir_entries)) + 1)];
  memset(dir_entries, 0, NUM_DIR_ENTRIES*sizeof(dir_entry));

  int cluster_parent = 0; //for use later.

  // check if path leading to directory doesn't exist
  if(strcmp(parent_path,"/") == 0) { // i am groot
    //get appropriate directory entry for root
    pread(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*4));
    parent_de.first_cluster = 4;
    parent_de.file_size = dir_entries[0].file_size;
    parent_de.mode = dir_entries[0].mode;
    strcpy(parent_de.name,".");
    parent_de.dir_type = 1;
  } else { //if not root,
    // get info
    cluster_parent = dir_exists(parent_path, 4, &parent_de);
    if (cluster_parent == -1) return -ENOENT; //doesn't exist
  }


  if ((parent_de.mode & u_write_perm) == 0) return -EACCES; //permission


  int target_cluster = parent_de.first_cluster;
  int target_index = -1;

  // check if there is space for more directory entries in parent
  while(target_cluster != (ENDOFFILE + 4)){
    memset(dir_entries, 0, sizeof(dir_entry) * NUM_DIR_ENTRIES);
    pread(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE * target_cluster));

    for(int i = 0; i < NUM_DIR_ENTRIES; i++){ // go through the block to see if there are empty spots
      if(dir_entries[i].dir_type == 0){ // this is empty!
        target_index = i;
        break;
      }
    }
    // found an empty spot, we good
    if(target_index != -1) break;
    // this was last block, we need to allocate new block
    else if(fat[target_cluster-4] == ENDOFFILE) break;
    // this wasn't last block, look at next one
    else target_cluster = fat[target_cluster-4] + 4;
  }

  // didn't find an empty spot, need to allocate more space.
  if(target_index == -1){
    // find an empty block that we can write out to
    for(int i = 0; i < FAT_LENGTH; i++){
      if(fat[i] == FREE){
        fat[target_cluster-4] = i;
        fat[i] = ENDOFFILE;
        target_cluster = i + 4;
        target_index = 0;
        pwrite(fd_disk, &fat, BLOCK_SIZE * 3, BLOCK_SIZE);
        break;
      }
    }
    // no space in fat table
    if(target_index == -1) return -ENOSPC;
  }




  for(int i= 0; i <FAT_LENGTH; i++){
    // if we find an empty cluster
    if(fat[i] == FREE){
      // set cluster to endoffile and write out
      fat[i] = ENDOFFILE;
      pwrite(fd_disk,&fat, (BLOCK_SIZE*3), BLOCK_SIZE);


      // FOR NEW DIR ENTRY IN NEW CLUSTER
      // creating new dir entry
      dir_entry de;
      de.first_cluster = i+4;
      de.file_size = 0;
      de.dir_type = 2;
      de.mode = mode | S_IFREG;


      //check mode to get permissions

      de.mode = S_IFREG | mode;
      memset(de.name,0,sizeof(de.name));
      strcpy(de.name,file_name);


      // FOR CHANGING PARENT CLUSTER
      // read in parent cluster
      memset(dir_entries, 0, NUM_DIR_ENTRIES * sizeof(dir_entry));
      pread(fd_disk,&dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*target_cluster));

      // add new entry in parent cluster
      dir_entries[target_index] = de;
      pwrite(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*target_cluster));

      memset(dir_entries, 0, NUM_DIR_ENTRIES * sizeof(dir_entry));
      pread(fd_disk,&dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*parent_de.first_cluster));

      // update size in .
      dir_entries[0].file_size += sizeof(dir_entry);

      // check if parent cluster is root
      if(parent_de.first_cluster == 4){
        // then also have to update ..
        dir_entries[1].file_size += sizeof(dir_entry);
      }
      // write out parent cluster
      pwrite(fd_disk,&dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*parent_de.first_cluster));

      // POSSIBLY CHANGING GRANDPARENT CLUSTER
      // if there is a grandparent
      if(parent_de.first_cluster != 4){
        // read in grandparent cluster
        memset(dir_entries, 0, sizeof(dir_entry) * NUM_DIR_ENTRIES);
        pread(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*cluster_parent));
        // change parent dir entry in gp cluster
        for (int j = 0; j < NUM_DIR_ENTRIES; j++){
          // check if is the parent dir entry
          if(strcmp(dir_entries[j].name,parent_de.name) == 0){
            dir_entries[j].file_size += sizeof(dir_entry);
            break;
          }
        }
        pwrite(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*cluster_parent));
      }
      return 0;
    }
  }
  return -ENOSPC;
}







static int fat_unlink(const char* path)
{
  // can't make root
  if(strcmp(path,"/") == 0) return -ENOENT;
  // see if name is too long
  if(strlen(path) > PATH_MAX){
    return -ENAMETOOLONG;
  }

  // separate path and name of last dir
  char parent_path[PATH_MAX];
  memset(parent_path, 0, PATH_MAX);
  if(strchr(path, '/') == NULL) parent_path[0] = '/'; // only one component in path
  else my_dirname(parent_path, path); // more than one component


  dir_entry de;
  dir_entry parent_de;

  // check if directory already exists (using parent_de, but not looking for parent directory entry, just for convenience)
  int target_cluster = dir_exists(path, 4, &de);
  if (target_cluster == -2) return -ENAMETOOLONG;
  if (target_cluster == -1) return -ENOENT;
  if (target_cluster == -3) return -ENOTDIR;
  if (de.dir_type == 1) return -ENOENT;
  if ((de.mode & u_write_perm) == 0) return -EACCES;

  dir_entry dir_entries[NUM_DIR_ENTRIES];//[((4096/sizeof(dir_entries)) + 1)];
  memset(dir_entries, 0, NUM_DIR_ENTRIES * sizeof(dir_entry));
  int cluster_parent = 0;

  // check if path leading to directory doesn't exist
  if(strcmp(parent_path,"/") == 0) { // i am groot
    //get appropriate directory entry for root
    pread(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*4));
    parent_de.first_cluster = 4;
    parent_de.file_size = dir_entries[0].file_size;
    strcpy(parent_de.name,".");
    parent_de.dir_type = 1;
    parent_de.mode = dir_entries[0].mode;
  } else { //if not root,
    // get info
    cluster_parent = dir_exists(parent_path, 4, &parent_de);
  }


  //permissions
  if ((parent_de.mode & u_write_perm) == 0) return -EACCES;

  // 0 out the dir entry in parent
  memset(dir_entries, 0, sizeof(dir_entry) * NUM_DIR_ENTRIES);
  pread(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*target_cluster));
  for(int i = 0; i < NUM_DIR_ENTRIES; i++){
    if(strcmp(dir_entries[i].name, de.name) == 0){
      memset(&dir_entries[i], 0, sizeof(dir_entry));
      break;
    }
  }
  pwrite(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*target_cluster));

  // decrease file size of parent
  memset(dir_entries, 0, sizeof(dir_entry) * NUM_DIR_ENTRIES);
  pread(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*parent_de.first_cluster));
  dir_entries[0].file_size -= sizeof(dir_entry);
  //if root
  if (parent_de.first_cluster == 4){
    dir_entries[1].file_size -= sizeof(dir_entry);
  }
  pwrite(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*parent_de.first_cluster));


  // 0 out the clusters of this file
  memset(&dir_entries, 0, NUM_DIR_ENTRIES * sizeof(dir_entry));
  int cluster_to_erase = de.first_cluster;
  int temp = 0;
  // while last cluster wasn't EOF (so this is equal to 3000 (+4 for actual cluster placement not fat placement))
  while(cluster_to_erase != (ENDOFFILE + 4)){
    // 0 out.
    if(pwrite(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE*cluster_to_erase)) == -1) return -EIO;
    // getting next cluster and setting this fat entry as free
    temp = cluster_to_erase;
    cluster_to_erase = fat[cluster_to_erase-4]+4;
    fat[temp-4] = FREE;
  }

  // set fat entry as free
  pwrite(fd_disk, &fat, 3*BLOCK_SIZE, BLOCK_SIZE);


  // decrease file size of parent in grandparent
  if (parent_de.first_cluster != 4){ // if not node
    if(pread(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE * cluster_parent)) == -1) return -EIO;
    for (int i = 2; i < NUM_DIR_ENTRIES; i++){
      if (strcmp(dir_entries[i].name, parent_de.name) == 0){
	        dir_entries[i].file_size -= sizeof(dir_entry);
	        break;
      }
    }
    pwrite(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE * cluster_parent));
  }
  return 0;
}







//return -1 for io error
static int read_helper(char *buf, size_t size, off_t offset, int block, int buf_place){


  char temp_buf[BLOCK_SIZE];
  memset(temp_buf, 0, BLOCK_SIZE);

  // read in block
  if(pread(fd_disk,&temp_buf,BLOCK_SIZE,(BLOCK_SIZE*block)) == -1) return -1;

  // calculate size to read from this cluster
  int size_to_read = (int) size;
  if((size+offset) > BLOCK_SIZE) size_to_read = (BLOCK_SIZE-offset);

  // copy from offset into buf
  for(int i = 0; i < size_to_read; i++){
    buf[buf_place+i] = temp_buf[i+offset];
  }

  buf_place += size_to_read;

  //decrement size left to read
  size = size - size_to_read;
  if(size > 0){

    if((fat[block-4] == ENDOFFILE) == 1) return 0;

    // read into latest place in buf the remaining size, from the next block with no offset
    return read_helper(buf,size,0,(fat[block-4]+4), buf_place);
  }

  // if we read everything
  return 0;
}







// read size bytes from the given file into the buffer buf, beginning offset bytes into the file
// returns the number of bytes transfered or 0 if offset was at or beyond the end of the file
static int fat_read(const char* path, char *buf, size_t size, off_t offset)
{

  // can't read root
  if(strcmp(path,"/") == 0) return -EISDIR;
  // see if name is too long
  if(strlen(path) > PATH_MAX){
    return -ENAMETOOLONG;
  }

  dir_entry de;

  // check if directory already exists (using parent_de, but not looking for parent directory entry, just for convenience)
  int call_rslt = dir_exists(path, 4, &de);
  if (call_rslt == -2) return -ENAMETOOLONG;
  if (call_rslt == -1) return -ENOENT;
  if (call_rslt == -3) return -ENOTDIR;
  if (de.dir_type == 1) return -EISDIR;
  if ((de.mode & u_read_perm) == 0) return -EACCES;


  // THREE CASES: offset > de.size: read nothing return 0
  //        size+offset <= de.size: read from offset, (size) amount
  //size > de.size or size + offset > size: read from offset, (de.size-offset) amount

  // case 1: offset > de.size
  if(offset>de.file_size) return 0;


  // otherwise...
  else {
    // find order of the block that we should start reading from
    int b = (offset / BLOCK_SIZE);
    int start_place;
    if (offset - (b*BLOCK_SIZE) == 0) start_place = 0;
    else {
      start_place = offset - b*BLOCK_SIZE;
      b++;
    }

    // set cluster num to the number of that block
    int cluster_num = de.first_cluster;
    for (int i = 1; i < b; i++){
      if(fat[cluster_num-4] == ENDOFFILE) return 0; // dont read anything if offset out ranges size (shouldn't because of the check uptop, but oh well)
      cluster_num = fat[cluster_num-4]+4;
    }

    // case 2: size+offset <= de.size
    if ((size+offset) <= de.file_size){ // read size amount
      call_rslt = read_helper(buf, size, start_place, cluster_num, 0);
      if(call_rslt == -1) return -EIO;
      return size;
    }

    else{ //case 3: size+offset > de.size, or size > de.size
      // read (de.size-offset) amount
      call_rslt = read_helper(buf, (de.file_size - start_place), start_place, cluster_num, 0);
      if(call_rslt == -1) return -EIO;
      return (de.file_size - start_place);
    }

  }

}









// returns -1 for io error, -2 for no more space
static int write_helper(char *buf, size_t size, off_t offset, int block, int buf_place){

  char temp_buf[BLOCK_SIZE];
  memset(temp_buf, 0, BLOCK_SIZE);

  // read in block
  if(pread(fd_disk,&temp_buf,BLOCK_SIZE,(BLOCK_SIZE*block)) == -1) return -1;

  // calculate size to write to this cluster
  int size_to_write = (int) size;
  if((size+offset) > BLOCK_SIZE) size_to_write = (BLOCK_SIZE-offset);

  // copy from offset into temp buf
  for(int i = 0; i < size_to_write; i++){
    temp_buf[i+offset] = buf[buf_place+i];
  }

  // write out block
  if(pwrite(fd_disk,&temp_buf,BLOCK_SIZE,(BLOCK_SIZE*block)) == -1) return -1;

  buf_place += size_to_write;

  //decrement size left to write
  size = size - size_to_write;

  if(size > 0){
    // if we ran out of space, allocate more space (the universe expands fast. so should storage)
    if(fat[block-4] == ENDOFFILE){

      for(int i = 0; i < FAT_LENGTH; i++){
        if(fat[i] == FREE){ // if we find a free cluster

          //set this one as new, and the previous cluster to point to this one
          fat[block-4] = i;
          fat[i] = ENDOFFILE;
          // write out fat
          if(pwrite(fd_disk,&fat,(BLOCK_SIZE*3),BLOCK_SIZE) == -1) return -1;

          // continue on
          // write into latest place in buf the remaining size, to the next block with no offset
          return write_helper(buf,size,0,(i+4), buf_place);
        }

      }
      // if there isn't any empty cluster left, return error
      return -2;

    }else{ // if there is another cluster left in the file, write into that one
      // write from latest place in buf the remaining size, to the next block with no offset
      return write_helper(buf,size,0,(fat[block-4]+4), buf_place);
    }
  }

  // if we write everything
  return 0;
}









static int fat_write(const char* path, char *buf, size_t size, off_t offset)
{
  // read size bytes from the given file into the buffer buf, beginning offset bytes into the file
  // returns the number of bytes transfered or 0 if offset was at or beyond the end of the file

  // can't read root
  if(strcmp(path,"/") == 0) return -EISDIR;
  // see if name is too long
  if(strlen(path) > PATH_MAX){
    return -ENAMETOOLONG;
  }

  dir_entry de;

  int dir_entry_cluster = dir_exists(path, 4, &de);
  if (dir_entry_cluster == -2) return -ENAMETOOLONG;
  if (dir_entry_cluster == -1) return -ENOENT;
  if (dir_entry_cluster == -3) return -ENOTDIR;
  if (de.dir_type == 1) return -EISDIR;
  if ((de.mode & u_write_perm) == 0) return -EACCES;

  // find index of linked clusters that we have to write to
  int num_new_b = (offset / BLOCK_SIZE);
  int start_place;
  if (offset - (num_new_b* BLOCK_SIZE) == 0){
    start_place = 0;
  }
  else {
    start_place = offset - (num_new_b*BLOCK_SIZE);
  }
  num_new_b++; // because we need to accomodate for the last cluster thats not full

  // find actual cluster number of the cluster that we have to write to
  int cluster_num = de.first_cluster;
  int current_block_num = (de.file_size/BLOCK_SIZE);
  int previous;

  if((de.file_size%BLOCK_SIZE)!=0) current_block_num++;
  for (int i = 0; i < num_new_b; i++){
    if(cluster_num == (ENDOFFILE+4)){ // if we reach the EOF, and have to increase it
      //find empty cluster
      for(int j = 0; j < FAT_LENGTH; j++){
        if(fat[j] == FREE){ // if we find a free cluster

          //set this one as new, and the previous cluster to point to this one
          fat[previous-4] = j;
          fat[j] = ENDOFFILE;
          cluster_num = j+4;
          // write out fat
          if(pwrite(fd_disk,&fat,(BLOCK_SIZE*3),BLOCK_SIZE) == -1) return -EIO;
          break;
        }
      }
      // if there isn't any empty cluster left, return error
      if(fat[previous-4] == ENDOFFILE) return -ENOSPC;

    }

    // otherwise, set cluster_num (first cluster to be written to) to the next cluster
    previous = cluster_num;
    cluster_num = fat[cluster_num-4]+4;
  }

  cluster_num = previous;

  // actually write out
  int call_rslt = write_helper(buf, size, start_place, cluster_num, 0);
  if(call_rslt == -1) return -EIO;
  if(call_rslt == -2) return -ENOSPC;



  // record new size of file in dir entry if necessary
  if((size+offset) > de.file_size){

    dir_entry dir_entries[NUM_DIR_ENTRIES];
    memset(dir_entries, 0, NUM_DIR_ENTRIES * sizeof(dir_entry));
    if (pread(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE * dir_entry_cluster)) == -1) return -EIO;

    for(int i = 0; i < NUM_DIR_ENTRIES; i++){ // find correct dir entry and set correct file size
      if(strcmp(dir_entries[i].name, de.name) == 0) dir_entries[i].file_size = (size+offset);
    }

    if(pwrite(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE * dir_entry_cluster)) == -1) return -EIO;

  }
  return size;
}







static int fat_truncate(const char *path,  off_t size){

  // groot not file
  if(strcmp(path,"/") == 0) return -EISDIR;
  // see if name is too long
  if(strlen(path) > PATH_MAX){
    return -ENAMETOOLONG;
  }

  dir_entry de;

  int dir_entry_cluster = dir_exists(path, 4, &de);
  if (dir_entry_cluster == -2) return -ENAMETOOLONG;
  if (dir_entry_cluster == -1) return -ENOENT;
  if (dir_entry_cluster == -3) return -ENOTDIR;
  if (de.dir_type == 1) return -EISDIR;
  if ((de.mode & u_write_perm) == 0) return -EACCES;

  // if size is already at the size the user wants,
  if(size == de.file_size) return 0; // such beautiful code. ideally we'd have nothing

  //else, check how many blocks this new file should have
  int new_num_blocks = size/BLOCK_SIZE;
  if((size % BLOCK_SIZE) != 0) new_num_blocks++;
  if(size == 0) new_num_blocks = 1;

  char buf[BLOCK_SIZE];

  int current_block = 1;
  int previous = 0;
  for(int i = de.first_cluster; i != (ENDOFFILE + 4); i = (fat[i-4] + 4) ){

    //if we reach new_num_blocks
    if(current_block == new_num_blocks){ // now we need to empty out the allocated blocks of the file
      // empty out the part of the block that is not needed
      if(pread(fd_disk, &buf, BLOCK_SIZE, BLOCK_SIZE * i) == -1) return -EIO;
      int pos_in_block = size % BLOCK_SIZE; // get position of the first byte we don't want
      if((size != 0 ) && (pos_in_block == 0)) {} // so size is a multiple of BLOCK_SIZE, then we don't want to empty out anything in this block.
      else memset(buf + pos_in_block, 0, BLOCK_SIZE - pos_in_block); // zero out the bytes we don't want anymore
      if(pwrite(fd_disk, &buf, BLOCK_SIZE, BLOCK_SIZE * i) == -1) return -EIO; // cleared part of the first block

      // check if this is not the last block of the file
      if(fat[i-4] != ENDOFFILE){
        // now this is the last block of the file
        previous = i;
        i = fat[i-4] + 4;
        fat[previous -4] = ENDOFFILE;
        if(pwrite(fd_disk, &fat, 3 * BLOCK_SIZE, BLOCK_SIZE) == -1) return -EIO;

        // if there are more allocated blocks, we need to continue to clear out and free all the subsequent blocks
        memset(buf, 0, BLOCK_SIZE);

        while(i != (ENDOFFILE + 4)){ // as long as we've got allocated blocks
          // clear out block;
          if(pwrite(fd_disk, &buf, BLOCK_SIZE, BLOCK_SIZE * i) == -1) return -EIO;

          // get index of next block
          previous = i;
          i = fat[i-4] + 4;

          // free up this block in fat_entry
          fat[previous - 4] = FREE;
          // write out new fat_table


        }
        if(pwrite(fd_disk, &fat, 3 * BLOCK_SIZE, BLOCK_SIZE) == -1) return -EIO;
        // once we're out, i == ENDOFFILE + 4

      }
      break;
    }
    previous = i;
    current_block++;
  }

  // we've reached the last allocated block of file
  if(current_block != new_num_blocks){ // so we need to allocate more blocks for this file
    for(int i = previous; current_block <= new_num_blocks; i = fat[i - 4] + 4){
      // find a free fat entry and set it as the new end
      for(int j = 0; j < FAT_LENGTH; j ++){
        if(fat[j] == FREE){
          fat[i-4] = j;
          fat[j] = ENDOFFILE;
          break;
        }
      }
      // if we didn't find any free fat entries, return error
      if(fat[i - 4] == ENDOFFILE) return -ENOSPC;
    }
    // write out updated fat
    if(pwrite(fd_disk, &fat, 3 * BLOCK_SIZE, BLOCK_SIZE) == -1) return -EIO;
  }

  // when done, update size.

  // reading in parent
  dir_entry dir_entries[NUM_DIR_ENTRIES];
  memset(dir_entries, 0, sizeof(dir_entry) * NUM_DIR_ENTRIES);

  if(pread(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE * dir_entry_cluster)) == -1) return -EIO;

  //update appropriate directory entry
  for(int i = 2; i < NUM_DIR_ENTRIES; i++){ // find correct dir entry and set correct file size
    if(strcmp(dir_entries[i].name,de.name) == 0) dir_entries[i].file_size = size;
  }

  //update
  if(pwrite(fd_disk, &dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), (BLOCK_SIZE * dir_entry_cluster)) == -1) return -EIO;

  return 0;
}








///////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////// user command interpretation ///////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////







/* pre: str and dest must be strings of the same size. dest must be dynamically allocated. str
        is the path we are given.

   post: returns -1 if path exceeds path_max characters. if no error, saves the appropriately
         altered path (according to the format that is needed in the fat methods above) into dest.
*/
int get_absolute_path(const char *str, char *dest){

  // check if path is from root
  if(str[0] == '/') strcpy(dest, str);
  // or if working directory is root
  else if (strcmp(workdir,rootdir) == 0){
    if((strlen(workdir) + strlen(str)) > PATH_MAX){
      return -1;
    }
    strcpy(dest, rootdir);
    strcat(dest, str);
  }
  else { // if path is from working directory, concat with workdir path
    // see if path doesn't exceed size
    if((strlen(workdir) + strlen(str) + 1) > PATH_MAX){
      return -1;
    }

    strcpy(dest, workdir);
    strcat(dest, "/");
    strcat(dest, str);
  }

  // trim dest of repeated /'s
  if(strcmp(dest, "/") != 0){
    char *trim = strchr(dest, '/');
    while(trim != NULL){
      while(trim[1] == '/'){ // this works because trim is a pointer to a char in dest
        memmove(trim, trim+1, strlen(trim));
      }
      trim = strchr(trim+1, '/');
    }
  }
  if(dest[strlen(dest)-1] == '/'){
    dest[strlen(dest)-1] = 0;
  }
  if(dest[0] == '/'){
    memmove(dest, dest+1, strlen(dest));
  }


  return 0;
}








static void throw_error(int err, char *instruct, char *path){
  ///////////////////// ERRORS!!! ///////////////////////
  if(err == 0) return;

  else{

    if(err == -ECOMMAND){ //if error with how the command was written
      if(strcmp(instruct, "ls") == 0) printf("ls: usage: ls [-al] directory ...\n");
      else if(strcmp(instruct, "cd") == 0) printf("cd: usage: cd directory ...\n");
      else if(strcmp(instruct, "more") == 0) printf("more: usage: more filename ...\n");
      else if(strcmp(instruct, "mkdir") == 0) printf("mkdir: usage: mkdir [-pv] [-m mode] directory ...\n");
      else if(strcmp(instruct, "rmdir") == 0) printf("rmdir: usage: rmdir [-p] directory ...\n");
      else if(strcmp(instruct, "rm") == 0) printf("rm: usage: rm filename ...\n");
      else if(strcmp(instruct, "cp") == 0) printf("cp: usage: cp source destination\n");
      else if(strcmp(instruct, ">") == 0) printf("-bash: syntax error near unexpected token 'newline'\n");
    }
    else {

      if(strcmp(instruct,">") == 0) printf("-bash: %s: ", path);
      else if(path) printf("%s: %s: ", instruct, path);
      else printf("%s: ", instruct);

      if(err == -EPERM) printf("Operation not permitted.\n");
      else if(err == -ENOENT) printf("File doesn't exist.\n");
      else if(err == -EACCES) printf("Permission denied.\n");
      else if(err == -EIO) printf("I/O error.\n");
      else if(err == -EEXIST) printf("File already exists.\n");
      else if(err == -ENOTDIR) printf("Not a directory.\n");
      else if(err == -EISDIR) printf("Is a directory.\n");
      else if(err == -ENOSPC) printf("No more space.\n");
      else if(err == -EMLINK) printf("Too many links.\n");
      else if(err == -ENAMETOOLONG) printf("File or path name too long.\n");
      else if(err == -ENOTEMPTY) printf("Directory not empty.\n");
      else if(err == -ELOOP) printf("Too many links.\n");
    }
  }
  return;
}







/* pre: requires a string containing the instruction, a string of size COM_LENGTH containing the remaining
        command (will always be smaller than COM_LENGTH in actuality), a string of size COM_LENGTH that
        is dynamically allocated, another string of size COM_LENGTH that is dynamically allocated, and
        finally, a pointer to an integer initially set to 0.

        uses a dynamically allocated string of size COM_LENGTH.

   post: saves appropriate string(s) to paths, separated by blank spaces if there are multiple. saves option(s),
         to options (duh,) if any, to options, again separated by blank spaces if there are multiple. saves the number of paths
         found to the integer pointer p_found. returns as an integer value an error code, if there was an error, and if not,
         returns the appropriate number of options.
         Doesn't change instruct or command.
         error codes are: -1 for incorrect format of command

*/
int option_helper(char *instruct, char *command, char *paths, char *options, int *p_found){

  if(command == NULL) return 0;

  char *op;


  memset(paths, 0, COM_LENGTH);
  memset(options, 0, COM_LENGTH);

  int path_found = 0; //after this turns to 1, no options can be taken in

  int op_index = 0;



  if(strcmp(instruct, "echo") == 0){ //supports -n

    op = (char *) malloc(COM_LENGTH);
    memset(op, 0, COM_LENGTH);

    memset(paths, 0, COM_LENGTH);
    *p_found = 0;

    //check if -n is there
    if((command[0] == '-') && (command[1] == 'n')){

      // just need to check the first part of the latter command
      if(strchr(command, ' ') != NULL){ // there's a space.
        //separate the first part of the command with the rest
        strncpy(op, command, (strchr(command, ' ') - command));
        //if there is a string after the space, we know there's a string to echo
        if((strchr(command, ' ') - command) == strlen(command)) *p_found = 1;
      }
      else {
        // or its just one continuous string, no spaces
        strcpy(op, command);
      }

      // check if op is an option (+1 to ignore '-')
      if(strtok((op + 1), "n") == NULL){ //if nothing but n's, its just an option
        if(strchr(command, ' ') != NULL){
          strcpy(paths, (strchr(command, ' ') +1));
          *p_found = 1;
        }
        else paths = NULL;
        free(op);
        return 1;
      }
      else{ //
        strcpy(paths, command);
        *p_found = 1;
        free(op);
        return 0;
      }

    }
    else{ //no possibility of options, only a string to print
      strcpy(paths,command);
      *p_found = 1;
      free(op);
      return 0; // no errors in my house
    }

  }


  //lets start reading in
  char *dup = strdup(command);
  if(strtok(dup, " ") == NULL){ // if there are more than
    free(dup);
    dup = strdup(command);
    op = dup; //op and dup point to the same thing! just free dup
  }
  else {
    // because strtok can alter the parameter string
    free(dup);
    dup = strdup(command);

    op = strtok(dup, " ");
  }
  // strtok is fine to use here because nothing else will call it, because we don't call any of our other functions here.
  while(op){

    if(strcmp(instruct, "cd") == 0){
      if(op[0] == '-'){
        free(dup);
        return -1; // cd shouldn't have any options
      }
      else{ //if a path, we only need the first path. We ignore all other paths user may have written. Unix rules, not me.
        strcpy(paths,op);
        *p_found = 1;
        free(dup);
        return 0;
      }
    }

    else if(strcmp(instruct, "ls") == 0){ //supports -l, -a
      if((op[0] != '-') || (path_found != 0)){ // found a path, or found a path previously
        if(path_found == 0) strcpy(paths, op); // if first time, add path
        else{ // if not first path, add a space to divide, then add new path
          strcat(paths, " ");
          strcat(paths, op);
        }
        path_found++;
      }
      else{ //found an option
        for(int i = 1; i < strlen(op); i++){ // from index 1 because we gotta ignore '-'
          if((op[i] == 'l') || (op[i] == 'a')){ //if it has any of these (don't care about repeats)
            if(op_index == 0) strcpy(options, (op + 1)); //if first option, add option
            else{ //f not first option, add a space to divide, then add new option
              strcat(options, " ");
              strcat(options, (op + 1));
            }
            op_index++;
          }
          else{
            free(dup);
            return -1; //if not -a or -l or -la or -al, return -1
          }
        }
      }

    }

    else if(strcmp(instruct, "more") == 0){ //supports no options
      if(op[0] == '-'){
        free(dup);
        return -1; // more shouldn't have any options
      }
      else{ //add path
        if(path_found == 0) strcpy(paths, op);
        else{
          strcat(paths, " ");
          strcat(paths, op);
        }
        path_found++;
      }
    }

    else if(strcmp(instruct, "less") == 0){

    }

    else if(strcmp(instruct, "mkdir") == 0){ // supports -m, -p, -v
      // not an option/path has been found
      if((op[0] != '-') || (path_found != 0)){
        if(path_found == 0) strcpy(paths, op);
        else{
          strcat(paths, " ");
          strcat(paths, op);
        }
        path_found++;
      }
      else{ // its possibly a valid option
        for(int i = 1; i < strlen(op); i++){ //scrutinize from after '-'
          if((op[i] == 'p') || (op[i] == 'v')){

            if(op_index == 0) strcpy(options, (op + 1)); //if first option, add option
            else{ //f not first option, add a space to divide, then add new option
              strcat(options, " ");
              strcat(options, (op + 1));
            }
            op_index++;
          }

          else if(op[i] == 'm'){
            if(i != (strlen(op) -1)){ //if there's a option following this one, error
              free(dup);
              return -1;
            }
            else if((op = strtok(NULL, " ")) && (op[0] != '-')){ // if it has something following it, and is not an option

              if(op_index == 0){ //if first option, add option
                strcpy(options, "m ");
                strcat(options, op);
                break;
              }
              else{ //f not first option, add a space to divide, then add new option
                strcat(options, " m ");
                strcat(options, op);
                break;
              }
              op_index++;

            }
            else{ //otherwise, if it has nothing following it, or has a option following it, error
              free(dup);
              return -1;
            }
          }

        }
      }

    }

    else if(strcmp(instruct, "rmdir") == 0){ // supports -p
      if((op[0] != '-') || (path_found != 0)){
        if(path_found == 0) strcpy(paths, op);
        else{
          strcat(paths, " ");
          strcat(paths, op);
        }
        path_found++;
      }
      else if(op[0] ==  '-'){ // because p is the only option
        for(int i = 1; i < strlen(op); i++){ // from index 1 because we gotta ignore '-'
          if(op[i] == 'p'){ //if it is p
            if(op_index == 0) strcpy(options, (op + 1)); //if first option, add option
            //if not first option, do nothing
          }
          else{
            free(dup);
            return -1; //if not -p
          }
          op_index = 1;
        }
      }
      else{
        free(dup);
        return -1;
      }
    }

    else if(strcmp(instruct, "rm") == 0){ //supports no options
      if(op[0] == '-'){
        free(dup);
        return -1; // rm shouldn't have any options
      }
      else{ //add path
        if(path_found == 0) strcpy(paths, op);
        else{
          strcat(paths, " ");
          strcat(paths, op);
        }
        path_found++;
      }
    }

    else if(strcmp(instruct, "cp") == 0){
      if(path_found == 0) strcpy(paths, op);
      else if(path_found < 2){
        strcat(paths, " ");
        strcat(paths, op);
      }
      else{ // more paths found than src and dest
        break;
      }
      path_found++;
    }
    else{

    }

    op = strtok(NULL, " ");
  }

  // error handling
  if((strcmp(instruct, "cp") == 0) && (path_found != 2)){ // cp needs to have src and dest strings
    free(dup);
    return -1;
  }
  // mkdir, rmdir, more, and rm need to have at least one path
  if((path_found == 0) && ((strcmp(instruct, "mkdir") == 0) || (strcmp(instruct, "rmdir") == 0) ||
                        (strcmp(instruct, "more") == 0) || (strcmp(instruct, "rm") == 0))){
    free(dup);
    return -1;
  }

  // release!
  *p_found = path_found;
  free(dup);
  return op_index;
}






void nodots_helper(const char *path, char *dest){

  char next_path[COM_LENGTH];
  char ret[COM_LENGTH];
  char dup[COM_LENGTH];
  char dup2[COM_LENGTH];

  memset(dest,0,COM_LENGTH);
  memset(next_path,0,COM_LENGTH);
  memset(ret,0,COM_LENGTH);
  memset(dup,0,COM_LENGTH);
  memset(dup2,0,COM_LENGTH);

  //cases
  // case1: just "/."
  if(strcmp(path,"/.") == 0){
    strcpy(ret, path);
    memset(ret, 0, strlen(ret));
    ret[0] = '/';
    strcpy(dest,ret);
    return;
  }
  // case2: just "/.."
  else if(strcmp(path,"/..") == 0){
    strcpy(ret, path);
    memset(ret,0,strlen(ret));
    ret[0] = '/';
    strcpy(dest,ret);
    return;
  }
  // case 3: starts with "/./...", or has "..././..."
  else if(strstr(path,"/./") != NULL){
    // see if it starts "/./"
    if(strcmp(strstr(path,"/./"),path) == 0){
      // just remove /.
      strcpy(next_path,path+2);
    }
    else{ //if "..././..."
    // get index of /./
    int index = strstr(path, "/./") - path;
    strcpy(next_path, path);
    // get rid of the /. portion
    memmove(&next_path[index], &next_path[index + 2], strlen(&next_path[index + 2]));
    }
    nodots_helper(next_path, ret);
    strcpy(dest, ret);
    return;
  }
  // case4: //starts with "/../...", or has ".../../..."
  else if(strstr(path,"/../") != NULL){
    // see if it starts with "/../"
    if(&(strstr(path,"/../")[0]) == &path[0]){
      // just remove /..
      strcpy(next_path,path+3);
    }
    //if ".../../..." (because strstr always looks for the first instance, we never are looking at the second of /../../, for example
    else{
      // get index of /../
      int index = strstr(path, "/../") - path;
      // copy first half
      memset(dup2, 0, COM_LENGTH);
      strncpy(dup2, path, index);
      // remove last directory of first half
      my_dirname(next_path, dup2);
      //incase we've come down to root. Then dirname(dup2) returns / -> "//nextfile/..."
      if(strcmp(next_path,"/") == 0){
        memset(dup, 0, COM_LENGTH);
        strcpy(dup,(strstr(path, "/../") + 4)); // get rid of all four chars
      }
      // copy second half without /..
      else strcpy(dup,(strstr(path, "/../") + 3)); // get rid of /..
      // merge
      strcat(next_path,dup);
    }
    nodots_helper(next_path, ret);
    strcpy(dest, ret);
    return;
  }
  // case 5: seeing if path ends with "/." or "/.."
  else if(strstr(path,"/.") != NULL){
    //if path ends with /. (otherwise, it would be /./, or /.hi and etc.
    if(strcmp(strstr(path,"/."),"/.") == 0){
      // get rid of /.
      memset(dup, 0, COM_LENGTH);
      strcpy(dup, path);
      my_dirname(next_path, dup);
      nodots_helper(next_path, ret);
      strcpy(dest, ret);
      return;
    }
    //if path ends with /.. (otherwise, it would be /../, or /..hi and etc.
    else if(strcmp(strstr(path,"/.."),"/..") == 0){
      // get rid of /.. and previous directory
      memset(dup, 0, COM_LENGTH);
      my_dirname(dup,path);
      my_dirname(next_path, dup);
      nodots_helper(next_path, ret);
      strcpy(dest, ret);
      return;
      }
    else{} //if path ends with a file starting with . or .., like .hi or ..hi
  }
  // case 6: no dots
  else {
    // absolute bliss
    strcpy(dest, path);
    return;
  }
  // shouldn't reach here, but...
  strcpy(dest, path);
  return;
}







// don't print out errors to altered outputs though. So we can just print out errors directly with printf. Unix Rules.
// we already checked if we could make the file before hand, so we can just assume the output file exists, and we have permission to write into it.
int print_helper(char *format){
  // print out to stdout
  if((strlen(output) == 0) && (format != NULL)) printf("%s", format);

  // we have a different ouput, so store in string
  else if(format != NULL){ // can't write what doesn't exist. but dont quote me on that
    // write out to file
    int err = fat_write(output, format, strlen(format) * sizeof(char), output_offset);

    if(err < 0) return err;
    else {
      output_offset = output_offset + strlen(format); //update the offset we have to write to
      return 0;
    }
  }

  return 0;
}






/* pre:

*/
int output_helper(const char *str, char *command){

  char *dup = malloc(COM_LENGTH);
  memset(dup, 0, COM_LENGTH);
  if(get_absolute_path(str, dup) == -1){
    free(dup);
    return -ENAMETOOLONG;
  }


  // check if it exists
  int err = fat_access(dup, u_write_perm);

  if(err == -ENOENT){ //file doesn't exist
    // try and make file. if it doesn't work, return error
    err = fat_mknod(dup, (u_write_perm | S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH));
    if(err != 0){
      free(dup);
      return err;
    }
  }
  else if(err == 0){ // file exists and have write permission
    // truncate. if truncate doesn't work, return error
    err = fat_truncate(dup, 0);
    if(err != 0){
      free(dup);
      return err;
    }
  }
  else return err; // no access no cigar

  if(command[0] == '>'){ // if command is just "> ..."
    //we made sure the output exists, then truncated it to 0. We done.
    memset(command, 0, strlen(command)); //cause there's no input other than ">..."
    strcpy(output, dup);
    free(dup);
    return 0;
  }
  else{ //ammend command and return 0
    memset(strchr(command, '>'), 0, (strchr(command, '>') - command)); //set everything after and including > to zero
    strcpy(output, dup);
    free(dup);
    return 0;
  }
}












// supports -al
static int i_ls(const char *path, char *options, int opt_num){

  // read in subfiles of working directory

  int err = 0;

  dir_entry dir_entries[NUM_DIR_ENTRIES];
  memset(dir_entries, 0, sizeof(dir_entry)*NUM_DIR_ENTRIES);

  char* dup = malloc(COM_LENGTH);
  memset(dup, 0, COM_LENGTH);

  // get full path
  if(path == NULL) dup[0] = '/';
  else if (get_absolute_path(path, dup) == -1){
    free(dup);
    return -ENAMETOOLONG;
  }
  int offset = 0;
  err = fat_readdir(dup, dir_entries, offset);
  //read in
  while(err > 0){
    offset = offset + err;

    char *temp = malloc(COM_LENGTH);


    // print out subfiles of de.
    for(int i = 0; i <NUM_DIR_ENTRIES; i++){

      memset(temp, 0, COM_LENGTH);

      if((dir_entries[i].dir_type != 0)){
        if(opt_num == 0){ // if no options
          if(dir_entries[i].name[0] != '.'){ // unless hidden, do
            strcpy(temp, dir_entries[i].name);
            strcat(temp, "\t\t");
            print_helper(temp);
          }
          // well hidden file, well hidden code.
        }

        else if((dir_entries[i].name[0] == '.') && (strchr(options, 'a') == NULL)) { // unless option '-a', hidden files are hidden!
        // if it starts with . but doesn't have '-a' option
        }

        else if(strchr(options, 'l') != NULL){
          int mode = dir_entries[i].mode;


          if((mode & S_IFDIR) != 0) strcpy(temp, "d");
        else if((mode & S_IFREG) != 0) strcpy(temp, "-");
        if((mode & S_IRUSR) != 0) strcat(temp, "r");
        else strcat(temp, "-");
        if((mode & S_IWUSR) != 0) strcat(temp, "w");
        else strcat(temp, "-");
        if((mode & S_IXUSR) != 0) strcat(temp, "x");
        else strcat(temp, "-");
        if((mode & S_IRGRP) != 0) strcat(temp, "r");
        else strcat(temp, "-");
        if((mode & S_IWGRP) != 0) strcat(temp, "w");
        else strcat(temp, "-");
        if((mode & S_IXGRP) != 0) strcat(temp, "x");
        else strcat(temp, "-");
        if((mode & S_IROTH) != 0) strcat(temp, "r");
        else strcat(temp, "-");
        if((mode & S_IWOTH) != 0) strcat(temp, "w");
        else strcat(temp, "-");
        if((mode & S_IXOTH) != 0) strcat(temp, "x ");
        else strcat(temp, "- ");

        int num_blocks = 0;
        int cluster = dir_entries[i].first_cluster;
        while(cluster != (ENDOFFILE + 4)){
          num_blocks++;
          cluster = fat[cluster - 4] + 4;
        }
        sprintf((temp + strlen(temp)), "%d ", num_blocks);

        strcat(temp, "user ");
        strcat(temp, "group ");
        sprintf((temp + strlen(temp)), "%d ", dir_entries[i].file_size);
        strcat(temp, "Dec ");
        sprintf((temp + strlen(temp)), "%d ", 25);
        sprintf((temp + strlen(temp)), "%d:", 12);
        sprintf((temp + strlen(temp)), "%d ", 25);
        strcat(temp, dir_entries[i].name);
        strcat(temp, "\n");


        print_helper(temp);
      }
      else { // -a but no -;
        memset(temp, 0, COM_LENGTH);
        strcpy(temp, dir_entries[i].name);
        strcat(temp, "\t\t");
        print_helper(temp);
      }
    }


  }
  memset(dir_entries, 0, sizeof(dir_entry) * NUM_DIR_ENTRIES);
  err = fat_readdir(dup, dir_entries, offset);
  free(temp);
}
if(err < 0){
  free(dup);
  return err;
}

  if((opt_num == 0) || (strchr(options, 'l') == NULL)) print_helper("\n");
  free(dup);
  return 0;
}






static int i_cd(const char *path, char *options, int opt_num){

  // cd into working directory
  if((path == NULL) || (strcmp(path,"/") == 0)){
    //change working directory to root
    strcpy(workdir,rootdir);
    //workdir_clusternum = 4;
  }
  else if(strcmp(path,".") == 0){
    // how wonderful. no work.
    return 0;
  }
  else if(strcmp(path,"..") == 0){
    // if in root, no work.
    if(strcmp(workdir, "/") == 0){
      return 0;
    }
    // if not, work.
    // change working directory to previous directory
    char dup[PATH_MAX];
    strcpy(dup, workdir);
    memset(workdir, 0, PATH_MAX);
    my_dirname(workdir,dup);

    /*
    // get working directory cluster num
    dir_entry de;
    dir_exists(workdir + 1, 4, &de);
    workdir_clusternum = de.first_cluster;
    */
    return 0;
  }

  else { // cd into another directory

    // check if user passed in an absolute path, or relative one
    char *dup = malloc(COM_LENGTH);
    memset(dup, 0, COM_LENGTH);
    if(get_absolute_path(path, dup) == -1){
      free(dup);
      return -ENAMETOOLONG;
    }

    int err = 0;
    err = fat_access(dup,R_OK); //path still works without taking care of . and ..
    if(err != 0){
      free(dup);
      return err;
    }

    // take care of . and ..
    char dup2[COM_LENGTH];
    strcpy(dup2,dup);
    memset(dup,0,strlen(dup));
    nodots_helper(dup2, dup);


    // change working directory to this one
    strcpy(workdir,dup);

    /*
    // get working directory cluster num
    dir_entry de;
    dir_exists(workdir, 4, &de);
    workdir_clusternum = de.first_cluster;
    */

    free(dup);
    return 0;
  }

  // if there are descriptions, switch by case (if statements in c)
  return 0;
}








//supports -mpv
static int i_mkdir(const char *path, char *options, int opt_num){


  int err = 0;


  // no path?
  if(path == NULL){
    return -ENOENT;
  }

  else {    // making new directory

    // check option -m
    int m = -1;
    if(strchr(options, 'm') != NULL){

      m = 0;

      int i = (strstr(options, "m ") - options) + 3;
      //make sure permission bits are in correct format
      for(int j = 0; j < 3; j++){

        // if permission bits are 0 through 7, set the corresponding m bit
        if(((options[i+j] - '0') == 0 ) || ((options[i+j] - '0') <= 7))   m = m + ((options[i+j] - '0') * pow(8,(2-j)));

        else return -ECOMMAND;
      }
    }


    // if m wasn't set to a certain permission bit, all dirs are 0755
    if(m == -1) m = 0755;


    char *dup = malloc(COM_LENGTH);
    memset(dup, 0, COM_LENGTH);
    if(get_absolute_path(path, dup) == -1){
      free(dup);
      return -ENAMETOOLONG;
    }

    //check permission in parent directory
    char parent_path[PATH_MAX];
    my_dirname(parent_path, dup);
    if(fat_access(parent_path, u_write_perm) == -EACCES){
      free(dup);
      return -EACCES; //if in root, dirname will just return root (assuming root == /)
    }

    err = fat_mkdir(dup, m);

    // if a directory in path doesn't exist
    if (err == -ENOENT){

      // if option -p, then make all the directories as needed. Unless some other error shows up
      if(strchr(options, 'p') != NULL){

        err = i_mkdir(parent_path, options, opt_num);

        // if previous directories were created successfully, let there be directory
        if(err == 0) err = fat_mkdir(dup,m);
      }

      // otherwise, user wrote a stupid command. blame the player not the game.
      else {
        free(dup);
        return -ENOENT;
      }
    }

    //if((v == 1) && (err == 0)) print_helper("mkdir: created directory '%s'\n", path);
    if((strchr(options, 'v') != NULL) && (err == 0)) {
      char *out = malloc(COM_LENGTH);
      memset(out, 0, COM_LENGTH);
      strcat(out, "mkdir: created directory '");

      char filename[NAME_MAX];
      my_basename(filename, dup);

      strcat(out, filename);
      strcat(out, "'\n");
      print_helper(out);
      free(out);
    }
    free(dup);
  }



  // if there are descriptions, switch by case (if statements in c)
  return err;


}







static int i_rmdir(const char *path){

  int err = 0;

  // no path?
  if(path == NULL){
    return -ENOENT;
  }

  else {

    char *dup = malloc(COM_LENGTH);
    memset(dup, 0, COM_LENGTH);
    if(get_absolute_path(path, dup) == -1){
      free(dup);
      return -ENAMETOOLONG;
    }

    //check permission in parent directory
    char parent_path[PATH_MAX];
    my_dirname(parent_path, dup);
    if(fat_access(parent_path, u_write_perm) == -EACCES){
      free(dup);
      return -EACCES; //if in root, dirname will just return root (assuming root == /)
    }

    //execute. or not whatever. throw an error or something
    err = fat_rmdir(dup);
    free(dup);
  }


  return err;

}








static int i_more(const char *path, char *options, int opt_num){

  int err = 0;

  char *dup = malloc(COM_LENGTH);
  memset(dup, 0, COM_LENGTH);
  if(get_absolute_path(path, dup) == -1){
    free(dup);
    return -ENAMETOOLONG;
  }

  // read in.
  char *str = malloc(COM_LENGTH);
  err = fat_read(dup, str, COM_LENGTH, 0);
  if(err < 0){
    free(dup);
    return err;
  }

  err = print_helper(str);
  free(dup);
  return err;
}







static int i_rm(const char *path, char *options, int opt_num){

  char *dup = malloc(COM_LENGTH);
  memset(dup, 0, COM_LENGTH);
  if(get_absolute_path(path, dup) == -1){
    free(dup);
    return -ENAMETOOLONG;
  }

  int err = fat_unlink(path);
  free(dup);
  return err;

}







static int i_cp(const char *paths, char *options, int opt_num){

  //get paths
  char *dup = strdup(paths);
  char *part1 = strtok(dup, " ");
  char *part2 = strtok(NULL, " ");

  if(strcmp(part1, part2) == 0){ // if they're the same, error
    printf("cp: %s and %s are identical (not copied)", part1, part2);
    free(dup);
    return 0;
  }

  char *src = malloc(COM_LENGTH);
  memset(src, 0, COM_LENGTH);
  if(get_absolute_path(part1, src) == -1){
    free(dup);
    free(src);
    return -ENAMETOOLONG;
  }
  char *dest = malloc(COM_LENGTH);
  memset(dest, 0, COM_LENGTH);
  if(get_absolute_path(part2, dest) == -1){
    free(dup);
    free(src);
    free(dest);
    return -ENAMETOOLONG;
  }
  free(dup);

  // check for read access to source
  int err = fat_access(src, u_read_perm);
  if(err != 0){
    throw_error(err, "cp", src);
    free(src);
    free(dest);
    return 0;
  }
  struct stat stbuf;
  err = fat_getattr(src, &stbuf);
  if((err != 0) || ((stbuf.st_mode & S_IFREG) == 0)){
    throw_error(err, "cp", src);
    free(src);
    free(dest);
    return 0;
  }

  // check for dest
  err = fat_access(dest, u_write_perm);
  if(err == -ENOENT){ //if it doesn't exist, try to create file
    err = fat_mknod(dest, u_write_perm | S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if(err != 0){
      throw_error(err, "cp", dest);
      free(src);
      free(dest);
      return 0;
    }
  }
  else if(err < 0){ // if there's another error, throw a tantrum
    throw_error(err, "cp", dest);
    free(src);
    free(dest);
    return 0;
  }
  else{ // the file exists!
    // empty out destination
    err = fat_truncate(dest,0);
    throw_error(err, "cp", dest);
    if(err != 0){
      free(src);
      free(dest);
      return 0;
    }
  }

  // err should be =0 if we reach here. that means, source and destination are ready
  // try to read in a block from src
  char buf[BLOCK_SIZE];
  for(int i = 0; (i < stbuf.st_blocks) || (err == 0); i++){

    memset(buf, 0, BLOCK_SIZE);
    // read in a block from src
    err = fat_read(src, buf, BLOCK_SIZE, BLOCK_SIZE*i);
    if (err < 0) {
      throw_error(err, "cp", src);
      free(src);
      free(dest);
      return 0;
    }

    //if we've reached the last block, only copy the bytes we need to
    if(i == (stbuf.st_blocks-1)) err = fat_write(dest, buf, (stbuf.st_size % BLOCK_SIZE), BLOCK_SIZE * i);
    //else, copy the whole block
    else err = fat_write(dest, buf, BLOCK_SIZE, BLOCK_SIZE * i);

    if (err < 0) {
      throw_error(err, "cp", dest);
      free(src);
      free(dest);
      return 0;
    }
  }

  free(src);
  free(dest);
  return 0;

}









void reorganization_helper(int first_cluster){

  dir_entry current_dir_entries[NUM_DIR_ENTRIES];

  if(fat[first_cluster - 4] == ENDOFFILE){
    memset(current_dir_entries, 0, NUM_DIR_ENTRIES * sizeof(dir_entry));
    pread(fd_disk, &current_dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), first_cluster * BLOCK_SIZE);

    for(int i = 0; i < NUM_DIR_ENTRIES; i++){ // recursive call
      if((current_dir_entries[i].dir_type == 1) && !((strcmp(current_dir_entries[i].name, ".") == 0) || (strcmp(current_dir_entries[i].name, "..") == 0)) ){ //if a subdirectory
        reorganization_helper(current_dir_entries[i].first_cluster);
      }
    }
    return;
  }
  // get list of all the blocks of this directory
  int blocks[FAT_LENGTH];
  int current_cluster = first_cluster;
  int last_cluster = 0;
  // add to list, while incrementing the pointer to the end
  while(current_cluster != (ENDOFFILE + 4)){
    blocks[last_cluster++] = current_cluster;
    current_cluster = fat[current_cluster - 4] + 4;
  }
  last_cluster--; // make it point (index) to the last element in array blocks[]
  current_cluster = 0;

  int spot_emptied = 0;
  dir_entry last_dir_entries[NUM_DIR_ENTRIES];


  int j = 0;
  int i = 0;

  // read in last block
  memset(last_dir_entries, 0, NUM_DIR_ENTRIES * sizeof(dir_entry));
  pread(fd_disk, &last_dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), blocks[last_cluster] * BLOCK_SIZE);

  // read in current block
  memset(current_dir_entries, 0, NUM_DIR_ENTRIES * sizeof(dir_entry));
  pread(fd_disk, &current_dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), blocks[current_cluster] * BLOCK_SIZE);


  // as long as we're not out of blocks to peruse for empty space
  while(current_cluster != last_cluster){

    // if there are any directory entries in the last block, try to see there's an empty spot to move it to in the current block
    while(i < NUM_DIR_ENTRIES){
      if(last_dir_entries[i].dir_type != 0){ // not empty

        // needs to be moved to an earlier block
        for(; j <NUM_DIR_ENTRIES; j++){
          if(current_dir_entries[j].dir_type == 0){ // found an empty spot to put this thing
            memcpy(&current_dir_entries[j], &last_dir_entries[i], sizeof(dir_entry)); // move! move! move!
            memset(&last_dir_entries[i], 0, sizeof(dir_entry));
            spot_emptied = 1; // make sure we know we emptied this dir_entry
            j++; // don't check this one again next time.
            break; // no need to go copy into every free spot in current block
          }
        }

        /////// didn't empty this out. current cluster doesn't have any empty space, check next!////////
        if(spot_emptied == 0){

          //update this current block in disk
          pwrite(fd_disk, &current_dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), blocks[current_cluster] * BLOCK_SIZE);
          // La Nouvelle Vague!. wave ~= current. Thus: new current! QED. (update current block to the new one)
          current_cluster++;


          //check if the two blocks have met, which would mean that we've run out of space
          if(current_cluster == last_cluster){
          //update in disk, all the changes we've made to the currently last block
          pwrite(fd_disk, &last_dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), blocks[last_cluster] * BLOCK_SIZE);
            // recursive call on all the subdirectory, before we get the hell out of here
            for(int k = 0; k <= last_cluster; k++){
              memset(current_dir_entries, 0, NUM_DIR_ENTRIES * sizeof(dir_entry));
              pread(fd_disk, &current_dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), blocks[k] * BLOCK_SIZE);
              for(int l = 0; l < NUM_DIR_ENTRIES; l++){
                if((current_dir_entries[l].dir_type == 1) && !((strcmp(current_dir_entries[l].name, ".") == 0) || (strcmp(current_dir_entries[l].name, "..") == 0)) ){ //if a subdirectory
                  // call on subdirectory
                  reorganization_helper(current_dir_entries[l].first_cluster);
                }
              }

            }
            // and we're done!
            return;
          }

          // if the blocks haven't met, we still have clusters that we need to check for empty spots:
          else{ // read in new cluster, and check again.
            memset(current_dir_entries, 0, NUM_DIR_ENTRIES * sizeof(dir_entry));
            pread(fd_disk, &current_dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), blocks[current_cluster] * BLOCK_SIZE);
            j = 0; // start reading from the beginning of this block
            // don't increment i, because we still need to move and free last_dir_entries[i]
          }
        }

        else{ // emptied this one out!, lets continue!
          i++;
        }

      }
      else{ // this spots already empty. check next
        i++;
      }
      spot_emptied = 0;
    }
    // if we're out of the above while loop, that means we've checked through, moved and freed all the existing dir_entries in the last block.
    // need to free this last block and take a look at the second to last.
    memset(last_dir_entries, 0, NUM_DIR_ENTRIES * sizeof(dir_entry));
    pwrite(fd_disk, &last_dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), blocks[last_cluster] * BLOCK_SIZE);
    fat[(blocks[last_cluster] - 4)] = FREE;
    last_cluster--;
    fat[(blocks[last_cluster] - 4)] = ENDOFFILE;
    pwrite(fd_disk, &fat, BLOCK_SIZE * 3, BLOCK_SIZE);


    // if we've freed the last possible block, we're done.
    if(last_cluster == current_cluster){
      // update current cluster
      pwrite(fd_disk, &current_dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), blocks[current_cluster] * BLOCK_SIZE);

      // recursive call on all the subdirectory, before we get the hell out of here
      for(int k = 0; k <= last_cluster; k++){
        memset(current_dir_entries, 0, NUM_DIR_ENTRIES * sizeof(dir_entry));
        pread(fd_disk, &current_dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), blocks[k] * BLOCK_SIZE);
        for(int l = 0; l < NUM_DIR_ENTRIES; l++){
          if((current_dir_entries[l].dir_type == 1) && !((strcmp(current_dir_entries[l].name, ".") == 0) || (strcmp(current_dir_entries[l].name, "..") == 0)) ){ //if a subdirectory
            // call on subdirectory
            reorganization_helper(current_dir_entries[l].first_cluster);
          }
        }

      }
      // and we're done!
      return;
    }

    // if there are more blocks to try and free
    else{
      pread(fd_disk, &last_dir_entries, NUM_DIR_ENTRIES * sizeof(dir_entry), blocks[last_cluster] * BLOCK_SIZE);
      i = 0; // read from start of this block
    }

  }

}









/* pre:

*/
void i_reorganization(){ // not actually garbage collection. reordering stuff in directory blocks to get more blocks

  // not checking for status of fat table and disk memory before reorganization. Future implementation will, for more judicious use of time
  // using the second method for now: trying to empty out from the last block, till the beginning.

  // go through directories, starting from root.
  reorganization_helper(4);


}








////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////// MAIN MAIN MAIN MAIN //////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



int main(){

  // initialize
  fat_init();

  // tell user how to use. sort of.
  printf("\nFAT Filesystem (c)2020 Jae Surh: \nVirtual simulation of a filesystem. Memory stored on a file called 'fat_disk'.\nSupports Unix commands. Place the path after the command and options, if any.\nTo quit, type 'quit' then press enter.\n\n");



  //prepare for command
  char command[COM_LENGTH];

  int err = 0;

  printf("%s user: ", workdir);

  // continuously get commands, until quit.
  memset(command, 0, COM_LENGTH);
  fgets(command, COM_LENGTH, stdin);
  while (strcmp(command, "quit\n") != 0) {

    if(command[COM_LENGTH-1] != 0){ //if we've got a command too large
      // so actually, the max sized command you can send is COM_LENGTH-1, even including \n;
      printf("-bash: command too long");
    }
    else{
      // get rid of \n at the end of the string
      command[(strlen(command)-1)] = 0;



      //see if there's a non-standard outputs
      char *str = NULL;
      int output_err = 0;

      if(strchr(command, '>') != NULL){
        //check if there is something after '>'
        str = strtok((strchr(command, '>') + 1), " ");
        if(str == NULL){ // if there is nothing after >, output error
          output_err = -ECOMMAND;
        }
        else{ // if possible and neeeded, create and truncate.
          output_err = output_helper(str, command);
        }

      }

      if(strlen(command) == 0){}
      else{
        // get rid of any trailing spaces
        while(command[(strlen(command) - 1)] == ' '){
          command[(strlen(command) - 1)] = 0;
        }
      }

      if(output_err != 0){
        throw_error(output_err, ">", str);
      }
      else if(strlen(command) == 0){ // output_helper hath smited thine command.
        // the best code is no code.
      }

      // if there is no output error
      else if(strchr(command, ' ') == NULL){    // if command has no path or description

        if(strcmp(command,"ls") == 0){
          err = i_ls(NULL, NULL, 0);
          throw_error(err,"ls",NULL);
        }
        else if(strcmp(command,"cd") == 0){
          err = i_cd(NULL, NULL, 0);
          throw_error(err,"cd",NULL);
        }
        else if(strcmp(command,"mkdir") == 0){ //should there be an error inherent? yes.
          throw_error(-ECOMMAND, "mkdir", NULL);
        }
        else if(strcmp(command,"rmdir") == 0){
          throw_error(-ECOMMAND, "rmdir", NULL);
        }
        else if(strcmp(command,"rm") == 0){
          throw_error(-ECOMMAND, "rm", NULL);
        }
        else if(strcmp(command, "echo") == 0){
          err = print_helper("\n");
          throw_error(err,">",output); //because, if any error, it will be because of output
        }
        else if(strcmp(command, "cp") == 0){
          throw_error(-ECOMMAND, "cp", NULL);
        }

        else if(strcmp(command, ">") == 0){
          throw_error(-ECOMMAND, ">", NULL);
        }

        else if(strcmp(command, "reorganize") == 0){
          if(user == 0) printf("You don't have root permissions\n");
          else i_reorganization();
        }

        else if(strcmp(command, "sudo") == 0){
          if(user == 1) printf("You already have root permissions\n");
          else{
            user = 1; // wow. you so powerful huh.
            u_write_perm = S_IWUSR;
            u_read_perm = S_IRUSR;
            printf("You now have root permissions. Type 'logout' and press enter to return to having regular user permissions.\n");
          }
        }
        else if(strcmp(command, "logout") == 0){
          if(user == 0) printf("Can't logout beyond regular user permissions.\n"); //already a pleb. pleb.
          else{
            user = 0; // lowly peasant like the rest of us
            u_write_perm = W_OK;
            u_read_perm = R_OK;
            printf("You now have regular user permissions. Type 'sudo' and press enter to return to having root permissions.\n");
          }
        }

        else if(strlen(command) == 0){
          // do you wanna build a snow man-
          // okay bye
        }
        else{
          printf("-bash: %s: command not recognized\n", command);
        }
      }




      //no output error and command has options or paths
      else{
        char *paths = malloc(COM_LENGTH);
        char *options = malloc(COM_LENGTH);
        char com[COM_LENGTH];
        char mand[COM_LENGTH];

        //split command into instruction and rest
        memset(mand,0,COM_LENGTH);
        strcpy(mand,strchr(command, ' ')+1);

        int index = strchr(command, ' ') - command;
        memset(com,0,COM_LENGTH);
        strncpy(com,command,index);


        // prepare to read in options and paths
        memset(paths, 0, COM_LENGTH);
        memset(options, 0, COM_LENGTH);

        int path_num = 0;
        // stop, drop, and read
        int opt_num = 0;
        opt_num = option_helper(com,mand,paths,options,&path_num);

        //char *path = malloc(COM_LENGTH);
        //memset(path, 0, COM_LENGTH);
        char *path;

        if(strcmp(com,"ls") == 0){
          // if command is in incorrect form
          if(opt_num == -1) throw_error(-ECOMMAND, "ls", NULL);

          else if(path_num == 0){
            // if there is no path
            err = i_ls(NULL, options, opt_num);
            throw_error(err, "ls", NULL);
          }
          else if(path_num == 1){
            err = i_ls(paths, options, opt_num);
            throw_error(err, "ls", paths);
          }
          else{ //otherwise
            // for all the paths there are
            path = strsep(&paths," "); // using strsep, because if strtok is called by any functions in between, it will get screwy
            while(path){
              print_helper(path);
              print_helper(":\n");
              err = i_ls(path, options, opt_num);
              throw_error(err, "ls", path);
              path = strsep(&paths, " ");
              if(path) print_helper("\n");
            }
          }
        }


        else if(strcmp(com,"cd") == 0){
          // if command is in incorrect form
          if(opt_num == -1) throw_error(err, "ls", NULL);

          else if(path_num == 0){
            // if there is no path
            err = i_cd(NULL, options, opt_num);
            throw_error(err, "ls", NULL);
          }
          else{
            // otherwise
            // just for the first path
            path = strtok(paths, " ");
            err = i_cd(path, options, opt_num);
            throw_error(err, "cd", path);
          }
        }


        else if(strcmp(com,"more") == 0){
          // if command is in incorrect form
          if((opt_num == -1) || (path_num == 0)) throw_error(err, "more", NULL);

          else if(path_num == 1){
            err = i_more(paths, options, opt_num);
            throw_error(err, "more", paths);
          }
          else{
            // for all the paths there are
            path = strsep(&paths, " ");
            while(path){
              err = i_more(path, options, opt_num);
              throw_error(err, "more", path);
              path = strsep(&paths, " ");
            }
          }
        }


        else if(strcmp(com,"less") == 0){

        }


        else if(strcmp(com,"mkdir") == 0){
          // if command is in incorrect form
          if((opt_num == -1) || (path_num == 0)) throw_error(-ECOMMAND, "mkdir", NULL);

          else if(path_num == 1){
            err = i_mkdir(paths, options, opt_num);
            throw_error(err, "mkdir", paths);
          }
          else{
            // for all the paths there are
            path = strsep(&paths, " ");
            while(path){
              err = i_mkdir(path, options, opt_num);
              throw_error(err, "mkdir", path);
              path = strsep(&paths, " ");
            }
          }
        }

        else if(strcmp(com,"rmdir") == 0){
          // if command is in incorrect form
          if((opt_num == -1) || (path_num == 0)) throw_error(-ECOMMAND, "rmdir", NULL);

          else{
            // for all the paths there are
            path = strsep(&paths, " ");
            while(path){
              err = i_rmdir(path);

              //if option '-p' and we got rid of the path without error
              if((opt_num != 0) && (err == 0)){

                // get path of parent directory
                char *dup = malloc(COM_LENGTH);
                my_dirname(dup, path);

                // as long as we have a parent
                while((strcmp(dup, "/") != 0) && (strcmp(dup, ".") != 0)){
                  err = i_rmdir(dup);

                  // if we get an error, exit
                  if(err != 0) {
                    throw_error(err, "rmdir", dup);
                    break;
                  }
                  // otherwise, continue with parent
                  else{
                    char temp[PATH_MAX];
                    my_dirname(temp, dup);
                    // check if parent is root
                    if(strcmp(dup, temp) == 0) strcpy(dup, "/");
                    // if parent isn't root,
                    else{
                      memset(dup, 0, COM_LENGTH);
                      strcpy(dup, temp);
                    }
                  }
                }

              }
              // if we couldn't get rid of the current directory
              else throw_error(err, "rmdir", path);
              path = strsep(&paths, " ");
            }
          }
        }

        else if(strcmp(com, "rm") == 0){
          // if command is in incorrect form
          if((opt_num == -1) || (path_num == 0)) throw_error(-ECOMMAND, "rm", NULL);

          else if(path_num == 1){
            err = i_rm(paths, options, opt_num);
            throw_error(err, "rm", paths);
          }
          else{
            // for all the paths there are
            path = strsep(&paths, " ");
            while(path){
              err = i_rm(path, options, opt_num);
              throw_error(err, "rm", path);
              path = strsep(&paths, " ");
            }
          }
        }

        else if(strcmp(com,"echo") == 0){
          //no incorrect form for echo. what an incredible command

          //print out
          if(path_num == 1) err = print_helper(paths); // print if there is something to print
          throw_error(err, ">", output); //because with a standard output, there will be no errors
          if(opt_num == 0) err = print_helper("\n"); // print newline if no option
          throw_error(err, ">", output);
        }

        else if(strcmp(com, "cp") == 0){
          // if command is in incorrect form
          if((opt_num == -1) || (path_num == 0)) throw_error(-ECOMMAND, "cp", NULL);
          else i_cp(paths, options, opt_num);
        }

        else {
          if(strcmp(command, "print fat") == 0){
            printf("fat:\n");
            for(int i = 0; i < FAT_LENGTH; i ++){
              printf("%d:[%d]\t", i, fat[i]);
            }
            printf("\n");
          }
          else printf("-bash: %s: Command not found.\n", com);
        }

        //free(path);
        free(paths);
        free(options);
      }

    }

    err = 0;
    memset(output, 0, sizeof(output));
    output_offset = 0;


    if(user == 0) printf("%s user: ", workdir);
    if(user == 1) printf("%s root: ", workdir);

    // next command. bring it on!
    memset(command, 0, COM_LENGTH);
    fgets(command, COM_LENGTH, stdin);

  }

  // The sweet release of simulated file system death. Only, the experience is saved into a "persistant" storage
  printf("Goodbye.\n");
}
