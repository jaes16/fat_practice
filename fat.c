// fat file system using fuse
// 2019 (c) storageWarriors.inc

#define FUSE_USE_VERSION 26

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500
#endif

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif


/////////////////////////////////////////////////////// SIZES ///////////////////////////////////////////////////////

#define DISK_SIZE 10485759
#define BLOCK_SIZE 4096
#define CLUSTER_TOTALNUM 2560
#define ENDOFFILE 3000
#define FREE 4000
#define PATH_MAX 4096 //unix definition.
/////////////////////////////////////////////////////// DEFINITION OF DATA STRUCTURES  ////////////////////////////////////////////////////////
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

typedef struct
{
  int first_cluster;
  int file_size; // in bytes
  int dir_type; // 0 for empty, 1 for directory, 2 for file
  int read_only; // 0 for read and write, 1 for read only
  char name[32];
}dir_entry;


/////////////////////////////////////////////////////// GLOBAL VARIABLES ////////////////////////////////////////////////////////

char disk_path[1024];
FILE *disk;
int fd_disk;

//free_node *free_list;

int fat[3072];

int last_free = 0;


/////////////////////////////////////////////////////// CODE ///////////////////////////////////////////////////////


// When theres no disk, create generic disk
void create_init_disk()
{
  // write out superblock
  fd_disk = fileno(disk);
  pwrite(fd_disk,&superblock,BLOCK_SIZE,0);

  // write out fat
  memset(&fat,0,sizeof(fat));
  for(int i = 0; i < 2556; i++){
    // fill buffers with value for free

    fat[i] = FREE;
  }
  //make sure root dir cluster is allocated
  fat[0] = ENDOFFILE;

  pwrite(fd_disk,fat,(sizeof(int)*3072),BLOCK_SIZE);


  // write out root dir
  dir_entry dir_entries[4096]; //rather arbitrary size, but point is its bigger than block size
  memset(dir_entries, 0, sizeof(dir_entries));
  //creating .
  dir_entries[0].first_cluster = 4;
  dir_entries[0].file_size = 2;
  dir_entries[0].dir_type = 1;
  memset(dir_entries[0].name,0,32);
  strcpy(dir_entries[0].name,".");
  // creating ..
  dir_entries[1].first_cluster = 4;
  dir_entries[1].file_size = 2;
  dir_entries[1].dir_type = 1;
  memset(dir_entries[1].name,0,32);
  strcpy(dir_entries[1].name,"..");
  pwrite(fd_disk, &dir_entries, 4096, (4*4096));

}


static void* fat_init(struct fuse_conn_info *conn)
{
  // find where "disk" file should be
  memset(disk_path, '\0', sizeof(disk_path));
  strcpy(disk_path, (char *) get_current_dir_name());
  strcat(disk_path, "/fat_disk");

  // see if "disk" file is initialized
  if((disk = fopen(disk_path, "r+"))){
    // if it is, read in superblock
    fd_disk = fileno(disk);
    pread(fd_disk,&superblock,sizeof(superblock),0);

    pread(fd_disk,&fat, sizeof(fat),BLOCK_SIZE);
    // create free list
    // create_free_list();

  }else{
    // if it isn't, create file and write out initial superblock
    disk = fopen(disk_path, "w+");
    fseek(disk, DISK_SIZE, SEEK_SET);
    fputc('\0', disk);

    // initial locations for fat and data
    superblock.s.fat_location = 1;
    superblock.s.fat_size = 3;
    superblock.s.data_location = 4;
    superblock.s.data_size = 2556;
    // superblock.s.freelist_location = 5;

    // write out initialized disk
    create_init_disk();
    // read in freelist
    //    create_free_list();


  }


  return NULL;
}

static void destroy(const void *privatedata)
{
  fclose(disk);
}

/*
static char* helpermethod(char *path){
  char ret[1024];
  memset(&ret,0,1024);
  int i;
  int len = strlen(path);
  for(i =0; i <= len; i++){
    if(i == len){
      path+(len-1);
      return ret;
    }
    if(path[i] == '/'){
      break;
    }
    ret[i] = path[i];
  }

  if(i == strlen(path)){
    return ret;
  }
  path = path+i+1;

  return ret;

  }*/

// returns the first directory in the current path
int helper(char *path, char *ret){
  memset(ret,0,32);
  char *ptr = strchr(path, '/');
  if(ptr) {
    int index = ptr - path;
    if (index > 32) return -2;

    strncpy(ret,path,index);
    return index+1;
  }else{ return -1;}
  return 0;
}

// return 0 if path exists, -1 if path doesn't exist, -2 if path name is too long, -3 if component of path is file
// later, can remove last and just find it from path in this function
static int dir_exists(char *path, int cluster_num, dir_entry *de, char *last)
{

  // bring in all the dir entries
  dir_entry dir_entries[4096];//[((4096/sizeof(dir_entries)) + 1)];
  memset(dir_entries, 0, sizeof(dir_entries));
  pread(fd_disk, &dir_entries, 4096, (4096*cluster_num));


  // name of the next directory in path to look for
  //char* token = strtok(path, "/");
  char token[32];
  int index;
  // helper returns wrong result when path = token, so
  if((strcmp(path,last) != 0) && (strlen(path) != strlen(last))){
    index = helper(path,token);
    // if name is too long
    if(index == -2) return -2;
    // should never return -1, but...
    if(index == -1) return -1;
  } else {

    strcpy(token,path);
  }

  int isFile = 0;
  //search through dir_entries in current directory to see if the token is there
  for (int i = 0; i < 4096; i++){
    if(dir_entries[i].dir_type !=0){
      if(strcmp(dir_entries[i].name, token) == 0){ //if we find a match
	if ((strcmp(token,last) ==0) && (strlen(path) == strlen(last))){ //if last component
	  strcpy(de->name,dir_entries[i].name);
	  de->first_cluster = dir_entries[i].first_cluster;
	  de->file_size = dir_entries[i].file_size;
	  de->dir_type = dir_entries[i].dir_type;
	  return 0;
	}else{
	  if (dir_entries[i].dir_type == 0) isFile = 1; //if match is file
	  else{ //if match is dir
	    char path2[strlen(path)];
	    for (int j = 0; j < strlen(path); j++){
	      path2[j] = path[j+index];
	    }
	    return dir_exists(path2, dir_entries[i].first_cluster, de, last);
	  }
	}
      }
    }
  }
  if(isFile == 1) return -3;
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

  // see if name is too long
  if((strlen(basename(path))>32) || (strlen(path) > PATH_MAX)){
    return -ENAMETOOLONG;
  }

  // if root
  if (strcmp(path, "/") == 0) {
    // get directory size, and the rest is the usual
    dir_entry dir_entries[4096];//[((4096/sizeof(dir_entries)) + 1)];
    memset(dir_entries, 0, sizeof(dir_entries));
    pread(fd_disk, &dir_entries, 4096, (4096*4));

    stbuf -> st_mode = S_IFDIR | 0755;
    stbuf -> st_nlink = dir_entries[0].file_size;
    stbuf -> st_size = 4096;
    stbuf -> st_blocks = 1;
    return res;
  } else {

    // prepare for dir_exists call
    char path2[PATH_MAX];
    strcpy(path2,path);
    if(path2[0] == '/'){
      // remove root
      memmove(path2, path2+1, sizeof(path2)-1); //same fix as in getattr
    }
    // get name of dir we want
    char *dup = strdup(path2);
    char *last;
    last = basename(dup);

    int cluster_num = 4;
    dir_entry de;

    //if path doesn't exist or name is too long, return error
    int call_rslt = dir_exists(path2,cluster_num,&de, last);
    if (call_rslt == -1) return -ENOENT;
    if (call_rslt == -2) return -ENAMETOOLONG;
    if (call_rslt == -3) return -ENOTDIR;
    else if (de.dir_type == 1) { //if path does exist, return correct attributes
      stbuf -> st_mode = S_IFDIR | 0755;
      stbuf -> st_nlink = de.file_size;
      stbuf -> st_size = 4096;
      stbuf -> st_blocks = 1;
    }else{
      // check permissions
      if(de.read_only == 1) stbuf -> st_mode = S_IFREG | 0444;
      else stbuf->st_mode = S_IFREG | 0666;
      stbuf -> st_nlink = 1;
      stbuf -> st_size = de.file_size; // by multiple of blocks
      int b = (de.file_size/4096);
      if((de.file_size%4096) != 0) b++;
      stbuf -> st_blocks = b;
    }
  }
  return res;
}



static int fat_access(const char *path, int mask)
{

  // see if name is too long
  if((strlen(basename(path))>32) || (strlen(path) > PATH_MAX)){
    return -ENAMETOOLONG;
  }
  

  // i am groot
  if (strcmp(path, "/") == 0) {
    return 0;
  } else {

    // prepare for dir_exists call
    char path2[PATH_MAX];
    strcpy(path2,path);
    if(path2[0] == '/'){
      // path2++;
      memmove(path2, path2+1, sizeof(path2)-1); //same fix as in getattr
    }

    // get name of dir we want
    char *dup = strdup(path2);
    char *last;
    last = basename(dup);

    int cluster_num = 4;
    dir_entry de;

    // if path doesn't exist, return error
    int call_rslt = dir_exists(path2,cluster_num,&de, last);
    if (call_rslt == -1) return -ENOENT;
    if (call_rslt == -2) return -ENAMETOOLONG;
    if (call_rslt == -3) return -ENOTDIR;
    else if (de.dir_type == 1){ // if path does exist, check for permissions
      // but all directories are 0755 so...
      return 0;
    }else{
      if((mask & W_OK) && (de.read_only == 1)){
	return -EACCES;
      }
      return 0;
    }
  }

}


static int fat_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
  // see if name is too long
  if((strlen(basename(path))>32) || (strlen(path) > PATH_MAX)){
    return -ENAMETOOLONG;
  }

  // get respective cluster number (4 for groot)
  int cluster_num = 4;

  if(strcmp(path, "/") != 0){
    // prepare for dir_exists call
    char path2[PATH_MAX];
    strcpy(path2,path);
    if(path2[0] == '/'){
      // path2++;
      memmove(path2, path2+1, sizeof(path2)-1); //same fix as in getattr
    }

    // get name of dir we want
    char *dup = strdup(path2);
    char *last;
    last = basename(dup);

    dir_entry de;

    int call_rslt = dir_exists(path2,cluster_num,&de, last);
    if (call_rslt == -1) return -ENOENT;
    if (call_rslt == -2) return -ENAMETOOLONG;
    if (call_rslt == -3) return -ENOTDIR;
    cluster_num = de.first_cluster;
  }


  dir_entry dir_entries[4096];
  memset(dir_entries, 0, sizeof(dir_entries));
  pread(fd_disk, &dir_entries, 4096, (4096*cluster_num));

  //  filler(buf, ".", NULL, 0);
  //filler(buf, "..", NULL, 0);
  //search through to add all dir_entries in directory
  for (int i = offset; i < 4096; i++){
    if(dir_entries[i].dir_type !=0){
      if (filler(buf, dir_entries[i].name, NULL, i + 1)) return 0;
    }
  }

  return 0;
}



static int fat_mkdir(const char* path, mode_t mode)
{

  // separate path and name of last dir
  char *dup1 = strdup(path);
  char *dup2 = strdup(path);
  char *parent_path = dirname(dup1);
  char *dir_name = basename(dup2);


  // can't make root
  if(strcmp(path,"/") == 0) return -ENOENT;
  
  // see if name is too long
  if((strlen(basename(path))>32) || (strlen(path) > PATH_MAX)){
    return -ENAMETOOLONG;
  }
  
  dir_entry parent_de;
  dir_entry grandparent_de;



  char path2[PATH_MAX];
  strcpy(path2,path);
  if(path2[0] == '/'){
    // path2++;
    memmove(path2, path2+1, sizeof(path2)-1); //same fix as in getattr
  }
  // check if directory already exists (using parent_de, but not looking for parent directory entry, just for convenience)
  int call_rslt = dir_exists(path2, 4, &parent_de, dir_name);
  if (call_rslt == -2) return -ENAMETOOLONG;
  if (call_rslt == 0) return -EEXIST;
  if (call_rslt == -3) return -ENOTDIR;
  //  memset(&parent_de,0,sizeof(parent_de));


  dir_entry dir_entries[4096];//[((4096/sizeof(dir_entries)) + 1)];
  memset(dir_entries, 0, sizeof(dir_entries));

  // check if path leading to directory doesn't exist
  if(strcmp(parent_path,"/") == 0) { // i am groot
    //get appropriate directory entry for root
    pread(fd_disk, &dir_entries, 4096, (4096*4));
    parent_de.first_cluster = 4;
    parent_de.file_size = dir_entries[0].file_size;
    strcpy(parent_de.name,".");
    parent_de.dir_type = 1;
  } else { //if not root,
    // prepare for dir_exists call

    strcpy(path2,parent_path);
    if(path2[0] == '/'){
      // path2++;
      memmove(path2, path2+1, sizeof(path2)-1); //same fix as in getattr
    }

    // get name of dir we want
    char *dup = strdup(path2);
    char *last;
    last = basename(dup);

    int call_rslt = dir_exists(path2, 4, &parent_de, last);
    if (call_rslt == -1) return -ENOENT; //doesn't exist

    // if parent isn't groot, we know it has a grandparent
    // all we need is the cluster number, so get it from .. of parent
    pread(fd_disk,&dir_entries, 4096, (4096*parent_de.first_cluster));
    grandparent_de.first_cluster = dir_entries[1].first_cluster;
  }
  // check if there is space for more directory entries
  int max = (4096/ sizeof(dir_entry));
  if (parent_de.file_size >= max) return -ENOSPC;






  for(int i= 0; i <2556; i++){
    // if we find an empty cluster
    if(fat[i] == 4000){
      // set cluster to endoffile and write out
      fat[i] = 3000;
      pwrite(fd_disk,&fat, (4096*3), 4096);


      // FOR NEW DIR ENTRY IN NEW CLUSTER
      // creating new dir entry
      memset(dir_entries,0,sizeof(dir_entries));

      dir_entry de;
      de.first_cluster = i+4;
      de.file_size = 2;
      de.dir_type = 1;
      memset(de.name,0,sizeof(de.name));
      strcpy(de.name,".");
      dir_entries[0] = de;

      dir_entries[1].first_cluster = parent_de.first_cluster;
      dir_entries[1].file_size = ((parent_de.file_size)+1);
      dir_entries[1].dir_type = 1;
      strcpy(dir_entries[1].name,"..");

      //write out new directory cluster
      pwrite(fd_disk,&dir_entries, 4096, (4096*(i+4)));



      // FOR CHANGING PARENT CLUSTER
      // read in parent cluster
      pread(fd_disk,&dir_entries, 4096, (4096*parent_de.first_cluster));

      // change parent cluster
      strcpy(de.name,dir_name);
      for (int j = 0; j < max; j++){
	if(dir_entries[j].dir_type == 0){
	  dir_entries[j] = de;
	  break;
	}
      }
      // update size in .
      dir_entries[0].file_size++;

      // check if parent cluster is root
      if(parent_de.first_cluster == 4){
	// then also have to update ..
	dir_entries[1].file_size++;
      }
      // write out parent cluster
      pwrite(fd_disk,&dir_entries, 4096, (4096*parent_de.first_cluster));



      // POSSIBLY CHANGING GRANDPARENT CLUSTER
      // if there is a grandparent
      if(strcmp(parent_path,"/") != 0){
	// read in grandparent cluster
	pread(fd_disk,&dir_entries, 4096, (4096*grandparent_de.first_cluster));
	// change parent dir entry in gp cluster
	for (int j = 0; j < 4096; j++){
	  //if valid dir entry
	  if(dir_entries[j].dir_type != 0){
	    // check if is the parent dir entry
	    if(strcmp(dir_entries[j].name,parent_de.name) == 0){
	      dir_entries[j].file_size++;
	    }
	  }
	}
	pwrite(fd_disk,&dir_entries,4096,(4096*grandparent_de.first_cluster));
      }


      return 0;
    }
  }

  return -ENOSPC;
}


static int fat_rmdir(const char* path)
{

  // separate path and name of last dir
  char *dup1 = strdup(path);
  char *dup2 = strdup(path);
  char *parent_path = dirname(dup1);
  char *dir_name = basename(dup2);


  // can't make root
  if(strcmp(path,"/") == 0) return -ENOENT;

  // see if name is too long
  if((strlen(basename(path))>32) || (strlen(path) > PATH_MAX)){
    return -ENAMETOOLONG;
  }

  dir_entry de;
  dir_entry parent_de;
  dir_entry grandparent_de;



  char path2[PATH_MAX];
  strcpy(path2,path);
  if(path2[0] == '/'){
    // path2++;
    memmove(path2, path2+1, sizeof(path2)-1); //same fix as in getattr
  }
  // check if directory already exists (using parent_de, but not looking for parent directory entry, just for convenience)
  int call_rslt = dir_exists(path2, 4, &de, dir_name);
  if (call_rslt == -2) return -ENAMETOOLONG;
  if (call_rslt == -1) return -ENOENT;
  if (call_rslt == -3) return -ENOTDIR;
  if (de.dir_type == 2) return -ENOTDIR;



  dir_entry dir_entries[4096];//[((4096/sizeof(dir_entries)) + 1)];
  memset(dir_entries, 0, sizeof(dir_entries));

  // check if path leading to directory doesn't exist
  if(strcmp(parent_path,"/") == 0) { // i am groot
    //get appropriate directory entry for root
    pread(fd_disk, &dir_entries, 4096, (4096*4));
    parent_de.first_cluster = 4;
    parent_de.file_size = dir_entries[0].file_size;
    strcpy(parent_de.name,".");
    parent_de.dir_type = 1;
  } else { //if not root,
    // prepare for dir_exists call

    strcpy(path2,parent_path);
    if(path2[0] == '/'){
      // path2++;
      memmove(path2, path2+1, sizeof(path2)-1); //same fix as in getattr
    }

    // get name of dir we want
    char *dup = strdup(path2);
    char *last;
    last = basename(dup);

    int call_rslt = dir_exists(path2, 4, &parent_de, last);

    // if parent isn't groot, we know it has a grandparent
    // all we need is the cluster number, so get it from .. of parent
    pread(fd_disk,&dir_entries, 4096, (4096*parent_de.first_cluster));
    grandparent_de.first_cluster = dir_entries[1].first_cluster;
  }

  // check if directory is empty
  pread(fd_disk, &dir_entries, 4096, (4096*de.first_cluster));
  for (int i = 2; i < 4096; i++){
    if (dir_entries[i].dir_type != 0){
      return -ENOTEMPTY;
    }
  }
  // 0 out the dir entry in parent
  pread(fd_disk, &dir_entries, 4096, (4096*parent_de.first_cluster));
  dir_entries[0].file_size--;

  for (int i = 2; i < 4096; i++){
    if (strcmp(dir_entries[i].name, de.name) == 0){
      memset(&dir_entries[i], 0, sizeof(dir_entry));
      break;
    }
  }
  if (strcmp(parent_de.name, "/") == 0){
    dir_entries[1].file_size--;
  }
  pwrite(fd_disk, &dir_entries, 4096, (4096*parent_de.first_cluster));

  // 0 out the dir cluster
  memset(&dir_entries, 0, 4096);
  pwrite(fd_disk, &dir_entries, 4096, (4096*de.first_cluster));


  // set fat entry as free
  fat[de.first_cluster - 4] = 4000;
  pwrite(fd_disk, &fat, 3*4096, 4096);


  // decrease file size of parent in grandparent
  if (parent_de.first_cluster != 4){
    pread(fd_disk, &dir_entries, 4096, (4096*grandparent_de.first_cluster));
    for (int i = 2; i < 4096; i++){
      if (strcmp(dir_entries[i].name, parent_de.name) == 0){
	dir_entries[i].file_size--;
	break;
      }
    }
    pwrite(fd_disk, &dir_entries, 4096, (4096*grandparent_de.first_cluster));
  }
  return 0;
}


static int fat_fgetattr(const char* path, struct stat* stbuf, struct fuse_file_info *fi){
  return fat_getattr(path, stbuf);
}


static int fat_open(const char* path, struct fuse_file_info* fi){

  // separate path and name of last dir
  char *dup1 = strdup(path);
  char *dup2 = strdup(path);
  char *parent_path = dirname(dup1);
  char *file_name = basename(dup2);
  dir_entry de;

  // can't make root
  if(strcmp(path,"/") == 0) return -ENOENT;
  // see if name is too long
  if((strlen(basename(path))>32) || (strlen(path) > PATH_MAX)){
    return -ENAMETOOLONG;
  }
  
  char path2[PATH_MAX];
  strcpy(path2,path);
  if(path2[0] == '/'){
    // path2++;
    memmove(path2, path2+1, sizeof(path2)-1); //same fix as in getattr
  }

  int call_rslt = dir_exists(path2, 4, &de, file_name);
  if (call_rslt == -2) return -ENAMETOOLONG;
  if (call_rslt == -1) return -ENOENT;
  if (call_rslt == -3) return -ENOTDIR;
  return 0;
}


static int fat_mknod(const char* path, mode_t mode, dev_t rdev)
{
  //check mode
  if(S_ISREG(mode)){

    // separate path and name of last dir
    char *dup1 = strdup(path);
    char *dup2 = strdup(path);
    char *parent_path = dirname(dup1);
    char *file_name = basename(dup2);


    // can't make root
    if(strcmp(path,"/") == 0) return -ENOENT;
    // see if name is too long
    if((strlen(basename(strdup(path)))>32) || (strlen(path) > PATH_MAX)){
      return -ENAMETOOLONG;
    }    

    dir_entry parent_de;
    dir_entry grandparent_de;



    char path2[PATH_MAX];
    strcpy(path2,path);
    if(path2[0] == '/'){
      // path2++;
      memmove(path2, path2+1, sizeof(path2)-1); //same fix as in getattr
    }
    // check if directory already exists (using parent_de, but not looking for parent directory entry, just for convenience)
    int call_rslt = dir_exists(path2, 4, &parent_de, file_name);
    if (call_rslt == -2) return -ENAMETOOLONG;
    if (call_rslt == 0) return -EEXIST;
    if (call_rslt == -3) return -ENOTDIR;
    //  memset(&parent_de,0,sizeof(parent_de));


    dir_entry dir_entries[4096];//[((4096/sizeof(dir_entries)) + 1)];
    memset(dir_entries, 0, sizeof(dir_entries));

    // check if path leading to directory doesn't exist
    if(strcmp(parent_path,"/") == 0) { // i am groot
      //get appropriate directory entry for root
      pread(fd_disk, &dir_entries, 4096, (4096*4));
      parent_de.first_cluster = 4;
      parent_de.file_size = dir_entries[0].file_size;
      strcpy(parent_de.name,".");
      parent_de.dir_type = 1;
    } else { //if not root,
      // prepare for dir_exists call

      strcpy(path2,parent_path);
      if(path2[0] == '/'){
	// path2++;
	memmove(path2, path2+1, sizeof(path2)-1); //same fix as in getattr
      }

      // get name of dir we want
      char *dup = strdup(path2);
      char *last;
      last = basename(dup);

      int call_rslt = dir_exists(path2, 4, &parent_de, last);
      if (call_rslt == -1) return -ENOENT; //doesn't exist

      // if parent isn't groot, we know it has a grandparent
      // all we need is the cluster number, so get it from .. of parent
      pread(fd_disk,&dir_entries, 4096, (4096*parent_de.first_cluster));
      grandparent_de.first_cluster = dir_entries[1].first_cluster;
    }
    // check if there is space for more directory entries
    int max = (4096/ sizeof(dir_entry));
    if (parent_de.file_size >= max) return -ENOSPC;


    for(int i= 0; i <2556; i++){
      // if we find an empty cluster
      if(fat[i] == 4000){
	// set cluster to endoffile and write out
	fat[i] = 3000;
	pwrite(fd_disk,&fat, (4096*3), 4096);


	// FOR NEW DIR ENTRY IN NEW CLUSTER
	// creating new dir entry

	dir_entry de;
	de.first_cluster = i+4;
	de.file_size = 0;
	de.dir_type = 2;


	//check mode to get permissions
	if (mode == (S_IFREG | 0444)) de.read_only = 1;
	else de.read_only = 0;
	memset(de.name,0,sizeof(de.name));
	strcpy(de.name,file_name);


	// FOR CHANGING PARENT CLUSTER
	// read in parent cluster
	pread(fd_disk,&dir_entries, 4096, (4096*parent_de.first_cluster));

	// change parent cluster
	for (int j = 0; j < max; j++){
	  if(dir_entries[j].dir_type == 0){
	    dir_entries[j] = de;
	    break;
	  }
	}
	// update size in .
	dir_entries[0].file_size++;

	// check if parent cluster is root
	if(parent_de.first_cluster == 4){
	  // then also have to update ..
	  dir_entries[1].file_size++;
	}
	// write out parent cluster
	pwrite(fd_disk,&dir_entries, 4096, (4096*parent_de.first_cluster));

	// POSSIBLY CHANGING GRANDPARENT CLUSTER
	// if there is a grandparent
	if(strcmp(parent_path,"/") != 0){
	  // read in grandparent cluster
	  pread(fd_disk,&dir_entries, 4096, (4096*grandparent_de.first_cluster));
	  // change parent dir entry in gp cluster
	  for (int j = 0; j < 4096; j++){
	    //if valid dir entry
	    if(dir_entries[j].dir_type != 0){
	      // check if is the parent dir entry
	      if(strcmp(dir_entries[j].name,parent_de.name) == 0){
		dir_entries[j].file_size++;
	      }
	    }
	  }
	  pwrite(fd_disk,&dir_entries,4096,(4096*grandparent_de.first_cluster));
	}
	return 0;
      }
    }
    return -ENOSPC;
  }
  return -EACCES;
}


static int fat_unlink(const char* path)
{

  // separate path and name of last dir
  char *dup1 = strdup(path);
  char *dup2 = strdup(path);
  char *parent_path = dirname(dup1);
  char *file_name = basename(dup2);


  // can't make root
  if(strcmp(path,"/") == 0) return -ENOENT;
  // see if name is too long
  if((strlen(basename(path))>32) || (strlen(path) > PATH_MAX)){
    return -ENAMETOOLONG;
  }    

  dir_entry de;
  dir_entry parent_de;
  dir_entry grandparent_de;



  char path2[PATH_MAX];
  strcpy(path2,path);
  if(path2[0] == '/'){
    // path2++;
    memmove(path2, path2+1, sizeof(path2)-1); //same fix as in getattr
  }
  // check if directory already exists (using parent_de, but not looking for parent directory entry, just for convenience)
  int call_rslt = dir_exists(path2, 4, &de, file_name);
  if (call_rslt == -2) return -ENAMETOOLONG;
  if (call_rslt == -1) return -ENOENT;
  if (call_rslt == -3) return -ENOTDIR;
  if (de.dir_type == 1) return -ENOENT;

  dir_entry dir_entries[4096];//[((4096/sizeof(dir_entries)) + 1)];
  memset(dir_entries, 0, sizeof(dir_entries));

  // check if path leading to directory doesn't exist
  if(strcmp(parent_path,"/") == 0) { // i am groot
    //get appropriate directory entry for root
    pread(fd_disk, &dir_entries, 4096, (4096*4));
    parent_de.first_cluster = 4;
    parent_de.file_size = dir_entries[0].file_size;
    strcpy(parent_de.name,".");
    parent_de.dir_type = 1;
  } else { //if not root,
    // prepare for dir_exists call

    strcpy(path2,parent_path);
    if(path2[0] == '/'){
      // path2++;
      memmove(path2, path2+1, sizeof(path2)-1); //same fix as in getattr
    }

    // get name of dir we want
    char *dup = strdup(path2);
    char *last;
    last = basename(dup);

    int call_rslt = dir_exists(path2, 4, &parent_de, last);

    // if parent isn't groot, we know it has a grandparent
    // all we need is the cluster number, so get it from .. of parent
    pread(fd_disk,&dir_entries, 4096, (4096*parent_de.first_cluster));
    grandparent_de.first_cluster = dir_entries[1].first_cluster;
  }

  // 0 out the dir entry in parent and decrease file size of parent
  pread(fd_disk, &dir_entries, 4096, (4096*parent_de.first_cluster));
  dir_entries[0].file_size--;

  for (int i = 2; i < 4096; i++){
    if (strcmp(dir_entries[i].name, de.name) == 0){
      memset(&dir_entries[i], 0, sizeof(dir_entry));
      break;
    }
  }
  //if root
  if (strcmp(parent_de.name, "/") == 0){
    dir_entries[1].file_size--;
  }
  pwrite(fd_disk, &dir_entries, 4096, (4096*parent_de.first_cluster));


  // 0 out the clusters of this file
  memset(&dir_entries, 0, 4096);
  int cluster_to_erase = de.first_cluster;
  int temp = 0;
  // while last cluster wasn't EOF (so this is equal to 3000 (+4 for actual cluster placement not fat placement))
  while(cluster_to_erase != 3004){
    // 0 out.
    if(pwrite(fd_disk, &dir_entries, 4096, (4096*cluster_to_erase)) == -1) return -EIO;
    // getting next cluster and setting this fat entry as free
    temp = cluster_to_erase;
    cluster_to_erase = fat[cluster_to_erase-4]+4;
    fat[temp-4] = 4000;
  }

  // set fat entry as free
  pwrite(fd_disk, &fat, 3*4096, 4096);


  // decrease file size of parent in grandparent
  if (parent_de.first_cluster != 4){
    if(pread(fd_disk, &dir_entries, 4096, (4096*grandparent_de.first_cluster)) == -1) return -EIO;
    for (int i = 2; i < 4096; i++){
      if (strcmp(dir_entries[i].name, parent_de.name) == 0){
	dir_entries[i].file_size--;
	break;
      }
    }
    pwrite(fd_disk, &dir_entries, 4096, (4096*grandparent_de.first_cluster));
  }
  return 0;
}

static int fat_statfs(const char* path, struct statvfs* buf){
  buf -> f_namemax = 32;
  buf -> f_frsize = 4096;
  buf -> f_bsize = 4096;
  int free_count = 0;
  for (int i = 0; i < 2556; i++){
    if (fat[i] == 4000) free_count++;
  }
  buf -> f_bfree = free_count;
  buf -> f_bavail = free_count;
  buf -> f_blocks = 2560;
  return 0;
}

static int fat_create(const char* path, mode_t mode)
{
  dev_t rdev;
  return fat_mknod(path, mode | S_IFREG, rdev);
}

static int fat_release(const char* path, struct fuse_file_info *fi)
{
  return 0;
}



// returns -1 for io error, 
static int read_helper(char *buf, size_t size, off_t offset, int block, int buf_place){


  char temp_buf[4096];
  memset(&temp_buf,0,sizeof(temp_buf));

  // read in block
  if(pread(fd_disk,&temp_buf,4096,(4096*block)) == -1) return -1;

  // calculate size to read from this cluster
  int size_to_read = (int) size;
  if((size+offset) > 4096) size_to_read = (4096-offset);

  // copy from offset into buf
  for(int i = 0; i < size_to_read; i++){
    buf[buf_place+i] = temp_buf[i+offset];
  }

  buf_place += size_to_read;

  //decrement size left to read
  size = size - size_to_read;
  if(size > 0){

    if((fat[block-4] == 3000) == 1) return 0;

    // read into latest place in buf the remaining size, from the next block with no offset
    return read_helper (buf,size,0,(fat[block-4]+4), buf_place);
  }

  // if we read everything
  return 0;
}



static int fat_read(const char* path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
  // read size bytes from the given file into the buffer buf, beginning offset bytes into the file
  // returns the number of bytes transfered or 0 if offset was at or beyond the end of the file

  // separate path and name of last dir
  char *dup1 = strdup(path);
  char *file_name = basename(dup1);

  // can't read root
  if(strcmp(path,"/") == 0) return -EISDIR;
  // see if name is too long
  if((strlen(basename(path))>32) || (strlen(path) > PATH_MAX)){
    return -ENAMETOOLONG;
  }      

  dir_entry de;

  char path2[PATH_MAX];
  strcpy(path2,path);
  if(path2[0] == '/'){
    memmove(path2, path2+1, sizeof(path2)-1); //same fix as in getattr
  }

  // check if directory already exists (using parent_de, but not looking for parent directory entry, just for convenience)
  int call_rslt = dir_exists(path2, 4, &de, file_name);
  if (call_rslt == -2) return -ENAMETOOLONG;
  if (call_rslt == -1) return -ENOENT;
  if (call_rslt == -3) return -ENOTDIR;
  if (de.dir_type == 1) return -EISDIR;

  // THREE CASES: offset > de.size: read nothing return 0
  //        size+offset <= de.size: read from offset, (size) amount
  //size > de.size or size + offset > size: read from offset, (de.size-offset) amount

  // case 1: offset > de.size
  if(offset>de.file_size) return 0;

  
  // otherwise...
  else {
    // find order of the block that we should start reading from
    int b = (offset / 4096);
    int start_place;
    if (offset - (b*4096) == 0) start_place = 0;
    else {
      start_place = offset - b*4096;
      b++;
    }
    
    // set cluster num to the number of that block
    int cluster_num = de.first_cluster;
    for (int i = 1; i < b; i++){ 
      if(fat[cluster_num-4] == 3000) return 0; // dont read anything if offset out ranges size (shouldn't because of the check uptop, but oh well)
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

  char temp_buf[4096];
  memset(&temp_buf,0,sizeof(temp_buf));

  // read in block
  if(pread(fd_disk,&temp_buf,4096,(4096*block)) == -1) return -1;

  // calculate size to write to this cluster
  int size_to_write = (int) size;
  if((size+offset) > 4096) size_to_write = (4096-offset);

  // copy from offset into temp buf
  for(int i = 0; i < size_to_write; i++){
    temp_buf[i+offset] = buf[buf_place+i];
  }

  // write out block
  if(pwrite(fd_disk,&temp_buf,4096,(4096*block)) == -1) return -1;

  buf_place += size_to_write;

  //decrement size left to write
  size = size - size_to_write;
  
  if(size > 0){
    // if we ran out of space, allocate more space (the universe expands fast. so should storage)
    if(fat[block-4] == 3000){

      for(int i = 0; i < 2556; i++){
	if(fat[i] = 4000){ // if we find a free cluster

	  //set this one as new, and the previous cluster to point to this one
	  fat[block-4] = i;
	  fat[i] = 3000;
	  // write out fat
	  if(pwrite(fd_disk,&fat,(4096*3),4096) == -1) return -1;

	  // continue on
	  // write into latest place in buf the remaining size, from the next block with no offset
	  return(buf,size,0,(i+4), buf_place);
	}
	
      }
      // if there isn't any empty cluster left, return error
	return -2;

    }else{ // if there is another cluster left in the file, write into that one 
    // write into latest place in buf the remaining size, from the next block with no offset
    return(buf,size,0,(fat[block-4]+4), buf_place);
    }
  }

  // if we write everything
  return 0;
}



static int fat_write(const char* path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
  // read size bytes from the given file into the buffer buf, beginning offset bytes into the file
  // returns the number of bytes transfered or 0 if offset was at or beyond the end of the file

  // separate path and name of last dir
  char *dup1 = strdup(path);
  char *file_name = basename(dup1);

  // can't read root
  if(strcmp(path,"/") == 0) return -EISDIR;
  // see if name is too long
  if((strlen(basename(path))>32) || (strlen(path) > PATH_MAX)){
    return -ENAMETOOLONG;
  }      


  dir_entry de;

  char path2[PATH_MAX];
  strcpy(path2,path);
  if(path2[0] == '/'){
    memmove(path2, path2+1, sizeof(path2)-1); //same fix as in getattr
  }

  int call_rslt = dir_exists(path2, 4, &de, file_name);
  if (call_rslt == -2) return -ENAMETOOLONG;
  if (call_rslt == -1) return -ENOENT;
  if (call_rslt == -3) return -ENOTDIR;
  if (de.dir_type == 1) return -EISDIR;

  // find index of linked clusters that we have to write to
  int num_new_b = (offset / 4096);
  int start_place;
  if (offset - (num_new_b*4096) == 0){
    start_place = 0;
  }
  else {
    start_place = offset - (num_new_b*4096);
  }
  num_new_b++; // because we need to accomodate for the last cluster thats not full

  // find actual cluster number of the cluster that we have to write to
  int cluster_num = de.first_cluster;
  int current_block_num = (de.file_size/4096);
  int previous;
  
  if((de.file_size%4096)!=0) current_block_num++;
  for (int i = 0; i < num_new_b; i++){
    if(cluster_num == 3004){ // if we reach the EOF, and have to increase it 
      //find empty cluster 
      for(int j = 0; j < 2556; j++){
	if(fat[j] == 4000){ // if we find a free cluster
	  
	  //set this one as new, and the previous cluster to point to this one
	  fat[previous-4] = j;
	  fat[j] = 3000;
	  cluster_num = j+4;
	  // write out fat
	  if(pwrite(fd_disk,&fat,(4096*3),4096) == -1) return -EIO;
	  break;
	}
      }
      // if there isn't any empty cluster left, return error
      if(fat[previous-4] == 3000) return -ENOSPC;
      
    }

    // otherwise, set cluster_num (first cluster to be written to) to the next cluster
    previous = cluster_num;
    cluster_num = fat[cluster_num-4]+4;
  }

  cluster_num = previous;
  
  // actually write out
  call_rslt = write_helper(buf, size, start_place, cluster_num, 0);
  if(call_rslt == -1) return -EIO;
  if(call_rslt == -2) return -ENOSPC;

  
  // record new size of file in dir entry if necessary
  if((size+offset) > de.file_size){

    char *dup = strdup(path);
    char *parent_path = dirname(dup);
    dir_entry parent_de;

    if(strlen(parent_path) == 1){ //if groot
      parent_de.first_cluster = 4;
    }
    else{ 
      memset(&path2,0,sizeof(path2));
      strcpy(path2,parent_path);
      if(path2[0] == '/'){
	memmove(path2, path2+1, sizeof(path2)-1); //same fix as in getattr
      }
      
      char* dup1 = strdup(parent_path);
      char* last = basename(dup1);
      
      int call_rslt = dir_exists(path2, 4, &parent_de, last); // we know that all the errors are accounted for
    }
    
    dir_entry dir_entries[4096];
    (pread(fd_disk,&dir_entries,4096,(4096*parent_de.first_cluster)) == -1);
    
    for(int i = 2; i < (4096/sizeof(dir_entry)); i++){ // find correct dir entry and set correct file size
      if(strcmp(dir_entries[i].name,de.name) == 0) dir_entries[i].file_size = (size+offset);
    }
    
    if(pwrite(fd_disk,&dir_entries,4096,(4096*parent_de.first_cluster)) == -1);
  
  } 
  return size;
}


static int fat_truncate(const char* path, off_t size)
{

  // separate path and name of last dir
  char *dup1 = strdup(path);
  char *file_name = basename(dup1);

  // groot not file
  if(strcmp(path,"/") == 0) return -EISDIR;
  // see if name is too long
  if((strlen(basename(path))>32) || (strlen(path) > PATH_MAX)){
    return -ENAMETOOLONG;
  }      


  dir_entry de;

  char path2[PATH_MAX];
  strcpy(path2,path);
  if(path2[0] == '/'){
    memmove(path2, path2+1, sizeof(path2)-1); //same fix as in getattr
  }

  int call_rslt = dir_exists(path2, 4, &de, file_name);
  if (call_rslt == -2) return -ENAMETOOLONG;
  if (call_rslt == -1) return -ENOENT;
  if (call_rslt == -3) return -ENOTDIR;
  if (de.dir_type == 1) return -EISDIR;

  // if size is already the same, we good
  if(de.file_size == size) return 0;

  // else, we have to calculate how many blocks the size is
  else{

    // calculate to which
    int num_new_b = (size / 4096);
    int start_place;
    if (size - (num_new_b*4096) == 0){
      start_place = 0;
    }
    else {
      start_place = size - (num_new_b*4096);
    }
    num_new_b++; // because we need to accomodate for the last cluster thats not full
    
    // find actual cluster number of the cluster that we have to write to
    int cluster_num = de.first_cluster;
    int current_block_num = (de.file_size/4096);
    int previous;
    
    if((de.file_size%4096)!=0) current_block_num++;
    for (int i = 0; i < num_new_b; i++){
      if(cluster_num == 3004){ // if we reach the EOF, and have to increase it 
	//find empty cluster 
	for(int j = 0; j < 2556; j++){
	  if(fat[j] == 4000){ // if we find a free cluster
	    
	    //set this one as new, and the previous cluster to point to this one
	    fat[previous-4] = j;
	    fat[j] = 3000;
	    cluster_num = j+4;
	    // write out fat
	    if(pwrite(fd_disk,&fat,(4096*3),4096) == -1) return -EIO;
	    break;
	  }
	}
	// if there isn't any empty cluster left, return error
	if(fat[previous-4] == 3000) return -ENOSPC;
	
      }
      
      // otherwise, set cluster_num (first cluster to be written to) to the next cluster
      previous = cluster_num;
      cluster_num = fat[cluster_num-4]+4;
    }

    cluster_num = previous;
    dir_entry dir_entries[4096];


    // either we just increased the size of the file, or not, but either way cluster_num is where the last block should be
    // so, if we're decreasing, we need to free up all the clusters after it.
    if(de.file_size > size){
      
      // 0 out the clusters of the remaining
      memset(&dir_entries, 0, 4096);
      int cluster_to_erase = fat[cluster_num-4]+4;
      int temp = 0;
      // while last cluster wasn't EOF (so this is equal to 3000 (+4 for actual cluster placement not fat placement))
      while(cluster_to_erase != 3004){
	// 0 out.
	if(pwrite(fd_disk, &dir_entries, 4096, (4096*cluster_to_erase)) == -1) return -EIO;
	// getting next cluster and setting this fat entry as free
	temp = cluster_to_erase;
	cluster_to_erase = fat[cluster_to_erase-4]+4;
	fat[temp-4] = 4000;
      }
      
      // set fat entry as free
      if(pwrite(fd_disk, &fat, 3*4096, 4096) == -1) return -EIO;
      
    }

    // for both decrease and increase, we have to set the new size
    char *dup = strdup(path);
    char *parent_path = dirname(dup);
    dir_entry parent_de;

    if(strlen(parent_path) == 1){ //if groot
      parent_de.first_cluster = 4;
    }
    else{ 
      memset(&path2,0,sizeof(path2));
      strcpy(path2,parent_path);
      if(path2[0] == '/'){
	memmove(path2, path2+1, sizeof(path2)-1); //same fix as in getattr
      }
      
      char* dup1 = strdup(parent_path);
      char* last = basename(dup1);
      
      int call_rslt = dir_exists(path2, 4, &parent_de, last); // we know that all the errors are accounted for
    }
    
    (pread(fd_disk,&dir_entries,4096,(4096*parent_de.first_cluster)) == -1);
    
    for(int i = 2; i < (4096/sizeof(dir_entry)); i++){ // find correct dir entry and set correct file size
      if(strcmp(dir_entries[i].name,de.name) == 0) dir_entries[i].file_size = size;
    }
    
    if(pwrite(fd_disk,&dir_entries,4096,(4096*parent_de.first_cluster)) == -1);

    return 0;
  }


}



static struct fuse_operations fat_operations = {
  .init= fat_init,
  .getattr= fat_getattr,
  .access= fat_access,
  .readdir= fat_readdir,
  .mkdir= fat_mkdir,
  .rmdir= fat_rmdir,
  .fgetattr= fat_fgetattr,
  .mknod= fat_mknod,
  .unlink= fat_unlink,
  .open= fat_open,
  .statfs= fat_statfs,
  .create= fat_create,
  .release= fat_release,
  .read = fat_read,
  .write= fat_write,
  .truncate= fat_truncate,


  /*
    .readlink= NULL,
    .symlink= NULL,
    .rename= NULL,
    .link= NULL,
    .chmod= NULL,
    .chown= NULL,
      .utimens= NULL,
    .fsync= NULL,
#ifdef HAVE_SETXATTR
.setxattr= NULL,
.getxattr= NULL,
.listxattr= NULL,
.removexattr= NULL,
#endif
  */
};


/* 
The best test cases are made with
.-.  .-..-. .-.  .--.  _______      ,---.  ,---.       .-.   .-.  .--.        ,'|"\    .---.  ,-..-. .-..
| |/\| || | | | / /\ \|__   __|     | .-'  | .-.\       \ \_/ )/ / /\ \       | |\ \  / .-. ) |(||  \| | 
| /  \ || `-' |/ /__\ \ )| |        | `-.  | `-'/        \   (_)/ /__\ \      | | \ \ | | |(_)(_)|   | | 
|  /\  || .-. ||  __  |(_) |        | .-'  |   (          ) (   |  __  |      | |  \ \| | | | | || |\  | 
|(/  \ || | |)|| |  |)|  | |        |  `--.| |\ \         | |   | |  |)|      /(|`-' /\ `-' / | || | |)| 
(_)   \|/(  (_)|_|  (_)  `-'        /( __.'|_| \)\       /(_|   |_|  (_)     (__)`--'  )---'  `-'/(  (_) 
       (__)                        (__)        (__)     (__)                          (_)       (__)     
       ,-..-. .-.                .--.           .---. .-.  .-.  .--.           ,---.  .-.   ?????          
       |(||  \| |      |\    /| / /\ \         ( .-._)| |/\| | / /\ \ |\    /| | .-.\ |  ) ?     ?         
       (_)|   | |      |(\  / |/ /__\ \       (_) \   | /  \ |/ /__\ \|(\  / | | |-' )| /      ??         
       | || |\  |      (_)\/  ||  __  |       _  \ \  |  /\  ||  __  |(_)\/  | | |--' |/      ?         
       | || | |)|      | \  / || |  |)|      ( `-'  ) |(/  \ || |  |)|| \  / | | |    (                  
       `-'/(  (_)      | |\/| ||_|  (_)       `----'  (_)   \||_|  (_)| |\/| | /(    (_)      ?           
         (__)          '-'  '-'                                       '-'  '-'(__)                       







Also,
  _   _   U _____ u   __   __        ____                    _         _      
 |'| |'|  \| ___"|/   \ \ / /     U | __")u      ___        |"|       |"|     
/| |_| |\  |  _|"      \ V /       \|  _ \/     |_"_|     U | | u   U | | u   
U|  _  |u  | |___     U_|"|_u       | |_) |      | |       \| |/__   \| |/__  
 |_| |_|   |_____|      |_|         |____/     U/| |\u      |_____|   |_____| 
 //   \\   <<   >>  .-,//|(_       _|| \\_  .-,_|___|_,-.   //  \\    //  \\  
 (_") ("_) (__) (__)  \_) (__)     (__) (__)  \_)-' '-(_/   (_")("_)  (_")("_)                                                                                           
*/



int main(int argc, char *argv[]) {

  umask(0);
  return fuse_main(argc, argv, &fat_operations, NULL);
}
