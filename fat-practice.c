#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>




/////////////////////////////////////////////////////// SIZES //////////////////////////////////////////////////////
#define DISK_SIZE 10485759
#define BLOCK_SIZE 4096
#define CLUSTER_TOTALNUM 2560
#define ENDOFFILE 3000
#define FREE 4000
#define PATH_MAX 4096 //unix definition.
#define COM_LENGTH 4100

// errors
#define ENOTPERM 1 //operation not permitted
#define ENOENT 2 //no such file or directory
#define EACCES 13 //permission denied
#define EEXIST 17 //file already exists
#define ENOTDIR 20 //not a directory
#define EISDIR 21 //is a directory
#define ENOSPC 28 //no more space
#define EMLINK 31 //too many links
#define ENAMETOOLONG 36 //file name too long
#define ENOTEMPTY 39 //directory not empty
#define ELOOP 40 //too many links


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


int fat[3072];


int last_free = 0;

char rootdir[PATH_MAX];
char workdir[PATH_MAX];
int workdir_clusternum;





////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////// CODE ///////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////









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





static void* fat_init()
{
  // find where "disk" file should be 
  memset(disk_path, '\0', sizeof(disk_path));
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
    superblock.s.data_size = 2556;
    // superblock.s.freelist_location = 5;

    // write out initialized disk  
    create_init_disk();

  }
  
  // set root and working directories
  memset(rootdir,0,PATH_MAX);
  memset(workdir,0,PATH_MAX);
  strcpy(rootdir,"/");
  strncpy(workdir,rootdir,PATH_MAX);
  workdir_clusternum = 4;
  return NULL;
}




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
  char *mayberoot = strdup(path);

  if((strlen(basename(mayberoot))>32) || (strlen(path) > PATH_MAX)){
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
  if((strlen(basename(strdup(path)))>32) || (strlen(path) > PATH_MAX)){
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






static int fat_readdir(const char *path, dir_entry *ret, off_t offset)
{
  // see if name is too long
  if((strlen(basename(strdup(path)))>32) || (strlen(path) > PATH_MAX)){
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


  //search through to add all dir_entries in directory
  int i;
  for (i = offset; i < 4096; i++){
    if(dir_entries[i].dir_type !=0){
      strcpy(ret[i-(offset+1)].name, dir_entries[i].name);
      ret[i-(offset+1)].first_cluster = dir_entries[i].first_cluster;
      ret[i-(offset+1)].file_size = dir_entries[i].file_size;
      ret[i-(offset+1)].dir_type = dir_entries[i].dir_type;
      ret[i-(offset+1)].read_only = dir_entries[i].read_only;
    }
  }

  return 0;
}






static int fat_mkdir(const char* path, mode_t mode)
{

  // separate path and name of last dir
  char *parent_path = dirname(strdup(path));
  char *dir_name = basename(strdup(path));


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
      strcpy(de.name,basename(strdup(path)));
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
  char *parent_path = dirname(strdup(path));
  char *dir_name = basename(strdup(path));


  // can't make root
  if(strcmp(path,"/") == 0) return -ENOENT;

  // see if name is too long
  if((strlen(basename(strdup(path)))>32) || (strlen(path) > PATH_MAX)){
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






///////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////// user command interpretation ///////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////

int description_helper(char *path, char *ret){
  int i = 0;
  for(int k = 0; strstr((path+i), " -") != NULL; k++){
    
    i = strstr((path+i), " -")-(path);
    ret[k] = path[i+2];
    i = i + 4; //after the ' ', '-', description character, and ' '
    //printf("%d, %c", index, ret[k]);
  }
  //if i is 0, no descriptions
  if(i == 0) return 0;
  else{
    //else adjust path and return 
    path = &path[i]; 
    return i;
  }
}

char* nodots_helper(char *path){ 

  char ret[4096];
  char dup[4096];
  char dup2[4096];

  memset(ret,0,4096);
  memset(dup,0,4096);
  memset(dup2,0,4096);

  //9 base cases
  //just "/."
  if(strcmp(path,"/.") == 0){
    memset(path,0,strlen(path));
    path[0] = '/';
    return path;
  }
  //just "/.."
  else if(strcmp(path,"/..") == 0){
    memset(path,0,strlen(path));
    path[0] = '/';
    return path;
  }
  //starts with "/./...", or has "..././..."
  else if(strstr(path,"/./") != NULL){
    // see if it starts "/./"
    if(strcmp(strstr(path,"/./"),path) == 0){
      // just remove /.
      path = path+2;
      strcpy(ret,path);
    }    
    else{ //if "..././..."
      // get index of /./
      int index = strstr(path, "/./") - path;
      // split into two halves, without /.
      strcpy(dup,(strstr(path, "/./") + 2));
      strncpy(ret,path,index);
      // merge
      strcat(ret,dup);
    }
    return nodots_helper(ret);
  }
  //starts with "/../...", or has ".../../..."
  else if(strstr(path,"/../") != NULL){
    // see if it starts with "/../"
    if(&(strstr(path,"/../")[0]) == & path[0]){
      // just remove /..
      path = path+3;
      strcpy(ret,path);
    }     
    //if ".../../..." (because strstr always looks for the first instance, we never are looking at the second of /../../, for example
    else{ 
      // get index of /../
      int index = strstr(path, "/../") - path;
      // copy first half
      strncpy(dup2,path,index);
      // remove last directory of first half
      strcpy(ret,dirname(dup2));
      //incase we've come down to root. Then dirname(dup2) returns / -> "//nextfile/..."
      if(strcmp(ret,"/") == 0) strcpy(dup,(strstr(path, "/../") + 4));
      // copy second half without /..
      else strcpy(dup,(strstr(path, "/../") + 3));
      // merge
      strcat(ret,dup);
    }
    return nodots_helper(ret);
  }
  // seeing if path ends with "/." or "/.."
  else if(strstr(path,"/.") != NULL){
    //if path ends with /. (otherwise, it would be /./, or /.hi and etc.
    if(strcmp(strstr(path,"/."),"/.") == 0){       
      // get rid of /.
      strcpy(ret,dirname(path));
      return nodots_helper(ret);
    }
    //if path ends with /.. (otherwise, it would be /../, or /..hi and etc.
    else if(strcmp(strstr(path,"/.."),"/..") == 0){ 
      // get rid of /.. and previous directory
      strcpy(dup,dirname(path));
      strcpy(ret,dirname(dup));
      return nodots_helper(ret);
    }
  }
  // no dots
  else {
    return path;
  }
  // shouldn't reach here, but...
  return path;
}





static int i_ls(char *path){

  char descriptions[PATH_MAX];
  int num_descript = 0;

  //  if((num_descript = description_helper(path,descriptions)) == 0){
  // read in subfiles of working directory
  
  int err = 0;
  
  dir_entry dir_entries[4096];
  memset(dir_entries, 0, sizeof(dir_entry)*4096);

  
  // in working directory
  if(path == NULL || (strcmp(path,"/") == 0)){
    // see if working directory is root
    if(strcmp(workdir,rootdir) == 0){
      err = fat_readdir(rootdir, dir_entries, 0);
    }
    else {
      char *dup = strdup(workdir);
      
      err = fat_readdir(dup, dir_entries, 0);
    }
  }
  else {    // in another directory
    
    // concat the workdir with further path
    // first check if the path + workdir exceeds path name length
    if((strlen(path) + strlen(workdir)) > PATH_MAX) return -ENAMETOOLONG;
    
    char *dup1 = strdup(workdir);
    strcat(dup1,path);
    
      err = fat_readdir(dup1, dir_entries, 0);
      
  }
  
  if(err != 0) return err;
  
  // print out nonhidden subfiles of de.
    for(int i = 0; i <4096; i++){
      // if a dir entry isn't hidden (ie starts with .)
      if((dir_entries[i].dir_type != 0) && (!(dir_entries[i].name[0] == '.'))){
	printf("%s\t\t", dir_entries[i].name);
      }
    }
    printf("\n");


  // if there are descriptions, switch by case (if statements in c)
  return 0;
}






static int i_cd(char *path){

  char descriptions[PATH_MAX];
  int num_descript = 0;

  
  
  //  if((num_descript = description_helper(path,descriptions)) == 0){
  // read in subfiles of working directory
  
  // in working directory
  if((path == NULL) || (strcmp(path,"/") == 0)){
    
    //change working directory to root
    strcpy(workdir,rootdir);
    workdir_clusternum = 4;
  }
  else if(strcmp(path,".") == 0){
    // how wonderful. no work.
    return 0;
  }
  else if(strcmp(path,"..") == 0){
    // if in root, no work.
    if(strcmp(workdir,rootdir) == 0) return 0;
    // change working directory to previous directory
    strcpy(workdir,dirname(strdup(workdir)));
    
    // get working directory cluster num
    dir_entry de;
    dir_exists(workdir, 4, &de, basename(strdup(workdir)));
    workdir_clusternum = de.first_cluster;
    return 0;
  }
  else {    // in another directory


    char dup[4096];
    strcpy(dup,path);

    
    // check if path is from root
    if(path[0] == '/'){}//dont do anything
    // or if working directory is root
    else if (strcmp(workdir,rootdir) == 0){
      memset(dup, 0, strlen(dup));
      strcpy(dup, rootdir); //set beginning as /
      strcat(dup, path);
    }      
    else { // if path is from working directory, concat with workdir path
      // see if path doesn't exceed size
      if((strlen(workdir) + strlen(path)) > PATH_MAX) return -ENAMETOOLONG;
      memset(dup, 0, strlen(dup));
      strcpy(dup, workdir);
      strcat(dup,"/");
      strcat(dup,path);
    }


    int err = 0;
    err = fat_access(dup,R_OK);    //path still works without taking care of . and ..
    if(err != 0) return err;

    // take care of . and ..
    char dup2[4096];
    strcpy(dup2,dup);
    memset(dup,0,strlen(dup));
    strcpy(dup,nodots_helper(dup2));



    
    // change working directory to this one
    strcpy(workdir,dup);

    // get working directory cluster num
    dir_entry de;
    dir_exists(workdir,4,&de,basename(strdup(workdir)));
    workdir_clusternum = de.first_cluster;
  }

  // if there are descriptions, switch by case (if statements in c)
  return 0;
}




static int i_mkdir(char *path){
  
  char descriptions[PATH_MAX];
  int num_descript = 0;


  
  // no path?
  if(path == NULL) return -ENOENT;
  
  else {    // making new directory
      //check permission in parent directory
    
    int err = 0;
    // check if path is from root
    if(path[0] == '/') err = fat_mkdir(path,(S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH));
    // or if working directory is root
    else if (strcmp(workdir,rootdir) == 0) err = fat_mkdir(strcat(strdup(rootdir),path),(S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH));
    else { // if path is from working directory, concat with workdir path
      // see if path doesn't exceed size
      if((strlen(workdir) + strlen(path)) > PATH_MAX) return -ENAMETOOLONG;
      char *dup = strdup(workdir);
      strcat(dup,"/");
      strcat(dup,path);
      err = fat_mkdir(dup,(S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH));
      
    }
    if (err == -1){
      printf("A directory in the path doesn't exist.\n");
      return 0;
    }
    else if (err != 0) return err;
  }
  

  // if there are descriptions, switch by case (if statements in c)
  return 0;


}







static int i_rmdir(char *path){


  char descriptions[PATH_MAX];
  int num_descript = 0;


  
  // no path?
  if(path == NULL) return -ENOENT;
  
  else {    // making new directory
      //check permission in parent directory
    
    int err = 0;
    // check if path is from root
    if(path[0] == '/') err = fat_rmdir(path);
    // or if working directory is root
    else if (strcmp(workdir,rootdir) == 0) err = fat_rmdir(strcat(strdup(rootdir),path));
    else { // if path is from working directory, concat with workdir path
      // see if path doesn't exceed size
      if((strlen(workdir) + strlen(path)) > PATH_MAX) return -ENAMETOOLONG;
      char *dup = strdup(workdir);
      strcat(dup,"/");
      strcat(dup,path);
      err = fat_rmdir(dup);
      
    }
    if (err != 0) return err;
  }
  

  // if there are descriptions, switch by case (if statements in c)
  return 0;

}







////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////  MAIN MAIN MAIN MAIN ///////////////////////////////////////////////////// 
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



int main(){

  // initialize
  fat_init();

  // tell user how to use
  printf("\nFAT Filesystem (c)2020  Jae Surh: \nVirtual simulation of a filesystem. Memory stored on a file called 'fat_disk'.\nSupports Unix commands. Place the path after the command and descriptions, if any.\nType 'quit' then enter, to quit.\n\n");



  //prepare for command
  char command[COM_LENGTH];

  int err = 0;

  // continuously get commands, until quit.
  fgets(command, COM_LENGTH, stdin);
  while (strcmp(command, "quit\n") != 0) {
    
    // get rid of \n at the end of the string
    command[(strlen(command)-1)] = 0;


    // if command has no path or description
    if(strchr(command, ' ') == NULL){
      
      if(strcmp(command,"ls") == 0){
	err = i_ls(NULL);
      }
      else if(strcmp(command,"cd") == 0){
	err = i_cd(NULL);
      }
      else if(strcmp(command,"mkdir") == 0){
	printf("Usage: mkdir [-m mode] directory");
      }
      else if(strcmp(command,"rmdir") == 0){
	printf("Usage: rmdir [-m mode] directory");
      }
      else if(strlen(command) == 0){
	// do you wanna build a snow man-
	// okay bye
      }
      else{
	printf("Error: Command not recognized\n");
      }
    }
    else{

      //split into command and rest
      char mand[COM_LENGTH];
      memset(mand,0,COM_LENGTH);
      strcpy(mand,strchr(command, ' ')+1);

      int index = strchr(command, ' ') - command;
      char com[COM_LENGTH];
      memset(com,0,COM_LENGTH);
      strncpy(com,command,index);


      if(strcmp(com,"ls") == 0){
	err = i_ls(mand);
      }
      else if(strcmp(com,"cd") == 0){
	err = i_cd(mand);
      }
      else if(strcmp(com,"more") == 0){
	
      }
      else if(strcmp(com,"less") == 0){
	
      }
      else if(strcmp(com,"mkdir") == 0){
	err = i_mkdir(mand);
      }
      else if(strcmp(com,"rmdir") == 0){
	err = i_rmdir(mand);
      }
      else if(strcmp(com,"echo") == 0){
	
      }
      else {
	printf("Error: Command not recognized.\n");
      }
    
    }


    ///////////////////// ERRORS!!! ///////////////////////
    if(err == -ENOTPERM) printf("Operation not permitted.\n");
    if(err == -ENOENT) printf("File doesn't exist.\n");
    if(err == -EACCES) printf("Permission denied.\n");
    if(err == -EEXIST) printf("File already exists.\n");
    if(err == -ENOTDIR) printf("Not a directory.\n");
    if(err == -EISDIR) printf("Is an existing directory.\n");
    if(err == -ENOSPC) printf("No more space.\n");
    if(err == -EMLINK) printf("Too many links.\n");
    if(err == -ENAMETOOLONG) printf("File or path name too long.\n");
    if(err == -ENOTEMPTY) printf("Directory not empty.\n");
    if(err == -ELOOP) printf("Too many links.\n");

    printf("%s user: ", workdir);
    // next command. bring it on!
    memset(command, 0, COM_LENGTH);
    fgets(command, COM_LENGTH, stdin);

  }
}




